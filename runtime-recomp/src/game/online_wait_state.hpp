#pragma once

#include <atomic>
#include <cstdint>

namespace dkr::runtime::netplay {

enum class OnlineWaitReason : std::uint8_t {
    None,
    Loading,
    RaceStart,
    Transition,
    RaceFinish,
    Recovery,
    ClientCatchUp,
    Cutscene,
};

// Presentation-only state for an authored simulation gate. A wait remains
// published until the authored timeline makes real progress or the session is
// reset; repeated attempts to service the same parked tick must not create a
// one-frame pulse that the renderer can miss.
class OnlineWaitState {
  public:
    void enter(OnlineWaitReason reason) {
        if (reason == OnlineWaitReason::None) {
            leave();
            return;
        }
        const OnlineWaitReason previous = reason_.exchange(reason, std::memory_order_acq_rel);
        if (previous != reason) {
            generation_.fetch_add(1U, std::memory_order_acq_rel);
        }
        if (previous == OnlineWaitReason::None)
            episode_.fetch_add(1U, std::memory_order_acq_rel);
    }

    void leave() {
        const OnlineWaitReason previous = reason_.exchange(OnlineWaitReason::None, std::memory_order_acq_rel);
        if (previous != OnlineWaitReason::None) {
            generation_.fetch_add(1U, std::memory_order_acq_rel);
        }
    }

    [[nodiscard]] OnlineWaitReason reason() const {
        return reason_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool active() const {
        return reason() != OnlineWaitReason::None;
    }

    [[nodiscard]] std::uint64_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }
    // Reason changes repaint, but do not restart a continuous wait's debounce.
    [[nodiscard]] std::uint64_t episode() const {
        return episode_.load(std::memory_order_acquire);
    }

  private:
    std::atomic<OnlineWaitReason> reason_{OnlineWaitReason::None};
    std::atomic<std::uint64_t> generation_{0U};
    std::atomic<std::uint64_t> episode_{0U};
};

} // namespace dkr::runtime::netplay
