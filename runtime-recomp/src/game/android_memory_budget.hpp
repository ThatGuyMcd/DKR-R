#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
namespace dkr::runtime::android_memory {
inline std::atomic<std::uint64_t> unused_cache_limit{512ULL * 1024 * 1024};
constexpr std::uint64_t limit_for_trim(int level) {
    // UI_HIDDEN (20) and BACKGROUND (40) are lifecycle, not urgent pressure.
    return (level == 15 || level >= 80) ? 64ULL * 1024 * 1024 :
           (level == 10 || level >= 60) ? 128ULL * 1024 * 1024 : 512ULL * 1024 * 1024;
}
inline void trim(int level) {
    const auto limit = limit_for_trim(level);
    auto previous = unused_cache_limit.load();
    while (previous > limit && !unused_cache_limit.compare_exchange_weak(previous, limit)) {}
}
}
