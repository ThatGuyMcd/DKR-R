#include "vehicle_library.hpp"
#include "magic_code_package.hpp"
#include "online_mod_bundle.hpp"
#include <algorithm>
#include <random>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dkr::mods::vehicles {
namespace {
constexpr unsigned MaxInstalled = 512;
constexpr std::size_t MaxIndex = 2 * MiB, MaxFrozenSource = 64 * MiB, MaxFrozenDecoded = 128 * MiB;
[[noreturn]] void bad(const std::string& s) {
    throw Error("Vehicle library: " + s);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        bad("operation cancelled.");
}
std::string nonce() {
    std::random_device random;
    Bytes bytes(16);
    for (auto& b : bytes)
        b = std::uint8_t(random());
    return sha256(bytes);
}
struct Lock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    explicit Lock(const std::filesystem::path& root) {
        auto ancestor = root;
        while (!std::filesystem::exists(ancestor)) {
            const auto parent = ancestor.parent_path();
            if (parent.empty() || parent == ancestor)
                bad("storage is unavailable.");
            ancestor = parent;
        }
        online::check_storage(ancestor, true);
        std::filesystem::create_directories(root);
        online::check_storage(root, true);
        const auto path = private_storage_path(root / "library.lock");
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            bad("the library is busy or not writable.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            bad("unsafe library lock.");
        }
#else
        handle = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
            bad("the library is not writable.");
        struct stat info {};
        if (fstat(handle, &info) || !S_ISREG(info.st_mode) || flock(handle, LOCK_EX | LOCK_NB)) {
            close(handle);
            handle = -1;
            bad("the library is busy or unsafe.");
        }
#endif
    }
    ~Lock() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (handle >= 0)
            close(handle);
#endif
    }
};
void sync_directory(const std::filesystem::path& path) {
#ifndef _WIN32
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        bad("could not open storage for its durability checkpoint.");
    const auto result = fsync(fd);
    const auto failure = errno;
    close(fd);
    if (result && failure != EINVAL && failure != ENOTSUP)
        bad("could not confirm storage durability.");
#else
    (void)path;
#endif
}
Json index(const std::filesystem::path& root) {
    const auto path = root / "library.json";
    if (!std::filesystem::exists(path))
        return {{"schema", 1}, {"items", Json::object()}};
    online::check_storage(path, false);
    auto doc = code::parse_document(read_file(path, MaxIndex), MaxIndex);
    if (!doc.is_object() || doc.size() != 2 || doc.value("schema", 0) != 1 || !doc.contains("items") ||
        !doc["items"].is_object() || doc["items"].size() > MaxInstalled)
        bad("invalid catalogue. Existing packages were not changed.");
    for (const auto& [hash, item] : doc["items"].items()) {
        if (!valid_digest(hash) || !item.is_object() || item.size() != 7 || !item.contains("type") ||
            !item["type"].is_number_unsigned() || item["type"].get<unsigned>() > 2 || !item.contains("enabled") ||
            !item["enabled"].is_boolean())
            bad("invalid catalogue entry.");
        for (const auto* field : {"name", "source"}) {
            if (!item.contains(field) || !item[field].is_string())
                bad("invalid display metadata.");
            const auto& s = item[field].get_ref<const std::string&>();
            if (s.empty() || s.size() > 240 ||
                std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                bad("invalid display name.");
        }
        for (const auto* field : {"width", "height"})
            if (!item.contains(field) || !item[field].is_number_unsigned() || !item[field].get<unsigned>() ||
                item[field].get<unsigned>() > 512)
                bad("invalid portrait dimensions.");
        if (!item.contains("portrait") || !item["portrait"].is_string() ||
            !valid_digest(item["portrait"].get<std::string>()))
            bad("invalid portrait identity.");
    }
    return doc;
}
void publish(const std::filesystem::path& root, const Json& doc) {
    const auto path = root / "library.json";
    const auto value = doc.dump();
    if (value.size() > MaxIndex)
        bad("the catalogue is too large.");
    const auto history = root / "history";
    std::filesystem::create_directory(history);
    online::check_storage(history, true);
    if (std::filesystem::exists(path)) {
        online::check_storage(path, false);
        const auto old = read_file(path, MaxIndex);
        const auto backup = history / (sha256(old) + ".json");
        if (!std::filesystem::exists(backup))
            write_new_file(backup, old);
        else {
            online::check_storage(backup, false);
            if (read_file(backup, MaxIndex) != old)
                bad("a catalogue backup failed verification.");
        }
        sync_directory(history);
    }
    const auto temporary = root / ("catalog-" + nonce() + ".tmp");
    write_new_file(temporary, View(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    try {
#ifdef _WIN32
        if (!MoveFileExW(private_storage_path(temporary).c_str(), private_storage_path(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            bad("could not commit the catalogue.");
#else
        std::filesystem::rename(temporary, path);
        sync_directory(root);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
std::shared_ptr<const Package> load(const std::filesystem::path& root, const Json& doc, std::string_view hash) {
    if (!valid_digest(hash) || !doc["items"].contains(std::string(hash)))
        bad("this vehicle is not installed.");
    const auto& item = doc["items"][std::string(hash)];
    const auto type = Type(item.at("type").get<unsigned>());
    const auto path = root / "packages" / (std::string(hash) + extension(type));
    online::check_storage(path, false);
    const auto bytes = read_file(path, MaxPackageBytes);
    if (sha256(bytes) != hash)
        bad("an installed package failed verification. Import it again.");
    auto p = inspect(bytes, item.at("source").get<std::string>(), type);
    if (p->name != item.at("name").get<std::string>())
        bad("the installed name does not match its package.");
    return p;
}
std::string install(const std::filesystem::path& root, Json& doc, const Package& p, bool enable) {
    auto& items = doc["items"];
    // Identical inner exports (including a ZIP and its standalone .dkrkart)
    // have one identity. Reimport never duplicates or silently enables one.
    if (items.contains(p.digest)) {
        (void)load(root, doc, p.digest);
        return p.digest;
    }
    if (items.size() >= MaxInstalled)
        bad("the installed vehicle limit has been reached.");
    if (enable && std::count_if(items.begin(), items.end(),
                                [](const auto& i) { return i.at("enabled").template get<bool>(); }) >= MaxEnabled)
        enable = false;
    const auto packages = root / "packages", portraits = root / "portraits";
    std::filesystem::create_directory(packages);
    std::filesystem::create_directory(portraits);
    online::check_storage(packages, true);
    online::check_storage(portraits, true);
    const auto source = packages / (p.digest + extension(p.type)), portrait = portraits / (p.digest + ".rgba");
    Image preview = p.portrait;
    if (preview.width > 128 || preview.height > 128) {
        const float factor = std::min(128.F / preview.width, 128.F / preview.height);
        Image small{
            std::max(1U, unsigned(preview.width * factor)), std::max(1U, unsigned(preview.height * factor)), {}};
        small.rgba.resize(std::size_t(small.width) * small.height * 4);
        for (unsigned y = 0; y < small.height; ++y)
            for (unsigned x = 0; x < small.width; ++x) {
                const auto sx = std::min(preview.width - 1, unsigned(x / factor)),
                           sy = std::min(preview.height - 1, unsigned(y / factor));
                std::copy_n(preview.rgba.begin() + (sy * preview.width + sx) * 4, 4,
                            small.rgba.begin() + (y * small.width + x) * 4);
            }
        preview = std::move(small);
    }
    for (const auto& pair : {std::pair{source, View(p.source)}, std::pair{portrait, View(preview.rgba)}}) {
        if (!std::filesystem::exists(pair.first))
            write_new_file(pair.first, pair.second);
        else {
            online::check_storage(pair.first, false);
            if (read_file(pair.first, MaxPackageBytes) != Bytes(pair.second.begin(), pair.second.end()))
                bad("an existing content-addressed file failed verification.");
        }
    }
    sync_directory(packages);
    sync_directory(portraits);
    items[p.digest] = {{"type", unsigned(p.type)},        {"enabled", enable},      {"name", p.name},
                       {"source", p.source_name},         {"width", preview.width}, {"height", preview.height},
                       {"portrait", sha256(preview.rgba)}};
    return p.digest;
}
std::string name_key(std::string name) {
    // A name is only a POTENTIAL duplicate, never a package identity.
    for (auto& c : name)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return name;
}
std::vector<ImportConflict> conflicts(const Json& doc, const std::vector<std::shared_ptr<const Package>>& packages) {
    std::vector<ImportConflict> out;
    for (const auto& p : packages) {
        if (doc["items"].contains(p->digest))
            continue;
        ImportConflict row{p->digest, p->name, {}};
        for (const auto& [hash, item] : doc["items"].items())
            if (item["type"].get<unsigned>() == unsigned(p->type) &&
                name_key(item["name"].get<std::string>()) == name_key(p->name))
                row.installed.push_back(hash);
        if (!row.installed.empty())
            out.push_back(std::move(row));
    }
    return out;
}
} // namespace
std::vector<Item> Library::scan(std::stop_token stop) const {
    if (!std::filesystem::exists(root_))
        return {};
    Lock lock(root_);
    const auto doc = index(root_);
    std::vector<Item> out;
    for (const auto& [hash, e] : doc["items"].items()) {
        cancelled(stop);
        Item item;
        item.digest = hash;
        item.id = "vehicle:" + hash;
        item.name = e.at("name").get<std::string>();
        item.source_name = e.at("source").get<std::string>();
        item.enabled = e.at("enabled").get<bool>();
        item.type = Type(e.at("type").get<unsigned>());
        item.portrait.width = e.at("width").get<unsigned>();
        item.portrait.height = e.at("height").get<unsigned>();
        const auto path = root_ / "portraits" / (hash + ".rgba");
        online::check_storage(path, false);
        item.portrait.rgba = read_file(path, 512 * 512 * 4);
        if (item.portrait.rgba.size() != std::size_t(item.portrait.width) * item.portrait.height * 4 ||
            sha256(item.portrait.rgba) != e.at("portrait").get<std::string>())
            bad("an installed portrait failed verification. Import the vehicle again.");
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return std::pair{a.name, a.digest} < std::pair{b.name, b.digest}; });
    return out;
}
std::vector<std::string> Library::import_file(const std::filesystem::path& path) {
    return import_prepared(prepare_import(path), ImportPolicy::RejectDuplicates);
}
ImportBatch Library::prepare_import(const std::filesystem::path& path) const {
    const auto inputs = read_inputs(path);
    std::vector<std::shared_ptr<const Package>> prepared;
    // Validate the entire user-selected bundle before changing activation.
    std::set<std::pair<unsigned, std::string>> names;
    for (const auto& input : inputs) {
        auto p = inspect(input.bytes, input.name, input.type);
        if (!names.insert({unsigned(p->type), name_key(p->name)}).second)
            bad("the bundle contains different cars with the same name. Import them separately.");
        prepared.push_back(std::move(p));
    }
    Lock lock(root_);
    const auto doc = index(root_);
    auto matches = conflicts(doc, prepared);
    return {std::move(prepared), std::move(matches)};
}
std::vector<std::string> Library::import_prepared(const ImportBatch& batch, ImportPolicy policy) {
    if (batch.packages.empty() || batch.packages.size() > 16)
        bad("invalid prepared import.");
    Lock lock(root_);
    auto doc = index(root_);
    std::vector<std::string> hashes;
    const auto matches = conflicts(doc, batch.packages);
    if (policy == ImportPolicy::RejectDuplicates && !matches.empty())
        bad("a car with this name is installed. Choose Replace older copies or Keep both.");
    if (policy == ImportPolicy::ReplaceSameName && matches != batch.conflicts)
        bad("the library changed while you were choosing. Import the bundle again.");
    std::map<std::string, bool> replacement_enabled;
    if (policy == ImportPolicy::ReplaceSameName)
        for (const auto& row : matches) {
            bool enabled = false;
            for (const auto& hash : row.installed) {
                enabled |= doc["items"].at(hash).at("enabled").get<bool>();
                doc["items"].erase(hash); // Old immutable blobs and catalogue history remain recoverable.
            }
            replacement_enabled.emplace(row.incoming, enabled);
        }
    for (const auto& p : batch.packages) {
        const auto prior = replacement_enabled.find(p->digest);
        hashes.push_back(install(root_, doc, *p, prior == replacement_enabled.end() ? true : prior->second));
    }
    publish(root_, doc);
    return hashes;
}
std::string Library::import(Input input, bool enable) {
    const auto p = inspect(input.bytes, std::move(input.name), input.type);
    Lock lock(root_);
    auto doc = index(root_);
    auto hash = install(root_, doc, *p, enable);
    publish(root_, doc);
    return hash;
}
void Library::enable(std::string_view hash, bool enabled) {
    if (!valid_digest(hash))
        bad("invalid vehicle identity.");
    Lock lock(root_);
    auto doc = index(root_);
    auto& items = doc["items"];
    if (!items.contains(std::string(hash)))
        bad("this vehicle is not installed.");
    if (enabled) {
        (void)load(root_, doc, hash);
        const auto count = std::count_if(items.begin(), items.end(),
                                         [](const auto& i) { return i.at("enabled").template get<bool>(); });
        if (!items[std::string(hash)].at("enabled").get<bool>() && count >= MaxEnabled)
            bad("enable up to 16 cars for one session.");
    }
    items[std::string(hash)]["enabled"] = enabled;
    publish(root_, doc);
}
void Library::remove(std::string_view hash) {
    if (!valid_digest(hash))
        bad("invalid vehicle identity.");
    Lock lock(root_);
    auto doc = index(root_);
    if (!doc["items"].erase(std::string(hash)))
        bad("this vehicle is not installed.");
    // Publish first. Keep immutable blobs for recovery and an admitted match;
    // never delete the user's download or another category's files.
    publish(root_, doc);
}
std::shared_ptr<const Package> Library::package(std::string_view hash) const {
    Lock lock(root_);
    return load(root_, index(root_), hash);
}
Frozen freeze_packages(std::vector<std::shared_ptr<const Package>> packages) {
    if (packages.size() > MaxEnabled)
        bad("too many enabled cars.");
    std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) { return a->digest < b->digest; });
    std::string canonical = "dkr-vehicles-v2:fixed-reference-seats:cars";
    std::string previous;
    std::size_t source = 0, decoded = 0;
    for (const auto& p : packages) {
        if (!p || !supported(p->type) || !valid_digest(p->digest) || p->digest == previous)
            bad("invalid or duplicate frozen vehicle.");
        previous = p->digest;
        if (p->source.size() > MaxFrozenSource - source)
            bad("enabled car packages exceed the session source budget.");
        source += p->source.size();
        std::size_t size = p->portrait.rgba.size();
        for (const auto& t : p->textures)
            size += t.image.rgba.size();
        for (const auto& lod : p->lods)
            for (const auto& m : lod.meshes)
                size += m.vertices.size() * sizeof(Vec3) + m.uv.size() * sizeof(Vec2) +
                        m.faces.size() * sizeof(std::array<unsigned, 3>);
        if (size > MaxFrozenDecoded - decoded)
            bad("enabled cars exceed the session model budget.");
        decoded += size;
        canonical += ':' + p->digest;
    }
    return {std::move(packages),
            sha256(View(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()))};
}
Frozen Library::freeze(std::stop_token stop) const {
    if (!std::filesystem::exists(root_))
        return freeze_packages({});
    Lock lock(root_);
    const auto doc = index(root_);
    std::vector<std::shared_ptr<const Package>> out;
    for (const auto& [hash, item] : doc["items"].items()) {
        cancelled(stop);
        if (item.at("enabled").get<bool>())
            out.push_back(load(root_, doc, hash));
    }
    return freeze_packages(std::move(out));
}
} // namespace dkr::mods::vehicles
