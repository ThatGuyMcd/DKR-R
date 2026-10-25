#include "runtime_rice_texture_import.hpp"
#include <stop_token>

#include "rice_texture_pack_policy.hpp"

#include "common/rt64_filesystem.h"
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <json/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::size_t kMaximumEntries = 200000;
constexpr std::size_t kMaximumSourceImageBytes = 256U * 1024U * 1024U;
constexpr std::uint64_t kMaximumDecodedPixels = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumTotalLoadedBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;

struct Sources {
    std::optional<std::string> all;
    std::optional<std::string> rgb;
    std::optional<std::string> alpha;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

bool AssignUnique(std::optional<std::string>& target,
                  const std::string& entry,
                  const std::string& identity,
                  std::string& error) {
    if (target.has_value()) {
        error = "Rice identity " + identity +
            " contains duplicate channel variants; import stopped to avoid an ambiguous replacement.";
        return false;
    }
    target = entry;
    return true;
}

bool LoadImage(RT64::FileSystem& archive,
               const std::string& entry,
               Image& image,
               std::uint64_t& total_loaded,
               std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!archive.load(entry, bytes)) {
        error = "Could not read " + entry + " from the Rice archive.";
        return false;
    }
    if (bytes.empty() || bytes.size() > kMaximumSourceImageBytes ||
        total_loaded > kMaximumTotalLoadedBytes - bytes.size()) {
        error = "Rice image " + entry + " exceeds the safe import limits.";
        return false;
    }
    total_loaded += bytes.size();

    int components = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                               &image.width, &image.height, &components) ||
        image.width <= 0 || image.height <= 0 ||
        static_cast<std::uint64_t>(image.width) * image.height >
            kMaximumDecodedPixels) {
        error = "Rice image " + entry + " has invalid or unsafe PNG dimensions.";
        return false;
    }
    stbi_uc* decoded = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()),
        &image.width, &image.height, &components, 4);
    if (decoded == nullptr) {
        error = "Rice image " + entry + " could not be decoded as PNG.";
        return false;
    }
    const std::size_t byte_count = static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height) * 4U;
    image.rgba.assign(decoded, decoded + byte_count);
    stbi_image_free(decoded);
    return true;
}

bool WritePng(const std::filesystem::path& path,
              const Image& image,
              std::string& error) {
    const auto utf8 = path.u8string();
    const std::string path_string(utf8.begin(), utf8.end());
    if (stbi_write_png(path_string.c_str(), image.width, image.height, 4,
                       image.rgba.data(), image.width * 4) == 0) {
        error = "Could not write converted Rice texture " + path.filename().string() + ".";
        return false;
    }
    return true;
}


// Match decoded pixels, not filenames/counts (different packs often replace
// the same identities). This audit never writes or activates installed data.
bool ComparePng(const std::filesystem::path& path, const Image& expected) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) return false;
    const auto size = std::filesystem::file_size(path);
    if (!size || size > kMaximumSourceImageBytes) return false;
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
        width != expected.width || height != expected.height) return false;
    auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) return false;
    const bool same = width == expected.width && height == expected.height &&
        std::equal(expected.rgba.begin(), expected.rgba.end(), pixels);
    stbi_image_free(pixels);
    return same;
}

} // namespace

