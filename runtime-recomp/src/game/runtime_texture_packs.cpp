#include "runtime_texture_packs.hpp"
#include "startup_performance.hpp"

#include "rice_texture_pack_policy.hpp"
#include "runtime_rice_texture_import.hpp"

#include "common/rt64_filesystem.h"
#include "common/rt64_filesystem_zip.h"
#include "hle/rt64_application.h"
#include "render/rt64_texture_cache.h"

#include <json/json.hpp>
#include <miniz/miniz.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <system_error>
#include <utility>

namespace {

std::mutex g_mutex;
std::filesystem::path g_pack_directory;
std::filesystem::path g_settings_path;
std::filesystem::path g_index_path;
std::vector<dkr::runtime::texture_packs::PackInfo> g_packs;
std::set<std::string> g_enabled_ids;
std::string g_last_selected_id;
std::set<std::string> g_hidden_ids;
std::map<std::string, std::int64_t> g_imported_at;
std::map<std::string, std::uintmax_t> g_managed_sizes;

// pack id -> the custom track it shipped with, and the digest both carry.
// Persisted in texture-packs.ini so the browser keeps filtering these out of
// its default list across restarts.
struct TrackPackOwnerRecord {
    std::string track_id;
    std::string digest;
    std::string fingerprint;   // ArchiveFingerprint of the imported archive
};
std::map<std::string, TrackPackOwnerRecord> g_track_pack_owners;
std::set<std::string> g_applied_ids;
std::string g_status;
std::uint64_t g_generation = 1;
std::uint64_t g_applied_generation = 0;
bool g_applied_modern = false;
std::vector<RT64::ReplacementDirectory> g_applied_replacements;
// request_reload() wants RT64 to reload even an unchanged set.
bool g_force_replacement_reload = false;
// Held while an import is mid-flight, so the set it leaves behind reaches RT64
// as one reload instead of one per intermediate step (the stale pack gone and
// the new one not yet scanned, then the new one in).
std::atomic<int> g_import_apply_hold{0};

struct CachedPackInfo {
    std::string fingerprint;
    dkr::runtime::texture_packs::PackInfo info;
};

std::map<std::string, CachedPackInfo> g_pack_index;
std::atomic<bool> g_background_refresh_started{false};
std::mutex g_refresh_mutex;

struct PendingDeletion {
    std::string id;
    std::filesystem::path path;
    std::string name;
};

std::vector<PendingDeletion> g_pending_deletions;
// Declared after every object used by the worker so its destructor requests a
// stop and joins before those dependencies begin static destruction.
std::jthread g_background_refresh;

bool ReportImportProgress(
    const dkr::runtime::texture_packs::ImportProgressCallback& callback,
    float fraction, std::string stage, bool commit_started = false) {
    if (!callback) return true;
    return callback({std::clamp(fraction, 0.0F, 1.0F), std::move(stage),
                     commit_started});
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::string StableId(const std::filesystem::path& path) {
    return Lower(path.filename().string());
}

std::string DisplayName(const std::filesystem::path& path) {
    std::string result = path.stem().string();
    std::replace(result.begin(), result.end(), '_', ' ');
    return result.empty() ? path.filename().string() : result;
}

std::string FileTimeText(std::filesystem::file_time_type time) {
#if defined(__ANDROID__)
    // libc++ uses a 128-bit timestamp representation on Android. Preserve
    // every bit in the cache key rather than narrowing it to std::to_string.
    const auto ticks = time.time_since_epoch().count();
    const bool negative = ticks < 0;
    auto magnitude = static_cast<unsigned __int128>(ticks);
    if (negative) magnitude = 0 - magnitude;
    std::string text;
    do { text.push_back(static_cast<char>('0' + magnitude % 10)); magnitude /= 10; } while (magnitude);
    if (negative) text.push_back('-');
    std::reverse(text.begin(), text.end());
    return text;
#else
    return std::to_string(time.time_since_epoch().count());
#endif
}

std::string FileSignature(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return "missing";
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return "missing";
    return std::to_string(size) + ":" +
        FileTimeText(modified);
}

std::string PackFingerprint(const std::filesystem::path& path,
                            bool directory) {
    if (!directory) return "file:" + FileSignature(path);
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    const std::string directory_time = error
        ? "missing" : FileTimeText(modified);
    return "directory:" + directory_time + ":database:" +
        FileSignature(path / "rt64.json") + ":report:" +
        FileSignature(path / "dkr-r-rice-import.json");
}

std::uintmax_t ManagedSize(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error)) {
        const auto size = std::filesystem::file_size(path, error);
        return error ? 0U : size;
    }
    error.clear();
    if (!std::filesystem::is_directory(path, error)) return 0U;

    std::uintmax_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            const auto size = iterator->file_size(error);
            if (!error) total += size;
        }
        error.clear();
        iterator.increment(error);
    }
    return total;
}

std::int64_t ManagedTimestamp(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error) return 0;
    const auto system_time = std::chrono::time_point_cast<std::chrono::seconds>(
        written - decltype(written)::clock::now() +
        std::chrono::system_clock::now());
    return system_time.time_since_epoch().count();
}

std::int64_t CurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool IsImage(const std::string& entry) {
    const std::string extension = Lower(
        std::filesystem::path(entry).extension().string());
    return extension == ".png" || extension == ".dds" ||
           extension == ".jpg" || extension == ".jpeg";
}

bool LooksLikeRiceName(const std::string& entry) {
    const std::string lower = Lower(entry);
    return lower.find("#0#") != std::string::npos ||
           lower.find("_rgb") != std::string::npos ||
           lower.find("_all") != std::string::npos ||
           lower.find("_ci") != std::string::npos;
}

