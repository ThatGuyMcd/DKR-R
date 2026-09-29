#pragma once
#include "performance_trace.hpp"

namespace dkr::runtime::render_metrics {
enum class Event { PassBegin, RedundantBind, DepthExcursion, Draw, DitherDraw,
                   EnhancedTarget, AllTargets, ReadBarrierRemoved, PassBreakRemoved, Count };
inline constexpr const char* names[] = {"render-pass-begin", "redundant-bind-removed",
    "depth-read-write-read-removed", "raster-draw", "dither-extra-draw",
    "enhanced-framebuffer", "all-framebuffers", "redundant-read-barrier-removed", "read-barrier-pass-break-removed"};
static_assert(std::size(names) == static_cast<std::size_t>(Event::Count));
inline std::array<std::atomic<std::uint64_t>, std::size(names)> counts{};
inline std::atomic<std::uint64_t> gpu_samples{0}, gpu_setup_ns{0}, gpu_raster_transfer_ns{0};
inline std::atomic<std::uint64_t> target_dimensions{0}, target_samples{0}, target_format{0};
inline void event(Event event, std::uint64_t amount = 1) {
    if (performance_trace::enabled()) counts[static_cast<std::size_t>(event)].fetch_add(amount, std::memory_order_relaxed);
}
inline void target(unsigned width, unsigned height, unsigned samples, unsigned format) {
    if (!performance_trace::enabled()) return;
    event(Event::EnhancedTarget);
    target_dimensions.store((std::uint64_t(width) << 32) | height, std::memory_order_relaxed);
    target_samples.store(samples, std::memory_order_relaxed);
    target_format.store(format, std::memory_order_relaxed);
}
inline bool valid_gpu_times(std::uint64_t start, std::uint64_t setup, std::uint64_t end) {
    // Reject unavailable/backwards/wrapped results. This is diagnostic only;
    // it never adjusts frame scheduling or treats a missing sample as zero cost.
    return start > 0 && setup >= start && end >= setup && end - start < 5'000'000'000ULL;
}
inline void gpu_times(std::uint64_t start, std::uint64_t setup, std::uint64_t end) {
    if (!valid_gpu_times(start, setup, end)) return;
    gpu_setup_ns.fetch_add(setup - start, std::memory_order_relaxed);
    gpu_raster_transfer_ns.fetch_add(end - setup, std::memory_order_relaxed);
    gpu_samples.fetch_add(1, std::memory_order_relaxed);
}
struct Snapshot {
    std::array<std::uint64_t, std::size(names)> events{};
    std::uint64_t samples = 0, setup = 0, raster_transfer = 0;
};
inline Snapshot snapshot() {
    Snapshot s;
    for (std::size_t i = 0; i < s.events.size(); ++i) s.events[i] = counts[i].load();
    s.samples = gpu_samples.load(); s.setup = gpu_setup_ns.load(); s.raster_transfer = gpu_raster_transfer_ns.load();
    return s;
}
inline std::string describe_delta(const Snapshot& baseline) {
    const auto end = snapshot();
    std::ostringstream out;
    out << "Android renderer counters (opt-in; all graphics workers, native and enhanced):\n";
    for (std::size_t i = 0; i < end.events.size(); ++i)
        out << names[i] << "=" << end.events[i] - baseline.events[i] << "\n";
    const auto samples = end.samples - baseline.samples;
    out << "Enhanced GPU timestamp samples=" << samples;
    if (samples) out << " setup-total-ms=" << (end.setup - baseline.setup) / 1e6
        << " raster-and-transfers-total-ms=" << (end.raster_transfer - baseline.raster_transfer) / 1e6
        << " setup-mean-ms=" << (end.setup - baseline.setup) / (1e6 * samples)
        << " raster-and-transfers-mean-ms=" << (end.raster_transfer - baseline.raster_transfer) / (1e6 * samples);
    else out << " (unavailable; not zero GPU cost)";
    out << "\nGPU timestamps are enhanced workload intervals, NOT whole-display frame time. "
        << "They exclude native framebuffer work, presentation and other queues; overlap is possible.\n";
    constexpr auto target_index = static_cast<std::size_t>(Event::EnhancedTarget);
    if (end.events[target_index] > baseline.events[target_index]) {
        const auto dimensions = target_dimensions.load();
        out << "Last enhanced target=" << (dimensions >> 32) << "x" << (dimensions & 0xffffffffULL)
            << " samples=" << target_samples.load() << " plume-format=" << target_format.load()
            << " (last observed target, not a resolution estimate or capture maximum)\n";
    }
    return out.str();
}
}
