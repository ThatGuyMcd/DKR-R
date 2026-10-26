#include "runtime_enhancements.hpp"
#include "runtime_netplay.hpp"
#include "detail_contract.hpp"

#include "character_select_animation_policy.hpp"
#include "character_select_music_policy.hpp"
#include "modern_camera_policy.hpp"
#include "presentation_identity.hpp"
#include "runtime_platform.hpp"
#include "revision_addresses.hpp"

#include "recomp.h"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

std::atomic<bool> g_maximum_detail_enabled{false};
std::atomic<dkr::runtime::enhancements::PresentationProfile> g_presentation_profile{
    dkr::runtime::enhancements::PresentationProfile::Accurate};
std::atomic<int> g_fov_offset{0};
std::atomic<int> g_view_distance_multiplier{2};
std::atomic<bool> g_keep_hub_scenery_enabled{false};
std::atomic<bool> g_keep_track_scenery_enabled{false};
std::atomic<bool> g_keep_minigame_scenery_enabled{false};
std::atomic<dkr::runtime::enhancements::SceneryRetentionMode> g_scenery_retention_mode{
    dkr::runtime::enhancements::SceneryRetentionMode::Authored};
std::atomic<int> g_animated_scenery_distance_multiplier{1};
std::atomic<int> g_billboard_effect_distance_multiplier{1};
std::atomic<int> g_water_lava_detail_multiplier{1};
std::atomic<bool> g_extended_culling_enabled{true};
std::atomic<int> g_frustum_guard_percent{5};
std::atomic<bool> g_fit_to_window_enabled{false};
std::atomic<int> g_anisotropy_level{16};
std::atomic<int> g_texture_lod_bias_hundredths{0};
std::atomic<bool> g_generated_mipmaps_requested{false};
std::atomic<bool> g_multiplayer_race_music_enabled{true};

const std::uint32_t& kBlockMusicChangeAddress = dkr::runtime::revision_addresses::BlockMusicChange;
const std::uint32_t& kMusicNextSequenceAddress = dkr::runtime::revision_addresses::MusicNextSequence;
const std::uint32_t& kDynamicMusicChannelMaskAddress = dkr::runtime::revision_addresses::DynamicMusicChannelMask;
const std::uint32_t& kMenuSelectedCharacterAddress = dkr::runtime::revision_addresses::MenuSelectedCharacter;
const std::uint32_t& kMenuCurrentCharacterAddress = dkr::runtime::revision_addresses::MenuCurrentCharacter;
const std::uint32_t& kMusicTempoAddress = dkr::runtime::revision_addresses::MusicTempo;
constexpr std::uint16_t kTimeTrialGhostBehaviour = 0x003AU;
const std::uint32_t& kFrustumReferenceAddress = dkr::runtime::revision_addresses::FrustumReference;
const std::uint32_t& kViewportLayoutAddress = dkr::runtime::revision_addresses::ViewportLayout;
const std::uint32_t& kCurrentLevelHeaderAddress = dkr::runtime::revision_addresses::CurrentLevelHeader;
constexpr std::uint32_t kLevelHeaderRaceTypeOffset = 0x4CU;
const std::uint32_t& kSceneActiveCameraAddress = dkr::runtime::revision_addresses::SceneActiveCamera;
const std::uint32_t& kWaveControllerAddress = dkr::runtime::revision_addresses::WaveController;
constexpr std::uint32_t kWaveViewDistanceOffset = 0x24U;
const std::uint32_t& kWaveSelectionMapAddress = dkr::runtime::revision_addresses::WaveSelectionMap;
const std::uint32_t& kWaveModelAddress = dkr::runtime::revision_addresses::WaveModel;
const std::uint32_t& kNumberOfLevelSegmentsAddress = dkr::runtime::revision_addresses::NumberOfLevelSegments;
constexpr std::uint32_t kWaveModelStride = 0x1CU;
constexpr std::uint32_t kWaveModelSelectionOffset = 0x0CU;
constexpr std::uint32_t kWaveModelFadeOffset = 0x14U;
constexpr std::uint32_t kWaveModelFadeBytes = 8U;
constexpr std::array<std::uint32_t, 4> kSideReferenceOffsets = {
    48U,
    60U,
    84U,
    96U,
};
gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
float ReadRdramFloat(std::uint8_t* rdram, std::uint32_t address) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(MEM_W(0, RdramAddress(address))));
}

