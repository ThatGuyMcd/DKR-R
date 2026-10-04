#include "probe_bridge.h"
#include "steering_wheel_policy.hpp"
#include "widescreen_policy.hpp"
#include "revision_addresses.hpp"
#include "vi_presentation_policy.hpp"
#include "scheduler_event_policy.hpp"
#include "intro_tail_policy.hpp"
#include "online_roster_policy.hpp"
#include "character_select_music_policy.hpp"
#include "character_select_animation_policy.hpp"
#include "finish_presentation_policy.hpp"
#include "presentation_identity.hpp"
#include "hud_layout_policy.hpp"
#include "water_scroll_policy.hpp"
#include <bit>
#include <cmath>

// Compile-time admission boundaries; no game window or test process required.
static_assert(0U<DKR_PROBE_SCENE_LEVEL_COUNT && 53U<DKR_PROBE_SCENE_LEVEL_COUNT);
static_assert((54U<DKR_PROBE_SCENE_LEVEL_COUNT)==bool(DKR_PROBE_HAS_FULL_SCENES));
static_assert((57U<DKR_PROBE_SCENE_LEVEL_COUNT)==bool(DKR_PROBE_HAS_FULL_SCENES));
static_assert((64U<DKR_PROBE_SCENE_LEVEL_COUNT)==bool(DKR_PROBE_HAS_FULL_SCENES));
static_assert(65U>=DKR_PROBE_SCENE_LEVEL_COUNT && UINT32_MAX>=DKR_PROBE_SCENE_LEVEL_COUNT);

extern "C" void dkr_probe_boss_diagnostic_addresses(uint32_t a[8]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77:dkr::runtime::revision_addresses::kUsV80;
    const uint32_t values[8]={t.GameMode,t.CurrentMapId,t.CurrentLevelHeader,
        t.LevelLoadTimer,t.RaceEndTimer,t.NumberOfRacers,t.NumberOfFinishedRacers,t.MusicNextSequence};
    std::copy(std::begin(values),std::end(values),a);
}

