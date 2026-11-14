#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RT64::water_work {
struct Region {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};
// Hi-Z has six levels. Capture whole 32-pixel cells so every coarse-depth
// sample at a viewport boundary has all of its original source pixels.
inline Region captureRegion(std::uint32_t width, std::uint32_t height, float left, float top, float right,
                            float bottom) {
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom))
        return {0, 0, width, height};
    const auto low = [](float value, std::uint32_t limit) {
        return std::uint32_t(std::clamp(std::floor(double(value) / 32) * 32, 0.0, double(limit)));
    };
    const auto high = [](float value, std::uint32_t limit) {
        return std::uint32_t(std::clamp(std::ceil(double(value) / 32) * 32, 0.0, double(limit)));
    };
    const auto x = low(left, width), y = low(top, height);
    const auto endX = high(right, width), endY = high(bottom, height);
    return {x, y, endX > x ? endX - x : 0, endY > y ? endY - y : 0};
}
inline Region reduceRegion(Region source, std::uint32_t width, std::uint32_t height) {
    const auto x = std::min(source.x / 2, width), y = std::min(source.y / 2, height);
    const auto endX = std::min((source.x + source.width + 1) / 2, width);
    const auto endY = std::min((source.y + source.height + 1) / 2, height);
    return {x, y, endX > x ? endX - x : 0, endY > y ? endY - y : 0};
}
inline std::uint32_t fieldResolution(bool active, bool allocated, std::uint32_t retained, std::uint32_t quality) {
    return active ? (quality >= 3 ? 512U : 256U) : allocated ? retained : 1U;
}
} // namespace RT64::water_work
