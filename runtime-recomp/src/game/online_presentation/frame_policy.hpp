#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dkr::online_presentation {
// Asset lookup and visual history have different lifetimes. This key must
// never be used for frame admission, pose matching or interpolation history.
inline std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}
struct ResourceScene {
    bool active = false;
    std::uint64_t epoch = 0;
    std::uint32_t scene = 0;
};
inline thread_local ResourceScene resource_scene{};
class ResourceScope {
    ResourceScene previous_;

  public:
    explicit ResourceScope(std::uint64_t epoch) noexcept : previous_(resource_scene) {
        resource_scene = {false, epoch, 0};
    }
    void scene(std::uint32_t value) noexcept {
        resource_scene.active = true;
        resource_scene.scene = value;
    }
    ~ResourceScope() {
        resource_scene = previous_;
    }
    ResourceScope(const ResourceScope&) = delete;
    ResourceScope& operator=(const ResourceScope&) = delete;
};
inline std::uint32_t asset_scene(std::uint32_t ordinary_scene, std::uint64_t lifetime = 0) noexcept {
    if (!resource_scene.active)
        return ordinary_scene;
    const auto key = mix(resource_scene.scene ^ mix(std::uint32_t(resource_scene.epoch)) ^
                         mix(std::uint32_t(resource_scene.epoch >> 32)) ^ mix(std::uint32_t(lifetime)) ^
                         mix(std::uint32_t(lifetime >> 32)));
    return key ? key : 1U;
}
inline bool idle_refresh_due(std::chrono::nanoseconds elapsed, bool displayed) noexcept {
    // A 30-Hz real frame is not an idle screen. Keep admission responsive,
    // but avoid injecting paused workloads between healthy race frames.
    return elapsed >= (displayed ? std::chrono::milliseconds(66) : std::chrono::milliseconds(33));
}
enum class Event : unsigned {
    Submitted,
    Focused,
    Shared,
    ResourceRebuild,
    IdleRepeat,
    NativeFallback,
    RenderTarget,
    PictureHeld,
    ForcedMissingTarget,
    MissingFramebuffer,
    MissingCapturedTarget,
    Count
};
inline std::array<std::atomic<std::uint64_t>, unsigned(Event::Count)> counters{};
inline std::array<std::atomic<std::uint64_t>, 16> view_reasons{};
inline thread_local unsigned window_width = 0, window_height = 0;
inline thread_local float window_cover = 1.F;
inline constexpr std::array<std::uint64_t, 16> frame_limits_us{
    2000, 4000, 6000, 8000, 10000, 12000, 16000, 18000, 20000, 24000, 33000, 40000, 50000, 66000, 100000, UINT64_MAX};
