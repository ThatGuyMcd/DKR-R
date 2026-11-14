#include "android_rotation_policy.hpp"
#include "android_frame_budget.hpp"
#include "presentation_wait.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
int main() {
    using namespace dkr::runtime::android_rotation;
    CHECK(select(rotate90, 0x1ff) == rotate90);
    CHECK(select(rotate180, rotate180) == rotate180);
    CHECK(select(rotate270, rotate270) == rotate270);
    CHECK(select(16, 0x1ff) == identity); // mirrored fallback
    CHECK(select(16, 16) == 0);           // cannot silently render an unsupported transform
    CHECK(select(rotate90, identity) == identity);
    CHECK(select_for_extent(rotate90, 15, {1920, 1080}, {1, 1}, {4096, 4096}) == rotate90);
    CHECK(select_for_extent(rotate90, 15, {1920, 1080}, {1920, 1080}, {1920, 1080}) == identity);
    CHECK(select_for_extent(rotate90, rotate90, {1920, 1080}, {1920, 1080}, {1920, 1080}) == 0);
    CHECK(select_for_extent(identity, 15, {0, 1080}, {1, 1}, {4096, 4096}) == 0);
    for (const auto rotation : {identity, rotate90, rotate180, rotate270}) {
        const auto physical = rotated_extent({2352, 2172}, rotation);
        const auto logical = rotated_extent(physical, rotation);
        CHECK(logical.width == 2352 && logical.height == 2172);
        for (const auto point : {Point{0, 0}, Point{2352, 0}, Point{0, 2172}, Point{2352, 2172}, Point{211, 711}}) {
            const auto image = to_image(point, 2352, 2172, rotation);
            CHECK(image.x >= 0 && image.x <= physical.width && image.y >= 0 && image.y <= physical.height);
            const auto inverse = to_logical(image, 2352, 2172, rotation);
            CHECK(inverse.x == point.x && inverse.y == point.y);
            // Pixel-shader UV mapping must match the vertex/viewport inverse.
            const auto uv = to_logical({image.x / physical.width, image.y / physical.height}, 1, 1, rotation);
            CHECK(std::abs(uv.x - point.x / 2352) < 1e-6 && std::abs(uv.y - point.y / 2172) < 1e-6);
        }
        const auto whole = to_image(Rect{0, 0, 2352, 2172}, 2352, 2172, rotation);
        CHECK(whole.left == 0 && whole.top == 0 && whole.right == physical.width && whole.bottom == physical.height);
        const auto clip = to_image(Rect{-10, 7, 30, 67}, 2352, 2172, rotation);
        CHECK(clip.right - clip.left == (quarter_turn(rotation) ? 60 : 40));
        CHECK(clip.bottom - clip.top == (quarter_turn(rotation) ? 40 : 60));
    }
    const auto r90 = to_image(Rect{10, 20, 30, 50}, 100, 200, rotate90);
    CHECK(r90.left == 150 && r90.top == 10 && r90.right == 180 && r90.bottom == 30);
    const auto r270 = to_image(Rect{10, 20, 30, 50}, 100, 200, rotate270);
    CHECK(r270.left == 20 && r270.top == 70 && r270.right == 50 && r270.bottom == 90);
    CHECK(dkr::runtime::presentation_tail_guard(true) == std::chrono::microseconds(250));
    CHECK(dkr::runtime::presentation_tail_guard(false) == std::chrono::milliseconds(2));
    dkr::runtime::android_frame_budget::Samples bounded;
    for (unsigned i = 0; i < 10'000; ++i)
        bounded.record(double(i));
    CHECK(bounded.count == 10'000 && bounded.total == 49'995'000);
    CHECK(bounded.values[9999 % bounded.values.size()] == 9999);
    std::puts(
        "Android presentation policy: transforms, fallback, extents, crops, UV agreement, bounded diagnostics and wait guard passed.");
}
