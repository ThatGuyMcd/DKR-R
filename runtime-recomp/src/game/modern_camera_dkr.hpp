#pragma once

#include "modern_cam/modern_cam.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

// DKR side of the Modern camera: unit scale, angle conventions and the
// Bancada ModernCam tuning. Pure functions only, so they can be tested
// without the runtime.
namespace dkr::runtime::modern_camera {

inline constexpr double kPi = 3.14159265358979323846;

// DKR has no metric scale. A retail one-player follow camera rests ~192 units
// behind the racer and ~50 above it, which is the Bancada framing
// (5.9 m back, 1.35 m up) at roughly 32 units per metre.
inline constexpr float kUnitsPerMetre = 32.0F;
// Joystick range DKR steers with (JOYSTICK_MAX_RANGE).
inline constexpr float kSteerRange = 70.0F;
// Retail pulls split-screen cameras in: 2P 120 and 3/4P 100 units vs 1P 192.
inline constexpr float kTwoPlayerFraming = 0.625F;
inline constexpr float kMultiPlayerFraming = 0.52F;
// cam_set_fov ignores anything at or above CAMERA_MAX_FOV (90).
inline constexpr float kMaximumFov = 89.5F;
inline constexpr float kMinimumFov = 1.0F;

constexpr std::int16_t to_binary_angle(double radians) {
    // 0x10000 is a full turn; wrap through uint16 so +pi and -pi agree.
    const double turns = radians / (2.0 * kPi);
    const double units = (turns - static_cast<double>(static_cast<std::int64_t>(turns))) * 65536.0;
    const std::int64_t rounded = static_cast<std::int64_t>(units < 0.0 ? units - 0.5 : units + 0.5);
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(rounded & 0xFFFF));
}

constexpr double from_binary_angle(std::int16_t angle) {
    return static_cast<double>(angle) * (kPi / 32768.0);
}

constexpr std::int16_t add_angles(std::int16_t a, std::int16_t b) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(a) + static_cast<std::uint16_t>(b)));
}

// A DKR follow camera with yaw c sits at pivot + d*(sin c, -cos c) and looks
// along (-sin c, cos c); modern_cam looks along (sin m, cos m). So m = -c.
// The retail car camera uses c = 0x8000 - racer yaw to sit behind the nose,
// so the racer's nose in modern_cam terms is (racer yaw - 0x8000).
constexpr double heading_from_racer_yaw(std::int16_t racer_yaw) {
    return from_binary_angle(add_angles(racer_yaw, static_cast<std::int16_t>(-0x8000)));
}

constexpr std::int16_t camera_yaw_from_view(double view_yaw) {
    return to_binary_angle(-view_yaw);
}

// DKR's camera x_rotation looks DOWN when positive; modern_cam pitch looks up.
constexpr std::int16_t camera_pitch_from_view(double view_pitch) {
    return to_binary_angle(-view_pitch);
}

// Retail raises its camera target when the racer's x_rotation is positive,
// i.e. a positive racer pitch is nose-up, which is modern_cam's uphill.
constexpr double slope_from_racer_pitch(std::int16_t racer_pitch) {
    return from_binary_angle(racer_pitch);
}

// DKR's stick X is positive to the right, and turning right lowers
// modern_cam's yaw, whose steer input is positive towards higher yaw.
inline float steer_from_racer(std::int8_t steer_angle) {
    return std::clamp(-static_cast<float>(steer_angle) / kSteerRange, -1.0F, 1.0F);
}

inline float layout_framing(std::uint32_t layout) {
    return layout == 0U ? 1.0F : layout == 1U ? kTwoPlayerFraming : kMultiPlayerFraming;
}

// What the in-game Camera tuning panel edits and modern-camera.ini stores.
// params is in metres with one-player framing; layout_params scales it.
struct Tuning {
    MCParams params{};
    // Retail hits and explosions shake the camera; this scales how much of
    // that reaches modern_cam as trauma.
    float retailShake = 1.0F;
    // Orbiting with the mouse, in degrees per 100 pixels of mouse travel.
    // 0 turns it off.
    float mouseOrbit = 30.0F;
    // Seconds over which recentring builds up to full speed once the orbit
    // has been left alone for recenterDelay. 0 starts it at full speed.
    float recenterEase = 0.8F;
    // How much of a wall or kart hit reaches the camera, in percent. Below
    // 100 the camera keeps following the motion it had, so its aim, zoom
    // and FOV do not react to the stop or the bounce, until the racer has
    // its speed back; meanwhile a knocked nose turns on screen instead of
    // turning the world. The lower, the longer it waits for a racer that
    // stays slow and the softer the nose follows (hit_timing). 100 is raw
    // modern_cam.
    float hitReaction = 25.0F;
};

// modern-camera.ini format. 1 (no version line) is the first release, whose
// yaw spring lagged in every corner and whose position spring trailed the
// racer by metres at speed; parse_tuning migrates such a file.
inline constexpr int kTuningVersion = 2;

