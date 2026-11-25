#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

namespace dkr::online_stability {
// Estimate correction pressure from the existing report fields. Forward work
// excludes replay; no new protocol fields or guest state are introduced.
class ReplayCost final {
    std::optional<std::uint32_t> frontier_;
    std::uint64_t tick_us_ = 0;

  public:
    void observe(std::uint32_t frame, unsigned work, unsigned replay, unsigned interval) {
        const auto previous = frontier_;
        frontier_ = frame;
        if (!previous || frame < *previous) {
            tick_us_ = 0;
            return;
        }
        const auto advanced = frame - *previous;
        if (!advanced || advanced > 240 || interval < 100000 || interval > 2000000 || replay > work)
            return;
        const auto sample = std::min<std::uint64_t>(1000000, (work - replay) / advanced);
        tick_us_ = tick_us_ ? (3 * tick_us_ + sample + 3) / 4 : sample;
    }
    bool pressure(unsigned late, unsigned replay) const {
        return late > 1 && replay >= 33333 && tick_us_ * late > 33333;
    }
    bool busy(unsigned late, unsigned replay) const {
        return late && replay >= 16667 && tick_us_ * late > 16667;
    }
};
} // namespace dkr::online_stability