void LoadSettingsLocked() {
    g_enabled_ids.clear();
    g_hidden_ids.clear();
    g_imported_at.clear();
    g_managed_sizes.clear();
    g_track_pack_owners.clear();
    g_last_selected_id.clear();
    std::ifstream input(g_settings_path);
    std::string line;
    while (std::getline(input, line)) {
        constexpr const char* enabled_prefix = "enabled=";
        constexpr const char* hidden_prefix = "hidden=";
        constexpr const char* selected_prefix = "last_selected=";
        constexpr const char* imported_prefix = "imported_at=";
        constexpr const char* size_prefix = "managed_size=";
        constexpr const char* track_pack_prefix = "track_pack=";
        if (line.rfind(track_pack_prefix, 0) == 0 && line.size() > 11) {
            // track_pack=<id>\t<track_id>\t<digest>[\t<fingerprint>]
            const std::string value = line.substr(11);
            const auto first = value.find('\t');
            if (first == std::string::npos || first == 0U) {
                continue;
            }
            const auto second = value.find('\t', first + 1U);
            const std::string id = Lower(value.substr(0, first));
            const std::string track_id = second == std::string::npos
                ? value.substr(first + 1U)
                : value.substr(first + 1U, second - first - 1U);
            std::string digest;
            std::string fingerprint;
            if (second != std::string::npos) {
                const auto third = value.find('\t', second + 1U);
                digest = third == std::string::npos
                    ? value.substr(second + 1U)
                    : value.substr(second + 1U, third - second - 1U);
                if (third != std::string::npos) {
                    fingerprint = value.substr(third + 1U);
                }
            }
            if (!track_id.empty()) {
                g_track_pack_owners[id] = {track_id, digest, fingerprint};
            }
            continue;
        }
        if (line.rfind(enabled_prefix, 0) == 0 && line.size() > 8) {
            g_enabled_ids.insert(Lower(line.substr(8)));
        } else if (line.rfind(hidden_prefix, 0) == 0 && line.size() > 7) {
            g_hidden_ids.insert(Lower(line.substr(7)));
        } else if (line.rfind(selected_prefix, 0) == 0 && line.size() > 14) {
            g_last_selected_id = Lower(line.substr(14));
        } else if (line.rfind(imported_prefix, 0) == 0 && line.size() > 12) {
            const std::string value = line.substr(12);
            const auto separator = value.find('\t');
            if (separator == std::string::npos || separator == 0U ||
                separator + 1U >= value.size()) {
                continue;
            }
            std::int64_t timestamp = 0;
            const char* begin = value.data() + separator + 1U;
            const char* end = value.data() + value.size();
            const auto parsed = std::from_chars(begin, end, timestamp);
            if (parsed.ec == std::errc{} && parsed.ptr == end &&
                timestamp > 0) {
                g_imported_at[Lower(value.substr(0, separator))] = timestamp;
            }
        } else if (line.rfind(size_prefix, 0) == 0 && line.size() > 13) {
            const std::string value = line.substr(13);
            const auto separator = value.find('\t');
            if (separator == std::string::npos || separator == 0U ||
                separator + 1U >= value.size()) {
                continue;
            }
            std::uintmax_t size = 0U;
            const char* begin = value.data() + separator + 1U;
            const char* end = value.data() + value.size();
            const auto parsed = std::from_chars(begin, end, size);
            if (parsed.ec == std::errc{} && parsed.ptr == end) {
                g_managed_sizes[Lower(value.substr(0, separator))] = size;
            }
        }
    }
    for (const auto& id : g_hidden_ids) g_enabled_ids.erase(id);
}

void SaveSettingsLocked() {
    std::error_code error;
    std::filesystem::create_directories(g_settings_path.parent_path(), error);
    const auto temporary = g_settings_path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        g_status = "Texture-pack preferences could not be saved.";
        return;
    }
    output << "# DKR-R native texture-pack state\n";
    if (!g_last_selected_id.empty()) {
        output << "last_selected=" << g_last_selected_id << '\n';
    }
    for (const auto& id : g_enabled_ids) output << "enabled=" << id << '\n';
    for (const auto& id : g_hidden_ids) output << "hidden=" << id << '\n';
    for (const auto& [id, timestamp] : g_imported_at) {
        output << "imported_at=" << id << '\t' << timestamp << '\n';
    }
    for (const auto& [id, size] : g_managed_sizes) {
        output << "managed_size=" << id << '\t' << size << '\n';
    }
    for (const auto& [id, owner] : g_track_pack_owners) {
        output << "track_pack=" << id << '\t' << owner.track_id << '\t'
               << owner.digest << '\t' << owner.fingerprint << '\n';
    }
    output.close();
    std::filesystem::rename(temporary, g_settings_path, error);
    if (error) {
        std::filesystem::remove(g_settings_path, error);
        error.clear();
        std::filesystem::rename(temporary, g_settings_path, error);
    }
    if (error) g_status = "Texture-pack preferences could not be committed.";
}

void LoadPackIndexLocked() {
    g_pack_index.clear();
    std::ifstream input(g_index_path);
    if (!input) return;
    try {
        const auto root = nlohmann::json::parse(input);
        if (!root.is_object() || root.value("version", 0) != 1 ||
            !root.contains("packs") || !root["packs"].is_array()) {
            return;
        }
        for (const auto& value : root["packs"]) {
            if (!value.is_object()) continue;
            CachedPackInfo cached{};
            cached.info.id = Lower(value.value("id", std::string{}));
            cached.fingerprint = value.value("fingerprint", std::string{});
            cached.info.name = value.value("name", std::string{});
            cached.info.zip_base_path =
                value.value("zipBasePath", std::string{});
            const int format = value.value(
                "format", static_cast<int>(
                              dkr::runtime::texture_packs::Format::Unknown));
            if (cached.info.id.empty() || cached.fingerprint.empty() ||
                format < static_cast<int>(
                             dkr::runtime::texture_packs::Format::NativeRt64) ||
                format > static_cast<int>(
                             dkr::runtime::texture_packs::Format::Unknown)) {
                continue;
            }
            cached.info.format =
                static_cast<dkr::runtime::texture_packs::Format>(format);
            cached.info.compatible = value.value("compatible", false);
            cached.info.image_count = value.value("imageCount", std::size_t{0});
            cached.info.detail = value.value("detail", std::string{});
            g_pack_index[cached.info.id] = std::move(cached);
        }
    } catch (const std::exception&) {
        g_pack_index.clear();
    }
}

void SavePackIndexLocked() {
    if (g_index_path.empty()) return;
    nlohmann::json root;
    root["version"] = 1;
    root["packs"] = nlohmann::json::array();
    for (const auto& [id, cached] : g_pack_index) {
        root["packs"].push_back({
            {"id", id},
            {"fingerprint", cached.fingerprint},
            {"name", cached.info.name},
            {"zipBasePath", cached.info.zip_base_path},
            {"format", static_cast<int>(cached.info.format)},
            {"compatible", cached.info.compatible},
            {"imageCount", cached.info.image_count},
            {"detail", cached.info.detail},
        });
    }
    std::error_code error;
    std::filesystem::create_directories(g_index_path.parent_path(), error);
    if (error) return;
    const std::filesystem::path temporary =
        std::filesystem::path(g_index_path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return;
    output << root.dump(2) << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        return;
    }
    std::filesystem::rename(temporary, g_index_path, error);
    if (!error) return;
    error.clear();
    std::filesystem::remove(g_index_path, error);
    error.clear();
    std::filesystem::rename(temporary, g_index_path, error);
    if (error) std::filesystem::remove(temporary, error);
}

