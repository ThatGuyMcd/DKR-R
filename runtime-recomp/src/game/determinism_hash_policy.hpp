#pragma once

#include <cstdint>

namespace dkr::runtime::netplay {

// Boot, logo, menus and character select legitimately contain machine-local
// renderer/audio work. Arm validation only after load_level_game has completed
// the authoritative racer/track setup, then allow the new scene to settle
// before checking at a fixed cadence.
inline constexpr std::uint32_t kDeterminismGraceFrames = 12U;
inline constexpr std::uint32_t kDeterminismHashInterval = 3U;
// Portable racer state is an exceptional recovery checkpoint, not a video-
// frame stream. Three authored ticks bounds a requested recovery to a 100 ms
// grid without putting bulk snapshot traffic on normal gameplay frames.
inline constexpr std::uint32_t kAuthorityCheckpointInterval = 3U;
inline constexpr std::uint32_t kDeterminismNotArmed = 0xFFFFFFFFU;

constexpr std::uint32_t first_determinism_hash_frame(std::uint32_t race_ready_frame) {
    return race_ready_frame + kDeterminismGraceFrames;
}

constexpr bool determinism_race_ready(std::uint32_t active_players, std::uint32_t racer_count,
                                      std::uint8_t expected_players) {
    return expected_players >= 2U && expected_players <= 4U && active_players == expected_players &&
           racer_count >= expected_players && racer_count <= 10U;
}

constexpr bool should_submit_determinism_hash(std::uint32_t frame, std::uint32_t first_frame) {
    return first_frame != kDeterminismNotArmed && frame >= first_frame &&
           ((frame - first_frame) % kDeterminismHashInterval) == 0U;
}

constexpr bool should_publish_authority_checkpoint(std::uint32_t completed_frame, std::uint32_t first_hash_frame) {
    return first_hash_frame != kDeterminismNotArmed && (completed_frame % kAuthorityCheckpointInterval) == 0U;
}

} // namespace dkr::runtime::netplay
