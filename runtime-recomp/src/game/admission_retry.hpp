#pragma once
#include <cstdint>

namespace dkr::spectator_admission {
// Only a watcher uses this policy. A completed confirmed tick clears the
// streak; merely downloading/admitting another checkpoint does not.
class AdmissionRetry {
  public:
    static constexpr unsigned MaximumFailures = 5;
    bool failed(std::uint64_t epoch) noexcept {
        if (epoch != epoch_) {
            epoch_ = epoch;
            failures_ = 0;
        }
        if (failures_ < MaximumFailures)
            ++failures_;
        return failures_ < MaximumFailures;
    }
    void progressed() noexcept {
        failures_ = 0;
    }
    unsigned failures() const noexcept {
        return failures_;
    }

  private:
    std::uint64_t epoch_ = 0;
    unsigned failures_ = 0;
};
} // namespace dkr::spectator_admission