dkr::runtime::texture_packs::PackInfo InspectArchive(
    const std::filesystem::path& path) {
    using dkr::runtime::texture_packs::Format;
    using dkr::runtime::texture_packs::PackInfo;

    PackInfo info;
    info.id = StableId(path);
    info.name = DisplayName(path);
    info.path = path;

    std::unique_ptr<RT64::FileSystem> archive =
        RT64::FileSystemZip::create(path, "");
    if (!archive) {
        info.detail = "The archive is unreadable or uses an unsupported ZIP compression method.";
        return info;
    }

    std::string database_entry;
    bool has_jabo_manifest = false;
    bool has_rice_names = false;
    std::size_t png_or_dds_count = 0;
    std::size_t jpg_count = 0;
    for (auto iterator = archive->begin(); iterator != archive->end(); ++iterator) {
        const std::string entry = *iterator;
        const std::string lower = Lower(entry);
        if (std::filesystem::path(lower).filename() == "rt64.json") {
            if (database_entry.empty() || entry.size() < database_entry.size()) {
                database_entry = entry;
            }
        }
        if (std::filesystem::path(lower).filename() == "pack.xml") {
            has_jabo_manifest = true;
        }
        if (LooksLikeRiceName(entry)) has_rice_names = true;
        if (IsImage(entry)) {
            ++info.image_count;
            const std::string extension = Lower(
                std::filesystem::path(entry).extension().string());
            if (extension == ".png" || extension == ".dds") ++png_or_dds_count;
            else ++jpg_count;
        }
    }

    if (!database_entry.empty()) {
        std::vector<std::uint8_t> database;
        if (!archive->load(database_entry, database)) {
            info.detail = "The native RT64 database could not be read.";
            return info;
        }
        try {
            const auto parsed = nlohmann::json::parse(database.begin(), database.end());
            if (!parsed.is_object() || !parsed.contains("configuration") ||
                !parsed.contains("textures")) {
                info.detail = "rt64.json is present but does not contain a native RT64 config.";
                return info;
            }
        } catch (const nlohmann::json::exception& exception) {
            info.detail = std::string("rt64.json is invalid: ") + exception.what();
            return info;
        }
        const auto slash = database_entry.find_last_of('/');
        info.zip_base_path = slash == std::string::npos
            ? std::string{} : database_entry.substr(0, slash);
        info.format = Format::NativeRt64;
        info.compatible = true;
        info.detail = std::to_string(info.image_count) +
            " replacement images; validated native RT64 database.";
        return info;
    }

    if (has_jabo_manifest) {
        info.format = Format::LegacyJabo;
        info.detail = "Unsupported: " + std::to_string(info.image_count) +
            " images use the legacy Jabo pack.xml/hash layout" +
            (jpg_count > 0 ? ", including " + std::to_string(jpg_count) +
                " JPG files" : std::string{}) +
            ". DKR-R cannot enable this format.";
    } else if (has_rice_names) {
        info.format = Format::LegacyRice;
        info.detail = std::to_string(info.image_count) +
            " images. Legacy Rice filename layout detected. Convert it with RT64's texture tools to produce rt64.json.";
    } else {
        info.detail = std::to_string(info.image_count) +
            " images, but no rt64.json replacement database was found.";
    }
    (void)png_or_dds_count;
    return info;
}

std::filesystem::path UniqueDestination(const std::filesystem::path& source) {
    std::filesystem::path destination = g_pack_directory / source.filename();
    const std::string stem = destination.stem().string();
    const std::string extension = destination.extension().string();
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(destination, error) && suffix < 1000;
         ++suffix) {
        destination = g_pack_directory /
            (stem + "-" + std::to_string(suffix) + extension);
    }
    return destination;
}

std::filesystem::path UniqueManagedDestination(const std::filesystem::path& source) {
    std::filesystem::path destination =
        g_pack_directory / (source.stem().string() + ".rice");
    const std::string stem = destination.stem().string();
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(destination, error) && suffix < 1000;
         ++suffix) {
        destination = g_pack_directory /
            (stem + "-" + std::to_string(suffix) + ".rice");
    }
    return destination;
}

bool EnsureManagedRiceCoordinatePolicy(const std::filesystem::path& database_path,
                                       nlohmann::json& database,
                                       std::string& error_text) {
    if (!database.contains("configuration") ||
        !database["configuration"].is_object()) {
        error_text = "Managed Rice database has no RT64 configuration.";
        return false;
    }
    auto& configuration = database["configuration"];
    if (configuration.value("defaultShift", std::string{}) ==
        dkr::runtime::rice_texture::kLegacyCoordinateShift) {
        return true;
    }

    // DKR-R owns directories carrying dkr-r-rice-import.json. Upgrade only
    // those managed bridges; native RT64 packs retain the author's shift.
    configuration["defaultShift"] = std::string(
        dkr::runtime::rice_texture::kLegacyCoordinateShift);
    std::ofstream database_file(database_path, std::ios::trunc);
    if (!database_file) {
        error_text = "Managed Rice coordinates could not be upgraded safely.";
        return false;
    }
    database_file << database.dump(2) << '\n';
    database_file.close();
    if (!database_file) {
        error_text = "Managed Rice coordinate upgrade could not be committed.";
        return false;
    }
    return true;
}

