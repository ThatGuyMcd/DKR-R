#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

struct LatencySummary {
    std::uint64_t samples = 0;
    std::uint32_t p50_ms = 0, p95_ms = 0, p99_ms = 0, maximum_ms = 0;
};

// Input-history-lifetime, fixed-memory histogram. Bucket upper bounds have <=4 ms
// error below one second; the overflow bucket reports the observed maximum.
// No allocation, disk I/O, cross-machine timestamps or raw player data.
class LatencyHistogram {
public:
    void observe(std::uint32_t milliseconds) {
        ++buckets_[(std::min<std::uint32_t>)(milliseconds / 4U, 256U)];
        ++samples_;
        maximum_ = (std::max)(maximum_, milliseconds);
    }
    LatencySummary summary() const {
        return {samples_, percentile(50), percentile(95), percentile(99), maximum_};
    }
private:
    std::uint32_t percentile(std::uint32_t percent) const {
        if (!samples_) return 0;
        const auto rank = (samples_ / 100U) * percent +
            ((samples_ % 100U) * percent + 99U) / 100U;
        std::uint64_t cumulative = 0;
        for (std::uint32_t i = 0; i < buckets_.size(); ++i) {
            cumulative += buckets_[i];
            if (cumulative >= rank)
                return i == 256U ? maximum_ : (std::min)(maximum_, i * 4U + 3U);
        }
        return maximum_;
    }
    std::array<std::uint64_t, 257> buckets_{};
    std::uint64_t samples_ = 0;
    std::uint32_t maximum_ = 0;
};

struct QueueLaneSummary {
    std::size_t packets = 0, bytes = 0;
    std::uint32_t oldest_ms = 0;
};

} // namespace dkr::runtime::netplay
