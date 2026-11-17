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

namespace {
struct Steer {
    int stick_x = 0, stick_y = 0;
};

// Port of the steering in func_80045C48 (decomp src/racer.c ~1360-1560), for
// the lane-less case (racer->unk1C9 != 0 and the per-racer lane offsets
// unk2E/unk32 are not applied, so this follows the track's center line).
// Stick sign is the game's own: gCurrentStickX = -(tangent change >> 3) +
// -(heading error >> 5) with errors measured as (path yaw - steerVisualRotation).
// The CPU loop is negative feedback, so positive stick X (right) turns the kart
// toward smaller steerVisualRotation. `lookahead` is the game's 500 units;
// `aim`, when set, replaces the look-ahead point (balloon detours).
// Y stick (pitch) is the same function's second axis, used for the plane and
// Smokey only. Not ported: the hovercraft's direct steerVisualRotation nudge
// (it writes the racer, which a test input must not).
Steer steer_core(const AutopilotObservation& obs, AutopilotState& state, double lookahead, const CheckpointPoint* aim,
                 bool pitch_axis) {
    const Cubic sx(column(obs, [](auto& n) { return double(n.x); }));
    const Cubic sy(column(obs, [](auto& n) { return double(n.y); }));
    const Cubic sz(column(obs, [](auto& n) { return double(n.z); }));
    double m = std::clamp(1.0 - double(obs.checkpoint_distance), 0.0, 1.0);

    // Feed-forward: how fast the path direction is turning.
    const auto tangent = std::uint16_t(path_yaw(sx.slope(m), sz.slope(m)));
    const int turn = state.has_tangent ? wrap16(int(tangent) - int(state.tangent)) : 0;
    state.tangent = tangent;
    state.has_tangent = true;
    state.turn = turn;
    int stick = -(turn >> 3);

    // Pitch feed-forward from the slope of the path (derivative scaled to 100).
    int stick_y = 0;
    if (pitch_axis) {
        const double len = std::sqrt(sx.slope(m) * sx.slope(m) + sy.slope(m) * sy.slope(m) + sz.slope(m) * sz.slope(m));
        const double yd = len != 0.0 ? sy.slope(m) * (100.0 / len) : sy.slope(m);
        const auto pt = std::uint16_t(arctan2_units(yd, 100.0));
        const int pturn = state.has_pitch ? wrap16(int(pt) - int(state.pitch_tangent)) : 0;
        state.pitch_tangent = pt;
        state.has_pitch = true;
        stick_y = -(pturn >> 3);
    }

    // Far off the path direction: aim at the end of this segment.
    const int off_path = wrap16(int(tangent) - int(std::uint16_t(obs.heading)));
    state.off_path = off_path;
    if (std::abs(off_path) > 0x3000)
        m = 1.0;

    // Look-ahead: a point `lookahead` units along the tangent from the spline point.
    double dx = sx.slope(m), dy = sy.slope(m), dz = sz.slope(m);
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double scale = length != 0.0 ? lookahead / length : 1.0;
    const double tx = (aim ? aim->x : sx.value(m) + dx * scale) - obs.position.x;
    const double ty = (aim ? aim->y : sy.value(m) + dy * scale) - obs.position.y;
    const double tz = (aim ? aim->z : sz.value(m) + dz * scale) - obs.position.z;
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
    if (pitch_axis && (obs.vehicle == 2 || obs.vehicle == 8)) {
        const int want = arctan2_units(ty, lookahead);
        const int v1 = -wrap16(int(std::uint16_t(want)) - int(std::uint16_t(obs.pitch)));
        stick_y += v1 >> (obs.vehicle == 2 ? 6 : 3);
    } else {
        stick_y = 0;
    }
    return {std::clamp(stick, -AutopilotStickLimit, AutopilotStickLimit),
            std::clamp(stick_y, -AutopilotStickLimit, AutopilotStickLimit)};
}

constexpr double CpuLookahead = 500, ExpertLookahead = 800;
constexpr std::uint16_t A = 0x8000, B = 0x4000, Z = 0x2000, R = 0x0010;
} // namespace

PackedInput check_autopilot_steer(const AutopilotObservation& obs, std::uint32_t frame, AutopilotState& state) {
    if (!obs.valid)
        return check_square_wave_input(frame);
    const auto s = steer_core(obs, state, CpuLookahead, nullptr, false);
    return PackedInput{A, std::int8_t(s.stick_x), 0};
}