void WriteRdramFloat(std::uint8_t* rdram, std::uint32_t address, float value) {
    MEM_W(0, RdramAddress(address)) = std::bit_cast<std::uint32_t>(value);
}

int CurrentLevelRaceType(std::uint8_t* rdram) {
    const std::uint32_t header_address = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(kCurrentLevelHeaderAddress)));
    if (header_address < 0x80000000U || header_address >= 0x80800000U) {
        return -1;
    }
    return static_cast<std::int8_t>(MEM_BU(kLevelHeaderRaceTypeOffset, RdramAddress(header_address)));
}

struct FrustumReferenceScope {
    std::array<float, kSideReferenceOffsets.size()> saved{};
    bool active = false;
};

thread_local FrustumReferenceScope g_frustum_scope;
std::atomic<bool> g_logged_extended_frustum{false};
std::atomic<int> g_last_logged_fov{-1};
float g_character_select_animation_phase = 0.0F;
bool g_character_select_animation_active = false;

bool SeedCharacterMusicMask(std::uint8_t* rdram, std::uint32_t character_address) {
    using namespace dkr::runtime::enhancements;

    // A blocked music_play intentionally leaves the current sequence running.
    // Do not leave a mask behind for an unrelated future sequence in that case.
    if (MEM_BU(0, RdramAddress(kMusicNextSequenceAddress)) != kChooseYourRacerSequence) {
        return false;
    }

    const int selected = static_cast<std::int8_t>(MEM_BU(0, RdramAddress(character_address)));
    const std::uint16_t mask = character_music_channel_mask(selected);
    MEM_W(0, RdramAddress(kDynamicMusicChannelMaskAddress)) = mask;
    return true;
}

} // namespace

