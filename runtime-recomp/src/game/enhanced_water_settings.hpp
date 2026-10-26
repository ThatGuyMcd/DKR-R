#pragma once
#include "enhanced_water_policy.hpp"
#include <atomic>
#include <string>

namespace dkr::runtime::water::enhanced {
// Presentation preferences are not guest data/checkpoint inputs.
inline std::atomic<Quality> requested_quality{Quality::Original};
inline std::atomic<Reflection> requested_reflection{Reflection::ScreenSpace};
inline std::atomic<bool> wakes_enabled{true};
inline std::atomic<bool> spray_enabled{true};
// -1 preserves the pre-tuning Enhanced/High behaviour on existing installs.
inline std::atomic<int> reflection_detail{-1}, wake_detail{-1}, spray_detail{-1};
inline std::atomic<int> reflection_strength{100}, reflection_distance{100};
inline std::atomic<int> roughness_scale{100}, ripple_scale{100}, foam_scale{100};
inline std::atomic<int> wake_strength{100}, clarity_scale{100}, refraction_scale{100};
inline std::atomic<int> waterfall_detail{100};
inline std::atomic<int> lava_lighting{100}, lava_pulse_strength{35}, lava_pulse_speed{100}, lava_light_range{100};
inline std::atomic<int> lava_lighting_enabled{1}, lava_lighting_quality{1};
// Compatibility keys only. All nearby lava glow now uses the cached path.
// Keep intensity/off preferences; old ray-mode selections cannot restore
// the expensive full-viewport lighting pass.
inline std::atomic<int> lava_lighting_mode{0};
inline std::atomic<int> lava_surface_brightness{100};
inline std::atomic<bool> use_track_colours{true};
inline std::atomic<int> tint_red{100}, tint_green{100}, tint_blue{100};
struct Setting {
    const char* key;
    std::atomic<int>* value;
    int low, high;
};
inline const Setting tuning_settings[] = {{"enhanced_water_reflection_detail", &reflection_detail, -1, 2},
                                          {"enhanced_water_wake_detail", &wake_detail, -1, 2},
                                          {"enhanced_water_spray_detail", &spray_detail, -1, 2},
                                          {"enhanced_water_reflection_strength", &reflection_strength, 0, 200},
                                          {"enhanced_water_reflection_distance", &reflection_distance, 25, 200},
                                          {"enhanced_water_roughness", &roughness_scale, 25, 200},
                                          {"enhanced_water_ripples", &ripple_scale, 0, 200},
                                          {"enhanced_water_foam", &foam_scale, 0, 200},
                                          {"enhanced_water_wake_strength", &wake_strength, 0, 200},
                                          {"enhanced_water_clarity", &clarity_scale, 25, 200},
                                          {"enhanced_water_refraction", &refraction_scale, 0, 200},
                                          {"enhanced_water_waterfall_detail", &waterfall_detail, 0, 200},
                                          {"rtx_lava_lighting_intensity", &lava_lighting, 0, 300},
                                          {"rtx_lava_lighting_enabled", &lava_lighting_enabled, 0, 1},
                                          {"rtx_lava_lighting_quality", &lava_lighting_quality, 0, 2},
                                          {"lava_lighting_mode", &lava_lighting_mode, 0, 0},
                                          {"lava_surface_brightness", &lava_surface_brightness, 25, 200},
                                          {"rtx_lava_pulse_strength", &lava_pulse_strength, 0, 100},
                                          {"rtx_lava_pulse_speed", &lava_pulse_speed, 25, 200},
                                          {"rtx_lava_light_range", &lava_light_range, 25, 200},
                                          {"enhanced_water_tint_red", &tint_red, 0, 200},
                                          {"enhanced_water_tint_green", &tint_green, 0, 200},
                                          {"enhanced_water_tint_blue", &tint_blue, 0, 200}};
inline bool load_tuning(const std::string& key, int value) {
    for (const auto& setting : tuning_settings)
        if (key == setting.key) {
            setting.value->store(std::clamp(value, setting.low, setting.high));
            return true;
        }
    if (key == "enhanced_water_track_colours") {
        use_track_colours.store(value != 0);
        return true;
    }
    return false;
}
inline void reset_appearance() {
    reflection_strength = 100;
    roughness_scale = 100;
    ripple_scale = 100;
    foam_scale = 100;
    wake_strength = 100;
    clarity_scale = 100;
    refraction_scale = 100;
    waterfall_detail = 100;
    use_track_colours = true;
    tint_red = 100;
    tint_green = 100;
    tint_blue = 100;
    lava_lighting = 100;
    lava_pulse_strength = 35;
    lava_pulse_speed = 100;
    lava_light_range = 100;
    lava_lighting_enabled = 1;
    lava_lighting_quality = 1;
    lava_lighting_mode = 0;
    lava_surface_brightness = 100;
}
inline void reset_tuning() {
    reflection_detail = -1;
    wake_detail = -1;
    spray_detail = -1;
    reflection_distance = 100;
    wakes_enabled = true;
    spray_enabled = true;
    reset_appearance();
}
inline void apply_preset(int preset) {
    if (preset == 0) {
        requested_quality = Quality::Original;
        return;
    }
    if (preset < 1 || preset > 3)
        return;
    reset_tuning();
    requested_quality = preset == 3 ? Quality::High : Quality::Enhanced;
    reflection_detail = preset - 1;
    wake_detail = preset - 1;
    spray_detail = preset - 1;
    // A water preset never changes reflection method, game resolution or MSAA.
}
inline int preset() {
    const auto quality = requested_quality.load();
    if (quality == Quality::Original)
        return 0;
    const int automatic = quality == Quality::High ? 2 : 1;
    const auto detail = [&](const std::atomic<int>& setting) {
        const int value = setting.load();
        return value < 0 ? automatic : value;
    };
    const int d = detail(reflection_detail);
    if (detail(wake_detail) != d || detail(spray_detail) != d || !wakes_enabled || !spray_enabled ||
        reflection_distance != 100 || reflection_strength != 100 || roughness_scale != 100 || ripple_scale != 100 ||
        foam_scale != 100 || wake_strength != 100 || clarity_scale != 100 || refraction_scale != 100 ||
        waterfall_detail != 100 || lava_lighting != 100 || lava_pulse_strength != 35 || lava_pulse_speed != 100 ||
        lava_light_range != 100 || lava_lighting_enabled != 1 || lava_surface_brightness != 100 || !use_track_colours ||
        (d == 2) != (quality == Quality::High))
        return 4;
    return d + 1;
}
// -1 until a game renderer has reported the actual enabled device features.
// This describes capability, not a promise that every scene fits its budget.
inline std::atomic<int> hardware_ray_capability{-1};
inline std::atomic<int> active_reflection{-1};
inline std::atomic<unsigned> scratch_fallbacks{0};
inline std::atomic<bool> timing_enabled{false};
inline void set_quality(int value) {
    requested_quality.store(static_cast<Quality>(std::clamp(value, 0, 2)), std::memory_order_relaxed);
}
inline void set_reflection(int value) {
    requested_reflection.store(static_cast<Reflection>(std::clamp(value, 0, 2)), std::memory_order_relaxed);
}
} // namespace dkr::runtime::water::enhanced
