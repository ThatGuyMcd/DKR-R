#pragma once

#include "controller_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

namespace dkr::runtime::input {

constexpr int kUnbound = -1;
inline constexpr std::size_t kPlayerCount = 4;

enum class Action : std::uint8_t {
    StickUp,
    StickDown,
    StickLeft,
    StickRight,
    A,
    B,
    Z,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    L,
    R,
    CUp,
    CDown,
    CLeft,
    CRight,
    // Host-only, no N64 bit: the Modern camera's held look-back, and its
    // Accelerate (presses A while the Modern camera drives the race).
    LookBack,
    Accelerate,
    OrbitUp, OrbitDown, OrbitLeft, OrbitRight,
    CameraRecenter, CameraCycle, CameraZoomIn, CameraZoomOut,
    CameraFirstPerson, CameraXrRecenter,
    Count,
};

enum class GyroAxis : std::uint8_t {
    Roll = 0,
    Yaw = 1,
};

enum class VehicleClass : std::uint8_t {
    Car = 0,
    Hovercraft = 1,
    Plane = 2,
    Count,
};

enum class ShortcutAction : std::uint8_t {
    QuickRestart = 0,
    ToggleOverlay,
    ToggleTexturePack,
    ToggleFullscreen,
    RecenterGyro,
    Count,
};

struct State {
    std::uint16_t buttons = 0;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    // Analogue values of the C-button bindings, the held look-back and
    // Accelerate, for the local Modern camera. Not N64 controller state.
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    bool look_back = false;
    bool accelerate = false;
    float orbit_x=0,orbit_y=0;
    std::uint32_t camera_actions=0;
};

struct ShortcutBinding {
    int primary = kUnbound;
    int secondary = kUnbound;
};

std::size_t action_count();
const char* action_identifier(Action action);
const char* action_label(Action action);

int keyboard_binding(Action action);
int controller_binding(Action action);
int keyboard_binding(std::size_t player, Action action);
int controller_binding(std::size_t player, Action action);
int secondary_controller_binding(std::size_t player, Action action);
void set_keyboard_binding(Action action, int scancode);
void set_controller_binding(Action action, int source);
void set_keyboard_binding(std::size_t player, Action action, int scancode);
void set_controller_binding(std::size_t player, Action action, int source);
void set_secondary_controller_binding(std::size_t player, Action action,
                                      int source);
void reset_defaults();
void reset_defaults(std::size_t player);
void copy_bindings(std::size_t source_player, std::size_t target_player);
int keyboard_player();
void set_keyboard_player(int player);
bool background_input_enabled(std::size_t player);
void set_background_input_enabled(std::size_t player, bool enabled);

bool quick_restart_enabled();
void set_quick_restart_enabled(bool enabled);
ShortcutBinding quick_restart_keyboard_binding();
ShortcutBinding quick_restart_controller_binding();
void set_quick_restart_keyboard_binding(ShortcutBinding binding);
void set_quick_restart_controller_binding(ShortcutBinding binding);
bool consume_quick_restart_request();
ShortcutBinding shortcut_keyboard_binding(ShortcutAction action);
ShortcutBinding shortcut_controller_binding(ShortcutAction action);
void set_shortcut_keyboard_binding(ShortcutAction action,
                                   ShortcutBinding binding);
void set_shortcut_controller_binding(ShortcutAction action,
                                     ShortcutBinding binding);
bool consume_shortcut_request(ShortcutAction action);

float stick_deadzone();
void set_stick_deadzone(float percent);
float stick_anti_deadzone();
void set_stick_anti_deadzone(float percent);
float stick_sensitivity();
void set_stick_sensitivity(float percent);
float stick_curve();
void set_stick_curve(float exponent);
bool stick_x_inverted();
void set_stick_x_inverted(bool inverted);
bool stick_y_inverted();
void set_stick_y_inverted(bool inverted);
bool vehicle_stick_x_inverted(VehicleClass vehicle);
void set_vehicle_stick_x_inverted(VehicleClass vehicle, bool inverted);
bool vehicle_stick_y_inverted(VehicleClass vehicle);
void set_vehicle_stick_y_inverted(VehicleClass vehicle, bool inverted);
void set_active_vehicle(std::size_t player, int vehicle);
float trigger_threshold();
void set_trigger_threshold(float threshold);

bool gyro_enabled(std::size_t player = 0U);
void set_gyro_enabled(bool enabled, std::size_t player = 0U);
float gyro_sensitivity(std::size_t player = 0U);
void set_gyro_sensitivity(float percent, std::size_t player = 0U);
float gyro_y_sensitivity(std::size_t player = 0U);
void set_gyro_y_sensitivity(float percent, std::size_t player = 0U);
float gyro_deadzone(std::size_t player = 0U);
void set_gyro_deadzone(float degrees_per_second, std::size_t player = 0U);
bool gyro_inverted(std::size_t player = 0U);
void set_gyro_inverted(bool inverted, std::size_t player = 0U);
bool gyro_y_inverted(std::size_t player = 0U);
void set_gyro_y_inverted(bool inverted, std::size_t player = 0U);
GyroAxis gyro_axis(std::size_t player = 0U);
void set_gyro_axis(GyroAxis axis, std::size_t player = 0U);
void begin_gyro_calibration(std::size_t player = 0U);
void recenter_gyro(std::size_t player = 0U);
float gyro_steering_position(std::size_t player = 0U);
float gyro_steering_y_position(std::size_t player = 0U);
bool gyro_calibrating(std::size_t player = 0U);
float gyro_calibration_progress(std::size_t player = 0U);

int encode_controller_button(int button);
int encode_controller_axis(int axis, bool positive);
std::string keyboard_binding_name(int scancode);
std::string controller_binding_name(int source);

State poll(std::size_t player, SDL_GameController* controller,
           SDL_GameController* gyro_controller,
           bool include_keyboard, bool blocked,
           bool allow_quick_restart = true,
           bool global_shortcut_owner = true);

// SDL-neutral input path used exclusively by isolated controller backends.
// The native SDL2 path above remains intact as the compatibility fallback.
State poll_snapshot(
    std::size_t player,
    const controllers::ControllerSnapshot* controller,
    const controllers::ControllerSnapshot* gyro_controller,
    bool include_keyboard, bool blocked,
    bool allow_quick_restart = true,
    bool global_shortcut_owner = true);

} // namespace dkr::runtime::input
