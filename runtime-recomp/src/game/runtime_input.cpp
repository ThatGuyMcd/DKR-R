#include "runtime_input.hpp"
#include "motion_steering_policy.hpp"
#include "runtime_enhancements.hpp"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace {

using dkr::runtime::input::Action;

constexpr int kAxisSourceBase = 1000;
constexpr std::uint16_t kButtonA = 0x8000;
constexpr std::uint16_t kButtonB = 0x4000;
constexpr std::uint16_t kButtonZ = 0x2000;
constexpr std::uint16_t kButtonStart = 0x1000;
constexpr std::uint16_t kDpadUp = 0x0800;
constexpr std::uint16_t kDpadDown = 0x0400;
constexpr std::uint16_t kDpadLeft = 0x0200;
constexpr std::uint16_t kDpadRight = 0x0100;
constexpr std::uint16_t kButtonL = 0x0020;
constexpr std::uint16_t kButtonR = 0x0010;
constexpr std::uint16_t kCUp = 0x0008;
constexpr std::uint16_t kCDown = 0x0004;
constexpr std::uint16_t kCLeft = 0x0002;
constexpr std::uint16_t kCRight = 0x0001;

struct BindingPair {
    int keyboard;
    int controller;
    int controller_secondary = dkr::runtime::input::kUnbound;
};

#if DKR_RUNTIME_HAS_RT64
constexpr std::array<BindingPair, static_cast<std::size_t>(Action::Count)> kDefaults{{
    {SDL_SCANCODE_W, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTY * 2},
    {SDL_SCANCODE_S, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTY * 2 + 1},
    {SDL_SCANCODE_A, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTX * 2},
    {SDL_SCANCODE_D, kAxisSourceBase + SDL_CONTROLLER_AXIS_LEFTX * 2 + 1},
    {SDL_SCANCODE_SPACE, SDL_CONTROLLER_BUTTON_A},
    {SDL_SCANCODE_LSHIFT, SDL_CONTROLLER_BUTTON_X},
    {SDL_SCANCODE_Z, kAxisSourceBase + SDL_CONTROLLER_AXIS_TRIGGERLEFT * 2 + 1},
    {SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START},
    {SDL_SCANCODE_UP, SDL_CONTROLLER_BUTTON_DPAD_UP},
    {SDL_SCANCODE_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {SDL_SCANCODE_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {SDL_SCANCODE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {SDL_SCANCODE_Q, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {SDL_SCANCODE_E, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {SDL_SCANCODE_I, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTY * 2},
    {SDL_SCANCODE_K, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTY * 2 + 1},
    {SDL_SCANCODE_J, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTX * 2},
    {SDL_SCANCODE_L, kAxisSourceBase + SDL_CONTROLLER_AXIS_RIGHTX * 2 + 1},
    {SDL_SCANCODE_O, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {dkr::runtime::input::kUnbound, kAxisSourceBase + SDL_CONTROLLER_AXIS_TRIGGERRIGHT * 2 + 1},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTY*2},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTY*2+1},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTX*2},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTX*2+1},
    {SDL_SCANCODE_R,SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {SDL_SCANCODE_C,SDL_CONTROLLER_BUTTON_Y},
    {SDL_SCANCODE_PAGEUP,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_PAGEDOWN,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_F6,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_HOME,dkr::runtime::input::kUnbound},
}};
#else
constexpr std::array<BindingPair, static_cast<std::size_t>(Action::Count)> kDefaults{};
#endif

using PlayerBindings =
    std::array<BindingPair, static_cast<std::size_t>(Action::Count)>;
std::array<PlayerBindings, dkr::runtime::input::kPlayerCount> g_bindings{
    kDefaults, kDefaults, kDefaults, kDefaults};
std::mutex g_binding_mutex;
std::atomic<int> g_keyboard_player{0};
std::array<std::atomic<bool>, dkr::runtime::input::kPlayerCount>
    g_background_input_enabled{};
#if DKR_RUNTIME_HAS_RT64
using ShortcutBindings = std::array<dkr::runtime::input::ShortcutBinding,
    static_cast<std::size_t>(dkr::runtime::input::ShortcutAction::Count)>;
ShortcutBindings g_shortcut_keyboard{{
    {SDL_SCANCODE_LCTRL, SDL_SCANCODE_R}, {}, {}, {},
    {SDL_SCANCODE_LCTRL, SDL_SCANCODE_G},
}};
ShortcutBindings g_shortcut_controller{{
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_START},
    {SDL_CONTROLLER_BUTTON_BACK, dkr::runtime::input::kUnbound},
    {}, {},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
}};
#else
using ShortcutBindings = std::array<dkr::runtime::input::ShortcutBinding,
    static_cast<std::size_t>(dkr::runtime::input::ShortcutAction::Count)>;
ShortcutBindings g_shortcut_keyboard{};
ShortcutBindings g_shortcut_controller{};
#endif
std::mutex g_shortcut_mutex;
std::atomic<bool> g_quick_restart_enabled{false};
std::array<std::atomic<bool>,
    static_cast<std::size_t>(dkr::runtime::input::ShortcutAction::Count)>
    g_shortcut_requested{};
std::array<std::atomic<bool>,
    static_cast<std::size_t>(dkr::runtime::input::ShortcutAction::Count)>
    g_shortcut_held{};
struct GyroState {
    std::atomic<bool> enabled{false};
    std::atomic<float> sensitivity{100.0F};
    std::atomic<float> y_sensitivity{100.0F};
    std::atomic<float> deadzone{2.0F};
    std::atomic<bool> inverted{false};
    std::atomic<bool> y_inverted{false};
    std::atomic<dkr::runtime::input::GyroAxis> axis{
        dkr::runtime::input::GyroAxis::Roll};
    std::atomic<float> bias{0.0F};
    std::atomic<float> y_bias{0.0F};
    std::atomic<float> calibration_sum{0.0F};
    std::atomic<float> y_calibration_sum{0.0F};
    std::atomic<int> calibration_samples{0};
    std::atomic<int> calibration_remaining{0};
    std::mutex motion_mutex;
    float angle_radians = 0.0F;
    float y_angle_radians = 0.0F;
    std::chrono::steady_clock::time_point last_sample{};
    std::uint64_t last_sensor_timestamp_us = 0U;
    bool has_last_sample = false;
};
std::array<GyroState, dkr::runtime::input::kPlayerCount> g_gyro_states{};
constexpr int kGyroCalibrationSampleCount = 90;
std::atomic<float> g_stick_deadzone{23.95F};
std::atomic<float> g_stick_anti_deadzone{0.0F};
std::atomic<float> g_stick_sensitivity{100.0F};
std::atomic<float> g_stick_curve{1.0F};
std::atomic<bool> g_stick_x_inverted{false};
std::atomic<bool> g_stick_y_inverted{false};
std::array<std::atomic<bool>,
    static_cast<std::size_t>(dkr::runtime::input::VehicleClass::Count)>
    g_vehicle_stick_x_inverted{};
std::array<std::atomic<bool>,
    static_cast<std::size_t>(dkr::runtime::input::VehicleClass::Count)>
    g_vehicle_stick_y_inverted{};
std::array<std::atomic<std::uint8_t>, dkr::runtime::input::kPlayerCount>
    g_active_vehicle{};
std::atomic<float> g_trigger_threshold{0.5F};

constexpr std::array<const char*, static_cast<std::size_t>(Action::Count)> kIdentifiers{{
    "stick_up", "stick_down", "stick_left", "stick_right", "a", "b", "z",
    "start", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "l", "r",
    "c_up", "c_down", "c_left", "c_right", "look_back", "accelerate",
    "camera_up","camera_down","camera_left","camera_right","camera_recenter","camera_cycle",
    "camera_zoom_in","camera_zoom_out","camera_first_person","camera_xr_recenter",
}};

constexpr std::array<const char*, static_cast<std::size_t>(Action::Count)> kLabels{{
    "Analogue up", "Analogue down", "Analogue left", "Analogue right", "A button",
    "B button", "Z trigger", "Start", "D-pad up", "D-pad down", "D-pad left",
    "D-pad right", "L shoulder", "R shoulder", "C-up", "C-down", "C-left", "C-right",
    "Look back", "Accelerate",
    "Camera up","Camera down","Camera left","Camera right","Centre camera","Cycle camera",
    "Camera zoom in","Camera zoom out","First-person camera","Centre headset",
}};

std::size_t Index(Action action) {
    return std::min(static_cast<std::size_t>(action),
                    static_cast<std::size_t>(Action::Count) - 1U);
}

std::size_t PlayerIndex(std::size_t player) {
    return std::min(player, dkr::runtime::input::kPlayerCount - 1U);
}

std::size_t ShortcutIndex(dkr::runtime::input::ShortcutAction action) {
    return std::min(static_cast<std::size_t>(action),
        static_cast<std::size_t>(
            dkr::runtime::input::ShortcutAction::Count) - 1U);
}

std::size_t VehicleIndex(dkr::runtime::input::VehicleClass vehicle) {
    return std::min(static_cast<std::size_t>(vehicle),
        static_cast<std::size_t>(
            dkr::runtime::input::VehicleClass::Count) - 1U);
}

#if DKR_RUNTIME_HAS_RT64
float NormaliseAxis(Sint16 value, Sint16 deadzone = 7849) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(magnitude - deadzone) /
                         static_cast<float>(32767 - deadzone);
    return std::copysign(std::min(scaled, 1.0F), static_cast<float>(value));
}

float SourceValue(SDL_GameController* controller, int source) {
    if (controller == nullptr || source < 0) {
        return 0.0F;
    }
    if (source < SDL_CONTROLLER_BUTTON_MAX) {
        // Purpose-built N64 pads commonly expose the physical B button through
        // SDL's B slot and have no X slot at all. DKR-R's generic modern-pad
        // layout uses X because it sits to the left of A. Preserve that layout,
        // but fall back to SDL B only on controllers where X is genuinely
        // absent (8BitDo/NSO/Hyperkin/Raphnet N64 layouts, for example).
        if (source == SDL_CONTROLLER_BUTTON_X &&
            SDL_GameControllerGetBindForButton(
                controller, SDL_CONTROLLER_BUTTON_X).bindType ==
                SDL_CONTROLLER_BINDTYPE_NONE &&
            SDL_GameControllerGetBindForButton(
                controller, SDL_CONTROLLER_BUTTON_B).bindType !=
                SDL_CONTROLLER_BINDTYPE_NONE) {
            source = SDL_CONTROLLER_BUTTON_B;
        }
        return SDL_GameControllerGetButton(
            controller, static_cast<SDL_GameControllerButton>(source)) != 0 ? 1.0F : 0.0F;
    }
    if (source < kAxisSourceBase) {
        return 0.0F;
    }
    const int encoded = source - kAxisSourceBase;
    const int axis = encoded / 2;
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) {
        return 0.0F;
    }
    const bool positive = (encoded & 1) != 0;
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const Sint16 deadzone = modern
        ? static_cast<Sint16>(std::lround(
              std::clamp(g_stick_deadzone.load(std::memory_order_relaxed),
                         0.0F, 35.0F) * 32767.0F / 100.0F))
        : 7849;
    const float value = NormaliseAxis(SDL_GameControllerGetAxis(
        controller, static_cast<SDL_GameControllerAxis>(axis)), deadzone);
    return positive ? std::max(value, 0.0F) : std::max(-value, 0.0F);
}

