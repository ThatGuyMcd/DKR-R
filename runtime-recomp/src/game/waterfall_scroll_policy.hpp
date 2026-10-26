#pragma once
#include <cmath>

namespace dkr::runtime::water {
inline bool valid_waterfall_period(float period) {
    return std::isfinite(period) && period >= 1 && period <= 2048;
}
inline float waterfall_uv_velocity(float previous, float current, float period) {
    if (!valid_waterfall_period(period) || !std::isfinite(previous) || !std::isfinite(current))
        return 0;
    float delta = current - previous;
    if (std::abs(delta) > period * 0.5F)
        delta -= std::round(delta / period) * period;
    return delta;
}
} // namespace dkr::runtime::water
