#pragma once

#include <algorithm>

namespace dkr::runtime::enhancements {

inline constexpr float kOriginalPresentationAspect = 4.0F / 3.0F;
inline constexpr float kSkyVerticalCorrectionAspect = 21.0F / 9.0F;
inline constexpr float kSkyVerticalCorrectionCover = kSkyVerticalCorrectionAspect / kOriginalPresentationAspect;

// cam_get_viewport_layout() uses 0 for the full-screen camera, 1 for the
// horizontal two-player split and 2/3 for the three/four-player quadrant
// layouts. Multiplayer deliberately renders a gradient/void backdrop instead
// of the detailed single-player skydome, so its coverage policy must be
// selected explicitly without changing the one-player path.
constexpr bool is_split_screen_sky_layout(int viewport_layout) {
    return viewport_layout >= 1 && viewport_layout <= 3;
}

// RT64's automatic aspect policy expands a projection only when its scissor
// covers the complete framebuffer width. DKR's three/four-player cameras are
// quadrants, so they require an explicit projection adjustment while keeping
// their authored viewport rectangles and HUD coordinates intact.
constexpr bool needs_split_world_aspect_adjust(bool modern_presentation, bool fit_to_window, int viewport_layout) {
    return modern_presentation && fit_to_window && (viewport_layout == 2 || viewport_layout == 3);
}

enum class WorldAspectPolicy {
    Automatic,
    AdjustToHost,
};

// World projection policy is intentionally limited to real gameplay cameras.
// Framed results and fixed-aspect UI use dedicated, exactly restored renderer
// scopes so a menu/background marker can never change the world policy.
constexpr WorldAspectPolicy world_aspect_policy(bool modern_presentation, bool fit_to_window, int viewport_layout) {
    if (!modern_presentation || !fit_to_window) {
        return WorldAspectPolicy::Automatic;
    }
    return needs_split_world_aspect_adjust(modern_presentation, fit_to_window, viewport_layout)
               ? WorldAspectPolicy::AdjustToHost
               : WorldAspectPolicy::Automatic;
}

struct SplitViewportHorizontalRange {
    float left;
    float right;
};

// RT64 deliberately keeps a projection inside the original presentation
// width when its viewport covers only part of a shared framebuffer. That is
// correct for authored picture-in-picture views, but DKR's three/four-player
// cameras are real quadrants and must collectively cover the expanded host
// canvas. Expand each quadrant away from the vertical divider: the left-hand
// cameras retain their right edge and the right-hand cameras retain their
// left edge. This preserves the divider exactly and works for both viewport
// and 10.2 fixed-point scissor coordinates.
constexpr SplitViewportHorizontalRange expand_split_viewport_horizontal_range(float left, float right,
                                                                              float horizontal_cover, int camera_id) {
    if (right == left || horizontal_cover <= 1.0F || camera_id < 0 || camera_id > 3) {
        return {left, right};
    }

    // Mirrored courses express handedness with a negative viewport X scale.
    // Expand its geometric bounds, then restore their original ordering.
    if (right < left) {
        const auto expanded = expand_split_viewport_horizontal_range(right, left, horizontal_cover, camera_id);
        return {expanded.right, expanded.left};
    }
    const float expanded_width = (right - left) * horizontal_cover;
    return (camera_id & 1) == 0 ? SplitViewportHorizontalRange{right - expanded_width, right}
                                : SplitViewportHorizontalRange{left, left + expanded_width};
}

// The Track Select preview is authored inside a wooden 4:3 frame and expands
// to the full 320x240 viewport only after a track is chosen. viewport_menu_set
// clamps its scissor to width - 1, which prevents RT64 AUTO from recognising
// the expanded state as full-width. Match only that terminal state: the
// framed preview and every intermediate expansion step remain untouched.
constexpr bool is_fullscreen_track_preview(int x1, int y1, int x2, int y2, int scissor_x1, int scissor_y1,
                                           int scissor_x2, int scissor_y2, int width, int height) {
    return width > 0 && height > 0 && x1 == 0 && y1 == 0 && x2 == width && y2 == height && scissor_x1 == 0 &&
           scissor_y1 == 0 && scissor_x2 == width - 1 && scissor_y2 == height - 1;
}

// The terminal-preview hook above normalises retail's inclusive 319x239
// scissor to 320x240 so RT64 can recognise the selected preview as a complete
// framebuffer. Later display-list hooks must accept both representations;
// otherwise they can mistake an already-normalised terminal preview for a
// framed one and restore 4:3 after the wooden frame has gone away.
constexpr bool is_presented_fullscreen_track_preview(int x1, int y1, int x2, int y2, int scissor_x1, int scissor_y1,
                                                     int scissor_x2, int scissor_y2, int width, int height) {
    if (width <= 0 || height <= 0 || x1 != 0 || y1 != 0 || x2 != width || y2 != height || scissor_x1 != 0 ||
        scissor_y1 != 0) {
        return false;
    }
    const bool authored_scissor = scissor_x2 == width - 1 && scissor_y2 == height - 1;
    const bool normalised_scissor = scissor_x2 == width && scissor_y2 == height;
    return authored_scissor || normalised_scissor;
}

constexpr bool preserve_track_select_lens_flare_tint(bool modern_presentation, bool fit_to_window,
                                                     bool tracks_menu_active, bool fullscreen_preview) {
    return modern_presentation && fit_to_window && tracks_menu_active && !fullscreen_preview;
}

// Preserve the player-approved uniform skydome cover through 21:9. Beyond
// that point, applying the full horizontal cover to the vertical projection
// drives DKR's authored horizon progressively upward. Invert only the excess
// vertical zoom, continuously at the 21:9 boundary, while horizontal coverage
// continues to grow with the viewport.
constexpr float sky_vertical_cover_scale(float horizontal_cover) {
    const float cover = std::max(horizontal_cover, 1.0F);
    if (cover <= kSkyVerticalCorrectionCover) {
        return cover;
    }
    return (kSkyVerticalCorrectionCover * kSkyVerticalCorrectionCover) / cover;
}

// The skydome shares the world's camera yaw and RT64's horizontal aspect
// expansion. Magnifying its X projection cancels that expansion and makes
// the sky pan faster than the world as the window gets wider. Only the
// separate vertical coverage correction belongs in its projection.
constexpr float sky_projection_column_scale(float horizontal_cover, unsigned column) {
    return column == 1U ? sky_vertical_cover_scale(horizontal_cover) : 1.0F;
}

constexpr float split_sky_horizontal_cover_scale(float horizontal_cover, int viewport_layout) {
    if (!is_split_screen_sky_layout(viewport_layout)) {
        return 1.0F;
    }

    // In DKR's two-player layout, each camera receives half the window's
    // height but its full width. Its effective aspect ratio is consequently
    // twice the host-window aspect. The old multiplayer correction used only
    // the host aspect, leaving both the gradient above the horizon and the
    // lower void at their original 4:3 width inside each player viewport.
    // Three/four-player cameras are quadrants (half width and half height), so
    // their effective aspect remains the host-window aspect.
    const float viewport_layout_scale = viewport_layout == 1 ? 2.0F : 1.0F;
    return std::max(horizontal_cover, 1.0F) * viewport_layout_scale;
}

constexpr float split_sky_vertical_cover_scale(float horizontal_cover, int viewport_layout) {
    return is_split_screen_sky_layout(viewport_layout)
               ? sky_vertical_cover_scale(split_sky_horizontal_cover_scale(horizontal_cover, viewport_layout))
               : 1.0F;
}

} // namespace dkr::runtime::enhancements
