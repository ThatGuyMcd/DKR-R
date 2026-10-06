#pragma once
#include "legacy_character_roster.hpp"
#include "legacy_runtime_io.hpp"
#include "recomp.h"

namespace dkr::mods {
struct CharacterAiState {
    unsigned humans=0,racers=0;
    std::optional<std::pair<std::uint32_t,unsigned>> spawn;
};
// Reviewed track_setup_racers ABI. Calls use a private scratch frame and never
// modify the intercepted CPU context. No disk, renderer or network work.
void dispatch_character_ai_event(CharacterRoster&,CharacterAiState&,std::span<std::uint8_t>,
    const recomp_context&,unsigned event,std::span<const std::uint8_t> unlocked,OriginalPiHandler random);
inline unsigned character_presentation_slot(unsigned player,unsigned racer_index){
    return player<4?player:(player==0xffff && racer_index<8?racer_index:8);
}
// The caller serializes its logical roster. No allocator, renderer, network,
// disk I/O, mutex or guest scheduler callback is used by this adapter.
void dispatch_character_event(CharacterRoster& roster,std::span<std::uint8_t> guest,
    recomp_context& ctx,unsigned event,std::uint32_t roster_address,
    std::optional<std::pair<std::uint32_t,unsigned>> ai_spawn=std::nullopt);
}