dkr::runtime::texture_packs::PackInfo InspectDirectory(
    const std::filesystem::path& path) {
    using dkr::runtime::texture_packs::Format;
    using dkr::runtime::texture_packs::PackInfo;

    PackInfo info;
    info.id = StableId(path);
    info.name = DisplayName(path);
    info.path = path;

    std::error_code error;
    const auto database_path = path / "rt64.json";
    const auto report_path = path / "dkr-r-rice-import.json";
    const bool managed_rice = std::filesystem::is_regular_file(report_path, error);
    error.clear();
    if (!std::filesystem::is_regular_file(database_path, error)) {
        info.detail = "Managed pack is missing rt64.json.";
        return info;
    }
    try {
        std::ifstream database_file(database_path);
        auto database = nlohmann::json::parse(database_file);
        if (!database.is_object() || !database.contains("configuration") ||
            !database.contains("textures") || !database["textures"].is_array()) {
            info.detail = "Managed rt64.json does not contain a native RT64 database.";
            return info;
        }
        if (managed_rice && !EnsureManagedRiceCoordinatePolicy(
                database_path, database, info.detail)) {
            return info;
        }
        if (!managed_rice) {
            info.image_count = database["textures"].size();
        } else {
            std::set<std::string> rice_identities;
            std::set<std::string> native_aliases;
            for (const auto& texture : database["textures"]) {
                if (!texture.is_object() || !texture.contains("hashes") ||
                    !texture["hashes"].is_object()) {
                    info.detail = "Managed RT64 database contains an invalid texture entry.";
                    return info;
                }
                const std::string rice = Lower(
                    texture["hashes"].value("rice", std::string{}));
                const std::string alias = Lower(
                    texture["hashes"].value("rt64", std::string{}));
                if (!dkr::runtime::rice_texture::valid_identity(rice) ||
                    alias != dkr::runtime::rice_texture::native_alias_string(rice) ||
                    !rice_identities.insert(rice).second ||
                    !native_aliases.insert(alias).second) {
                    info.detail = "Managed Rice database has an invalid, duplicate, or mismatched identity.";
                    return info;
                }
                const auto image_path = path /
                    ("Diddy Kong Racing#" + rice + "_all.png");
                if (!std::filesystem::is_regular_file(image_path, error)) {
                    info.detail = "Managed Rice database is missing a converted PNG for " + rice + ".";
                    return info;
                }
            }
            info.image_count = rice_identities.size();
        }
    } catch (const std::exception& exception) {
        info.detail = std::string("Managed rt64.json is invalid: ") + exception.what();
        return info;
    }

    if (managed_rice) {
        try {
            std::ifstream report_file(report_path);
            const auto report = nlohmann::json::parse(report_file);
            const std::size_t source_images = report.value("sourceImages", 0U);
            const std::size_t identities = report.value("convertedIdentities", 0U);
            const std::size_t pairs = report.value("mergedRgbAlphaPairs", 0U);
            info.name = std::filesystem::path(
                report.value("sourceArchive", path.filename().string())).stem().string();
            info.format = Format::RiceRt64;
            info.compatible = identities > 0 && identities == info.image_count;
            info.detail = std::to_string(identities) + "/" +
                std::to_string(identities) + " Rice identities ready from " +
                std::to_string(source_images) + " source PNGs; " +
                std::to_string(pairs) + " split RGB/alpha pairs reconstructed.";
            return info;
        } catch (const std::exception& exception) {
            info.detail = std::string("Rice import report is invalid: ") + exception.what();
            return info;
        }
    }

    info.format = Format::NativeRt64;
    info.compatible = true;
    info.detail = std::to_string(info.image_count) +
        " replacement identities; validated native RT64 directory.";
    return info;
}

bool IsDirectManagedChild(const std::filesystem::path& path) {
    if (g_pack_directory.empty() || path.empty()) return false;
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::absolute(g_pack_directory, error).lexically_normal();
    if (error) return false;
    error.clear();
    const std::filesystem::path candidate =
        std::filesystem::absolute(path, error).lexically_normal();
    return !error && candidate != root && candidate.parent_path() == root;
}

bool RemoveManagedPath(const std::filesystem::path& path,
                       std::string& error_text) {
    if (!IsDirectManagedChild(path)) {
        error_text = "DKR-R refused to delete a path outside its managed texture-pack folder.";
        return false;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        error_text = "The managed texture pack could not be inspected: " +
            error.message();
        return false;
    }
    error.clear();
    if (std::filesystem::is_symlink(status)) {
        error_text = "DKR-R will not recursively delete a symbolic link.";
        return false;
    }
    if (std::filesystem::exists(status)) {
        std::filesystem::remove_all(path, error);
    }
    if (error) {
        error_text = "The managed texture pack could not be deleted: " +
            error.message();
        return false;
    }
    return true;
}

