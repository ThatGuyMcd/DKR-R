#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace dkr::runtime::launcher {

inline int refresh_target(bool visible, bool focused, bool recent_input, bool recent_controller_input, int display_hz) {
    (void)recent_input;
    if (!visible)
        return 0;
    const int interactive = std::clamp(display_hz, 30, 60);
    // A visible controller-operated launcher may have no keyboard focus under
    // a compositor. This changes redraw policy only, never native focus/input.
    if (recent_controller_input)
        return interactive;
    if (!focused)
        return 10;
    return interactive;
}

class FrameSchedule {
  public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    void started(Time now, int target_hz) {
        const auto interval = std::chrono::microseconds(1000000 / std::max(target_hz, 1));
        // Preserve the timebase across SDL's whole-millisecond wait rounding,
        // otherwise a 60 Hz target gradually becomes a 57-58 Hz cap. Never
        // replay missed frames after a slow draw or a blocked service call.
        next_frame_ = has_frame_ && target_hz == target_hz_ && now < next_frame_ + interval ? next_frame_ + interval
                                                                                            : now + interval;
        last_frame_ = now;
        target_hz_ = target_hz;
        has_frame_ = true;
    }
    int wait_ms(Time now, int target_hz) const {
        // Services continue independently of rendering, even while hidden.
        if (target_hz <= 0)
            return 50;
        if (!has_frame_)
            return 0;
        const auto due =
            target_hz == target_hz_ ? next_frame_ : last_frame_ + std::chrono::microseconds(1000000 / target_hz);
        if (now >= due)
            return 0;
        return static_cast<int>(
            std::min<std::int64_t>(50, std::chrono::ceil<std::chrono::milliseconds>(due - now).count()));
    }

  private:
    Time last_frame_{};
    Time next_frame_{};
    int target_hz_ = 0;
    bool has_frame_ = false;
};

} // namespace dkr::runtime::launcher
