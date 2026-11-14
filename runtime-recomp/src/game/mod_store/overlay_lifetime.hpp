#pragma once
#include <atomic>
#include <cstdint>
namespace dkr::store::overlay {
struct Lifetime {
    std::atomic<std::uint64_t> submitted{0}, completed{0};
    void finish() noexcept {
        const auto value = submitted.load(std::memory_order_acquire);
        auto prior = completed.load(std::memory_order_relaxed);
        while (prior < value &&
               !completed.compare_exchange_weak(prior, value, std::memory_order_release, std::memory_order_relaxed)) {
        }
    }
    bool can_retire(std::uint64_t used, std::uint64_t drawing) const noexcept {
        return used < drawing && used <= completed.load(std::memory_order_acquire);
    }
};
} // namespace dkr::store::overlay