// Identifies a pack archive by its entries - each name, CRC-32 and size, read
// from the zip's own central directory and sorted - so an identical pack
// re-exported with fresh file timestamps still matches, while any change to a
// name or an image does not. Empty when the archive cannot be read.
std::string ArchiveFingerprint(const std::filesystem::path& path) {
    try {
        mz_zip_archive zip{};
        if (mz_zip_reader_init_file(&zip, path.string().c_str(), 0) ==
            MZ_FALSE) {
            return {};
        }
        std::vector<std::string> entries;
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        entries.reserve(count);
        for (mz_uint index = 0; index < count; ++index) {
            mz_zip_archive_file_stat stat{};
            if (mz_zip_reader_file_stat(&zip, index, &stat) == MZ_FALSE) {
                continue;
            }
            char suffix[48];
            std::snprintf(suffix, sizeof(suffix), ":%08x:%llu",
                          static_cast<unsigned>(stat.m_crc32),
                          static_cast<unsigned long long>(stat.m_uncomp_size));
            entries.push_back(std::string(stat.m_filename) + suffix);
        }
        mz_zip_reader_end(&zip);
        std::sort(entries.begin(), entries.end());
        std::uint64_t hash = 14695981039346656037ULL;   // FNV-1a
        for (const std::string& entry : entries) {
            for (const unsigned char character : entry) {
                hash = (hash ^ character) * 1099511628211ULL;
            }
            hash = (hash ^ 0x0AU) * 1099511628211ULL;
        }
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx",
                      static_cast<unsigned long long>(hash));
        return text;
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace

namespace dkr::runtime::texture_packs {

const char* format_name(Format format) {
    switch (format) {
    case Format::NativeRt64: return "Native RT64";
    case Format::RiceRt64: return "Rice / RT64 Bridge";
    case Format::LegacyRice: return "Legacy Rice";
    case Format::LegacyJabo: return "Legacy Jabo";
    default: return "Unknown";
    }
}

namespace {

PackInfo PlaceholderInfo(const std::filesystem::path& path, bool directory) {
    PackInfo info{};
    info.id = StableId(path);
    info.name = DisplayName(path);
    info.path = path;
    if (directory) {
        std::error_code error;
        info.format = std::filesystem::is_regular_file(
                          path / "dkr-r-rice-import.json", error)
            ? Format::RiceRt64 : Format::NativeRt64;
    } else if (Lower(path.extension().string()) == ".rtz") {
        info.format = Format::NativeRt64;
    }
    info.detail = "Pack details have not been indexed yet.";
    return info;
}

void ScanLibrary(bool cache_only, bool force_deep,
                 std::stop_token stop_token = {}) {
    std::scoped_lock refresh_lock(g_refresh_mutex);
    const auto refresh_started_at =
        dkr::runtime::startup_performance::Clock::now();
    std::filesystem::path directory;
    std::set<std::string> enabled;
    std::set<std::string> hidden;
    std::map<std::string, std::int64_t> imported_at;
    std::map<std::string, std::uintmax_t> managed_sizes;
    std::map<std::string, TrackPackOwnerRecord> track_pack_owners;
    std::map<std::string, CachedPackInfo> pack_index;
    {
        std::scoped_lock lock(g_mutex);
        directory = g_pack_directory;
        enabled = g_enabled_ids;
        hidden = g_hidden_ids;
        imported_at = g_imported_at;
        managed_sizes = g_managed_sizes;
        track_pack_owners = g_track_pack_owners;
        pack_index = g_pack_index;
    }
    const auto apply_origin = [&track_pack_owners](PackInfo& info) {
        const auto owner = track_pack_owners.find(info.id);
        if (owner == track_pack_owners.end()) {
            info.origin = Origin::User;
            info.owner_track_id.clear();
            info.texture_digest.clear();
            return;
        }
        info.origin = Origin::TrackPack;
        info.owner_track_id = owner->second.track_id;
        info.texture_digest = owner->second.digest;
    };
    std::vector<PackInfo> scanned;
    std::map<std::string, CachedPackInfo> next_index;
    bool imported_metadata_changed = false;
    std::size_t cache_hits = 0U;
    std::size_t deep_scans = 0U;
    std::size_t placeholders = 0U;
    const auto populate_metadata = [&](PackInfo& info, bool allow_deep_size) {
        const auto managed_size = managed_sizes.find(info.id);
        if (managed_size != managed_sizes.end()) {
            info.managed_size_bytes = managed_size->second;
        } else if (allow_deep_size ||
                   std::filesystem::is_regular_file(info.path)) {
            info.managed_size_bytes = ManagedSize(info.path);
            managed_sizes[info.id] = info.managed_size_bytes;
            imported_metadata_changed = true;
        }
        const auto imported = imported_at.find(info.id);
        if (imported != imported_at.end()) {
            info.imported_at_unix_seconds = imported->second;
            return;
        }
        info.imported_at_unix_seconds = ManagedTimestamp(info.path);
        if (info.imported_at_unix_seconds <= 0) {
            info.imported_at_unix_seconds = CurrentTimestamp();
        }
        imported_at[info.id] = info.imported_at_unix_seconds;
        imported_metadata_changed = true;
    };
    std::error_code error;
    if (!directory.empty()) {
        std::filesystem::create_directories(directory, error);
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (stop_token.stop_requested()) return;
            if (error) break;
            const bool is_directory = entry.is_directory(error);
            if (error) break;
            if (is_directory &&
                Lower(entry.path().extension().string()) == ".importing") {
                continue;
            }
            if (!is_directory) {
                if (!entry.is_regular_file(error)) continue;
                const std::string extension =
                    Lower(entry.path().extension().string());
                if (extension != ".zip" && extension != ".rtz") continue;
            }
            const std::string id = StableId(entry.path());
            const std::string fingerprint =
                PackFingerprint(entry.path(), is_directory);
            PackInfo info{};
            bool fully_inspected = false;
            const auto cached = pack_index.find(id);
            if (!force_deep && cached != pack_index.end() &&
                cached->second.fingerprint == fingerprint) {
                info = cached->second.info;
                info.path = entry.path();
                fully_inspected = true;
                ++cache_hits;
            } else if (cache_only && !enabled.contains(id)) {
                info = PlaceholderInfo(entry.path(), is_directory);
                ++placeholders;
            } else {
                info = is_directory ? InspectDirectory(entry.path())
                                    : InspectArchive(entry.path());
                fully_inspected = true;
                ++deep_scans;
            }
            populate_metadata(info, !cache_only);
            apply_origin(info);
            info.hidden = hidden.contains(info.id);
            info.enabled = !info.hidden && info.compatible && enabled.contains(info.id);
            if (fully_inspected) {
                PackInfo cached_info = info;
                cached_info.path.clear();
                cached_info.enabled = false;
                cached_info.hidden = false;
                cached_info.managed_size_bytes = 0U;
                cached_info.imported_at_unix_seconds = 0;
                cached_info.origin = Origin::User;
                cached_info.owner_track_id.clear();
                cached_info.texture_digest.clear();
                next_index[id] = {fingerprint, std::move(cached_info)};
            }
            scanned.emplace_back(std::move(info));
        }
    }
    std::sort(scanned.begin(), scanned.end(),
              [](const PackInfo& left, const PackInfo& right) {
                  return Lower(left.name) < Lower(right.name);
              });
    const std::size_t scanned_count = scanned.size();
    {
        std::scoped_lock lock(g_mutex);
        for (auto& info : scanned) {
            const auto owner = g_track_pack_owners.find(info.id);
            if (owner == g_track_pack_owners.end()) {
                info.origin = Origin::User;
                info.owner_track_id.clear();
                info.texture_digest.clear();
            } else {
                info.origin = Origin::TrackPack;
                info.owner_track_id = owner->second.track_id;
                info.texture_digest = owner->second.digest;
            }
            info.hidden = g_hidden_ids.contains(info.id);
            info.enabled = !info.hidden && info.compatible &&
                           g_enabled_ids.contains(info.id);
        }
        if (imported_metadata_changed) {
            g_imported_at = std::move(imported_at);
            g_managed_sizes = std::move(managed_sizes);
            SaveSettingsLocked();
        }
        if (!cache_only) {
            g_pack_index = std::move(next_index);
            SavePackIndexLocked();
        }
        g_packs = std::move(scanned);
        ++g_generation;
        if (error) g_status = "The texture-pack folder could not be scanned: " + error.message();
        else if (g_packs.empty()) g_status = "No texture packs imported yet.";
        else if (placeholders > 0U)
            g_status = "Texture-pack details will be indexed when the library is opened.";
    }
    std::fprintf(stderr,
                 "[perf][startup] texture-pack-count=%zu cache-hits=%zu "
                 "deep-scans=%zu deferred=%zu\n",
                 scanned_count, cache_hits, deep_scans, placeholders);
    dkr::runtime::startup_performance::report(
        cache_only ? "texture-pack-index-load" : "texture-pack-refresh",
        refresh_started_at);
}

} // namespace

void configure(const std::filesystem::path& config_directory) {
    g_background_refresh.request_stop();
    if (g_background_refresh.joinable()) g_background_refresh.join();
    g_background_refresh_started.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(g_mutex);
        g_pack_directory = config_directory / "texture-packs";
        g_settings_path = config_directory / "texture-packs.ini";
        g_index_path = config_directory / "texture-packs-index-v1.json";
        g_applied_ids.clear();
        g_pending_deletions.clear();
        g_applied_replacements.clear();
        g_applied_generation = 0;
        g_applied_modern = false;
        std::error_code error;
        std::filesystem::create_directories(g_pack_directory, error);
        LoadSettingsLocked();
        LoadPackIndexLocked();
    }
    // The launcher's critical path inspects enabled packs and reuses cached
    // cards, but never walks every inactive archive or directory.
    ScanLibrary(true, false);
}

void request_background_refresh() {
    bool expected = false;
    if (!g_background_refresh_started.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }
    if (g_background_refresh.joinable()) g_background_refresh.join();
    g_background_refresh = std::jthread([](std::stop_token stop_token) {
        ScanLibrary(false, false, stop_token);
    });
}

