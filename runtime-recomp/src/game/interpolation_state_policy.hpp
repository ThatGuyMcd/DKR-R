#pragma once

#include "presentation_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::interpolation {

// F3DDKR exposes three independently selectable combined-MVP slots. RT64's
// interpolation identity is state beside that matrix, so selecting an older
// slot must restore both pieces together.
inline constexpr std::size_t kMatrixSlotCount = 3U;
inline constexpr std::size_t kMaxPresentationScopeDepth = 32U;
inline constexpr std::uint32_t kTitleMenuId = 0U;
inline constexpr std::uint8_t kShadowScopeMode = 2U;
inline constexpr std::uint8_t kVehiclePartScopeMode = 4U;
inline constexpr std::uint8_t kAspectAdjustScopeMode = 5U;
inline constexpr std::uint8_t kBillboardScopeMode = 6U;
inline constexpr std::uint8_t kSurfaceScopeMode = 7U;
// Sidecar-only presentation mode. The legacy display-list marker reserves
// bit 3 for its variant, so authored-aspect scopes are emitted only through
// the host marker map and never encoded into a retail display-list word.
inline constexpr std::uint8_t kAspectOriginalScopeMode = 8U;
// Level geometry is submitted in BSP-dependent order. Give every segment/pass
// a sidecar-only owner so a draw cannot be paired with whichever neighbouring
// segment occupied the same linear position in the preceding authored frame.
inline constexpr std::uint8_t kLevelSegmentScopeMode = 9U;

constexpr bool is_aspect_policy_scope(std::uint8_t mode) {
    return mode == kAspectAdjustScopeMode ||
           mode == kAspectOriginalScopeMode;
}

// A projected shadow owns world-space geometry and must not inherit the
// selected model matrix: its semantic owner remains stable while the ground
// projection changes. Ordinary billboards are different. One scenery object
// can draw the same sprite through several matrices, so its selected matrix is
// required to distinguish those instances and to carry camera continuity.
// Vehicle parts, animated surfaces and static level segments are likewise
// matrix-relative. Static segment vertices do not deform; the selected world
// matrix supplies the interpolated camera transform.
constexpr bool scope_identity_uses_selected_matrix(std::uint8_t mode) {
    return mode == kVehiclePartScopeMode ||
           mode == kBillboardScopeMode ||
           mode == kSurfaceScopeMode ||
           mode == kLevelSegmentScopeMode;
}

// The title demo contains two water-heavy scripted shots. RT64's tile matcher
// has no stable one-to-one pairing for their rapidly recycled procedural wave
// tiles and can spend tens of milliseconds searching each authored frame.
// Geometry and UV interpolation remain enabled; only tile-state interpolation
// falls back to the authored cadence while the title demo owns the frontend.
constexpr bool effective_tile_interpolation(bool requested,
                                            std::uint32_t current_menu_id) {
    return requested && current_menu_id != kTitleMenuId;
}

struct Group {
    std::uint32_t identity = presentation::kIgnoredIdentity;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
    std::uint8_t mode = 0U;
    bool procedural_water = false;
    std::uint8_t water_scroll_tag = 0U;
};

struct EndScopeResult {
    Group ended{};
    bool had_scope = false;
    bool rejected_begin = false;
};

class GroupState {
public:
    constexpr void reset() {
        matrix_groups_ = {};
        scopes_ = {};
        selected_matrix_ = 0U;
        scope_depth_ = 0U;
        rejected_scope_begins_ = 0U;
        rejected_scope_depth_ = 0U;
    }

    constexpr void load_matrix(std::size_t slot, std::uint32_t identity,
                               bool interpolate_vertices = false,
                               bool interpolate_texcoords = false,
                               bool interpolate_tiles = false,
                               bool procedural_water = false,
                               std::uint8_t water_scroll_tag = 0U) {
        selected_matrix_ = clamp_slot(slot);
        matrix_groups_[selected_matrix_] = Group{
            identity, interpolate_vertices, interpolate_texcoords,
            interpolate_tiles, 0U, procedural_water, water_scroll_tag};
    }