extern "C" void dkr_probe_parity_addresses(uint32_t a[24]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77:dkr::runtime::revision_addresses::kUsV80;
    const uint32_t values[24]={t.PlayerHud,t.CurrentHud,t.HudDisplayList,t.HudColour,
        t.ViewportLayout,t.HudNumPlayers,t.NumberOfGameplayPlayers,t.NumberOfActivePlayers,
        t.CurrentLevelHeader,DKR_PROBE_REVISION==77?0x80126D24U:0x801272E4U,
        DKR_PROBE_REVISION==77?0x8011AEF4U:0x8011B474U,t.TrackDisplayList,
        t.ObjectCurrentMatrix,t.Cameras,t.ActiveCameraId,t.CurrentCameraFov,
        t.CutsceneCameraActive,t.WaveController,t.WaveTexUVMaskX,t.WaveTexUVMaskY,
        t.ShadowHeapFlip,t.ShadowHeapData,t.SceneActiveCamera,t.ViewProjectionMatrix};
    std::copy(std::begin(values),std::end(values),a);
}
extern "C" void dkr_probe_scenery_addresses(uint32_t a[11]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77:dkr::runtime::revision_addresses::kUsV80;
    const uint32_t values[11]={t.TrackDisplayList,t.ActiveCameraId,t.CurrentLevelHeader,t.SceneActiveCamera,
        DKR_PROBE_REVISION==77?0x800DE8E8U:0x800DEE68U,
        DKR_PROBE_REVISION==77?0x80120D0CU:0x8012128CU,
        DKR_PROBE_REVISION==77?0x80126174U:0x80126714U,t.GameMode,
        DKR_PROBE_REVISION==77?0x8011AEF5U:0x8011B475U, // gIsTimeTrial, symbol tables.
        DKR_PROBE_REVISION==77?0x800DC918U:0x800DCE88U,t.CutsceneCameraActive};
    std::copy(std::begin(values),std::end(values),a);
}
extern "C" uint32_t dkr_probe_object_identity(uint32_t scene,uint32_t object,uint32_t generation,uint16_t id,uint16_t behaviour) {
    return dkr::runtime::presentation::make_object_identity(scene,object&0x7FFFFFU,generation,id,behaviour);
}
extern "C" uint32_t dkr_probe_camera_identity(uint32_t scene,uint32_t camera,uint32_t epoch,unsigned role) {
    using namespace dkr::runtime::presentation;
    return role<2 ? make_camera_matrix_identity(scene,camera,uint8_t(role),epoch):make_camera_continuity_identity(scene,camera,epoch);
}
extern "C" uint32_t dkr_probe_matrix_identity(uint32_t object,uint32_t ordinal,uint32_t camera) {
    using namespace dkr::runtime::presentation;
    return with_camera_continuity(make_matrix_identity(object,ordinal),camera);
}
extern "C" uint32_t dkr_probe_wave_identity(uint32_t scene,uint32_t viewport,uint32_t block,const uint32_t f[7],uint32_t camera) {
    using namespace dkr::runtime::presentation;
    const auto topology=make_wave_topology_variant(f[0],f[1]!=0,uint8_t(f[2]),f[3]);
    return with_camera_continuity(make_wave_matrix_identity(scene,viewport,block&0x7FFFFFU,f[4],f[5],f[6],topology),camera);
}
extern "C" int dkr_probe_camera_discontinuous(const uint32_t previous[4],const uint32_t current[4]) {
    using namespace dkr::runtime::presentation;
    return camera_sample_discontinuous({std::bit_cast<float>(previous[0]),std::bit_cast<float>(previous[1]),std::bit_cast<float>(previous[2]),std::bit_cast<float>(previous[3])},
        {std::bit_cast<float>(current[0]),std::bit_cast<float>(current[1]),std::bit_cast<float>(current[2]),std::bit_cast<float>(current[3])});
}
extern "C" void dkr_probe_hud_element(uint32_t element,uint32_t fields[4]) {
    using namespace dkr::runtime::hud;
    fields[0]=element<kElements.size()?uint32_t(kElements[element].widget):UINT32_MAX;
    fields[1]=uint32_t(kElements.size());
    fields[2]=fields[3]=0;
}
extern "C" int dkr_probe_finish_shot(uint32_t mode,uint32_t previous_mode,uint32_t owner,uint32_t node,uint32_t previous_owner,uint32_t previous_node) {
    using namespace dkr::runtime::presentation;
    FinishCameraShot shot{};shot.valid=true;shot.previous_mode=int16_t(previous_mode);
    shot.previous_owner=previous_owner;shot.previous_node=previous_node;
    shot.observe_node(owner,node);return shot.sample(int16_t(mode));
}
extern "C" void dkr_probe_geometry_key(uint32_t scene,uint32_t address,unsigned segment,unsigned pass,uint32_t f[2]) {
    using namespace dkr::runtime::presentation;
    auto packed=normalise_identity(0x53555246U^(address&0x7FFFFFU)^(scene*0x9E3779B9U))&0x1FFFFFU;
    auto key=segment!=UINT32_MAX ? level_segment_presentation_key(segment,pass!=0):
        PresentationKey{uint16_t((packed&65535U)?packed&65535U:1U),uint8_t((packed>>16)&31U)};
    f[0]=key.token;f[1]=key.variant;
}
extern "C" uint32_t dkr_probe_part_identity(uint32_t owner,uint32_t ordinal,uint32_t transform,int mirrored,uint32_t camera) {
    using namespace dkr::runtime::presentation;
    (void)ordinal;
    return with_camera_continuity(make_vehicle_part_matrix_identity(owner,transform,mirrored!=0),camera);
}
extern "C" unsigned dkr_probe_address_variant(uint32_t address) {return dkr::runtime::presentation::presentation_variant_for_address(address);}
extern "C" unsigned dkr_probe_part_variant(uint32_t frame,uint32_t count) {return dkr::runtime::presentation::vehicle_part_frame_variant(frame,uint16_t(count));}
extern "C" unsigned dkr_probe_water_tag(uint32_t x,uint32_t y) {return dkr::runtime::water::scroll_tag(x,y);}

