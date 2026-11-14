#pragma once
#include <cstdint>

namespace dkr_scene_work {
// Only the confirmed, GPU-drained scene transaction gets loader headroom.
// A modded race can construct eight unique racers, their resident model LODs,
// animations, attachment sprites and textures in ONE such transaction. The
// old menu-sized ceiling was shared by that entire load, not by each asset.
// These are finite work limits, never timeout/retry loops or unmetered calls.
inline constexpr std::uint64_t PreviewOperations = 40000000ULL;
inline constexpr std::uint64_t ConfirmedOperations = 256000000ULL;
inline constexpr std::uint64_t limit(bool background) noexcept {
    return background ? PreviewOperations : ConfirmedOperations;
}
static_assert(ConfirmedOperations > PreviewOperations && ConfirmedOperations < 1000000000ULL);
} // namespace dkr_scene_work
