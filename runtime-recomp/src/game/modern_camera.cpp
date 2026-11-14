#include "recomp.h"
#include "camera_obstruction_guest.h"
#include "game_payload.hpp"
#include "modern_camera.hpp"
#include "modern_camera_dkr.hpp"
#include "presentation_identity.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace {

namespace dkr_cam = dkr::runtime::modern_camera;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kPorts = 4U;
constexpr std::uint32_t kCameraSize = 0x44U;
constexpr std::uint32_t kPlayerCameras = 4U;
constexpr auto kOwnershipWindow = std::chrono::milliseconds(300);
constexpr int kCameraModePlane = 1;

// Object_Racer members (identical in both revisions).
constexpr std::uint32_t kRacerBoostTimer = 0x1D3U;
constexpr std::uint32_t kRacerSteerAngle = 0x1E1U;
constexpr std::uint32_t kRacerGroundedWheels = 0x1E2U;
constexpr std::uint32_t kRacerWaterTimer = 0x1E5U;
constexpr std::uint32_t kRacerDriftDirection = 0x1E6U;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_look_back_enabled{true};

struct PortInput {
    std::atomic<float> x{0.0F};
    std::atomic<float> y{0.0F};
    std::atomic<bool> look_back{false};
};
std::array<PortInput, kPorts> g_input{};

// Mouse orbit, from the SDL event thread: pixels moved since the last step.
struct MouseOrbit {
    std::atomic<float> dx{0.0F};
    std::atomic<float> dy{0.0F};
    std::atomic<bool> held{false};
};
std::array<MouseOrbit, kPorts> g_mouse{};

void accumulate(std::atomic<float>& total, float amount) {
    float current = total.load(std::memory_order_relaxed);
    while (!total.compare_exchange_weak(current, current + amount, std::memory_order_relaxed)) {
    }
}
std::atomic<bool> g_online{false};
std::atomic<std::uint64_t> g_reset_generation{1U};
std::array<std::atomic<Clock::rep>, kPorts> g_owned_until{};
std::array<std::atomic<bool>, kPlayerCameras> g_cut{};

// Written by the Camera tuning panel, read by every step.
std::mutex g_tuning_mutex;
dkr_cam::Tuning g_tuning = dkr_cam::default_tuning();

struct Published {
    dkr_cam::Readout readout{};
    Clock::time_point at{};
};
constexpr auto kReadoutWindow = std::chrono::milliseconds(500);
std::mutex g_readout_mutex;
std::array<Published, kPorts> g_readouts{};

// Everything below is only touched by the recompiled game thread.
struct Player {
    bool active = false;
    std::uint32_t header = 0U;
    std::uint32_t camera = 0U;
    std::uint64_t frame = 0U;
    MCState state{};
    dkr_cam_vec last_position{};
    dkr_cam_vec velocity{};
    bool look_back = false;
    dkr_cam::Pose pose{};
    std::int16_t roll = 0;
    float fov = 0.0F;
    unsigned trace_ticks = 0U;
    dkr_cam::BumpGuard bump{};
    std::uint32_t hits = 0U; // seen since this camera started, for the panel
    bool view_cut = false;   // look back toggled since the last step
};
std::array<Player, kPorts> g_players{};

// One-player FOV: DKR has a single perspective matrix shared by every view.
struct Fov {
    bool owned = false;
    float level = 0.0F;
    std::uint32_t header = 0U;
};
Fov g_fov{};

// The Camera members one Modern camera frame may change: its pose, and the
// boom length and segment the second clearance pass may rewrite.
struct Fields {
    std::uint32_t x = 0U, y = 0U, z = 0U, boom = 0U;
    std::int16_t yaw = 0, pitch = 0, roll = 0, segment = 0;
};
struct Saved {
    bool valid = false;
    std::uint32_t camera = 0U;
    Fields retail{};
    Fields written{};
};
std::array<Saved, kPlayerCameras> g_saved{};
bool g_pending = false;
std::uint32_t g_pending_camera = 0U;
Fields g_pending_retail{};
std::uint64_t g_guest_generation = 0U;

void reset_guest_state_if_needed() {
    const auto generation = g_reset_generation.load(std::memory_order_acquire);
    if (generation == g_guest_generation)
        return;
    g_guest_generation = generation;
    g_players = {};
    g_saved = {};
    g_fov = {};
    g_pending = false;
    g_pending_camera = 0U;
    g_pending_retail = {};
}

