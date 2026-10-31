#pragma once
#include "modern_camera_policy.hpp"
#include <cmath>
#include <cstdint>

namespace dkr::runtime::live_fov {
struct Scene {
    std::uintptr_t image = 0;
    std::uint32_t generation = 0, header = 0;
    int revision = 0;
    bool operator==(const Scene&) const = default;
};
struct Frame {
    Scene scene;
    enhancements::PresentationProfile profile = enhancements::PresentationProfile::Accurate;
    int offset = 0, authored = 60;
    float current = 60;
    bool eligible = false, online = false;
};

// Owned by the guest thread. Observe settings even if the level itself has not
// changed; never add the offset to an already adjusted camera FOV. Once applied,
// leave subsequent retail camera changes alone until the next setting/scene.
class Tracker {
  public:
    template <class Apply> bool update(const Frame& frame, Apply apply) {
        if (!frame.eligible || frame.online || !frame.scene.image || frame.authored < 1 || frame.authored >= 120 ||
            !std::isfinite(frame.current) || frame.current <= 0 || frame.current >= 120)
            return false;
        const auto profile = enhancements::normalise_presentation_profile(frame.profile);
        const int offset = enhancements::clamp_fov_offset(frame.offset);
        const bool new_scene = !seen_ || scene_ != frame.scene;
        if (!new_scene && profile_ == profile && offset_ == offset && authored_ == frame.authored)
            return false;
        const bool restore = !new_scene && owned_ && profile == enhancements::PresentationProfile::Accurate;
        bool changed = false;
        if (profile == enhancements::PresentationProfile::Modern || restore) {
            // cam_set_fov accepts only 0 < FOV < 90. Keep the existing Modern
            // lower bound, and saturate the upper end instead of silently
            // losing a slider update on a level with a wider authored FOV.
            const float target =
                float(std::min(89, enhancements::effective_gameplay_fov(profile, frame.authored, offset)));
            if (std::fabs(frame.current - target) > 0.01F) {
                if (!apply(target))
                    return false; // Retry when the camera becomes ready.
                changed = true;
            }
        }
        seen_ = true;
        scene_ = frame.scene;
        profile_ = profile;
        offset_ = offset;
        authored_ = frame.authored;
        owned_ = profile == enhancements::PresentationProfile::Modern;
        return changed;
    }

  private:
    Scene scene_;
    enhancements::PresentationProfile profile_ = enhancements::PresentationProfile::Accurate;
    int offset_ = 0, authored_ = 0;
    bool seen_ = false, owned_ = false;
};
} // namespace dkr::runtime::live_fov
