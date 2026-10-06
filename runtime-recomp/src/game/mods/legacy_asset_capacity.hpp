#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace dkr::mods {
// Retail caches are fixed-size even though additive mods append valid asset
// IDs. Size their metadata from the immutable boot namespace, never from the
// current scene, player selection, or local renderer settings. Replay peers
// therefore allocate the same cache layout. Unmodified games retain retail
// allocations and limits exactly.
constexpr std::array<std::uint32_t,4> legacy_asset_cache_capacities(
    bool augmented, std::uint32_t textures_2d, std::uint32_t textures_3d,
    std::uint32_t sprites, std::uint32_t models) {
    if(!augmented)return {700,100,70,100};
    // Admission already bounds each section to 32767 records. One spare
    // model/sprite cell preserves retail's strict post-increment comparison.
    const auto model_cells=std::max(70U,models+1U);
    return {std::max(700U,textures_2d+textures_3d),
        std::max(100U,sprites+1U),model_cells,std::max(100U,model_cells)};
}
}
