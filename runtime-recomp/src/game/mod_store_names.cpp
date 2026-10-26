#include "common/rt64_filesystem.h"
#include "common/rt64_filesystem_zip.h"
#include "mod_store.hpp"
#include "mod_store_install.hpp"
#include "runtime_texture_packs.hpp"
#include "updates/update_core.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

namespace dkr::runtime::rice_texture {
bool matches_converted_archive(RT64::FileSystem&, const std::filesystem::path&, std::stop_token);
}

namespace dkr::store {
namespace {
struct Names {
    std::map<std::string, std::string> mods, textures;
    bool operator==(const Names&) const = default;
};
std::mutex names_mutex;
Names names;
std::atomic<std::uint64_t> names_generation{1};
std::string utf8(const std::filesystem::path& p) {
    const auto s = p.lexically_normal().generic_u8string();
    return {s.begin(), s.end()};
}
std::string folded(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
bool digest(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == s.npos;
}
Json read(const std::filesystem::path& path) {
    try {
        if (std::filesystem::is_symlink(path))
            return Json::object();
        return Json::parse(dkr::updates::read_text(path, 4 * 1024 * 1024));
    } catch (...) {
        return Json::object();
    }
}
std::string stamp(const std::filesystem::path& path, std::stop_token stop) {
    // Cheap invalidation of a display-only association. No cached name is an
    // asset proof. Actual matching below compares every decoded texture pixel.
    std::map<std::string, std::string> entries;
    if (std::filesystem::is_symlink(path))
        return {};
    if (std::filesystem::is_regular_file(path)) {
        entries["file"] = std::to_string(std::filesystem::file_size(path)) + ":" +
                          std::to_string(static_cast<std::int64_t>(
                              std::filesystem::last_write_time(path).time_since_epoch().count()));
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (stop.stop_requested() || entries.size() >= 200005 || entry.is_symlink())
                return {};
            if (!entry.is_regular_file())
                return {};
            entries[utf8(entry.path().filename())] =
                std::to_string(entry.file_size()) + ":" +
                std::to_string(static_cast<std::int64_t>(entry.last_write_time().time_since_epoch().count()));
        }
    }
    const auto value = Json(entries).dump();
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}
void publish(Names value) {
    std::lock_guard lock(names_mutex);
    if (value == names)
        return;
    names = std::move(value);
    ++names_generation;
}
} // namespace

bool generic_package_name(const std::string& text) {
    const auto value = folded(text);
    if (value == "package")
        return true;
    return value.starts_with("package-") && value.size() > 8 && value.find_first_not_of("0123456789", 8) == value.npos;
}

std::string package_filename(const std::string& title, const std::string& published) {
    const auto extension = folded(std::filesystem::u8path(published).extension().string());
    std::string stem;
    for (unsigned char c : title) {
        if (c < 32 || c == 127 || std::string_view("\\/:*?\"<>|").find(char(c)) != std::string_view::npos)
            stem += '_';
        else
            stem += char(c);
    }
    if (stem.size() > 112) {
        std::size_t end = 112;
        while (end && (static_cast<unsigned char>(stem[end]) & 0xC0) == 0x80)
            --end;
        stem.resize(end);
    }
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
        stem.pop_back();
    while (!stem.empty() && stem.front() == ' ')
        stem.erase(stem.begin());
    if (stem.empty() || generic_package_name(stem))
        stem = std::filesystem::u8path(published).stem().string();
    if (stem.empty() || stem == "." || stem == "..")
        stem = "Downloaded mod";
    const auto device = folded(stem.substr(0, stem.find('.')));
    if (device == "con" || device == "prn" || device == "aux" || device == "nul" ||
        (device.size() == 4 && (device.starts_with("com") || device.starts_with("lpt")) && device[3] >= '0' &&
         device[3] <= '9'))
        stem = "_" + stem;
    return stem + extension;
}

std::uint64_t library_names_generation() {
    return names_generation.load();
}
std::string library_display_name(const std::string& hash, const std::string& category, const std::string& fallback) {
    if (!generic_package_name(fallback))
        return fallback;
    std::lock_guard lock(names_mutex);
    const auto found = names.mods.find(category + ":" + hash);
    return found == names.mods.end() ? fallback : found->second;
}
std::string texture_display_name(const std::filesystem::path& path, const std::string& fallback) {
    std::lock_guard lock(names_mutex);
    const auto found = names.textures.find(utf8(path));
    return found == names.textures.end() ? fallback : found->second;
}

void refresh_library_names(const std::filesystem::path& config, const Json& items, std::stop_token stop) {
    if (!items.is_array())
        return;
    Names next;
    const auto root = config / "mod-store";
    const auto saved = read(root / "installed-names.json");
    Json records = Json::object();
    struct Candidate {
        std::string title, file, hash;
    };
    std::map<std::string, Candidate> candidates;
    std::map<std::string, std::filesystem::path> verified_sources;
    std::set<std::string> ambiguous;
    for (const auto& item : items) {
        const auto title = text(item, "title", 200), category = text(item, "category", 32);
        if (title.empty() || !item.contains("releases") || !item["releases"].is_array())
            continue;
        for (const auto& release : item["releases"]) {
            const auto hash = text(release, "sha256", 64), file = text(release, "fileName", 180);
            if (!digest(hash) || file.empty() || file.find_first_of("\\/:*?\"<>|") != file.npos)
                continue;
            const auto key = category + ":" + hash;
            auto [entry, added] = next.mods.emplace(key, title);
            if (!added && entry->second != title)
                ambiguous.insert(key);
            if (category == "textures")
                candidates.emplace(hash, Candidate{title, file, hash});
        }
    }
    for (const auto& key : ambiguous) {
        next.mods.erase(key);
        if (key.starts_with("textures:"))
            candidates.erase(key.substr(9));
    }
    // Keep current texture labels visible during the background audit; the
    // final publication drops associations whose installed data has changed.
    {
        Names first = next;
        {
            std::lock_guard lock(names_mutex);
            first.textures = names.textures;
        }
        publish(std::move(first));
    }
    try {
        for (const auto& pack : dkr::runtime::texture_packs::snapshot(true)) {
            if (stop.stop_requested())
                return;
            if (pack.origin != dkr::runtime::texture_packs::Origin::User || !pack.compatible)
                continue;
            const auto path = pack.path.lexically_normal();
            if (path.parent_path() != (config / "texture-packs").lexically_normal())
                continue;
            const auto key = utf8(path);
            const bool have_saved =
                saved.contains("textures") && saved["textures"].is_object() && saved["textures"].contains(key);
            const auto path_stem = utf8(path.stem());
            bool relevant = have_saved || generic_package_name(path_stem);
            for (const auto& [sha, candidate] : candidates) {
                const auto label =
                    utf8(std::filesystem::u8path(package_filename(candidate.title, candidate.file)).stem());
                relevant |= path_stem == label || path_stem.starts_with(label + "-");
            }
            if (!relevant)
                continue; // Never walk unrelated installed packs.
            const auto proof = stamp(path, stop);
            if (proof.empty())
                continue;
            if (have_saved) {
                const auto& row = saved["textures"][key];
                const auto title = text(row, "title", 200);
                if (text(row, "stamp", 64) == proof && digest(text(row, "sha256", 64)) && !title.empty()) {
                    next.textures[key] = title;
                    records[key] = row;
                    continue;
                }
            }
            std::string matched, hash;
            for (const auto& [sha, candidate] : candidates) {
                if (stop.stop_requested())
                    return;
                const auto label = std::filesystem::u8path(package_filename(candidate.title, candidate.file)).stem();
                if (!generic_package_name(path_stem) && path_stem != utf8(label) &&
                    !path_stem.starts_with(utf8(label) + "-"))
                    continue;
                const auto directory = root / "downloads" / sha;
                if (std::filesystem::is_symlink(directory))
                    continue;
                std::filesystem::path source;
                if (verified_sources.contains(sha))
                    source = verified_sources.at(sha);
                else
                    for (const auto& name : {package_filename(candidate.title, candidate.file), candidate.file,
                                             "package" + std::filesystem::path(candidate.file).extension().string()}) {
                        const auto file = directory / std::filesystem::u8path(name);
                        if (!std::filesystem::is_symlink(file) && std::filesystem::is_regular_file(file)) {
                            source = file;
                            break;
                        }
                    }
                if (source.empty())
                    continue;
                const auto canonical =
                    directory / std::filesystem::u8path(package_filename(candidate.title, candidate.file));
                if (!verified_sources.contains(sha)) {
                    if (!recover_cached_package(directory, canonical, sha, std::filesystem::file_size(source), stop))
                        continue;
                    source = canonical;
                }
                if (!verified_sources.contains(sha) &&
                    dkr::updates::hash_file(source, 2ULL * 1024 * 1024 * 1024) != sha)
                    continue;
                verified_sources.emplace(sha, source);
                bool same = false;
                if (std::filesystem::is_regular_file(path))
                    same = dkr::updates::hash_file(path, 4ULL * 1024 * 1024 * 1024) == sha;
                else {
                    auto archive = RT64::FileSystemZip::create(source, "");
                    same = archive && dkr::runtime::rice_texture::matches_converted_archive(*archive, path, stop);
                }
                if (!same)
                    continue;
                if (!matched.empty() && matched != candidate.title) {
                    matched.clear();
                    break;
                }
                matched = candidate.title;
                hash = sha;
            }
            if (!matched.empty()) {
                next.textures[key] = matched;
                records[key] = {{"title", matched}, {"sha256", hash}, {"stamp", proof}};
            }
        }
        if (stop.stop_requested())
            return;
        dkr::updates::durable_write(root / "installed-names.json", Json{{"schema", 1}, {"textures", records}}.dump());
    } catch (...) { /* Cosmetic repair cannot break the library or sign-in. */
    }
    publish(std::move(next));
}
} // namespace dkr::store
