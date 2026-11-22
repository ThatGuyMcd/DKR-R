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
    unsigned third_lap = 0; // frame the third lap completed, 0 if it did not
};
// skill: -1 steer-only, 0 cpu, 1 expert. Speed drops with the stick angle
// (scrubbing), so a smoother line is a faster one.
Lap drive(bool clockwise, double lateral, int heading_error, unsigned frames, int skill = -1) {
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
            if (next % N == 1 && ++result.laps == 3)
                result.third_lap = f;
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
        const auto in = skill < 0
                            ? check_autopilot_steer(o, f, state)
                            : check_autopilot_drive(o, f, state, skill ? AutopilotSkill::Expert : AutopilotSkill::Cpu);
        assert(in.stick_x >= -AutopilotStickLimit && in.stick_x <= AutopilotStickLimit);
        h = std::int16_t(std::uint16_t(int(std::uint16_t(h)) - int(in.stick_x) * int(Turn)));
        double dx, dz;
        forward(h, dx, dz);
        const double speed = Speed * (skill < 0 ? 1.0 : 1.0 - 0.6 * std::abs(in.stick_x) / double(AutopilotStickLimit));
        x += dx * speed;
        z += dz * speed;
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

// ---- racing behaviour (check_autopilot_drive) ----

constexpr std::uint16_t A = 0x8000, B = 0x4000, Z = 0x2000, R = 0x0010;
constexpr auto Cpu = AutopilotSkill::Cpu;
constexpr auto Expert = AutopilotSkill::Expert;

// Straight road, +z heading, racer 50 units past the middle of a segment.
AutopilotObservation racing() {
    auto o = straight_observation();
    o.velocity = -10;
    return o;
}
PackedInput drive_once(const AutopilotObservation& o, AutopilotSkill skill) {
    AutopilotState state;
    return check_autopilot_drive(o, 100, state, skill);
}
// Where a point `ahead` units in front of the racer along its heading and `side`
// to the right (the stick-positive side) lies.
CheckpointPoint relative(const AutopilotObservation& o, double ahead, double side) {
    double fx, fz;
    forward(o.heading, fx, fz);
    return {float(o.position.x + fx * ahead + fz * side), 0, float(o.position.z + fz * ahead - fx * side)};
}

void throttle_and_start_cases() {
    auto o = racing();
    for (auto skill : {Cpu, Expert}) {
        const auto in = drive_once(o, skill);
        assert(in.buttons == A && in.stick_x == 0 && in.stick_y == 0);
    }
    // The driver's steering is the steer-only steering.
    auto turning = o;
    turning.heading = std::int16_t(o.heading + 0x800);
    AutopilotState s1, s2;
    assert(check_autopilot_drive(turning, 0, s1, Cpu).stick_x == check_autopilot_steer(turning, 0, s2).stick_x);

    // Countdown: nobody holds A early. The CPU goes at green; expert presses
    // A with Z held once gRaceStartTimer <= 15 (start boost window is 14 +- 1).
    for (int timer : {100, 60, 30, 16}) {
        o.race_start_timer = timer;
        assert(!(drive_once(o, Cpu).buttons & A) && !(drive_once(o, Expert).buttons & A));
    }
    for (int timer : {15, 14, 13, 5, 1}) {
        o.race_start_timer = timer;
        assert(drive_once(o, Expert).buttons == (A | Z));
        assert(drive_once(o, Cpu).buttons == 0);
    }
    o.race_start_timer = 0;
    assert(drive_once(o, Cpu).buttons == A && drive_once(o, Expert).buttons == A);
    // Finished racers keep A down.
    o.race_finished = true;
    o.race_start_timer = 5;
    assert(drive_once(o, Cpu).buttons & A);
}

// Press Z one frame and release it the next: returns the buttons of both frames.
std::pair<std::uint16_t, std::uint16_t> two_frames(const AutopilotObservation& o, AutopilotSkill skill) {
    AutopilotState state;
    const auto first = check_autopilot_drive(o, 10, state, skill);
    const auto second = check_autopilot_drive(o, 11, state, skill);
    return {first.buttons, second.buttons};
}

void item_cases() {
    auto o = racing();
    // No item: Z is never touched.
    assert(two_frames(o, Cpu) == std::make_pair(A, A));
    o.balloon_quantity = 1;

    // Rocket (action 1): needs a racer ahead in range.
    o.weapon_id = 1;
    assert(two_frames(o, Cpu) == std::make_pair(A, A));
    o.has_ahead = true;
    o.ahead = relative(o, 800, 0);
    assert(two_frames(o, Cpu) == std::make_pair(std::uint16_t(A | Z), A)); // press, then release fires
    o.ahead = relative(o, 2000, 0);
    assert(two_frames(o, Cpu) == std::make_pair(A, A));
    // Expert waits until it lines up, and is closer.
    o.ahead = relative(o, 500, 0);
    assert(two_frames(o, Expert).first == (A | Z));
    o.ahead = relative(o, 500, 300);
    assert(two_frames(o, Expert) == std::make_pair(A, A));
    assert(two_frames(o, Cpu).first == (A | Z));
    o.ahead = relative(o, 800, 0);
    assert(two_frames(o, Expert) == std::make_pair(A, A)); // out of its shorter range
    // Homing rocket (weapon 0) does not need alignment.
    o.weapon_id = 0;
    o.ahead = relative(o, 1200, 400);
    assert(two_frames(o, Expert).first == (A | Z));
    assert(two_frames(o, Cpu) == std::make_pair(A, A));

    // Drops (action 2) go at a racer behind.
    o = racing();
    o.balloon_quantity = 1;
    o.weapon_id = 2;
    o.has_ahead = true;
    o.ahead = relative(o, 100, 0);
    assert(two_frames(o, Cpu) == std::make_pair(A, A)); // ahead is no use for a mine
    o.has_behind = true;
    o.behind = relative(o, -300, 0);
    assert(two_frames(o, Cpu).first == (A | Z));
    assert(two_frames(o, Expert) == std::make_pair(A, A));
    o.behind = relative(o, -200, 0);
    assert(two_frames(o, Expert).first == (A | Z));

    // Nitro (action 4): when not already boosting, on a straight, grounded.
    o = racing();
    o.balloon_quantity = 1;
    o.weapon_id = 4;
    assert(two_frames(o, Cpu).first == (A | Z));
    // Expert lifts A on the release frame (empowered boost); cpu keeps it.
    assert(two_frames(o, Cpu).second == A);
    assert(two_frames(o, Expert).second == 0);
    auto boosting = o;
    boosting.boost_timer = 20;
    assert(two_frames(boosting, Cpu) == std::make_pair(A, A));
    auto air = o;
    air.grounded_wheels = 0;
    assert(two_frames(air, Cpu) == std::make_pair(A, A));
    auto bend = o;
    bend.heading = std::int16_t(o.heading + 0x2000);
    assert(two_frames(bend, Cpu).first == A);
    // The third nitro is a boost too; a non-boost weapon does not lift A.
    o.weapon_id = 15;
    assert(two_frames(o, Expert).second == 0);
    o.weapon_id = 1;
    o.has_ahead = true;
    o.ahead = relative(o, 400, 0);
    assert(two_frames(o, Expert).second == A);

    // Magnets (3/6) fire at a racer ahead within the magnet's own range.
    o = racing();
    o.balloon_quantity = 1;
    o.has_ahead = true;
    o.weapon_id = 5;
    o.ahead = relative(o, 1200, 0);
    assert(two_frames(o, Cpu) == std::make_pair(A, A));
    o.weapon_id = 7;
    assert(two_frames(o, Cpu).first == (A | Z));

    // Shields go when a racer is close; everything else times out so each
    // item is exercised at least once.
    o = racing();
    o.balloon_quantity = 1;
    o.weapon_id = 13;
    assert(two_frames(o, Cpu) == std::make_pair(A, A));
    o.has_behind = true;
    o.behind = relative(o, -200, 0);
    assert(two_frames(o, Cpu).first == (A | Z));
    for (int weapon : {-1, 9, 3, 1}) {
        auto stale = racing();
        stale.balloon_quantity = 1;
        stale.weapon_id = std::int8_t(weapon);
        stale.has_ahead = weapon == 3; // nothing ever qualifies, except rocket's absent target
        stale.ahead = relative(stale, 5000, 0);
        AutopilotState state;
        int first_press = -1;
        for (int f = 0; f < 1200 && first_press < 0; ++f)
            if (check_autopilot_drive(stale, f, state, Cpu).buttons & Z)
                first_press = f;
        assert(first_press >= 0 && first_press <= 900);
    }
    // No Z while the countdown runs or once the item is gone.
    o = racing();
    o.balloon_quantity = 1;
    o.weapon_id = 4;
    o.race_start_timer = 40;
    assert(!(two_frames(o, Cpu).first & Z));
}

void zipper_and_corner_cases() {
    auto o = racing();
    o.has_zipper = true;
    o.zipper = relative(o, 110, 0);             // inside 100 + 2 * speed (120)
    assert(drive_once(o, Expert).buttons == 0); // A lifted for the green boost
    assert(drive_once(o, Cpu).buttons == A);
    o.zipper = relative(o, 400, 0);
    assert(drive_once(o, Expert).buttons == A);
    o.zipper = relative(o, 110, 0);
    auto boosting = o;
    boosting.boost_timer = 12;
    assert(drive_once(boosting, Expert).buttons == A);
    auto latched = o;
    latched.zipper_correction = true;
    assert(drive_once(latched, Expert).buttons == A);
    // Stopped short of a pad: A stays down, or the kart would sit there (the
    // unstick cooldown only runs down while it moves).
    auto parked = o;
    parked.zipper = relative(o, 60, 0);
    parked.velocity = 0;
    assert(drive_once(parked, Expert).buttons == A);
    parked.velocity = -3;
    assert(drive_once(parked, Expert).buttons == A);
    // Faster is earlier: more distance counts as "about to touch".
    o.zipper = relative(o, 160, 0);
    assert(drive_once(o, Expert).buttons == A);
    o.velocity = -40;
    assert(drive_once(o, Expert).buttons == 0);

    // R through corners for expert only: car and hover when grounded and moving,
    // plane at any time. Straight road: no R.
    auto bend = racing();
    bend.heading = std::int16_t(bend.heading + 0x1800);
    assert(std::abs(drive_once(bend, Expert).stick_x) >= 45);
    assert(drive_once(bend, Expert).buttons == (A | R));
    assert(drive_once(bend, Cpu).buttons == A);
    bend.vehicle = 1;
    assert(drive_once(bend, Expert).buttons & R);
    auto airborne = bend;
    airborne.grounded_wheels = 0;
    assert(!(drive_once(airborne, Expert).buttons & R));
    auto slow = bend;
    slow.velocity = -1;
    assert(!(drive_once(slow, Expert).buttons & R));
    assert(!(drive_once(racing(), Expert).buttons & R));
    auto plane = racing();
    plane.vehicle = 2;
    plane.heading = std::int16_t(plane.heading + 0x6000);
    plane.grounded_wheels = 0;
    assert(drive_once(plane, Expert).buttons & R);
}

void plane_pitch_case() {
    // Plane: Y stick follows the path slope; the car never uses it.
    auto o = racing();
    o.vehicle = 2;
    o.nodes = {{{0, 0, -300}, {0, 0, 0}, {0, 400, 300}, {0, 800, 600}}}; // climbing
    o.pitch = 0;
    const auto climb = drive_once(o, Cpu);
    auto dive = o;
    for (auto& n : dive.nodes)
        n.y = -n.y;
    const auto down = drive_once(dive, Cpu);
    assert(climb.stick_y != 0 && climb.stick_y == -down.stick_y);
    auto car = o;
    car.vehicle = 0;
    assert(drive_once(car, Cpu).stick_y == 0);
}

void unstick_cases() {
    for (auto skill : {Cpu, Expert}) {
        const int need = skill == Expert ? 20 : 60, back = skill == Expert ? 30 : 60;
        auto o = racing();
        o.velocity = 0; // pinned to a wall
        o.heading = std::int16_t(o.heading + 0x800);
        AutopilotState state;
        const auto normal = check_autopilot_drive(o, 0, state, skill);
        assert(normal.buttons & A && normal.stick_x > 0);
        int f = 1;
        for (; f < need; ++f)
            assert(check_autopilot_drive(o, f, state, skill).buttons & A);
        // Frame `need`: reverse. Steering flips, A off, B on, stick Y -50.
        for (int i = 0; i < back; ++i, ++f) {
            const auto in = check_autopilot_drive(o, f, state, skill);
            assert(!(in.buttons & A) && (in.buttons & B) && in.stick_y == -50 && in.stick_x < 0);
        }
        // Driving again, and no second kick until the cooldown has passed.
        assert(check_autopilot_drive(o, f, state, skill).buttons & A);
        // Moving cars never reverse.
        auto moving = racing();
        AutopilotState fresh;
        for (int g = 0; g < 400; ++g)
            assert(!(check_autopilot_drive(moving, g, fresh, skill).buttons & B));
        // Not while airborne, at the start line, or during the countdown.
        for (int variant = 0; variant < 2; ++variant) {
            auto held = o;
            if (variant == 0)
                held.grounded_wheels = 0;
            else
                held.race_start_timer = 50;
            AutopilotState fresh2;
            for (int g = 0; g < 300; ++g)
                assert(!(check_autopilot_drive(held, g, fresh2, skill).buttons & B));
        }
    }
}

void balloon_detour_case() {
    auto o = racing();
    o.has_balloon = true;
    o.balloon = relative(o, 400, -150);
    const auto plain = drive_once(racing(), Expert).stick_x;
    assert(plain == 0);
    assert(drive_once(o, Expert).stick_x > 0); // toward the balloon on the right
    assert(drive_once(o, Cpu).stick_x == 0);   // the CPU ignores it
    auto holding = o;
    holding.balloon_quantity = 1;
    holding.weapon_id = 13;
    assert(drive_once(holding, Expert).stick_x == 0);
    auto behind = o;
    behind.balloon = relative(o, -400, -150);
    assert(drive_once(behind, Expert).stick_x == 0);
}

void determinism_case() {
    // Same observations, fresh state: the same inputs, twice over; and the
    // convenience entry point answers the same for a repeated frame.
    auto o = racing();
    o.balloon_quantity = 1;
    o.weapon_id = 4;
    o.has_zipper = true;
    for (auto skill : {Cpu, Expert}) {
        AutopilotState a, b;
        for (std::uint32_t f = 0; f < 200; ++f) {
            o.zipper = relative(o, 400 - 3.0 * f, 0);
            o.heading = std::int16_t(o.heading + (f % 7 == 0 ? 0x150 : -0x20));
            assert(check_autopilot_drive(o, f, a, skill) == check_autopilot_drive(o, f, b, skill));
        }
    }
    AutopilotView view{};
    view[0] = o;
    view[0].boost_timer = 0;
    const auto first = check_autopilot_input(view, 0, 5000);
    assert(check_autopilot_input(view, 0, 5000) == first);
}

void mode_cases() {
    assert(parse_autopilot_mode(nullptr, nullptr) == AutopilotMode::Cpu);
    assert(parse_autopilot_mode("", "") == AutopilotMode::Cpu);
    assert(parse_autopilot_mode(nullptr, "cpu") == AutopilotMode::Cpu);
    assert(parse_autopilot_mode(nullptr, "expert") == AutopilotMode::Expert);
    assert(parse_autopilot_mode(nullptr, "bogus") == AutopilotMode::Cpu);
    assert(parse_autopilot_mode("wave", "expert") == AutopilotMode::Wave);
    assert(parse_autopilot_mode("steer", "expert") == AutopilotMode::Steer);
    assert(parse_autopilot_mode("other", "expert") == AutopilotMode::Expert);
}

// With a speed that drops while steering hard, the expert's longer look-ahead
// cuts the corners and gets round faster than the CPU line. (Steer-only is
// the CPU's steering without items, so it cannot be faster than cpu.)
void expert_is_faster_case() {
    for (bool clockwise : {false, true}) {
        const auto cpu = drive(clockwise, 0, 0, 9000, 0);
        const auto expert = drive(clockwise, 0, 0, 9000, 1);
        std::printf("scrub loop cw=%d cpu third lap at %u expert at %u\n", int(clockwise), cpu.third_lap,
                    expert.third_lap);
        assert(cpu.third_lap > 0 && expert.third_lap > 0 && expert.third_lap < cpu.third_lap);
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

// Item, throttle and company state, all through guest memory.
void capture_extras_case() {
    using namespace check_autopilot_symbols;
    for (unsigned revision : {77u, 80u}) {
        const auto& s = revision == 80 ? symbols80 : symbols77;
        constexpr std::uint32_t Ports = 0x80330000, ObjA = 0x80300000, DataA = 0x80310000, ObjB = 0x80304000,
                                DataB = 0x80314000, ObjC = 0x80308000, DataC = 0x80318000, Nodes = 0x80320000,
                                Objects = 0x80340000, Pad = 0x80341000, Balloon = 0x80342000, Table = 0x80350000,
                                Section = 0x80360000;
        Ram ram;
        ram.put(s.checkpoint_count, 6, 4);
        ram.put(s.checkpoints, Nodes, 4);
        ram.put(s.racers_by_port, Ports, 4);
        ram.put(s.racer_count, 3, 4);
        ram.put(s.race_start_timer, 0, 4);
        const std::uint32_t objs[3] = {ObjA, ObjB, ObjC}, datas[3] = {DataA, DataB, DataC};
        const float where[3] = {0, 100, -100};
        const int laps[3] = {1, 1, 1}, nexts[3] = {2, 3, 1};
        for (int i = 0; i < 3; ++i) {
            ram.put(Ports + i * 4, objs[i], 4);
            ram.put(objs[i] + 0x48, 1, 2);
            ram.put(objs[i] + 0x64, datas[i], 4);
            ram.real(objs[i] + 0x0C, where[i]);
            ram.real(objs[i] + 0x14, where[i]);
            ram.put(datas[i], i == 0 ? 0 : 0xFFFF, 2);
            ram.put(datas[i] + 0x192, nexts[i], 1);
            ram.put(datas[i] + 0x193, laps[i], 1);
            ram.real(datas[i] + 0xA8, 0.5f);
            ram.put(datas[i] + 0x1D6, 0, 1);
        }
        ram.put(DataA + 0x1D3, 7, 1);
        ram.put(DataA + 0x1E2, 3, 1);
        ram.put(DataA + 0x1F5, 1, 1);
        ram.put(DataA + 0x1A0, 0x8000, 2); // faces +z... (heading + 0x8000 = 0)
        ram.put(ObjA + 0x02, 0x0123, 2);
        ram.real(DataA + 0x2C, -12.5f);
        ram.put(s.race_start_timer, 14, 4);
        for (unsigned i = 0; i < 6; ++i) {
            ram.real(Nodes + i * 0x3C + 0x10, float(i));
            ram.real(Nodes + i * 0x3C + 0x18, float(i));
            ram.put(Nodes + i * 0x3C + 0x3A, 0xFF, 1);
        }
        // Item: type 3 level 1 holding 2. Table entry 12 sits at Table + 48 and
        // counts s32 words into the section (get_misc_asset), so 0x40 is byte 0x100.
        ram.put(DataA + 0x172, 3, 1);
        ram.put(DataA + 0x173, 2, 1);
        ram.put(DataA + 0x174, 1, 1);
        ram.put(s.misc_table_length, 40, 4);
        ram.put(s.misc_table, Table, 4);
        ram.put(s.misc_section, Section, 4);
        ram.put(Table + 12 * 4, 0x40, 4);
        ram.put(Section + 0x100 + 3 * 10 + 1 * 2, 6, 1);
        // Objects: a ground zipper and a weapon balloon in front (+z), one zipper behind.
        ram.put(s.object_list, Objects, 4);
        ram.put(s.object_count, 3, 4);
        const std::uint32_t listed[3] = {Pad, Balloon, Pad + 0x400};
        const std::uint16_t kind[3] = {82, 17, 82};
        const float z[3] = {200, 300, -200};
        for (int i = 0; i < 3; ++i) {
            ram.put(Objects + i * 4, listed[i], 4);
            ram.put(listed[i] + 0x48, kind[i], 2);
            ram.real(listed[i] + 0x14, z[i]);
        }
        auto view = capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), revision);
        const auto& o = view[0];
        assert(o.valid && o.race_start_timer == 14 && o.velocity == -12.5f && o.boost_timer == 7);
        assert(o.grounded_wheels == 3 && o.zipper_correction && o.pitch == 0x0123 && o.lap == 1);
        assert(o.balloon_quantity == 2 && o.weapon_id == 6);
        // Racer B is further along (next 3 vs 2), racer C behind (next 1).
        assert(o.has_ahead && o.ahead.x == 100 && o.has_behind && o.behind.x == -100);
        assert(o.has_zipper && o.zipper.z == 200 && o.has_balloon && o.balloon.z == 300);
        // A plane wants an air zipper: the ground pad no longer counts.
        ram.put(DataA + 0x1D6, 2, 1);
        view = capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), revision);
        assert(!view[0].has_zipper && view[0].has_balloon);
        // No item: weapon stays unknown. Missing tables leave defaults.
        ram.put(DataA + 0x173, 0, 1);
        ram.put(s.object_count, 0, 4);
        ram.put(s.racer_count, 0, 4);
        ram.put(s.race_start_timer, 0xFFFFFFFFu, 4);
        view = capture_autopilot_observations(ram.bytes.data(), ram.bytes.size(), revision);
        assert(view[0].valid && view[0].weapon_id == -1 && !view[0].has_zipper && !view[0].has_balloon);
        assert(!view[0].has_ahead && !view[0].has_behind && view[0].race_start_timer == 0);
    }
}
} // namespace

int main() {
    // The steering is what's under test: don't inherit the wave-only switch.
#if defined(_WIN32)
    _putenv_s("DKR_OWNED_CHECK_INPUT", "");
    _putenv_s("DKR_OWNED_CHECK_SKILL", "");
#else
    unsetenv("DKR_OWNED_CHECK_INPUT");
    unsetenv("DKR_OWNED_CHECK_SKILL");
#endif
    steering_cases();
    fallback_cases();
    closed_loop();
    throttle_and_start_cases();
    item_cases();
    zipper_and_corner_cases();
    plane_pitch_case();
    unstick_cases();
    balloon_detour_case();
    determinism_case();
    mode_cases();
    expert_is_faster_case();
    capture_case();
    capture_extras_case();
    std::puts("check autopilot ok");
    return 0;
}