namespace {
double distance_xz(const CheckpointPoint& a, const CheckpointPoint& b) {
    return std::hypot(double(a.x) - b.x, double(a.z) - b.z);
}
// Stick-sign bearing of a point from the racer's heading, like the steering's
// `error`: positive means turn right. 0 is dead ahead.
int bearing_of(const AutopilotObservation& obs, const CheckpointPoint& p) {
    return -wrap16(int(std::uint16_t(path_yaw(double(p.x) - obs.position.x, double(p.z) - obs.position.z))) -
                   int(std::uint16_t(obs.heading)));
}

// WeaponType values (decomp src/racer.h) and gRacerAIBalloonActionTable
// (src/racer.c): 1 fire at a racer ahead, 2 drop behind, 3 and 6 magnets, 4
// boost, 5 shields. The table gives 0 for the third nitro; it is a boost too.
enum Weapon : int { Homing = 0, Magnet1 = 5 };
constexpr int WeaponAction[16] = {1, 1, 2, 2, 4, 3, 0, 6, 4, 3, 2, 2, 5, 5, 5, 4};

// The CPU (func_80042D20) fires only after rolls against percentages in the
// AI table, so the conditions here are the table's actions with fixed ranges:
// test choices, not game numbers. Nothing waits forever: any item is used
// after MaxHold frames so every weapon gets exercised.
constexpr int MaxHold = 900;

bool wants_to_fire(const AutopilotObservation& obs, AutopilotState& st, bool expert) {
    if (obs.balloon_quantity <= 0 || obs.race_start_timer > 0 || obs.race_finished) {
        st.item_hold = 0;
        return false;
    }
    ++st.item_hold;
    const int w = obs.weapon_id;
    const int action = w >= 0 && w < 16 ? WeaponAction[w] : -1;
    const double ahead = obs.has_ahead ? distance_xz(obs.position, obs.ahead) : 1e9;
    const double behind = obs.has_behind ? distance_xz(obs.position, obs.behind) : 1e9;
    const bool straight = std::abs(st.off_path) < 0x1000 && std::abs(st.turn) < 0x80;
    bool fire = false;
    switch (action) {
    case 1:
        if (w == Homing)
            fire = ahead < (expert ? 1500 : 1000);
        else
            fire = ahead < (expert ? 600 : 1000) && (!expert || std::abs(bearing_of(obs, obs.ahead)) < 0x0A00);
        break;
    case 2:
        fire = behind < (expert ? 250 : 400);
        break;
    case 3:
    case 0:
    case 6:
        fire = ahead < (w == Magnet1 ? 1000 : 1500);
        break;
    case 4:
        fire = obs.boost_timer == 0 && obs.grounded_wheels > 0 && straight;
        break;
    case 5:
        fire = std::min(ahead, behind) < 350 || st.item_hold >= 300;
        break;
    default:
        break;
    }
    return fire || st.item_hold >= MaxHold;
}

bool is_boost_weapon(const AutopilotObservation& obs) {
    return obs.weapon_id >= 0 && obs.weapon_id < 16 && WeaponAction[obs.weapon_id] == 4;
}
} // namespace

