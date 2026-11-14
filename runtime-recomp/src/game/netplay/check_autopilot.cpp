#include "check_autopilot.hpp"
#include "autopilot_symbols.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string_view>

namespace dkr::runtime::netplay::experimental {
namespace {
using Axis = std::array<double, 4>;

// Catmull-Rom segment between data[1] and data[2], as cubic_spline_interpolation
// and catmull_rom_derivative in the decomp (src/objects.c).
struct Cubic {
    double a, b, c, p1;
    explicit Cubic(const Axis& d)
        : a(-0.5 * d[0] + 1.5 * d[1] - 1.5 * d[2] + 0.5 * d[3]), b(d[0] - 2.5 * d[1] + 2.0 * d[2] - 0.5 * d[3]),
          c(0.5 * (d[2] - d[0])), p1(d[1]) {}
    double value(double m) const {
        return ((a * m + b) * m + c) * m + p1;
    }
    double slope(double m) const {
        return (3 * a * m + 2 * b) * m + c;
    }
};

// arctan2_f(y, x) is libultra atan2s(y, x): atan2 in s16 units, 0x8000 = pi.
int arctan2_units(double y, double x) {
    return int(std::lround(std::atan2(y, x) * (32768.0 / std::numbers::pi)));
}
// The game's s0: yaw of a horizontal direction, `arctan2_f(dx, dz) - 0x8000`.
int path_yaw(double dx, double dz) {
    return arctan2_units(dx, dz) - 0x8000;
}
// WRAP(x, -0x8000, 0x8000) on an angle difference (the macro is off by one at
// the seam; a proper modulo is the same everywhere that matters).
int wrap16(int v) {
    return ((v + 0x8000) & 0xFFFF) - 0x8000;
}
template <class F> Axis column(const AutopilotObservation& obs, F f) {
    return {f(obs.nodes[0]), f(obs.nodes[1]), f(obs.nodes[2]), f(obs.nodes[3])};
}
} // namespace

PackedInput check_square_wave_input(std::uint32_t frame) {
    return PackedInput{0x8000, std::int8_t((frame / 15) % 2 ? 30 : -30), 0};
}

// Port of the steering in func_80045C48 (decomp src/racer.c ~1360-1560), for
// the lane-less case (racer->unk1C9 != 0 and the per-racer lane offsets
// unk2E/unk32 are not applied, so this follows the track's center line).
// Stick sign is the game's own: gCurrentStickX = -(tangent change >> 3) +
// -(heading error >> 5) with errors measured as (path yaw - steerVisualRotation).
// The CPU loop is negative feedback, so positive stick X (right) turns the kart
// toward smaller steerVisualRotation. Not ported: the hovercraft's direct
// steerVisualRotation nudge and the Y stick (pitch).
PackedInput check_autopilot_steer(const AutopilotObservation& obs, std::uint32_t frame, AutopilotState& state) {
    if (!obs.valid)
        return check_square_wave_input(frame);
    const Cubic sx(column(obs, [](auto& n) { return double(n.x); }));
    const Cubic sy(column(obs, [](auto& n) { return double(n.y); }));
    const Cubic sz(column(obs, [](auto& n) { return double(n.z); }));
    double m = std::clamp(1.0 - double(obs.checkpoint_distance), 0.0, 1.0);

    // Feed-forward: how fast the path direction is turning.
    const auto tangent = std::uint16_t(path_yaw(sx.slope(m), sz.slope(m)));
    const int turn = state.has_tangent ? wrap16(int(tangent) - int(state.tangent)) : 0;
    state.tangent = tangent;
    state.has_tangent = true;
    int stick = -(turn >> 3);

    // Far off the path direction: aim at the end of this segment.
    if (std::abs(wrap16(int(tangent) - int(std::uint16_t(obs.heading)))) > 0x3000)
        m = 1.0;

    // Look-ahead: a point 500 units along the tangent from the spline point.
    double dx = sx.slope(m), dy = sy.slope(m), dz = sz.slope(m);
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double scale = length != 0.0 ? 500.0 / length : 1.0;
    const double tx = sx.value(m) + dx * scale - obs.position.x;
    const double tz = sz.value(m) + dz * scale - obs.position.z;
    const int error = -wrap16(int(std::uint16_t(path_yaw(tx, tz))) - int(std::uint16_t(obs.heading)));
    switch (obs.vehicle) {
    case 2: // plane
        stick += error >> 6;
        break;
    case 4: // loop-de-loop
        break;
    case 5: // Tricky
    case 8: // Smokey
        stick += error >> 4;
        break;
    default:
        stick += error >> 5;
    }
    stick = std::clamp(stick, -AutopilotStickLimit, AutopilotStickLimit);
    return PackedInput{0x8000, std::int8_t(stick), 0};
}

PackedInput check_autopilot_input(const AutopilotView& view, unsigned port, std::uint32_t frame) {
    thread_local AutopilotState state;
    thread_local bool have_last = false;
    thread_local bool have_logged = false;
    thread_local std::uint32_t last_frame = 0, logged_frame = 0;
    thread_local PackedInput last;
    // DKR_OWNED_CHECK_INPUT=wave keeps the old left/right pattern, for comparing
    // against earlier runs or when the steering itself is under suspicion.
    static const bool wave_only = [] {
        const char* choice = std::getenv("DKR_OWNED_CHECK_INPUT");
        return choice && std::string_view(choice) == "wave";
    }();
    const auto& obs = view[port < view.size() ? port : 0];
    const bool usable = !wave_only && port < view.size() && obs.valid;
    // The pump may ask again for a frame it already sampled; the feed-forward
    // term must see each frame once.
    if (!have_last || frame != last_frame) {
        last = usable ? check_autopilot_steer(obs, frame, state) : check_square_wave_input(frame);
        if (!usable)
            state.has_tangent = false;
        have_last = true;
        last_frame = frame;
    }
    if (frame % 60 == 0 && !(have_logged && frame == logged_frame)) {
        have_logged = true;
        logged_frame = frame;
        std::fprintf(stderr, "[check][autopilot] frame=%u stick=%d checkpoint=%d valid=%d\n", frame, int(last.stick_x),
                     usable ? int(obs.next_checkpoint) : -1, usable ? 1 : 0);
    }
    return last;
}

namespace {
class GuestRam {
    const std::uint8_t* ram_;
    std::size_t size_;

