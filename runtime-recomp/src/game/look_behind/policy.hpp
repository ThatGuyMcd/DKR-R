#pragma once
#include <atomic>
#include <cmath>
#include <initializer_list>
namespace dkr::runtime::look_behind {
inline std::atomic<bool> enabled{true};
template <class Snapshot, class Pose> bool pose(const Snapshot& s, Pose& p) {
    if (!enabled.load() || !(s.flags & 1U) || (s.flags & 2U) || s.vehicle > 2)
        return false;
    p = {};
    p.api = 1;
    p.size = sizeof(p);
    p.view = s.view;
    p.mode = 1;
    p.racer_id = s.racer_id;
    p.racer_generation = s.racer_generation;
    p.cut = 1;
    for (unsigned a = 0; a < 3; ++a)
        p.position[a] = a == 1 ? s.original_position[a] : 2 * s.position[a] - s.original_position[a];
    constexpr float pi = 3.14159265358979323846F;
    p.yaw = std::remainder(s.original_yaw + pi, 2 * pi);
    p.pitch = s.original_pitch;
    p.roll = s.original_roll;
    p.vertical_fov = s.original_fov;
    p.near_plane = s.original_near;
    p.far_plane = s.original_far;
    for (const float v : {p.position[0], p.position[1], p.position[2], p.yaw, p.pitch, p.roll})
        if (!std::isfinite(v))
            return false;
    return p.vertical_fov > 0 && p.vertical_fov < 3 && p.near_plane > 0 && p.far_plane > p.near_plane;
}
} // namespace dkr::runtime::look_behind
