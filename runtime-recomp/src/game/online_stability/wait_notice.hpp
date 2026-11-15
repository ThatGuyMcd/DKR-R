#pragma once
// When the "WAITING FOR RACERS" notice is on screen.
//
// A race that is short of input stalls in bursts: a few hundred milliseconds
// blocked, one frame through, blocked again. Showing the notice only while
// blocked makes it flash. It appears after a wait has lasted long enough to
// be worth mentioning, and once up it stays until play has been smooth for a
// while, so a choppy stretch reads as one steady notice.
#include <chrono>
#include <optional>

namespace dkr::online_stability {

class WaitNotice {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kAppear{300}, kLinger{750};

    // `waiting`: something is being waited for right now. `immediate`: this
    // wait is a lobby launch stage, announced without the delay and cleared
    // the moment it ends. `restart`: it is a different wait from the one last
    // seen, so the delay starts over unless the notice is already up.
    bool visible(bool waiting, bool immediate, bool restart, Clock::time_point now) {
        if (waiting) {
            if (!since_ || (restart && !shown_))
                since_ = now;
            last_ = now;
            lingers_ = !immediate;
            shown_ = shown_ || immediate || now - *since_ >= kAppear;
        } else if (!shown_ || !lingers_ || now - last_ >= kLinger) {
            shown_ = false;
            since_.reset();
        }
        return shown_;
    }

  private:
    std::optional<Clock::time_point> since_;
    Clock::time_point last_{};
    bool shown_ = false, lingers_ = false;
};

} // namespace dkr::online_stability
