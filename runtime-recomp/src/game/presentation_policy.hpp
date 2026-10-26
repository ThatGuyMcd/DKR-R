#pragma once

#include <cstdint>

namespace dkr::runtime::enhancements {

enum class PresentationProfile : std::uint8_t {
    Accurate = 0,
    Modern = 1,
};

inline constexpr int kMinimumPresentationRate = 30;
inline constexpr int kMaximumPresentationRate = 500;
inline constexpr int kMinimumTextureLodBiasHundredths = -200;
inline constexpr int kMaximumTextureLodBiasHundredths = 200;
inline constexpr int kCurrentSettingsVersion = 9;
inline constexpr int kOldestCompatibleSettingsVersion = 6;

constexpr PresentationProfile normalise_presentation_profile(int value) {
    return value == static_cast<int>(PresentationProfile::Modern) ? PresentationProfile::Modern
                                                                  : PresentationProfile::Accurate;
}

constexpr PresentationProfile normalise_presentation_profile(PresentationProfile profile) {
    return normalise_presentation_profile(static_cast<int>(profile));
}

constexpr int clamp_presentation_rate(int rate) {
    return rate < kMinimumPresentationRate   ? kMinimumPresentationRate
           : rate > kMaximumPresentationRate ? kMaximumPresentationRate
                                             : rate;
}

constexpr int clamp_texture_lod_bias_hundredths(int bias) {
    return bias < kMinimumTextureLodBiasHundredths   ? kMinimumTextureLodBiasHundredths
           : bias > kMaximumTextureLodBiasHundredths ? kMaximumTextureLodBiasHundredths
                                                     : bias;
}

constexpr bool maximum_detail_effective(PresentationProfile profile, bool requested) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern && requested;
}

constexpr bool multiplayer_race_music_effective(PresentationProfile profile, bool requested) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern && requested;
}

constexpr bool interpolation_allowed(PresentationProfile profile) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern;
}

// Finish shots can interpolate continuously within a spectator node. Actual
// changes of shot are separated by per-camera continuity epochs instead of
// disabling interpolation for every viewport in the entire submitted task.
inline constexpr int kCameraFinishChallenge = 5;
inline constexpr int kCameraFinishRace = 7;

constexpr bool is_finish_camera_mode(int camera_mode) {
    return camera_mode == kCameraFinishChallenge || camera_mode == kCameraFinishRace;
}

constexpr bool interpolation_allowed_for_camera(PresentationProfile profile, int /*camera_mode*/) {
    return interpolation_allowed(profile);
}

constexpr bool modern_options_visible(PresentationProfile profile) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern;
}

constexpr bool fit_to_window_allowed(PresentationProfile profile) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern;
}

constexpr bool graphics_api_selection_allowed(PresentationProfile profile) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern;
}

constexpr int resolve_effective_presentation_rate(PresentationProfile profile, bool manual_target, int requested_rate,
                                                  int display_rate) {
    if (!interpolation_allowed(profile)) {
        return kMinimumPresentationRate;
    }
    return clamp_presentation_rate(manual_target ? requested_rate : display_rate);
}

constexpr PresentationProfile resolve_settings_profile(int settings_version, bool settings_complete,
                                                       PresentationProfile requested_profile) {
    return settings_version >= kOldestCompatibleSettingsVersion && settings_version <= kCurrentSettingsVersion &&
                   settings_complete
               ? normalise_presentation_profile(requested_profile)
               : PresentationProfile::Accurate;
}

} // namespace dkr::runtime::enhancements
