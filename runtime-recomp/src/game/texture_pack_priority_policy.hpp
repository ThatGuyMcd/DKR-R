#pragma once

#include "runtime_texture_packs.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace dkr::runtime::texture_priority {

using Preferences = std::map<std::string, std::size_t>;

constexpr std::size_t rank(std::size_t value) {
    return value == 0U ? std::numeric_limits<std::size_t>::max() : value;
}

static_assert(rank(1U) < rank(2U));
static_assert(rank(0U) > rank(1U));

inline bool higher(const texture_packs::PackInfo& left,
                   const texture_packs::PackInfo& right) {
    if (rank(left.priority) != rank(right.priority)) {
        return rank(left.priority) < rank(right.priority);
    }
    return left.id < right.id;
}

inline std::string folded_name(const std::string& name) {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

// Number all installed packs, including inactive/hidden/track-owned packs.
// New packs go beneath saved layers. On first migration reverse the old
// alphabetical loading order, retaining the old winner for existing clashes.
inline bool normalize(std::vector<texture_packs::PackInfo>& packs,
                      Preferences& preferences) {
    struct Entry {
        texture_packs::PackInfo* pack;
        std::size_t saved;
        std::string name;
    };
    std::vector<Entry> ordered;
    ordered.reserve(packs.size());
    for (auto& pack : packs) {
        const auto saved = preferences.find(pack.id);
        ordered.push_back({&pack, saved == preferences.end() ? 0U : saved->second,
                           folded_name(pack.name)});
    }
    std::sort(ordered.begin(), ordered.end(), [](const Entry& left, const Entry& right) {
        if (rank(left.saved) != rank(right.saved)) {
            return rank(left.saved) < rank(right.saved);
        }
        if (left.name != right.name) return left.name > right.name;
        return left.pack->id > right.pack->id;
    });
    bool changed = false;
    for (std::size_t index = 0U; index < ordered.size(); ++index) {
        auto& pack = *ordered[index].pack;
        pack.priority = index + 1U;
        const auto previous = preferences.find(pack.id);
        changed = changed || previous == preferences.end() ||
                  previous->second != pack.priority;
        preferences[pack.id] = pack.priority;
    }
    return changed;
}

// Move one layer, shifting the intervening layers. Duplicate priority numbers
// never arise, and enabling/disabling/hiding a pack never moves another layer.
inline bool move(std::vector<texture_packs::PackInfo>& packs,
                 Preferences& preferences, const std::string& id,
                 std::size_t requested) {
    const auto selected = std::find_if(packs.begin(), packs.end(),
        [&](const auto& pack) { return pack.id == id; });
    if (selected == packs.end() || requested == 0U || requested > packs.size()) {
        return false;
    }
    const std::size_t previous = selected->priority;
    if (previous == 0U || previous > packs.size()) return false;
    if (previous == requested) return true;
    for (auto& pack : packs) {
        if (pack.id == id) pack.priority = requested;
        else if (requested < previous && pack.priority >= requested && pack.priority < previous) {
            ++pack.priority;
        } else if (requested > previous && pack.priority > previous && pack.priority <= requested) {
            --pack.priority;
        }
        preferences[pack.id] = pack.priority;
    }
    return true;
}

// RT64 resolves replacements from last to first, so submit low priority first.
inline std::vector<const texture_packs::PackInfo*> load_order(
    const std::vector<texture_packs::PackInfo>& packs) {
    std::vector<const texture_packs::PackInfo*> result;
    result.reserve(packs.size());
    for (const auto& pack : packs) {
        if (pack.enabled && pack.compatible && !pack.hidden) result.push_back(&pack);
    }
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return higher(*right, *left);
    });
    return result;
}

} // namespace dkr::runtime::texture_priority