PackedInput check_autopilot_drive(const AutopilotObservation& obs, std::uint32_t frame, AutopilotState& st,
                                  AutopilotSkill skill) {
    if (!obs.valid)
        return check_square_wave_input(frame);
    const bool expert = skill == AutopilotSkill::Expert;
    const double speed = -double(obs.velocity);

    // Expert, empty handed with a balloon in reach: steer at it, unless it is
    // behind the shoulder.
    CheckpointPoint balloon;
    const bool detour =
        expert && obs.has_balloon && obs.balloon_quantity <= 0 && std::abs(bearing_of(obs, obs.balloon)) < 0x2000;
    if (detour)
        balloon = obs.balloon;
    const bool pitch_axis = obs.vehicle == 2 || obs.vehicle == 8;
    const auto steer =
        steer_core(obs, st, expert ? ExpertLookahead : CpuLookahead, detour ? &balloon : nullptr, pitch_axis);
    int stick_x = steer.stick_x, stick_y = steer.stick_y;
    std::uint16_t buttons = 0;

    const bool racing = obs.race_start_timer <= 0;
    // A: held through the race (the game's AI holds it once its start delay is
    // over, and everyone holds it after finishing). At the line, a human gets a
    // start boost by first pressing A within a frame of gRaceStartTimer == 14
    // (handle_racer_top_speed), perfect with Z held; the CPU just goes at green.
    if (racing || obs.race_finished)
        buttons |= A;
    else if (expert && obs.race_start_timer <= 15)
        buttons |= A | Z;
    // Z for items: pressed one frame, released the next (the release fires).
    bool fired = false;
    if (racing && st.z_down) {
        st.z_down = false;
        st.item_hold = 0;
        fired = true;
    } else if (racing && wants_to_fire(obs, st, expert)) {
        st.z_down = true;
        buttons |= Z;
    }
    if (!racing)
        st.z_down = false;
    if (expert && racing) {
        // Empowered boost: A up on the frame a boost starts (Object_Racer::
        // throttleReleased), for nitro on its release and for a zipper pad.
        if (fired && is_boost_weapon(obs))
            buttons &= ~A;
        // Only while rolling: a kart stopped short of the pad needs A to reach it.
        if (obs.has_zipper && obs.boost_timer == 0 && !obs.zipper_correction && speed > 3) {
            const double pad = distance_xz(obs.position, obs.zipper);
            if (pad < 100 + 2 * speed)
                buttons &= ~A;
        }
        // R in corners: power slide for cars, tight turn for hover and plane.
        const bool corner = std::abs(stick_x) >= (obs.vehicle == 2 ? 50 : 45);
        if (corner && obs.grounded_wheels > 0 && speed > 3 && obs.vehicle <= 1)
            buttons |= R;
        if (corner && obs.vehicle == 2)
            buttons |= R;
    }

    // Port of the reverse kick in racer_AI_pathing_inputs (unk213/214/215):
    // barely moving for over a second while grounded, reverse for a second.
    // Expert reacts in a third of the time.
    const int need = expert ? 20 : 60, back = expert ? 30 : 60, rest = expert ? 30 : 120;
    if (st.reverse == 0 && obs.velocity < -0.5f)
        st.cooldown = std::max(st.cooldown - 1, 0);
    if (obs.velocity > -1.0f && st.reverse == 0 && racing && obs.grounded_wheels != 0 && st.cooldown == 0) {
        if (++st.stuck > need) {
            st.stuck = 0;
            st.reverse = back;
            st.cooldown = rest;
        }
    } else {
        st.reverse = std::max(st.reverse - 1, 0);
        st.stuck = 0;
    }
    if (st.reverse != 0) {
        stick_x = -stick_x;
        buttons &= ~(A | R);
        stick_y = -50;
        buttons |= B;
    }
    stick_x = std::clamp(stick_x, -AutopilotStickLimit, AutopilotStickLimit);
    stick_y = std::clamp(stick_y, -AutopilotStickLimit, AutopilotStickLimit);
    return PackedInput{buttons, std::int8_t(stick_x), std::int8_t(stick_y)};
}

AutopilotMode parse_autopilot_mode(const char* input, const char* skill) {
    const std::string_view in = input ? input : "", sk = skill ? skill : "";
    if (in == "wave")
        return AutopilotMode::Wave;
    if (in == "steer")
        return AutopilotMode::Steer;
    return sk == "expert" ? AutopilotMode::Expert : AutopilotMode::Cpu;
}

