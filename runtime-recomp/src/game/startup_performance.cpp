#include "startup_performance.hpp"

#include <cstdio>

namespace dkr::runtime::startup_performance {
namespace {

const Clock::time_point g_process_epoch = Clock::now();

} // namespace

double elapsed_milliseconds() noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - g_process_epoch).count();
}

void mark(const char* event) noexcept {
    std::fprintf(stderr, "[perf][startup] elapsed=%.3fms event=%s\n", elapsed_milliseconds(),
                 event != nullptr ? event : "unknown");
}

void report(const char* phase, Clock::time_point started_at) noexcept {
    const double duration = std::chrono::duration<double, std::milli>(Clock::now() - started_at).count();
    std::fprintf(stderr, "[perf][startup] elapsed=%.3fms duration=%.3fms phase=%s\n", elapsed_milliseconds(), duration,
                 phase != nullptr ? phase : "unknown");
}

ScopedPhase::ScopedPhase(const char* phase) noexcept : phase_(phase), started_at_(Clock::now()) {}

ScopedPhase::~ScopedPhase() {
    report(phase_, started_at_);
}

} // namespace dkr::runtime::startup_performance
