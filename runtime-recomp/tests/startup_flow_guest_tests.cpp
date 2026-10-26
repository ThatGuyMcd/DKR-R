#include "startup_flow.hpp"
#include "revision_addresses.hpp"
#include "recomp.h"

#include <cassert>
#include <cstdio>
#include <vector>

extern "C" void dkr_skip_intro_logos(std::uint8_t*, recomp_context*);
extern "C" void dkr_skip_intro_audio(std::uint8_t*, recomp_context*);
extern "C" void dkr_skip_intro_title(std::uint8_t*, recomp_context*);
extern "C" void dkr_skip_intro_character_select(std::uint8_t*, recomp_context*);

int main() {
    using namespace dkr::runtime;
    std::vector<std::uint8_t> memory(8U * 1024U * 1024U);
    auto* rdram = memory.data();
    recomp_context registers{};
    auto* ctx = &registers;
    for (const auto revision : {rom::Revision::UsV77, rom::Revision::UsV80}) {
        assert(revision_addresses::select(revision));
        const gpr timer = static_cast<std::int32_t>(revision == rom::Revision::UsV80 ? 0x801269F0U : 0x80126450U);
        const gpr index = static_cast<std::int32_t>(revision_addresses::TitleDemoIndex);
        const gpr reveal = static_cast<std::int32_t>(revision_addresses::TitleRevealTimer);
        const gpr base = static_cast<std::int32_t>(0x80010000U);
        for (const auto destination : {startup::Destination::StartMenu, startup::Destination::CharacterSelect})
            for (const bool online : {false, true}) {
                MEM_W(0, timer) = 0x41800000; // retail 16 seconds
                MEM_W(0, index) = 0;
                MEM_W(0, reveal) = 0;
                MEM_B(3, base) = 5;
                MEM_B(4, base) = -2;
                MEM_B(5, base) = 7;
                ctx->r2 = base;
                ctx->r4 = 0x1234;
                const auto table_before = MEM_W(0, base);
                MEM_W(0, static_cast<std::int32_t>(revision_addresses::MenuDelay)) = 0;
                startup::prepare_launch(true, online, false, destination);
                const gpr music_lock = static_cast<std::int32_t>(revision_addresses::BlockMusicChange);
                const gpr fade =
                    static_cast<std::int32_t>(revision == rom::Revision::UsV80 ? 0x800DCBC0U : 0x800DC650U);
                const gpr fade_timer =
                    static_cast<std::int32_t>(revision == rom::Revision::UsV80 ? 0x801162B8U : 0x80115D38U);
                const gpr fade_length =
                    static_cast<std::int32_t>(revision == rom::Revision::UsV80 ? 0x801162BCU : 0x80115D3CU);
                MEM_W(0, music_lock) = 1;
                MEM_W(0, fade) = 0;
                MEM_W(0, fade_timer) = 60;
                MEM_W(0, fade_length) = -120;
                dkr_skip_intro_audio(rdram, ctx);
                assert(MEM_W(0, music_lock) == (online ? 1 : 0));
                assert(MEM_W(0, fade) == (online ? 0 : 0x3F800000));
                assert(MEM_W(0, fade_timer) == (online ? 60 : 0));
                assert(MEM_W(0, fade_length) == (online ? -120 : 0));
                assert(ctx->r4 == 0x1234 && ctx->r2 == base);
                dkr_skip_intro_logos(rdram, ctx);
                assert(MEM_W(0, timer) == (online ? 0x41800000 : 0));
                dkr_skip_intro_title(rdram, ctx);
                MEM_W(0, music_lock) = 1;
                dkr_skip_intro_audio(rdram, ctx);
                assert(MEM_W(0, music_lock) == 1); // no later title/music interference
                assert(ctx->r2 == base);           // FIRST sequence, never the first racing demo
                assert(ctx->r4 == 0x1234);
                assert(MEM_W(0, index) == 0);
                assert(MEM_W(0, reveal) == 0);          // normal cinematic/reveal
                assert(MEM_W(0, base) == table_before); // never rewrite the retail table
                dkr_skip_intro_character_select(rdram, ctx);
                const bool selected = !online && destination == startup::Destination::CharacterSelect;
                const auto delay = static_cast<std::int32_t>(revision_addresses::MenuDelay);
                assert(MEM_W(0, delay) == (selected ? 31 : 0));
                if (selected) {
                    assert(MEM_W(0, static_cast<std::int32_t>(revision_addresses::NumberOfActivePlayers)) == 1);
                    for (int i = 0; i < 4; ++i) {
                        assert(MEM_B(i, static_cast<std::int32_t>(revision_addresses::PlayersCharacterArray)) ==
                               (i == 0 ? 1 : -1));
                        assert(MEM_B(i, static_cast<std::int32_t>(revision_addresses::ActivePlayersArray)) ==
                               (i == 0 ? 1 : 0));
                    }
                    MEM_W(0, delay) = 0;
                    dkr_skip_intro_character_select(rdram, ctx);
                    assert(MEM_W(0, delay) == 0); // one shot
                }
                ctx->r2 = base;
                dkr_skip_intro_title(rdram, ctx);
                assert(ctx->r2 == base); // one shot per offline launch
            }
        startup::prepare_launch(true, false, false);
        MEM_B(3, base) = -1; // terminator, no usable second demo
        ctx->r2 = base;
        dkr_skip_intro_title(rdram, ctx);
        assert(ctx->r2 == base);
        assert(!startup::skip_logos());
        startup::prepare_launch(true, false, false);
        ctx->r2 = static_cast<std::int32_t>(0x80800000U);
        dkr_skip_intro_title(rdram, ctx);
        assert(static_cast<std::uint32_t>(ctx->r2) == 0x80800000U);
    }
    std::puts("Both retail revisions: title hooks, invalid assets, table preservation and online no-op passed.");
}
