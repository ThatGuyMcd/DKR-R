#pragma once

#include <cstdint>

namespace dkr::runtime::presentation {

inline constexpr std::uint32_t kCanonicalViWidth = 320U;
inline constexpr std::uint32_t kCanonicalViHeight = 240U;

// DKR's NTSC LPN1 VI uses ORIGIN(640), then fb_init_vi enables divot and
// dither filtering and disables gamma (libultra also clears the AA mode).
// VI origin is a scanout pointer, NOT the color-image base: RT64::VI::fbAddress
// removes this one 16-bit scanline before finding the high-resolution target.
inline constexpr std::uint32_t kRetailNtscViStatus = 0x00013016U;
constexpr std::uint32_t retail_ntsc_vi_origin(std::uint32_t color_image) {
    return (color_image & 0x007FFFFFU) + kCanonicalViWidth * 2U;
}

// Native VI scheduling owns a live buffer history. An immutable owned frame
// instead specifies the exact VI it must present; history-based early present
// can select another scene's buffer and bypass that explicit VI entirely.
constexpr bool present_from_vi_history(bool owned_frame) {
    return !owned_frame;
}

// gDPSetScissor stores its lower-right corner in quarter-pixel units and
// treats that corner as exclusive. DKR's full-frame clear incorrectly emits
// (width - 1, height - 1), which leaves the final framebuffer row and column
// outside the clear. Keep this correction narrowly conditional so authored
// viewport, menu-frame and split-screen scissors are never rewritten.
constexpr std::uint32_t pack_scissor_lower_right(std::uint32_t width,
                                                 std::uint32_t height) {
    return (((width << 2U) & 0xFFFU) << 12U) |
           ((height << 2U) & 0xFFFU);
}

constexpr std::uint32_t correct_fullscreen_clear_scissor(
    std::uint32_t lower_right, std::uint32_t width,
    std::uint32_t height) {
    if (width == 0U || height == 0U || width > 0x3FFU ||
        height > 0x3FFU) {
        return lower_right;
    }
    const std::uint32_t retail_lower_right =
        pack_scissor_lower_right(width - 1U, height - 1U);
    return lower_right == retail_lower_right
        ? pack_scissor_lower_right(width, height)
        : lower_right;
}

constexpr std::uint32_t vi_region_start(std::uint32_t region) {
    return (region >> 16U) & 0x3FFU;
}

constexpr std::uint32_t vi_region_end(std::uint32_t region) {
    return region & 0x3FFU;
}

constexpr std::uint32_t round_positive(double value) {
    return static_cast<std::uint32_t>(value + 0.5);
}

// Mirrors RT64::VI::fbSize for the progressive vertical path. RT64 adds two
// guard rows and rounds the result to a multiple of four; DKR's otherwise
// canonical 320x240 signal can consequently be inferred as 244 rows.
constexpr std::uint32_t inferred_vi_height(std::uint32_t v_region,
                                           std::uint32_t y_scale) {
    const std::uint32_t start = vi_region_start(v_region);
    const std::uint32_t end = vi_region_end(v_region);
    y_scale &= 0xFFFU;
    if (end <= start || y_scale == 0U) {
        return 0U;
    }

    const double sampled_rows =
        (static_cast<double>(end - start) * static_cast<double>(y_scale)) /
        2048.0;
    const std::uint32_t with_guard_rows = round_positive(sampled_rows) + 2U;
    return round_positive(static_cast<double>(with_guard_rows) / 4.0) * 4U;
}

// Keep the authored start line and trim only enough of RT64's inferred guard
// region for the final present to resolve to 240 rows. This is presentation
// metadata only; the game's framebuffer, projection and display lists remain
// untouched. Signals that are not the small 240/244 DKR family pass through.
constexpr std::uint32_t canonicalise_dkr_v_region(std::uint32_t v_region,
                                                  std::uint32_t y_scale) {
    const std::uint32_t height = inferred_vi_height(v_region, y_scale);
    if (height <= kCanonicalViHeight || height > kCanonicalViHeight + 8U) {
        return v_region;
    }

    const std::uint32_t start = vi_region_start(v_region);
    const std::uint32_t end = vi_region_end(v_region);
    for (std::uint32_t trim = 1U; trim <= 16U && end > start + trim; ++trim) {
        const std::uint32_t candidate =
            (v_region & 0xFFFFFC00U) | ((end - trim) & 0x3FFU);
        if (inferred_vi_height(candidate, y_scale) == kCanonicalViHeight) {
            return candidate;
        }
    }
    return v_region;
}

} // namespace dkr::runtime::presentation
