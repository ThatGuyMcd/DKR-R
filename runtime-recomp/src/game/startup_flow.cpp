#include "startup_flow.hpp"
#include "revision_addresses.hpp"
#include "recomp.h"

#include <cstdio>

extern "C" void dkr_skip_intro_audio(std::uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !dkr::runtime::startup::skip_logos())
        return;
    using namespace dkr::runtime::revision_addresses;
    const bool v80 = selected_revision() == dkr::runtime::rom::Revision::UsV80;
    // The logo animation can leave a music-change lock/fade in flight when
    // its sixteen-second lifetime is shortened. Reset only that ownership
    // boundary BEFORE retail queues its title song and loads the cinematic.
    // Equivalent to music_change_on + music_volume_reset, not a forced song.
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(BlockMusicChange))) = 0;
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(v80 ? 0x801162B8U : 0x80115D38U))) = 0;
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(v80 ? 0x801162BCU : 0x80115D3CU))) = 0;
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(v80 ? 0x800DCBC0U : 0x800DC650U))) = 0x3F800000;
}

extern "C" void dkr_skip_intro_logos(std::uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !dkr::runtime::startup::skip_logos())
        return;
    const auto timer = dkr::runtime::revision_addresses::selected_revision() == dkr::runtime::rom::Revision::UsV80
                           ? 0x801269F0U
                           : 0x80126450U;
    // Float zero. Let retail disable its logo viewport, initialize EEPROM
    // unlocks/language/subtitles and construct the title screen normally.
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(timer))) = 0;
}

extern "C" void dkr_skip_intro_title(std::uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !dkr::runtime::startup::consume_title_skip())
        return;
    // Preserve the FIRST retail cinematic, reveal timing, table and music.
    // Skipping the publisher logos must not advance to a racing demo.
    const bool characters =
        dkr::runtime::startup::launch_destination.load() == dkr::runtime::startup::Destination::CharacterSelect;
    dkr::runtime::startup::character_select_pending.store(characters);
    std::fprintf(stderr, "[boot][startup] skipped logos; destination=%s\n",
                 characters ? "character-select" : "original-start-menu");
}

extern "C" void dkr_skip_intro_character_select(std::uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !dkr::runtime::startup::consume_character_select())
        return;
    using namespace dkr::runtime::revision_addresses;
    // The initialized title's normal exit owns asset/font cleanup, unlock
    // variants, charselect_prev and loading the selection scene. Request that
    // path, rather than changing a menu ID or injecting a held Start button.
    // Equivalent to titlescreen_controller_assign(0), which has only these
    // writes. Keep launcher player-1 input ownership and allow others to join.
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(NumberOfActivePlayers))) = 1;
    for (int i = 0; i < 4; ++i) {
        MEM_B(i, static_cast<gpr>(static_cast<std::int32_t>(PlayersCharacterArray))) = i == 0 ? 1 : -1;
        MEM_B(i, static_cast<gpr>(static_cast<std::int32_t>(ActivePlayersArray))) = i == 0 ? 1 : 0;
    }
    MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(MenuDelay))) = 31;
}
