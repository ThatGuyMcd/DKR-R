#include "vehicle_lighting/capture.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace light = dkr::runtime::vehicles::lighting;
using light::Vec;
namespace {
void near(float actual, float expected) {
    assert(std::abs(actual - expected) < 1e-5F);
}
void mesh_normals() {
    // UV-split vertices on one plane share smooth normals. A perpendicular
    // wall at the same points retains its hard edge. A collapsed face is safe.
    const std::vector<Vec> vertices{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}};
    const std::vector<std::array<unsigned, 3>> faces{{0, 1, 2}, {3, 4, 5}, {0, 6, 1}, {0, 0, 0}};
    const auto normals = light::normals(vertices, faces);
    for (unsigned f = 0; f < 2; ++f)
        for (auto n : normals[f]) {
            near(n.x, 0);
            near(n.y, 0);
            near(n.z, 1);
        }
    for (auto n : normals[2]) {
        near(n.x, 0);
        near(n.y, 1);
        near(n.z, 0);
    }
    for (auto n : normals[3]) {
        near(n.x, 0);
        near(n.y, 0);
        near(n.z, 0);
    }
    // A shallow join is smoothed, rather than giving every triangle flat light.
    const std::vector<Vec> curved{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 1, .25F}};
    const auto smooth = light::normals(curved, std::vector<std::array<unsigned, 3>>{{0, 1, 2}, {0, 1, 3}});
    assert(smooth[0][0].y < 0 && smooth[0][0].z > .9F);
    near(smooth[0][0].y, smooth[1][0].y);
}
void transforms_and_shading() {
    constexpr float pi = 3.14159265358979323846F;
    const light::Matrix turn{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};
    const auto n = light::transform({0, 0, 1}, light::normal_matrix(turn));
    near(n.x, 1);
    near(n.z, 0);
    const auto yaw = light::yaw({0, 0, 1}, pi * .5F);
    near(yaw.x, 1);
    near(yaw.z, 0);
    const auto scaled = light::unit(
        light::transform(light::unit({1, 1, 0}), light::normal_matrix({{{2, 0, 0}, {0, 1, 0}, {0, 0, 1}}})));
    near(scaled.y / scaled.x, 2);
    bool rejected = false;
    try {
        (void)light::normal_matrix({});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
    light::Sample sample;
    sample.flags = 1;
    sample.ambient = .25F;
    sample.diffuse = .25F;
    sample.direction = {0, 0, 1};
    near(light::brightness(sample, {0, 0, -1}), .25F);
    near(light::brightness(sample, {0, 0, 1}), .75F);
    sample.exposure = .2F;
    near(light::brightness(sample, {0, 0, 1}), .15F);
    const auto rgb = light::shade(sample, {0, 0, 1}, {200, 100, 0, 37});
    assert(rgb[0] == 30 && rgb[1] == 15 && rgb[2] == 0);
    sample.exposure = 1;
    sample.flags = 3;
    near(light::brightness(sample, {0, 0, -1}), 160.F / 255.F);
    near(light::brightness(sample, {0, 0, 1}), 1);
    sample.flags = 0;
    near(light::brightness(sample, {}), 1);
    // Steering/spin change the normal in the same coordinate basis as geometry.
    sample.flags = 1;
    sample.exposure = 1;
    sample.direction = {1, 0, 0};
    assert(light::brightness(sample, n) > light::brightness(sample, {0, 0, 1}));
}
void draw_snapshot() {
    std::map<unsigned, unsigned> memory;
    const auto set_float = [&](unsigned at, float value) { memory[at] = std::bit_cast<unsigned>(value); };
    memory[0x1054] = 0x2000;
    set_float(0x2000, .8F);
    set_float(0x2028, .3F);
    set_float(0x202c, .4F);
    memory[0x201c] = 0;
    memory[0x201e] = 0;
    memory[0x2020] = 16384;
    memory[0x2004] = 0xff800020;
    set_float(0x3000, .5F);
    const light::Matrix view{{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            set_float(0x4000 + (r * 4 + c) * 4, view[r][c]);
    const auto read = [&](unsigned at, unsigned) { return memory.at(at); };
    const auto captured = light::capture(read, 0x1000, 0x3000, 0x4000, false);
    near(captured.exposure, .4F);
    near(captured.direction.x, 1);
    near(captured.direction.z, 0);
    light::Stream stream;
    const auto words = light::words(captured);
    for (unsigned i = 0; i < 8; ++i) {
        assert(!stream.ready());
        assert(stream.put(i, words[i]));
    }
    assert(stream.ready() && !stream.put(0, words[0]));
    set_float(0x3000, 1);
    memory[0x2004] = 0;
    near(stream.sample.exposure, .4F);
    assert(stream.sample.environment == 0xff800020);
    const auto fast = light::capture(read, 0x1000, 0x3000, 0x4000, true);
    near(fast.direction.z, 1);
    assert(fast.fast());
    memory[0x1054] = 0;
    assert(!light::capture(read, 0x1000, 0x3000, 0x4000, false).enabled());
    light::Stream invalid;
    assert(!invalid.put(0, std::bit_cast<unsigned>(std::numeric_limits<float>::quiet_NaN())));
    assert(!invalid.put(0, std::bit_cast<unsigned>(-1.F)));
    assert(!invalid.put(7, 4));
    assert(!invalid.put(8, 0));
    assert(!invalid.ready());
}
void opacity_and_colour_blend() {
    // Decode the packed mux as RT64 does. The second cycle must blend colour
    // with ENV_ALPHA and preserve COMBINED alpha, never use shade/fog alpha.
    for (bool textured : {false, true}) {
        const auto cc = light::combine(textured);
        const auto low = std::uint32_t(cc), high = std::uint32_t(cc >> 32);
        assert(((low >> 5) & 15) == 5 && ((high >> 24) & 15) == 0 && (low & 31) == 12 && ((high >> 6) & 7) == 0);
        assert(((high >> 21) & 7) == 7 && ((high >> 18) & 7) == 7 && ((high >> 3) & 7) == 7 && (high & 7) == 0);
        if (textured)
            assert(((low >> 12) & 7) == 1 && ((low >> 9) & 7) == 3);
        else
            assert(((high >> 9) & 7) == 3);
    }
}
} // namespace
int main() {
    mesh_normals();
    transforms_and_shading();
    draw_snapshot();
    opacity_and_colour_blend();
    std::cout
        << "Vehicle lighting: normals, creases, transforms, native shade response, draw snapshots and independent opacity passed\n";
}
