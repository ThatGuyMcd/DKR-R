#include "runtime_telemetry.hpp"
#include "runtime_netplay.hpp"

#include "recomp.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace {

using Clock = std::chrono::steady_clock;
struct Counters {
    std::atomic<std::uint64_t> simulation_ticks{0};
    std::atomic<std::uint64_t> audio_frames{0};
    std::atomic<std::uint64_t> graphics_tasks{0};
    std::atomic<std::uint64_t> vi_presents{0};
    std::atomic<std::uint64_t> interpolated_presents{0};
    std::atomic<std::uint64_t> presented_frames{0};
};

struct Snapshot {
    std::uint64_t simulation_ticks = 0;
    std::uint64_t audio_frames = 0;
    std::uint64_t graphics_tasks = 0;
    std::uint64_t vi_presents = 0;
    std::uint64_t interpolated_presents = 0;
    std::uint64_t presented_frames = 0;
};

Counters g_counters;

Snapshot ReadSnapshot() {
    Snapshot result;
    result.simulation_ticks = g_counters.simulation_ticks.load(std::memory_order_relaxed);
    result.audio_frames = g_counters.audio_frames.load(std::memory_order_relaxed);
    result.graphics_tasks = g_counters.graphics_tasks.load(std::memory_order_relaxed);
    result.vi_presents = g_counters.vi_presents.load(std::memory_order_relaxed);
    result.interpolated_presents = g_counters.interpolated_presents.load(std::memory_order_relaxed);
    result.presented_frames = g_counters.presented_frames.load(std::memory_order_relaxed);
    return result;
}

std::uint64_t Delta(std::uint64_t current, std::uint64_t previous) {
    return current >= previous ? current - previous : 0;
}

double Rate(std::uint64_t count, double seconds) {
    return seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
}

} // namespace

void dkr::runtime::telemetry::record_simulation_tick() {
    g_counters.simulation_ticks.fetch_add(1, std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_audio_buffer(std::size_t interleaved_sample_count) {
    g_counters.audio_frames.fetch_add(interleaved_sample_count / 2U, std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_graphics_task() {
    g_counters.graphics_tasks.fetch_add(1, std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_vi_present() {
    g_counters.vi_presents.fetch_add(1, std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_interpolated_present() {
    record_interpolated_presents(1U);
}

void dkr::runtime::telemetry::record_interpolated_presents(std::uint64_t count) {
    g_counters.interpolated_presents.fetch_add(count, std::memory_order_relaxed);
}

void dkr::runtime::telemetry::record_presented_frames(std::uint64_t count) {
    g_counters.presented_frames.fetch_add(count, std::memory_order_relaxed);
}

dkr::runtime::telemetry::Metrics dkr::runtime::telemetry::metrics() {
    static std::mutex guard;
    static auto previous_time = Clock::now();
    static Snapshot previous = ReadSnapshot();
    static Metrics cached{};
    std::scoped_lock lock(guard);
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - previous_time).count();
    if (elapsed < 0.25) {
        return cached;
    }

    const Snapshot current = ReadSnapshot();
    cached.presented_fps = Rate(Delta(current.presented_frames, previous.presented_frames), elapsed);
    cached.frame_time_ms = cached.presented_fps > 0.0 ? 1000.0 / cached.presented_fps : 0.0;
    cached.simulation_hz = Rate(Delta(current.simulation_ticks, previous.simulation_ticks), elapsed);
    cached.graphics_hz = Rate(Delta(current.graphics_tasks, previous.graphics_tasks), elapsed);
    cached.vi_hz = Rate(Delta(current.vi_presents, previous.vi_presents), elapsed);
    cached.interpolated_hz = Rate(Delta(current.interpolated_presents, previous.interpolated_presents), elapsed);
    cached.audio_frames_per_second = Rate(Delta(current.audio_frames, previous.audio_frames), elapsed);
    previous = current;
    previous_time = now;
    return cached;
}

extern "C" void dkr_telemetry_simulation_tick(std::uint8_t*, recomp_context*) {
    if (!dkr::runtime::netplay::external_side_effects_allowed())
        return;
    dkr::runtime::telemetry::record_simulation_tick();
}