// What DKR-R ships. Started as the Bancada ModernCam export (framing,
// stiffness, the impulses); the direction values are the comfort pass of
// 2026-10: the camera keeps up in corners instead of lagging and leading,
// stays level, and breathes less with speed.
inline Tuning default_tuning() {
    Tuning tuning;
    MCParams& p = tuning.params;
    ModernCam_DefaultParams(&p);
    p.distMin = 5.9F;
    // The retail camera sits 252 units back at speed. The first release had
    // 4.8 m plus ~3 m of position-spring lag; the lag is compensated now.
    p.distMax = 8.2F;
    p.heightMin = 1.35F;
    // A drift shows less of the kart's side and the alignment moves at the
    // pace of a camera operator, so a drift ending does not swing the view.
    p.driftAlign = 0.45F;
    p.alignSharpness = 3.0F;
    p.yawFreq = 2.3F;
    p.yawDamping = 1.0F;
    p.lookAhead = 0.12F;
    p.lookAheadSharpness = 3.0F;
    p.fovSpeed = 5.0F;
    p.fovBoost = 5.0F;
    p.rollFromYawRate = 0.0F;
    p.rollMax = 0.0F;
    p.boostTrauma = 0.35F;
    p.landingTrauma = 0.81F;
    p.steerLead = 0.2007F;
    p.enablePosSpring = 1;
    p.posFreq = 2.3F;
    p.posDamping = 0.98F;
    p.enableLandingDip = 1;
    p.landingDip = 1.65F;
    p.dipFreq = 5.6F;
    p.dipDamping = 0.67F;
    p.enableBoostKick = 1;
    p.boostKick = 1.8F;
    p.kickFreq = 2.3F;
    p.kickDamping = 0.5F;
    p.rotationalShake = 1;
    p.unitScale = kUnitsPerMetre;
    return tuning;
}

// What a version 1 modern-camera.ini was written against: the Bancada
// ModernCam export on top of modern_cam's old defaults. A value equal to
// this in such a file was never touched by the player.
inline Tuning legacy_default_tuning() {
    Tuning tuning;
    MCParams& p = tuning.params;
    ModernCam_DefaultParams(&p);
    p.distMin = 5.9F;
    p.heightMin = 1.35F;
    p.yawFreq = 2.3F;
    p.boostTrauma = 0.35F;
    p.landingTrauma = 0.81F;
    p.enableSteerLead = 1;
    p.steerLead = 0.2007F;
    p.enablePosSpring = 1;
    p.posFreq = 2.3F;
    p.posDamping = 0.98F;
    p.enableLandingDip = 1;
    p.landingDip = 1.65F;
    p.dipFreq = 5.6F;
    p.dipDamping = 0.67F;
    p.enableBoostKick = 1;
    p.boostKick = 1.8F;
    p.kickFreq = 2.3F;
    p.kickDamping = 0.5F;
    p.rotationalShake = 1;
    // Version 1 had no keep-up and no alignment filter.
    p.yawKeepUp = 0.0F;
    p.posKeepUp = 0.0F;
    p.alignSharpness = 100.0F;
    p.unitScale = kUnitsPerMetre;
    return tuning;
}

// A tuning in DKR units for a screen layout.
inline MCParams layout_params(const Tuning& tuning, std::uint32_t layout) {
    MCParams p = tuning.params;
    p.unitScale = kUnitsPerMetre;
    const float framing = layout_framing(layout);
    p.distMin *= framing;
    p.distMax *= framing;
    p.heightMin *= framing;
    p.heightMax *= framing;
    return p;
}

inline MCParams dkr_params(std::uint32_t layout) {
    return layout_params(default_tuning(), layout);
}

// ------------------------------------------------------------------ tuning

enum class TuningGroup : std::uint8_t {
    Framing,
    Direction,
    YawSpring,
    Follow,
    Terrain,
    LookAhead,
    Fov,
    Roll,
    Orbit,
    Collision,
    Shake,
    Hits,
    SteerLead,
    PosSpring,
    LandingDip,
    BoostKick,
    ShakeStyle,
};

// One tunable value. The key is the MCParams member name (and the
// modern-camera.ini key); minimum..maximum is the panel's range, which a
// loaded file is clamped to. Degree values are stored in radians.
struct TuningField {
    const char* key;
    float MCParams::*member; // nullptr: the Tuning value `extra`
    TuningGroup group;
    const char* label;
    float minimum;
    float maximum;
    const char* unit;
    int decimals;
    bool degrees = false;
    float Tuning::*extra = nullptr;
};

