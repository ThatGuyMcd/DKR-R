#pragma once

#include <chrono>

namespace dkr::runtime::startup_performance {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_milliseconds() noexcept;
void mark(const char* event) noexcept;
void report(const char* phase, Clock::time_point started_at) noexcept;

class ScopedPhase {
  public:
    explicit ScopedPhase(const char* phase) noexcept;
    ~ScopedPhase();

    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;

  private:
    const char* phase_;
    Clock::time_point started_at_;
};

} // namespace dkr::runtime::startup_performance
