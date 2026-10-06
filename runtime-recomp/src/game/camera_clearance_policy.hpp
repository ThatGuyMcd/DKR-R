#pragma once
#include "camera_clearance_metadata.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dkr::runtime::presentation {

// The retail camera uses near=10, far=15000 (both supported revisions).
// Preserve its near-plane footprint, not a fixed distance that gets wider
// when Modern expands the aspect/FOV. Accurate and non-world matrices bypass
// this entirely. No position, X/Y projection or far distance is changed.
struct WorldProjection {
    float authored_fov = 0.0F;
    float effective_fov = 0.0F;
    std::uint8_t viewport_layout = 0U;
    bool operator==(const WorldProjection&) const = default;
};

inline float world_near_distance(WorldProjection projection, float window_aspect) {
    constexpr float authored_near = 10.0F;
    if (!std::isfinite(projection.authored_fov) ||
        !std::isfinite(projection.effective_fov) ||
        projection.authored_fov < 1.0F || projection.authored_fov >= 120.0F ||
        projection.effective_fov < 1.0F || projection.effective_fov >= 120.0F ||
        projection.viewport_layout > 3 || !std::isfinite(window_aspect) ||
        window_aspect <= 0.0F) return authored_near;
    constexpr double half_degree = 3.14159265358979323846 / 360.0;
    const double vertical = std::tan(projection.effective_fov * half_degree) /
        std::tan(projection.authored_fov * half_degree);
    // Two-player horizontal split doubles BOTH displayed and authored aspect.
    // Cancelling that common factor avoids applying the split expansion twice.
    const double horizontal = vertical * window_aspect / (4.0 / 3.0);
    const double growth = std::max({1.0, vertical, horizontal});
    return std::isfinite(growth)
        ? static_cast<float>(std::clamp(authored_near / growth, 0.5, 10.0))
        : authored_near;
}

struct WorldDepthRemap {
    double z_scale = 1.0;
    double w_scale = 0.0;
    bool active = false;
};

inline WorldDepthRemap world_depth_remap(WorldProjection projection, float aspect) {
    const double near_distance = world_near_distance(projection, aspect);
    if (near_distance >= 10.0) return {};
    constexpr double far_distance = 15000.0, old_near = 10.0;
    constexpr double old_a = (old_near + far_distance) / (old_near - far_distance);
    constexpr double old_b = 2.0 * old_near * far_distance / (old_near - far_distance);
    const double new_a = (near_distance + far_distance) / (near_distance - far_distance);
    const double new_b = 2.0 * near_distance * far_distance / (near_distance - far_distance);
    const double scale = new_b / old_b;
    // Row-vector combined MVP: Z' = scale*Z + (scale*Aold-Anew)*W.
    return {scale, scale * old_a - new_a, true};
}

template<class Matrix>
inline bool apply_world_depth_remap(Matrix& matrix, WorldDepthRemap remap) {
    if (!remap.active || !std::isfinite(remap.z_scale) ||
        !std::isfinite(remap.w_scale)) return false;
    std::array<float, 4> depth{};
    bool perspective = false;
    for (unsigned row = 0; row < 4; ++row) {
        const float z = static_cast<float>(matrix[row][2]);
        const float w = static_cast<float>(matrix[row][3]);
        if (row < 3 && w != 0.0F) perspective = true;
        depth[row] = static_cast<float>(remap.z_scale * z + remap.w_scale * w);
        if (!std::isfinite(z) || !std::isfinite(w) || !std::isfinite(depth[row]))
            return false; // Transactional: never partially modify a matrix.
    }
    // Affine sprite-corner matrices are not combined MVPs. Their world anchor
    // already has corrected depth; transforming them would apply it twice.
    if (!perspective) return false;
    for (unsigned row = 0; row < 4; ++row) matrix[row][2] = depth[row];
    return true;
}
} // namespace dkr::runtime::presentation
