// Runs the actual recompiled racer_boss_finish, with isolated guest memory.
#include "recomp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void racer_boss_finish(uint8_t*, recomp_context*);
static int custom, two_players, music, transition, bear, swaps, assets, writes;
static uint8_t memory[0x20000];
static uint64_t digest = 14695981039346656037ULL;
static void hash(const void* data, size_t size) {
    const uint8_t* bytes = data;
    for (size_t i = 0; i < size; ++i)
        digest = (digest ^ bytes[i]) * 1099511628211ULL;
}
static void event(int id, recomp_context* ctx) {
    hash(&id, sizeof(id));
    hash(&ctx->r4, sizeof(gpr) * 4);
}
int dkr_custom_tracks_is_boss_level(int32_t level) {
    return custom && level == 70;
}
void get_settings(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = (int32_t)0x80001000;
}
void get_misc_asset(uint8_t* rdram, recomp_context* ctx) {
    assert(!custom); // A custom id has no entry; even attempting the scan is wrong.
    ++assets;
    ctx->r2 = (int32_t)(ctx->r4 == 0x43 ? 0x80005000 : 0x80006000);
}
void music_play(uint8_t* rdram, recomp_context* ctx) {
    music = ctx->r4;
    event(1, ctx);
}
void level_transition_begin(uint8_t* rdram, recomp_context* ctx) {
    transition = ctx->r4;
    event(2, ctx);
}
void instShowBearBar(uint8_t* rdram, recomp_context* ctx) {
    ++bear;
    event(3, ctx);
}
void is_in_two_player_adventure(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = two_players;
}
void swap_lead_player(uint8_t* rdram, recomp_context* ctx) {
    ++swaps;
    event(4, ctx);
}
void get_save_file_index(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}
void safe_mark_write_save_file(uint8_t* rdram, recomp_context* ctx) {
    ++writes;
    event(5, ctx);
}
void set_eeprom_settings_value(uint8_t* rdram, recomp_context* ctx) {
    ++writes;
    event(6, ctx);
}
void level_properties_push(uint8_t* rdram, recomp_context* ctx) {
    ++writes;
    event(7, ctx);
}

int main(int argc, char** argv) {
    custom = argc > 1 && atoi(argv[1]);
    uint8_t* rdram = memory;
    const int worlds[] = {0, 1, 5};
    for (int w = 0; w < 3; ++w)
        for (int cleared = 0; cleared <= 2; cleared += 2)
            for (int timer = 0; timer <= 2; ++timer)
                for (int place = 1; place <= 2; ++place)
                    for (two_players = 0; two_players <= 1; ++two_players) {
                        memset(memory, 0, sizeof(memory));
                        music = transition = bear = swaps = assets = writes = 0;
                        int level = custom ? 70 : 5;
                        MEM_W(4, (int32_t)0x80001000) = (int32_t)0x80004000;
                        MEM_B(0x48, (int32_t)0x80001000) = worlds[w];
                        MEM_B(0x49, (int32_t)0x80001000) = level;
                        MEM_W(4 * level, (int32_t)0x80004000) = cleared;
                        MEM_H(0x1AC, (int32_t)0x80002000) = place;
                        MEM_B(0, (int32_t)0x80003000) = timer;
                        // Stock course 5 is the second table entry, exercising the scan.
                        MEM_B(0, (int32_t)0x80005000) = 3;
                        MEM_B(1, (int32_t)0x80005000) = 9;
                        MEM_B(2, (int32_t)0x80005000) = 5;
                        MEM_B(3, (int32_t)0x80005000) = 10;
                        uint8_t settings[0x100], flags[0x400];
                        memcpy(settings, memory + 0x1000, sizeof(settings));
                        memcpy(flags, memory + 0x4000, sizeof(flags));
                        recomp_context ctx = {0};
                        ctx.r4 = (int32_t)0x80002000;
                        ctx.r5 = (int32_t)0x80003000;
                        ctx.r16 = 0x12345678;
                        ctx.r29 = (int32_t)0x80010000;
                        ctx.r31 = 0x1234;
                        racer_boss_finish(rdram, &ctx);
                        assert(ctx.r16 == 0x12345678 && ctx.r29 == (gpr)(int32_t)0x80010000 && ctx.r31 == 0x1234);
                        if (custom) {
                            assert(!assets && !writes);
                            assert(memcmp(settings, memory + 0x1000, sizeof(settings)) == 0);
                            assert(memcmp(flags, memory + 0x4000, sizeof(flags)) == 0);
                            assert(MEM_B(0, (int32_t)0x80003000) == (timer == 1 ? 2 : timer));
                            assert(music == (timer == 1 ? (place == 1 ? 0x3D : 0x3E) : 0));
                            assert(transition == (timer == 1 ? (place == 1 ? 4 : 3) : 0));
                            assert(bear == (timer == 1 && place == 1));
                            assert(swaps == (timer == 1 && place == 2 && two_players));
                        }
                        hash(memory, sizeof(memory));
                        hash(&ctx, sizeof(ctx));
                    }
    printf("%016llx\n", (unsigned long long)digest);
    return 0;
}