float SnapshotSourceValue(
    const dkr::runtime::controllers::ControllerSnapshot* controller,
    int source) {
    using dkr::runtime::controllers::kSnapshotAxisCount;
    using dkr::runtime::controllers::kSnapshotButtonCount;
    if (controller == nullptr || !controller->connected || source < 0) {
        return 0.0F;
    }
    if (source < SDL_CONTROLLER_BUTTON_MAX) {
        if (source >= static_cast<int>(kSnapshotButtonCount)) return 0.0F;
        // Preserve the SDL2 N64-controller compatibility rule exactly.
        if (source == SDL_CONTROLLER_BUTTON_X &&
            controller->button_bound[SDL_CONTROLLER_BUTTON_X] == 0U &&
            controller->button_bound[SDL_CONTROLLER_BUTTON_B] != 0U) {
            source = SDL_CONTROLLER_BUTTON_B;
        }
        return controller->buttons[static_cast<std::size_t>(source)] != 0U
            ? 1.0F : 0.0F;
    }
    if (source < kAxisSourceBase) return 0.0F;
    const int encoded = source - kAxisSourceBase;
    const int axis = encoded / 2;
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX ||
        axis >= static_cast<int>(kSnapshotAxisCount)) {
        return 0.0F;
    }
    const bool positive = (encoded & 1) != 0;
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const Sint16 deadzone = modern
        ? static_cast<Sint16>(std::lround(
              std::clamp(g_stick_deadzone.load(std::memory_order_relaxed),
                         0.0F, 35.0F) * 32767.0F / 100.0F))
        : 7849;
    const float value = NormaliseAxis(
        controller->axes[static_cast<std::size_t>(axis)], deadzone);
    return positive ? std::max(value, 0.0F) : std::max(-value, 0.0F);
}