bool camera_slot(std::uint32_t camera, std::uint32_t& slot) {
    const std::uint32_t base = dkr::runtime::revision_addresses::Cameras;
    if (camera < base || (camera - base) % kCameraSize != 0U)
        return false;
    slot = (camera - base) / kCameraSize;
    return slot < kPlayerCameras && dkr_cam_span(camera, kCameraSize, 4);
}

Fields read_fields(std::uint8_t* rdram, std::uint32_t camera) {
    Fields fields;
    fields.x = dkr_cam_word(rdram, camera + 0x0CU);
    fields.y = dkr_cam_word(rdram, camera + 0x10U);
    fields.z = dkr_cam_word(rdram, camera + 0x14U);
    fields.boom = dkr_cam_word(rdram, camera + 0x1CU);
    fields.yaw = MEM_H(0x00, dkr_cam_addr(camera));
    fields.pitch = MEM_H(0x02, dkr_cam_addr(camera));
    fields.roll = MEM_H(0x04, dkr_cam_addr(camera));
    fields.segment = MEM_H(0x34, dkr_cam_addr(camera));
    return fields;
}

float read_float(std::uint8_t* rdram, std::uint32_t address) {
    return dkr_cam_float(rdram, address);
}

void set_fov(std::uint8_t* rdram, recomp_context* context, float fov) {
    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->cam_set_fov == nullptr || context == nullptr)
        return;
    recomp_context call = *context;
    call.f12.fl = fov;
    payload->cam_set_fov(rdram, &call);
}

void release_fov(std::uint8_t* rdram, recomp_context* context) {
    if (!g_fov.owned)
        return;
    g_fov.owned = false;
    set_fov(rdram, context, g_fov.level);
}

// The level segment the eye is in, by retail trackGetBlock's rule (inside a
// box in XZ, nearest box centre in Y), as the clearance pass resolves it.
int segment_at(std::uint8_t* rdram, std::uint32_t model, dkr_cam_vec eye) {
    const std::uint32_t boxes = dkr_cam_word(rdram, model + 8U);
    const unsigned count = static_cast<unsigned>(MEM_H(0x1A, dkr_cam_addr(model)));
    if (!count || count > 512U || !dkr_cam_span(boxes, count * 12U, 2))
        return -1;
    int segment = -1;
    double nearest = 1000000.0;
    const int x = static_cast<int>(eye.x), y = static_cast<int>(eye.y), z = static_cast<int>(eye.z);
    for (unsigned s = 0U; s < count; ++s) {
        const std::uint32_t box = boxes + s * 12U;
        const dkr_cam_vec lo = dkr_cam_vertex(rdram, box), hi = dkr_cam_vertex(rdram, box + 6U);
        if (x > lo.x && x < hi.x && z > lo.z && z < hi.z) {
            const double distance = std::fabs(y - ((static_cast<int>(lo.y) + static_cast<int>(hi.y)) >> 1));
            if (distance < nearest) {
                nearest = distance;
                segment = static_cast<int>(s);
            }
        }
    }
    return segment;
}

// DKR_MODERN_CAMERA_TRACE=1 logs the measured inputs twice a second, for
// calibrating the unit scale against real racer speeds. =2 logs every step
// with what moves the camera without steering (shake, dip, roll), to find
// where a jolt comes from.
// The Camera tuning panel's step log is level 2 too.
std::atomic<bool> g_step_log{false};

int trace_level() {
    static const int level = [] {
        const char* value = std::getenv("DKR_MODERN_CAMERA_TRACE");
        return value == nullptr ? 0 : std::max(std::atoi(value), 1);
    }();
    return g_step_log.load(std::memory_order_relaxed) ? 2 : level;
}

double degrees(double radians) {
    return radians * (180.0 / dkr_cam::kPi);
}

