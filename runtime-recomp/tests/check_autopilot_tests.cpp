#include "netplay/check_autopilot.hpp"
#include "autopilot_symbols.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <vector>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;

namespace {
constexpr double TwoPi = 2 * std::numbers::pi;

// Kart heading h (steerVisualRotation) faces the direction whose yaw is
// h + 0x8000, measured from +z toward +x (arctan2_f(dx, dz) - 0x8000 == h).
std::int16_t heading_for(double dx, double dz) {
    return std::int16_t(int(std::lround(std::atan2(dx, dz) * 32768.0 / std::numbers::pi)) - 0x8000);
}
void forward(std::int16_t h, double& dx, double& dz) {
    const double phi = (int(std::uint16_t(h)) + 0x8000) * TwoPi / 65536.0;
    dx = std::sin(phi);
    dz = std::cos(phi);
}

AutopilotObservation straight_observation() {
    AutopilotObservation o;
    o.valid = true;
    o.nodes = {{{0, 0, -300}, {0, 0, 0}, {0, 0, 300}, {0, 0, 600}}};
    o.position = {0, 0, 150};
    o.checkpoint_distance = 0.5f;
    o.next_checkpoint = 3;
    o.heading = heading_for(0, 1);
    return o;
}
int stick_of(const AutopilotObservation& o) {
    AutopilotState state;
    const auto out = check_autopilot_steer(o, 0, state);
    assert(out.buttons == 0x8000 && out.stick_y == 0);
    return out.stick_x;
}

void steering_cases() {
    auto o = straight_observation();
    assert(stick_of(o) == 0);

    // Left of the heading is +x here (counterclockwise seen from above);
    // an N64 stick pushed left is negative.
    auto left = o;
    left.nodes = {{{0, 0, -300}, {0, 0, 0}, {60, 0, 300}, {400, 0, 500}}};
    auto right = left;
    for (auto& n : right.nodes)
        n.x = -n.x;
    const int l = stick_of(left), r = stick_of(right);
    assert(l < 0 && r > 0);
    assert(std::abs(l + r) <= 2); // mirror images, up to arithmetic-shift rounding

    // Pointing left of a straight path needs a right (positive) stick.
    auto off = o;
    off.heading = std::int16_t(o.heading + 0x1000);
    assert(stick_of(off) > 0);
    off.heading = std::int16_t(o.heading - 0x1000);
    assert(stick_of(off) < 0);
    // Pointing straight backwards: clamped like the game's CLAMP.
    off.heading = std::int16_t(o.heading + 0x4000);
    assert(stick_of(off) == AutopilotStickLimit);
    off.heading = std::int16_t(o.heading - 0x4000);
    assert(stick_of(off) == -AutopilotStickLimit);
}

void fallback_cases() {
    AutopilotObservation none;
    AutopilotState state;
    for (std::uint32_t f : {0u, 14u, 15u, 29u, 30u, 1000u}) {
        assert(check_autopilot_steer(none, f, state) == check_square_wave_input(f));
        AutopilotView view{};
        assert(check_autopilot_input(view, 0, f) == check_square_wave_input(f));
        assert(check_autopilot_input(view, 7, f) == check_square_wave_input(f));
    }
    assert(check_square_wave_input(0) == (PackedInput{0x8000, -30, 0}));
    assert(check_square_wave_input(15) == (PackedInput{0x8000, 30, 0}));
    // A usable observation is sampled once per frame.
    AutopilotView view{};
    view[1] = straight_observation();
    view[1].heading = std::int16_t(view[1].heading + 0x400);
    const auto first = check_autopilot_input(view, 1, 61);
    assert(first.stick_x > 0 && check_autopilot_input(view, 1, 61) == first);
}

// A point kart on a closed loop of checkpoints, fed by the autopilot alone.
// One stick unit turns the heading by 10 angle units per frame, to smaller
// steerVisualRotation for positive stick (the sign the CPU's own negative
// feedback loop implies); speed is 8 units per frame.
struct Lap {
    int laps;
    double worst;
};
Lap drive(bool clockwise, double lateral, int heading_error, unsigned frames) {
    constexpr int N = 20;
    constexpr double R = 1200, Speed = 8, Turn = 10;
    std::vector<CheckpointPoint> track(N);
    for (int i = 0; i < N; ++i) {
        const double a = (clockwise ? -1 : 1) * i * TwoPi / N;
        track[i] = {float(R * std::cos(a)), 0, float(R * std::sin(a))};
    }
    const auto at = [&](int i) { return track[((i % N) + N) % N]; };
    int next = 1, passed = 0;
    double x = at(0).x + lateral, z = at(0).z;
    auto h = std::int16_t(heading_for(at(1).x - at(0).x, at(1).z - at(0).z) + heading_error);
    AutopilotState state;
    Lap result{0, 0};
    for (unsigned f = 0; f < frames; ++f) {
        const auto prev = at(next - 1), cur = at(next);
        const double sx = cur.x - prev.x, sz = cur.z - prev.z;
        double progress = ((x - prev.x) * sx + (z - prev.z) * sz) / (sx * sx + sz * sz);
        if (progress >= 1.0) {
            ++next;
            if (next % N == 1)
                ++result.laps;
            continue;
        }
        AutopilotObservation o;
        o.valid = true;
        o.position = {float(x), 0, float(z)};
        o.heading = h;
        o.next_checkpoint = next % N;
        o.checkpoint_distance = float(1.0 - std::max(progress, 0.0));
        for (int i = 0; i < 4; ++i)
            o.nodes[i] = at(next - 2 + i);
        const auto in = check_autopilot_steer(o, f, state);
        assert(in.stick_x >= -AutopilotStickLimit && in.stick_x <= AutopilotStickLimit);
        h = std::int16_t(std::uint16_t(int(std::uint16_t(h)) - int(in.stick_x) * int(Turn)));
        double dx, dz;
        forward(h, dx, dz);
        x += dx * Speed;
        z += dz * Speed;
        if (f > 300)
            result.worst = std::max(result.worst, std::fabs(std::hypot(x, z) - R));
    }
    return result;
}

void closed_loop() {
    for (bool clockwise : {false, true}) {
        const auto clean = drive(clockwise, 0, 0, 3200);
        std::printf("loop cw=%d laps=%d worst=%.1f\n", int(clockwise), clean.laps, clean.worst);
        assert(clean.laps >= 3 && clean.worst < 120);
        const auto rough = drive(clockwise, 150, 0x1800, 3200);
        std::printf("loop cw=%d rough laps=%d worst=%.1f\n", int(clockwise), rough.laps, rough.worst);
        assert(rough.laps >= 3 && rough.worst < 250);
    }
}

// Guest RAM is stored byte-swapped (address ^ 3 per byte).
struct Ram {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x400000);
    void put(std::uint32_t addr, std::uint32_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            bytes[((addr & 0x1FFFFFFF) + i) ^ 3] = std::uint8_t(v >> (8 * (n - 1 - i)));
    }
    void real(std::uint32_t addr, float f) {
        std::uint32_t v;
        std::memcpy(&v, &f, 4);
        put(addr, v, 4);
    }
};

