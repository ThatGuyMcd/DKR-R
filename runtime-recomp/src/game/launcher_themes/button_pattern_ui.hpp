#pragma once
#include "button_pattern.hpp"
#include "settings.hpp"
#include "imgui.h"

namespace dkr::launcher_themes {
inline void draw_button_pattern(ImDrawList* draw, ImVec2 a, ImVec2 b, const ImFontAtlasCustomRect& rect, float rounding,
                                float opacity, double seconds) {
    auto* atlas = ImGui::GetIO().Fonts;
    const int tile = button_variant(displayed, preferences.calendar);
    const float row = float(rect.Y + tile / ButtonPatternColumns * ButtonTileHeight);
    const float column = float(rect.X + tile % ButtonPatternColumns * ButtonAtlasTileWidth) + ButtonTileWidth * .5F;
    const float scale = (b.y - a.y) / ButtonTileHeight;
    if (scale <= 0 || b.x <= a.x)
        return;
    const float tile_width = ButtonTileWidth * scale;
    if (tile_width < 1.F)
        return;
    rounding = std::clamp(rounding, 0.F, std::min(b.x - a.x, b.y - a.y) * .5F);
    float phase = preferences.animate ? float(std::fmod(seconds * 12., double(tile_width))) : 0;
    if (phase >= tile_width)
        phase = 0;
    float x = a.x;
    const ImU32 tint = IM_COL32(255, 255, 255, int(std::lround(opacity * 255)));
    while (x < b.x) {
        const float remaining = b.x - x;
        const float edge = std::min(b.x - a.x, rounding * 2.F + 1.F);
        float span = std::min(tile_width - phase, remaining);
        if (x == a.x && span < edge)
            span = std::min(remaining, span + tile_width);
        if (remaining > span && remaining - span < edge)
            span = remaining;
        const ImVec2 uv0{(column + phase / scale) / atlas->TexWidth, row / atlas->TexHeight};
        const ImVec2 uv1{(column + (phase + span) / scale) / atlas->TexWidth,
                         (row + ButtonTileHeight) / atlas->TexHeight};
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (x == a.x)
            corners = ImDrawFlags_RoundCornersLeft;
        if (x + span >= b.x)
            corners = x == a.x ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersRight;
        draw->AddImageRounded(atlas->TexID, {x, a.y}, {x + span, b.y}, uv0, uv1, tint, rounding, corners);
        x += span;
        phase = 0;
    }
}
} // namespace dkr::launcher_themes
