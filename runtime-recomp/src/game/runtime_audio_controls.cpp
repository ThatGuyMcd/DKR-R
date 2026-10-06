#include "runtime_audio_controls.hpp"
#include "audio_mix_policy.hpp"
#include "custom_music.hpp"
#include "game_payload.hpp"
#include "runtime_enhancements.hpp"
#include "revision_addresses.hpp"
#include "runtime_legacy_mods.hpp"

#include "recomp.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace {

std::atomic<float> g_music_volume{1.0F};
std::atomic<float> g_music_applied{1.0F};
std::atomic<float> g_sound_effects_volume{1.0F};
std::atomic<float> g_vehicle_volume{1.0F};
std::atomic<float> g_nature_volume{1.0F};
thread_local int g_vehicle_audio_scope_depth = 0;
thread_local int g_nature_audio_scope_depth = 0;

const std::uint32_t& kMusicPlayerAddress =
    dkr::runtime::revision_addresses::MusicPlayer;
const std::uint32_t& kMusicBaseVolumeAddress =
    dkr::runtime::revision_addresses::MusicBaseVolume;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
} // namespace

float dkr::runtime::audio::music_volume() {
    return g_music_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_music_volume(float volume) {
    g_music_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

float dkr::runtime::audio::sound_effects_volume() {
    return g_sound_effects_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_sound_effects_volume(float volume) {
    g_sound_effects_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

float dkr::runtime::audio::vehicle_volume() {
    return g_vehicle_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_vehicle_volume(float volume) {
    g_vehicle_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

float dkr::runtime::audio::nature_volume() {
    return g_nature_volume.load(std::memory_order_acquire);
}

void dkr::runtime::audio::set_nature_volume(float volume) {
    g_nature_volume.store(clamp_mix_volume(volume), std::memory_order_release);
}

extern "C" void dkr_scale_sequence_player_volume(std::uint8_t* rdram,
                                                    recomp_context* context) {
    const std::uint32_t music_player = static_cast<std::uint32_t>(
        MEM_W(0, RdramAddress(kMusicPlayerAddress)));
    if (music_player == 0U ||
        static_cast<std::uint32_t>(context->r4) != music_player) {
        return;
    }
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const std::uint32_t authored = static_cast<std::uint32_t>(context->r5) & 0xFFFFU;
    const std::uint32_t scaled = modern
        ? dkr::runtime::audio::scale_authored_volume(
              authored, g_music_applied.load(std::memory_order_acquire))
        : authored;
    // A custom track's music file takes the volume its silent carrier song
    // would have had - every fade and slider included - in any presentation.
    recomp_context frozen=*context;frozen.r5=scaled;
    if (dkr::runtime::legacy::frozen_music_volume(rdram,&frozen) ||
        (!dkr::runtime::legacy::frozen_online_resources() &&
         dkr::runtime::custom_music::intercept_music_volume(rdram, static_cast<std::int16_t>(scaled)))) {
        context->r5 = 0;
        return;
    }
    if (modern) {
        context->r5 = dkr::runtime::audio::scale_authored_volume(
            static_cast<std::uint32_t>(context->r5),
            g_music_applied.load(std::memory_order_acquire));
    }
}

extern "C" void dkr_audio_mix_tick(std::uint8_t* rdram,
                                     recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        const float previous = g_music_applied.exchange(
            1.0F, std::memory_order_acq_rel);
        if (previous != 1.0F) {
            recomp_context call = *context;
            call.r4 = MEM_BU(0, RdramAddress(kMusicBaseVolumeAddress));
            dkr::runtime::invoke_music_volume_set(rdram, &call);
        }
        return;
    }
    const float current = g_music_applied.load(std::memory_order_acquire);
    const float next = dkr::runtime::audio::advance_mix_volume(
        current, dkr::runtime::audio::music_volume());
    if (next == current) {
        return;
    }
    g_music_applied.store(next, std::memory_order_release);

    // Re-submit DKR's unmodified authored base volume only while the user gain
    // is ramping. music_volume_set recomputes the game's fade/slider product;
    // the alCSP boundary hook above then applies our final attenuation without
    // corrupting gMusicBaseVolume or waiting for the next map.
    recomp_context call = *context;
    call.r4 = MEM_BU(0, RdramAddress(kMusicBaseVolumeAddress));
    dkr::runtime::invoke_music_volume_set(rdram, &call);
}

extern "C" void dkr_enter_vehicle_audio_scope(std::uint8_t*, recomp_context*) {
    ++g_vehicle_audio_scope_depth;
}

extern "C" void dkr_leave_vehicle_audio_scope(std::uint8_t*, recomp_context*) {
    g_vehicle_audio_scope_depth = std::max(g_vehicle_audio_scope_depth - 1, 0);
}

extern "C" void dkr_enter_nature_audio_scope(std::uint8_t*, recomp_context*) {
    ++g_nature_audio_scope_depth;
}

extern "C" void dkr_leave_nature_audio_scope(std::uint8_t*, recomp_context*) {
    g_nature_audio_scope_depth = std::max(g_nature_audio_scope_depth - 1, 0);
}

extern "C" void dkr_scale_sound_effect_volume(std::uint8_t*,
                                                recomp_context* context) {
    constexpr std::uint32_t kVolumeEvent = 1U << 3U;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        (static_cast<std::uint32_t>(context->r5) & 0xFFFFU) != kVolumeEvent) {
        return;
    }
    std::uint32_t value = static_cast<std::uint32_t>(context->r6);
    if (g_vehicle_audio_scope_depth > 0) {
        value = dkr::runtime::audio::scale_authored_volume(
            value, dkr::runtime::audio::vehicle_volume());
    } else if (g_nature_audio_scope_depth > 0) {
        value = dkr::runtime::audio::scale_authored_volume(
            value, dkr::runtime::audio::nature_volume());
    } else {
        value = dkr::runtime::audio::scale_authored_volume(
            value, dkr::runtime::audio::sound_effects_volume());
    }
    context->r6 = value;
}
