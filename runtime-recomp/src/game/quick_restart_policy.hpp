#pragma once

#include <cstdint>

namespace dkr::runtime::quick_restart {

inline constexpr std::int32_t kCentralAreaWorld = 0;
inline constexpr std::int32_t kDefaultRaceType = 0;
inline constexpr std::int32_t kBossRaceType = 8;
inline constexpr std::int32_t kChallengeRaceMask = 0x40;

struct State {
    bool modern_profile = false;
    bool paused = false;
    bool post_race = false;
    bool level_loading = false;
    std::int32_t trophy_race_world = 0;
    std::int32_t world_id = kCentralAreaWorld;
    std::int32_t race_type = kDefaultRaceType;
    bool tracks_mode = false;
};

constexpr bool retail_restart_available(const State& state) {
    if (!state.modern_profile || state.paused || state.post_race || state.level_loading ||
        state.trophy_race_world != 0) {
        return false;
    }

    if (state.world_id > kCentralAreaWorld && state.race_type != kBossRaceType) {
        return state.race_type == kDefaultRaceType || (state.race_type & kChallengeRaceMask) != 0;
    }

    return !state.tracks_mode && state.race_type == kBossRaceType;
}

} // namespace dkr::runtime::quick_restart