void refresh() {
    g_background_refresh.request_stop();
    if (g_background_refresh.joinable()) g_background_refresh.join();
    g_background_refresh_started.store(true, std::memory_order_release);
    ScanLibrary(false, true);
}

std::vector<PackInfo> snapshot(bool include_hidden) {
    std::scoped_lock lock(g_mutex);
    if (include_hidden) return g_packs;
    std::vector<PackInfo> visible;
    visible.reserve(g_packs.size());
    std::copy_if(g_packs.begin(), g_packs.end(), std::back_inserter(visible),
                 [](const PackInfo& pack) { return !pack.hidden; });
    return visible;
}

std::uint64_t generation() {
    std::scoped_lock lock(g_mutex);
    return g_generation;
}

bool import_archive(const std::filesystem::path& source, std::string& status_text,
                    const ImportProgressCallback& progress,
                    const TrackPackOwner* owner) {
    // See g_import_apply_hold: whatever this import changes reaches RT64 as a
    // single reload, after it returns, however many steps it takes to get there.
    struct ApplyHold {
        ApplyHold() { g_import_apply_hold.fetch_add(1, std::memory_order_acq_rel); }
        ~ApplyHold() { g_import_apply_hold.fetch_sub(1, std::memory_order_acq_rel); }
    } apply_hold;
    if (!ReportImportProgress(progress, 0.01F,
                              "Validating texture-pack archive")) {
        status_text = "Texture-pack import cancelled.";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        status_text = "Choose a readable ZIP or RTZ texture-pack archive.";
        return false;
    }
    const std::string extension = Lower(source.extension().string());
    if (extension != ".zip" && extension != ".rtz") {
        status_text = "Texture packs must be supplied as ZIP or RTZ archives.";
        return false;
    }
    constexpr std::uintmax_t maximum_archive_size =
        static_cast<std::uintmax_t>(4) * 1024U * 1024U * 1024U;
    const auto size = std::filesystem::file_size(source, error);
    if (error || size == 0 || size > maximum_archive_size) {
        status_text = "The texture-pack archive is empty, unreadable, or larger than 4 GB.";
        return false;
    }

    // A <track>-hd.zip: skip when this exact pack is already filed against a
    // track (the authoring loop rescans on every editor save), and drop the
    // one from an earlier export of the same track so packs never accumulate.
    // "Exact" means the archive's entries, not only the textureDigest: that
    // digest covers the 64x32 payloads, and the same payloads can ship under
    // different replacement names - the addon once hashed the Rice identity
    // over the wrong byte order, and a fixed re-export changed every name but
    // not one payload. A record from before fingerprints existed never
    // matches, so such an install heals on its next import.
    const std::string fingerprint =
        owner != nullptr ? ArchiveFingerprint(source) : std::string{};
    std::string stale_track_pack_id;
    if (owner != nullptr) {
        std::scoped_lock lock(g_mutex);
        for (const auto& pack : g_packs) {
            if (pack.origin != Origin::TrackPack) continue;
            const auto record = g_track_pack_owners.find(pack.id);
            const bool same_pack =
                !owner->texture_digest.empty() && !fingerprint.empty() &&
                pack.texture_digest == owner->texture_digest &&
                record != g_track_pack_owners.end() &&
                record->second.fingerprint == fingerprint;
            if (same_pack) {
                if (pack.compatible && !pack.hidden &&
                    !g_enabled_ids.contains(pack.id)) {
                    g_enabled_ids.insert(pack.id);
                    SaveSettingsLocked();
                    ++g_generation;
                }
                status_text =
                    pack.name + " is already installed for this track.";
                return true;
            }
            if (pack.owner_track_id == owner->track_id) {
                stale_track_pack_id = pack.id;
            }
        }
    }
    if (!stale_track_pack_id.empty()) {
        std::string ignored;
        delete_managed(stale_track_pack_id, ignored);
    }

    const auto record_track_pack = [&](const std::filesystem::path& dest) {
        if (owner == nullptr) return;
        const std::string id = StableId(dest);
        std::scoped_lock lock(g_mutex);
        g_track_pack_owners[id] = {owner->track_id, owner->texture_digest,
                                   fingerprint};
        g_enabled_ids.insert(id);   // a track pack is born enabled
        SaveSettingsLocked();
        ++g_generation;
    };

    if (!ReportImportProgress(progress, 0.04F,
                              "Inspecting texture-pack archive")) {
        status_text = "Texture-pack import cancelled.";
        return false;
    }
    PackInfo inspection = InspectArchive(source);
    if (inspection.detail.find("unreadable") != std::string::npos) {
        status_text = inspection.detail;
        return false;
    }

    if (inspection.format == Format::LegacyRice) {
        std::unique_ptr<RT64::FileSystem> archive =
            RT64::FileSystemZip::create(source, "");
        if (!archive) {
            status_text = "The Rice archive could not be reopened for conversion.";
            return false;
        }

        std::filesystem::path destination;
        {
            std::scoped_lock lock(g_mutex);
            std::filesystem::create_directories(g_pack_directory, error);
            destination = UniqueManagedDestination(source);
        }
        const std::filesystem::path temporary =
            destination.parent_path() / (destination.filename().string() + ".importing");
        if (temporary.parent_path() != g_pack_directory ||
            destination.parent_path() != g_pack_directory) {
            status_text = "The managed Rice destination failed its safety check.";
            return false;
        }
        std::filesystem::remove_all(temporary, error);
        error.clear();

        rice_texture::ImportResult conversion;
        std::string conversion_error;
        const auto rice_progress = [&](std::size_t completed,
                                       std::size_t total,
                                       const char* stage) {
            const float conversion_fraction = total == 0
                ? 0.08F
                : 0.08F + 0.82F * static_cast<float>(completed) /
                    static_cast<float>(total);
            return ReportImportProgress(progress, conversion_fraction, stage);
        };
        if (!rice_texture::convert_archive(*archive, temporary,
                                           source.filename().string(),
                                           conversion, conversion_error,
                                           rice_progress)) {
            std::filesystem::remove_all(temporary, error);
            status_text = conversion_error == "Import cancelled."
                ? "Texture-pack import cancelled."
                : "Rice import failed: " + conversion_error;
            return false;
        }
        // From this point forward cancellation is disabled: rename is the
        // atomic commit and must be allowed to finish once announced.
        ReportImportProgress(progress, 0.94F,
                             "Committing converted texture pack", true);
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            const std::string rename_error = error.message();
            std::filesystem::remove_all(temporary, error);
            status_text = "The converted Rice pack could not be committed: " + rename_error;
            return false;
        }
        {
            std::scoped_lock lock(g_mutex);
            g_imported_at[StableId(destination)] = CurrentTimestamp();
            g_managed_sizes[StableId(destination)] = ManagedSize(destination);
            SaveSettingsLocked();
        }
        record_track_pack(destination);
        refresh();
        const auto imported = InspectDirectory(destination);
        status_text = "Imported " + source.filename().string() + " as " +
            format_name(imported.format) + ". " + imported.detail;
        {
            std::scoped_lock lock(g_mutex);
            g_status = status_text;
        }
        ReportImportProgress(progress, 1.0F, "Texture pack imported", true);
        return imported.compatible;
    }

    std::filesystem::path destination;
    {
        std::scoped_lock lock(g_mutex);
        std::filesystem::create_directories(g_pack_directory, error);
        destination = UniqueDestination(source);
    }
    const std::filesystem::path temporary = destination.parent_path() /
        (destination.filename().string() + ".importing");
    if (temporary.parent_path() != g_pack_directory ||
        destination.parent_path() != g_pack_directory) {
        status_text = "The managed texture-pack destination failed its safety check.";
        return false;
    }
    std::filesystem::remove(temporary, error);
    error.clear();
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        std::filesystem::remove(temporary, error);
        status_text = "The texture pack could not be opened for managed import.";
        return false;
    }
    // Imports run on a background jthread whose Windows stack is smaller than
    // the main thread's. Keeping this 1 MiB transfer buffer as a local array
    // made the function's stack frame overflow before either the Rice or RTZ
    // branch could execute. Allocate the workspace on the heap instead.
    std::vector<char> buffer(1024U * 1024U);
    std::uintmax_t copied = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) break;
        output.write(buffer.data(), count);
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            status_text = "The managed texture-pack copy could not be written.";
            return false;
        }
        copied += static_cast<std::uintmax_t>(count);
        const float copy_fraction = 0.08F + 0.84F *
            static_cast<float>(copied) / static_cast<float>(size);
        if (!ReportImportProgress(progress, copy_fraction,
                                  "Copying texture-pack archive")) {
            output.close();
            input.close();
            std::filesystem::remove(temporary, error);
            status_text = "Texture-pack import cancelled.";
            return false;
        }
    }
    output.close();
    input.close();
    if (!output || (copied != size)) {
        std::filesystem::remove(temporary, error);
        status_text = "The managed texture-pack copy ended before the archive was complete.";
        return false;
    }
    ReportImportProgress(progress, 0.96F, "Committing texture pack", true);
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        const std::string rename_error = error.message();
        std::filesystem::remove(temporary, error);
        status_text = "The texture pack could not be committed: " + rename_error;
        return false;
    }
    {
        std::scoped_lock lock(g_mutex);
        g_imported_at[StableId(destination)] = CurrentTimestamp();
        g_managed_sizes[StableId(destination)] = size;
        SaveSettingsLocked();
    }
    record_track_pack(destination);
    refresh();
    const auto imported = InspectArchive(destination);
    status_text = "Imported " + destination.filename().string() + " as " +
        format_name(imported.format) + ". " + imported.detail;
    {
        std::scoped_lock lock(g_mutex);
        g_status = status_text;
    }
    ReportImportProgress(progress, 1.0F, "Texture pack imported", true);
    return true;
}

