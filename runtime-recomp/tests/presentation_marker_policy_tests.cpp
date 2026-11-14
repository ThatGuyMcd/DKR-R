#include "presentation_identity.hpp"
#include "interpolation_state_policy.hpp"
#include "track_performance.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

using namespace dkr::runtime::presentation;
using dkr::runtime::interpolation::GroupState;

int main() {
    // Every valid identity variant belongs to geometry, including the six
    // values formerly mistaken for UI/viewport commands by the live decoder.
    for (std::uint8_t mode = 0U; mode <= 9U; ++mode) {
        for (std::uint8_t variant = 0U; variant < 32U; ++variant) {
            const PresentationMarker marker{mode, 123U, variant, PresentationMarkerKind::Geometry};
            assert(valid_presentation_marker(marker.kind, marker.mode, marker.variant));
            assert(!is_aspect_marker(marker.kind));
            assert(marker.kind != PresentationMarkerKind::HudPass);
            assert(marker.kind != PresentationMarkerKind::SplitViewport);
            assert(marker.variant == variant);
        }
    }
    for (std::uint8_t variant = 0U; variant < 32U; ++variant) {
        GroupState state;
        state.load_matrix(0U, 100U);
        const auto segment = make_level_segment_group_identity(1U, 0U, 1U);
        const auto surface = make_surface_group_identity(2U, variant);
        assert(state.begin_scope(9U, segment, false));
        assert(state.begin_scope(7U, surface, false, true, true));
        assert(state.active_group().identity == surface);
        assert(state.active_group().interpolate_tiles);
        assert(state.active_group().interpolate_texcoords);
        assert(state.end_scope().ended.mode == 7U);
        assert(state.active_group().identity == segment);
        assert(state.end_scope().ended.mode == 9U);
        assert(state.active_group().identity == 100U);
        assert(!state.has_active_scope());
        // Compatibility decoder must not repeat the dynamic geometry bug.
        for (std::uint8_t mode : {2U, 4U, 6U, 7U}) {
            assert(legacy_presentation_marker_kind(mode, variant) == PresentationMarkerKind::Geometry);
        }
    }
    for (auto kind : {PresentationMarkerKind::FramedResults, PresentationMarkerKind::FixedUi,
                      PresentationMarkerKind::BackgroundAspect, PresentationMarkerKind::HudPass,
                      PresentationMarkerKind::TrackSelectUi, PresentationMarkerKind::TrackSelectLensFlare}) {
        assert(valid_presentation_marker(kind, 0U, 0U));
        assert(valid_presentation_marker(kind, 1U, 0U));
        assert(!valid_presentation_marker(kind, 7U, 0U));
        assert(is_aspect_marker(kind) == (kind != PresentationMarkerKind::HudPass));
    }
    assert(valid_presentation_marker(PresentationMarkerKind::SplitViewport, 0U, 0U));
    assert(!valid_presentation_marker(PresentationMarkerKind::SplitViewport, 1U, 0U));
    assert(valid_presentation_marker(PresentationMarkerKind::PostraceFullViewport, 0U, 0U));
    assert(!valid_presentation_marker(PresentationMarkerKind::PostraceFullViewport, 1U, 0U));
    assert(!is_aspect_marker(PresentationMarkerKind::PostraceFullViewport));
    assert(valid_presentation_marker(PresentationMarkerKind::TrackSelectPreview, 0U, 0U));
    assert(!valid_presentation_marker(PresentationMarkerKind::TrackSelectPreview, 1U, 0U));
    assert(!is_aspect_marker(PresentationMarkerKind::TrackSelectPreview));
    assert(!valid_presentation_marker(PresentationMarkerKind::Geometry, 10U, 0U));
    assert(!valid_presentation_marker(PresentationMarkerKind::Geometry, 7U, 32U));
    assert(!valid_presentation_marker(static_cast<PresentationMarkerKind>(255), 0U, 0U));
    assert(legacy_presentation_marker_kind(1U, 26U) == PresentationMarkerKind::FramedResults);
    assert(legacy_presentation_marker_kind(0U, 27U) == PresentationMarkerKind::FixedUi);
    assert(legacy_presentation_marker_kind(1U, 28U) == PresentationMarkerKind::BackgroundAspect);
    assert(legacy_presentation_marker_kind(1U, 29U) == PresentationMarkerKind::HudPass);
    assert(legacy_presentation_marker_kind(0U, 30U) == PresentationMarkerKind::TrackSelectLensFlare);
    assert(legacy_presentation_marker_kind(0U, 31U) == PresentationMarkerKind::SplitViewport);
    assert(legacy_presentation_marker_kind(1U, 31U) == PresentationMarkerKind::Geometry);

    PresentationDiagnosticBudget budget;
    assert(budget.record(10U));
    assert(budget.take_pending() == 1U);
    for (unsigned i = 0; i < 10000U; ++i)
        assert(!budget.record(10U));
    assert(!budget.record(129U));
    assert(budget.record(130U));
    assert(budget.total == 10003U);
    assert(budget.take_pending() == 10002U);
    assert(budget.pending == 0U);

    GroupState full;
    for (std::size_t i = 0U; i < dkr::runtime::interpolation::kMaxPresentationScopeDepth; ++i) {
        assert(full.begin_scope(7U, 100U + static_cast<std::uint32_t>(i), false));
    }
    const auto outer = full.active_group().identity;
    assert(!full.begin_scope(6U, 999U, true));
    assert(!full.begin_scope(7U, 998U, false));
    assert(full.end_scope().rejected_begin);
    assert(full.end_scope().rejected_begin);
    assert(full.active_group().identity == outer);
    assert(full.end_scope().had_scope);
    full.reset();
    assert(!full.has_active_scope());
    assert(full.rejected_scope_begins() == 0U);

    for (const std::size_t capacity : {8U, 16U}) {
        RejectedMarkerScopes rejected;
        assert(!rejected.reject_begin(capacity - 1U, capacity));
        assert(rejected.reject_begin(capacity, capacity));
        assert(rejected.reject_begin(capacity - 1U, capacity));
        assert(rejected.consume_end());
        assert(rejected.consume_end());
        assert(!rejected.consume_end());
        assert(!rejected.reject_begin(0U, capacity));
        rejected = {};
        assert(!rejected.consume_end());
    }
    MarkerRecordingIntegrity recording;
    for (std::size_t i = 0U; i < kMaximumMarkersPerCommand; ++i) {
        assert(recording.accept(true, i, kMaximumMarkersPerCommand));
    }
    assert(!recording.accept(true, kMaximumMarkersPerCommand, kMaximumMarkersPerCommand));
    assert(!recording.accept(true, 0U, kMaximumMarkersPerCommand));
    recording = {};
    assert(recording.accept(true, 0U, kMaximumMarkersPerCommand));
    assert(!recording.accept(false, 0U, kMaximumMarkersPerCommand));
    assert(!recording.valid);
    dkr::runtime::track_performance::Samples samples{};
    assert(dkr::runtime::track_performance::summarize(samples).count == 0U);
    for (std::size_t i = 0U; i < samples.size(); ++i)
        samples[i] = double(i + 1U);
    const auto timings = dkr::runtime::track_performance::summarize(samples);
    assert(timings.count == 120U && timings.median == 60.0);
    assert(timings.p95 == 114.0 && timings.p99 == 119.0);
    samples[0] = -1.0;
    samples[1] = std::numeric_limits<double>::quiet_NaN();
    assert(dkr::runtime::track_performance::summarize(samples).count == 118U);
    std::puts("[test][presentation-marker-policy] PASS");
}
