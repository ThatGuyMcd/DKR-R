#include "launcher_render_policy.hpp"
#include "launcher_frame_policy.hpp"
#include "launcher_solid_geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
bool near(float a, float b) {
    return std::abs(a - b) < 0.002F;
}
} // namespace

int main() {
    struct Point {
        float x, y;
    };
    struct Vertex {
        Point uv;
    };
    Vertex vertices[] = {{{.25F, .25F}}, {{.25F, .25F}}, {{.25F, .25F}}, {{.25F, .26F}}};
    unsigned short indices[] = {0, 1, 2};
    const Point white{.25F, .25F};
    using dkr::runtime::launcher::is_solid_triangle;
    check(is_solid_triangle(vertices, 4, indices, 0, white), "white triangle not recognized");
    check(!is_solid_triangle(vertices, 4, indices, 1, white), "glyph mistaken for solid colour");
    check(!is_solid_triangle(vertices, 4, indices, 4, white), "invalid vertex offset accepted");
    indices[2] = 65535;
    check(!is_solid_triangle(vertices, 4, indices, 0, white), "invalid index accepted");
    using dkr::runtime::launcher::background_tiles;
    for (float width : {800.F, 1280.F, 1440.F, 1920.F, 3840.F, 7680.F}) {
        for (float height : {600.F, 800.F, 900.F, 2160.F}) {
            const float tile_height = height * 1.03F;
            const float tile_width = tile_height * 1672.F / 941.F;
            for (double phase : {0.0, 0.001, 12.5, double(tile_width), double(tile_width) * 1.99999, 1.0e9}) {
                const auto tiles = background_tiles(width, height, tile_width, tile_height, phase);
                check(!tiles.empty(), "visible background missing");
                check(tiles.size() <= static_cast<std::size_t>(std::ceil(width / tile_width)) + 1,
                      "offscreen tile submitted");
                float right = 0;
                for (const auto& tile : tiles) {
                    check(near(tile.left, right), "gap or overlap between background tiles");
                    check(tile.right > tile.left && tile.right <= width, "unclipped horizontal extent");
                    check(tile.top == 0 && tile.bottom == height, "unclipped vertical extent");
                    check(tile.u0 >= 0 && tile.u1 <= 1 && tile.u0 < tile.u1, "bad horizontal UV");
                    check(tile.v0 >= 0 && tile.v1 <= 1 && tile.v0 < tile.v1, "bad vertical UV");
                    check(near(tile.right - tile.left, (tile.u1 - tile.u0) * tile_width),
                          "clipping stretched background");
                    right = tile.right;
                }
                check(near(right, width), "right edge uncovered");
                const auto wrapped =
                    background_tiles(width, height, tile_width, tile_height, phase + double(tile_width) * 2);
                check(wrapped.size() == tiles.size(), "mirror period changed tile count");
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                    check(wrapped[i].mirrored == tiles[i].mirrored, "mirror parity jumps at wrap");
                    check(near(wrapped[i].left, tiles[i].left), "scroll discontinuity at wrap");
                }
            }
        }
    }
    check(background_tiles(0, 800, 1000, 900, 0).empty(), "zero viewport accepted");
    check(background_tiles(1280, 800, 0, 900, 0).empty(), "zero tile accepted");
    using namespace dkr::runtime::launcher;
    using namespace std::chrono_literals;
    check(refresh_target(true, true, true, false, 90) == 60, "interactive ceiling");
    check(refresh_target(true, true, false, false, 60) == 60, "idle active launcher fell below 60 Hz");
    check(refresh_target(true, false, true, false, 60) == 10, "background ceiling");
    check(refresh_target(true, false, true, true, 60) == 60, "controller without keyboard focus");
    check(refresh_target(false, false, true, true, 60) == 0, "hidden controller window rendered");
    FrameSchedule schedule;
    const FrameSchedule::Time start{1s};
    check(schedule.wait_ms(start, 60) == 0, "first frame delayed");
    schedule.started(start, 60);
    check(schedule.wait_ms(start + 5ms, 60) == 12, "interactive deadline");
    check(schedule.wait_ms(start + 5ms, 10) == 50, "background service cadence");
    check(schedule.wait_ms(start + 99ms, 10) == 1, "event burst bypassed background cap");
    check(schedule.wait_ms(start + 100ms, 10) == 0, "background frame not released");
    check(schedule.wait_ms(start + 88ms, 60) == 0, "slow draw added another sleep");
    check(schedule.wait_ms(start + 20ms, 30) == 14 && schedule.wait_ms(start + 20ms, 60) == 0,
          "new interaction did not shorten idle deadline");
    check(schedule.wait_ms(start + 1s, 0) == 50, "hidden launcher busy loop");
    schedule.started(start + 17ms, 60);
    check(schedule.wait_ms(start + 33ms, 60) == 1, "millisecond rounding lost the 60 Hz timebase");
    schedule.started(start + 34ms, 60);
    check(schedule.wait_ms(start + 50ms, 60) == 0, "millisecond rounding accumulated drift");
    schedule.started(start + 200ms, 60);
    check(schedule.wait_ms(start + 200ms, 60) == 17, "slow frame created catch-up burst");
    std::cout << "Launcher background clipping and mirror continuity passed\n";
}
