#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dkr::runtime::netplay {

// Isolated mathematical prototype, NOT wired into retail matrices or enabled
// by a setting. A renderer adapter + collision/vehicle/camera visual acceptance
// is required before this may affect a released frame. No RDRAM access here.
struct VisualPredictionIdentity {
    std::uint64_t match = 0;
    std::uint32_t scene = 0, input_epoch = 0, racer = 0, camera = 0, vehicle = 0;
    bool operator==(const VisualPredictionIdentity&) const = default;
};
struct VisualPredictionSample {
    VisualPredictionIdentity identity;
    // Velocity is explicitly world units/second, NOT retail units/tick.
    float vx = 0, vy = 0, vz = 0, steering = 0;
    bool local_owner = false, racing = false, discontinuity = false;
};
struct VisualPredictionOffset {
    float x = 0, y = 0, z = 0, yaw_radians = 0;
};
class LocalPresentationPrototype {
public:
    static constexpr bool kRendererQualified = false;
    void reset() { valid_ = false; offset_ = {}; }
    VisualPredictionOffset update(const VisualPredictionSample& sample,
                                  float seconds, bool experiment_enabled = false) {
        if (!experiment_enabled || !sample.local_owner || !sample.racing || sample.discontinuity ||
            !std::isfinite(seconds) || seconds < 0 || !std::isfinite(sample.vx) ||
            !std::isfinite(sample.vy) || !std::isfinite(sample.vz) || !std::isfinite(sample.steering)) {
            reset(); return {};
        }
        if (!valid_ || sample.identity != identity_) {
            reset(); identity_ = sample.identity; valid_ = true;
            return {}; // never carry an offset across a lifecycle/owner boundary
        }
        const float horizon = std::min(seconds, 0.05F);
        VisualPredictionOffset next{sample.vx * horizon, sample.vy * horizon,
            sample.vz * horizon, std::clamp(sample.steering, -1.0F, 1.0F) * horizon};
        const float distance = std::hypot(next.x, next.y, next.z);
        if (!std::isfinite(distance)) { reset(); return {}; }
        if (distance > 32.0F) {
            const float scale = 32.0F / distance;
            next.x *= scale; next.y *= scale; next.z *= scale;
        }
        // Bounded reconciliation; every output is sidecar-only. Camera and
        // racer must consume the SAME offset if an adapter is later approved.
        offset_.x += (next.x - offset_.x) * 0.5F;
        offset_.y += (next.y - offset_.y) * 0.5F;
        offset_.z += (next.z - offset_.z) * 0.5F;
        offset_.yaw_radians += (next.yaw_radians - offset_.yaw_radians) * 0.5F;
        return offset_;
    }
private:
    bool valid_ = false;
    VisualPredictionIdentity identity_{};
    VisualPredictionOffset offset_{};
};
} // namespace dkr::runtime::netplay
