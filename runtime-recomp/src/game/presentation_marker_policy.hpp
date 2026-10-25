#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::presentation {

// Sidecar command type is independent of the full five-bit geometry variant.
// Do not infer HUD/aspect commands from hashed surface or sprite identity bits.
enum class PresentationMarkerKind : std::uint8_t {
    Geometry,
    FramedResults,
    FixedUi,
    BackgroundAspect,
    HudPass,
    TrackSelectLensFlare,
    SplitViewport,
    PostraceFullViewport,
    HudWidget,
    HudRect,
    TrackSelectUi,
    TrackSelectPreview,
    CameraWorldBegin, CameraWorldEnd,
};

// An overflowed nested begin and its end are one rejected pair. Never let
// that end pop a valid outer scope. Reset with the owning task's draw state.
struct RejectedMarkerScopes {
    std::size_t depth = 0U;
    bool reject_begin(std::size_t accepted_depth, std::size_t capacity) {
        if (depth == 0U && accepted_depth < capacity) return false;
        ++depth;
        return true;
    }
    bool consume_end() {
        if (depth == 0U) return false;
        --depth;
        return true;
    }
};

// A partial sidecar is not a valid transaction: a refused begin/end could
// otherwise unbalance unrelated scopes later in the same display list.
struct MarkerRecordingIntegrity {
    bool valid = true;
    bool accept(bool metadata_valid, std::size_t count, std::size_t capacity) {
        valid = valid && metadata_valid && count < capacity;
        return valid;
    }
};

constexpr bool is_aspect_marker(PresentationMarkerKind kind) {
    return kind == PresentationMarkerKind::FramedResults ||
           kind == PresentationMarkerKind::FixedUi ||
           kind == PresentationMarkerKind::BackgroundAspect ||
           kind == PresentationMarkerKind::TrackSelectUi ||
           kind == PresentationMarkerKind::TrackSelectLensFlare;
}

constexpr bool valid_presentation_marker(PresentationMarkerKind kind,
                                         std::uint8_t mode,
                                         std::uint8_t variant) {
    if (variant > 31U) return false;
    if(kind==PresentationMarkerKind::CameraWorldBegin||kind==PresentationMarkerKind::CameraWorldEnd)return mode==0&&variant==0;
    if (kind == PresentationMarkerKind::Geometry) return mode <= 9U;
    if (kind == PresentationMarkerKind::SplitViewport ||
        kind == PresentationMarkerKind::PostraceFullViewport ||
        kind == PresentationMarkerKind::TrackSelectPreview) return mode == 0U;
    return (is_aspect_marker(kind) || kind == PresentationMarkerKind::HudPass ||
            kind == PresentationMarkerKind::HudWidget || kind == PresentationMarkerKind::HudRect) &&
           mode <= 1U;
}

// Compatibility only for the old encoded MoveWord entry point. Current
// producers use typed sidecars. Legacy control commands used modes 0/1;
// dynamic geometry modes must never be decoded as controls, even here.
constexpr PresentationMarkerKind legacy_presentation_marker_kind(
    std::uint8_t mode, std::uint8_t variant) {
    if (mode > 1U) return PresentationMarkerKind::Geometry;
    switch (variant) {
    case 26U: return PresentationMarkerKind::FramedResults;
    case 27U: return PresentationMarkerKind::FixedUi;
    case 28U: return PresentationMarkerKind::BackgroundAspect;
    case 29U: return PresentationMarkerKind::HudPass;
    case 30U: return PresentationMarkerKind::TrackSelectLensFlare;
    case 31U: return mode == 0U ? PresentationMarkerKind::SplitViewport
                              : PresentationMarkerKind::Geometry;
    default: return PresentationMarkerKind::Geometry;
    }
}

// Count every error, but perform stream I/O only for the first event and
// sparse summaries. Lifetime is the bridge, not a reset-per-task draw state.
struct PresentationDiagnosticBudget {
    std::uint64_t total = 0U;
    std::uint64_t pending = 0U;
    std::uint64_t last_report_task = 0U;

    bool record(std::uint64_t task) {
        ++total;
        ++pending;
        if (total != 1U && task - last_report_task < 120U) return false;
        last_report_task = task;
        return true;
    }

    std::uint64_t take_pending() {
        const auto count = pending;
        pending = 0U;
        return count;
    }
};

} // namespace dkr::runtime::presentation
