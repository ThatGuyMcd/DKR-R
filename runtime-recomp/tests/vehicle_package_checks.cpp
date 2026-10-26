#include "package_compatibility.hpp"
#include "driver_anchor_units.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dkr::mods;
using namespace dkr::mods::vehicles;
namespace ot = dkr::mods::vehicles::original_textures;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
Bytes read(const char* name) {
    std::ifstream file(std::filesystem::u8path(name), std::ios::binary);
    if (!file)
        throw std::runtime_error("Missing package fixture");
    Bytes data((std::istreambuf_iterator<char>(file)), {});
    if (data.size() > MaxPackageBytes)
        throw std::runtime_error("Fixture exceeds package limit");
    return data;
}
std::string fingerprint(const Package& p) {
    Bytes out;
    const auto number = [&](unsigned v) {
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(std::uint8_t(v >> (i * 8)));
    };
    const auto real = [&](float v) { number(std::bit_cast<unsigned>(v)); };
    const auto vec = [&](Vec3 v) {
        real(v.x);
        real(v.y);
        real(v.z);
    };
    const auto text = [&](const std::string& s) {
        number(unsigned(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    };
    text(p.id);
    text(p.name);
    text(p.digest);
    real(p.scale);
    real(p.yaw);
    real(p.driver_yaw);
    vec(p.seat);
    vec(p.driver_offset);
    number(unsigned(p.seating));
    number(p.authored_anchors);
    vec(p.origin);
    vec(p.kart_offset);
    for (auto v : p.driver_anchors)
        vec(v);
    for (auto v : p.driver_seats)
        vec(v);
    number(p.axle);
    for (const auto& w : p.wheels)
        text(w);
    for (auto v : p.pivots)
        vec(v);
    number(unsigned(p.textures.size()));
    for (const auto& t : p.textures) {
        number(t.image.width);
        number(t.image.height);
        number(t.repeat_s);
        number(t.repeat_t);
        out.insert(out.end(), t.image.rgba.begin(), t.image.rgba.end());
    }
    for (const auto& lod : p.lods) {
        number(lod.triangles);
        number(unsigned(lod.meshes.size()));
        for (const auto& m : lod.meshes) {
            text(m.id);
            text(m.name);
            number(unsigned(m.texture));
            number(m.double_sided);
            out.insert(out.end(), m.colour.begin(), m.colour.end());
            number(unsigned(m.vertices.size()));
            for (auto v : m.vertices)
                vec(v);
            for (auto uv : m.uv) {
                real(uv.x);
                real(uv.y);
            }
            for (auto f : m.faces)
                for (auto v : f)
                    number(v);
        }
    }
    number(p.portrait.width);
    number(p.portrait.height);
    out.insert(out.end(), p.portrait.rgba.begin(), p.portrait.rgba.end());
    return sha256(out);
}
Package square(bool repeat_s = true, bool repeat_t = true, unsigned width = 128, unsigned height = 64) {
    Package car;
    Texture t;
    t.repeat_s = repeat_s;
    t.repeat_t = repeat_t;
    t.image = {width, height, Bytes(width * height * 4)};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            auto* c = t.image.rgba.data() + (y * width + x) * 4;
            c[0] = std::uint8_t(x);
            c[1] = std::uint8_t(y);
            c[2] = std::uint8_t(x ^ y);
            c[3] = std::uint8_t(50 + (x + y) % 200);
        }
    car.textures.push_back(std::move(t));
    car.wheels[0] = "wheel";
    car.pivots[0] = {.5F, .5F, 0};
    Mesh m;
    m.id = "wheel";
    m.name = "wheel";
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    m.uv = {{-.25F, -.25F}, {1.75F, -.25F}, {1.75F, 1.75F}, {-.25F, 1.75F}};
    m.faces = {{0, 1, 2}, {0, 2, 3}};
    m.texture = 0;
    m.double_sided = true;
    for (auto& lod : car.lods) {
        lod.meshes.push_back(m);
        lod.triangles = 2;
    }
    return car;
}
std::array<std::uint8_t, 4> sample(const Texture& t, double u, double v) {
    const auto axis = [](double v, unsigned size, bool repeat) {
        int i = int(std::floor(v * size + 1e-8));
        if (!repeat)
            return unsigned(std::clamp(i, 0, int(size) - 1));
        i %= int(size);
        return unsigned(i < 0 ? i + int(size) : i);
    };
    const auto p = (axis(v, t.image.height, t.repeat_t) * t.image.width + axis(u, t.image.width, t.repeat_s)) * 4;
    return {t.image.rgba[p], t.image.rgba[p + 1], t.image.rgba[p + 2], t.image.rgba[p + 3]};
}
void pixels(const Package& car, const Texture& original) {
    double area = 0;
    for (const auto& m : car.lods[0].meshes)
        for (auto f : m.faces) {
            auto a = m.vertices[f[0]], b = m.vertices[f[1]], c = m.vertices[f[2]];
            area += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * .5;
        }
    check(std::abs(area - 1) < 1e-5, "Clipping changed the covered geometry area");
    for (unsigned y = 0; y < 33; ++y)
        for (unsigned x = 0; x < 33; ++x) {
            const double px = (x + .317) / 33, py = (y + .613) / 33, u = px * 2 - .25, v = py * 2 - .25;
            // Keep samples away from the renderer's 1/32-texel quantization boundary.
            if (std::abs(u * original.image.width - std::round(u * original.image.width)) < .05 ||
                std::abs(v * original.image.height - std::round(v * original.image.height)) < .05)
                continue;
            const auto wanted = sample(original, u, v);
            unsigned found = 0;
            for (const auto& m : car.lods[0].meshes)
                for (auto f : m.faces) {
                    const auto a = m.vertices[f[0]], b = m.vertices[f[1]], c = m.vertices[f[2]];
                    const double d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                    if (std::abs(d) < 1e-15)
                        continue;
                    const auto w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / d;
                    const auto w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / d, w2 = 1 - w0 - w1;
                    if (w0 < -1e-6 || w1 < -1e-6 || w2 < -1e-6)
                        continue;
                    const auto t0 = m.uv[f[0]], t1 = m.uv[f[1]], t2 = m.uv[f[2]];
                    check(sample(car.textures.at(unsigned(m.texture)), w0 * t0.x + w1 * t1.x + w2 * t2.x,
                                 w0 * t0.y + w1 * t1.y + w2 * t2.y) == wanted,
                          "Tiled artwork changed a sampled RGBA pixel");
                    ++found;
                }
            check(found > 0, "Clipping left a hole");
        }
}
void synthetic() {
    for (const auto repeat_s : {false, true})
        for (const auto repeat_t : {false, true}) {
            auto car = square(repeat_s, repeat_t);
            const auto original = car.textures[0];
            ot::prepare(car);
            check(car.textures.size() == 15, "Unexpected 128x64 tile count");
            for (const auto& t : car.textures) {
                const auto row = (t.image.width * 2 + 7) & ~7U;
                check(t.image.rgba.size() <= 4096 && row * t.image.height <= 2048,
                      "Material exceeds RGBA32 TMEM banks");
            }
            for (const auto& lod : car.lods) {
                check(lod.triangles <= 4096 && lod.meshes.size() <= 64, "Draw bounds exceeded");
                for (const auto& m : lod.meshes)
                    check(m.id == "wheel" && m.double_sided, "Wheel ownership or culling changed");
            }
            check(car.pivots[0].x == .5F && car.pivots[0].y == .5F, "Tiling recentered the authored wheel pivot");
            pixels(car, original);
        }
    auto small = square(true, true, 32, 16);
    auto expected = small;
    ot::prepare(small);
    check(fingerprint(small) == fingerprint(expected), "Already safe artwork changed");
    auto oversized = square(true, true, 256, 256);
    bool rejected = false;
    try {
        ot::prepare(oversized);
    } catch (const Error&) {
        rejected = true;
    }
    check(rejected && oversized.textures.size() == 1 && oversized.lods[0].triangles == 2,
          "Tile-budget failure published partial preparation");
    for (const float scale : {.21F, .25F, .28F}) {
        Package car;
        car.seating = SeatingMode::PerBaseDriverV1;
        car.driver_anchors[0] = {0, 32, 11};
        const auto legacy = dkr::runtime::vehicles::original_textures::model_anchor(car, 0, scale);
        check(std::abs(legacy.y - 8.F / scale) < 1e-5, "Old per-driver anchor units changed");
        car.authored_anchors = true;
        const auto raw = dkr::runtime::vehicles::original_textures::model_anchor(car, 0, scale);
        check(raw.y == 32 && raw.z == 11, "Explicit native anchor was scaled a second time");
        car.seating = SeatingMode::LegacyShared;
        check(std::abs(dkr::runtime::vehicles::original_textures::model_anchor(car, 0, scale).y - 8.F / scale) < 1e-5,
              "Legacy authored anchor selected the new contract");
    }
    // Degenerate UVs at a wrap boundary select one cell, not overlapping copies.
    auto fixed = square();
    for (auto& lod : fixed.lods)
        for (auto& mesh : lod.meshes)
            for (auto& uv : mesh.uv)
                uv = {1, 1};
    ot::prepare(fixed);
    check(fixed.lods[0].triangles == 2, "Constant boundary UVs duplicated triangles");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--fingerprint") {
            const auto data = read(argv[2]);
            const auto p = inspect(data, "fixture.dkrkart", Type::Car);
            std::cout << fingerprint(*p) << '\n';
            return 0;
        }
#if defined(DKR_VEHICLE_BEFORE)
        if (argc != 2)
            return 2;
        const auto data = read(argv[1]);
        (void)inspect(data, "fixture.dkrkart", Type::Car);
        std::cerr << "Before control unexpectedly accepted original profile\n";
        return 2;
#else
        synthetic();
        if (argc == 2) {
            const auto data = read(argv[1]);
            const auto p = inspect(data, "brute v2.dkrkart", Type::Car);
            check(p->source == data && p->digest == sha256(data), "Package source or identity was rewritten");
            check(p->per_driver() && p->authored_anchors, "Explicit seating contract not selected");
            check(p->textures.size() == 18, "Unexpected real-package tile count");
            for (const auto& t : p->textures)
                check(t.image.rgba.size() <= 4096, "Real package exceeds material limit");
            std::cout << "Real archive parsed as data: safe-tiles=" << p->textures.size() << " lod-triangles=";
            for (const auto& lod : p->lods)
                std::cout << lod.triangles << ',';
            std::cout << " source-sha256=" << p->digest << '\n';
        }
        std::cout << checks << " vehicle compatibility checks passed; no game, ROM, RSP or GPU executed\n";
        return 0;
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
#if defined(DKR_VEHICLE_BEFORE)
        return std::string_view(error.what()).find("unsupported car texture profile") != std::string_view::npos ? 1 : 2;
#else
        return 1;
#endif
    }
}
