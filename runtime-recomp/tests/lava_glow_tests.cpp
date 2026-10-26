#include "lava_glow_policy.hpp"
#include "enhanced_water_settings.hpp"
#include "lobby_privacy_policy.hpp"
#include <iostream>
#include <stdexcept>

using namespace dkr::runtime::water::enhanced;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
GlowTriangle triangle(Vec3 a, Vec3 b, Vec3 c, std::uint32_t address, bool source = false) {
    return {{a, b, c}, {address, address + 10, address + 20}, source};
}
void finish(LavaGlowCache& cache) {
    for (unsigned i = 0; i < 4096 && !cache.ready(); ++i)
        cache.step(1, std::chrono::microseconds(650));
    check(cache.ready(), "bounded cache completes");
}
int main() {
    try {
        const auto start = std::chrono::steady_clock::now();
        auto scene = std::make_shared<GlowGeometry>();
        scene->triangles.push_back(triangle({-100, 0, -100}, {100, 0, -100}, {0, 0, 100}, 100, true));
        scene->triangles.push_back(triangle({-30, 40, -30}, {30, 40, -30}, {0, 40, 30}, 1000));
        LavaGlowCache cache;
        cache.prepare(scene, 100);
        finish(cache);
        const float bright = cache.energy(1000, {-30, 40, -30}, 2);
        check(bright > 0.005F && bright <= 0.14F, "subtle local warm illumination");
        check(cache.energy(1000, {-30, 40, -30}, 1) == 0, "cache fades in instead of popping");
        check(cache.energy(1000, {-30, 40, -30}, 1.08F) > 0, "continuous fade");
        check(cache.energy(1000, {900, 40, -30}, 2) == 0, "recycled address requires exact position");
        check(cache.energy(999, {-30, 40, -30}, 2) == 0, "unqualified vertices stay unchanged");
        const auto rays = cache.ray_tests();
        for (unsigned i = 0; i < 10000; ++i) {
            cache.step(2);
            (void)cache.energy(1000, {-30, 40, -30}, 2);
        }
        check(cache.ray_tests() == rays, "steady frames never retrace static illumination");
        cache.prepare(scene, 25);
        check(cache.ready() && cache.energy(1000, {-30, 40, -30}, 2) == bright,
              "range edit keeps previous complete result");
        cache.step(3, std::chrono::milliseconds(10));
        check(cache.energy(1000, {-30, 40, -30}, 3) == bright, "range rebuild crossfades atomically");
        check(cache.energy(1000, {-30, 40, -30}, 4) < bright, "range limits glow");
        auto blocked = std::make_shared<GlowGeometry>(*scene);
        blocked->triangles.push_back(triangle({-500, 20, -500}, {500, 20, -500}, {0, 20, 500}, 2000));
        cache.prepare(blocked, 100);
        finish(cache);
        check(cache.energy(1000, {-30, 40, -30}, 2) == 0, "opaque floor blocks light leaking through it");
        cache.prepare(nullptr, 100);
        check(!cache.ready(), "scene retirement clears illumination");
        auto invalid = std::make_shared<GlowGeometry>(*scene);
        invalid->complete = false;
        cache.prepare(invalid, 100);
        check(!cache.ready(), "truncated blockers fail closed");
        auto long_river = std::make_shared<GlowGeometry>();
        for (unsigned i = 0; i < 40; ++i) {
            const float x = float(i) * 500;
            long_river->triangles.push_back(
                triangle({x - 80, 0, -80}, {x + 80, 0, -80}, {x, 0, 80}, 100 + i * 100, true));
            long_river->triangles.push_back(
                triangle({x - 20, 40, -20}, {x + 20, 40, -20}, {x, 40, 20}, 10000 + i * 100));
        }
        cache.prepare(long_river, 100);
        finish(cache);
        check(cache.energy(10000, {-20, 40, -20}, 2) > 0.005F, "first river region lit");
        check(cache.energy(13900, {19480, 40, -20}, 2) > 0.005F, "last region lit beyond sixteen-emitter limit");
        lava_lighting_mode = 0;
        lava_lighting_enabled = 0;
        check(load_tuning("rtx_lava_lighting_intensity", 125) && lava_lighting == 125,
              "old intensity preference loads");
        check(lava_lighting_enabled == 0 && lava_lighting_mode == 0,
              "old off preference stays off, new mode defaults lightweight");
        check(load_tuning("lava_lighting_mode", 1) && lava_lighting_mode == 0,
              "old RTX selection migrates to cached glow");
        check(load_tuning("lava_lighting_mode", 99) && lava_lighting_mode == 0,
              "unsupported mode cannot restore per-frame lighting rays");
        reset_appearance();
        check(lava_lighting_mode == 0, "reset uses lightweight");
        check(lava_pulse(0, 0, 1) == 1, "pulse-off gives steady light");
        for (float strength : {0.0F, 0.35F, 1.0F})
            for (float speed : {0.25F, 1.0F, 2.0F})
                for (unsigned i = 0; i < 10000; ++i) {
                    const float seconds = float(i) / 120;
                    const auto cooling = lava_cooling(seconds, strength, speed),
                               pulse = lava_pulse(seconds, strength, speed);
                    check(cooling >= 0 && cooling <= strength, "palette stays between hot and cool endpoints");
                    check(pulse >= 0.8F && pulse <= 1, "nearby lighting never pulses to black at any strength/speed");
                }
        const std::string code = "C9BFQ", text = "Code C9BFQ copied. Waiting for ABCDE.";
        using dkr::runtime::ui::privacy::redact;
        check(redact(text, true, {code, "ABCDE"}) == "Code ***** copied. Waiting for *****.",
              "both active and joining codes masked");
        check(redact(text, false, {code, "ABCDE"}) == text, "show leaves displayed codes unchanged");
        check(code == "C9BFQ", "mask never changes stored/copyable invite");
        check(redact("C9BFQ C9BFQ", true, {code, {}}) == "***** *****", "all repeated codes masked");
        check(redact("C9BFQ", true, {"C9", {}}) == "C9BFQ", "partial input is not a token");
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout
            << "PASS: cached occlusion, full river coverage, bounded rebuild/fade, no steady-frame rays, settings migration and streamer privacy ("
            << ms << " ms).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
