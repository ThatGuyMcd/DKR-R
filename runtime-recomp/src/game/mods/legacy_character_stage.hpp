#pragma once
#include "legacy_character_menu_render.hpp"

namespace dkr::mods {
struct CharacterStageCalls {
    OriginalPiHandler spawn=nullptr,free=nullptr,animation_fraction=nullptr,particles=nullptr;
    OriginalPiHandler animation_override=nullptr,camera=nullptr;
};
// Menu-only actors, with independent sign counters. The original object/model
// allocator and destructor retain ownership of every guest allocation.
class CharacterStage {
public:
    void initialize(std::span<std::uint8_t>,const CharacterMenuFields&,recomp_context&,
        const CharacterStageCalls&,const std::vector<AllocatedCharacter>&);
    void release(std::span<std::uint8_t>,recomp_context&,const CharacterStageCalls&);
    // Called at the menu input boundary, never inside object iteration. Failure
    // leaves the prior visible roster intact for the caller to restore its cursor.
    bool synchronize(std::span<std::uint8_t>,recomp_context&,const CharacterStageCalls&,
        const std::vector<AllocatedCharacter>&,const CharacterMenuView&);
    bool update(std::span<std::uint8_t>,const CharacterMenuFields&,recomp_context&,
        const CharacterStageCalls&,const CharacterMenuView&);
    bool redirect_spawn(std::span<std::uint8_t>,recomp_context&);
    Bytes checkpoint()const;
    CharacterStage stage_checkpoint(View,std::size_t character_count)const;
private:
    struct Actor {std::uint32_t object=0;std::size_t entry=0;unsigned slot=0,sign_ticks=0,sign_index=0;std::vector<unsigned> sign_batches;};
    Actor spawn(std::span<std::uint8_t>,recomp_context&,const CharacterStageCalls&,const AllocatedCharacter&,std::size_t,unsigned);
    void retire(std::span<std::uint8_t>,recomp_context&,const CharacterStageCalls&,const Actor&);
    std::vector<Actor> actors_;
    std::vector<std::uint32_t> retiring_;
    CharacterMenuFields fields_{};
    std::uint32_t pending_entry_=0,pending_header_=0;
    unsigned pending_source_=0;
};
}
