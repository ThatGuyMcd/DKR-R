#include <dkr/development.h>

typedef unsigned char U8;
typedef short S16;
typedef union FloatWord {
    float value;
    DkrU32 bits;
} FloatWord;
static float pace[14];
static DkrS32* game_mode;
static U8** level_header;
static U8* time_trial;

/* The decomp's vehicle IDs, not character IDs. Custom character models retain
 * their carrier's vehicle physics. None of these addresses are ROM-specific. */
enum {
    CAR = 0,
    HOVERCRAFT = 1,
    PLANE = 2,
    TRICKY = 5,
    BLUEY = 6,
    SMOKEY = 7,
    PTERODACTYL = 8,
    BUBBLER = 11,
    WIZPIG = 12,
    ROCKET = 13
};

static float setting(const char* name) {
    double value = dkr_setting_number(name);
    if (!(value >= 50.0 && value <= 150.0))
        return 1.0f;
    return (float)(value * 0.01);
}
DKR_CALLBACK(on_boot) void boot(void) {
    game_mode = dkr_game_symbol("gGameMode");
    level_header = dkr_game_symbol("gCurrentLevelHeader");
    time_trial = dkr_game_symbol("gIsTimeTrial");
    for (unsigned i = 0; i < 14; ++i)
        pace[i] = 1.0f;
    pace[CAR] = pace[HOVERCRAFT] = pace[PLANE] = setting("race_ai");
    pace[TRICKY] = setting("tricky");
    pace[BLUEY] = setting("bluey");
    pace[SMOKEY] = pace[PTERODACTYL] = setting("smokey");
    pace[BUBBLER] = setting("bubbler");
    pace[WIZPIG] = setting("wizpig");
    pace[ROCKET] = setting("rocket");
}
static int native_range(DkrU32 address, DkrU32 bytes) {
    return address >= 0x80000400U && !(address & 3) && address <= 0x80800000U - bytes;
}
static float racer_pace(DkrU32 address) {
    if (!game_mode || *game_mode != 0 || !level_header || !native_range((DkrU32)*level_header, 0x50) || !time_trial ||
        *time_trial)
        return 1.0f;
    const unsigned race = (*level_header)[0x4c];
    /* Do not change menu previews, title demos, challenge arenas, ghosts,
     * hub traffic, Taj challenges or post-finish computer control. */
    if ((race != 0 && race != 3 && race != 8) || !native_range(address, 0x224))
        return 1.0f;
    const U8* racer = (const U8*)address;
    if (*(const S16*)racer != -1 || racer[0x1d8])
        return 1.0f;
    const unsigned vehicle = racer[0x1d6];
    return vehicle < 14 ? pace[vehicle] : 1.0f;
}
DKR_CALLBACK(after_handle_racer_top_speed) void physical_pace(DkrU32 object, DkrU32 racer) {
    (void)object;
    const float scale = racer_pace(racer);
    if (scale == 1.0f)
        return;
    /* Scale the freshly calculated native top-speed multiplier ONCE, not the
     * accumulated racer velocity. Keep native start/boost/banana decisions. */
    DkrGameCall call;
    dkr_game_context_read(&call);
    FloatWord result = {.bits = (DkrU32)call.f[0]};
    if (!(result.value > 0.0f && result.value <= 4.0f))
        return;
    result.value *= scale;
    call.f[0] = (call.f[0] & 0xffffffff00000000ULL) | result.bits;
    dkr_game_context_write(&call);
}
DKR_CALLBACK(before_func_8005B818) void distant_pace(DkrU32 object, DkrU32 racer) {
    (void)object;
    const float scale = racer_pace(racer);
    if (scale == 1.0f)
        return;
    /* Distant AI follows checkpoint splines instead of vehicle physics and
     * never calls handle_racer_top_speed. Scale ONLY this movement helper's
     * O32 float updateRateF (a3). Its integer timers, race clock, weapons and
     * global simulation cadence retain their original updateRate. */
    DkrGameCall call;
    dkr_game_context_read(&call);
    FloatWord rate = {.bits = (DkrU32)call.r[7]};
    if (!(rate.value > 0.0f && rate.value <= 16.0f))
        return;
    rate.value *= scale;
    call.r[7] = (DkrU64)(long long)(DkrS32)rate.bits;
    dkr_game_context_write(&call);
}

DKR_CALLBACK(on_live_setting) void update_live_settings(unsigned option, unsigned bits, unsigned tag) {
    (void)option;
    (void)bits;
    (void)tag;
    boot();
}
