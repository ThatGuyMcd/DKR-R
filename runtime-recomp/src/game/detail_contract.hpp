#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include "netplay/experimental_draw_events.h"

namespace dkr::runtime::detail {
// Eight racers, four views and separate opaque/transparent submissions.
// Preparation is shared by matching pose requests; GPU draws remain per view.
inline constexpr unsigned Racers = 8, Views = 4, Passes = 2, Observations = 256;
inline constexpr unsigned ModelTriangles = 8192, PacketSteps = 32768;
inline constexpr unsigned PreparedSteps = Racers * Passes * PacketSteps;
inline constexpr unsigned ViewTriangles = Racers * Passes * ModelTriangles;
inline constexpr unsigned KernelOperations = Racers * Passes * 2000000U;
inline constexpr unsigned TextureBegin = 0xF00000U, TextureEnd = 0x1000000U;
std::span<const dkr_owned_draw_event> ordinary_events() noexcept;

struct Evidence {
    std::uint32_t scene = 0, object = 0;
    unsigned view = 0, source = 0, selected = 0, layer = 0;
    bool effective = false;
    const char* reason = "submitted";
};
// Bounded per-scene/change diagnostics. Unlike the previous process-wide
// first-24 budget, a later race and its other viewports get fresh evidence.
inline void note(Evidence e) noexcept {
    struct Seen {
        std::uint32_t object = 0;
        unsigned view = 0, layer = 0, source = 0, selected = 0;
        bool effective = false;
        std::uint32_t reason = 0;
    };
    struct State {
        std::uint32_t scene = ~0U;
        unsigned reports = 0, calls = 0;
        std::array<Seen, 128> seen{};
    };
    static thread_local State state;
    if (state.scene != e.scene) {
        state = {};
        state.scene = e.scene;
    }
    std::uint32_t reason = 2166136261U;
    for (const char* p = e.reason; *p; ++p) {
        reason ^= static_cast<unsigned char>(*p);
        reason *= 16777619U;
    }
    ++state.calls;
    bool changed = true;
    for (auto& row : state.seen)
        if (!row.object || (row.object == e.object && row.view == e.view && row.layer == e.layer)) {
            changed = !row.object || row.source != e.source || row.selected != e.selected ||
                      row.effective != e.effective || row.reason != reason;
            row = {e.object, e.view, e.layer, e.source, e.selected, e.effective, reason};
            break;
        }
    if (changed && (state.reports < 64 || state.calls % 4096 == 0)) {
        ++state.reports;
        std::fprintf(
            stderr,
            "[graphics][all-player-detail] scene=%u view=%u object=%08X layer=%u source-lod=%u selected-lod=%u effective=%u reason=%s; all-player-detail-v1\n",
            e.scene, e.view, e.object, e.layer, e.source, e.selected, unsigned(e.effective), e.reason);
    }
}
} // namespace dkr::runtime::detail