void set_enabled(const std::string& id, bool enabled) {
    std::scoped_lock lock(g_mutex);
    const std::string normalized = Lower(id);
    const auto match = std::find_if(g_packs.begin(), g_packs.end(),
        [&](const PackInfo& pack) { return pack.id == normalized; });
    if (match == g_packs.end() || !match->compatible || match->hidden) return;
    match->enabled = enabled;
    g_last_selected_id = normalized;
    if (enabled) g_enabled_ids.insert(normalized);
    else g_enabled_ids.erase(normalized);
    ++g_generation;
    SaveSettingsLocked();
    g_status = match->name + (enabled ? " queued for live activation." : " queued for live removal.");
}

bool toggle_last_selected(std::string& status) {
    std::scoped_lock lock(g_mutex);
    auto match = std::find_if(g_packs.begin(), g_packs.end(),
        [](const PackInfo& pack) {
            return Lower(pack.id) == g_last_selected_id;
        });
    if (match == g_packs.end() || !match->compatible || match->hidden) {
        status = "No available texture pack has been selected yet.";
        g_status = status;
        return false;
    }
    match->enabled = !match->enabled;
    if (match->enabled) g_enabled_ids.insert(g_last_selected_id);
    else g_enabled_ids.erase(g_last_selected_id);
    SaveSettingsLocked();
    ++g_generation;
    status = match->name + (match->enabled
        ? " queued for live activation."
        : " queued for live removal.");
    g_status = status;
    return true;
}

bool set_hidden(const std::string& id, bool hidden, std::string& status_text) {
    std::scoped_lock lock(g_mutex);
    const std::string normalized = Lower(id);
    const auto match = std::find_if(g_packs.begin(), g_packs.end(),
        [&](const PackInfo& pack) { return pack.id == normalized; });
    if (match == g_packs.end()) {
        status_text = "The selected texture pack is no longer available.";
        return false;
    }
    match->hidden = hidden;
    if (hidden) {
        match->enabled = false;
        g_enabled_ids.erase(normalized);
        g_hidden_ids.insert(normalized);
    } else {
        g_hidden_ids.erase(normalized);
    }
    ++g_generation;
    SaveSettingsLocked();
    g_status = match->name + (hidden
        ? " is hidden. Its managed files remain on disk."
        : " is visible in the texture-pack library again.");
    status_text = g_status;
    return true;
}

bool delete_managed(const std::string& id, std::string& status_text) {
    const std::string normalized = Lower(id);
    PackInfo selected;
    bool defer_until_reload = false;
    {
        std::scoped_lock lock(g_mutex);
        const auto match = std::find_if(g_packs.begin(), g_packs.end(),
            [&](const PackInfo& pack) { return pack.id == normalized; });
        if (match == g_packs.end()) {
            status_text = "The selected texture pack is no longer available.";
            return false;
        }
        if (!IsDirectManagedChild(match->path)) {
            status_text = "DKR-R refused to delete a path outside its managed texture-pack folder.";
            return false;
        }
        selected = *match;
        defer_until_reload = g_applied_ids.contains(normalized);
    }

    if (!defer_until_reload) {
        if (!RemoveManagedPath(selected.path, status_text)) return false;
    }

    {
        std::scoped_lock lock(g_mutex);
        g_enabled_ids.erase(normalized);
        if (defer_until_reload) {
            // Keep a tombstone in the settings file until RT64 has released
            // the archive/directory. A crash before the next reload therefore
            // leaves the pack hidden rather than silently re-enabling it.
            g_hidden_ids.insert(normalized);
            g_pending_deletions.push_back(
                {normalized, selected.path, selected.name});
        } else {
            g_hidden_ids.erase(normalized);
            g_imported_at.erase(normalized);
            g_managed_sizes.erase(normalized);
        }
        g_track_pack_owners.erase(normalized);
        g_packs.erase(std::remove_if(g_packs.begin(), g_packs.end(),
            [&](const PackInfo& pack) { return pack.id == normalized; }),
            g_packs.end());
        ++g_generation;
        SaveSettingsLocked();
        g_status = defer_until_reload
            ? selected.name +
                " will be permanently deleted after RT64 releases the active pack."
            : selected.name + " was permanently deleted from DKR-R's managed library.";
        status_text = g_status;
    }
    return true;
}