// The Bancada's ranges. fovBase is left out: DKR keeps each level's FOV and
// only adds modern_cam's change, so the base is not a setting. So is
// collisionRadius: the clearance probe uses the level's authored radius.
inline constexpr std::array<TuningField, 60> kTuningFields{{
    {"distMin", &MCParams::distMin, TuningGroup::Framing, "Distance at rest", 1.0F, 10.0F, "m", 2},
    {"distMax", &MCParams::distMax, TuningGroup::Framing, "Distance at top speed", 1.0F, 12.0F, "m", 2},
    {"heightMin", &MCParams::heightMin, TuningGroup::Framing, "Height at rest", 0.2F, 4.0F, "m", 2},
    {"heightMax", &MCParams::heightMax, TuningGroup::Framing, "Height at top speed", 0.2F, 5.0F, "m", 2},
    {"targetHeight", &MCParams::targetHeight, TuningGroup::Framing, "Aim point height", 0.0F, 2.5F, "m", 2},
    {"speedForMax", &MCParams::speedForMax, TuningGroup::Framing, "Speed for full zoom", 5.0F, 40.0F, "m/s", 1},
    {"zoomSharpness", &MCParams::zoomSharpness, TuningGroup::Framing, "Zoom speed", 0.2F, 8.0F, "/s", 1},
    {"velocityAlign", &MCParams::velocityAlign, TuningGroup::Direction, "Follows the motion", 0.0F, 1.0F, "", 2},
    {"driftAlign", &MCParams::driftAlign, TuningGroup::Direction, "Follows the motion in a drift", 0.0F, 1.0F, "", 2},
    {"driftLead", &MCParams::driftLead, TuningGroup::Direction, "Turns into the drift", -25.0F, 25.0F, "\xC2\xB0", 1,
     true},
    {"spinFollowSpeed", &MCParams::spinFollowSpeed, TuningGroup::Direction, "In a spin, follows the motion above", 0.0F,
     20.0F, "m/s", 1},
    {"minAlignSpeed", &MCParams::minAlignSpeed, TuningGroup::Direction, "Minimum speed to align", 0.0F, 5.0F, "m/s", 1},
    {"alignSharpness", &MCParams::alignSharpness, TuningGroup::Direction, "Speed of the alignment", 0.5F, 15.0F, "/s",
     1},
    {"yawFreq", &MCParams::yawFreq, TuningGroup::YawSpring, "Stiffness", 0.2F, 6.0F, "Hz", 2},
    {"yawFreqSpeedBoost", &MCParams::yawFreqSpeedBoost, TuningGroup::YawSpring, "Extra stiffness at top speed", 0.0F,
     3.0F, "Hz", 2},
    {"yawDamping", &MCParams::yawDamping, TuningGroup::YawSpring, "Damping (below 1 sways)", 0.2F, 1.5F, "", 2},
    {"yawKeepUp", &MCParams::yawKeepUp, TuningGroup::YawSpring, "Keeps up in a steady corner (0 lags)", 0.0F, 1.0F, "",
     2},
    {"posSharpness", &MCParams::posSharpness, TuningGroup::Follow, "Horizontal", 1.0F, 40.0F, "/s", 1},
    {"heightSharpness", &MCParams::heightSharpness, TuningGroup::Follow, "Vertical on the ground", 0.5F, 30.0F, "/s",
     1},
    {"airHeightSharpness", &MCParams::airHeightSharpness, TuningGroup::Follow, "Vertical in the air", 0.2F, 20.0F, "/s",
     1},
    {"maxVerticalLag", &MCParams::maxVerticalLag, TuningGroup::Follow, "Maximum vertical lag", 0.2F, 6.0F, "m", 1},
    {"slopeFollow", &MCParams::slopeFollow, TuningGroup::Terrain, "Follows the slope", 0.0F, 1.0F, "", 2},
    {"slopeSharpness", &MCParams::slopeSharpness, TuningGroup::Terrain, "Speed", 0.5F, 15.0F, "/s", 1},
    {"lookAhead", &MCParams::lookAhead, TuningGroup::LookAhead, "Anticipates the motion", 0.0F, 0.6F, "s", 2},
    {"lookAheadMax", &MCParams::lookAheadMax, TuningGroup::LookAhead, "Limit", 0.0F, 8.0F, "m", 1},
    {"lookAheadSharpness", &MCParams::lookAheadSharpness, TuningGroup::LookAhead, "Smoothness", 0.5F, 20.0F, "/s", 1},
    {"fovSpeed", &MCParams::fovSpeed, TuningGroup::Fov, "Extra at top speed", 0.0F, 30.0F, "\xC2\xB0", 1},
    {"fovSharpness", &MCParams::fovSharpness, TuningGroup::Fov, "Speed", 0.5F, 10.0F, "/s", 1},
    {"fovBoost", &MCParams::fovBoost, TuningGroup::Fov, "Extra on a boost", 0.0F, 25.0F, "\xC2\xB0", 1},
    {"boostFovAttack", &MCParams::boostFovAttack, TuningGroup::Fov, "Boost: opening", 0.5F, 30.0F, "/s", 1},
    {"boostFovRelease", &MCParams::boostFovRelease, TuningGroup::Fov, "Boost: return", 0.2F, 10.0F, "/s", 1},
    {"rollFromYawRate", &MCParams::rollFromYawRate, TuningGroup::Roll, "Roll per turn (negative inverts)", -0.1F, 0.1F,
     "", 3},
    {"rollMax", &MCParams::rollMax, TuningGroup::Roll, "Maximum roll", 0.0F, 12.0F, "\xC2\xB0", 2, true},
    {"rollSharpness", &MCParams::rollSharpness, TuningGroup::Roll, "Speed", 0.5F, 15.0F, "/s", 1},
    {"orbitSpeedYaw", &MCParams::orbitSpeedYaw, TuningGroup::Orbit, "Horizontal speed", 0.5F, 6.0F, "rad/s", 1},
    {"orbitSpeedPitch", &MCParams::orbitSpeedPitch, TuningGroup::Orbit, "Vertical speed", 0.2F, 4.0F, "rad/s", 1},
    {"recenterDelay", &MCParams::recenterDelay, TuningGroup::Orbit, "Wait before recentring", 0.0F, 5.0F, "s", 1},
    {"recenterSharpness", &MCParams::recenterSharpness, TuningGroup::Orbit, "Recentring speed", 0.2F, 10.0F, "/s", 1},
    {"recenterEase", nullptr, TuningGroup::Orbit, "Recentring builds up over", 0.0F, 3.0F, "s", 1, false,
     &Tuning::recenterEase},
    {"mouseOrbit", nullptr, TuningGroup::Orbit, "Mouse, 0 = off", 0.0F, 90.0F, "\xC2\xB0/100 px", 0, false,
     &Tuning::mouseOrbit},
    {"collisionReleaseSharpness", &MCParams::collisionReleaseSharpness, TuningGroup::Collision, "Return after a wall",
     0.5F, 20.0F, "/s", 1},
    {"traumaDecay", &MCParams::traumaDecay, TuningGroup::Shake, "Decay", 0.2F, 5.0F, "/s", 1},
    {"boostTrauma", &MCParams::boostTrauma, TuningGroup::Shake, "On a boost", 0.0F, 1.0F, "", 2},
    {"landingTrauma", &MCParams::landingTrauma, TuningGroup::Shake, "On landing", 0.0F, 1.0F, "", 2},
    {"landingSpeedForMax", &MCParams::landingSpeedForMax, TuningGroup::Shake, "Fall speed for full shake", 2.0F, 25.0F,
     "m/s", 1},
    {"retailShake", nullptr, TuningGroup::Shake, "Game hits and explosions", 0.0F, 2.0F, "x", 2, false,
     &Tuning::retailShake},
    {"shakeFreq", &MCParams::shakeFreq, TuningGroup::Shake, "Frequency", 2.0F, 40.0F, "Hz", 1},
    {"shakeMaxAngle", &MCParams::shakeMaxAngle, TuningGroup::Shake, "Maximum angle", 0.0F, 8.0F, "\xC2\xB0", 2, true},
    {"shakeMaxOffset", &MCParams::shakeMaxOffset, TuningGroup::Shake, "Maximum offset", 0.0F, 0.5F, "m", 2},
    {"hitReaction", nullptr, TuningGroup::Hits, "Reaction to hits (0 = calm, 100 = raw)", 0.0F, 100.0F, "%", 0, false,
     &Tuning::hitReaction},
    {"steerLead", &MCParams::steerLead, TuningGroup::SteerLead, "Lead angle", 0.0F, 30.0F, "\xC2\xB0", 1, true},
    {"posFreq", &MCParams::posFreq, TuningGroup::PosSpring, "Stiffness", 0.5F, 8.0F, "Hz", 2},
    {"posDamping", &MCParams::posDamping, TuningGroup::PosSpring, "Damping", 0.2F, 1.5F, "", 2},
    {"posKeepUp", &MCParams::posKeepUp, TuningGroup::PosSpring, "Keeps up at speed (0 trails behind)", 0.0F, 1.0F, "",
     2},
    {"landingDip", &MCParams::landingDip, TuningGroup::LandingDip, "Depth", 0.0F, 2.0F, "m", 2},
    {"dipFreq", &MCParams::dipFreq, TuningGroup::LandingDip, "Stiffness", 0.5F, 8.0F, "Hz", 1},
    {"dipDamping", &MCParams::dipDamping, TuningGroup::LandingDip, "Damping", 0.1F, 1.2F, "", 2},
    {"boostKick", &MCParams::boostKick, TuningGroup::BoostKick, "Kick distance", 0.0F, 4.0F, "m", 2},
    {"kickFreq", &MCParams::kickFreq, TuningGroup::BoostKick, "Stiffness", 0.5F, 6.0F, "Hz", 1},
    {"kickDamping", &MCParams::kickDamping, TuningGroup::BoostKick, "Damping", 0.1F, 1.2F, "", 2},
}};