void capture_case() {
    using namespace check_autopilot_symbols;
    for (unsigned revision : {77u, 80u}) {
        const auto& s = revision == 80 ? symbols80 : symbols77;
        constexpr std::uint32_t Ports = 0x80330000, Object = 0x80300000, Data = 0x80310000, Nodes = 0x80320000;
        Ram ram;
        ram.put(s.checkpoint_count, 6, 4);
        ram.put(s.checkpoints, Nodes, 4);
        ram.put(s.racers_by_port, Ports, 4); // Object**: the variable holds the array's address
        ram.put(Ports + 4, Object, 4);
        ram.put(Object + 0x48, 1, 2);
        ram.put(Object + 0x64, Data, 4);
        ram.real(Object + 0x0C, 10);
        ram.real(Object + 0x10, 20);
        ram.real(Object + 0x14, 30);
        ram.put(Data, 1, 2);
        ram.put(Data + 0x192, 0, 1); // wraps: nodes 4, 5, 0, 1
        ram.put(Data + 0x1A0, 0x8123, 2);
        ram.put(Data + 0x1D6, 2, 1);
        ram.real(Data + 0xA8, 0.25f);
        for (unsigned i = 0; i < 6; ++i) {
            ram.real(Nodes + i * 0x3C + 0x10, float(i));
            ram.real(Nodes + i * 0x3C + 0x14, float(i) + 0.5f);
            ram.real(Nodes + i * 0x3C + 0x18, -float(i));
            ram.put(Nodes + i * 0x3C + 0x3A, 0xFF, 1);
        }
        const auto view = capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), revision);
        assert(!view[0].valid && !view[2].valid && !view[3].valid);
        const auto& o = view[1];
        assert(o.valid && o.vehicle == 2 && o.position.x == 10 && o.position.y == 20 && o.position.z == 30);
        assert(o.heading == std::int16_t(0x8123) && o.next_checkpoint == 0 && o.checkpoint_distance == 0.25f);
        const float want[4] = {4, 5, 0, 1};
        for (int i = 0; i < 4; ++i)
            assert(o.nodes[i].x == want[i] && o.nodes[i].z == -want[i]);
        // Unreadable: no checkpoints, or a missing image.
        ram.put(s.checkpoint_count, 0, 4);
        assert(!capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), revision)[1].valid);
        assert(!capture_autopilot_observations(nullptr, 0, revision)[1].valid);
        assert(!capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), 99)[1].valid);
    }
}
} // namespace

int main() {
    // The steering is what's under test: don't inherit the wave-only switch.
#if defined(_WIN32)
    _putenv_s("DKR_OWNED_CHECK_INPUT", "");
#else
    unsetenv("DKR_OWNED_CHECK_INPUT");
#endif
    steering_cases();
    fallback_cases();
    closed_loop();
    capture_case();
    std::puts("check autopilot ok");
    return 0;
}
