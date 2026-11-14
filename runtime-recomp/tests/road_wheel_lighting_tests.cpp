#include "vehicle_lighting/road_wheels.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace w = dkr::runtime::vehicles::lighting::road_wheels;
namespace l = dkr::runtime::vehicles::lighting;
namespace {
constexpr unsigned P = 0x80001000, H = 0x80002000, R = 0x80003000, A = 0x80004000, Slots = 0x80005000,
                   Instance = 0x80006000, Model = 0x80007000, Anchors = 0x80008000, Vertices = 0x80009000,
                   Shade = 0x8000a000, WheelHeader = 0x8000b000, Sprite = 0x8000c000, Holder = 0x8000d000,
                   Commands = 0x80010000, I = 0x8000e000, V = 0x8000e100, Layout = 0x8000e200, Indices = 0x8000f000;
struct Fixture {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x20000);
    w::Memory g{ram};
    static unsigned wheel(unsigned i) {
        return 0x80011000 + i * 0x100;
    }
    void put(unsigned at, unsigned value, unsigned size = 4) {
        for (unsigned i = 0; i < size; ++i)
            ram[((at - 0x80000000) + i) ^ 3U] = std::uint8_t(value >> ((size - 1 - i) * 8));
    }
    void real(unsigned at, float value) {
        put(at, std::bit_cast<unsigned>(value));
    }
    Fixture() {
        put(P + 0x40, H);
        put(P + 0x64, R);
        put(P + 0x60, A);
        put(P + 0x48, 1, 2);
        put(H + 0x55, 6, 1);
        put(A, 4);
        put(A + 0x2c, Indices);
        put(P + 0x68, Slots);
        put(Slots, Instance);
        put(Instance, Model);
        put(Model + 0x14, Anchors);
        put(Model + 0x18, 4, 2);
        put(Model + 0x24, 4, 2);
        put(P + 0x44, Vertices);
        put(WheelHeader + 0x53, 2, 1);
        put(WheelHeader + 0x55, 4, 1);
        put(Sprite, 16, 2);
        put(Sprite + 6, 16, 2);
        for (unsigned i = 0; i < 4; ++i) {
            put(A + 4 + i * 4, wheel(i));
            put(wheel(i) + 0x40, WheelHeader);
            put(Indices + i, i, 1);
            put(Anchors + i * 2, i, 2);
            put(Vertices + i * 10, (i == 0 || i == 3) ? 39 : unsigned(-39), 2);
        }
        put(P + 0x54, Shade);
        real(Shade, .5F);
        real(Shade + 0x28, .328F);
        real(Shade + 0x2c, .903F);
        put(Shade + 0x1c, 16384, 2);
        real(I, 1);
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                real(V + (r * 4 + c) * 4, r == c ? 1.F : 0.F);
        put(Holder, Commands);
    }
    void begin(w::Scope& scope, bool custom = true, bool car = false) {
        scope.begin(ram, P, custom, car, {I, V, Layout});
    }
    auto draw(w::Scope& scope, unsigned i = 1) {
        return scope.wheel_begin(ram, wheel(i), Holder, Sprite, 0x10b);
    }
};
void near(float a, float b) {
    assert(std::abs(a - b) < 1e-5F);
}
void physical_light() {
    l::Sample sample;
    sample.flags = 1;
    sample.ambient = .328F;
    sample.diffuse = .903F;
    sample.direction = {1, 0, 0};
    near(w::multiplier(sample, w::normal(-1, 0)), .328F);
    near(w::multiplier(sample, w::normal(1, 0)), 1);
    sample.exposure = .2F;
    near(w::multiplier(sample, w::normal(-1, 0)), .328F);
    sample.exposure = 0;
    near(w::multiplier(sample, w::normal(-1, 0)), 1);
    sample.exposure = .8F;
    sample.flags = 3;
    near(w::multiplier(sample, w::normal(-1, 0)), 160.F / 255.F);
    sample.flags = 1;
    sample.exposure = 1;
    sample.direction = {0, 0, 1};
    assert(w::multiplier(sample, w::normal(-1, -16384)) > w::multiplier(sample, w::normal(-1, 16384)));
    const auto n = w::rotate({1, 0, 0}, 16384, 0, 0);
    near(n.x, 0);
    near(n.y, 0);
    near(n.z, -1);
    const auto pitch = w::rotate({0, 1, 0}, 0, 16384, 0);
    near(pitch.x, 0);
    near(pitch.y, 0);
    near(pitch.z, 1);
    const auto roll = w::rotate({1, 0, 0}, 0, 0, 16384);
    near(roll.x, 0);
    near(roll.y, 1);
    near(roll.z, 0);
}
void scoped_commands() {
    Fixture f;
    w::Scope scope;
    const auto before = f.ram;
    f.begin(scope);
    for (unsigned i = 0; i < 4; ++i) {
        const auto start = f.g.get(Holder);
        const auto result = f.draw(scope, i);
        assert(result && result->ordinal == i);
        const auto prefix = i == 0 ? 80U : 0U;
        if (!i) {
            assert(f.g.get(start) == 0xb0ca001c && f.g.get(start + 4) == std::bit_cast<unsigned>(.5F));
            assert(f.g.get(start + 64) == 0xb0ca0024 && f.g.get(start + 72) == 0xb0ca0025);
        }
        assert(f.g.get(start + prefix) == 0xb0ca001a &&
               f.g.get(start + prefix + 4) == ((i == 1 || i == 2) ? 0x10000U : 0));
        near(w::multiplier(result->sample, w::normal((i == 0 || i == 3) ? 1.F : -1.F, 0)),
             (i == 0 || i == 3) ? 1.F : .328F);
        // A non-owned effect cannot close the tyre scope or inherit its owner.
        scope.wheel_end(f.ram, 0x8001f000);
        assert(f.g.get(Holder) == start + prefix + 8);
        scope.wheel_end(f.ram, Fixture::wheel(i));
        assert(f.g.get(start + prefix + 8) == 0xb0ca001b && !f.g.get(start + prefix + 12));
    }
    scope.end(P);
    assert(!f.draw(scope));
    for (unsigned i = 0; i < f.ram.size(); ++i) {
        const unsigned at = 0x80000000 + (i ^ 3U);
        if ((at >= Commands && at < Commands + 144) || (at >= Holder && at < Holder + 4))
            continue;
        assert(f.ram[i] == before[i]); // No actor, model, sprite, texture or light write.
    }
    assert(f.g.get(Holder) == Commands + 144);
}
void exclusions_and_worlds() {
    const auto excluded = [](auto change, bool custom = true, bool car = false) {
        Fixture f;
        w::Scope scope;
        change(f);
        const auto before = f.ram;
        f.begin(scope, custom, car);
        assert(!f.draw(scope));
        assert(f.ram == before);
    };
    excluded([](Fixture&) {}, false);
    excluded([](Fixture&) {}, true, true);
    excluded([](Fixture& f) { f.put(R + 0x1d6, 1, 1); }); // Hovercraft.
    excluded([](Fixture& f) { f.put(R + 0x1d6, 2, 1); }); // Plane.
    excluded([](Fixture& f) { f.put(P + 0x48, 56, 2); }); // Title prop.
    excluded([](Fixture& f) { f.put(H + 0x71, 1, 1); });
    excluded([](Fixture& f) { f.put(A, 3); }); // Cargo.
    excluded([](Fixture& f) { f.put(Sprite, 1, 2); });
    excluded([](Fixture& f) { f.put(Sprite + 6, 0, 2); });
    excluded([](Fixture& f) { f.put(WheelHeader + 0x55, 2, 1); });
    excluded([](Fixture& f) { f.put(Indices + 1, 255, 1); });
    excluded([](Fixture& f) { f.put(A + 0x2c, 0); });
    excluded([](Fixture& f) { f.put(Model + 0x18, 0, 2); }); // Distant baked-car proxy.
    excluded([](Fixture& f) { f.real(I, 100); });
    excluded([](Fixture& f) { f.real(Shade + 0x28, -.1F); });
    Fixture first, second;
    w::Scope scope;
    first.begin(scope);
    assert(!second.draw(scope)); // Snapshot identity is part of the scope.
    assert(!scope.wheel_begin(first.ram, 0x8001f000, Holder, Sprite, 0x10b));
    assert(!scope.wheel_begin(first.ram, Fixture::wheel(1), Holder, Sprite, 0x0b));
    first.put(R, 0xffff, 2);
    const auto fast = first.draw(scope);
    assert(fast && fast->sample.fast());
    near(w::multiplier(fast->sample, w::normal(-1, 0)), 160.F / 255.F);
    scope.wheel_end(first.ram, Fixture::wheel(1));
    second.begin(scope, false);
    assert(!first.draw(scope));
    first.begin(scope);
    first.real(I, std::numeric_limits<float>::quiet_NaN());
    const auto unchanged = first.ram;
    bool rejected = false;
    try {
        (void)first.draw(scope);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected && first.ram == unchanged);
    Fixture bad;
    w::Scope invalid;
    bad.begin(invalid);
    bad.put(Holder, 0x8001fffc);
    const auto original = bad.ram;
    rejected = false;
    try {
        (void)bad.draw(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected && bad.ram == original);
    Fixture short_packet;
    w::Scope short_scope;
    short_packet.begin(short_scope);
    short_packet.put(Holder, 0x8001ffc0);
    const auto short_before = short_packet.ram;
    rejected = false;
    try {
        (void)short_packet.draw(short_scope);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected && short_packet.ram == short_before);
    Fixture unaligned;
    w::Scope unaligned_scope;
    unaligned.begin(unaligned_scope);
    const auto untouched = unaligned.ram;
    rejected = false;
    try {
        (void)unaligned_scope.wheel_begin(unaligned.ram, Fixture::wheel(1), Holder + 1, Sprite, 0x10b);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected && unaligned.ram == untouched);
}
void character_only_catalogue() {
    w::publish_headers({304, 401, 304});
    assert(w::custom_header(304) && w::custom_header(401));
    assert(!w::custom_header(0) && !w::custom_header(303));
    const auto pinned = w::driver_headers.load();
    w::publish_headers({});
    assert(!w::custom_header(304));
    assert(pinned->size() == 2 && pinned->front() == 304); // Old readers keep immutable storage.
}
void raw_orientation_packet() {
    Fixture f;
    w::Scope scope;
    f.put(P, 16384, 2);
    f.put(P + 2, unsigned(-8192), 2);
    f.put(P + 4, 4096, 2);
    f.put(Fixture::wheel(1), unsigned(-4567), 2);
    f.begin(scope);
    assert(f.draw(scope));
    assert(f.g.get(Commands + 68) == ((16384U << 16) | std::uint16_t(-8192)));
    assert(f.g.get(Commands + 76) == 4096);
    assert(f.g.get(Commands + 84) == (0x10000U | std::uint16_t(-4567)));
    // Canonical draw bytes carry integer angles, never a host-libm result.
    scope.wheel_end(f.ram, Fixture::wheel(1));
    const auto next = f.g.get(Holder);
    f.real(I, .25F);
    const auto second = f.draw(scope, 0);
    assert(second);
    near(second->sample.exposure, .5F);
    assert(f.g.get(next) == 0xb0ca001a); // The parent sample is sent only once.
    scope.wheel_end(f.ram, Fixture::wheel(0));
}
} // namespace
int main() {
    physical_light();
    scoped_commands();
    exclusions_and_worlds();
    character_only_catalogue();
    raw_orientation_packet();
    std::cout
        << "Native road-wheel lighting: physical orientation, raw angle packets, exposure, all four attachments, draw-only writes, exclusions, character-only catalogue and snapshot isolation passed\n";
}