void publish_readout(std::size_t port, const Player& player, const MCInput& in, float nose, const MCOutput& out,
                     dkr_cam_vec eye, dkr_cam_vec position) {
    dkr_cam::Readout readout;
    readout.live = true;
    readout.port = port;
    const double speed = std::hypot(player.velocity.x, player.velocity.z);
    readout.speed = static_cast<float>(speed / dkr_cam::kUnitsPerMetre);
    readout.lag = static_cast<float>(degrees(std::remainder(nose - out.yaw, 2.0 * dkr_cam::kPi)));
    if (speed > dkr_cam::kUnitsPerMetre) {
        const double motion = std::atan2(player.velocity.x, player.velocity.z);
        readout.slip = static_cast<float>(std::fabs(degrees(std::remainder(motion - nose, 2.0 * dkr_cam::kPi))));
    }
    readout.roll = static_cast<float>(degrees(out.roll));
    readout.fov = player.fov;
    readout.arm = static_cast<float>(std::hypot(eye.x - position.x, eye.y - position.y, eye.z - position.z) /
                                     dkr_cam::kUnitsPerMetre);
    readout.trauma = player.state.trauma;
    readout.clearance = player.state.collisionDist;
    readout.grounded = in.grounded != 0;
    readout.drifting = in.drifting != 0;
    readout.boosting = in.boost > 0.0F;
    readout.look_back = in.lookBack != 0;
    readout.steadying = player.bump.blend > 0.0F;
    readout.hits = player.hits;
    std::scoped_lock lock(g_readout_mutex);
    g_readouts[port] = Published{readout, Clock::now()};
}

// The retail clearance pass sweeps from this far above the racer.
constexpr double kClearanceHeight = 24.0;
// A sweep this close to its start began touching a wall.
constexpr double kStartContact = 0.001;
constexpr double kThinRay = 1.0;

struct Raycast {
    std::uint8_t* rdram = nullptr;
    std::uint32_t model = 0U;
    double radius = 0.0;
    dkr_cam_vec origin{}; // the racer, at the clearance height
};

// The fraction of origin -> target a sphere sweeps clear; negative when the
// level geometry is unusable.
double sweep(const Raycast& ray, dkr_cam_vec target, double radius) {
    unsigned budget = 8192U;
    double fraction = 1.0;
    dkr_cam_vec push{};
    if (!dkr_cam_query(ray.rdram, ray.model, ray.origin, target, radius, 1, &budget, &fraction, &push)) {
        return -1.0;
    }
    return fraction;
}

// modern_cam's clearance probe, the bounded level-geometry sweep the retail
// clearance pass uses. Like that pass it starts from the racer, not from
// modern_cam's pivot, which lags the racer and can end up inside a wall
// after a crash. A sweep that starts already touching a wall (driving along
// one) hits at once and would fold the boom into the racer until it moved
// off, so it is checked again with a thin ray: the boom only shortens for a
// wall really in between, and the clearance pass that follows keeps the eye
// out of the one it brushes. Malformed geometry counts as clear.
int raycast(void* user, MCVec3 from, MCVec3 to, float, float* hit) {
    const Raycast& ray = *static_cast<const Raycast*>(user);
    const dkr_cam_vec target = dkr_cam_v(to.x, to.y, to.z);
    double fraction = sweep(ray, target, ray.radius);
    if (fraction >= 0.0 && fraction < kStartContact)
        fraction = sweep(ray, target, kThinRay);
    if (!(fraction >= 0.0 && fraction < 1.0))
        return 0;
    // modern_cam shortens its boom from the pivot: stop it as far short of
    // the desired eye as the hit is.
    const dkr_cam_vec stop = dkr_cam_add(ray.origin, dkr_cam_scale(dkr_cam_sub(target, ray.origin), fraction));
    const dkr_cam_vec boom = dkr_cam_sub(target, dkr_cam_v(from.x, from.y, from.z));
    const double length = std::sqrt(dkr_cam_dot(boom, boom));
    if (!(length > 1.0))
        return 0;
    const dkr_cam_vec short_by = dkr_cam_sub(target, stop);
    *hit = static_cast<float>(std::clamp(1.0 - std::sqrt(dkr_cam_dot(short_by, short_by)) / length, 0.0, 1.0));
    return 1;
}

struct Scene {
    std::uint32_t camera = 0U;
    std::uint32_t header = 0U;
    std::uint32_t layout = 0U;
    std::uint32_t model = 0U;
    std::uint32_t slot = 0U;
    int mode = 0;
};

