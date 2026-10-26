#include "presentation_policy.hpp"

#include <cstdio>

using dkr::runtime::enhancements::PresentationProfile;
using dkr::runtime::enhancements::clamp_presentation_rate;
using dkr::runtime::enhancements::clamp_texture_lod_bias_hundredths;
using dkr::runtime::enhancements::interpolation_allowed;
using dkr::runtime::enhancements::interpolation_allowed_for_camera;
using dkr::runtime::enhancements::is_finish_camera_mode;
using dkr::runtime::enhancements::fit_to_window_allowed;
using dkr::runtime::enhancements::graphics_api_selection_allowed;
using dkr::runtime::enhancements::kCurrentSettingsVersion;
using dkr::runtime::enhancements::maximum_detail_effective;
using dkr::runtime::enhancements::modern_options_visible;
using dkr::runtime::enhancements::multiplayer_race_music_effective;
using dkr::runtime::enhancements::normalise_presentation_profile;
using dkr::runtime::enhancements::resolve_settings_profile;
using dkr::runtime::enhancements::resolve_effective_presentation_rate;

static_assert(normalise_presentation_profile(-1) == PresentationProfile::Accurate);
static_assert(normalise_presentation_profile(0) == PresentationProfile::Accurate);
static_assert(normalise_presentation_profile(1) == PresentationProfile::Modern);
static_assert(normalise_presentation_profile(2) == PresentationProfile::Accurate);
static_assert(clamp_presentation_rate(1) == 30);
static_assert(clamp_presentation_rate(120) == 120);
static_assert(clamp_presentation_rate(1000) == 500);
static_assert(clamp_texture_lod_bias_hundredths(-201) == -200);
static_assert(clamp_texture_lod_bias_hundredths(-125) == -125);
static_assert(clamp_texture_lod_bias_hundredths(0) == 0);
static_assert(clamp_texture_lod_bias_hundredths(175) == 175);
static_assert(clamp_texture_lod_bias_hundredths(201) == 200);
static_assert(!maximum_detail_effective(PresentationProfile::Accurate, true));
static_assert(maximum_detail_effective(PresentationProfile::Modern, true));
static_assert(!maximum_detail_effective(PresentationProfile::Modern, false));
static_assert(!multiplayer_race_music_effective(PresentationProfile::Accurate, true));
static_assert(multiplayer_race_music_effective(PresentationProfile::Modern, true));
static_assert(!multiplayer_race_music_effective(PresentationProfile::Modern, false));
static_assert(!interpolation_allowed(PresentationProfile::Accurate));
static_assert(interpolation_allowed(PresentationProfile::Modern));
static_assert(is_finish_camera_mode(5));
static_assert(is_finish_camera_mode(7));
static_assert(!is_finish_camera_mode(0));
static_assert(!is_finish_camera_mode(6));
static_assert(!interpolation_allowed_for_camera(PresentationProfile::Accurate, 0));
static_assert(interpolation_allowed_for_camera(PresentationProfile::Modern, 0));
static_assert(interpolation_allowed_for_camera(PresentationProfile::Modern, 5));
static_assert(interpolation_allowed_for_camera(PresentationProfile::Modern, 7));
static_assert(!modern_options_visible(PresentationProfile::Accurate));
static_assert(modern_options_visible(PresentationProfile::Modern));
static_assert(!fit_to_window_allowed(PresentationProfile::Accurate));
static_assert(fit_to_window_allowed(PresentationProfile::Modern));
static_assert(!graphics_api_selection_allowed(PresentationProfile::Accurate));
static_assert(graphics_api_selection_allowed(PresentationProfile::Modern));
static_assert(resolve_effective_presentation_rate(PresentationProfile::Accurate, true, 500, 144) == 30);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, false, 500, 144) == 144);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 500, 60) == 500);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 20, 60) == 30);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 30, 60) == 30);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 50, 60) == 50);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 60, 60) == 60);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 75, 60) == 75);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 90, 60) == 90);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 100, 60) == 100);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 120, 60) == 120);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 144, 60) == 144);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 165, 60) == 165);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 240, 60) == 240);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 360, 60) == 360);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 480, 60) == 480);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, true, 500, 60) == 500);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, false, 30, 59) == 59);
static_assert(resolve_effective_presentation_rate(PresentationProfile::Modern, false, 30, 500) == 500);
static_assert(kCurrentSettingsVersion == 9);
static_assert(resolve_settings_profile(0, false, PresentationProfile::Modern) == PresentationProfile::Accurate);
static_assert(resolve_settings_profile(3, true, PresentationProfile::Modern) == PresentationProfile::Accurate);
static_assert(resolve_settings_profile(4, true, PresentationProfile::Modern) == PresentationProfile::Accurate);
static_assert(resolve_settings_profile(5, true, PresentationProfile::Modern) == PresentationProfile::Accurate);
static_assert(resolve_settings_profile(6, false, PresentationProfile::Modern) == PresentationProfile::Accurate);
static_assert(resolve_settings_profile(6, true, PresentationProfile::Modern) == PresentationProfile::Modern);
static_assert(resolve_settings_profile(7, true, PresentationProfile::Modern) == PresentationProfile::Modern);
static_assert(resolve_settings_profile(8, true, PresentationProfile::Modern) == PresentationProfile::Modern);
static_assert(resolve_settings_profile(9, true, PresentationProfile::Modern) == PresentationProfile::Modern);
static_assert(resolve_settings_profile(7, true, static_cast<PresentationProfile>(99)) == PresentationProfile::Accurate);

int main() {
    std::puts("[test][presentation-policy] PASS");
    return 0;
}