namespace dkr::runtime::rice_texture {

static bool convert_or_compare_archive(RT64::FileSystem& archive,
                     const std::filesystem::path& destination,
                     const std::string& source_name,
                     ImportResult& result,
                     std::string& error,
                     const ProgressCallback& progress, bool compare_only) {
    result = {};
    if (progress && !progress(0, 0, "Inspecting Rice texture identities")) {
        error = "Import cancelled.";
        return false;
    }
    std::map<std::string, Sources> identities;
    std::size_t entry_count = 0;
    for (auto iterator = archive.begin(); iterator != archive.end(); ++iterator) {
        const std::string entry = *iterator;
        if (++entry_count > kMaximumEntries) {
            error = "The archive contains too many entries to import safely.";
            return false;
        }
        if (!safe_archive_path(entry)) {
            error = "The archive contains an unsafe path: " + entry;
            return false;
        }
        const auto parsed = parse_filename(entry);
        if (!parsed.has_value()) continue;
        ++result.source_images;
        Sources& sources = identities[parsed->identity];
        bool assigned = false;
        switch (parsed->variant) {
        case Variant::All:
            assigned = AssignUnique(sources.all, entry, parsed->identity, error);
            break;
        case Variant::Rgb:
            assigned = AssignUnique(sources.rgb, entry, parsed->identity, error);
            break;
        case Variant::Alpha:
            assigned = AssignUnique(sources.alpha, entry, parsed->identity, error);
            break;
        }
        if (!assigned) return false;
        if (progress && (entry_count & 0xFFU) == 0U &&
            !progress(0, 0, "Inspecting Rice texture identities")) {
            error = "Import cancelled.";
            return false;
        }
    }
    if (identities.empty()) {
        error = "No valid Rice PNG names were found in the archive.";
        return false;
    }

    std::unordered_map<std::uint64_t, std::string> aliases;
    for (const auto& [identity, sources] : identities) {
        if (!sources.all.has_value() && !sources.rgb.has_value()) {
            error = "Rice identity " + identity +
                " has an alpha image but no colour image.";
            return false;
        }
        const std::uint64_t alias = native_alias(identity);
        const auto [existing, inserted] = aliases.emplace(alias, identity);
        if (!inserted && existing->second != identity) {
            error = "Two Rice identities produced the same native alias; import stopped safely.";
            return false;
        }
    }

    std::error_code filesystem_error;
    if (!compare_only) std::filesystem::create_directories(destination, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the managed Rice pack: " + filesystem_error.message();
        return false;
    }

    nlohmann::json textures = nlohmann::json::array();
    std::uint64_t total_loaded = 0;
    std::size_t completed_identities = 0;
    for (const auto& [identity, sources] : identities) {
        if (progress &&
            !progress(completed_identities, identities.size(),
                      "Converting Rice textures")) {
            error = "Import cancelled.";
            return false;
        }
        Image colour;
        if (sources.all.has_value()) {
            if (!LoadImage(archive, *sources.all, colour, total_loaded, error)) return false;
            ++result.all_images;
        } else {
            if (!LoadImage(archive, *sources.rgb, colour, total_loaded, error)) return false;
            if (sources.alpha.has_value()) {
                Image alpha;
                if (!LoadImage(archive, *sources.alpha, alpha, total_loaded, error)) return false;
                if (colour.width != alpha.width || colour.height != alpha.height) {
                    error = "Rice RGB/alpha dimensions do not match for " + identity + ".";
                    return false;
                }
                for (std::size_t pixel = 0; pixel < colour.rgba.size(); pixel += 4) {
                    colour.rgba[pixel + 3] = alpha.rgba[pixel];
                }
                ++result.merged_pairs;
            } else {
                for (std::size_t pixel = 3; pixel < colour.rgba.size(); pixel += 4) {
                    colour.rgba[pixel] = 255;
                }
                ++result.opaque_rgb;
            }
        }

        const std::string filename = "Diddy Kong Racing#" + identity + "_all.png";
        if (compare_only) {
            if (!ComparePng(destination / filename, colour)) return false;
        } else if (!WritePng(destination / filename, colour, error)) return false;
        textures.push_back({
            {"path", ""},
            {"hashes", {
                {"rt64", native_alias_string(identity)},
                {"rice", identity}
            }}
        });
        ++completed_identities;
    }

    if (progress &&
        !progress(completed_identities, identities.size(),
                  "Writing texture database")) {
        error = "Import cancelled.";
        return false;
    }

    nlohmann::json database = {
        {"configuration", {
            {"configurationVersion", 3},
            {"autoPath", "rice"},
            {"defaultOperation", "stream"},
            {"defaultShift", std::string(kLegacyCoordinateShift)},
            {"hashVersion", 5}
        }},
        {"textures", std::move(textures)},
        {"operationFilters", nlohmann::json::array()},
        {"shiftFilters", nlohmann::json::array()},
        {"extraFiles", nlohmann::json::array()}
    };
    if (compare_only) {
        const auto db_path = destination / "rt64.json";
        const auto report_path = destination / "dkr-r-rice-import.json";
        if (std::filesystem::is_symlink(db_path) || std::filesystem::is_symlink(report_path) ||
            !std::filesystem::is_regular_file(db_path) || !std::filesystem::is_regular_file(report_path) ||
            std::filesystem::file_size(db_path) > 32U * 1024U * 1024U ||
            std::filesystem::file_size(report_path) > 64U * 1024U) return false;
        std::ifstream db_file(db_path), report_file(report_path);
        const auto existing = nlohmann::json::parse(db_file);
        const auto report = nlohmann::json::parse(report_file);
        return existing == database && report.value("sourceImages", 0U) == result.source_images &&
            report.value("convertedIdentities", 0U) == identities.size() &&
            report.value("mergedRgbAlphaPairs", 0U) == result.merged_pairs &&
            report.value("opaqueRgbImages", 0U) == result.opaque_rgb &&
            report.value("nativeAllImages", 0U) == result.all_images;
    }
    std::ofstream database_file(destination / "rt64.json", std::ios::trunc);
    if (!database_file) {
        error = "Could not create the native RT64 database for the Rice pack.";
        return false;
    }
    database_file << database.dump(2) << '\n';
    database_file.close();

    result.identities = identities.size();
    nlohmann::json metadata = {
        {"schemaVersion", 1},
        {"sourceArchive", source_name},
        {"sourceImages", result.source_images},
        {"convertedIdentities", result.identities},
        {"mergedRgbAlphaPairs", result.merged_pairs},
        {"opaqueRgbImages", result.opaque_rgb},
        {"nativeAllImages", result.all_images},
        {"coordinatePolicy", std::string(kLegacyCoordinatePolicy)}
    };
    std::ofstream metadata_file(destination / "dkr-r-rice-import.json", std::ios::trunc);
    if (!metadata_file) {
        error = "Could not write the Rice import report.";
        return false;
    }
    metadata_file << metadata.dump(2) << '\n';
    metadata_file.close();

    result.detail = std::to_string(result.identities) + "/" +
        std::to_string(result.identities) + " Rice identities converted from " +
        std::to_string(result.source_images) + " source PNGs (" +
        std::to_string(result.merged_pairs) + " RGB/alpha pairs merged).";
    return true;
}


bool convert_archive(RT64::FileSystem& archive, const std::filesystem::path& destination,
                     const std::string& source_name, ImportResult& result, std::string& error,
                     const ProgressCallback& progress) {
    return convert_or_compare_archive(archive, destination, source_name, result, error, progress, false);
}
bool matches_converted_archive(RT64::FileSystem& archive, const std::filesystem::path& destination,
                               std::stop_token stop) {
    try {
        ImportResult result;
        std::string error;
        return convert_or_compare_archive(archive, destination, {}, result, error,
            [&](std::size_t, std::size_t, const char*) { return !stop.stop_requested(); }, true);
    } catch (...) { return false; }
}
} // namespace dkr::runtime::rice_texture