// The optional modern_cam behaviours. Their values are the fields of the
// same group.
struct TuningFeature {
    const char* key;
    int MCParams::*member;
    TuningGroup group;
    const char* label;
    const char* help;
};

inline constexpr std::array<TuningFeature, 5> kTuningFeatures{{
    {"enableSteerLead", &MCParams::enableSteerLead, TuningGroup::SteerLead, "Steering lead",
     "The camera starts turning when you steer, before the kart turns."},
    {"enablePosSpring", &MCParams::enablePosSpring, TuningGroup::PosSpring, "Position spring",
     "A spring instead of the plain follow: inertia when braking and accelerating."},
    {"enableLandingDip", &MCParams::enableLandingDip, TuningGroup::LandingDip, "Landing dip",
     "The camera dips and springs back when the kart lands."},
    {"enableBoostKick", &MCParams::enableBoostKick, TuningGroup::BoostKick, "Boost kick",
     "The boom stretches for a moment when a boost starts."},
    {"rotationalShake", &MCParams::rotationalShake, TuningGroup::ShakeStyle, "Rotation-only shake",
     "Layered noise that turns the view without moving the eye."},
}};

inline float& field_value(Tuning& tuning, const TuningField& field) {
    return field.member != nullptr ? tuning.params.*field.member : tuning.*field.extra;
}

inline float field_value(const Tuning& tuning, const TuningField& field) {
    return field.member != nullptr ? tuning.params.*field.member : tuning.*field.extra;
}

inline bool same_tuning(const Tuning& a, const Tuning& b) {
    bool same = true;
    for (const TuningField& field : kTuningFields) {
        same = same && field_value(a, field) == field_value(b, field);
    }
    for (const TuningFeature& feature : kTuningFeatures) {
        same = same && (a.params.*feature.member != 0) == (b.params.*feature.member != 0);
    }
    return same;
}

// Shortest decimal that reads back as the same float.
inline std::string format_float(float value) {
    char text[32];
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));
        if (std::strtof(text, nullptr) == value)
            break;
    }
    return text;
}

