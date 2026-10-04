#include "../src/game/graphics_snapshot_pool.hpp"
#include "common/rt64_timer.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <vector>
#include <algorithm>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include <sys/resource.h>
#endif

double process_cpu_ms() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) / 10000.0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0
        + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
#endif
}

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE unsigned consume(const std::uint8_t* bytes, std::size_t size) {
    unsigned value = 0;
    for (std::size_t i = 0; i < size; i += 4096) value += bytes[i];
    return value;
}
int main() {
#if defined(_WIN32)
    // Match SDL's 1 ms timer hint in the actual game process.
    timeBeginPeriod(1);
#endif
    constexpr std::size_t size = 8 * 1024 * 1024;
    std::vector<std::uint8_t> source(size, 7);
    dkr::runtime::GraphicsSnapshotPool<size> pool;
    unsigned checksum = 0;
    for (const bool pooled : {false, true}) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i != 300; ++i) {
            if (pooled) {
                auto snapshot = pool.acquire();
                std::memcpy(snapshot.get(), source.data(), size);
                checksum += consume(snapshot.get(), size);
            } else {
                auto snapshot = std::make_unique<std::uint8_t[]>(size);
                std::memcpy(snapshot.get(), source.data(), size);
                checksum += consume(snapshot.get(), size);
            }
        }
        std::printf("snapshot_%s_300_tasks_ms=%.3f\n", pooled ? "pooled" : "baseline",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    const auto cpu = process_cpu_ms();
    const auto start = RT64::Timer::current();
    double max_late_us = 0;
    double total_late_us = 0;
    std::vector<double> lateness;
    for (unsigned i = 1; i <= 600; ++i) {
        const auto deadline = start + std::chrono::microseconds(i * 16667);
        RT64::Timer::preciseSleepUntil(deadline);
        const double late = double(RT64::Timer::deltaMicroseconds(deadline, RT64::Timer::current()));
        total_late_us += late;
        lateness.push_back(late);
        if (late > max_late_us) max_late_us = late;
    }
    const auto consumed = process_cpu_ms() - cpu;
    std::sort(lateness.begin(), lateness.end());
    std::printf("pacing_600_frames_cpu_ms=%.3f mean_late_us=%.2f p95_late_us=%.2f p99_late_us=%.2f max_late_us=%.2f checksum=%u\n",
        consumed, total_late_us / lateness.size(), lateness[569], lateness[593], max_late_us, checksum);
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}