TrackPackState track_pack_state(const std::string& track_id,
                                const std::string& expected_digest) {
    std::scoped_lock lock(g_mutex);
    TrackPackState state;
    for (const auto& pack : g_packs) {
        if (pack.origin != Origin::TrackPack ||
            pack.owner_track_id != track_id) {
            continue;
        }
        state.installed = true;
        state.pack_id = pack.id;
        state.enabled = pack.enabled;
        state.digest_matches = !expected_digest.empty() &&
                               pack.texture_digest == expected_digest;
    }
    return state;
}

bool forget_track_pack(const std::string& track_id, std::string& status_text) {
    std::string pack_id;
    {
        std::scoped_lock lock(g_mutex);
        for (const auto& [id, owner] : g_track_pack_owners) {
            if (owner.track_id == track_id) {
                pack_id = id;
            }
        }
    }
    if (pack_id.empty()) {
        status_text = "That track has no HD texture pack to remove.";
        return false;
    }
    return delete_managed(pack_id, status_text);
}

void request_reload() {
    std::scoped_lock lock(g_mutex);
    g_force_replacement_reload = true;
    ++g_generation;
}

void apply_pending(RT64::Application& application, bool modern_profile) {
    std::vector<RT64::ReplacementDirectory> replacements;
    std::vector<RT64::ReplacementDirectory> previous_replacements;
    std::set<std::string> replacement_ids;
    std::vector<PendingDeletion> completed_deletions;
    std::uint64_t generation = 0;
    // An import in flight applies once, when it is done. The generation is left
    // unconsumed, so the frame after the hold lifts picks it up.
    if (g_import_apply_hold.load(std::memory_order_acquire) > 0) return;
    {
        std::scoped_lock lock(g_mutex);
        generation = g_generation;
        if (generation == g_applied_generation && modern_profile == g_applied_modern) return;
        previous_replacements = g_applied_replacements;
        if (modern_profile) {
            for (const auto& pack : g_packs) {
                if (pack.enabled && pack.compatible) {
                    replacements.emplace_back(pack.path, pack.zip_base_path);
                    replacement_ids.insert(pack.id);
                }
            }
        }
        // Only a changed replacement set is worth handing to RT64. The reload
        // is transactional - streaming stops, every mapping is cleared and
        // rebuilt, resident textures are re-resolved - and it runs here, on the
        // graphics thread, where a long enough stall makes the game's scheduler
        // drop a late completion and wait on it forever. The generation also
        // moves for changes that never reach RT64 (a rescan, hiding a disabled
        // pack), so compare the set itself.
        const bool same_set =
            g_applied_generation != 0 && !g_force_replacement_reload &&
            modern_profile == g_applied_modern &&
            g_pending_deletions.empty() &&
            std::equal(replacements.begin(), replacements.end(),
                       g_applied_replacements.begin(),
                       g_applied_replacements.end(),
                       [](const RT64::ReplacementDirectory& left,
                          const RT64::ReplacementDirectory& right) {
                           return left.dirOrZipPath == right.dirOrZipPath &&
                                  left.zipBasePath == right.zipBasePath;
                       });
        if (same_set) {
            g_applied_generation = generation;
            return;
        }
        g_force_replacement_reload = false;
    }

    const bool cache_available = application.textureCache != nullptr;
    bool success = cache_available &&
        application.textureCache->loadReplacementDirectories(replacements);
    bool restored = false;
    if (!success && cache_available) {
        restored = application.textureCache->loadReplacementDirectories(
            previous_replacements);
    }
    // One line per real reload - rare, since unchanged sets are skipped above -
    // because a rejected or slow reload is otherwise invisible in the log.
    std::fprintf(stderr,
                 "[texture-packs] replacement set reloaded: %zu pack(s) "
                 "accepted=%d restored=%d\n",
                 replacements.size(), success ? 1 : 0, restored ? 1 : 0);
    for (const auto& directory : replacements) {
        std::fprintf(stderr, "[texture-packs]   %s\n",
                     directory.dirOrZipPath.string().c_str());
    }
    {
        std::scoped_lock lock(g_mutex);
        if (success) {
            g_applied_generation = generation;
            g_applied_modern = modern_profile;
            g_applied_replacements = replacements;
            g_applied_ids = replacement_ids;
            completed_deletions.swap(g_pending_deletions);
            g_status = replacements.empty()
                ? "Native texture replacements are disabled."
                : std::to_string(replacements.size()) +
                    " native texture pack(s) active. Existing textures were reloaded safely.";
        } else {
            // Mark this generation as consumed so a rejected archive cannot
            // trigger an expensive reload on every presented frame.
            g_applied_generation = generation;
            g_applied_modern = modern_profile;
            g_status = restored
                ? "RT64 rejected the replacement set; the previous texture set was restored."
                : "RT64 rejected the replacement set and could not restore it. Disable the affected pack before continuing.";
        }
    }
    if (success && !completed_deletions.empty()) {
        std::vector<std::string> deleted_ids;
        std::string deletion_error;
        for (const auto& pending : completed_deletions) {
            std::string error_text;
            if (RemoveManagedPath(pending.path, error_text)) {
                deleted_ids.push_back(pending.id);
            } else if (deletion_error.empty()) {
                deletion_error = pending.name + ": " + error_text;
            }
        }
        std::scoped_lock lock(g_mutex);
        for (const auto& deleted_id : deleted_ids) {
            g_hidden_ids.erase(deleted_id);
            g_imported_at.erase(deleted_id);
            g_managed_sizes.erase(deleted_id);
        }
        SaveSettingsLocked();
        if (!deletion_error.empty()) {
            g_status = "RT64 released the texture pack, but permanent deletion failed: " +
                deletion_error;
        } else {
            g_status = completed_deletions.size() == 1
                ? completed_deletions.front().name +
                    " was permanently deleted from DKR-R's managed library."
                : std::to_string(completed_deletions.size()) +
                    " texture packs were permanently deleted from DKR-R's managed library.";
        }
    }
}

std::string status() {
    std::scoped_lock lock(g_mutex);
    return g_status;
}

} // namespace dkr::runtime::texture_packs