// modern-camera.ini: every value, one key=value per line.
inline std::string serialize_tuning(const Tuning& tuning) {
    std::string text = "# DKR-R Modern camera tuning. Edit it in game with F7.\n";
    text += "version=" + std::to_string(kTuningVersion) + '\n';
    for (const TuningField& field : kTuningFields) {
        text += field.key;
        text += '=';
        text += format_float(field_value(tuning, field));
        text += '\n';
    }
    for (const TuningFeature& feature : kTuningFeatures) {
        text += feature.key;
        text += tuning.params.*feature.member != 0 ? "=1\n" : "=0\n";
    }
    return text;
}

// The lag a version 1 position spring (or plain follow) had at speedForMax,
// in metres: with the lag compensated, that much of the eye's distance at
// speed has to come from distMax instead.
inline float legacy_position_lag(const MCParams& p) {
    const float lag_per_speed = p.enablePosSpring != 0
                                    ? 2.0F * p.posDamping / (2.0F * static_cast<float>(kPi) * std::max(p.posFreq, 0.1F))
                                    : 1.0F / std::max(p.posSharpness, 0.1F);
    return lag_per_speed * p.speedForMax;
}

// A version 1 tuning brought up to date. A value the player never changed
// follows the new default (version 1's steering lead, roll, damping, FOV
// breathing and look-ahead were the module's defaults, not choices); a
// changed value is kept, except that a changed distMax grows by the lag the
// old position spring added at speed, so the framing at speed stays as it
// was tuned.
inline Tuning migrate_legacy_tuning(const Tuning& legacy) {
    const Tuning old_defaults = legacy_default_tuning();
    const Tuning new_defaults = default_tuning();
    Tuning tuning = legacy;
    const bool custom_distance = legacy.params.distMax != old_defaults.params.distMax;
    const float lag = legacy_position_lag(legacy.params);
    for (const TuningField& field : kTuningFields) {
        if (field_value(legacy, field) == field_value(old_defaults, field)) {
            field_value(tuning, field) = field_value(new_defaults, field);
        }
    }
    for (const TuningFeature& feature : kTuningFeatures) {
        if (legacy.params.*feature.member == old_defaults.params.*feature.member) {
            tuning.params.*feature.member = new_defaults.params.*feature.member;
        }
    }
    if (custom_distance) {
        for (const TuningField& field : kTuningFields) {
            if (std::string_view(field.key) == "distMax") {
                field_value(tuning, field) = std::clamp(legacy.params.distMax + lag, field.minimum, field.maximum);
            }
        }
    }
    return tuning;
}

// The default tuning with every recognised line of text applied. Unknown
// keys and unreadable values are skipped; values are clamped to their range.
// A file without a version line is a version 1 file and is migrated;
// `migrated`, when given, says whether that happened.
inline Tuning parse_tuning(std::string_view text, bool* migrated = nullptr) {
    int version = 1;
    for (std::string_view rest = text; !rest.empty();) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.remove_suffix(1);
        if (line.substr(0, 8) == "version=")
            version = std::atoi(std::string(line.substr(8)).c_str());
    }
    const bool legacy = version < kTuningVersion;
    if (migrated != nullptr)
        *migrated = legacy;
    Tuning tuning = legacy ? legacy_default_tuning() : default_tuning();
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.remove_suffix(1);
        const std::size_t equals = line.find('=');
        if (line.empty() || line.front() == '#' || equals == std::string_view::npos)
            continue;
        const std::string_view key = line.substr(0, equals);
        const std::string value(line.substr(equals + 1));
        char* parsed_end = nullptr;
        const float number = std::strtof(value.c_str(), &parsed_end);
        if (parsed_end == value.c_str() || !std::isfinite(number))
            continue;
        for (const TuningField& field : kTuningFields) {
            if (key != field.key)
                continue;
            const float scale = field.degrees ? static_cast<float>(kPi / 180.0) : 1.0F;
            field_value(tuning, field) = std::clamp(number, field.minimum * scale, field.maximum * scale);
        }
        for (const TuningFeature& feature : kTuningFeatures) {
            if (key == feature.key)
                tuning.params.*feature.member = number != 0.0F ? 1 : 0;
        }
    }
    return legacy ? migrate_legacy_tuning(tuning) : tuning;
}

// A C++ block for default_tuning(): the values that differ from
// ModernCam_DefaultParams, in the same style.
inline std::string tuning_code(const Tuning& tuning) {
    MCParams base;
    ModernCam_DefaultParams(&base);
    std::string text = "// Modern camera tuning from the in-game panel (metres).\n";
    const auto number = [](float value) {
        std::string literal = format_float(value);
        if (literal.find_first_of(".e") == std::string::npos)
            literal += ".0";
        return literal + "F";
    };
    const Tuning shipped{};
    for (const TuningField& field : kTuningFields) {
        const float value = field_value(tuning, field);
        if (field.member == nullptr) {
            if (value != shipped.*field.extra) {
                text += std::string("tuning.") + field.key + " = " + number(value) + ";\n";
            }
            continue;
        }
        if (value == base.*field.member)
            continue;
        text += std::string("p.") + field.key + " = " + number(value) + ";";
        if (field.degrees) {
            char degrees[32];
            std::snprintf(degrees, sizeof(degrees), " // %.1f degrees", value * 180.0 / kPi);
            text += degrees;
        }
        text += '\n';
    }
    for (const TuningFeature& feature : kTuningFeatures) {
        const int value = tuning.params.*feature.member != 0 ? 1 : 0;
        if (value != base.*feature.member)
            text += std::string("p.") + feature.key + " = " + std::to_string(value) + ";\n";
    }
    return text;
}

