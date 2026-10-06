#pragma once

#include "runtime_texture_packs.hpp"
#include "texture_pack_priority_policy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::runtime::texture_browser {

enum class SortMode : int {
    NameAscending = 0,
    NameDescending,
    SizeLargest,
    SizeSmallest,
    ImportNewest,
    ImportOldest,
    Type,
    Priority,
};

enum class StateFilter : int {
    All = 0,
    Active,
    Inactive,
};

enum class CompatibilityFilter : int {
    All = 0,
    Compatible,
    Incompatible,
};

enum class VisibilityFilter : int {
    Visible = 0,
    All,
    Hidden,
    TrackPacks,   // only the <track>-hd.zip packs, for auditing
};

struct Filters {
    std::string query;
    StateFilter state = StateFilter::All;
    CompatibilityFilter compatibility = CompatibilityFilter::All;
    VisibilityFilter visibility = VisibilityFilter::Visible;
    texture_packs::Format format = texture_packs::Format::Unknown;
};

inline std::string lower_ascii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

inline std::string format_search_text(texture_packs::Format format) {
    switch (format) {
    case texture_packs::Format::NativeRt64: return "native rt64";
    case texture_packs::Format::RiceRt64: return "rice rt64 bridge";
    case texture_packs::Format::LegacyRice: return "legacy rice";
    case texture_packs::Format::LegacyJabo: return "legacy jabo unsupported";
    default: return "unknown";
    }
}

inline bool matches(const texture_packs::PackInfo& pack,
                    const Filters& filters) {
    const std::string query = lower_ascii(filters.query);
    if (!query.empty()) {
        const std::string haystack = lower_ascii(
            pack.name + " " + format_search_text(pack.format));
        if (haystack.find(query) == std::string::npos) return false;
    }
    // A pack that shipped with a custom track is filed against that track, not
    // browsed here. It shows only under the explicit "Track packs" filter; the
    // ordinary Visible / All / Hidden views never list it.
    const bool is_track_pack =
        pack.origin == texture_packs::Origin::TrackPack;
    if (filters.visibility == VisibilityFilter::TrackPacks) {
        if (!is_track_pack) return false;
    } else if (is_track_pack) {
        return false;
    }
    if (filters.state == StateFilter::Active && !pack.enabled) return false;
    if (filters.state == StateFilter::Inactive && pack.enabled) return false;
    if (filters.compatibility == CompatibilityFilter::Compatible &&
        !pack.compatible) {
        return false;
    }
    if (filters.compatibility == CompatibilityFilter::Incompatible &&
        pack.compatible) {
        return false;
    }
    if (filters.visibility == VisibilityFilter::Visible && pack.hidden) {
        return false;
    }
    if (filters.visibility == VisibilityFilter::Hidden && !pack.hidden) {
        return false;
    }
    return filters.format == texture_packs::Format::Unknown ||
           pack.format == filters.format;
}

inline void sort(std::vector<texture_packs::PackInfo>& packs, SortMode mode) {
    const auto name_less = [](const texture_packs::PackInfo& left,
                              const texture_packs::PackInfo& right) {
        const std::string left_name = lower_ascii(left.name);
        const std::string right_name = lower_ascii(right.name);
        if (left_name != right_name) return left_name < right_name;
        return left.id < right.id;
    };
    std::stable_sort(packs.begin(), packs.end(),
        [&](const texture_packs::PackInfo& left,
            const texture_packs::PackInfo& right) {
            switch (mode) {
            case SortMode::Priority:
                return texture_priority::higher(left, right);
            case SortMode::NameDescending:
                return name_less(right, left);
            case SortMode::SizeLargest:
                if (left.managed_size_bytes != right.managed_size_bytes) {
                    return left.managed_size_bytes > right.managed_size_bytes;
                }
                break;
            case SortMode::SizeSmallest:
                if (left.managed_size_bytes != right.managed_size_bytes) {
                    return left.managed_size_bytes < right.managed_size_bytes;
                }
                break;
            case SortMode::ImportNewest:
                if (left.imported_at_unix_seconds !=
                    right.imported_at_unix_seconds) {
                    return left.imported_at_unix_seconds >
                           right.imported_at_unix_seconds;
                }
                break;
            case SortMode::ImportOldest:
                if (left.imported_at_unix_seconds !=
                    right.imported_at_unix_seconds) {
                    return left.imported_at_unix_seconds <
                           right.imported_at_unix_seconds;
                }
                break;
            case SortMode::Type:
                if (left.format != right.format) {
                    return static_cast<int>(left.format) <
                           static_cast<int>(right.format);
                }
                break;
            case SortMode::NameAscending:
            default:
                break;
            }
            return name_less(left, right);
        });
}

inline std::vector<texture_packs::PackInfo> select(
    const std::vector<texture_packs::PackInfo>& packs,
    const Filters& filters, SortMode mode) {
    std::vector<texture_packs::PackInfo> selected;
    selected.reserve(packs.size());
    std::copy_if(packs.begin(), packs.end(), std::back_inserter(selected),
                 [&](const texture_packs::PackInfo& pack) {
                     return matches(pack, filters);
                 });
    sort(selected, mode);
    return selected;
}

inline int responsive_column_count(float available_width, float gap,
                                   float minimum_card_width = 260.0F) {
    if (!std::isfinite(available_width) || available_width <= 0.0F) return 1;
    const float safe_gap = std::max(gap, 0.0F);
    const float safe_minimum = std::max(minimum_card_width, 1.0F);
    return std::max(1, static_cast<int>(
        std::floor((available_width + safe_gap) /
                   (safe_minimum + safe_gap))));
}

} // namespace dkr::runtime::texture_browser