  public:
    GuestRam(const std::uint8_t* ram, std::size_t size) : ram_(ram), size_(size) {}
    bool range(std::uint32_t p, std::size_t n) const {
        return ram_ && p >= 0x80000000U && p < 0xA0000000U && (p & 0x1FFFFFFFU) <= size_ &&
               n <= size_ - (p & 0x1FFFFFFFU);
    }
    // Big-endian guest value of n bytes in the byte-swapped image; 0 if unreadable.
    std::uint32_t get(std::uint32_t p, unsigned n = 4) const {
        if (!range(p, n))
            return 0;
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v = v << 8 | ram_[((p & 0x1FFFFFFFU) + i) ^ 3U];
        return v;
    }
    float real(std::uint32_t p) const {
        return std::bit_cast<float>(get(p));
    }
};
bool sane(float v) {
    return std::isfinite(v) && std::fabs(v) < 1.0e6f;
}
} // namespace

// Guest layouts (decomp include/structs.h and src/objects.h), offsets only:
//   Object:          trans.x/y/z_position 0x0C/0x10/0x14, behaviorId 0x48 (s16, 1 = racer), Object_Racer* 0x64
//   Object_Racer:    playerIndex 0x000, nextCheckpoint 0x192 (s8), steerVisualRotation 0x1A0 (s16),
//                    isOnAlternateRoute 0x1C8, vehicleID 0x1D6; checkpoint_distance 0x0A8 (f32)
//   CheckpointNode:  x/y/z 0x10/0x14/0x18 (f32), altRouteID 0x3A (s8); sizeof 0x3C
AutopilotView capture_autopilot_observations(const std::uint8_t* ram, std::size_t bytes, unsigned revision) {
    AutopilotView view{};
    if (revision != 77 && revision != 80)
        return view;
    constexpr std::uint32_t NodeSize = 0x3C;
    const auto& a = revision == 80 ? check_autopilot_symbols::symbols80 : check_autopilot_symbols::symbols77;
    const GuestRam g(ram, bytes);
    const auto count = g.get(a.checkpoint_count);
    const auto nodes = g.get(a.checkpoints);
    const auto racers = g.get(a.racers_by_port);
    if (count < 4 || count > 127 || !g.range(nodes, count * NodeSize) || !g.range(racers, 16))
        return view;
    for (unsigned port = 0; port < view.size(); ++port) {
        const auto object = g.get(racers + port * 4);
        if (!g.range(object, 0x68) || g.get(object + 0x48, 2) != 1)
            continue;
        const auto data = g.get(object + 0x64);
        if (!g.range(data, 0x1D8) || g.get(data, 2) > 3)
            continue;
        auto& obs = view[port];
        obs.vehicle = std::uint8_t(g.get(data + 0x1D6, 1));
        obs.position = {g.real(object + 0x0C), g.real(object + 0x10), g.real(object + 0x14)};
        obs.heading = std::int16_t(g.get(data + 0x1A0, 2));
        obs.next_checkpoint = std::int8_t(g.get(data + 0x192, 1));
        obs.checkpoint_distance = g.real(data + 0xA8);
        bool ok = sane(obs.position.x) && sane(obs.position.y) && sane(obs.position.z) && obs.next_checkpoint >= 0 &&
                  std::uint32_t(obs.next_checkpoint) < count && std::isfinite(obs.checkpoint_distance);
        const bool alternate = g.get(data + 0x1C8, 1) != 0;
        auto index = obs.next_checkpoint - 2;
        for (std::size_t i = 0; ok && i < obs.nodes.size(); ++i) {
            index = (index % int(count) + int(count)) % int(count);
            auto node = nodes + std::uint32_t(index) * NodeSize;
            const auto route = std::int8_t(g.get(node + 0x3A, 1));
            if (alternate && route != -1 && route >= 0) {
                node = nodes + std::uint32_t(route) * NodeSize;
                ok = g.range(node, NodeSize);
            }
            obs.nodes[i] = {g.real(node + 0x10), g.real(node + 0x14), g.real(node + 0x18)};
            ok = ok && sane(obs.nodes[i].x) && sane(obs.nodes[i].y) && sane(obs.nodes[i].z);
            ++index;
        }
        obs.valid = ok;
    }
    return view;
}
} // namespace dkr::runtime::netplay::experimental