// ---------------------------------------------------------------- presets

// No roll, shake, FOV pumping, dip or kick, and a firm yaw that does not
// sway: for players the default makes motion sick.
inline Tuning comfort_tuning() {
    Tuning tuning = default_tuning();
    MCParams& p = tuning.params;
    p.distMax = p.distMin;
    p.heightMax = p.heightMin;
    p.velocityAlign = 0.15F;
    p.driftAlign = 0.25F;
    p.driftLead = 0.0F;
    p.yawFreq = 2.8F;
    p.yawFreqSpeedBoost = 0.4F;
    p.yawDamping = 1.0F;
    p.slopeFollow = 0.3F;
    p.lookAhead = 0.1F;
    p.fovSpeed = 2.0F;
    p.fovBoost = 0.0F;
    p.rollFromYawRate = 0.0F;
    p.rollMax = 0.0F;
    p.boostTrauma = 0.0F;
    p.landingTrauma = 0.0F;
    tuning.retailShake = 0.0F;
    p.enableSteerLead = 0;
    p.enablePosSpring = 0;
    p.enableLandingDip = 0;
    p.enableBoostKick = 0;
    return tuning;
}

// Locked behind the racer at the retail framing, to compare against.
inline Tuning rigid_tuning() {
    Tuning tuning = default_tuning();
    MCParams& p = tuning.params;
    p.yawFreq = 6.0F;
    p.yawFreqSpeedBoost = 0.0F;
    p.yawDamping = 1.0F;
    p.velocityAlign = 0.0F;
    p.driftAlign = 0.0F;
    p.posSharpness = 40.0F;
    p.heightSharpness = 30.0F;
    p.lookAhead = 0.0F;
    p.fovSpeed = 0.0F;
    p.fovBoost = 0.0F;
    p.rollFromYawRate = 0.0F;
    p.rollMax = 0.0F;
    p.distMax = p.distMin;
    p.heightMax = p.heightMin;
    p.slopeFollow = 0.0F;
    p.boostTrauma = 0.0F;
    p.landingTrauma = 0.0F;
    p.enableSteerLead = 0;
    p.enablePosSpring = 0;
    p.enableLandingDip = 0;
    p.enableBoostKick = 0;
    p.rotationalShake = 0;
    return tuning;
}

inline Tuning kart_tuning() {
    Tuning tuning = default_tuning();
    MCParams& p = tuning.params;
    p.velocityAlign = 0.1F;
    p.driftAlign = 0.0F;
    p.driftLead = 0.21F;
    p.yawFreq = 1.6F;
    p.yawDamping = 1.0F;
    p.lookAhead = 0.1F;
    p.fovSpeed = 6.0F;
    p.enablePosSpring = 0;
    return tuning;
}

inline Tuning cinematic_tuning() {
    Tuning tuning = default_tuning();
    MCParams& p = tuning.params;
    p.yawFreq = 0.6F;
    p.yawFreqSpeedBoost = 0.2F;
    p.yawDamping = 0.65F;
    p.yawKeepUp = 0.4F;
    p.distMax = 6.5F;
    p.heightMax = 1.8F;
    p.lookAhead = 0.3F;
    p.fovSpeed = 16.0F;
    p.rollFromYawRate = 0.05F;
    p.rollMax = 0.09F;
    p.posFreq = 1.6F;
    p.posDamping = 0.6F;
    p.posKeepUp = 0.0F;
    return tuning;
}

// modern_cam.c's own defaults at DKR scale.
inline Tuning module_tuning() {
    Tuning tuning;
    ModernCam_DefaultParams(&tuning.params);
    tuning.params.unitScale = kUnitsPerMetre;
    return tuning;
}

struct TuningPreset {
    const char* name;
    const char* help;
    Tuning (*make)();
};

inline constexpr std::array<TuningPreset, 6> kTuningPresets{{
    {"DKR-R", "The tuning DKR-R ships (the Bancada ModernCam export).", default_tuning},
    {"Comfort",
     "Steady for motion-sensitive players: no roll, shake, FOV pumping, landing dip or "
     "boost kick, and a firm camera that does not sway.",
     comfort_tuning},
    {"Rigid N64", "Locked behind the racer at the retail framing, to compare against.", rigid_tuning},
    {"Kart style", "Turns into the corner in a drift.", kart_tuning},
    {"Cinematic", "Looser, with more sway and roll.", cinematic_tuning},
    {"Module C", "modern_cam.c's own defaults at DKR scale.", module_tuning},
}};

