#pragma once

#include "presentation_policy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::enhancements {

inline constexpr int kMinimumGameplayFov = 40;
inline constexpr int kMaximumGameplayFov = 100;
inline constexpr int kMinimumFovOffset = -20;
inline constexpr int kMaximumFovOffset = 20;
inline constexpr int kMinimumViewDistanceMultiplier = 1;
inline constexpr int kMaximumViewDistanceMultiplier = 8;
inline constexpr int kMinimumClassViewDistanceMultiplier = 1;
inline constexpr int kMaximumAnimatedSceneryMultiplier = 4;
inline constexpr int kMaximumBillboardEffectMultiplier = 4;
inline constexpr int kMaximumWaterLavaDetailMultiplier = 5;
inline constexpr int kMaximumWaveViewDistance = 5;
inline constexpr int kMinimumFrustumGuardPercent = 0;
inline constexpr int kMaximumFrustumGuardPercent = 20;
inline constexpr float kOriginalAspectRatio = 4.0F / 3.0F;
inline constexpr int kRaceTypeDefault = 0;
inline constexpr int kRaceTypeHorseshoeGulch = 3;
inline constexpr int kRaceTypeHubWorld = 5;
inline constexpr int kRaceTypeBoss = 8;
inline constexpr int kRaceTypeChallengeMask = 0x40;

enum class SceneryRetentionMode : int {
    Authored = 0,
    CurrentRegion = 1,
    VisibleAndAdjacent = 2,
    FullForwardView = 3,
};

constexpr SceneryRetentionMode normalise_scenery_retention_mode(int mode) {
    return static_cast<SceneryRetentionMode>(std::clamp(mode, static_cast<int>(SceneryRetentionMode::Authored),
                                                        static_cast<int>(SceneryRetentionMode::FullForwardView)));
}

constexpr int clamp_fov_offset(int offset) {
    return std::clamp(offset, kMinimumFovOffset, kMaximumFovOffset);
}

constexpr int effective_gameplay_fov(PresentationProfile profile, int authored_fov, int offset) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern) {
        return authored_fov;
    }
    return std::clamp(authored_fov + clamp_fov_offset(offset), kMinimumGameplayFov, kMaximumGameplayFov);
}

constexpr int clamp_view_distance_multiplier(int multiplier) {
    return std::clamp(multiplier, kMinimumViewDistanceMultiplier, kMaximumViewDistanceMultiplier);
}

constexpr int clamp_animated_scenery_multiplier(int multiplier) {
    return std::clamp(multiplier, kMinimumClassViewDistanceMultiplier, kMaximumAnimatedSceneryMultiplier);
}

constexpr int clamp_billboard_effect_multiplier(int multiplier) {
    return std::clamp(multiplier, kMinimumClassViewDistanceMultiplier, kMaximumBillboardEffectMultiplier);
}

constexpr int clamp_water_lava_detail_multiplier(int multiplier) {
    return std::clamp(multiplier, kMinimumClassViewDistanceMultiplier, kMaximumWaterLavaDetailMultiplier);
}

constexpr int effective_view_distance(PresentationProfile profile, int authored_distance, int multiplier) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern || authored_distance <= 0) {
        return authored_distance;
    }
    const int scaled = authored_distance * clamp_view_distance_multiplier(multiplier);
    return std::min(scaled, 32767);
}

// check_if_in_draw_range halves its register-local distance for layout 3.
// Compensate at that existing hook, without changing an object's authored
// header or its fade behaviour, so Modern's multiplier means the same in all
// four local viewports. Accurate retains the retail reduction.
constexpr int object_distance_before_multiplayer_reduction(PresentationProfile profile, int authored, int multiplier,
                                                           int layout) {
    const int distance = effective_view_distance(profile, authored, multiplier);
    return profile == PresentationProfile::Modern && authored > 0 && layout == 3 ? distance * 2 : distance;
}

constexpr bool is_gameplay_scenery_race_type(int race_type) {
    return race_type >= 0 &&
           (race_type == kRaceTypeDefault || race_type == kRaceTypeHorseshoeGulch || race_type == kRaceTypeHubWorld ||
            race_type == kRaceTypeBoss || (race_type & kRaceTypeChallengeMask) != 0);
}

constexpr bool is_hub_scenery_race_type(int race_type) {
    return race_type == kRaceTypeHubWorld;
}

constexpr bool is_minigame_scenery_race_type(int race_type) {
    return race_type >= 0 && (race_type & kRaceTypeChallengeMask) != 0;
}

constexpr bool is_track_or_boss_scenery_race_type(int race_type) {
    return race_type == kRaceTypeDefault || race_type == kRaceTypeHorseshoeGulch || race_type == kRaceTypeBoss;
}

constexpr bool scenery_retention_enabled_for_race_type(PresentationProfile profile, int race_type, bool keep_hub,
                                                       bool keep_track_and_boss, bool keep_minigame_and_battle) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern) {
        return false;
    }
    if (is_hub_scenery_race_type(race_type)) {
        return keep_hub;
    }
    if (is_minigame_scenery_race_type(race_type)) {
        return keep_minigame_and_battle;
    }
    return is_track_or_boss_scenery_race_type(race_type) && keep_track_and_boss;
}

