#pragma once
#include "enhanced_water_policy.hpp"

namespace dkr::runtime::water::enhanced {
// Immutable world -> authored texture coordinates. Keep the phase separate
// from the retail scrolling accumulator so wrapping cannot reset the film.
struct FlowMapping {
    std::array<float, 4> u{}, v{};
    float direction = 0; // V sign that runs downhill; zero means unqualified.
    std::array<float, 2> at(Vec3 p) const {
        return {u[0] * p.x + u[1] * p.y + u[2] * p.z + u[3], v[0] * p.x + v[1] * p.y + v[2] * p.z + v[3]};
    }
};

inline FlowMapping flow_mapping(std::array<Vec3, 3> p, std::array<std::array<float, 2>, 3> uv,
                                std::array<float, 2> period) {
    FlowMapping result{};
    if (!valid_triangle(p[0], p[1], p[2]) || !std::isfinite(period[0]) || !std::isfinite(period[1]) || period[0] < 1 ||
        period[1] < 1 || period[0] > 2048 || period[1] > 2048)
        return result;
    for (const auto& tc : uv)
        for (float c : tc)
            if (!std::isfinite(c))
                return result;
    const Vec3 a{p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z};
    const Vec3 b{p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z};
    const auto dot = [](Vec3 x, Vec3 y) { return double(x.x) * y.x + double(x.y) * y.y + double(x.z) * y.z; };
    const double aa = dot(a, a), ab = dot(a, b), bb = dot(b, b), det = aa * bb - ab * ab;
    if (!std::isfinite(det) || det <= aa * bb * 1e-10)
        return result;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double da = (uv[1][axis] - uv[0][axis]) / period[axis];
        const double db = (uv[2][axis] - uv[0][axis]) / period[axis];
        const double ca = (da * bb - db * ab) / det, cb = (db * aa - da * ab) / det;
        auto& row = axis ? result.v : result.u;
        row = {float(ca * a.x + cb * b.x), float(ca * a.y + cb * b.y), float(ca * a.z + cb * b.z), 0};
        row[3] = uv[0][axis] / period[axis] - (row[0] * p[0].x + row[1] * p[0].y + row[2] * p[0].z);
        for (float c : row)
            if (!std::isfinite(c))
                return {};
    }
    const float vLength = std::sqrt(result.v[0] * result.v[0] + result.v[1] * result.v[1] + result.v[2] * result.v[2]);
    const Vec3 cross{result.u[1] * result.v[2] - result.u[2] * result.v[1],
                     result.u[2] * result.v[0] - result.u[0] * result.v[2],
                     result.u[0] * result.v[1] - result.u[1] * result.v[0]};
    if (vLength < 1e-7F || dot(cross, cross) < 1e-16)
        return {};
    // A level channel keeps authored +V. On a slope/vertical sheet, choose
    // the direction that decreases world height, even for mirrored UVs.
    result.direction = std::abs(result.v[1]) > vLength * 0.02F && result.v[1] > 0 ? -1.0F : 1.0F;
    return result;
}

inline bool flow_matches(const FlowMapping& mapping, Vec3 p, std::array<float, 2> uv, std::array<float, 2> period) {
    if (!mapping.direction)
        return false;
    const auto at = mapping.at(p);
    for (unsigned axis = 0; axis < 2; ++axis) {
        const float error = at[axis] - uv[axis] / period[axis];
        // Adjacent authored patches may differ by whole repeating tiles.
        if (!std::isfinite(error) || std::abs(error - std::round(error)) > 0.002F)
            return false;
    }
    return true;
}
} // namespace dkr::runtime::water::enhanced