extern "C" void dkr_probe_presentation_addresses(uint32_t a[8]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77 : dkr::runtime::revision_addresses::kUsV80;
    a[0]=t.ScreenViewports;a[1]=t.GameMode;a[2]=t.PostRaceViewport;
    a[3]=t.NumberOfActivePlayers;a[4]=t.TrophyRaceWorldId;a[5]=t.ViewportLayout;
    a[6]=t.IsInTracksMenu;a[7]=t.ActiveCameraId;
}
extern "C" int dkr_probe_postrace_scope(int mode,int postrace,int players,int trophy,int layout) {
    return dkr::runtime::presentation::postrace_full_view_scope(true,true,mode==0,
        postrace,players,trophy,layout);
}
extern "C" int dkr_probe_contracted_wood(const int32_t b[4]) {
    dkr::runtime::presentation::PostraceFrameGate gate;
    gate.observe_wood(b[0],b[1],b[2],b[3],320,240);
    return gate.pending_contracted_frame;
}
extern "C" int dkr_probe_lens_scope(const int32_t b[8],int tracks) {
    const bool full=dkr::runtime::enhancements::is_presented_fullscreen_track_preview(
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],320,240);
    return dkr::runtime::enhancements::preserve_track_select_lens_flare_tint(true,true,tracks!=0,full);
}

extern "C" int dkr_probe_fullscreen_preview_bounds(const int32_t v[8]) {
    return dkr::runtime::enhancements::is_fullscreen_track_preview(
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],320,240);
}

extern "C" void dkr_probe_character_music_addresses(uint32_t a[6]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77 : dkr::runtime::revision_addresses::kUsV80;
    a[0]=t.BlockMusicChange;a[1]=t.MusicNextSequence;a[2]=t.DynamicMusicChannelMask;
    a[3]=t.MenuCurrentCharacter;a[4]=t.MenuSelectedCharacter;a[5]=t.MusicTempo;
}
extern "C" uint32_t dkr_probe_character_music_mask(int selected) {
    return dkr::runtime::enhancements::character_music_channel_mask(selected);
}
extern "C" uint32_t dkr_probe_character_animation_advance(uint32_t bits,int rate,int tempo) {
    return std::bit_cast<uint32_t>(dkr::runtime::enhancements::advance_character_select_phase(
        std::bit_cast<float>(bits),rate,tempo));
}
extern "C" int dkr_probe_character_animation_valid(uint32_t active,uint32_t bits) {
    const auto phase=std::bit_cast<float>(bits);
    return active<=1 && std::isfinite(phase) && phase>=0 && phase<1 && (active || !bits);
}

extern "C" void dkr_probe_roster_addresses(uint32_t addresses[7]) {
    const auto& t=DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77 : dkr::runtime::revision_addresses::kUsV80;
    addresses[0]=t.NumberOfActivePlayers;addresses[1]=t.NumberOfReadyPlayers;
    addresses[2]=t.ActivePlayersArray;addresses[3]=t.CharacterSelectStatus;
    addresses[4]=t.PlayersCharacterArray;addresses[5]=t.PlayerIdMap;addresses[6]=t.MenuButtons;
}
extern "C" void dkr_probe_roster_seed(uint32_t mask,int8_t active[4],int8_t characters[8],uint8_t ids[16]) {
    std::array<bool,4> occupied{};std::array<int8_t,8> initial;initial.fill(-1);
    for(unsigned p=0;p<4;++p)occupied[p]=(mask&(1U<<p))!=0;
    const auto seed=dkr::runtime::netplay::make_character_select_seed(occupied,initial);
    std::copy(seed.active_players.begin(),seed.active_players.end(),active);
    std::copy(seed.characters.begin(),seed.characters.end(),characters);
    std::copy(seed.player_ids.begin(),seed.player_ids.end(),ids);
}
extern "C" uint32_t dkr_probe_roster_buttons(int occupied,int8_t status,uint32_t buttons) {
    return dkr::runtime::netplay::locked_character_select_buttons(occupied!=0,status,buttons);
}

