#pragma once

#include "modern_camera_dkr.hpp"

#include <cstddef>
#include <cstdint>

struct recomp_context;

// Modern camera: replaces the player follow camera (car, hovercraft, plane)
// with modern_cam, the right-stick orbit and look-back included. It is local
// presentation: the retail camera state is put back at the start of the next
// update_player_camera, so the game's own camera keeps running underneath.
// Look back also works with the original camera. Off while netplay routes
// input.
namespace dkr::runtime::modern_camera {

// Settings, persisted by runtime_ui. Each is effective only in the Modern
// presentation profile.
bool requested();
bool enabled();
void set_enabled(bool enabled);
bool look_back_requested();
bool look_back_enabled();
void set_look_back_enabled(bool enabled);

// The tuning every Modern camera step uses (the Camera tuning panel edits it
// live; runtime_ui persists it in modern-camera.ini). Safe from any thread.
Tuning tuning();
void set_tuning(const Tuning& tuning);

// What the last Modern camera step did, for the Camera tuning panel. live is
// false when no port stepped in the last half second.
struct Readout {
    bool live = false;
    std::size_t port = 0U;
    float speed = 0.0F;     // m/s, flat
    float lag = 0.0F;       // degrees the view points away from the nose
    float slip = 0.0F;      // degrees between the nose and the motion
    float roll = 0.0F;      // degrees
    float fov = 0.0F;       // effective vertical FOV, degrees
    float arm = 0.0F;       // eye to racer, m
    float trauma = 0.0F;    // 0..1; the shake is trauma squared
    float clearance = 1.0F; // 1 = boom at full length, less = pulled in by a wall
    bool grounded = false;
    bool drifting = false;
    bool boosting = false;
    bool look_back = false;
    bool steadying = false;  // easing out of a wall or kart hit
    std::uint32_t hits = 0U; // hits seen since this camera started
};
Readout readout();

// Logs every Modern camera step to the runtime log, as
// DKR_MODERN_CAMERA_TRACE=2 does. Not persisted.
bool step_log();
void set_step_log(bool enabled);

// Mouse orbit: mouse motion (in pixels) turns this port's camera around the
// racer; it recentres once the mouse has been left alone for the tuning's
// recentre delay, or never while its right button is held. available() is
// false unless the Modern camera drives this port's race and the tuning's
// mouse speed is above zero.
bool mouse_orbit_available(std::size_t port);
void add_mouse_motion(std::size_t port, float dx, float dy);
void set_mouse_orbit_held(std::size_t port, bool held);

// Latest host input for an N64 controller port. camera_x/camera_y are the
// analogue values of whatever is bound to the C-buttons (right stick by
// default); online is true while netplay owns input routing.
void publish_input(std::size_t port, float camera_x, float camera_y, bool look_back, bool online);

// Update the online gate at routing handoff, before the first input poll.
// Reset requests are serviced by the guest thread; never retain poses/FOV
// from an earlier runtime, or mutate guest memory from the SDL thread.
void set_online_routing(bool online);
void reset_session();

// True while the Modern camera drives this port's follow camera (it did so
// in the last few hundred milliseconds): its C-buttons belong to the camera
// and the Accelerate control presses A.
bool owns_controls(std::size_t port);

// Called with the finished retail camera (after its clearance pass). Returns
// true when it moved the camera; the caller then resolves clearance for the
// new pose and calls commit().
bool begin(std::uint8_t* rdram, recomp_context* context, std::uint32_t object, const std::uint32_t fields[8]);
void commit(std::uint8_t* rdram, const std::uint32_t fields[8]);

// At update_player_camera entry: put back the retail pose of the camera about
// to be updated, so the game's smoothing continues from its own state.
void restore(std::uint8_t* rdram, const std::uint32_t fields[8]);

// One-shot interpolation cut for a player camera slot (look-back toggles).
bool consume_cut(std::uint32_t camera_slot);

} // namespace dkr::runtime::modern_camera