    constexpr void select_matrix(std::size_t slot) {
        selected_matrix_ = clamp_slot(slot);
    }

    [[nodiscard]] constexpr std::size_t selected_matrix() const {
        return selected_matrix_;
    }

    [[nodiscard]] constexpr Group matrix_group(std::size_t slot) const {
        return matrix_groups_[clamp_slot(slot)];
    }

    [[nodiscard]] constexpr bool begin_scope(std::uint8_t mode,
                                             std::uint32_t identity,
                                             bool interpolate_vertices,
                                             bool interpolate_texcoords = false,
                                             bool interpolate_tiles = false) {
        if (rejected_scope_depth_ != 0U || scope_depth_ >= scopes_.size()) {
            ++rejected_scope_begins_;
            ++rejected_scope_depth_;
            return false;
        }
        scopes_[scope_depth_++] = Group{
            identity, interpolate_vertices, interpolate_texcoords,
            interpolate_tiles, mode};
        return true;
    }

    [[nodiscard]] constexpr EndScopeResult end_scope() {
        // A rejected nested begin still has a matching end command. Consume
        // that end without popping a successfully recorded outer scope.
        if (rejected_scope_depth_ != 0U) {
            --rejected_scope_depth_;
            return {{}, false, true};
        }
        if (scope_depth_ == 0U) {
            return {};
        }
        const Group ended = scopes_[--scope_depth_];
        scopes_[scope_depth_] = {};
        return {ended, true};
    }

    [[nodiscard]] constexpr Group active_group() const {
        if (scope_depth_ == 0U ||
            is_aspect_policy_scope(scopes_[scope_depth_ - 1U].mode)) {
            // Aspect scopes carry only RT64 projection policy. They must not
            // replace the selected matrix's interpolation identity, including
            // when a new world matrix is loaded after the scope begins.
            auto group = matrix_groups_[selected_matrix_];
            // Aspect changes restore the existing matrix identity, but must
            // not grant the water fast path to an enclosing shadow, scenery
            // or vehicle scope. Only provenance changes; identity policy does
            // not. Rejected scopes also fall back to the original draw path.
            if (rejected_scope_depth_ != 0U) group.procedural_water = false;
            for (std::size_t i = 0; group.procedural_water && i < scope_depth_; ++i)
                if (!is_aspect_policy_scope(scopes_[i].mode)) group.procedural_water = false;
            if (!group.procedural_water) group.water_scroll_tag = 0U;
            return group;
        }
        return scopes_[scope_depth_ - 1U];
    }

    [[nodiscard]] constexpr bool has_active_scope() const {
        return scope_depth_ != 0U;
    }

    [[nodiscard]] constexpr std::size_t scope_depth() const {
        return scope_depth_;
    }

    [[nodiscard]] constexpr std::uint32_t rejected_scope_begins() const {
        return rejected_scope_begins_;
    }

    [[nodiscard]] constexpr bool has_rejected_scope() const {
        return rejected_scope_depth_ != 0U;
    }

    [[nodiscard]] constexpr bool contains_mode(std::uint8_t mode) const {
        for (std::size_t index = 0U; index < scope_depth_; ++index) {
            if (scopes_[index].mode == mode) {
                return true;
            }
        }
        return false;
    }

private:
    [[nodiscard]] static constexpr std::size_t clamp_slot(std::size_t slot) {
        return slot < kMatrixSlotCount ? slot : kMatrixSlotCount - 1U;
    }

    std::array<Group, kMatrixSlotCount> matrix_groups_{};
    std::array<Group, kMaxPresentationScopeDepth> scopes_{};
    std::size_t selected_matrix_ = 0U;
    std::size_t scope_depth_ = 0U;
    std::uint32_t rejected_scope_begins_ = 0U;
    std::uint32_t rejected_scope_depth_ = 0U;
};

} // namespace dkr::runtime::interpolation
