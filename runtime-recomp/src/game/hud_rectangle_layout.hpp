#pragma once
#include "hud_group_layout.hpp"
#include <cstdint>

namespace dkr::runtime::hud::groups {
struct TextureRectangle {
    int ulx, uly, lrx, lry;
    std::int16_t dsdx, dtdy;
};
inline TextureRectangle transform_rectangle(const Transform& t, int ulx, int uly, int lrx, int lry, std::int16_t dsdx,
                                            std::int16_t dtdy, unsigned bias_axes) {
    const auto x = [&](int v) { return int(std::lround((v - ((bias_axes & 1) ? 1024 : 0)) * t.scale + t.x * 4)); };
    const auto y = [&](int v) { return int(std::lround((v - ((bias_axes & 2) ? 512 : 0)) * t.y_scale() + t.y * 4)); };
    const auto derivative = [&](std::int16_t value, float scale) {
        return static_cast<std::int16_t>(std::clamp(std::lround(value / scale), -32768L, 32767L));
    };
    return {x(ulx), y(uly), x(lrx), y(lry), derivative(dsdx, t.scale), derivative(dtdy, t.y_scale())};
}
} // namespace dkr::runtime::hud::groups
