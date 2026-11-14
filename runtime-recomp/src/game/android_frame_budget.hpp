#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dkr::runtime::android_frame_budget {
enum class Region { Decoder, FullSync, NativeFence, UploadFence, PresentationLock, Count };
inline constexpr const char* labels[] = {"decoder", "full-sync", "native-fence", "upload-fence", "presentation-lock"};
inline constexpr std::size_t capacity = 128;
struct Samples {
    std::array<double, capacity> values{};
    std::uint64_t count = 0;
    double total = 0;
    void record(double ms) noexcept {
        values[count++ % capacity] = ms;
        total += ms;
    }
};
inline thread_local std::array<Samples, static_cast<std::size_t>(Region::Count)> samples;
class Scope final {
    using Clock = std::chrono::steady_clock;
    Region region_;
    Clock::time_point start_;

  public:
    explicit Scope(Region region) : region_(region), start_(Clock::now()) {}
    ~Scope() {
        samples[static_cast<std::size_t>(region_)].record(
            std::chrono::duration<double, std::milli>(Clock::now() - start_).count());
    }
};
inline void report() {
    // No GPU queries, renderer locks, Java/PSS polling or recording-session work.
    // This thread owns the bounded buffers and emits only after 120 real tasks.
    if (samples[0].count < 120)
        return;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto& s = samples[i];
        if (!s.count)
            continue;
        const auto n = std::min<std::uint64_t>(s.count, capacity);
        auto values = s.values;
        std::sort(values.begin(), values.begin() + n);
        const auto percentile = [&](unsigned p) { return values[(n * p + 99) / 100 - 1]; };
        std::fprintf(
            stderr,
            "[android][frame-budget] %s calls=%llu total-wall-ms=%.3f last-n=%llu ms(p50/p95/p99)=%.3f/%.3f/%.3f\n",
            labels[i], static_cast<unsigned long long>(s.count), s.total, static_cast<unsigned long long>(n),
            percentile(50), percentile(95), percentile(99));
        s = {};
    }
    // Nested and concurrent WALL times, not additive CPU utilisation.
}
} // namespace dkr::runtime::android_frame_budget
