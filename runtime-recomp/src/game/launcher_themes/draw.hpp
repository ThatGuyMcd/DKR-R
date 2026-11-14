#pragma once
#include "pixels.hpp"
#include "art.hpp"
#include "imgui.h"
#include <cmath>

namespace dkr::launcher_themes {
inline constexpr float Pi = 3.14159265359F;
inline ImU32 colour(unsigned rgb, unsigned alpha = 255) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha);
}
inline ImVec2 point(ImVec2 at, float size, float x, float y) {
    return {at.x + size * x, at.y + size * y};
}
inline float random01(unsigned value) {
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    return float(value & 65535U) / 65535.F;
}
inline void weather(ImDrawList* draw, ImVec2 at, ImVec2 extent, Theme theme, double seconds, Calendar calendar) {
    if (theme == Theme::Original || extent.x <= 0 || extent.y <= 0)
        return;
    const float time = float(std::fmod(seconds, 3600.0));
    const auto accent = Palettes[int(theme)].accent;
    const bool snow = theme == Theme::Christmas || theme == Theme::Winter;
    const bool leaves = theme == Theme::Autumn || theme == Theme::Thanksgiving;
    const bool petals = theme == Theme::Spring || theme == Theme::Easter || theme == Theme::Valentine;
    const bool fireworks = theme == Theme::Bonfire || theme == Theme::NewYear;
    draw->PushClipRect(at, {at.x + extent.x, at.y + extent.y}, true);
    for (unsigned i = 0; i < 40; ++i) {
        const float seed = random01(i * 17 + 3), phase = random01(i * 29 + 7);
        float x = std::fmod(seed * extent.x + time * (snow ? 7 : 3), extent.x);
        const float vertical = phase * extent.y + time * (snow ? 17 : leaves ? 23 : petals ? 13 : -8);
        const float y = std::fmod(std::fmod(vertical, extent.y) + extent.y, extent.y);
        const ImVec2 p{at.x + x, at.y + y};
        if (snow)
            draw->AddCircleFilled(p, 1.4F + seed * 1.6F, colour(0xF3FAFF, 140), 8);
        else if (leaves || petals) {
            const float r = 2.5F + seed * 2.5F;
            const unsigned fill = leaves ? (i % 2 ? 0xC85F34 : 0xE8AD56) : (i % 2 ? 0xF7D7E9 : 0xF4ECB9);
            draw->AddQuadFilled({p.x - r, p.y}, {p.x, p.y - r}, {p.x + r, p.y}, {p.x, p.y + r}, colour(fill, 150));
        } else if (!fireworks)
            draw->AddCircleFilled(p, 1.2F + seed, colour(accent, 110), 6);
    }
    if (fireworks)
        for (unsigned i = 0; i < 3; ++i) {
            const float phase = std::fmod(time * .15F + float(i) * .33F, 1.F), radius = 8 + phase * 48;
            const ImVec2 center{at.x + extent.x * (.15F + float(i) * .34F),
                                at.y + extent.y * (.13F + float(i % 2) * .13F)};
            for (int ray = 0; ray < 12; ++ray) {
                const float angle = float(ray) * Pi / 6;
                draw->AddLine({center.x + std::cos(angle) * radius * .7F, center.y + std::sin(angle) * radius * .7F},
                              {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius},
                              colour(accent, unsigned(160 * (1 - phase))), 1.7F);
            }
        }
    if (theme == Theme::Christmas || theme == Theme::Lunar || theme == Theme::Hanukkah || theme == Theme::National) {
        const int count = theme == Theme::Hanukkah ? 9 : 18;
        const float length = std::min(extent.x * .48F, 700.F), begin = at.x + (extent.x - length) * .5F;
        draw->AddLine({begin, at.y + 9}, {begin + length, at.y + 9}, colour(0xFFF0CB, 130), 1);
        for (int i = 0; i < count; ++i) {
            const float x = begin + (float(i) + .5F) * length / float(count);
            unsigned fill = theme == Theme::Christmas ? (i % 2 ? 0xF1C75B : 0xDC5967) : accent;
            if (theme == Theme::National)
                fill = calendar == Calendar::Canada ? (i % 2 ? 0xFFF4DF : 0xDA5364)
                                                    : (i % 3 == 0   ? 0xDA5364
                                                       : i % 3 == 1 ? 0xFFF4DF
                                                                    : 0x6F9FD8);
            if (theme == Theme::National)
                draw->AddTriangleFilled({x - 6, at.y + 10}, {x + 6, at.y + 10}, {x, at.y + 23}, colour(fill, 220));
            else
                draw->AddCircleFilled({x, at.y + 15 + (theme == Theme::Lunar ? 4.F : 0.F)},
                                      theme == Theme::Lunar ? 5.F : 3.F, colour(fill, 230), 10);
        }
    }
    draw->PopClipRect();
}
inline void corners(ImDrawList* draw, ImVec2 at, ImVec2 extent, Theme theme, ImTextureID atlas = {},
                    Calendar calendar = Calendar::International) {
    if (!atlas || extent.x < 180 || extent.y < 180 || !std::isfinite(extent.x) || !std::isfinite(extent.y))
        return;
    const auto [first, second] = icons_for(theme, calendar);
    const float size = std::clamp(extent.y * .065F, 52.F, 88.F), inset = 10.F;
    const auto image = [&](Icon icon, ImVec2 pos) {
        if (int(icon) < 0 || icon >= Icon::Count)
            return;
        const int x = int(icon) % ArtColumns, y = int(icon) / ArtColumns;
        draw->AddImage(atlas, pos, {pos.x + size, pos.y + size}, {float(x) / ArtColumns, float(y) / ArtRows},
                       {float(x + 1) / ArtColumns, float(y + 1) / ArtRows});
    };
    draw->PushClipRect(at, {at.x + extent.x, at.y + extent.y}, true);
    image(first, {at.x + inset, at.y + inset});
    image(second, {at.x + extent.x - size - inset, at.y + extent.y - size - inset});
    draw->PopClipRect();
}
} // namespace dkr::launcher_themes