// A wall or an object bounces the racer back or sideways for a few ticks, or
// stops it dead. A bounce turns its motion faster than driving, drifting or
// spinning ever turns it (in a spin the nose turns, not the motion); a stop
// takes its speed away faster than any brake. modern_cam reads either as a
// change of direction: a bounce looks like a spin and it whips round to face
// it; a stop drops the motion it was following mid-drift, out to the side,
// and it swings to the nose. So on a hit the camera keeps the velocity from
// just before it, in the nose's frame (it still turns with the nose, and its
// speed-dependent framing does not jump), then eases back to the measured
// velocity. The hit also knocks the nose round in a tick or two; meanwhile
// the camera follows the nose through a lag, so the racer turns on screen
// instead of the whole world turning with it.
inline constexpr float kBumpSpeed = 96.0F;       // 3 m/s: slower motion is too noisy to read
inline constexpr float kBumpTurnRate = 10.5F;    // rad/s (600 degrees/s) of motion turn
inline constexpr float kBumpMinimumTurn = 0.26F; // rad (15 degrees) in one step
inline constexpr float kBumpHold = 0.4F;         // seconds held after the last hit
inline constexpr float kBumpRelease = 0.8F;      // seconds to ease back afterwards
// A stop: within kStopWindow the speed falls by kStopDrop (6 m/s) and by
// kStopFraction of itself. In traced hub driving, hits lost 200-460 units/s
// in a tick or two; everything else stayed under 70 a tick.
inline constexpr float kStopDrop = 192.0F;
inline constexpr float kStopFraction = 0.35F;
inline constexpr float kStopWindow = 0.1F;

// How a hit is softened. Tuning::hitReaction 0 is the calmest: the velocity
// is kept for a second and eased back over three if the racer stays slow,
// and the nose is followed through a 0.4 s lag. 100 is none of it. Whatever
// the timing, the hold ends by itself as the racer regains the speed it had
// (BumpGuard::recovered), so driving off after a hit shows no transition.
struct HitTiming {
    float hold = 0.0F;        // seconds the velocity is kept
    float release = 0.0F;     // seconds to ease it back afterwards
    float heading_lag = 0.0F; // seconds of lag on the nose while held
};

inline constexpr float kHitHoldMax = 1.0F;
inline constexpr float kHitReleaseMax = 3.0F;
inline constexpr float kHitHeadingLagMax = 0.4F;

inline HitTiming hit_timing(float reaction_percent) {
    const float calm = 1.0F - std::clamp(reaction_percent / 100.0F, 0.0F, 1.0F);
    return HitTiming{kHitHoldMax * calm, kHitReleaseMax * calm, kHitHeadingLagMax * calm};
}

// A recent velocity, in the nose's frame.
struct BumpSample {
    float speed = 0.0F;
    float forward = 0.0F;
    float side = 0.0F;
    float age = 0.0F;
};

struct BumpGuard {
    float motion_x = 0.0F; // last motion direction on the ground, or zero
    float motion_z = 0.0F;
    std::array<BumpSample, 8> recent{}; // the last steps' velocities, newest first
    std::size_t count = 0U;
    float held_forward = 0.0F;
    float held_side = 0.0F;
    float hold = 0.0F;
    float timeline = 0.0F;  // 1 while held, easing to 0 over the release
    float recovered = 0.0F; // 0..1, how much of the held velocity the racer has regained
    float blend = 0.0F;     // what is applied: timeline * (1 - recovered)
    bool hit = false;       // this step was a hit
    bool started = false;   // heading is valid
    float heading = 0.0F;   // the nose as given to modern_cam
};

// What modern_cam is given for this step.
struct HitInput {
    MCVec3 velocity{};
    float heading = 0.0F;
};