bool dkr::runtime::enhancements::maximum_detail_requested() {
    return g_maximum_detail_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::maximum_detail_enabled() {
    return maximum_detail_effective(presentation_profile(), maximum_detail_requested());
}

void dkr::runtime::enhancements::set_maximum_detail_enabled(bool enabled) {
    g_maximum_detail_enabled.store(enabled, std::memory_order_release);
}

dkr::runtime::enhancements::PresentationProfile dkr::runtime::enhancements::presentation_profile() {
    return g_presentation_profile.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_presentation_profile(PresentationProfile profile) {
    g_presentation_profile.store(normalise_presentation_profile(profile), std::memory_order_release);
}

bool dkr::runtime::enhancements::modern_presentation_enabled() {
    return presentation_profile() == PresentationProfile::Modern;
}

bool dkr::runtime::enhancements::multiplayer_race_music_requested() {
    return g_multiplayer_race_music_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::multiplayer_race_music_enabled() {
    return multiplayer_race_music_effective(presentation_profile(), multiplayer_race_music_requested());
}

void dkr::runtime::enhancements::set_multiplayer_race_music_enabled(bool enabled) {
    g_multiplayer_race_music_enabled.store(enabled, std::memory_order_release);
}

int dkr::runtime::enhancements::fov_offset() {
    return g_fov_offset.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_fov_offset(int offset) {
    g_fov_offset.store(clamp_fov_offset(offset), std::memory_order_release);
}

int dkr::runtime::enhancements::view_distance_multiplier() {
    return g_view_distance_multiplier.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_view_distance_multiplier(int multiplier) {
    g_view_distance_multiplier.store(clamp_view_distance_multiplier(multiplier), std::memory_order_release);
}

bool dkr::runtime::enhancements::keep_hub_scenery_requested() {
    return g_keep_hub_scenery_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::keep_hub_scenery_enabled() {
    return modern_presentation_enabled() && keep_hub_scenery_requested();
}

void dkr::runtime::enhancements::set_keep_hub_scenery_enabled(bool enabled) {
    g_keep_hub_scenery_enabled.store(enabled, std::memory_order_release);
}

bool dkr::runtime::enhancements::keep_track_scenery_requested() {
    return g_keep_track_scenery_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::keep_track_scenery_enabled() {
    return modern_presentation_enabled() && keep_track_scenery_requested();
}

void dkr::runtime::enhancements::set_keep_track_scenery_enabled(bool enabled) {
    g_keep_track_scenery_enabled.store(enabled, std::memory_order_release);
}

bool dkr::runtime::enhancements::keep_minigame_scenery_requested() {
    return g_keep_minigame_scenery_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::keep_minigame_scenery_enabled() {
    return modern_presentation_enabled() && keep_minigame_scenery_requested();
}

void dkr::runtime::enhancements::set_keep_minigame_scenery_enabled(bool enabled) {
    g_keep_minigame_scenery_enabled.store(enabled, std::memory_order_release);
}

dkr::runtime::enhancements::SceneryRetentionMode dkr::runtime::enhancements::scenery_retention_mode() {
    return g_scenery_retention_mode.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_scenery_retention_mode(SceneryRetentionMode mode) {
    g_scenery_retention_mode.store(normalise_scenery_retention_mode(static_cast<int>(mode)), std::memory_order_release);
}

int dkr::runtime::enhancements::animated_scenery_distance_multiplier() {
    return g_animated_scenery_distance_multiplier.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_animated_scenery_distance_multiplier(int multiplier) {
    g_animated_scenery_distance_multiplier.store(clamp_animated_scenery_multiplier(multiplier),
                                                 std::memory_order_release);
}

int dkr::runtime::enhancements::billboard_effect_distance_multiplier() {
    return g_billboard_effect_distance_multiplier.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_billboard_effect_distance_multiplier(int multiplier) {
    g_billboard_effect_distance_multiplier.store(clamp_billboard_effect_multiplier(multiplier),
                                                 std::memory_order_release);
}

int dkr::runtime::enhancements::water_lava_detail_multiplier() {
    return g_water_lava_detail_multiplier.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_water_lava_detail_multiplier(int multiplier) {
    g_water_lava_detail_multiplier.store(clamp_water_lava_detail_multiplier(multiplier), std::memory_order_release);
}

bool dkr::runtime::enhancements::extended_culling_requested() {
    return g_extended_culling_enabled.load(std::memory_order_acquire);
}

bool dkr::runtime::enhancements::extended_culling_enabled() {
    return modern_presentation_enabled() && extended_culling_requested();
}

void dkr::runtime::enhancements::set_extended_culling_enabled(bool enabled) {
    g_extended_culling_enabled.store(enabled, std::memory_order_release);
}

int dkr::runtime::enhancements::frustum_guard_percent() {
    return g_frustum_guard_percent.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_frustum_guard_percent(int percent) {
    g_frustum_guard_percent.store(clamp_frustum_guard_percent(percent), std::memory_order_release);
}

bool dkr::runtime::enhancements::fit_to_window_enabled() {
    return g_fit_to_window_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_fit_to_window_enabled(bool enabled) {
    g_fit_to_window_enabled.store(enabled, std::memory_order_release);
}

int dkr::runtime::enhancements::anisotropy_level() {
    return modern_presentation_enabled() ? g_anisotropy_level.load(std::memory_order_acquire) : 16;
}

void dkr::runtime::enhancements::set_anisotropy_level(int level) {
    constexpr std::array<int, 5> supported{1, 2, 4, 8, 16};
    int closest = supported.front();
    for (const int candidate : supported) {
        if (std::abs(candidate - level) < std::abs(closest - level)) {
            closest = candidate;
        }
    }
    g_anisotropy_level.store(closest, std::memory_order_release);
}

float dkr::runtime::enhancements::texture_lod_bias() {
    return static_cast<float>(texture_lod_bias_hundredths()) / 100.0F;
}

float dkr::runtime::enhancements::effective_texture_lod_bias() {
    return modern_presentation_enabled() ? texture_lod_bias() : 0.0F;
}

bool dkr::runtime::enhancements::generated_mipmaps_requested() {
    return g_generated_mipmaps_requested.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_generated_mipmaps_requested(bool enabled) {
    g_generated_mipmaps_requested.store(enabled, std::memory_order_release);
}

int dkr::runtime::enhancements::texture_lod_bias_hundredths() {
    return g_texture_lod_bias_hundredths.load(std::memory_order_acquire);
}

void dkr::runtime::enhancements::set_texture_lod_bias_hundredths(int bias) {
    g_texture_lod_bias_hundredths.store(clamp_texture_lod_bias_hundredths(bias), std::memory_order_release);
}

extern "C" void dkr_character_select_music_unblock(std::uint8_t* rdram, recomp_context*) {
    // The character-select initializer immediately starts its own sequence and
    // then restores DKR's music-change lock. Clear a stale lock only at that
    // ownership boundary so the intended sequence can replace intro/menu music.
    MEM_W(0, RdramAddress(kBlockMusicChangeAddress)) = 0;
}

extern "C" void dkr_character_select_music_mask(std::uint8_t* rdram, recomp_context*) {
    // music_play queues SEQUENCE_CHOOSE_YOUR_RACER and resets the pending
    // dynamic-channel mask before the original initializer mutes channels on
    // the old sequence player. Seed the queued sequence explicitly so it
    // starts with only the shared backing channels and the selected racer's
    // arrangement. 0x64 is DKR's invalid/no-secondary-channel sentinel.
    if (!SeedCharacterMusicMask(rdram, kMenuCurrentCharacterAddress)) {
        return;
    }
    // Character-select models are authored to dance to the music beat. Reset
    // the deterministic beat phase at the same sequence ownership boundary;
    // the original audio clock continues running and the mix remains exact.
    g_character_select_animation_phase = 0.0F;
    g_character_select_animation_active = true;
}

extern "C" void dkr_character_menu_music_mask(std::uint8_t* rdram, recomp_context*) {
    // Game Select and File Select can each restart Choose Your Racer after
    // returning from another menu. Seed that queued sequence from the selected
    // character, but do not reset the character-select model animation phase.
    SeedCharacterMusicMask(rdram, kMenuSelectedCharacterAddress);
}

extern "C" void dkr_character_select_animation_tick(std::uint8_t* rdram, recomp_context* context) {
    if (!g_character_select_animation_active) {
        return;
    }
    const int tempo = static_cast<std::int16_t>(MEM_H(0, RdramAddress(kMusicTempoAddress)));
    g_character_select_animation_phase = dkr::runtime::enhancements::advance_character_select_phase(
        g_character_select_animation_phase, static_cast<std::int32_t>(context->r4), tempo);
}

extern "C" void dkr_character_select_animation_fraction(std::uint8_t*, recomp_context* context) {
    if (g_character_select_animation_active) {
        context->f0.fl = g_character_select_animation_phase;
    }
}

extern "C" void dkr_apply_maximum_racer_detail(std::uint8_t* rdram, recomp_context* context) {
    // Online geometry selection stays canonical, even when peers choose
    // different quality. Its immutable render copy applies the local policy.
    // One local machine can safely use the original fast selector for
    // ALL viewports. Only online peers require independent render-only LODs.
    if (dkr::runtime::netplay::session().runtime_view().active)
        return;
    if (!dkr::runtime::enhancements::maximum_detail_enabled()) {
        return;
    }

    // set_temp_model_transforms has already found the first/last valid model
    // and clamped its chosen index when this hook runs. r4 is the first valid
    // (highest-detail) index, r8 is the index about to be stored, and r16 is
    // the Object pointer. Preserve the ghost's deliberately distinct model.
    const std::uint16_t behaviour = MEM_HU(0x48, context->r16);
    if (behaviour != kTimeTrialGhostBehaviour) {
        const auto authored_lod = static_cast<unsigned>(context->r8);
        context->r8 = context->r4;
        // Bounded evidence of the native selector's actual first resident LOD.
        // No extra guest reads, allocation, animation tick or model mutation.
        const auto view = dkr::runtime::revision_addresses::selected_revision() == dkr::runtime::rom::Revision::UsV80
                              ? 0x8011B634U
                              : 0x8011B0B4U;
        dkr::runtime::detail::note(
            {dkr::runtime::presentation::recording_scene_generation(), static_cast<unsigned>(context->r16),
             static_cast<unsigned>(MEM_W(0, RdramAddress(view))), authored_lod, static_cast<unsigned>(context->r8), 0,
             true, "native highest resident; local-all-views-v2"});
    }
}

extern "C" void dkr_restore_multiplayer_race_music(std::uint8_t*, recomp_context* context) {
    if (context != nullptr && dkr::runtime::enhancements::multiplayer_race_music_enabled() &&
        static_cast<std::int32_t>(context->r2) >= 2) {
        // The following authored `slti v0, 2` chooses between muting music and
        // calling level_music_start. Present the existing branch with the
        // two-player value so the complete retail start/fade path is reused.
        // No sequence player is started from native code and Accurate retains
        // the original 3/4-player silence exactly.
        context->r2 = 1;
    }
}

extern "C" void dkr_apply_gameplay_fov(std::uint8_t*, recomp_context* context) {
    const int authored = static_cast<std::int32_t>(context->r12);
    if (authored <= 0 || authored > 120) {
        return;
    }
    const int effective = dkr::runtime::enhancements::effective_gameplay_fov(
        dkr::runtime::enhancements::presentation_profile(), authored, dkr::runtime::enhancements::fov_offset());
    context->r12 = static_cast<gpr>(effective);
    if (effective != authored && g_last_logged_fov.exchange(effective, std::memory_order_relaxed) != effective) {
        std::fprintf(stderr, "[boot][modern] gameplay FOV authored=%d effective=%d\n", authored, effective);
    }
}

extern "C" void dkr_extend_object_draw_distance(std::uint8_t* rdram, recomp_context* context) {
    const int authored = static_cast<std::int32_t>(context->r3);
    int behaviour = -1;
    const std::uint32_t object = static_cast<std::uint32_t>(context->r4);
    if (object >= 0x80000000U && object <= 0x807FFFB6U) {
        behaviour = static_cast<std::int16_t>(MEM_H(0x48, RdramAddress(object)));
    }
    const int multiplier = dkr::runtime::enhancements::object_view_distance_multiplier(
        behaviour, dkr::runtime::enhancements::view_distance_multiplier(),
        dkr::runtime::enhancements::animated_scenery_distance_multiplier(),
        dkr::runtime::enhancements::billboard_effect_distance_multiplier(),
        dkr::runtime::enhancements::water_lava_detail_multiplier());
    context->r3 = static_cast<gpr>(dkr::runtime::enhancements::object_distance_before_multiplayer_reduction(
        dkr::runtime::enhancements::presentation_profile(), authored, multiplier,
        static_cast<std::int32_t>(MEM_W(
            0, RdramAddress(dkr::runtime::revision_addresses::selected_revision() == dkr::runtime::rom::Revision::UsV80
                                ? 0x8011D8FCU
                                : 0x8011D37CU)))));
}

extern "C" void dkr_keep_local_multiplayer_scenery(std::uint8_t* rdram, recomp_context* context) {
    using namespace dkr::runtime::enhancements;
    const auto object = static_cast<std::uint32_t>(context->r17);
    if (object < 0x80000000U || object > 0x807FFF80U ||
        !scenery_retention_enabled_for_race_type(presentation_profile(), CurrentLevelRaceType(rdram),
                                                 keep_hub_scenery_requested(), keep_track_scenery_requested(),
                                                 keep_minigame_scenery_requested()))
        return;
    const auto header = static_cast<std::uint32_t>(MEM_W(0x40, RdramAddress(object)));
    if (header < 0x80000000U || header > 0x807FFF88U || MEM_HU(0x48, RdramAddress(object)) != 0 ||
        MEM_BU(0x53, RdramAddress(header)) != 1)
        return;
    if (MEM_W(0x5C, RdramAddress(object)) != 0)
        return;
    const auto interaction = static_cast<std::uint32_t>(MEM_W(0x4C, RdramAddress(object)));
    if (interaction &&
        (interaction < 0x80000000U || interaction > 0x807FFFEAU || MEM_HU(0x14, RdramAddress(interaction)) != 0))
        return;
    // Only the constructor's temporary NO_MULTIPLAYER test is changed. Keep
    // the separate NO_TIME_TRIAL gate, header flags and interactive actors.
    context->r16 &= ~static_cast<gpr>(0x40U);
}

extern "C" void dkr_maximise_persistent_water_detail(std::uint8_t* rdram, recomp_context*) {
    const int authored =
        static_cast<std::int32_t>(MEM_W(kWaveViewDistanceOffset, RdramAddress(kWaveControllerAddress)));
    const int effective = dkr::runtime::enhancements::effective_wave_view_distance(
        dkr::runtime::enhancements::presentation_profile(), dkr::runtime::enhancements::water_lava_detail_multiplier(),
        CurrentLevelRaceType(rdram), authored);
    MEM_W(kWaveViewDistanceOffset, RdramAddress(kWaveControllerAddress)) = static_cast<gpr>(effective);
}

extern "C" void dkr_anchor_persistent_water_to_camera(std::uint8_t* rdram, recomp_context* context) {
    if (!dkr::runtime::enhancements::persistent_water_override_enabled(
            dkr::runtime::enhancements::presentation_profile(),
            dkr::runtime::enhancements::water_lava_detail_multiplier(), CurrentLevelRaceType(rdram))) {
        return;
    }
    const std::uint32_t camera = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(kSceneActiveCameraAddress)));
    if (camera < 0x80000000U || camera > 0x807FFFE8U) {
        return;
    }

    // Keep DKR's maximum safe 5x5 HQ wave cache centred on what the player can
    // actually see. All BSP water segments remain in the render list through
    // the existing scenery hook, while the authored low-detail fallback can no
    // longer replace water immediately around an offset or spectator camera.
    context->r4 = static_cast<gpr>(static_cast<std::int32_t>(ReadRdramFloat(rdram, camera + 0x0CU)));
    context->r5 = static_cast<gpr>(static_cast<std::int32_t>(ReadRdramFloat(rdram, camera + 0x10U)));
    context->r6 = static_cast<gpr>(static_cast<std::int32_t>(ReadRdramFloat(rdram, camera + 0x14U)));
}

extern "C" void dkr_stabilise_persistent_water_transition(std::uint8_t* rdram, recomp_context*) {
    using namespace dkr::runtime::enhancements;
    if (!persistent_water_override_enabled(presentation_profile(), water_lava_detail_multiplier(),
                                           CurrentLevelRaceType(rdram))) {
        return;
    }

    const std::uint32_t selections = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(kWaveSelectionMapAddress)));
    const std::uint32_t models = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(kWaveModelAddress)));
    const int segment_count = static_cast<std::int32_t>(MEM_W(0, RdramAddress(kNumberOfLevelSegmentsAddress)));
    if (selections < 0x80000000U || selections >= 0x80800000U || models < 0x80000000U || models >= 0x80800000U ||
        segment_count <= 0 || segment_count > 512 ||
        models > 0x80800000U - static_cast<std::uint32_t>(segment_count) * kWaveModelStride) {
        return;
    }

    // Retail fades a newly selected procedural block in after immediately
    // suppressing its flat fallback batch. On a persistent widescreen scene
    // that exposes a one-frame hole/pop at the moving 5x5 HQ boundary. Keep
    // the fixed, safely allocated 5x5 pool, but make every selected cell fully
    // visible at the hand-off. Both viewport fade banks are initialised so a
    // later split-screen view cannot inherit a stale transition.
    for (int segment = 0; segment < segment_count; ++segment) {
        const std::uint32_t model = models + static_cast<std::uint32_t>(segment) * kWaveModelStride;
        const std::uint32_t selector_index =
            static_cast<std::uint32_t>(MEM_W(kWaveModelSelectionOffset, RdramAddress(model)));
        if (selector_index >= static_cast<std::uint32_t>(segment_count)) {
            continue;
        }
        const std::uint32_t selection =
            static_cast<std::uint32_t>(MEM_W(selector_index * sizeof(std::uint32_t), RdramAddress(selections)));
        if (persistent_water_hq_fade(presentation_profile(), water_lava_detail_multiplier(), selection != 0U, 0x80) !=
            0) {
            continue;
        }
        for (std::uint32_t offset = 0U; offset < kWaveModelFadeBytes; ++offset) {
            MEM_B(kWaveModelFadeOffset + offset, RdramAddress(model)) = 0;
        }
    }
}

extern "C" void dkr_extend_hub_segment_bitfield(std::uint8_t* rdram, recomp_context* context) {
    const int race_type = CurrentLevelRaceType(rdram);
    const bool authored_region_visible = context->r12 != 0;
    const bool retention_active = dkr::runtime::enhancements::relax_scenery_segment_bitfield(
        dkr::runtime::enhancements::presentation_profile(), race_type,
        dkr::runtime::enhancements::scenery_retention_mode(), dkr::runtime::enhancements::keep_hub_scenery_requested(),
        dkr::runtime::enhancements::keep_track_scenery_requested(),
        dkr::runtime::enhancements::keep_minigame_scenery_requested());
    if (retention_active) {
        // At 0x80029D78 r12 is the original per-region bitfield result.
        // block_visible still performs the ordinary camera-plane test. This
        // only relaxes DKR's authored regional visibility ownership.
        context->r12 = 1;
    }
    dkr::runtime::presentation::interpolation_trace_segment_region(rdram, static_cast<std::uint32_t>(context->r7),
                                                                   race_type, authored_region_visible,
                                                                   context->r12 != 0, retention_active);
}

extern "C" void dkr_keep_hub_segment_visible(std::uint8_t* rdram, recomp_context* context) {
    // Intentionally preserve block_visible's camera-plane result. The former
    // master toggle overwrote r2 here and rendered geometry behind the camera,
    // which could overflow authored display-list/matrix budgets. Retention is
    // now implemented only by the regional bitfield and forward frustum hooks.
    const std::uint32_t segment_id = static_cast<std::uint32_t>(MEM_W(0x18, context->r29));
    dkr::runtime::presentation::interpolation_trace_segment_block(rdram, segment_id, context->r2 != 0);
}

extern "C" void dkr_extended_frustum_begin(std::uint8_t* rdram, recomp_context*) {
    // Defensive recovery for an interrupted/nested scope. DKR normally calls
    // this serially once per viewport, but never leave the authored table in a
    // widened state if a host-side exception or future call-site changes that.
    if (g_frustum_scope.active) {
        for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
            WriteRdramFloat(rdram, kFrustumReferenceAddress + kSideReferenceOffsets[index],
                            g_frustum_scope.saved[index]);
        }
        g_frustum_scope.active = false;
    }

#if DKR_RUNTIME_HAS_RT64
    auto* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    const int layout = static_cast<std::int32_t>(MEM_W(0, RdramAddress(kViewportLayoutAddress)));
    float scale = dkr::runtime::enhancements::frustum_horizontal_scale(
        dkr::runtime::enhancements::presentation_profile(), dkr::runtime::enhancements::fit_to_window_enabled(),
        dkr::runtime::enhancements::extended_culling_enabled(), static_cast<float>(width) / static_cast<float>(height),
        layout, dkr::runtime::enhancements::frustum_guard_percent());
    const float retention_scale = dkr::runtime::enhancements::scenery_retention_frustum_scale(
        dkr::runtime::enhancements::presentation_profile(), CurrentLevelRaceType(rdram),
        dkr::runtime::enhancements::scenery_retention_mode(), dkr::runtime::enhancements::view_distance_multiplier(),
        dkr::runtime::enhancements::keep_hub_scenery_requested(),
        dkr::runtime::enhancements::keep_track_scenery_requested(),
        dkr::runtime::enhancements::keep_minigame_scenery_requested());
    scale = std::max(scale, retention_scale);
    if (scale <= 1.0001F) {
        return;
    }
    for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
        const std::uint32_t address = kFrustumReferenceAddress + kSideReferenceOffsets[index];
        g_frustum_scope.saved[index] = ReadRdramFloat(rdram, address);
        WriteRdramFloat(rdram, address, g_frustum_scope.saved[index] * scale);
    }
    g_frustum_scope.active = true;
    if (!g_logged_extended_frustum.exchange(true, std::memory_order_relaxed)) {
        std::fprintf(stderr, "[boot][modern] CPU frustum scale=%.3f layout=%d window=%dx%d\n", scale, layout, width,
                     height);
    }
#endif
}

extern "C" void dkr_extended_frustum_end(std::uint8_t* rdram, recomp_context*) {
    if (!g_frustum_scope.active) {
        return;
    }
    for (std::size_t index = 0; index < kSideReferenceOffsets.size(); ++index) {
        WriteRdramFloat(rdram, kFrustumReferenceAddress + kSideReferenceOffsets[index], g_frustum_scope.saved[index]);
    }
    g_frustum_scope.active = false;
}
