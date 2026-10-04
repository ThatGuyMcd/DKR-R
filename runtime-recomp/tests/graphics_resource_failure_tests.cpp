#include "../src/game/graphics_health.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
std::atomic<unsigned> notices{0}, returned_to_caller{0};
void report(dkr::runtime::graphics_health::Stage stage, std::int32_t result) {
    if (stage != dkr::runtime::graphics_health::Stage::BufferAllocation || result != -2) std::_Exit(1);
    ++notices;
}
}
// Separate short-lived process. Deliberately parked workers MUST NOT join or
// unwind; terminate only this test process after checking containment/reporting.
int main() {
    using namespace dkr::runtime::graphics_health;
    reset(); resource_failure_reporter.store(report);
    std::thread fault([] {
        quarantine_resource(Stage::BufferAllocation, -2);
        ++returned_to_caller; // A memcpy/use of an invalid object must be unreachable.
    });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!notices && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    if (notices != 1 || !failed() || !resource_quarantined) std::_Exit(1);
    if (record(Stage::BufferMap, -5)) std::_Exit(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (returned_to_caller != 0 || notices != 1 ||
        static_cast<std::int32_t>(failure.load()) != -2) std::_Exit(1);
    std::puts("PASS: terminal resource fault reported independently, first error retained, invalid use/unwind prevented");
    std::fflush(stdout);
    std::_Exit(0);
}