inline HitInput hit_steady_input(MCVec3 velocity, float heading, float dt, BumpGuard& guard, const HitTiming& timing) {
    const float flat = std::hypot(velocity.x, velocity.z);
    bool bounced = false;
    if (flat > kBumpSpeed) {
        const float x = velocity.x / flat, z = velocity.z / flat;
        if (guard.motion_x != 0.0F || guard.motion_z != 0.0F) {
            const float turn =
                std::fabs(std::atan2(guard.motion_x * z - guard.motion_z * x, guard.motion_x * x + guard.motion_z * z));
            bounced = turn > std::max(kBumpMinimumTurn, kBumpTurnRate * dt);
        }
        guard.motion_x = x;
        guard.motion_z = z;
    } else {
        guard.motion_x = guard.motion_z = 0.0F;
    }
    // The fastest velocity of the last kStopWindow: what a hit interrupted.
    const BumpSample* fastest = nullptr;
    for (std::size_t i = 0U; i < guard.count; ++i) {
        guard.recent[i].age += dt;
        if (guard.recent[i].age > kStopWindow + 1e-4F) {
            guard.count = i;
            break;
        }
        if (fastest == nullptr || guard.recent[i].speed > fastest->speed)
            fastest = &guard.recent[i];
    }
    const bool stopped =
        fastest != nullptr && fastest->speed - flat > kStopDrop && flat < fastest->speed * (1.0F - kStopFraction);
    const float nose_x = std::sin(heading), nose_z = std::cos(heading);
    const float forward = velocity.x * nose_x + velocity.z * nose_z;
    const float side = velocity.x * nose_z - velocity.z * nose_x;
    guard.hit = bounced || stopped;
    if (timing.hold <= 0.0F && timing.release <= 0.0F) {
        guard.hold = 0.0F;
        guard.timeline = 0.0F;
        guard.recovered = 0.0F;
    } else if (guard.hit) {
        // What the hit interrupted. A hit while an earlier one is still
        // easing out starts from the mix given so far, not from the earlier
        // velocity, which went stale as the racer turned.
        const float f = fastest != nullptr ? fastest->forward : forward;
        const float s = fastest != nullptr ? fastest->side : side;
        guard.held_forward = f + (guard.held_forward - f) * guard.blend;
        guard.held_side = s + (guard.held_side - s) * guard.blend;
        guard.hold = timing.hold;
        guard.timeline = 1.0F;
        guard.recovered = 0.0F;
    } else if (guard.hold > 0.0F) {
        guard.hold = std::max(guard.hold - dt, 0.0F);
    } else {
        guard.timeline = timing.release > 0.0F ? std::max(guard.timeline - dt / timing.release, 0.0F) : 0.0F;
    }
    // The hold is over as soon as the racer has the held velocity back: what
    // it has regained along it, in the nose's frame, and never less than
    // before, so braking after a partial recovery does not bring the hold
    // back. Below it the mix eases in step with the recovery, and there is
    // nothing to ease out later.
    if (guard.timeline > 0.0F && !guard.hit) {
        const float held = guard.held_forward * guard.held_forward + guard.held_side * guard.held_side;
        if (held > 1.0F) {
            const float progress = (forward * guard.held_forward + side * guard.held_side) / held;
            guard.recovered = std::max(guard.recovered, std::clamp(progress, 0.0F, 1.0F));
        }
        if (guard.recovered >= 0.98F) {
            guard.hold = 0.0F;
            guard.timeline = 0.0F;
        }
    }
    guard.blend = guard.timeline * (1.0F - guard.recovered);
    if (guard.hit) {
        guard.count = 0U; // so the same stop is not read again
    } else {
        guard.count = std::min(guard.count + 1U, guard.recent.size());
        for (std::size_t i = guard.count - 1U; i > 0U; --i)
            guard.recent[i] = guard.recent[i - 1U];
        guard.recent[0] = BumpSample{flat, forward, side, 0.0F};
    }
    // The nose, through a lag that shrinks to nothing as the hit eases out.
    if (!guard.started || guard.blend <= 0.0F || timing.heading_lag <= 0.0F) {
        guard.heading = heading;
    } else {
        const double lag = static_cast<double>(timing.heading_lag) * guard.blend;
        guard.heading = static_cast<float>(std::remainder(
            guard.heading + std::remainder(heading - guard.heading, 2.0 * kPi) * (1.0 - std::exp(-dt / lag)),
            2.0 * kPi));
    }
    guard.started = true;
    if (guard.blend <= 0.0F)
        return HitInput{velocity, guard.heading};
    // Mixed in the frame of the nose modern_cam is given.
    const float given_x = std::sin(guard.heading), given_z = std::cos(guard.heading);
    const float measured_forward = velocity.x * given_x + velocity.z * given_z;
    const float measured_side = velocity.x * given_z - velocity.z * given_x;
    const float f = measured_forward + (guard.held_forward - measured_forward) * guard.blend;
    const float s = measured_side + (guard.held_side - measured_side) * guard.blend;
    return HitInput{MCVec3{f * given_x + s * given_z, velocity.y, f * given_z - s * given_x}, guard.heading};
}

// The velocity part alone, with no lag on the nose.
inline MCVec3 bump_steady_velocity(MCVec3 velocity, float heading, float dt, BumpGuard& guard,
                                   float hold_time = kBumpHold, float release_time = kBumpRelease) {
    return hit_steady_input(velocity, heading, dt, guard, HitTiming{hold_time, release_time, 0.0F}).velocity;
}

// The tuning for one step of a hit. While the held velocity is in use the
// yaw spring must not keep up with the nose (that is the point: a knocked
// nose turns on screen, not the world), so its keep-up fades with the hold
// and comes back as the racer regains its speed. The position spring keeps
// following the racer: the bounce moves the camera with the racer, as the
// retail camera does, instead of making the racer leap at the camera and
// the camera back away again.
inline MCParams hit_step_params(const MCParams& params, const BumpGuard& guard) {
    MCParams p = params;
    p.yawKeepUp *= 1.0F - std::clamp(guard.blend, 0.0F, 1.0F);
    return p;
}

// modern_cam starts recentring at full speed the moment the orbit has been
// idle for recenterDelay. This builds the speed up over `ease` seconds
// instead, so the camera drifts back rather than snapping into motion.
// idle is the orbit's idle time at the end of this step.
inline float eased_recenter_sharpness(float sharpness, float idle, float delay, float ease) {
    if (ease <= 0.0F)
        return sharpness;
    const float t = std::clamp((idle - delay) / ease, 0.0F, 1.0F);
    return sharpness * t * t * (3.0F - 2.0F * t);
}

// The level's FOV plus whatever modern_cam adds for speed and boost.
inline float effective_fov(float level_fov, float modern_fov, const MCParams& p) {
    return std::clamp(level_fov + (modern_fov - p.fovBase), kMinimumFov, kMaximumFov);
}

struct Pose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::int16_t yaw = 0;   // Camera trans.rotation.y_rotation
    std::int16_t pitch = 0; // Camera trans.rotation.x_rotation, + looks down
};

// Look back with the original camera: a rigid half turn of the whole camera
// around the vertical axis through the racer, so the racer stays framed.
// Adding to a follow camera's yaw turns its offset as below.
inline Pose turn_around(const Pose& camera, double pivot_x, double pivot_z, double yaw_offset) {
    const double ox = camera.x - pivot_x;
    const double oz = camera.z - pivot_z;
    const double c = std::cos(yaw_offset);
    const double s = std::sin(yaw_offset);
    Pose result = camera;
    result.x = pivot_x + ox * c - oz * s;
    result.z = pivot_z + ox * s + oz * c;
    result.yaw = add_angles(camera.yaw, to_binary_angle(yaw_offset));
    return result;
}

} // namespace dkr::runtime::modern_camera
