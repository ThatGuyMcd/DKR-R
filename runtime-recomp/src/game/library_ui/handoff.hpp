#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace dkr::library_ui {
inline std::atomic<bool> runtime_intro_visible{false};
inline bool initial_sync(bool online, bool expects_owned, bool owned, bool admitting, std::uint64_t epoch) {
    return online && expects_owned && (!owned || admitting || epoch == 0);
}
inline float handoff_blend(double elapsed) {
    const float t = float(std::clamp(elapsed / .35, 0.0, 1.0));
    return t * t * (3.F - 2.F * t);
}
class RuntimeIntro {
    bool active_ = false, online_ = false, ready_ = false;
    bool shown_ = false;
    double ready_at_ = 0;
    double seconds_ = 0;

  public:
    void begin(bool online, double seconds = 0) {
        active_ = true;
        shown_ = true;
        online_ = online;
        ready_ = false;
        ready_at_ = 0;
        seconds_ = seconds;
        runtime_intro_visible = true;
    }
    void clear(bool reset = true) {
        active_ = false;
        ready_ = false;
        if (reset)
            shown_ = false;
        runtime_intro_visible = false;
    }
    bool active() const {
        return active_;
    }
    bool may_begin() const {
        return !shown_;
    }
    bool online() const {
        return online_;
    }
    double advance(float delta) {
        seconds_ += std::clamp(delta, 0.F, .1F);
        return seconds_;
    }
    // Called only by presentation. These signals never release a game peer.
    float opacity(double now, bool verified_first_frame) {
        if (!active_)
            return 0;
        if (verified_first_frame && !ready_) {
            ready_ = true;
            ready_at_ = now;
        }
        const float alpha = ready_ ? 1.F - handoff_blend(now - ready_at_) : 1.F;
        if (alpha <= 0)
            clear(false);
        return alpha;
    }
};
inline RuntimeIntro runtime_intro;
} // namespace dkr::library_ui
