#pragma once
#include "policy.hpp"
#include <algorithm>
#include <cstddef>
#include <span>

namespace dkr::launcher_themes {
struct Palette {
    unsigned dark, light, accent;
};
inline constexpr std::array<Palette, int(Theme::Count)> Palettes{{{0x164DA6, 0xA5E0FF, 0xFFCB55},
                                                                  {0x203E67, 0xADD2E3, 0xE4F7FF},
                                                                  {0x3C8575, 0xD4F3C5, 0xFFE3EC},
                                                                  {0x087CD1, 0x94DCFF, 0xFFE37B},
                                                                  {0x884424, 0xFFD18D, 0xFFB84C},
                                                                  {0x25133F, 0x8662BB, 0xFFB84D},
                                                                  {0x0D1B39, 0x345776, 0xFFC65C},
                                                                  {0x5C96B7, 0xEFFBFF, 0xE64157},
                                                                  {0x080F2B, 0x243B64, 0xFFE8A1},
                                                                  {0x925471, 0xF2BED1, 0xFAE0E9},
                                                                  {0x125435, 0x70B86F, 0xFFE6A1},
                                                                  {0x8B7AB2, 0xE0D3F5, 0xFFE4A8},
                                                                  {0x670E24, 0xD64B56, 0xFFE097},
                                                                  {0x241136, 0x704679, 0xFFD578},
                                                                  {0x135D65, 0x60B4B6, 0xFFE5A5},
                                                                  {0x0F332E, 0x4F7968, 0xF7EDC9},
                                                                  {0x0D286D, 0x427FCC, 0xE4F2FF},
                                                                  {0x815022, 0xE9C078, 0xFFE0A0},
                                                                  {0x163E78, 0x7CB7DB, 0xFFE5AC}}};
inline bool tint(std::span<unsigned char> pixels, int width, int height, Theme theme) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        pixels.size() != std::size_t(width) * std::size_t(height) * 4 || int(theme) < 0 || theme >= Theme::Count)
        return false;
    if (theme == Theme::Original)
        return true;
    const auto palette = Palettes[int(theme)];
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned luminance = (54U * pixels[i] + 183U * pixels[i + 1] + 19U * pixels[i + 2]) >> 8;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned shift = (2 - channel) * 8;
            const unsigned dark = (palette.dark >> shift) & 255, light = (palette.light >> shift) & 255;
            pixels[i + channel] = static_cast<unsigned char>((dark * (255 - luminance) + light * luminance) / 255);
        }
    }
    return true;
}
} // namespace dkr::launcher_themes
