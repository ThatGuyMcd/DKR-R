#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace dkr::online_presentation::test_receive_delay {
// Armed only by the existing CLI check after it validates a new, isolated
// profile. Normal sessions cannot enable this through environment variables.
inline std::atomic<unsigned> delay_ms{0},jitter_ms{0};
inline void arm(unsigned delay,unsigned jitter) noexcept {
    jitter_ms.store(jitter<=50?jitter:0,std::memory_order_relaxed);
    delay_ms.store(delay<=150?delay:0,std::memory_order_relaxed);
}
using Clock=std::chrono::steady_clock;
inline Clock::time_point ready_at() noexcept {
    const auto delay=delay_ms.load(std::memory_order_relaxed);
    if(!delay)return {};
    const auto jitter=jitter_ms.load(std::memory_order_relaxed);
    static thread_local std::uint32_t sequence=0;
    const int offset=jitter?int((++sequence*2654435761U)%(2*jitter+1))-int(jitter):0;
    return Clock::now()+std::chrono::milliseconds(std::max(0,int(delay)+offset));
}
inline bool ready(Clock::time_point at) noexcept {return at==Clock::time_point{}||Clock::now()>=at;}
}
namespace dkr::online_presentation::test_fixture {
// Armed only through the existing new/empty-profile CLI qualification path.
inline std::atomic<bool> missing_gpu_target{false};
}