extern "C" void dkr_probe_title_tail_addresses(uint32_t addresses[2]) {
    const auto& table = DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77 : dkr::runtime::revision_addresses::kUsV80;
    addresses[0]=table.TitleDemoIndex; addresses[1]=table.TitleRevealTimer;
}
extern "C" int dkr_probe_title_tail_valid(uint32_t phase,uint32_t units) {
    return dkr::runtime::intro::TailGate::valid({phase,units});
}
extern "C" int dkr_probe_title_tail_update(uint32_t* phase,uint32_t* units,
    int complete,int first,int revealed,uint32_t rate) {
    dkr::runtime::intro::TailGate gate;
    if(!gate.restore({*phase,*units}))return -1;
    const auto action=gate.update(complete!=0,first!=0,revealed!=0,rate);
    const auto value=gate.capture();*phase=value.phase;*units=value.held_update_units;
    return static_cast<int>(action);
}

extern "C" int dkr_probe_valid_scissor_pointer(uint32_t command) {
    return dkr::runtime::scheduler::is_rdram_word_address(command,7);
}
extern "C" int dkr_probe_valid_preview_pointer(uint32_t viewport) {
    return dkr::runtime::scheduler::is_rdram_word_address(viewport,0x2FU);
}

extern "C" uint32_t dkr_probe_fullscreen_clear_scissor(uint32_t lower_right) {
    return dkr::runtime::presentation::correct_fullscreen_clear_scissor(lower_right,320,240);
}

extern "C" void dkr_probe_void_basis_addresses(uint32_t addresses[4]) {
    // Immutable tables, not the mutable live revision aliases.
    const auto& table = DKR_PROBE_REVISION==77 ?
        dkr::runtime::revision_addresses::kUsV77 : dkr::runtime::revision_addresses::kUsV80;
    addresses[0]=table.VoidLateralX; addresses[1]=table.VoidLateralZ;
    addresses[2]=table.VoidCentreX; addresses[3]=table.VoidCentreZ;
}

extern "C" void dkr_probe_sky_scales(int layout,float* horizontal,float* vertical) {
    *horizontal=dkr::runtime::enhancements::split_sky_horizontal_cover_scale(1.0F,layout);
    *vertical=dkr::runtime::enhancements::split_sky_vertical_cover_scale(1.0F,layout);
}

extern "C" int dkr_probe_valid_material_pointer(uint32_t address,uint32_t offset) {
    return dkr::runtime::steering_wheel::is_valid_render_pointer(address,offset);
}
extern "C" uint32_t dkr_probe_material_flags(const uint32_t f[13],uint32_t effective) {
    // Reuse the actual production predicate, not a private approximation.
    dkr::runtime::steering_wheel::BatchIdentity batch;
    batch.behaviour=static_cast<uint16_t>(f[0]); batch.vehicle=static_cast<int8_t>(f[1]);
    batch.batch_count=static_cast<uint16_t>(f[2]); batch.batch_index=static_cast<uint16_t>(f[3]);
    batch.texture_index=static_cast<uint8_t>(f[4]); batch.vertex_count=static_cast<int32_t>(f[5]);
    batch.triangle_count=static_cast<int32_t>(f[6]); batch.authored_flags=f[7];
    batch.texture_width=static_cast<uint8_t>(f[8]); batch.texture_height=static_cast<uint8_t>(f[9]);
    batch.texture_format=static_cast<uint8_t>(f[10]); batch.texture_flags=static_cast<uint16_t>(f[11]);
    batch.texture_asset_matches=f[12]!=0;
    return dkr::runtime::steering_wheel::corrected_material_flags(batch,effective);
}
