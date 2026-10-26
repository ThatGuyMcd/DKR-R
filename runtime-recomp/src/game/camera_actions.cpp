#include "camera_actions.hpp"
#include <algorithm>
#include <cmath>

namespace dkr::runtime::camera {
namespace {
constexpr unsigned KnownActions = DKR_VIEW_LOOK_BACK | DKR_VIEW_RECENTER | DKR_VIEW_CYCLE | DKR_VIEW_ZOOM_IN |
                                  DKR_VIEW_ZOOM_OUT | DKR_VIEW_FIRST_PERSON | DKR_VIEW_XR_RECENTER;
float finite_axis(float value) {
    return std::isfinite(value) ? std::clamp(value, -1.F, 1.F) : 0;
}
} // namespace
void Actions::publish(unsigned profile, const ActionSample& sample) {
    if (profile >= 4)
        return;
    std::scoped_lock lock(mutex_);
    auto& p = ports_[profile];
    auto next = sample;
    next.held &= KnownActions;
    next.orbit_x = finite_axis(next.orbit_x);
    next.orbit_y = finite_axis(next.orbit_y);
    next.zoom = finite_axis(next.zoom);
    if (p.latest.blocked && !next.blocked) {
        p.suppressed |= next.held;
        p.neutral = true;
    }
    // Closing UI while holding a button must not trigger a camera action.
    if (next.blocked || !next.connected) {
        p.suppressed |= next.held;
        p.pressed = 0;
        p.mouse_x = p.mouse_y = 0;
        next.orbit_x = next.orbit_y = next.zoom = 0;
    }
    p.suppressed &= next.held;
    next.held &= ~p.suppressed;
    if (next.blocked || !next.connected)
        next.held = 0;
    if (p.neutral) {
        if (!next.blocked && std::abs(next.orbit_x) < 0.15F && std::abs(next.orbit_y) < 0.15F)
            p.neutral = false;
        else
            next.orbit_x = next.orbit_y = 0;
    }
    p.pressed |= next.held & ~p.latest.held;
    p.latest = next;
}
void Actions::mouse(unsigned profile, float x, float y) {
    if (profile >= 4 || !std::isfinite(x) || !std::isfinite(y))
        return;
    std::scoped_lock lock(mutex_);
    auto& p = ports_[profile];
    if (p.latest.blocked || !p.latest.connected)
        return;
    p.mouse_x = std::clamp(p.mouse_x + x, -4096.F, 4096.F);
    p.mouse_y = std::clamp(p.mouse_y + y, -4096.F, 4096.F);
}
DkrViewInput Actions::consume(unsigned profile, float dt) {
    DkrViewInput out{};
    out.api = 1;
    out.size = sizeof(out);
    out.blocked = 1;
    if (profile >= 4 || !std::isfinite(dt) || dt <= 0 || dt > 0.1F)
        return out;
    std::scoped_lock lock(mutex_);
    auto& p = ports_[profile];
    out.blocked = p.latest.blocked || !p.latest.connected;
    if (!out.blocked) {
        out.held = p.latest.held;
        out.pressed = p.pressed;
        // Mouse is a delta, stick is a rate. Consume the delta once per batch,
        // not once per eye. The mod applies its sensitivity and dt afterward.
        out.orbit_x = finite_axis(p.latest.orbit_x + p.mouse_x * 0.002F / dt);
        out.orbit_y = finite_axis(p.latest.orbit_y + p.mouse_y * 0.002F / dt);
        out.zoom = p.latest.zoom;
    }
    p.pressed = 0;
    p.mouse_x = p.mouse_y = 0;
    return out;
}
void Actions::reset() {
    std::scoped_lock lock(mutex_);
    ports_ = {};
}
} // namespace dkr::runtime::camera