// The same scene gate as dkr_cam_resolve: a gameplay follow camera for a
// local player, with no cutscene or dialogue camera in charge.
bool follow_camera(std::uint8_t* rdram, std::uint32_t object, const std::uint32_t fields[8], Scene& scene) {
    if (!dkr_cam_span(object, 0x68, 4) || dkr_cam_word(rdram, fields[0]) != 0U || MEM_BU(0, dkr_cam_addr(fields[3])) ||
        MEM_H(0, dkr_cam_addr(fields[6]))) {
        return false;
    }
    scene.header = dkr_cam_word(rdram, fields[1]);
    scene.layout = dkr_cam_word(rdram, fields[2]);
    scene.model = dkr_cam_word(rdram, fields[4]);
    scene.camera = dkr_cam_word(rdram, fields[5]);
    if (!dkr_cam_span(scene.header, 0xA0, 4) || !dkr_cam_span(scene.model, 0x40, 4) ||
        !camera_slot(scene.camera, scene.slot) ||
        !dkr_world_projection_eligible(0, MEM_BU(0x4C, dkr_cam_addr(scene.header)), scene.layout, 1)) {
        return false;
    }
    scene.mode = MEM_H(0x36, dkr_cam_addr(scene.camera));
    return dkr_cam_follow_mode(scene.mode);
}

void write_pose(std::uint8_t* rdram, std::uint32_t camera, const dkr_cam::Pose& pose, std::int16_t roll) {
    dkr_cam_store(rdram, camera + 0x0CU, pose.x);
    dkr_cam_store(rdram, camera + 0x10U, pose.y);
    dkr_cam_store(rdram, camera + 0x14U, pose.z);
    MEM_H(0x00, dkr_cam_addr(camera)) = pose.yaw;
    MEM_H(0x02, dkr_cam_addr(camera)) = pose.pitch;
    MEM_H(0x04, dkr_cam_addr(camera)) = roll;
}

