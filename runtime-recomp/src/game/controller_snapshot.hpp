#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::controllers {

// DKR-R stores controller bindings using SDL2's stable game-controller button
// and axis numbering. Keep that serialized numbering at the application
// boundary so existing settings continue to work with either backend. SDL3's
// corresponding south/east/west/north and six standard axes use the same
// leading indices; the input host performs the explicit translation.
inline constexpr std::size_t kSnapshotButtonCount = 26U;
inline constexpr std::size_t kSnapshotAxisCount = 6U;

enum class StandardButton : std::size_t {
    South = 0U,
    East = 1U,
    West = 2U,
    North = 3U,
    Back = 4U,
    Guide = 5U,
    Start = 6U,
    LeftStick = 7U,
    RightStick = 8U,
    LeftShoulder = 9U,
    RightShoulder = 10U,
    DpadUp = 11U,
    DpadDown = 12U,
    DpadLeft = 13U,
    DpadRight = 14U,
};

enum class StandardAxis : std::size_t {
    LeftX = 0U,
    LeftY = 1U,
    RightX = 2U,
    RightY = 3U,
    LeftTrigger = 4U,
    RightTrigger = 5U,
};

struct GyroSnapshot {
    bool available = false;
    bool valid = false;
    std::uint64_t sensor_timestamp_us = 0U;
    float sample_rate_hz = 0.0F;
    std::array<float, 3> data{};
};

struct ControllerSnapshot {
    bool connected = false;
    std::array<std::uint8_t, kSnapshotButtonCount> buttons{};
    std::array<std::uint8_t, kSnapshotButtonCount> button_bound{};
    std::array<std::int16_t, kSnapshotAxisCount> axes{};
    GyroSnapshot gyro{};
};

// Diagnostic button readout; a disconnected device cannot retain stale presses.
inline std::uint32_t pressed_button_mask(const ControllerSnapshot& snapshot) {
    static_assert(kSnapshotButtonCount <= 32U);
    if (!snapshot.connected) return 0U;
    std::uint32_t mask = 0U;
    for (std::size_t index = 0; index < snapshot.buttons.size(); ++index)
        if (snapshot.buttons[index] != 0U) mask |= std::uint32_t{1} << index;
    return mask;
}

} // namespace dkr::runtime::controllers