bool KeyboardSourceHeld(const Uint8* keys, int source) {
    return keys != nullptr && source >= 0 && source < SDL_NUM_SCANCODES &&
           keys[source] != 0;
}

bool ControllerSourceHeld(SDL_GameController* controller, int source) {
    return source >= 0 && source < SDL_CONTROLLER_BUTTON_MAX &&
           SourceValue(controller, source) > 0.5F;
}

bool SnapshotControllerSourceHeld(
    const dkr::runtime::controllers::ControllerSnapshot* controller,
    int source) {
    return source >= 0 && source < SDL_CONTROLLER_BUTTON_MAX &&
           SnapshotSourceValue(controller, source) > 0.5F;
}

template <typename Predicate>
bool ShortcutHeld(const dkr::runtime::input::ShortcutBinding& binding,
                  Predicate&& predicate) {
    if (binding.primary == dkr::runtime::input::kUnbound ||
        !predicate(binding.primary)) {
        return false;
    }
    return binding.secondary == dkr::runtime::input::kUnbound ||
           predicate(binding.secondary);
}

bool ShortcutContains(const dkr::runtime::input::ShortcutBinding& binding,
                      int source) {
    return source != dkr::runtime::input::kUnbound &&
           (binding.primary == source || binding.secondary == source);
}

float ShapeStick(float value, bool inverted) {
    const float magnitude = std::fabs(value);
    if (magnitude <= 0.0F) {
        return 0.0F;
    }
    const float anti = std::clamp(
        g_stick_anti_deadzone.load(std::memory_order_relaxed) / 100.0F,
        0.0F, 0.5F);
    const float curve = std::clamp(
        g_stick_curve.load(std::memory_order_relaxed), 0.5F, 2.5F);
    const float sensitivity = std::clamp(
        g_stick_sensitivity.load(std::memory_order_relaxed) / 100.0F,
        0.5F, 1.5F);
    float shaped = anti + (1.0F - anti) * std::pow(magnitude, curve);
    shaped = std::clamp(shaped * sensitivity, 0.0F, 1.0F);
    return std::copysign(shaped, inverted ? -value : value);
}

struct GyroSample {
    float x = 0.0F;
    float y = 0.0F;
};

std::optional<GyroSample> PollGyro(std::size_t player,
                                   SDL_GameController* controller) {
    using namespace dkr::runtime::input;
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    if (controller == nullptr || !gyro_enabled(player) ||
        !dkr::runtime::enhancements::modern_presentation_enabled() ||
        SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) != SDL_TRUE) {
        recenter_gyro(player);
        return std::nullopt;
    }
    if (SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_GYRO) != SDL_TRUE &&
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO,
                                           SDL_TRUE) != 0) {
        recenter_gyro(player);
        return std::nullopt;
    }
    float sensor[3]{};
        std::uint64_t sensor_timestamp_us = 0U;
    if (SDL_GameControllerGetSensorDataWithTimestamp(
            controller, SDL_SENSOR_GYRO, &sensor_timestamp_us,
            sensor, 3) != 0) {
        recenter_gyro(player);
        return std::nullopt;
    }
    const float raw_x = gyro_axis(player) == GyroAxis::Yaw ? sensor[1] : sensor[2];
    const float raw_y = sensor[0];
    const int remaining = state.calibration_remaining.load(
        std::memory_order_acquire);
    if (remaining > 0) {
        const float sum = state.calibration_sum.fetch_add(
            raw_x, std::memory_order_acq_rel) + raw_x;
        const float y_sum = state.y_calibration_sum.fetch_add(
            raw_y, std::memory_order_acq_rel) + raw_y;
        const int samples = state.calibration_samples.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (state.calibration_remaining.fetch_sub(
                1, std::memory_order_acq_rel) == 1) {
            state.bias.store(sum / static_cast<float>(samples),
                              std::memory_order_release);
            state.y_bias.store(y_sum / static_cast<float>(samples),
                                std::memory_order_release);
            recenter_gyro(player);
        }
        return GyroSample{};
    }
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock motion_lock(state.motion_mutex);
    const float host_delta_seconds = state.has_last_sample
        ? std::chrono::duration<float>(now - state.last_sample).count()
        : 0.0F;
    const float delta_seconds = gyro_sample_delta_seconds(
        sensor_timestamp_us, state.last_sensor_timestamp_us,
        host_delta_seconds, state.has_last_sample);
    state.last_sample = now;
    if (sensor_timestamp_us != 0U) {
        state.last_sensor_timestamp_us = sensor_timestamp_us;
    }
    state.has_last_sample = true;
    state.angle_radians = integrate_gyro_angle(
        state.angle_radians, raw_x,
        state.bias.load(std::memory_order_acquire), gyro_deadzone(player),
        delta_seconds, gyro_inverted(player));
    state.y_angle_radians = integrate_gyro_angle(
        state.y_angle_radians, raw_y,
        state.y_bias.load(std::memory_order_acquire), gyro_deadzone(player),
        delta_seconds, gyro_y_inverted(player));
    return GyroSample{
        gyro_angle_to_steering(state.angle_radians, gyro_sensitivity(player)),
        gyro_angle_to_steering(state.y_angle_radians,
                               gyro_y_sensitivity(player))};
}