// One modern_cam step for this tick. Returns false if the inputs are unusable.
bool step_modern(std::uint8_t* rdram, recomp_context* context, std::uint32_t object, std::uint32_t racer,
                 const Scene& scene, Player& player, std::size_t port, bool look_back) {
    const std::uint32_t sp = static_cast<std::uint32_t>(context->r29);
    // update_player_camera keeps its float updateRateF argument at sp+0x40:
    // the number of 60 Hz frames this tick advances.
    float rate = dkr_cam_span(sp, 0x44, 4) ? read_float(rdram, sp + 0x40U) : 1.0F;
    if (!std::isfinite(rate) || rate <= 0.0F || rate > 10.0F)
        rate = 1.0F;
    const float dt = rate / 60.0F;

    const dkr_cam_vec position = dkr_cam_position(rdram, object);
    if (!dkr_cam_finite(position))
        return false;
    if (player.state.initialized) {
        player.velocity = dkr_cam_scale(dkr_cam_sub(position, player.last_position), 1.0 / dt);
    }
    player.last_position = position;

    const dkr_cam::Tuning tuning = dkr_cam::tuning();
    MCParams params = dkr_cam::layout_params(tuning, scene.layout);
    const PortInput& input = g_input[port];
    MCInput in{};
    in.pos = MCVec3{static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
    const float nose = static_cast<float>(dkr_cam::heading_from_racer_yaw(MEM_H(0x00, dkr_cam_addr(object))));
    const dkr_cam::HitTiming hit_timing = dkr_cam::hit_timing(tuning.hitReaction);
    const dkr_cam::HitInput steady =
        dkr_cam::hit_steady_input(MCVec3{static_cast<float>(player.velocity.x), static_cast<float>(player.velocity.y),
                                         static_cast<float>(player.velocity.z)},
                                  nose, dt, player.bump, hit_timing);
    in.yaw = steady.heading;
    in.vel = steady.velocity;
    if (player.bump.hit)
        ++player.hits;
    in.slopePitch = static_cast<float>(dkr_cam::slope_from_racer_pitch(MEM_H(0x02, dkr_cam_addr(object))));
    in.grounded = scene.mode == kCameraModePlane ||
                  static_cast<std::int8_t>(MEM_B(kRacerGroundedWheels, dkr_cam_addr(racer))) > 0 ||
                  static_cast<std::int8_t>(MEM_B(kRacerWaterTimer, dkr_cam_addr(racer))) > 0;
    in.drifting = static_cast<std::int8_t>(MEM_B(kRacerDriftDirection, dkr_cam_addr(racer))) != 0;
    in.boost = static_cast<std::int8_t>(MEM_B(kRacerBoostTimer, dkr_cam_addr(racer))) > 0 ? 1.0F : 0.0F;
    // Right on the stick turns the view right: towards lower modern_cam yaw.
    in.stickX = -input.x.load(std::memory_order_relaxed);
    in.stickY = -input.y.load(std::memory_order_relaxed);
    in.lookBack = look_back ? 1 : 0;
    in.steer = dkr_cam::steer_from_racer(static_cast<std::int8_t>(MEM_B(kRacerSteerAngle, dkr_cam_addr(racer))));

    // Retail hits and explosions shake the camera; feed them in as trauma.
    const float shake = std::fabs(read_float(rdram, scene.camera + 0x30U));
    if (std::isfinite(shake) && player.state.initialized) {
        player.state.trauma = std::max(player.state.trauma, std::min(shake / 12.0F, 0.5F) * tuning.retailShake);
    }

    Raycast ray{rdram, scene.model, dkr_cam_authored_radius(scene.layout, MEM_BU(0x9C, dkr_cam_addr(scene.header))),
                dkr_cam_add(position, dkr_cam_v(0.0, kClearanceHeight, 0.0))};
    MCOutput out{};
    const float trauma_before = player.state.trauma;
    // While a hit is being steadied the yaw spring is not to keep up with
    // the nose, and a hop off a wall is not a landing: no dip, no shake.
    params = dkr_cam::hit_step_params(params, player.bump);
    if (player.state.initialized && player.bump.blend > 0.0F)
        player.state.prevGrounded = in.grounded;
    const bool landing = player.state.initialized && in.grounded && !player.state.prevGrounded;
    // modern_cam starts over on its first step and after a teleport.
    const MCVec3 anchor = player.state.anchor;
    const bool reset =
        !player.state.initialized || std::hypot(in.pos.x - anchor.x, in.pos.y - anchor.y, in.pos.z - anchor.z) >
                                         params.teleportDistance * params.unitScale;
    // Mouse orbit. Right turns the view right (lower modern_cam yaw) and up
    // looks up (lower orbit pitch), as the right stick does. Moving the mouse,
    // or holding its right button, keeps the camera where it was put; it
    // recentres once the mouse has been left alone for recenterDelay.
    const float mouse_x = g_mouse[port].dx.exchange(0.0F, std::memory_order_relaxed);
    const float mouse_y = g_mouse[port].dy.exchange(0.0F, std::memory_order_relaxed);
    const bool mouse_moved = mouse_x != 0.0F || mouse_y != 0.0F;
    const bool mouse_held = g_mouse[port].held.load(std::memory_order_relaxed);
    if (player.state.initialized) {
        const float per_pixel = tuning.mouseOrbit * static_cast<float>(dkr_cam::kPi / 180.0) / 100.0F;
        if (mouse_moved) {
            player.state.orbitYaw =
                static_cast<float>(std::remainder(player.state.orbitYaw - mouse_x * per_pixel, 2.0 * dkr_cam::kPi));
            player.state.orbitPitch = std::clamp(player.state.orbitPitch + mouse_y * per_pixel, -0.5F, 0.8F);
        }
        if (mouse_moved || mouse_held)
            player.state.idleTime = 0.0F;
    }
    params.recenterSharpness = dkr_cam::eased_recenter_sharpness(params.recenterSharpness, player.state.idleTime + dt,
                                                                 params.recenterDelay, tuning.recenterEase);
    ModernCam_Update(&player.state, &params, &in, dt, raycast, &ray, &out);
    if (!std::isfinite(out.eye.x) || !std::isfinite(out.eye.y) || !std::isfinite(out.eye.z) ||
        !std::isfinite(out.yaw) || !std::isfinite(out.pitch) || !std::isfinite(out.roll) ||
        !std::isfinite(out.fovDeg)) {
        return false;
    }
    player.view_cut = false;
    const dkr_cam_vec eye = dkr_cam_v(out.eye.x, out.eye.y, out.eye.z);
    player.pose.x = eye.x;
    player.pose.y = eye.y;
    player.pose.z = eye.z;
    player.pose.yaw = dkr_cam::camera_yaw_from_view(out.yaw);
    player.pose.pitch = dkr_cam::camera_pitch_from_view(out.pitch);
    player.roll = dkr_cam::to_binary_angle(out.roll);
    player.fov = dkr_cam::effective_fov(g_fov.level, out.fovDeg, params);
    publish_readout(port, player, in, nose, out, eye, position);
    if (trace_level() >= 2) {
        std::fprintf(stderr,
                     "[modern-camera][step] port=%zu dt=%.4f speed=%.0f vy=%.0f grounded=%d landing=%d "
                     "retail-shake=%.2f trauma=%.3f->%.3f dip=%.1f kick=%.1f roll=%.2f nose=%.3f heading=%.3f "
                     "view=%.3f pitch=%.3f eye=(%.0f,%.0f,%.0f) racer=(%.0f,%.0f,%.0f) clear=%.2f "
                     "bump-hold=%.2f bump-blend=%.2f hit=%d recovered=%.2f drift=%d boost=%.0f fov=%.1f\n",
                     port, dt, std::hypot(player.velocity.x, player.velocity.z), player.velocity.y, in.grounded,
                     landing ? 1 : 0, shake, trauma_before, player.state.trauma, player.state.dipY, player.state.kick,
                     degrees(out.roll), nose, in.yaw, out.yaw, out.pitch, eye.x, eye.y, eye.z, position.x, position.y,
                     position.z, player.state.collisionDist, player.bump.hold, player.bump.blend,
                     player.bump.hit ? 1 : 0, player.bump.recovered, in.drifting, in.boost, player.fov);
    } else if (trace_level() == 1 && (++player.trace_ticks % 15U) == 0U) {
        const double speed = std::hypot(player.velocity.x, player.velocity.z);
        const double motion = speed > 1.0 ? std::atan2(player.velocity.x, player.velocity.z) : 0.0;
        std::fprintf(stderr,
                     "[modern-camera] port=%zu rate=%.2f speed=%.0fu/s heading=%.3f motion=%.3f "
                     "steer=%.2f grounded=%d drift=%d boost=%.0f fov=%.1f\n",
                     port, rate, speed, in.yaw, motion, in.steer, in.grounded, in.drifting, in.boost, player.fov);
    }
    return true;
}

} // namespace