inline std::array<std::atomic<std::uint64_t>, frame_limits_us.size()> frame_histogram{};
inline std::atomic<std::uint64_t> frame_nanos{0};
inline std::uint64_t percentile(const std::array<std::uint64_t, 16>& histogram, unsigned percent) noexcept {
    std::uint64_t total = 0;
    for (const auto count : histogram)
        total += count;
    if (!total || !percent || percent > 100)
        return 0;
    const auto rank = (total / 100) * percent + ((total % 100) * percent + 99) / 100;
    std::uint64_t prefix = 0;
    for (unsigned i = 0; i < histogram.size(); ++i)
        if ((prefix += histogram[i]) >= rank)
            return frame_limits_us[i];
    return 0;
}
inline void presented(bool owned, bool paused, bool visible) noexcept {
    // Include idle redraws in the same worker's intervals; excluding them
    // would hide the gaps caused by paused workloads. Reset for real blank
    // VIs and ordinary non-owned playback, never for a rollback generation.
    static thread_local bool active = false;
    static thread_local std::chrono::steady_clock::time_point last{};
    if (!visible || (!owned && (!paused || !active))) {
        active = false;
        last = {};
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (active) {
        const auto ns = std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count());
        unsigned bucket = 0;
        while (bucket + 1 < frame_limits_us.size() && ns / 1000 > frame_limits_us[bucket])
            ++bucket;
        frame_nanos.fetch_add(ns, std::memory_order_relaxed);
        frame_histogram[bucket].fetch_add(1, std::memory_order_relaxed);
    }
    last = now;
    active = true;
}
inline void count(Event event) noexcept {
    counters[unsigned(event)].fetch_add(1, std::memory_order_relaxed);
}
inline void view(bool focus, unsigned reason, unsigned width, unsigned height, float cover) noexcept {
    count(Event::Submitted);
    count(focus ? Event::Focused : Event::Shared);
    window_width = width;
    window_height = height;
    window_cover = cover;
    if (reason < view_reasons.size())
        view_reasons[reason].fetch_add(1, std::memory_order_relaxed);
}
inline void report() noexcept {
    // Called by the existing five-second renderer report. No worker-owned
    // vectors, GPU drain, allocation, lock or guest RAM is inspected here.
    static thread_local std::array<std::uint64_t, unsigned(Event::Count)> previous{};
    std::array<std::uint64_t, unsigned(Event::Count)> delta{};
    for (unsigned i = 0; i < delta.size(); ++i) {
        const auto value = counters[i].load(std::memory_order_relaxed);
        delta[i] = value - previous[i];
        previous[i] = value;
    }
    std::fprintf(
        stderr,
        "[online][presentation] submitted=%llu focused=%llu shared=%llu asset-rebuilds=%llu idle-repeats=%llu native-fallbacks=%llu render-targets=%llu held-gpu-pictures=%llu forced-missing-targets=%llu missing-framebuffers=%llu missing-captured-targets=%llu window=%ux%u cover=%.4f\n",
        static_cast<unsigned long long>(delta[0]), static_cast<unsigned long long>(delta[1]),
        static_cast<unsigned long long>(delta[2]), static_cast<unsigned long long>(delta[3]),
        static_cast<unsigned long long>(delta[4]), static_cast<unsigned long long>(delta[5]),
        static_cast<unsigned long long>(delta[6]), static_cast<unsigned long long>(delta[7]),
        static_cast<unsigned long long>(delta[8]), static_cast<unsigned long long>(delta[9]),
        static_cast<unsigned long long>(delta[10]), window_width, window_height, double(window_cover));
    static thread_local std::array<std::uint64_t, 16> prior_histogram{};
    static thread_local std::uint64_t prior_nanos = 0;
    std::array<std::uint64_t, 16> histogram{};
    std::uint64_t frames = 0;
    for (unsigned i = 0; i < histogram.size(); ++i) {
        const auto value = frame_histogram[i].load(std::memory_order_relaxed);
        frames += (histogram[i] = value - prior_histogram[i]);
        prior_histogram[i] = value;
    }
    const auto nanos = frame_nanos.load(std::memory_order_relaxed);
    if (frames)
        std::fprintf(
            stderr,
            "[online][frame-times] count=%llu mean-ms=%.3f p95-upper-us=%llu p99-upper-us=%llu; swap submissions include idle redraws\n",
            static_cast<unsigned long long>(frames), double(nanos - prior_nanos) / double(frames) / 1e6,
            static_cast<unsigned long long>(percentile(histogram, 95)),
            static_cast<unsigned long long>(percentile(histogram, 99)));
    prior_nanos = nanos;
    static thread_local std::array<std::uint64_t, 16> prior_reasons{};
    for (unsigned i = 1; i < view_reasons.size(); ++i) {
        const auto value = view_reasons[i].load(std::memory_order_relaxed), delta_reason = value - prior_reasons[i];
        prior_reasons[i] = value;
        if (delta_reason)
            std::fprintf(stderr, "[online][view-decision] reason=%u frames=%llu\n", i,
                         static_cast<unsigned long long>(delta_reason));
    }
}
} // namespace dkr::online_presentation

namespace dkr::runtime::netplay::experimental::present_gate {
// C++17 queue seam; implementation reads the exact immutable tagged snapshot.
bool requires_gpu_picture(const void* slot, std::uint64_t present, std::uint64_t workload);
} // namespace dkr::runtime::netplay::experimental::present_gate
