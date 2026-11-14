#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace dkr::runtime::launcher {

struct BackgroundTile {
    float left, top, right, bottom;
    float u0, v0, u1, v1;
    bool mirrored;
};

// Clip both geometry and UVs BEFORE submission. SDL's software flipped-copy
// path otherwise transforms entire (even invisible) tiles before clipping.
inline std::vector<BackgroundTile> background_tiles(float width, float height, float tile_width, float tile_height,
                                                    double scroll_distance) {
    std::vector<BackgroundTile> result;
    if (!(width > 0 && height > 0 && tile_width > 0 && tile_height > 0) ||
        !std::isfinite(width + height + tile_width + tile_height) || !std::isfinite(scroll_distance))
        return result;
    const double phase = std::fmod(std::max(scroll_distance, 0.0), static_cast<double>(tile_width) * 2.0);
    const double top = (static_cast<double>(height) - tile_height) * 0.5;
    const double bottom = std::min(top + tile_height, static_cast<double>(height));
    const double clipped_top = std::max(top, 0.0);
    int index = static_cast<int>(std::floor(-phase / tile_width));
    // Production tiles span almost two viewport heights; the bound also keeps
    // malformed/tiny extents from turning a diagnostic into an unbounded loop.
    for (int count = 0; count < 4096; ++count, ++index) {
        const double left = index * static_cast<double>(tile_width) + phase;
        if (left >= width)
            break;
        const double right = left + tile_width;
        if (right <= 0.0)
            continue;
        const double clipped_left = std::max(left, 0.0);
        const double clipped_right = std::min(right, static_cast<double>(width));
        result.push_back({static_cast<float>(clipped_left), static_cast<float>(clipped_top),
                          static_cast<float>(clipped_right), static_cast<float>(bottom),
                          static_cast<float>((clipped_left - left) / tile_width),
                          static_cast<float>((clipped_top - top) / tile_height),
                          static_cast<float>((clipped_right - left) / tile_width),
                          static_cast<float>((bottom - top) / tile_height), index % 2 != 0});
    }
    return result;
}

} // namespace dkr::runtime::launcher