std::optional<GyroSample> PollSnapshotGyro(
    std::size_t player,
    const dkr::runtime::controllers::ControllerSnapshot* controller) {
    using namespace dkr::runtime::input;
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    if (controller == nullptr || !controller->connected ||
        !controller->gyro.available || !controller->gyro.valid ||
        !gyro_enabled(player) ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) {
        recenter_gyro(player);
        return std::nullopt;
    }
    const auto& sample = controller->gyro;
    const float raw_x = gyro_axis(player) == GyroAxis::Yaw
        ? sample.data[1] : sample.data[2];
    const float raw_y = sample.data[0];
    const int remaining = state.calibration_remaining.load(
        std::memory_order_acquire);
    const bool repeated_timestamp = sample.sensor_timestamp_us != 0U &&
        state.has_last_sample &&
        sample.sensor_timestamp_us == state.last_sensor_timestamp_us;
    if (remaining > 0 && !repeated_timestamp) {
        const float sum = state.calibration_sum.fetch_add(
            raw_x, std::memory_order_acq_rel) + raw_x;
        const float y_sum = state.y_calibration_sum.fetch_add(
            raw_y, std::memory_order_acq_rel) + raw_y;
        const int samples = state.calibration_samples.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (state.calibration_remaining.fetch_sub(
                1, std::memory_order_acq_rel) == 1) {
            state.bias.store(sum / static_cast<float>(samples),
                             std::memory_order_release);
            state.y_bias.store(y_sum / static_cast<float>(samples),
                               std::memory_order_release);
            recenter_gyro(player);
        } else {
            state.last_sensor_timestamp_us = sample.sensor_timestamp_us;
            state.has_last_sample = true;
        }
        return GyroSample{};
    }

    std::scoped_lock motion_lock(state.motion_mutex);
    if (!repeated_timestamp) {
        const auto now = std::chrono::steady_clock::now();
        const float host_delta_seconds = state.has_last_sample
            ? std::chrono::duration<float>(now - state.last_sample).count()
            : 0.0F;
        const float delta_seconds = gyro_sample_delta_seconds(
            sample.sensor_timestamp_us, state.last_sensor_timestamp_us,
            host_delta_seconds, state.has_last_sample);
        state.last_sample = now;
        if (sample.sensor_timestamp_us != 0U) {
            state.last_sensor_timestamp_us = sample.sensor_timestamp_us;
        }
        state.has_last_sample = true;
        state.angle_radians = integrate_gyro_angle(
            state.angle_radians, raw_x,
            state.bias.load(std::memory_order_acquire), gyro_deadzone(player),
            delta_seconds, gyro_inverted(player));
        state.y_angle_radians = integrate_gyro_angle(
            state.y_angle_radians, raw_y,
            state.y_bias.load(std::memory_order_acquire), gyro_deadzone(player),
            delta_seconds, gyro_y_inverted(player));
    }
    return GyroSample{
        gyro_angle_to_steering(state.angle_radians, gyro_sensitivity(player)),
        gyro_angle_to_steering(state.y_angle_radians,
                               gyro_y_sensitivity(player))};
}
#endif

} // namespace

std::size_t dkr::runtime::input::action_count() {
    return static_cast<std::size_t>(Action::Count);
}

const char* dkr::runtime::input::action_identifier(Action action) {
    return kIdentifiers[Index(action)];
}

const char* dkr::runtime::input::action_label(Action action) {
    return kLabels[Index(action)];
}

int dkr::runtime::input::keyboard_binding(Action action) {
    return keyboard_binding(0U, action);
}

int dkr::runtime::input::keyboard_binding(std::size_t player, Action action) {
    std::scoped_lock lock(g_binding_mutex);
    return g_bindings[PlayerIndex(player)][Index(action)].keyboard;
}

int dkr::runtime::input::controller_binding(Action action) {
    return controller_binding(0U, action);
}

int dkr::runtime::input::controller_binding(std::size_t player, Action action) {
    std::scoped_lock lock(g_binding_mutex);
    return g_bindings[PlayerIndex(player)][Index(action)].controller;
}

int dkr::runtime::input::secondary_controller_binding(
    std::size_t player, Action action) {
    std::scoped_lock lock(g_binding_mutex);
    return g_bindings[PlayerIndex(player)][Index(action)].controller_secondary;
}

void dkr::runtime::input::set_keyboard_binding(Action action, int scancode) {
    set_keyboard_binding(0U, action, scancode);
}

void dkr::runtime::input::set_keyboard_binding(std::size_t player, Action action,
                                                int scancode) {
    std::scoped_lock lock(g_binding_mutex);
    PlayerBindings& bindings = g_bindings[PlayerIndex(player)];
    const std::size_t target = Index(action);
    if (scancode != kUnbound) {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            if (index != target && bindings[index].keyboard == scancode) {
                bindings[index].keyboard = kUnbound;
            }
        }
    }
    bindings[target].keyboard = scancode;
}

void dkr::runtime::input::set_controller_binding(Action action, int source) {
    set_controller_binding(0U, action, source);
}

void dkr::runtime::input::set_controller_binding(std::size_t player, Action action,
                                                  int source) {
    std::scoped_lock lock(g_binding_mutex);
    PlayerBindings& bindings = g_bindings[PlayerIndex(player)];
    const std::size_t target = Index(action);
    if (source != kUnbound) {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            if (index != target && bindings[index].controller == source) {
                bindings[index].controller = kUnbound;
            }
            if (bindings[index].controller_secondary == source) {
                bindings[index].controller_secondary = kUnbound;
            }
        }
    }
    bindings[target].controller = source;
    if (bindings[target].controller_secondary == source) {
        bindings[target].controller_secondary = kUnbound;
    }
}

void dkr::runtime::input::set_secondary_controller_binding(
    std::size_t player, Action action, int source) {
    std::scoped_lock lock(g_binding_mutex);
    PlayerBindings& bindings = g_bindings[PlayerIndex(player)];
    const std::size_t target = Index(action);
    if (source != kUnbound) {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            if (bindings[index].controller == source) {
                bindings[index].controller = kUnbound;
            }
            if (index != target &&
                bindings[index].controller_secondary == source) {
                bindings[index].controller_secondary = kUnbound;
            }
        }
    }
    bindings[target].controller_secondary = source;
}

void dkr::runtime::input::reset_defaults() {
    {
        std::scoped_lock lock(g_binding_mutex);
        g_bindings.fill(kDefaults);
    }
#if DKR_RUNTIME_HAS_RT64
    {
        std::scoped_lock lock(g_shortcut_mutex);
        g_shortcut_keyboard.fill({});
        g_shortcut_controller.fill({});
        g_shortcut_keyboard[ShortcutIndex(ShortcutAction::QuickRestart)] =
            {SDL_SCANCODE_LCTRL, SDL_SCANCODE_R};
        g_shortcut_controller[ShortcutIndex(ShortcutAction::QuickRestart)] =
            {SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_START};
        g_shortcut_controller[ShortcutIndex(ShortcutAction::ToggleOverlay)] =
            {SDL_CONTROLLER_BUTTON_BACK, kUnbound};
        g_shortcut_keyboard[ShortcutIndex(ShortcutAction::RecenterGyro)] =
            {SDL_SCANCODE_LCTRL, SDL_SCANCODE_G};
        g_shortcut_controller[ShortcutIndex(ShortcutAction::RecenterGyro)] =
            {SDL_CONTROLLER_BUTTON_LEFTSTICK,
             SDL_CONTROLLER_BUTTON_RIGHTSTICK};
    }
#endif
    g_quick_restart_enabled.store(false, std::memory_order_release);
    for (auto& requested : g_shortcut_requested) requested.store(false);
    for (auto& held : g_shortcut_held) held.store(false);
}

