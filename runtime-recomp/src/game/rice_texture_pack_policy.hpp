#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace dkr::runtime::rice_texture {

// Rice packs were authored against legacy Project64/GlideN64 coordinates.
// RT64's v3 half-texel replacement shift moves those assets into the adjacent
// tile at the right and bottom edges, most visibly on DKR's sliced title art.
inline constexpr std::string_view kLegacyCoordinateShift = "none";
inline constexpr std::string_view kLegacyCoordinatePolicy = "legacy-rice-no-shift";

enum class Variant {
    All,
    Rgb,
    Alpha,
};

struct ParsedName {
    std::string identity;
    Variant variant = Variant::All;
};

inline std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

inline bool is_hex_component(std::string_view value, std::size_t digits) {
    if (value.size() != digits)
        return false;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; });
}

inline bool is_decimal_component(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(),
                                         [](unsigned char character) { return std::isdigit(character) != 0; });
}

inline bool valid_identity(std::string_view identity) {
    std::array<std::string_view, 4> components{};
    std::size_t count = 0;
    std::size_t begin = 0;
    bool complete = false;
    while (begin <= identity.size() && count < components.size()) {
        const std::size_t separator = identity.find('#', begin);
        components[count++] =
            identity.substr(begin, separator == std::string_view::npos ? identity.size() - begin : separator - begin);
        if (separator == std::string_view::npos) {
            complete = true;
            break;
        }
        begin = separator + 1;
    }
    if (!complete)
        return false;
    if (count != 3 && count != 4)
        return false;
    return is_hex_component(components[0], 8) && is_decimal_component(components[1]) &&
           is_decimal_component(components[2]) && (count == 3 || is_hex_component(components[3], 8));
}

inline std::optional<ParsedName> parse_filename(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string file = lower_ascii(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
    if (!file.ends_with(".png"))
        return std::nullopt;
    file.resize(file.size() - 4);

    Variant variant;
    std::size_t suffix_length = 0;
    if (file.ends_with("_all")) {
        variant = Variant::All;
        suffix_length = 4;
    } else if (file.ends_with("_rgb")) {
        variant = Variant::Rgb;
        suffix_length = 4;
    } else if (file.ends_with("_a")) {
        variant = Variant::Alpha;
        suffix_length = 2;
    } else {
        return std::nullopt;
    }

    const std::size_t first_hash = file.find('#');
    if (first_hash == std::string::npos || first_hash + 1 >= file.size() - suffix_length) {
        return std::nullopt;
    }
    std::string identity = file.substr(first_hash + 1, file.size() - suffix_length - first_hash - 1);
    if (!valid_identity(identity))
        return std::nullopt;
    return ParsedName{std::move(identity), variant};
}

// The same domain-separated 64-bit alias is generated in the RT64 patch.
// It gives a Rice identity a stable native replacement key without requiring
// a game/ROM-specific handwritten hash table.
inline std::uint64_t native_alias(std::string_view identity) {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    constexpr std::string_view domain = "dkr-r:rice:";
    for (const unsigned char character : domain) {
        hash = (hash ^ character) * prime;
    }
    for (const unsigned char character : identity) {
        hash = (hash ^ static_cast<unsigned char>(std::tolower(character))) * prime;
    }
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

inline std::string native_alias_string(std::string_view identity) {
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(native_alias(identity)));
    return buffer;
}

inline bool safe_archive_path(std::string_view path) {
    if (path.empty() || path.starts_with('/') || path.starts_with('\\')) {
        return false;
    }
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t separator = path.find_first_of("/\\", begin);
        const auto component =
            path.substr(begin, separator == std::string_view::npos ? path.size() - begin : separator - begin);
        if (component == "..")
            return false;
        if (separator == std::string_view::npos)
            break;
        begin = separator + 1;
    }
    return true;
}

} // namespace dkr::runtime::rice_texture
