#pragma once
#include <chrono>
#include <thread>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dkr::runtime {
// Presentation only. Neither simulation clocks nor input/network deadlines
// use this wait. Keep the upstream measured short-sleep/spin tail intact.
#if defined(_WIN32) && !defined(DKR_PRESENTATION_LEGACY_WAIT)
class PresentationTimer {
public:
    PresentationTimer() : handle_(CreateWaitableTimerExW(nullptr, nullptr,
        0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS)) {}
    ~PresentationTimer() { if (handle_) CloseHandle(handle_); }
    PresentationTimer(const PresentationTimer&) = delete;
    PresentationTimer& operator=(const PresentationTimer&) = delete;
    bool wait(std::chrono::nanoseconds remaining) {
        if (!handle_) return false;
        if (remaining.count() <= 0) return true;
        LARGE_INTEGER due{};
        // Negative = relative. Round up so the kernel wait cannot finish early
        // merely from truncation to Windows' 100 ns units.
        due.QuadPart = -(remaining.count() / 100 + (remaining.count() % 100 != 0));
        return SetWaitableTimerEx(handle_, &due, 0, nullptr, nullptr, nullptr, 0)
            && WaitForSingleObject(handle_, INFINITE) == WAIT_OBJECT_0;
    }
private:
    HANDLE handle_;
};
#endif

template <class Clock, class Duration>
void coarse_presentation_wait(std::chrono::time_point<Clock, Duration> deadline) {
#if defined(_WIN32) && !defined(DKR_PRESENTATION_LEGACY_WAIT)
    // One reusable high-resolution kernel timer per presentation thread avoids
    // spending most of a 1 ms scheduler quantum in the precision spin tail.
    // Unsupported timer flags or OS errors fall back to the proven 2 ms path.
    static const bool use_native_timer = [] {
        const char* legacy = std::getenv("DKR_LEGACY_PRESENT_WAIT");
        return !(legacy && legacy[0] == '1' && legacy[1] == '\0');
    }();
    if (use_native_timer) {
        thread_local PresentationTimer timer;
        const auto remaining = deadline - Clock::now() - std::chrono::microseconds(500);
        if (timer.wait(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining))) return;
    }
#endif
    const auto coarse_deadline = deadline - std::chrono::milliseconds(2);
    if (Clock::now() < coarse_deadline) std::this_thread::sleep_until(coarse_deadline);
}
}
