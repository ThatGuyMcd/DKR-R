#pragma once

#include <cstdint>

namespace dkr::runtime::water {
// Native-only extension of RT64's texcoordInterpolation byte. Never emit this
// through the packed N64 GBI (which has only two component bits). The checked
// game-frame adapter decodes it; ordinary RT64 component values stay intact.
// Keeping the period in the immutable transform group gives it the same
// queue/frame lifetime as its vertices, without a global side table or locks.
constexpr bool is_scroll_tag(std::uint8_t value) {
    return (value & 0xC0U) == 0x80U;
}
constexpr std::uint8_t scroll_tag(std::uint32_t mask_u, std::uint32_t mask_v) {
    const auto exponent = [](std::uint32_t mask) constexpr -> int {
        for (int n = 0; n <= 7; ++n)
            if (mask == (32U << n) - 1U) return n;
        return -1;
    };
    const int u = exponent(mask_u), v = exponent(mask_v);
    return u < 0 || v < 0 ? 0U : std::uint8_t(0x80U | (u << 3U) | v);
}
constexpr float scroll_period(std::uint8_t tag, unsigned axis) {
    return is_scroll_tag(tag) && axis < 2U
        ? float(1U << ((tag >> (axis == 0U ? 3U : 0U)) & 7U)) : 0.0F;
}
}
