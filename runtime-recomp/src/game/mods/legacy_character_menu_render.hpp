#pragma once
#include "legacy_character_menu.hpp"
#include "legacy_runtime_assets.hpp"

namespace dkr::mods {
struct CharacterMenuDrawCalls {
    OriginalPiHandler allocate=nullptr,release=nullptr,load_texture=nullptr,free_texture=nullptr;
    OriginalPiHandler texture=nullptr,font=nullptr,colour=nullptr,background=nullptr,text=nullptr,reset=nullptr;
};
// Native drawing/resources only. Never hold an adapter mutex while these
// methods invoke guest functions, which can yield to the scheduler.
class CharacterMenuRenderer {
public:
    void initialize(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& context,
        const CharacterMenuDrawCalls& calls,const std::vector<AllocatedCharacter>& entries);
    void release(std::span<std::uint8_t> memory,recomp_context& context,const CharacterMenuDrawCalls& calls);
    void draw(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& context,
        const CharacterMenuDrawCalls& calls,const std::vector<AllocatedCharacter>& entries,const CharacterMenuView& view);
    void draw_hint(std::span<std::uint8_t>,const CharacterMenuFields&,recomp_context&,
        const CharacterMenuDrawCalls&,const std::vector<AllocatedCharacter>&,const CharacterMenuView&);
    Bytes checkpoint()const;
    CharacterMenuRenderer stage_checkpoint(View,std::size_t character_count)const;
private:
    std::uint32_t scratch_=0;
    std::vector<std::uint32_t> portraits_;
};
}
