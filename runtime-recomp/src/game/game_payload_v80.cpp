#include "game_payload.hpp"
extern "C" void dkr_track_select_background_cover(std::uint8_t*, recomp_context*);

#include "funcs.h"
#include "librecomp/overlays.hpp"

#include "recomp_overlays.inl"

namespace {

void RegisterSectionsV80() {
    const recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };
    const recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };
    recomp::overlays::register_overlays(sections, overlays);
}

} // namespace

const dkr::runtime::GamePayload& dkr::runtime::payload_v80() {
    static const GamePayload payload{
        .revision = rom::Revision::UsV80,
        .rom_hash = rom::kUsV80Xxh3,
        .entrypoint = recomp_entrypoint,
        .music_volume_set = music_volume_set,
        .main_game_loop = main_game_loop,
        .get_settings = get_settings,
        .leveltable_type = leveltable_type,
        .asset_load = asset_load,
        .mempool_alloc_safe = mempool_alloc_safe,
        .get_misc_asset = get_misc_asset,
        .sound_clear_delayed = sound_clear_delayed,
        .reset_delayed_text = reset_delayed_text,
        .should_check_lead_player = func_80023568,
        .is_in_two_player_adventure = is_in_two_player_adventure,
        .is_player_two_in_control = is_player_two_in_control,
        .swap_lead_player = swap_lead_player,
        .asset_allocate = mempool_alloc_safe,
        .asset_release = mempool_free,
        .asset_copy = dmacopy,
        .menu_sound_play = sound_play,
        .menu_texture_load = load_texture,
        .menu_texture_free = tex_free,
        .menu_texture_draw = texrect_draw_scaled,
        .menu_font = set_text_font,
        .menu_colour = set_text_colour,
        .menu_background = set_text_background_colour,
        .menu_text = draw_text,
        .menu_render_reset = rendermode_reset,
        .menu_material_set = material_set,
        .menu_background_cover = dkr_track_select_background_cover,
        .filtered_cheats = get_filtered_cheats,
        .stage_spawn = spawn_object,
        .stage_free = free_object,
        .stage_music_fraction = music_animation_fraction,
        .stage_particles = obj_spawn_particle,
        .stage_camera = cam_get_active_camera,
        .unlock_drumstick = is_drumstick_unlocked,
        .unlock_tt = is_tt_unlocked,
        .sound_bank_relocate = alBnkfNew,
        .sound_bank_play = sndp_play_with_priority,
        .sound_parameter = sndp_set_param,
        .sound_spatial_point = audspat_point_create,
        .titlescreen_controller_assign = titlescreen_controller_assign,
        .input_assign_players = input_assign_players,
        .charselect_assign_ai = charselect_assign_ai,
        .init_racer_headers = init_racer_headers,
        .leveltable_vehicle_default = leveltable_vehicle_default,
        .leveltable_vehicle_usable = leveltable_vehicle_usable,
        .set_level_default_vehicle = set_level_default_vehicle,
        .set_time_trial_enabled = set_time_trial_enabled,
        .cam_set_fov = cam_set_fov,
        .register_sections = RegisterSectionsV80,
    };
    return payload;
}
