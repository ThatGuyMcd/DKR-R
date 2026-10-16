#pragma once

#include "rom_revision.hpp"

#include "recomp.h"

#include <cstdint>

namespace dkr::runtime {

using RecompiledEntrypoint = void (*)(std::uint8_t*, recomp_context*);

struct GamePayload {
    rom::Revision revision;
    std::uint64_t rom_hash;
    RecompiledEntrypoint entrypoint;
    RecompiledEntrypoint music_volume_set;
    RecompiledEntrypoint main_game_loop;
    RecompiledEntrypoint get_settings;
    RecompiledEntrypoint leveltable_type;
    // Asset plumbing used by the custom track hooks: the extended level table
    // is allocated through the retail pool, and retail offsets are delegated
    // back to the retail loader rather than reimplemented.
    RecompiledEntrypoint asset_load;
    RecompiledEntrypoint mempool_alloc_safe;
    RecompiledEntrypoint get_misc_asset;
    RecompiledEntrypoint sound_clear_delayed;
    RecompiledEntrypoint reset_delayed_text;
    RecompiledEntrypoint should_check_lead_player;
    RecompiledEntrypoint is_in_two_player_adventure;
    RecompiledEntrypoint is_player_two_in_control;
    RecompiledEntrypoint swap_lead_player;
    RecompiledEntrypoint asset_allocate;
    RecompiledEntrypoint asset_release;
    RecompiledEntrypoint asset_copy;
    RecompiledEntrypoint menu_sound_play;
    RecompiledEntrypoint menu_texture_load;
    RecompiledEntrypoint menu_texture_free;
    RecompiledEntrypoint menu_texture_draw;
    RecompiledEntrypoint menu_font;
    RecompiledEntrypoint menu_colour;
    RecompiledEntrypoint menu_background;
    RecompiledEntrypoint menu_text;
    RecompiledEntrypoint menu_render_reset;
    RecompiledEntrypoint menu_material_set;
    RecompiledEntrypoint menu_background_cover;
    RecompiledEntrypoint filtered_cheats;
    RecompiledEntrypoint stage_spawn;
    RecompiledEntrypoint stage_free;
    RecompiledEntrypoint stage_music_fraction;
    RecompiledEntrypoint stage_particles;
    RecompiledEntrypoint stage_camera;
    RecompiledEntrypoint unlock_drumstick;
    RecompiledEntrypoint unlock_tt;
    RecompiledEntrypoint sound_bank_relocate;
    RecompiledEntrypoint sound_bank_play;
    RecompiledEntrypoint sound_parameter;
    RecompiledEntrypoint sound_spatial_point;
    RecompiledEntrypoint titlescreen_controller_assign;
    RecompiledEntrypoint input_assign_players;
    RecompiledEntrypoint charselect_assign_ai;
    RecompiledEntrypoint init_racer_headers;
    RecompiledEntrypoint leveltable_vehicle_default;
    RecompiledEntrypoint leveltable_vehicle_usable;
    RecompiledEntrypoint set_level_default_vehicle;
    RecompiledEntrypoint set_time_trial_enabled;
    // The Modern camera widens the one-player view with speed.
    RecompiledEntrypoint cam_set_fov;
    void (*register_sections)();
};

const GamePayload& payload_v77();
const GamePayload& payload_v80();
const GamePayload* payload_for(rom::Revision revision);
bool select_payload(rom::Revision revision);
const GamePayload* active_payload();
bool invoke_music_volume_set(std::uint8_t* rdram, recomp_context* context);
bool invoke_main_game_loop(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::runtime