void dkr::runtime::input::reset_defaults(std::size_t player) {
    std::scoped_lock lock(g_binding_mutex);
    g_bindings[PlayerIndex(player)] = kDefaults;
}

void dkr::runtime::input::copy_bindings(std::size_t source_player,
                                        std::size_t target_player) {
    std::scoped_lock lock(g_binding_mutex);
    g_bindings[PlayerIndex(target_player)] = g_bindings[PlayerIndex(source_player)];
}

int dkr::runtime::input::keyboard_player() {
    return g_keyboard_player.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_keyboard_player(int player) {
    g_keyboard_player.store(std::clamp(player, 0,
        static_cast<int>(kPlayerCount) - 1), std::memory_order_release);
}

bool dkr::runtime::input::background_input_enabled(std::size_t player) {
    return g_background_input_enabled[PlayerIndex(player)].load(
        std::memory_order_acquire);
}

void dkr::runtime::input::set_background_input_enabled(std::size_t player,
                                                       bool enabled) {
    g_background_input_enabled[PlayerIndex(player)].store(
        enabled, std::memory_order_release);
}

bool dkr::runtime::input::quick_restart_enabled() {
    return g_quick_restart_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_quick_restart_enabled(bool enabled) {
    g_quick_restart_enabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        const std::size_t index = ShortcutIndex(ShortcutAction::QuickRestart);
        g_shortcut_requested[index].store(false, std::memory_order_release);
        g_shortcut_held[index].store(false, std::memory_order_release);
    }
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::quick_restart_keyboard_binding() {
    return shortcut_keyboard_binding(ShortcutAction::QuickRestart);
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::quick_restart_controller_binding() {
    return shortcut_controller_binding(ShortcutAction::QuickRestart);
}

void dkr::runtime::input::set_quick_restart_keyboard_binding(
    ShortcutBinding binding) {
    set_shortcut_keyboard_binding(ShortcutAction::QuickRestart, binding);
}

void dkr::runtime::input::set_quick_restart_controller_binding(
    ShortcutBinding binding) {
    set_shortcut_controller_binding(ShortcutAction::QuickRestart, binding);
}

bool dkr::runtime::input::consume_quick_restart_request() {
    return consume_shortcut_request(ShortcutAction::QuickRestart);
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::shortcut_keyboard_binding(ShortcutAction action) {
    std::scoped_lock lock(g_shortcut_mutex);
    return g_shortcut_keyboard[ShortcutIndex(action)];
}

dkr::runtime::input::ShortcutBinding
dkr::runtime::input::shortcut_controller_binding(ShortcutAction action) {
    std::scoped_lock lock(g_shortcut_mutex);
    return g_shortcut_controller[ShortcutIndex(action)];
}

void dkr::runtime::input::set_shortcut_keyboard_binding(
    ShortcutAction action, ShortcutBinding binding) {
    std::scoped_lock lock(g_shortcut_mutex);
    g_shortcut_keyboard[ShortcutIndex(action)] = binding;
}

void dkr::runtime::input::set_shortcut_controller_binding(
    ShortcutAction action, ShortcutBinding binding) {
    std::scoped_lock lock(g_shortcut_mutex);
    g_shortcut_controller[ShortcutIndex(action)] = binding;
}

bool dkr::runtime::input::consume_shortcut_request(ShortcutAction action) {
    return g_shortcut_requested[ShortcutIndex(action)].exchange(
        false, std::memory_order_acq_rel);
}

float dkr::runtime::input::stick_deadzone() { return g_stick_deadzone.load(); }
void dkr::runtime::input::set_stick_deadzone(float value) {
    g_stick_deadzone.store(std::clamp(value, 0.0F, 35.0F));
}
float dkr::runtime::input::stick_anti_deadzone() { return g_stick_anti_deadzone.load(); }
void dkr::runtime::input::set_stick_anti_deadzone(float value) {
    g_stick_anti_deadzone.store(std::clamp(value, 0.0F, 50.0F));
}
float dkr::runtime::input::stick_sensitivity() { return g_stick_sensitivity.load(); }
void dkr::runtime::input::set_stick_sensitivity(float value) {
    g_stick_sensitivity.store(std::clamp(value, 50.0F, 150.0F));
}
float dkr::runtime::input::stick_curve() { return g_stick_curve.load(); }
void dkr::runtime::input::set_stick_curve(float value) {
    g_stick_curve.store(std::clamp(value, 0.5F, 2.5F));
}
bool dkr::runtime::input::stick_x_inverted() { return g_stick_x_inverted.load(); }
void dkr::runtime::input::set_stick_x_inverted(bool value) {
    g_stick_x_inverted.store(value);
    for (auto& vehicle : g_vehicle_stick_x_inverted) vehicle.store(value);
}
bool dkr::runtime::input::stick_y_inverted() { return g_stick_y_inverted.load(); }
void dkr::runtime::input::set_stick_y_inverted(bool value) {
    g_stick_y_inverted.store(value);
    for (auto& vehicle : g_vehicle_stick_y_inverted) vehicle.store(value);
}
bool dkr::runtime::input::vehicle_stick_x_inverted(VehicleClass vehicle) {
    return g_vehicle_stick_x_inverted[VehicleIndex(vehicle)].load();
}
void dkr::runtime::input::set_vehicle_stick_x_inverted(
    VehicleClass vehicle, bool value) {
    g_vehicle_stick_x_inverted[VehicleIndex(vehicle)].store(value);
}
bool dkr::runtime::input::vehicle_stick_y_inverted(VehicleClass vehicle) {
    return g_vehicle_stick_y_inverted[VehicleIndex(vehicle)].load();
}
void dkr::runtime::input::set_vehicle_stick_y_inverted(
    VehicleClass vehicle, bool value) {
    g_vehicle_stick_y_inverted[VehicleIndex(vehicle)].store(value);
}
void dkr::runtime::input::set_active_vehicle(std::size_t player, int vehicle) {
    g_active_vehicle[PlayerIndex(player)].store(static_cast<std::uint8_t>(
        std::clamp(vehicle, 0, static_cast<int>(VehicleClass::Count) - 1)));
}
float dkr::runtime::input::trigger_threshold() { return g_trigger_threshold.load(); }
void dkr::runtime::input::set_trigger_threshold(float value) {
    g_trigger_threshold.store(std::clamp(value, 0.05F, 0.95F));
}

bool dkr::runtime::input::gyro_enabled(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].enabled.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_enabled(bool enabled, std::size_t player) {
    g_gyro_states[PlayerIndex(player)].enabled.store(enabled, std::memory_order_release);
    recenter_gyro(player);
}

float dkr::runtime::input::gyro_sensitivity(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].sensitivity.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_sensitivity(float percent,
                                                std::size_t player) {
    g_gyro_states[PlayerIndex(player)].sensitivity.store(clamp_gyro_sensitivity(percent),
                             std::memory_order_release);
}

float dkr::runtime::input::gyro_y_sensitivity(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].y_sensitivity.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_y_sensitivity(float percent,
                                                  std::size_t player) {
    g_gyro_states[PlayerIndex(player)].y_sensitivity.store(clamp_gyro_sensitivity(percent),
                               std::memory_order_release);
}

float dkr::runtime::input::gyro_deadzone(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].deadzone.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_deadzone(float degrees_per_second,
                                            std::size_t player) {
    g_gyro_states[PlayerIndex(player)].deadzone.store(clamp_gyro_deadzone(degrees_per_second),
                          std::memory_order_release);
}

bool dkr::runtime::input::gyro_inverted(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].inverted.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_inverted(bool inverted,
                                            std::size_t player) {
    g_gyro_states[PlayerIndex(player)].inverted.store(inverted, std::memory_order_release);
}

bool dkr::runtime::input::gyro_y_inverted(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].y_inverted.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_y_inverted(bool inverted,
                                              std::size_t player) {
    g_gyro_states[PlayerIndex(player)].y_inverted.store(inverted, std::memory_order_release);
}

dkr::runtime::input::GyroAxis dkr::runtime::input::gyro_axis(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].axis.load(std::memory_order_acquire);
}

void dkr::runtime::input::set_gyro_axis(GyroAxis axis, std::size_t player) {
    g_gyro_states[PlayerIndex(player)].axis.store(
                       axis == GyroAxis::Yaw ? GyroAxis::Yaw : GyroAxis::Roll,
                       std::memory_order_release);
    recenter_gyro(player);
}

void dkr::runtime::input::begin_gyro_calibration(std::size_t player) {
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    state.calibration_sum.store(0.0F, std::memory_order_release);
    state.y_calibration_sum.store(0.0F, std::memory_order_release);
    state.calibration_samples.store(0, std::memory_order_release);
    state.calibration_remaining.store(kGyroCalibrationSampleCount,
                                       std::memory_order_release);
    recenter_gyro(player);
}

void dkr::runtime::input::recenter_gyro(std::size_t player) {
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    std::scoped_lock motion_lock(state.motion_mutex);
    state.angle_radians = 0.0F;
    state.y_angle_radians = 0.0F;
    state.last_sample = {};
    state.last_sensor_timestamp_us = 0U;
    state.has_last_sample = false;
}

float dkr::runtime::input::gyro_steering_position(std::size_t player) {
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    std::scoped_lock motion_lock(state.motion_mutex);
    return gyro_angle_to_steering(state.angle_radians,
                                  gyro_sensitivity(player));
}

float dkr::runtime::input::gyro_steering_y_position(std::size_t player) {
    GyroState& state = g_gyro_states[PlayerIndex(player)];
    std::scoped_lock motion_lock(state.motion_mutex);
    return gyro_angle_to_steering(state.y_angle_radians,
                                  gyro_y_sensitivity(player));
}

bool dkr::runtime::input::gyro_calibrating(std::size_t player) {
    return g_gyro_states[PlayerIndex(player)].calibration_remaining.load(
               std::memory_order_acquire) > 0;
}

float dkr::runtime::input::gyro_calibration_progress(std::size_t player) {
    const int remaining = g_gyro_states[PlayerIndex(player)].calibration_remaining.load(
        std::memory_order_acquire);
    return std::clamp(1.0F - static_cast<float>(remaining) /
                                 static_cast<float>(kGyroCalibrationSampleCount),
                      0.0F, 1.0F);
}

int dkr::runtime::input::encode_controller_button(int button) {
#if DKR_RUNTIME_HAS_RT64
    return button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX ? button : kUnbound;
#else
    (void)button;
    return kUnbound;
#endif
}

int dkr::runtime::input::encode_controller_axis(int axis, bool positive) {
#if DKR_RUNTIME_HAS_RT64
    return axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX
        ? kAxisSourceBase + axis * 2 + (positive ? 1 : 0)
        : kUnbound;
#else
    (void)axis;
    (void)positive;
    return kUnbound;
#endif
}

std::string dkr::runtime::input::keyboard_binding_name(int scancode) {
#if DKR_RUNTIME_HAS_RT64
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) {
        return "Unbound";
    }
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
    return name != nullptr && *name != '\0' ? name : "Unknown key";
#else
    (void)scancode;
    return "Unbound";
#endif
}

std::string dkr::runtime::input::controller_binding_name(int source) {
#if DKR_RUNTIME_HAS_RT64
    if (source >= 0 && source < SDL_CONTROLLER_BUTTON_MAX) {
        const char* name = SDL_GameControllerGetStringForButton(
            static_cast<SDL_GameControllerButton>(source));
        return name != nullptr ? name : "Unknown button";
    }
    if (source >= kAxisSourceBase) {
        const int encoded = source - kAxisSourceBase;
        const int axis = encoded / 2;
        if (axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX) {
            const char* name = SDL_GameControllerGetStringForAxis(
                static_cast<SDL_GameControllerAxis>(axis));
            return std::string(name != nullptr ? name : "axis") +
                   ((encoded & 1) != 0 ? " +" : " -");
        }
    }
#else
    (void)source;
#endif
    return "Unbound";
}

dkr::runtime::input::State dkr::runtime::input::poll(
    std::size_t player, SDL_GameController* controller,
    SDL_GameController* gyro_controller,
    bool include_keyboard, bool blocked, bool allow_quick_restart,
    bool global_shortcut_owner) {
    State state{};
#if DKR_RUNTIME_HAS_RT64
    // The platform supplies the shared gyro controller only to the physical
    // profile that currently owns it. Polling that accumulator from every
    // profile would recenter it repeatedly and erase the owner's sample.
    const std::optional<GyroSample> gyro = owns_gyro_accumulator(
                                               gyro_controller != nullptr)
        ? PollGyro(player, gyro_controller)
        : std::nullopt;
    std::array<BindingPair, static_cast<std::size_t>(Action::Count)> bindings;
    {
        std::scoped_lock lock(g_binding_mutex);
        bindings = g_bindings[PlayerIndex(player)];
    }
    const Uint8* keys = include_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    ShortcutBindings shortcut_keyboard;
    ShortcutBindings shortcut_controller;
    {
        std::scoped_lock lock(g_shortcut_mutex);
        shortcut_keyboard = g_shortcut_keyboard;
        shortcut_controller = g_shortcut_controller;
    }
    std::array<bool, static_cast<std::size_t>(ShortcutAction::Count)>
        keyboard_shortcut_held{};
    std::array<bool, static_cast<std::size_t>(ShortcutAction::Count)>
        controller_shortcut_held{};
    if (global_shortcut_owner) {
        for (std::size_t index = 0U; index < g_shortcut_held.size(); ++index) {
            const auto action = static_cast<ShortcutAction>(index);
            const bool enabled = action != ShortcutAction::QuickRestart ||
                (allow_quick_restart && !blocked && quick_restart_enabled() &&
                 dkr::runtime::enhancements::modern_presentation_enabled());
            keyboard_shortcut_held[index] = enabled && include_keyboard &&
                ShortcutHeld(shortcut_keyboard[index], [&](int source) {
                    return KeyboardSourceHeld(keys, source);
                });
            controller_shortcut_held[index] = enabled &&
                ShortcutHeld(shortcut_controller[index], [&](int source) {
                    return ControllerSourceHeld(controller, source);
                });
            const bool held = keyboard_shortcut_held[index] ||
                              controller_shortcut_held[index];
            const bool was_held = g_shortcut_held[index].exchange(
                held, std::memory_order_acq_rel);
            if (held && !was_held) {
                g_shortcut_requested[index].store(true,
                                                   std::memory_order_release);
            }
        }
    }
    if (blocked) {
        // Keep sampling Controller 1 while the overlay is open so calibration
        // and both live preview bars remain truthful. Gameplay receives a
        // neutral sample until the overlay closes.
        return state;
    }
    const auto value = [&](Action action) {
        const BindingPair& binding = bindings[Index(action)];
        const auto controller_suppressed = [&](int source) {
            for (std::size_t index = 0U;
                 index < controller_shortcut_held.size(); ++index) {
                if (controller_shortcut_held[index] &&
                    ShortcutContains(shortcut_controller[index], source)) {
                    return true;
                }
            }
            return false;
        };
        const auto keyboard_suppressed = [&](int source) {
            for (std::size_t index = 0U;
                 index < keyboard_shortcut_held.size(); ++index) {
                if (keyboard_shortcut_held[index] &&
                    ShortcutContains(shortcut_keyboard[index], source)) {
                    return true;
                }
            }
            return false;
        };
        float result = controller_suppressed(binding.controller)
            ? 0.0F : SourceValue(controller, binding.controller);
        if (!controller_suppressed(binding.controller_secondary)) {
            result = std::max(result, SourceValue(
                controller, binding.controller_secondary));
        }
        if (keys != nullptr && binding.keyboard >= 0 && binding.keyboard < SDL_NUM_SCANCODES &&
            keys[binding.keyboard] != 0 &&
            !keyboard_suppressed(binding.keyboard)) {
            result = 1.0F;
        }
        return result;
    };
    const auto press = [&](Action action, std::uint16_t mask, float threshold = 0.5F) {
        if (value(action) > threshold) {
            state.buttons |= mask;
        }
    };
    press(Action::A, kButtonA);
    press(Action::B, kButtonB);
    press(Action::Z, kButtonZ,
          dkr::runtime::enhancements::modern_presentation_enabled()
              ? trigger_threshold() : 0.5F);
    press(Action::Start, kButtonStart);
    press(Action::DpadUp, kDpadUp);
    press(Action::DpadDown, kDpadDown);
    press(Action::DpadLeft, kDpadLeft);
    press(Action::DpadRight, kDpadRight);
    press(Action::L, kButtonL);
    press(Action::R, kButtonR);
    press(Action::CUp, kCUp);
    press(Action::CDown, kCDown);
    press(Action::CLeft, kCLeft);
    press(Action::CRight, kCRight);
    state.camera_x = std::clamp(value(Action::CRight) - value(Action::CLeft), -1.0F, 1.0F);
    state.camera_y = std::clamp(value(Action::CUp) - value(Action::CDown), -1.0F, 1.0F);
    state.look_back = value(Action::LookBack) > 0.5F;
    state.orbit_x=std::clamp(value(Action::OrbitLeft)-value(Action::OrbitRight),-1.F,1.F);
    state.orbit_y=std::clamp(value(Action::OrbitDown)-value(Action::OrbitUp),-1.F,1.F);
    const Action camera_keys[]={Action::LookBack,Action::CameraRecenter,Action::CameraCycle,
        Action::CameraZoomIn,Action::CameraZoomOut,Action::CameraFirstPerson,Action::CameraXrRecenter};
    for(unsigned i=0;i<7;++i)if(value(camera_keys[i])>0.5F)state.camera_actions|=1U<<i;
    state.accelerate = value(Action::Accelerate) >
        (dkr::runtime::enhancements::modern_presentation_enabled() ? trigger_threshold() : 0.5F);
    state.stick_x = std::clamp(value(Action::StickRight) - value(Action::StickLeft), -1.0F, 1.0F);
    state.stick_y = std::clamp(value(Action::StickUp) - value(Action::StickDown), -1.0F, 1.0F);
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        const auto vehicle = static_cast<VehicleClass>(
            g_active_vehicle[PlayerIndex(player)].load(
                std::memory_order_acquire));
        state.stick_x = ShapeStick(
            state.stick_x, vehicle_stick_x_inverted(vehicle));
        state.stick_y = ShapeStick(
            state.stick_y, vehicle_stick_y_inverted(vehicle));
    }
    if (gyro.has_value()) {
        state.stick_x = blend_gyro_steering(state.stick_x, gyro->x);
        state.stick_y = blend_gyro_steering(state.stick_y, gyro->y);
    }
#else
    (void)controller;
    (void)gyro_controller;
    (void)include_keyboard;
    (void)blocked;
    (void)allow_quick_restart;
    (void)global_shortcut_owner;
#endif
    return state;
}

dkr::runtime::input::State dkr::runtime::input::poll_snapshot(
    std::size_t player,
    const controllers::ControllerSnapshot* controller,
    const controllers::ControllerSnapshot* gyro_controller,
    bool include_keyboard, bool blocked, bool allow_quick_restart,
    bool global_shortcut_owner) {
    State state{};
#if DKR_RUNTIME_HAS_RT64
    const std::optional<GyroSample> gyro = owns_gyro_accumulator(
                                               gyro_controller != nullptr)
        ? PollSnapshotGyro(player, gyro_controller)
        : std::nullopt;
    std::array<BindingPair, static_cast<std::size_t>(Action::Count)> bindings;
    {
        std::scoped_lock lock(g_binding_mutex);
        bindings = g_bindings[PlayerIndex(player)];
    }
    const Uint8* keys = include_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    ShortcutBindings shortcut_keyboard;
    ShortcutBindings shortcut_controller;
    {
        std::scoped_lock lock(g_shortcut_mutex);
        shortcut_keyboard = g_shortcut_keyboard;
        shortcut_controller = g_shortcut_controller;
    }
    std::array<bool, static_cast<std::size_t>(ShortcutAction::Count)>
        keyboard_shortcut_held{};
    std::array<bool, static_cast<std::size_t>(ShortcutAction::Count)>
        controller_shortcut_held{};
    if (global_shortcut_owner) {
        for (std::size_t index = 0U; index < g_shortcut_held.size(); ++index) {
            const auto action = static_cast<ShortcutAction>(index);
            const bool enabled = action != ShortcutAction::QuickRestart ||
                (allow_quick_restart && !blocked && quick_restart_enabled() &&
                 dkr::runtime::enhancements::modern_presentation_enabled());
            keyboard_shortcut_held[index] = enabled && include_keyboard &&
                ShortcutHeld(shortcut_keyboard[index], [&](int source) {
                    return KeyboardSourceHeld(keys, source);
                });
            controller_shortcut_held[index] = enabled &&
                ShortcutHeld(shortcut_controller[index], [&](int source) {
                    return SnapshotControllerSourceHeld(controller, source);
                });
            const bool held = keyboard_shortcut_held[index] ||
                              controller_shortcut_held[index];
            const bool was_held = g_shortcut_held[index].exchange(
                held, std::memory_order_acq_rel);
            if (held && !was_held) {
                g_shortcut_requested[index].store(true,
                                                   std::memory_order_release);
            }
        }
    }
    if (blocked) return state;
    const auto value = [&](Action action) {
        const BindingPair& binding = bindings[Index(action)];
        const auto controller_suppressed = [&](int source) {
            for (std::size_t index = 0U;
                 index < controller_shortcut_held.size(); ++index) {
                if (controller_shortcut_held[index] &&
                    ShortcutContains(shortcut_controller[index], source)) {
                    return true;
                }
            }
            return false;
        };
        const auto keyboard_suppressed = [&](int source) {
            for (std::size_t index = 0U;
                 index < keyboard_shortcut_held.size(); ++index) {
                if (keyboard_shortcut_held[index] &&
                    ShortcutContains(shortcut_keyboard[index], source)) {
                    return true;
                }
            }
            return false;
        };
        float result = controller_suppressed(binding.controller)
            ? 0.0F : SnapshotSourceValue(controller, binding.controller);
        if (!controller_suppressed(binding.controller_secondary)) {
            result = std::max(result, SnapshotSourceValue(
                controller, binding.controller_secondary));
        }
        if (keys != nullptr && binding.keyboard >= 0 &&
            binding.keyboard < SDL_NUM_SCANCODES && keys[binding.keyboard] != 0 &&
            !keyboard_suppressed(binding.keyboard)) {
            result = 1.0F;
        }
        return result;
    };
    const auto press = [&](Action action, std::uint16_t mask,
                           float threshold = 0.5F) {
        if (value(action) > threshold) state.buttons |= mask;
    };
    press(Action::A, kButtonA);
    press(Action::B, kButtonB);
    press(Action::Z, kButtonZ,
          dkr::runtime::enhancements::modern_presentation_enabled()
              ? trigger_threshold() : 0.5F);
    press(Action::Start, kButtonStart);
    press(Action::DpadUp, kDpadUp);
    press(Action::DpadDown, kDpadDown);
    press(Action::DpadLeft, kDpadLeft);
    press(Action::DpadRight, kDpadRight);
    press(Action::L, kButtonL);
    press(Action::R, kButtonR);
    press(Action::CUp, kCUp);
    press(Action::CDown, kCDown);
    press(Action::CLeft, kCLeft);
    press(Action::CRight, kCRight);
    state.camera_x = std::clamp(value(Action::CRight) - value(Action::CLeft),
                                -1.0F, 1.0F);
    state.camera_y = std::clamp(value(Action::CUp) - value(Action::CDown),
                                -1.0F, 1.0F);
    state.look_back = value(Action::LookBack) > 0.5F;
    state.orbit_x=std::clamp(value(Action::OrbitLeft)-value(Action::OrbitRight),-1.F,1.F);
    state.orbit_y=std::clamp(value(Action::OrbitDown)-value(Action::OrbitUp),-1.F,1.F);
    const Action camera_keys[]={Action::LookBack,Action::CameraRecenter,Action::CameraCycle,
        Action::CameraZoomIn,Action::CameraZoomOut,Action::CameraFirstPerson,Action::CameraXrRecenter};
    for(unsigned i=0;i<7;++i)if(value(camera_keys[i])>0.5F)state.camera_actions|=1U<<i;
    state.accelerate = value(Action::Accelerate) >
        (dkr::runtime::enhancements::modern_presentation_enabled() ? trigger_threshold() : 0.5F);
    state.stick_x = std::clamp(value(Action::StickRight) -
                               value(Action::StickLeft), -1.0F, 1.0F);
    state.stick_y = std::clamp(value(Action::StickUp) -
                               value(Action::StickDown), -1.0F, 1.0F);
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        const auto vehicle = static_cast<VehicleClass>(
            g_active_vehicle[PlayerIndex(player)].load(std::memory_order_acquire));
        state.stick_x = ShapeStick(
            state.stick_x, vehicle_stick_x_inverted(vehicle));
        state.stick_y = ShapeStick(
            state.stick_y, vehicle_stick_y_inverted(vehicle));
    }
    if (gyro.has_value()) {
        state.stick_x = blend_gyro_steering(state.stick_x, gyro->x);
        state.stick_y = blend_gyro_steering(state.stick_y, gyro->y);
    }
#else
    (void)player;
    (void)controller;
    (void)gyro_controller;
    (void)include_keyboard;
    (void)blocked;
    (void)allow_quick_restart;
    (void)global_shortcut_owner;
#endif
    return state;
}
