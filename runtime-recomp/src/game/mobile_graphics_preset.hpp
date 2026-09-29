#pragma once
#include "ultramodern/config.hpp"

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
    if (preset == Preset::LowLoad30 || preset == Preset::LowLoad60) config.res_option = Resolution::Original;
    config.msaa_option = Antialiasing::None;
    config.ds_option = 1;
    config.rr_option = RefreshRate::Manual;
    config.rr_manual_value = (preset == Preset::Battery || preset == Preset::LowLoad30) ? 30 : 60;
    config.hpfb_option = preset == Preset::Quality ? HighPrecisionFramebuffer::On : HighPrecisionFramebuffer::Off;
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
}