// DKR allocates fixed selector and vertex pools for either a 3x3 or 5x5 HQ
// wave window. Five is the largest safe value; intermediate allocation sizes
// and values above five are deliberately never produced.
constexpr int effective_wave_view_distance(PresentationProfile profile, int detail_multiplier, int race_type,
                                           int authored_distance) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
                   clamp_water_lava_detail_multiplier(detail_multiplier) == kMaximumWaterLavaDetailMultiplier &&
                   is_gameplay_scenery_race_type(race_type)
               ? kMaximumWaveViewDistance
               : authored_distance;
}

constexpr bool persistent_water_override_enabled(PresentationProfile profile, int detail_multiplier, int race_type) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
           clamp_water_lava_detail_multiplier(detail_multiplier) >= 2 && is_gameplay_scenery_race_type(race_type);
}

constexpr int persistent_water_hq_fade(PresentationProfile profile, int detail_multiplier, bool selected_for_hq,
                                       int authored_fade) {
    return normalise_presentation_profile(profile) == PresentationProfile::Modern &&
                   clamp_water_lava_detail_multiplier(detail_multiplier) >= 2 && selected_for_hq
               ? 0
               : authored_fade;
}

constexpr bool relax_scenery_segment_bitfield(PresentationProfile profile, int race_type, SceneryRetentionMode mode,
                                              bool keep_hub, bool keep_track_and_boss, bool keep_minigame_and_battle) {
    return scenery_retention_enabled_for_race_type(profile, race_type, keep_hub, keep_track_and_boss,
                                                   keep_minigame_and_battle) &&
           (mode == SceneryRetentionMode::VisibleAndAdjacent || mode == SceneryRetentionMode::FullForwardView);
}

constexpr float scenery_retention_frustum_scale(PresentationProfile profile, int race_type, SceneryRetentionMode mode,
                                                int multiplier, bool keep_hub, bool keep_track_and_boss,
                                                bool keep_minigame_and_battle) {
    if (mode != SceneryRetentionMode::FullForwardView ||
        !scenery_retention_enabled_for_race_type(profile, race_type, keep_hub, keep_track_and_boss,
                                                 keep_minigame_and_battle)) {
        return 1.0F;
    }
    return 1.0F + 0.08F * static_cast<float>(clamp_view_distance_multiplier(multiplier) - 1);
}

// Behaviour IDs are the decompiled ObjectBehaviour enum. These allow the
// granular controls to raise the authored range of selected classes without
// multiplying every object in every scene.
constexpr bool is_animated_scenery_behaviour(int behaviour) {
    switch (behaviour) {
    case 12:
    case 14:
    case 36:
    case 38:
    case 40:
    case 50:
    case 67:
    case 68:
    case 75:
    case 81:
    case 84:
    case 85:
    case 86:
    case 96:
    case 97:
    case 101:
    case 102:
        return true;
    default:
        return false;
    }
}

constexpr bool is_billboard_or_effect_behaviour(int behaviour) {
    switch (behaviour) {
    case 3:
    case 6:
    case 17:
    case 22:
    case 23:
    case 28:
    case 32:
    case 37:
    case 41:
    case 43:
    case 45:
    case 47:
    case 57:
    case 61:
    case 64:
    case 73:
    case 76:
    case 77:
    case 78:
    case 80:
    case 82:
    case 87:
    case 88:
    case 89:
    case 90:
    case 93:
    case 95:
        return true;
    default:
        return false;
    }
}

constexpr bool is_water_or_lava_behaviour(int behaviour) {
    switch (behaviour) {
    case 40:
    case 57:
    case 59:
    case 60:
    case 67:
    case 68:
    case 75:
        return true;
    default:
        return false;
    }
}

constexpr int object_view_distance_multiplier(int behaviour, int overall_multiplier, int animated_multiplier,
                                              int billboard_multiplier, int water_lava_multiplier) {
    int result = clamp_view_distance_multiplier(overall_multiplier);
    if (is_animated_scenery_behaviour(behaviour)) {
        result = std::max(result, clamp_animated_scenery_multiplier(animated_multiplier));
    }
    if (is_billboard_or_effect_behaviour(behaviour)) {
        result = std::max(result, clamp_billboard_effect_multiplier(billboard_multiplier));
    }
    if (is_water_or_lava_behaviour(behaviour)) {
        result = std::max(result, clamp_water_lava_detail_multiplier(water_lava_multiplier));
    }
    return result;
}

constexpr int clamp_frustum_guard_percent(int percent) {
    return std::clamp(percent, kMinimumFrustumGuardPercent, kMaximumFrustumGuardPercent);
}

constexpr float active_viewport_aspect(float window_aspect, int viewport_layout) {
    return viewport_layout == 1 ? window_aspect * 2.0F : window_aspect;
}

constexpr float frustum_horizontal_scale(PresentationProfile profile, bool fit_to_window, bool extended_culling,
                                         float window_aspect, int viewport_layout, int guard_percent) {
    if (normalise_presentation_profile(profile) != PresentationProfile::Modern || !fit_to_window || !extended_culling ||
        window_aspect <= 0.0F) {
        return 1.0F;
    }
    const float aspect_scale =
        std::max(1.0F, active_viewport_aspect(window_aspect, viewport_layout) / kOriginalAspectRatio);
    const float guard = 1.0F + static_cast<float>(clamp_frustum_guard_percent(guard_percent)) / 100.0F;
    return aspect_scale * guard;
}

} // namespace dkr::runtime::enhancements
