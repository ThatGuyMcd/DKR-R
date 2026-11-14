#pragma once
#include "shared/rt64_water_params.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace RT64 {
// Conservative bounds of the LIGHT influence, not just the lava surface.
// Camera-inside/near-plane and invalid projections deliberately retain the
// viewport. Never make a light disappear merely because its source is offscreen.
inline std::array<float, 4> lavaLightingBounds(const interop::WaterDrawParams& draw) {
    const std::array<float, 4> full{float(draw.viewBounds.x), float(draw.viewBounds.y), float(draw.viewBounds.z),
                                    float(draw.viewBounds.w)};
    std::array<float, 4> bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
    bool found = false;
    for (unsigned light = 0; light < 16; ++light) {
        const auto& e = draw.material.shoreSegments[light];
        if (e.w <= 0)
            continue;
        const float radius = std::max(80.0F, float(e.w) * 3 + 180) * float(draw.material.skyColor.w);
        unsigned behind = 0;
        std::array<std::array<float, 2>, 8> points{};
        for (unsigned corner = 0; corner < 8; ++corner) {
            const auto clip = hlslpp::mul(hlslpp::float4(float(e.x) + (corner & 1 ? radius : -radius),
                                                         float(e.y) + (corner & 2 ? radius : -radius),
                                                         float(e.z) + (corner & 4 ? radius : -radius), 1),
                                          hlslpp::float4x4(draw.viewProjection));
            if (!std::isfinite(float(clip.w)))
                return full;
            if (clip.w <= 0.0001F) {
                ++behind;
                continue;
            }
            const float sx = float(clip.x / clip.w) * float(draw.viewportScale.x) + float(draw.viewportTranslate.x);
            const float sy = -float(clip.y / clip.w) * float(draw.viewportScale.y) + float(draw.viewportTranslate.y);
            const float x =
                ((sx / float(draw.screen.x) * 2 - 1) * float(draw.screenTransform.x) + float(draw.screenTransform.z)) *
                    0.5F +
                0.5F;
            const float y =
                ((-sy / float(draw.screen.y) * 2 + 1) * float(draw.screenTransform.y) + float(draw.screenTransform.w)) *
                    -0.5F +
                0.5F;
            points[corner] = {x * float(draw.viewportScale.w), y * float(draw.viewportTranslate.w)};
            if (!std::isfinite(points[corner][0]) || !std::isfinite(points[corner][1]))
                return full;
        }
        if (behind == 8)
            continue;
        if (behind)
            return full;
        for (auto point : points) {
            bounds[0] = std::min(bounds[0], point[0]);
            bounds[1] = std::min(bounds[1], point[1]);
            bounds[2] = std::max(bounds[2], point[0]);
            bounds[3] = std::max(bounds[3], point[1]);
        }
        found = true;
    }
    if (!found)
        return {full[0], full[1], full[0], full[1]};
    return {std::max(full[0], std::floor(bounds[0]) - 2), std::max(full[1], std::floor(bounds[1]) - 2),
            std::min(full[2], std::ceil(bounds[2]) + 2), std::min(full[3], std::ceil(bounds[3]) + 2)};
}
} // namespace RT64
