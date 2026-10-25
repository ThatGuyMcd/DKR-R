#pragma once
#include "vehicle_resources.hpp"
#include "legacy_checkpoint.hpp"
#include "game_payload.hpp"
#include <functional>

namespace dkr::runtime::vehicles {
// Sourced from both revision-specific SDK symbol tables by the Patch Pipeline.
struct Symbols {
    std::uint32_t stage,vehicles,confirmed,ready,players,delay,buttons,stick_x,stick_y,
        display_list,dialogue_begin,dialogue_modes,tracks_mode,current_menu,
        object_display_list,object_matrix,view,menu_images,frame_height,player_images,arrows,race_images,rng_seed,
        current_player,racers_by_port;
};
const Symbols& symbols(bool revision80);
class World {
public:
    using Resources=dkr::mods::vehicles::Resources;
    explicit World(std::shared_ptr<const Resources>,bool revision80);
    // 100 initialize, 101 before option input, 102 after option input,
    // 103 draw options, 104 leave Track Select. True replaces that one call.
    bool menu(std::span<std::uint8_t>,recomp_context&,unsigned,const GamePayload&,
        const std::function<unsigned(unsigned)>& character_header);
    // Callbacks arrive from the currently executing normal/private guest
    // variant. Do not look up a global function from the other online world.
    void draw(std::span<std::uint8_t>,recomp_context&,const GamePayload&,
        RecompiledEntrypoint frame,RecompiledEntrypoint text_width);
    // Read-only identity query, apart from the scoped far-LOD cap restored by
    // render_end. It never allocates models, changes physics or advances time.
    std::uint32_t render_begin(std::span<std::uint8_t>,std::uint32_t object);
    void render_end(std::span<std::uint8_t>,std::uint32_t object,std::uint32_t scope);
    void wheel_scope(std::span<std::uint8_t>,std::uint32_t object,bool begin);
    void advance_wheels(std::span<std::uint8_t>,std::uint32_t object,float native_update_rate);
    // Reviewed primary-racer setup hooks, including builds with no custom
    // characters. Choices are made on the logic path, never by the renderer.
    void race(std::span<std::uint8_t>,const recomp_context&,unsigned event);
    dkr::mods::Bytes checkpoint()const;
    std::shared_ptr<World> stage_checkpoint(dkr::mods::View)const;
    const std::shared_ptr<const Resources>& resources()const{return resources_;}
private:
    void initialize(std::span<std::uint8_t>,recomp_context&,const GamePayload&);
    void release(std::span<std::uint8_t>,recomp_context&,const GamePayload&);
    unsigned slot(std::span<std::uint8_t>,std::uint32_t object,std::uint32_t racer)const;
    unsigned choice(unsigned slot)const;
    std::shared_ptr<const Resources> resources_;
    bool rev80_=false,in_tracks_=false,picker_=false,option_pending_=false;
    int before_=-2,next_=0;
    unsigned players_=0;
    std::array<unsigned,4> selected_{},committed_{};
    // Low 32 bits are one turn; upper bits retain direction/full revolutions.
    // This is deterministic simulation state, not a wall-clock animation.
    std::array<std::array<std::uint64_t,4>,8> wheel_phases_{};
    std::uint32_t wheel_epoch_=0;
    std::array<unsigned,8> cpu_choices_{},cpu_objects_{};
    unsigned race_humans_=0,race_racers_=0,spawn_slot_=0;
    std::uint64_t race_serial_=0;
    std::array<bool,4> active_{},ready_{};
    std::uint32_t scratch_=0;
    std::vector<std::uint32_t> portraits_;
    // Render-call scratch only. Never part of simulation/checkpoint state.
    std::uint32_t rendering_=0,wheel_=0;
    bool wheel_hidden_=false;
};
} // namespace dkr::runtime::vehicles