bool dkr::runtime::modern_camera::requested() {
    return g_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::modern_camera::enabled() {
    return dkr::runtime::enhancements::modern_presentation_enabled() && requested();
}

void dkr::runtime::modern_camera::set_enabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_release);
}

bool dkr::runtime::modern_camera::look_back_requested() {
    return g_look_back_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::modern_camera::look_back_enabled() {
    return dkr::runtime::enhancements::modern_presentation_enabled() && look_back_requested();
}

void dkr::runtime::modern_camera::set_look_back_enabled(bool enabled) {
    g_look_back_enabled.store(enabled, std::memory_order_release);
}

dkr::runtime::modern_camera::Tuning dkr::runtime::modern_camera::tuning() {
    std::scoped_lock lock(g_tuning_mutex);
    return g_tuning;
}

void dkr::runtime::modern_camera::set_tuning(const Tuning& tuning) {
    std::scoped_lock lock(g_tuning_mutex);
    g_tuning = tuning;
}

bool dkr::runtime::modern_camera::step_log() {
    return g_step_log.load(std::memory_order_relaxed);
}

void dkr::runtime::modern_camera::set_step_log(bool enabled) {
    g_step_log.store(enabled, std::memory_order_relaxed);
}

dkr::runtime::modern_camera::Readout dkr::runtime::modern_camera::readout() {
    const Clock::time_point now = Clock::now();
    std::scoped_lock lock(g_readout_mutex);
    for (const Published& published : g_readouts) {
        if (published.readout.live && now - published.at < kReadoutWindow)
            return published.readout;
    }
    return Readout{};
}

void dkr::runtime::modern_camera::publish_input(std::size_t port, float camera_x, float camera_y, bool look_back,
                                                bool online) {
    if (g_online.load(std::memory_order_acquire) != online)
        set_online_routing(online);
    if (port >= kPorts)
        return;
    g_input[port].x.store(camera_x, std::memory_order_relaxed);
    g_input[port].y.store(camera_y, std::memory_order_relaxed);
    g_input[port].look_back.store(look_back, std::memory_order_relaxed);
}

void dkr::runtime::modern_camera::reset_session() {
    for (auto& owned : g_owned_until)
        owned.store(0, std::memory_order_release);
    for (auto& cut : g_cut)
        cut.store(false, std::memory_order_release);
    for (auto& mouse : g_mouse) {
        mouse.dx.store(0.0F, std::memory_order_relaxed);
        mouse.dy.store(0.0F, std::memory_order_relaxed);
        mouse.held.store(false, std::memory_order_relaxed);
    }
    g_reset_generation.fetch_add(1U, std::memory_order_release);
}

void dkr::runtime::modern_camera::set_online_routing(bool online) {
    if (g_online.exchange(online, std::memory_order_acq_rel) != online)
        reset_session();
}

bool dkr::runtime::modern_camera::mouse_orbit_available(std::size_t port) {
    return owns_controls(port) && tuning().mouseOrbit > 0.0F;
}

void dkr::runtime::modern_camera::add_mouse_motion(std::size_t port, float dx, float dy) {
    if (port >= kPorts)
        return;
    accumulate(g_mouse[port].dx, dx);
    accumulate(g_mouse[port].dy, dy);
}

void dkr::runtime::modern_camera::set_mouse_orbit_held(std::size_t port, bool held) {
    if (port < kPorts)
        g_mouse[port].held.store(held, std::memory_order_relaxed);
}

bool dkr::runtime::modern_camera::owns_controls(std::size_t port) {
    return port < kPorts && !g_online.load(std::memory_order_acquire) && enabled() &&
           Clock::now().time_since_epoch().count() < g_owned_until[port].load(std::memory_order_acquire);
}

bool dkr::runtime::modern_camera::begin(std::uint8_t* rdram, recomp_context* context, std::uint32_t object,
                                        const std::uint32_t fields[8]) {
    g_pending = false;
    reset_guest_state_if_needed();
    if (g_online.load(std::memory_order_acquire))
        return false;
    const bool modern = enabled();
    const bool look_back_allowed = look_back_enabled();
    if (!modern && !look_back_allowed) {
        release_fov(rdram, context);
        return false;
    }
    const std::uint32_t player_index = dkr_cam_word(rdram, fields[7]);
    if (player_index > 3U)
        return false;
    // sPlayerID: the controller port driving this player (Adventure Two swaps).
    const std::uint32_t port =
        MEM_BU(static_cast<std::int32_t>(player_index), dkr_cam_addr(dkr::runtime::revision_addresses::PlayerIdMap));
    if (port >= kPorts)
        return false;
    Player& player = g_players[port];

    Scene scene;
    const std::uint32_t sp = static_cast<std::uint32_t>(context->r29);
    const std::uint32_t racer = dkr_cam_span(sp, 0x44, 4) ? dkr_cam_word(rdram, sp + 0x3CU) : 0U;
    if (!follow_camera(rdram, object, fields, scene) || !dkr_cam_span(racer, 0x1F0, 4)) {
        // Finish, loop and door cameras are retail; start fresh afterwards.
        player.active = false;
        if (scene.layout == 0U)
            release_fov(rdram, context);
        return false;
    }
    if (!player.active || player.header != scene.header || player.camera != scene.camera) {
        player = Player{};
        player.active = true;
        player.header = scene.header;
        player.camera = scene.camera;
        // A new level has just set its own FOV; within a level, ours may be
        // the one in place, so keep the level's.
        if (scene.layout == 0U && (!g_fov.owned || g_fov.header != scene.header)) {
            g_fov.level = read_float(rdram, dkr::runtime::revision_addresses::CurrentCameraFov);
            g_fov.header = scene.header;
            g_fov.owned = false;
        }
    }

    const bool look_back = look_back_allowed && g_input[port].look_back.load(std::memory_order_relaxed);
    if (look_back != player.look_back) {
        player.look_back = look_back;
        player.view_cut = true;
        // A half turn in one tick is a cut, not something to interpolate.
        g_cut[scene.slot].store(true, std::memory_order_release);
    }

    const Fields retail = read_fields(rdram, scene.camera);
    if (modern) {
        // update_player_camera runs twice in a tick when the camera mode
        // changes; step modern_cam once and reuse that pose.
        const std::uint64_t frame = dkr::runtime::presentation::authored_frame_sequence();
        if (!player.state.initialized || frame != player.frame) {
            if (!step_modern(rdram, context, object, racer, scene, player, port, look_back)) {
                player.active = false;
                return false;
            }
            player.frame = frame;
        }
        write_pose(rdram, scene.camera, player.pose, player.roll);
        if (scene.layout == 0U && std::isfinite(g_fov.level) && g_fov.level > 0.0F) {
            const float current = read_float(rdram, dkr::runtime::revision_addresses::CurrentCameraFov);
            if (!(std::fabs(current - player.fov) < 0.01F))
                set_fov(rdram, context, player.fov);
            g_fov.owned = true;
        }
        g_owned_until[port].store((Clock::now() + kOwnershipWindow).time_since_epoch().count(),
                                  std::memory_order_release);
    } else {
        release_fov(rdram, context);
        if (!look_back)
            return false;
        dkr_cam::Pose camera;
        const dkr_cam_vec position = dkr_cam_position(rdram, scene.camera);
        const dkr_cam_vec pivot = dkr_cam_position(rdram, object);
        if (!dkr_cam_finite(position) || !dkr_cam_finite(pivot))
            return false;
        camera.x = position.x;
        camera.y = position.y;
        camera.z = position.z;
        camera.yaw = retail.yaw;
        camera.pitch = retail.pitch;
        write_pose(rdram, scene.camera, dkr_cam::turn_around(camera, pivot.x, pivot.z, dkr_cam::kPi), retail.roll);
    }
    g_pending = true;
    g_pending_camera = scene.camera;
    g_pending_retail = retail;
    return true;
}

void dkr::runtime::modern_camera::commit(std::uint8_t* rdram, const std::uint32_t fields[8]) {
    reset_guest_state_if_needed();
    if (g_online.load(std::memory_order_acquire))
        return;
    if (!g_pending)
        return;
    g_pending = false;
    const std::uint32_t camera = dkr_cam_word(rdram, fields[5]);
    const std::uint32_t model = dkr_cam_word(rdram, fields[4]);
    std::uint32_t slot = 0U;
    if (camera != g_pending_camera || !camera_slot(camera, slot))
        return;
    // Rendering and BSP selection start from the segment the moved eye is in.
    // The clearance pass only rewrites it when it had to move the eye.
    if (dkr_cam_span(model, 0x40, 4)) {
        const int segment = segment_at(rdram, model, dkr_cam_position(rdram, camera));
        if (segment >= 0)
            MEM_H(0x34, dkr_cam_addr(camera)) = static_cast<std::int16_t>(segment);
    }
    g_saved[slot] = Saved{true, camera, g_pending_retail, read_fields(rdram, camera)};
}

void dkr::runtime::modern_camera::restore(std::uint8_t* rdram, const std::uint32_t fields[8]) {
    reset_guest_state_if_needed();
    if (g_online.load(std::memory_order_acquire))
        return;
    const std::uint32_t camera = dkr_cam_word(rdram, fields[5]);
    std::uint32_t slot = 0U;
    if (!camera_slot(camera, slot))
        return;
    Saved& saved = g_saved[slot];
    if (!saved.valid || saved.camera != camera)
        return;
    saved.valid = false;
    // Hand the retail update its own previous pose. A member something else
    // changed since the Modern camera wrote it (respawn, door, reset) is kept.
    const Fields now = read_fields(rdram, camera);
    const gpr base = dkr_cam_addr(camera);
    if (now.x == saved.written.x)
        MEM_W(0x0C, base) = static_cast<std::int32_t>(saved.retail.x);
    if (now.y == saved.written.y)
        MEM_W(0x10, base) = static_cast<std::int32_t>(saved.retail.y);
    if (now.z == saved.written.z)
        MEM_W(0x14, base) = static_cast<std::int32_t>(saved.retail.z);
    if (now.boom == saved.written.boom)
        MEM_W(0x1C, base) = static_cast<std::int32_t>(saved.retail.boom);
    if (now.yaw == saved.written.yaw)
        MEM_H(0x00, base) = saved.retail.yaw;
    if (now.pitch == saved.written.pitch)
        MEM_H(0x02, base) = saved.retail.pitch;
    if (now.roll == saved.written.roll)
        MEM_H(0x04, base) = saved.retail.roll;
    if (now.segment == saved.written.segment)
        MEM_H(0x34, base) = saved.retail.segment;
}

bool dkr::runtime::modern_camera::consume_cut(std::uint32_t camera_slot) {
    return !g_online.load(std::memory_order_acquire) && camera_slot < kPlayerCameras &&
           g_cut[camera_slot].exchange(false, std::memory_order_acq_rel);
}
