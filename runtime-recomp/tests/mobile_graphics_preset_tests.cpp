#include "../src/game/mobile_graphics_preset.hpp"
#include <iostream>

int main() {
    using namespace ultramodern::renderer;
    GraphicsConfig accurate{};
    accurate.ar_option = AspectRatio::Expand;
    accurate.hr_option = HUDRatioMode::Original;
    accurate.api_option = GraphicsApi::Vulkan;
    accurate.rr_option = RefreshRate::Manual;
    accurate.rr_manual_value = 30;
    accurate.res_option = Resolution::Auto;
    accurate.hpfb_option = HighPrecisionFramebuffer::Auto;
    accurate.ds_option = 4;
    accurate.msaa_option = Antialiasing::MSAA8X;
    dkr::runtime::mobile_graphics::apply_accurate_budget(accurate);
    if (accurate.res_option != Resolution::Original2x || accurate.ds_option != 1 ||
        accurate.hpfb_option != HighPrecisionFramebuffer::Off || accurate.msaa_option != Antialiasing::None ||
        accurate.rr_manual_value != 30 || accurate.rr_option != RefreshRate::Manual ||
        accurate.ar_option != AspectRatio::Expand || accurate.hr_option != HUDRatioMode::Original ||
        accurate.api_option != GraphicsApi::Vulkan) return 5;
    for (const auto precision : {HighPrecisionFramebuffer::Auto,
                                 HighPrecisionFramebuffer::On,
                                 HighPrecisionFramebuffer::Off}) {
        for (const auto aspect : {AspectRatio::Original, AspectRatio::Expand}) {
            GraphicsConfig config{};
            config.res_option = Resolution::Auto;
            config.msaa_option = Antialiasing::MSAA8X;
            config.ds_option = 4;
            config.rr_option = RefreshRate::Display;
            config.rr_manual_value = 120;
            config.hpfb_option = precision;
            config.ar_option = aspect;
            config.hr_option = HUDRatioMode::Original;
            config.api_option = GraphicsApi::Vulkan;
            config.wm_option = WindowMode::Fullscreen;
            for (int repeat = 0; repeat != 2; ++repeat) {
                dkr::runtime::mobile_graphics::apply_60_fps_preset(config);
                if (config.res_option != Resolution::Original2x ||
                    config.msaa_option != Antialiasing::None || config.ds_option != 1 ||
                    config.rr_option != RefreshRate::Manual || config.rr_manual_value != 60)
                    return 1;
                if (config.hpfb_option != precision || config.ar_option != aspect ||
                    config.hr_option != HUDRatioMode::Original ||
                    config.api_option != GraphicsApi::Vulkan ||
                    config.wm_option != WindowMode::Fullscreen) return 2;
            }
        }
    }
    for (auto preset : {dkr::runtime::mobile_graphics::Preset::Battery,
                        dkr::runtime::mobile_graphics::Preset::Balanced,
                        dkr::runtime::mobile_graphics::Preset::Quality}) {
        GraphicsConfig config{};
        config.ar_option = AspectRatio::Expand;
        config.hr_option = HUDRatioMode::Original;
        config.api_option = GraphicsApi::Vulkan;
        dkr::runtime::mobile_graphics::apply_preset(config, preset);
        if (config.ar_option != AspectRatio::Expand || config.hr_option != HUDRatioMode::Original ||
            config.api_option != GraphicsApi::Vulkan || config.ds_option != 1 || config.msaa_option != Antialiasing::None) return 3;
        const bool quality = preset == dkr::runtime::mobile_graphics::Preset::Quality;
        if (config.res_option != (quality ? Resolution::Auto : Resolution::Original2x) ||
            config.hpfb_option != (quality ? HighPrecisionFramebuffer::On : HighPrecisionFramebuffer::Off) ||
            config.rr_manual_value != (preset == dkr::runtime::mobile_graphics::Preset::Battery ? 30 : 60)) return 4;
    }
    for (auto preset : {dkr::runtime::mobile_graphics::Preset::LowLoad30,
                        dkr::runtime::mobile_graphics::Preset::LowLoad60}) {
        GraphicsConfig config{};
        config.ar_option = AspectRatio::Expand;
        config.hr_option = HUDRatioMode::Original;
        dkr::runtime::mobile_graphics::apply_preset(config, preset);
        if (config.res_option != Resolution::Original || config.ds_option != 1 ||
            config.msaa_option != Antialiasing::None || config.hpfb_option != HighPrecisionFramebuffer::Off ||
            config.ar_option != AspectRatio::Expand || config.hr_option != HUDRatioMode::Original ||
            config.rr_manual_value != (preset == dkr::runtime::mobile_graphics::Preset::LowLoad30 ? 30 : 60)) return 5;
    }
    std::cout << "PASS: explicit performance budgets and HUD/API/aspect preservation\n";
}
