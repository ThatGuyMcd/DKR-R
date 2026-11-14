#include "interpolation_state_policy.hpp"

#include <cassert>
#include <cstdio>

using dkr::runtime::interpolation::effective_tile_interpolation;
using dkr::runtime::interpolation::GroupState;
using dkr::runtime::interpolation::kAspectAdjustScopeMode;
using dkr::runtime::interpolation::kAspectOriginalScopeMode;
using dkr::runtime::interpolation::kBillboardScopeMode;
using dkr::runtime::interpolation::kLevelSegmentScopeMode;
using dkr::runtime::interpolation::kShadowScopeMode;
using dkr::runtime::interpolation::kSurfaceScopeMode;
using dkr::runtime::interpolation::kVehiclePartScopeMode;
using dkr::runtime::interpolation::scope_identity_uses_selected_matrix;

int main() {
    assert(!effective_tile_interpolation(true, 0U));
    assert(effective_tile_interpolation(true, 1U));
    assert(effective_tile_interpolation(true, 7U));
    assert(!effective_tile_interpolation(false, 0U));
    assert(!effective_tile_interpolation(false, 7U));

    // Projected shadows remain semantic-owner scoped. Repeated billboards,
    // vehicle parts and surfaces inherit their selected matrix identity.
    assert(!scope_identity_uses_selected_matrix(kShadowScopeMode));
    assert(scope_identity_uses_selected_matrix(kBillboardScopeMode));
    assert(scope_identity_uses_selected_matrix(kVehiclePartScopeMode));
    assert(scope_identity_uses_selected_matrix(kSurfaceScopeMode));
    assert(scope_identity_uses_selected_matrix(kLevelSegmentScopeMode));
    assert(!scope_identity_uses_selected_matrix(0U));

    GroupState state{};
    constexpr std::uint32_t world = 0x11111111U;
    constexpr std::uint32_t object = 0x22222222U;
    constexpr std::uint32_t billboard = 0x33333333U;
    constexpr std::uint32_t shadow = 0x44444444U;

    state.load_matrix(0U, world);
    state.load_matrix(1U, object);
    state.load_matrix(2U, billboard, true, true, true);
    assert(state.active_group().identity == billboard);

    // Aspect-only scopes must preserve the selected matrix identity so
    // split-screen widening cannot disable or cross-wire interpolation.
    assert(state.begin_scope(kAspectAdjustScopeMode, 0U, false));
    assert(state.contains_mode(kAspectAdjustScopeMode));
    assert(state.active_group().identity == billboard);
    state.select_matrix(1U);
    assert(state.active_group().identity == object);
    assert(state.end_scope().ended.mode == kAspectAdjustScopeMode);
    assert(state.begin_scope(kAspectOriginalScopeMode, 0U, false));
    assert(state.contains_mode(kAspectOriginalScopeMode));
    assert(state.active_group().identity == object);
    assert(state.end_scope().ended.mode == kAspectOriginalScopeMode);
    state.select_matrix(2U);
    assert(state.active_group().interpolate_vertices);
    assert(state.active_group().interpolate_texcoords);
    assert(state.active_group().interpolate_tiles);
    state.load_matrix(1U, object);

    // Regression: gSPSelectMatrixDKR used to restore only the matrix. The
    // billboard/object ID remained active and was then inherited by terrain.
    state.select_matrix(0U);
    assert(state.active_group().identity == world);
    state.select_matrix(1U);
    assert(state.active_group().identity == object);

    // A scoped draw overrides the selected slot but must restore that exact
    // slot when it ends. Matrices loaded while scoped are retained for later.
    assert(state.begin_scope(2U, shadow, true));
    assert(state.active_group().identity == shadow);
    assert(state.active_group().interpolate_vertices);
    assert(!state.active_group().interpolate_texcoords);
    assert(!state.active_group().interpolate_tiles);
    state.load_matrix(2U, billboard, true, true);
    assert(state.active_group().identity == shadow);
    const auto ended_shadow = state.end_scope();
    assert(ended_shadow.had_scope && ended_shadow.ended.mode == 2U);
    assert(state.active_group().identity == billboard);

    // Nested scopes restore their parent rather than falling straight back to
    // world geometry. This protects transitions and dynamically generated
    // billboards that can be emitted from another presentation scope.
    assert(state.begin_scope(3U, 0U, false));
    assert(state.contains_mode(3U));
    assert(state.begin_scope(6U, billboard, true));
    assert(state.active_group().identity == billboard);
    assert(state.active_group().interpolate_vertices);

    assert(state.begin_scope(7U, 0x55555555U, false, true, true));
    assert(!state.active_group().interpolate_vertices);
    assert(state.active_group().interpolate_texcoords);
    assert(state.active_group().interpolate_tiles);
    assert(state.end_scope().ended.mode == 7U);
    assert(state.end_scope().ended.mode == 6U);
    assert(state.active_group().mode == 3U);
    assert(state.contains_mode(3U));
    assert(state.end_scope().ended.mode == 3U);
    assert(!state.has_active_scope());
    assert(!state.contains_mode(3U));
    assert(state.active_group().identity == billboard);

    // A level segment owns the outer static-geometry identity while a water
    // batch may temporarily override it. Closing the water scope must restore
    // the segment; closing the segment restores the selected world matrix.
    state.select_matrix(0U);
    constexpr std::uint32_t segment = 0x66666666U;
    constexpr std::uint32_t surface = 0x77777777U;
    assert(state.begin_scope(kLevelSegmentScopeMode, segment, false));
    assert(state.active_group().identity == segment);
    assert(!state.active_group().interpolate_vertices);
    assert(!state.active_group().interpolate_texcoords);
    assert(!state.active_group().interpolate_tiles);
    assert(state.begin_scope(kSurfaceScopeMode, surface, false, true, true));
    assert(state.active_group().identity == surface);
    assert(state.end_scope().ended.mode == kSurfaceScopeMode);
    assert(state.active_group().identity == segment);
    assert(state.end_scope().ended.mode == kLevelSegmentScopeMode);
    assert(state.active_group().identity == world);

    state.select_matrix(99U);
    assert(state.selected_matrix() == 2U);
    assert(!state.end_scope().had_scope);

    std::puts("[test][interpolation-state-policy] PASS");
    return 0;
}
