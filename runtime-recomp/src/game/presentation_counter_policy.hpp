#pragma once

#include <cstdint>

namespace dkr::runtime::presentation_counter {

struct Snapshot {
    std::uint64_t value = 0U;
    bool initialized = false;
};

// Presentation happens on RT64's queue thread. Sample its monotonic completed
// swapchain counter without inventing a frame for the caller's VI callback.
inline std::uint64_t consume_delta(std::uint64_t completed, Snapshot& snapshot) {
    if (!snapshot.initialized || completed < snapshot.value) {
        snapshot.value = completed;
        snapshot.initialized = true;
        return 0U;
    }
    const std::uint64_t delta = completed - snapshot.value;
    snapshot.value = completed;
    return delta;
}

// A VI callback only says that the game requested a screen update. In RT64's
// early-present path the corresponding queue entry can still be empty until a
// workload reaches the swap chain. Repeating before that first completion can
// select an uninitialised Present whose framebuffer storage has no backing
// bytes. Only replay an image after the queue has proved that one was actually
// presented.
inline bool can_repeat_last_present(std::uint64_t vi_callbacks, bool queue_ready,
                                    std::uint64_t completed_presentations) {
    return vi_callbacks != 0U && queue_ready && completed_presentations != 0U;
}

} // namespace dkr::runtime::presentation_counter
