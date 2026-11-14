#pragma once
#include "ultramodern/config.hpp"
#include <optional>

namespace dkr::runtime::mobile_graphics {
enum class Preset { Battery, Balanced, Quality, LowLoad30, LowLoad60 };
inline void apply_accurate_budget(ultramodern::renderer::GraphicsConfig& config) {
    using namespace ultramodern::renderer;
    config.res_option = Resolution::Original2x;
    config.hpfb_option = HighPrecisionFramebuffer::Off;
    config.ds_option = 1;
    config.msaa_option = Antialiasing::None;
}
inline void apply_preset(ultramodern::renderer::GraphicsConfig& config, Preset preset) {
    using namespace ultramodern::renderer;
    config.res_option = preset == Preset::Quality ? Resolution::Auto : Resolution::Original2x;
    if (preset == Preset::LowLoad30 || preset == Preset::LowLoad60)
        config.res_option = Resolution::Original;
    config.msaa_option = Antialiasing::None;
    config.ds_option = 1;
    config.rr_option = RefreshRate::Manual;
    config.rr_manual_value = (preset == Preset::Battery || preset == Preset::LowLoad30) ? 30 : 60;
    config.hpfb_option = preset == Preset::Quality ? HighPrecisionFramebuffer::On : HighPrecisionFramebuffer::Off;
}
// Applying a preset also sets texture filtering.
inline int preset_anisotropy(Preset preset) {
    return preset == Preset::Quality ? 16 : 4;
}
// The preset the saved settings currently match, or nullopt once any of its
// fields has been changed by hand. Low-load presets are only offered on Android.
inline std::optional<Preset> matching_preset(const ultramodern::renderer::GraphicsConfig& config, int anisotropy,
                                             bool include_low_load) {
    const int count = include_low_load ? 5 : 3;
    for (int index = 0; index != count; ++index) {
        const auto preset = static_cast<Preset>(index);
        auto expected = config;
        apply_preset(expected, preset);
        if (config.res_option == expected.res_option && config.msaa_option == expected.msaa_option &&
            config.ds_option == expected.ds_option && config.rr_option == expected.rr_option &&
            config.rr_manual_value == expected.rr_manual_value && config.hpfb_option == expected.hpfb_option &&
            anisotropy == preset_anisotropy(preset))
            return preset;
    }
    return std::nullopt;
}
// Explicit opt-in only. Never overwrite saved settings on app upgrade.
// Retain the existing aspect ratio, framebuffer precision and HUD choices.
inline void apply_60_fps_preset(ultramodern::renderer::GraphicsConfig& config) {
    using namespace ultramodern::renderer;
    config.res_option = Resolution::Original2x;
    config.msaa_option = Antialiasing::None;
    config.ds_option = 1;
    config.rr_option = RefreshRate::Manual;
    config.rr_manual_value = 60;
}
} // namespace dkr::runtime::mobile_graphics
