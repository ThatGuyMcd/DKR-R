#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "performance_capture.hpp"

namespace dkr::runtime::performance_trace {
// Opt-in, bounded aggregates: no per-frame logging, allocation, private data,
// renderer locks or changes to scheduling. Durations are WALL time, not CPU.
enum class Region { SnapshotCopy, FullSync, GpuFence, Readback, FriendSnapshot,
                    VulkanCompute, VulkanGraphics, UiBuild, BufferUpload, BufferUploadWait,
                    FramebufferFence, WorkloadFence, PresentFence, TextureFence, OtherFence,
                    IdleGpuSubmit, IdleGpuSkipped, OnlineStateCapture, OnlineStateApply,
                    OnlineStateEncode, OnlineStateDecode, OnlinePacingView, Count };
inline Region fence_region(std::string_view name) {
    if (name == "Framebuffer Graphics") return Region::FramebufferFence;
    if (name == "Workload Graphics") return Region::WorkloadFence;
    if (name == "Present Graphics") return Region::PresentFence;
    if (name == "Texture Direct" || name == "Texture Copy" || name == "RT64 Stream Worker") return Region::TextureFence;
    return Region::OtherFence;
}
struct Counter {
    std::atomic<std::uint64_t> calls{0}, nanoseconds{0}, bytes{0};
};
inline std::array<Counter, static_cast<std::size_t>(Region::Count)> counters{};
inline bool enabled() {
    static const bool active = [] {
        const char* value = std::getenv("DKR_POWER_PROFILE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return active || performance_capture::active();
}
inline void event(Region region) {
    if (enabled()) counters[static_cast<std::size_t>(region)].calls.fetch_add(1, std::memory_order_relaxed);
}
class Scope {
    using Clock = std::chrono::steady_clock;
    Counter* counter_;
    Clock::time_point start_{};
    std::uint64_t bytes_;
public:
    explicit Scope(Region region, std::uint64_t bytes = 0)
        : counter_(enabled() ? &counters[static_cast<std::size_t>(region)] : nullptr), bytes_(bytes) {
        if (counter_) start_ = Clock::now();
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() {
        if (!counter_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count();
        counter_->nanoseconds.fetch_add(static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
        counter_->bytes.fetch_add(bytes_, std::memory_order_relaxed);
        counter_->calls.fetch_add(1, std::memory_order_relaxed);
    }
};
inline void report() {
    if (!enabled()) return;
    constexpr const char* labels[] = {"snapshot-copy", "full-sync", "gpu-fence", "readback",
        "friend-snapshot", "vulkan-compute-pipeline", "vulkan-graphics-pipeline", "UI-build",
        "buffer-upload", "buffer-upload-wait", "fence-framebuffer", "fence-workload",
        "fence-present", "fence-texture", "fence-other", "idle-gpu-submit", "idle-gpu-skipped",
        "online-state-capture", "online-state-apply", "online-state-encode", "online-state-decode", "online-pacing-view"};
    static_assert(std::size(labels) == static_cast<std::size_t>(Region::Count));
    for (std::size_t i = 0; i < counters.size(); ++i) {
        const auto calls = counters[i].calls.load(std::memory_order_relaxed);
        if (calls == 0) continue;
        std::fprintf(stderr, "[perf][total] %s calls=%llu wall-ms=%.3f bytes=%llu\n", labels[i],
            static_cast<unsigned long long>(calls), counters[i].nanoseconds.load(std::memory_order_relaxed) / 1000000.0,
            static_cast<unsigned long long>(counters[i].bytes.load(std::memory_order_relaxed)));
    }
    // Cumulative totals are approximate while writers are active. Regions may
    // nest or run concurrently: adding their wall times is NOT CPU usage.
}
inline void record_present() {
    performance_capture::present();
    using Clock = std::chrono::steady_clock;
    // Only the presentation thread calls this. Keep its bounded history local;
    // no cross-thread access to RT64's profiling buffers or renderer locks.
    thread_local Clock::time_point previous{};
    thread_local std::array<double, 300> intervals{};
    thread_local std::size_t count = 0;
    if (!enabled()) { previous = {}; count = 0; return; }
    const auto now = Clock::now();
    if (previous != Clock::time_point{}) {
        intervals[count++] = std::chrono::duration<double, std::milli>(now - previous).count();
        if (count == intervals.size()) {
            auto sorted = intervals;
            std::sort(sorted.begin(), sorted.end());
            std::fprintf(stderr, "[perf][present-interval] n=300 ms(p50/p95/p99/max)=%.3f/%.3f/%.3f/%.3f\n",
                sorted[149], sorted[284], sorted[296], sorted[299]);
            count = 0;
        }
    }
    previous = now;
    // Successful swapchain-return intervals, NOT physical display scanout.
}
using Snapshot = std::array<std::array<std::uint64_t, 3>, static_cast<std::size_t>(Region::Count)>;
inline Snapshot snapshot() {
    Snapshot result{};
    for (std::size_t i = 0; i < counters.size(); ++i) result[i] = {
        counters[i].calls.load(), counters[i].nanoseconds.load(), counters[i].bytes.load()};
    return result;
}
inline std::string describe_delta(const Snapshot& baseline) {
    constexpr const char* labels[] = {"snapshot-copy", "full-sync", "gpu-fence", "readback",
        "friend-snapshot", "vulkan-compute-pipeline", "vulkan-graphics-pipeline", "UI-build",
        "buffer-upload", "buffer-upload-wait", "fence-framebuffer", "fence-workload",
        "fence-present", "fence-texture", "fence-other", "idle-gpu-submit", "idle-gpu-skipped",
        "online-state-capture", "online-state-apply", "online-state-encode", "online-state-decode", "online-pacing-view"};
    static_assert(std::size(labels) == static_cast<std::size_t>(Region::Count));
    const auto end = snapshot();
    std::ostringstream out;
    out << "CPU-side regions (WALL time, overlapping/concurrent; not additive CPU utilisation):\n";
    for (std::size_t i = 0; i < end.size(); ++i) {
        out << labels[i] << " calls=" << end[i][0] - baseline[i][0]
            << " wall-ms=" << (end[i][1] - baseline[i][1]) / 1e6
            << " bytes=" << end[i][2] - baseline[i][2] << "\n";
    }
    return out.str();
}
}
