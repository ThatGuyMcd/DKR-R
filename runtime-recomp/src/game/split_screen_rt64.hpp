#pragma once

#include "hle/rt64_rsp.h"
#include "hle/rt64_framebuffer_pair.h"
#include "hle/rt64_rdp.h"
#include "hud_layout_policy.hpp"

#include <algorithm>
#include <cmath>

namespace dkr::runtime::presentation {

inline RT64::ExtendedAlignment split_counter_alignment(RT64::ExtendedAlignment alignment, std::uint8_t anchor,
                                                       std::uint16_t native_width, float viewport_cover) {
    const bool right = anchor == hud::kHudAnchorRight;
    alignment.leftOrigin = alignment.rightOrigin = G_EX_ORIGIN_NONE;
    // The runtime keeps extAspectRatio=Original (extOriginPercentage=0), so
    // LEFT/RIGHT origins deliberately collapse to the centre. Translate only
    // this draw in signed 10.2 coordinates, after retail's rectangle clipping.
    // Equal offsets preserve both dimensions and UVs, and NONE avoids the
    // extended-origin native-pixel snapping. Do not change the global policy.
    const auto gutter = static_cast<std::int32_t>(
        std::lround(static_cast<float>(native_width) * 2.0F * (std::max(viewport_cover, 1.0F) - 1.0F)));
    alignment.leftOffset = alignment.rightOffset = right ? gutter : -gutter;
    return alignment;
}

inline RT64::ExtendedAlignment split_panel_alignment(std::uint16_t native_width, float viewport_cover) {
    RT64::ExtendedAlignment alignment{};
    alignment.leftOrigin = alignment.rightOrigin = G_EX_ORIGIN_NONE;
    alignment.rightOffset = static_cast<std::int32_t>(
        std::lround(static_cast<float>(native_width) * 2.0F * (std::max(viewport_cover, 1.0F) - 1.0F)));
    return alignment;
}

// F3DDKR's combined MVP remains in the model stack. Aspect correction belongs
// to the separate identity projection, not that model's interpolation group.
// Push/pop preserves the exact surrounding projection policy (including menus).
inline void select_split_world_projection(RT64::RSP& rsp, bool requested, bool& active) {
    if (requested == active)
        return;
    if (requested) {
        rsp.matrixId(G_EX_ID_IGNORE, true, true, false, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                     G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                     G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_ASPECT_ADJUST, G_EX_EDIT_NONE,
                     false, false);
    } else {
        rsp.popMatrixId(1, true);
    }
    active = requested;
    // A group change alone does not cause RSP::addCurrentProjection to record
    // a new projection. Without this, adjacent HUD draws can inherit the world
    // policy, or the first world draws can keep the preceding UI policy.
    rsp.projectionMatrixChanged = true;
    rsp.modelViewProjChanged = true;
}

// The bridge expands quadrant scissors beyond the native framebuffer to cover
// a wider host window. Individual draw scissors MUST retain that coverage, but
// their union is not a new native framebuffer width. RT64 uses this aggregate
// to infer the native aspect for rectangle rendering; treating overscan as a
// larger native framebuffer stretches HUD rectangles separately from sprites.
// Call only for color images explicitly tagged by a SplitViewport marker, and
// only on the producer-owned workload before the full-sync submission.
inline void normalise_split_framebuffer_extent(RT64::FramebufferPair& pair) {
    if (pair.scissorRect.isNull() || pair.colorImage.width != 320U)
        return;
    pair.scissorRect.ulx = std::max(pair.scissorRect.ulx, 0);
    pair.scissorRect.lrx = std::min(pair.scissorRect.lrx, 320 * 4);
}

} // namespace dkr::runtime::presentation