PackedInput check_autopilot_input(const AutopilotView& view, unsigned port, std::uint32_t frame) {
    thread_local AutopilotState state;
    thread_local bool have_last = false;
    thread_local bool have_logged = false;
    thread_local std::uint32_t last_frame = 0, logged_frame = 0;
    thread_local PackedInput last;
    // Lap bookkeeping for the "[check][lap]" lines: the frame the light went
    // green and the frame each lap started, so lap times compare across skills.
    thread_local bool racing_seen = false;
    thread_local int lap_seen = 0;
    thread_local std::uint32_t lap_start = 0;
    // DKR_OWNED_CHECK_INPUT=wave keeps the old left/right pattern and =steer the
    // steer-only driver, for comparing against earlier runs or when the
    // steering itself is under suspicion; DKR_OWNED_CHECK_SKILL picks cpu or expert.
    static const AutopilotMode mode =
        parse_autopilot_mode(std::getenv("DKR_OWNED_CHECK_INPUT"), std::getenv("DKR_OWNED_CHECK_SKILL"));
    static const char* const names[] = {"wave", "steer", "cpu", "expert"};
    const auto& obs = view[port < view.size() ? port : 0];
    const bool usable = mode != AutopilotMode::Wave && port < view.size() && obs.valid;
    // The pump may ask again for a frame it already sampled; the driver's
    // memory must see each frame once.
    if (!have_last || frame != last_frame) {
        if (!usable) {
            last = check_square_wave_input(frame);
            state = AutopilotState{};
            racing_seen = false;
        } else if (mode == AutopilotMode::Steer) {
            last = check_autopilot_steer(obs, frame, state);
        } else {
            last = check_autopilot_drive(obs, frame, state,
                                         mode == AutopilotMode::Expert ? AutopilotSkill::Expert : AutopilotSkill::Cpu);
        }
        if (usable && obs.race_start_timer <= 0) {
            if (!racing_seen || obs.lap < lap_seen) {
                racing_seen = true;
                lap_seen = obs.lap;
                lap_start = frame;
                std::fprintf(stderr, "[check][lap] port=%u mode=%s go frame=%u lap=%d\n", port, names[int(mode)], frame,
                             int(obs.lap));
            } else if (obs.lap > lap_seen) {
                std::fprintf(stderr, "[check][lap] port=%u mode=%s lap=%d done frame=%u lap_frames=%u\n", port,
                             names[int(mode)], lap_seen, frame, frame - lap_start);
                lap_seen = obs.lap;
                lap_start = frame;
            }
        }
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
// Racer progress along the course in checkpoint units (laps * count + segment).
double course_progress(const GuestRam& g, std::uint32_t data, std::uint32_t count) {
    return double(std::int8_t(g.get(data + 0x193, 1))) * count + std::int8_t(g.get(data + 0x192, 1)) -
           g.real(data + 0xA8);
}

// Items, throttle and company state for one racer already known to be readable.
// Everything has a "nothing known" default, so a missing table only turns the
// matching behaviour off.
template <class Symbols>
void read_extras(const GuestRam& g, const Symbols& a, AutopilotObservation& obs, unsigned port, std::uint32_t racers,
                 std::uint32_t object, std::uint32_t data, std::uint32_t count) {
    if (!g.range(data, 0x1F8))
        return;
    const auto timer = std::int32_t(g.get(a.race_start_timer));
    obs.race_start_timer = timer > 0 && timer < 1000 ? timer : 0;
    obs.velocity = g.real(data + 0x2C);
    if (!std::isfinite(obs.velocity))
        obs.velocity = 0;
    obs.lap = std::int8_t(g.get(data + 0x193, 1));
    obs.boost_timer = std::int8_t(g.get(data + 0x1D3, 1));
    obs.race_finished = g.get(data + 0x1D8, 1) != 0;
    obs.grounded_wheels = std::int8_t(g.get(data + 0x1E2, 1));
    obs.zipper_correction = g.get(data + 0x1F5, 1) != 0;
    obs.pitch = std::int16_t(g.get(object + 0x02, 2));
    obs.balloon_quantity = std::int8_t(g.get(data + 0x173, 1));
    // Held weapon: misc asset ASSET_MISC_BALLOON_DATA (index 12), s8 at
    // [balloon_type * 10 + balloon_level * 2] as in the human item code. The
    // table counts s32 words into the section (get_misc_asset indexes s32*).
    const auto type = std::int8_t(g.get(data + 0x172, 1)), level = std::int8_t(g.get(data + 0x174, 1));
    if (obs.balloon_quantity > 0 && type >= 0 && type < 16 && level >= 0 && level <= 3) {
        const auto length = g.get(a.misc_table_length);
        const auto table = g.get(a.misc_table), section = g.get(a.misc_section);
        constexpr std::uint32_t BalloonData = 12;
        if (length > BalloonData && g.range(table + BalloonData * 4, 4)) {
            const auto entry = section + g.get(table + BalloonData * 4) * 4 + std::uint32_t(type) * 10 + level * 2;
            if (g.range(entry, 1))
                obs.weapon_id = std::int8_t(g.get(entry, 1));
        }
    }

    // Nearest racers ahead and behind by course progress.
    const auto others = std::min<std::uint32_t>(g.get(a.racer_count), 8);
    if (others > 1 && g.range(racers, others * 4)) {
        const double mine = course_progress(g, data, count);
        double best_ahead = 1e30, best_behind = -1e30;
        for (std::uint32_t j = 0; j < others; ++j) {
            const auto other = g.get(racers + j * 4);
            if (j == port || !g.range(other, 0x68) || g.get(other + 0x48, 2) != 1)
                continue;
            const auto other_data = g.get(other + 0x64);
            if (!g.range(other_data, 0x1F8))
                continue;
            const double delta = course_progress(g, other_data, count) - mine;
            const CheckpointPoint at{g.real(other + 0x0C), g.real(other + 0x10), g.real(other + 0x14)};
            if (!sane(at.x) || !sane(at.y) || !sane(at.z))
                continue;
            if (delta > 0 && delta < best_ahead) {
                best_ahead = delta;
                obs.has_ahead = true;
                obs.ahead = at;
            } else if (delta < 0 && delta > best_behind) {
                best_behind = delta;
                obs.has_behind = true;
                obs.behind = at;
            }
        }
    }

    // Boost pads for this vehicle and weapon balloons in the object list, the
    // nearest one in front of the racer. Behavior ids: BHV_WEAPON_BALLOON 17,
    // BHV_ZIPPER_WATER 57, BHV_ZIPPER_GROUND 82, BHV_ZIPPER_AIR 93
    // (decomp include/object_behaviors.h).
    const auto objects = g.get(a.object_list);
    const auto total = std::min<std::uint32_t>(g.get(a.object_count), 512);
    if (total == 0 || !g.range(objects, total * 4))
        return;
    const double phi = (int(std::uint16_t(obs.heading)) + 0x8000) * 2.0 * std::numbers::pi / 65536.0;
    const double fx = std::sin(phi), fz = std::cos(phi);
    double best_pad = 1e30, best_balloon = 1e30;
    for (std::uint32_t i = 0; i < total; ++i) {
        const auto o = g.get(objects + i * 4);
        if (!g.range(o, 0x68))
            continue;
        const auto behavior = g.get(o + 0x48, 2);
        const bool pad = obs.vehicle == 2   ? behavior == 93
                         : obs.vehicle == 1 ? behavior == 82 || behavior == 57
                                            : obs.vehicle == 0 && behavior == 82;
        if (!pad && behavior != 17)
            continue;
        const CheckpointPoint at{g.real(o + 0x0C), g.real(o + 0x10), g.real(o + 0x14)};
        if (!sane(at.x) || !sane(at.y) || !sane(at.z))
            continue;
        const double dx = double(at.x) - obs.position.x, dz = double(at.z) - obs.position.z;
        const double dist = std::hypot(dx, dz);
        if (dx * fx + dz * fz <= 0 || std::fabs(double(at.y) - obs.position.y) > 150)
            continue;
        if (pad && dist < 600 && dist < best_pad) {
            best_pad = dist;
            obs.has_zipper = true;
            obs.zipper = at;
        } else if (!pad && dist < 700 && dist < best_balloon) {
            best_balloon = dist;
            obs.has_balloon = true;
            obs.balloon = at;
        }
    }
}
} // namespace

// Guest layouts (decomp include/structs.h and src/objects.h), offsets only:
//   Object:          trans.x/y/z_position 0x0C/0x10/0x14, behaviorId 0x48 (s16, 1 = racer), Object_Racer* 0x64
//   Object_Racer:    playerIndex 0x000, nextCheckpoint 0x192 (s8), steerVisualRotation 0x1A0 (s16),
//                    isOnAlternateRoute 0x1C8, vehicleID 0x1D6; checkpoint_distance 0x0A8 (f32)
//                    plus velocity 0x2C (f32), balloon_type/quantity/level 0x172/0x173/0x174, lap 0x193,
//                    boostTimer 0x1D3, raceFinished 0x1D8, groundedWheels 0x1E2, zipperDirCorrection 0x1F5;
//                    Object x_rotation 0x02. Globals: gRaceStartTimer, gNumRacers, gObjPtrList/gObjectCount,
//                    gAssetsMisc* (balloon table).
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
        if (ok)
            read_extras(g, a, obs, port, racers, object, data, count);
    }
    return view;
}
} // namespace dkr::runtime::netplay::experimental
