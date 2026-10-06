#pragma once
#include "legacy_character_menu.hpp"
#include "legacy_runtime_io.hpp"

namespace dkr::mods {
struct CharacterPresentationCalls {
    OriginalPiHandler allocate=nullptr,texture_load=nullptr,bank_relocate=nullptr,
        bank_play=nullptr,parameter=nullptr,spatial_point=nullptr;
};
// Session-owned resources, never donor-global substitutions. Queued native
// audio and result-menu references may outlive character-select and a race.
class CharacterPresentation {
public:
    void initialize(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,std::span<const std::uint32_t> sample_addresses);
    // Unused library entries allocate no guest resources. Loaded resources
    // remain immutable until shutdown, including previously queued sounds.
    void prepare(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,unsigned index,std::uint32_t sample_address,bool audio);
    std::uint32_t selection_bank(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,unsigned index,std::uint32_t sample_address);
    std::uint32_t portrait(const CharacterRoster&,const std::vector<AllocatedCharacter>&,
        unsigned racer)const;
    // A HUD override belongs to one native draw invocation, not a donor ID or
    // global HUD cache. Stack + element identity excludes nested/other draws.
    void bind_hud(std::uint32_t stack,std::uint32_t hud,const CharacterRoster&,
        const std::vector<AllocatedCharacter>&,unsigned racer);
    void unbind_hud(std::uint32_t stack);
    std::uint32_t hud_lookup(std::uint32_t stack,std::uint32_t hud,unsigned sprite)const;
    unsigned cinematic_id(const CharacterRoster&,const std::vector<AllocatedCharacter>&,
        unsigned racer,unsigned native_id);
    std::uint32_t cinematic_portrait(unsigned id)const;
    unsigned sound(const CharacterRoster&,const std::vector<AllocatedCharacter>&,
        unsigned racer,unsigned native_character,unsigned native_sound)const;
    // kind 0: native spatial AudioPoint creation, 1: direct sound-bank play,
    // 2: parameterized sound-table play (e.g. the native spatial horn wrapper).
    bool play(std::span<std::uint8_t>,const recomp_context&,const CharacterPresentationCalls&,
        const std::vector<AllocatedCharacter>&,unsigned kind)const;
    Bytes checkpoint()const;
    CharacterPresentation stage_checkpoint(View,std::size_t character_count)const;
private:
    std::vector<std::uint32_t> portrait_cells_,banks_,selection_banks_;
    std::array<std::uint32_t,8> cinematic_cells_{};
    struct HudBinding {std::uint32_t stack,hud,cell;unsigned sprite;};
    std::vector<HudBinding> hud_bindings_;
    bool ready_=false;
};
}
