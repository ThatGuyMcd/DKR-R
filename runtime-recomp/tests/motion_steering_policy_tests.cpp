#include "motion_steering_policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime::input;

int main() {
    static_assert(owns_gyro_accumulator(true));
    static_assert(!owns_gyro_accumulator(false));
    static_assert(clamp_gyro_sensitivity(0.0F) == 25.0F);
    static_assert(clamp_gyro_sensitivity(100.0F) == 100.0F);
    static_assert(clamp_gyro_sensitivity(500.0F) == 300.0F);
    static_assert(clamp_gyro_deadzone(-1.0F) == 0.0F);
    static_assert(clamp_gyro_deadzone(20.0F) == 12.0F);
    static_assert(blend_gyro_steering(0.75F, 0.75F) == 1.0F);
    static_assert(blend_gyro_steering(-0.75F, -0.75F) == -1.0F);
    static_assert(gyro_sample_delta_seconds(20000ULL, 10000ULL, 0.5F, true) == 0.01F);
    static_assert(gyro_sample_delta_seconds(10000ULL, 10000ULL, 0.02F, true) == 0.0F);
    static_assert(gyro_sample_delta_seconds(0ULL, 0ULL, 0.02F, true) == 0.02F);
    static_assert(gyro_sample_delta_seconds(20000ULL, 10000ULL, 0.02F, false) == 0.0F);

    assert(gyro_angular_velocity(0.01F, 0.01F, 2.0F, false) == 0.0F);
    float right_angle = 0.0F;
    for (int sample = 0; sample < 10; ++sample) {
        right_angle = integrate_gyro_angle(right_angle, -45.0F * kRadiansPerDegree, 0.0F, 0.0F, 0.1F, false);
    }
    const float right = gyro_angle_to_steering(right_angle, 100.0F);
    assert(std::fabs(right - 1.0F) < 0.0001F);
    const float held_angle = integrate_gyro_angle(right_angle, 0.0F, 0.0F, 0.0F, 0.1F, false);
    assert(std::fabs(held_angle - right_angle) < 0.0001F);
    float centred = held_angle;
    for (int sample = 0; sample < 10; ++sample) {
        centred = integrate_gyro_angle(centred, 45.0F * kRadiansPerDegree, 0.0F, 0.0F, 0.1F, false);
    }
    assert(std::fabs(centred) < 0.0001F);
    float inverted = 0.0F;
    for (int sample = 0; sample < 10; ++sample) {
        inverted = integrate_gyro_angle(inverted, -45.0F * kRadiansPerDegree, 0.0F, 0.0F, 0.1F, true);
    }
    assert(std::fabs(gyro_angle_to_steering(inverted, 100.0F) + 1.0F) < 0.0001F);
    std::puts("[test][motion-steering-policy] PASS");
    return 0;
}
