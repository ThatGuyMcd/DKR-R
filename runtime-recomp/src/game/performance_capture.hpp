#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace dkr::runtime::performance_capture {
using Clock = std::chrono::steady_clock;
inline std::int64_t now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
constexpr bool in_window(std::int64_t now, std::int64_t deadline) { return deadline > 0 && now < deadline; }
inline std::atomic<std::int64_t> deadline_ns{0};
inline bool active() { const auto end = deadline_ns.load(std::memory_order_relaxed); return end != 0 && in_window(now_ns(), end); }
inline std::mutex mutex;
inline std::array<double, 32768> intervals{};
inline std::size_t count = 0, overflow = 0;
inline std::int64_t previous_ns = 0, started_ns = 0;
inline void start() {
    std::lock_guard lock(mutex);
    count = overflow = 0; previous_ns = 0;
    started_ns = now_ns(); deadline_ns.store(started_ns + 60'000'000'000LL);
}
inline void present() {
    if (!active()) return;
    const auto now = now_ns();
    std::lock_guard lock(mutex);
    if (!in_window(now, deadline_ns.load()) || now < started_ns) return;
    if (previous_ns != 0) {
        if (count < intervals.size()) intervals[count++] = (now - previous_ns) / 1e6;
        else ++overflow;
    }
    previous_ns = now;
}
inline std::string finish() {
    std::lock_guard lock(mutex);
    deadline_ns.store(0);
    std::ostringstream out;
    out << "Performance capture (maximum 60 s; successful swapchain returns, NOT physical scanout)\n"
        << "Elapsed wall seconds: " << (started_ns ? (now_ns() - started_ns) / 1e9 : 0) << "\n"
        << "Intervals: " << count << ", dropped at capacity: " << overflow << "\n";
    if (count) {
        std::sort(intervals.begin(), intervals.begin() + count);
        const auto percentile = [](std::size_t p) { return intervals[(count * p + 99) / 100 - 1]; };
        out << "Interval ms p50/p95/p99/max: " << percentile(50) << "/" << percentile(95)
            << "/" << percentile(99) << "/" << intervals[count - 1] << "\n";
        for (const double threshold : {33.333, 50.0, 100.0})
            out << "Intervals over " << threshold << " ms: "
                << std::count_if(intervals.begin(), intervals.begin() + count, [=](double value) { return value > threshold; }) << "\n";
    } else out << "Frame intervals: unavailable (no successful presentation pair captured)\n";
    out << "Physical scanout timing: unavailable. Scene/simulation/async GPU history, when available, is in runtime.log.\n";
    return out.str();
}
}
