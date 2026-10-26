#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dkr::runtime::water::enhanced {
using LavaEmitter = std::array<float, 4>; // world XYZ, area-equivalent radius
inline constexpr unsigned kMaximumLavaEmitters = 16;
inline void add_lava_emitter(std::vector<LavaEmitter>& emitters, LavaEmitter value) {
    for (float v : value)
        if (!std::isfinite(v))
            return;
    if (value[3] <= 0 || value[3] > 1200)
        return;
    for (unsigned k = 0; k < 3; ++k)
        if (std::abs(value[k]) > 10000000)
            return;
    emitters.push_back(value);
    if (emitters.size() <= kMaximumLavaEmitters)
        return;
    // Merge the closest pair, rather than truncating one end of a river.
    unsigned a = 0, b = 1;
    float closest = INFINITY;
    for (unsigned i = 0; i < emitters.size(); ++i)
        for (unsigned j = i + 1; j < emitters.size(); ++j) {
            float d = 0;
            for (unsigned k = 0; k < 3; ++k)
                d += (emitters[i][k] - emitters[j][k]) * (emitters[i][k] - emitters[j][k]);
            if (d < closest) {
                closest = d;
                a = i;
                b = j;
            }
        }
    const float wa = emitters[a][3] * emitters[a][3], wb = emitters[b][3] * emitters[b][3];
    for (unsigned k = 0; k < 3; ++k)
        emitters[a][k] = (emitters[a][k] * wa + emitters[b][k] * wb) / (wa + wb);
    emitters[a][3] = std::min(1200.0F, std::sqrt(wa + wb));
    emitters.erase(emitters.begin() + b);
}
inline float lava_cooling(float seconds, float strength, float speed) {
    return std::clamp(strength, 0.0F, 1.0F) * 0.5F * (1.0F - std::sin(seconds * speed * 1.7F));
}
inline float lava_pulse(float seconds, float strength, float speed) {
    // The surface changes heat palette, never fades out. Nearby light only
    // breathes gently, including at maximum pulse strength.
    return 1.0F - 0.2F * lava_cooling(seconds, strength, speed);
}
} // namespace dkr::runtime::water::enhanced
