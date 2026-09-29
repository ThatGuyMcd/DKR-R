#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "performance_capture.hpp"

namespace dkr::runtime::track_performance {

inline constexpr std::size_t kSamples = 120U;
using Samples = std::array<double, kSamples>;
using Clock = std::chrono::steady_clock;

inline bool enabled() {
    static const bool value = [] {
        const char* setting = std::getenv("DKR_TRACK_PROFILE");
        return setting != nullptr && setting[0] == '1' && setting[1] == '\0';
    }();
    return value || performance_capture::active();
}

struct Summary {
    std::size_t count = 0U;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

inline Summary summarize(Samples values) {
    // RT64's unfilled rolling-history entries are zero. Exclude them and
    // invalid samples; never present an empty/unavailable timer as 0 ms.
    const auto end = std::remove_if(values.begin(), values.end(),
        [](double value) { return !std::isfinite(value) || value <= 0.0; });
    const auto count = static_cast<std::size_t>(end - values.begin());
    if (count == 0U) return {};
    std::sort(values.begin(), end);
    const auto percentile = [&](std::size_t percent) {
        return values[(count * percent + 99U) / 100U - 1U];
    };
    return {count, percentile(50U), percentile(95U), percentile(99U)};
}

struct Capture {
    Samples decode_ms{};
    std::size_t count = 0U;
    std::uint32_t scene = 0U;
    std::uint32_t map = 0U;
    std::uint32_t menu = 0U;
    std::uint64_t unavailable_windows = 0U;
    Clock::time_point started{};
};

} // namespace dkr::runtime::track_performance
