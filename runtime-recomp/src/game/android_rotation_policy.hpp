#pragma once

#include <algorithm>
#include <cstdint>

namespace dkr::runtime::android_rotation {
// Vulkan surface-transform bit values, kept independent of Vulkan for tests.
inline constexpr std::uint32_t identity = 1, rotate90 = 2, rotate180 = 4, rotate270 = 8;
constexpr bool quarter_turn(std::uint32_t rotation) {
    return rotation == rotate90 || rotation == rotate270;
}
constexpr std::uint32_t select(std::uint32_t current, std::uint32_t supported) {
    if ((current == identity || current == rotate90 || current == rotate180 || current == rotate270) &&
        (supported & current))
        return current;
    // Mirror/inherit transforms need a different adapter. Preserve the existing
    // compositor path where identity is supported; never claim a mirrored image.
    return (supported & identity) ? identity : 0;
}
struct Extent {
    std::uint32_t width, height;
};
constexpr Extent rotated_extent(Extent size, std::uint32_t rotation) {
    return quarter_turn(rotation) ? Extent{size.height, size.width} : size;
}
constexpr bool fits(Extent size, Extent minimum, Extent maximum) {
    return size.width && size.height && size.width >= minimum.width && size.height >= minimum.height &&
           size.width <= maximum.width && size.height <= maximum.height;
}
constexpr std::uint32_t select_for_extent(std::uint32_t current, std::uint32_t supported, Extent logical,
                                          Extent minimum, Extent maximum) {
    const auto selected = select(current, supported);
    if (selected && fits(rotated_extent(logical, selected), minimum, maximum))
        return selected;
    // Preserve compositor presentation on asymmetric bounds rather than resizing
    // the game or creating an invalid rotated swapchain.
    return (supported & identity) && fits(logical, minimum, maximum) ? identity : 0;
}
struct Point {
    float x, y;
};
struct Rect {
    float left, top, right, bottom;
};
// preTransform declares rotation ALREADY applied to content relative to natural
// orientation, not a request for another rotation. Match Android's pre-rotation
// viewport convention: clockwise in pixel space. Input/layout remain logical.
constexpr Point to_image(Point p, float width, float height, std::uint32_t rotation) {
    switch (rotation) {
    case rotate90:
        return {height - p.y, p.x};
    case rotate180:
        return {width - p.x, height - p.y};
    case rotate270:
        return {p.y, width - p.x};
    default:
        return p;
    }
}
constexpr Point to_logical(Point p, float width, float height, std::uint32_t rotation) {
    switch (rotation) {
    case rotate90:
        return {p.y, height - p.x};
    case rotate180:
        return {width - p.x, height - p.y};
    case rotate270:
        return {width - p.y, p.x};
    default:
        return p;
    }
}
constexpr Rect to_image(Rect r, float width, float height, std::uint32_t rotation) {
    const auto a = to_image(Point{r.left, r.top}, width, height, rotation);
    const auto b = to_image(Point{r.right, r.bottom}, width, height, rotation);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
} // namespace dkr::runtime::android_rotation
