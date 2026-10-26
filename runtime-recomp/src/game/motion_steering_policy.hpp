#pragma once

#include <algorithm>
#include <cmath>

namespace dkr::runtime::input {

inline constexpr float kRadiansPerDegree = 0.01745329251994329577F;

constexpr bool owns_gyro_accumulator(bool primary_player) {
    return primary_player;
}

constexpr float clamp_gyro_sensitivity(float percent) {
    return std::clamp(percent, 25.0F, 300.0F);
}

constexpr float clamp_gyro_deadzone(float degrees_per_second) {
    return std::clamp(degrees_per_second, 0.0F, 12.0F);
}

inline float gyro_angular_velocity(float radians_per_second, float bias, float deadzone_degrees_per_second,
                                   bool inverted) {
    float value = radians_per_second - bias;
    const float deadzone = clamp_gyro_deadzone(deadzone_degrees_per_second) * kRadiansPerDegree;
    if (std::fabs(value) <= deadzone) {
        return 0.0F;
    }
    value = std::copysign(std::fabs(value) - deadzone, value);
    // SDL's positive roll/yaw direction is opposite the natural right-turn
    // direction for the controller orientations offered by the launcher.
    // Invert is an explicit user reversal of this corrected default.
    return inverted ? value : -value;
}

inline float integrate_gyro_angle(float angle_radians, float radians_per_second, float bias,
                                  float deadzone_degrees_per_second, float delta_seconds, bool inverted) {
    const float velocity = gyro_angular_velocity(radians_per_second, bias, deadzone_degrees_per_second, inverted);
    constexpr float kMaximumIntegratedAngle = 90.0F * kRadiansPerDegree;
    return std::clamp(angle_radians + velocity * std::clamp(delta_seconds, 0.0F, 0.1F), -kMaximumIntegratedAngle,
                      kMaximumIntegratedAngle);
}

inline float gyro_angle_to_steering(float angle_radians, float sensitivity_percent) {
    // At 100%, holding the controller at 45 degrees produces full steering.
    // Sensitivity changes the contribution without changing the integrated
    // physical angle, so the controller remains wheel-like and predictable.
    constexpr float kFullSteerAngle = 45.0F * kRadiansPerDegree;
    const float scaled = angle_radians * (clamp_gyro_sensitivity(sensitivity_percent) / 100.0F) / kFullSteerAngle;
    return std::clamp(scaled, -1.0F, 1.0F);
}

constexpr float blend_gyro_steering(float analogue, float gyro) {
    return std::clamp(analogue + gyro, -1.0F, 1.0F);
}

constexpr float gyro_sample_delta_seconds(unsigned long long sensor_timestamp_us,
                                          unsigned long long previous_sensor_timestamp_us, float host_delta_seconds,
                                          bool have_previous_sample) {
    if (!have_previous_sample)
        return 0.0F;
    if (sensor_timestamp_us != 0ULL && previous_sensor_timestamp_us != 0ULL) {
        if (sensor_timestamp_us == previous_sensor_timestamp_us) {
            // The game can poll faster than the IMU. Never integrate the same
            // physical reading twice merely because presentation is faster.
            return 0.0F;
        }
        if (sensor_timestamp_us > previous_sensor_timestamp_us) {
            return std::clamp(static_cast<float>(sensor_timestamp_us - previous_sensor_timestamp_us) / 1000000.0F, 0.0F,
                              0.1F);
        }
    }
    return std::clamp(host_delta_seconds, 0.0F, 0.1F);
}

} // namespace dkr::runtime::input
