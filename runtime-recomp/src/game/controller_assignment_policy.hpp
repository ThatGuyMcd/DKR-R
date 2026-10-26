#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace dkr::runtime::controllers {

inline constexpr std::size_t kPlayerCount = 4;

enum class AssignmentMode : int {
    Automatic = 0,
    Manual = 1,
};

struct Device {
    int instance = -1;
    std::string persistent_key;
};

using DesiredAssignments = std::array<std::string, kPlayerCount>;
using ResolvedAssignments = std::array<int, kPlayerCount>;

inline ResolvedAssignments resolve(const DesiredAssignments& desired, const std::vector<Device>& devices) {
    ResolvedAssignments result{};
    result.fill(-1);
    std::vector<bool> used(devices.size(), false);
    for (std::size_t player = 0; player < kPlayerCount; ++player) {
        if (desired[player].empty()) {
            continue;
        }
        for (std::size_t device = 0; device < devices.size(); ++device) {
            if (!used[device] && devices[device].persistent_key == desired[player]) {
                result[player] = devices[device].instance;
                used[device] = true;
                break;
            }
        }
    }
    return result;
}

inline bool claim(DesiredAssignments& desired, std::size_t player, const std::string& key) {
    if (player >= kPlayerCount || key.empty()) {
        return false;
    }
    const std::string previous = desired[player];
    const auto owner = std::find(desired.begin(), desired.end(), key);
    if (owner != desired.end() && static_cast<std::size_t>(owner - desired.begin()) != player) {
        *owner = previous;
    }
    desired[player] = key;
    return true;
}

inline bool clear(DesiredAssignments& desired, std::size_t player) {
    if (player >= kPlayerCount) {
        return false;
    }
    desired[player].clear();
    return true;
}

inline void populate_automatic_claims(DesiredAssignments& desired, const std::vector<Device>& devices) {
    for (const Device& device : devices) {
        if (device.persistent_key.empty() ||
            std::find(desired.begin(), desired.end(), device.persistent_key) != desired.end()) {
            continue;
        }
        const auto empty = std::find(desired.begin(), desired.end(), std::string{});
        if (empty == desired.end()) {
            break;
        }
        *empty = device.persistent_key;
    }
}

} // namespace dkr::runtime::controllers
