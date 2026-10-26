#pragma once
#include <chrono>
#include <cstdint>

namespace dkr::runtime::netplay {
// Route negotiation already has a 15-second budget. Permit several attempts,
// while retaining the existing 90-second hard ceiling. Only authored/stage
// progress extends the idle budget; duplicate packets/heartbeats do not.
struct ProgressBudget {
    using Clock = std::chrono::steady_clock;
    Clock::time_point started{}, progressed{};
    std::uint64_t marker = 0;
    void start(std::uint64_t value, Clock::time_point now = Clock::now()) {
        started = progressed = now;
        marker = value;
    }
    bool expired(std::uint64_t value, Clock::time_point now = Clock::now()) {
        if (value != marker) {
            marker = value;
            progressed = now;
        }
        return now - progressed >= std::chrono::seconds(45) || now - started >= std::chrono::seconds(90);
    }
};
} // namespace dkr::runtime::netplay
