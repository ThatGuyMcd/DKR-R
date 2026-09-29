#pragma once

#include "track_performance.hpp"
#include <cstdio>
#include <mutex>

namespace dkr::runtime::water {
inline bool enabled() {
    static const bool value = [] {
        const auto* v = std::getenv("DKR_WATER_PROFILE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return value;
}
inline bool equal_uv_enabled() {
    // Diagnostic A/B switch only; no save/settings migration.
    static const bool value = [] {
        const auto* v = std::getenv("DKR_WATER_UV_BASELINE");
        return !(v && v[0] == '1' && v[1] == '\0');
    }();
    return value;
}
inline bool coalesce_enabled() {
    static const bool value = [] {
        const auto* v = std::getenv("DKR_WATER_DRAW_BASELINE");
        return !(v && v[0] == '1' && v[1] == '\0');
    }();
    return value;
}
struct BridgeCounters {
    std::uint64_t triangles = 0, skipped = 0, added_vertices = 0;
    std::uint64_t skipped_culling = 0;
    double batch_ms = 0;
};
inline thread_local BridgeCounters bridge{};
// Render worker publishes bounded history while it owns its own timers.
// A separate try-lock on BOTH sides never stalls rendering or decoding.
struct RendererHistory {
    std::array<track_performance::Samples, 4> values{};
    std::uint64_t sequence = 0;
    track_performance::Clock::time_point published{};
};
inline std::mutex history_mutex;
inline RendererHistory history{};
template<class Timer>
inline void publish_history(const Timer& matching, const Timer& cpu,
                            const Timer& gpu, const Timer& workload) {
    if (!track_performance::enabled()) return;
    thread_local unsigned count = 0;
    if (++count % 30U != 0U) return;
    RendererHistory next{};
    const Timer* timers[] = {&matching, &cpu, &gpu, &workload};
    for (unsigned i = 0; i < 4; ++i)
        std::copy_n(timers[i]->data(), std::min(timers[i]->size(), track_performance::kSamples), next.values[i].begin());
    std::unique_lock lock(history_mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        next.sequence = history.sequence + 1U;
        next.published = track_performance::Clock::now();
        history = next;
    }
}
inline RendererHistory read_history() {
    std::unique_lock lock(history_mutex, std::try_to_lock);
    return lock.owns_lock() ? history : RendererHistory{};
}
inline void end_task(unsigned scene, unsigned map, unsigned menu) {
    if (!enabled()) return;
    struct Window {
        unsigned scene = 0, map = 0, menu = 0, count = 0;
        BridgeCounters totals{};
        track_performance::Samples times{};
    };
    thread_local Window window{};
    if (window.scene != scene || window.map != map || window.menu != menu) {
        window = {}; window.scene = scene; window.map = map; window.menu = menu;
    }
    window.times[window.count++] = bridge.batch_ms;
    window.totals.triangles += bridge.triangles;
    window.totals.skipped += bridge.skipped;
    window.totals.added_vertices += bridge.added_vertices;
    window.totals.skipped_culling += bridge.skipped_culling;
    bridge = {};
    if (window.count != track_performance::kSamples) return;
    if (window.totals.triangles) {
        const auto stats = track_performance::summarize(window.times);
        std::fprintf(stderr, "[perf][water-bridge] scene=%u map=%u menu=%u tasks=%u water-tasks=%zu "
            "uv-fast=%u triangles=%llu skipped-uv-copies=%llu added-vertices=%llu batch-ms(p50/p95/p99)=%.3f/%.3f/%.3f "
            "row-coalescing=%u skipped-cull-flushes=%llu\n",
            scene, map, menu, window.count, stats.count, equal_uv_enabled() ? 1U : 0U,
            (unsigned long long)window.totals.triangles, (unsigned long long)window.totals.skipped,
            (unsigned long long)window.totals.added_vertices, stats.median, stats.p95, stats.p99,
            coalesce_enabled() ? 1U : 0U, (unsigned long long)window.totals.skipped_culling);
    }
    window.count = 0; window.totals = {};
}
}
