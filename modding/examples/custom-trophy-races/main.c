#include <dkr/magic_codes.h>

typedef unsigned char U8;
typedef signed char S8;
typedef short S16;
typedef unsigned short U16;
static DkrU32 lap_times[8][100];
static DkrTrophyRound round_info;
static char cup_name[164], round_name[48];
static DkrU32 count, current, active, ranked, quit_pending;
static S8 saved_tracks[4];
static S8* track_ids;
static DkrS32 *trophy_world, *native_round, *tracks_mode, *voice_delay, *track_to_load;
static S8* vehicles;
static U8** header;
static DkrU32* current_hud;
static DkrU32* menu_dl;
static DkrU32* hud_dl;
static DkrS32* racers_count;
static DkrU32* racers_by_port;
static DkrS32* menu_option;
static DkrS32* menu_stage;
static DkrS32* menu_delay;
static DkrU32* result_options;
static U8* time_trial;
static DkrS32* result_option_count;

static void zero(void* p, DkrU32 bytes) {
    U8* b = p;
    while (bytes--)
        *b++ = 0;
}
static DkrU32 ptr(const void* p) {
    return (DkrU32)p;
}
static DkrU64 call5(const char* name, DkrU32 a, DkrU32 b, DkrU32 c, DkrU32 d, DkrU32 e) {
    DkrGameCall call = {0};
    DkrU32 stack[16] __attribute__((aligned(16))) = {0};
    call.r[4] = (DkrU64)(long long)(DkrS32)a;
    call.r[5] = (DkrU64)(long long)(DkrS32)b;
    call.r[6] = (DkrU64)(long long)(DkrS32)c;
    call.r[7] = (DkrU64)(long long)(DkrS32)d;
    call.r[29] = (DkrU64)(long long)(DkrS32)ptr(stack);
    stack[4] = e;
    dkr_game_call(name, &call);
    return call.r[2];
}
static void* symbol(const char* name) {
    return dkr_game_symbol(name);
}
static void line(DkrU32 dl, int y, const char* text) {
    call5("draw_text", dl, 160, (DkrU32)y, ptr(text), 12);
}
static unsigned decimal(char* p, unsigned value) {
    unsigned count = 0;
    char digits[12];
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    for (unsigned i = 0; i < count; ++i)
        p[i] = digits[count - i - 1];
    p[count] = 0;
    return count;
}
static void title(void) {
    char* p = round_name;
    const char* s = "ROUND ";
    while (*s)
        *p++ = *s++;
    p += decimal(p, current + 1);
    s = " OF ";
    while (*s)
        *p++ = *s++;
    decimal(p, count);
}
static void stop_cup(void) {
    if (track_ids)
        for (unsigned i = 0; i < 4; ++i)
            track_ids[i] = saved_tracks[i];
    track_ids = 0;
    active = 0;
    ranked = 0;
    current = 0;
    dkr_race_laps((DkrU32(*)[100])0);
}
DKR_CALLBACK(on_boot) void boot(void) {
    trophy_world = symbol("gTrophyRaceWorldId");
    native_round = symbol("gTrophyRaceRound");
    tracks_mode = symbol("gIsInTracksMode");
    voice_delay = symbol("gTrackNameVoiceDelay");
    track_to_load = symbol("gTrackIdToLoad");
    vehicles = symbol("gPlayerSelectVehicle");
    header = symbol("gCurrentLevelHeader");
    current_hud = symbol("gCurrentHud");
    menu_dl = symbol("sMenuCurrDisplayList");
    hud_dl = symbol("gHudDL");
    racers_count = symbol("gNumRacers");
    racers_by_port = symbol("gRacersByPort");
    time_trial = symbol("gIsTimeTrial");
    menu_option = symbol("gMenuOption");
    menu_stage = symbol("gMenuStage");
    menu_delay = symbol("gMenuDelay");
    result_options = symbol("gResultOptionText");
    result_option_count = symbol("gResultOptionCount");
}
DKR_CALLBACK(before_menu_init) void menu_enter(DkrU32 menu) {
    if (quit_pending && menu == 20) {
        DkrGameCall call;
        dkr_game_context_read(&call);
        call.r[4] = 15;
        dkr_game_context_write(&call);
        quit_pending = 0;
        stop_cup();
        return;
    }
    if (active && (menu == 15 || menu == 3 || menu == 0 || menu == 1 || menu == 23))
        stop_cup();
}
DKR_CALLBACK(before_cinematic_start) void ceremony_begin(void) {
    if (active && ranked && current + 1 >= count)
        stop_cup();
}
DKR_CALLBACK(before_menu_trophy_race_round_init) void round_begin(void) {
    /* Only an explicit additive custom-cup tile activates this mod. Both
     * Adventure and the original Tracks-mode trophies stay native. */
    if (!*tracks_mode)
        return;
    count = dkr_trophy_info(cup_name, sizeof(cup_name));
    if (!count)
        return;
    if (!active) {
        active = 1;
        current = 0;
        ranked = 0;
        track_ids = (S8*)(DkrU32)dkr_call4("get_misc_asset", 26, 0, 0, 0);
        for (unsigned i = 0; i < 4; ++i)
            saved_tracks[i] = track_ids[i];
    } else if (ranked) {
        ++current;
        ranked = 0;
    }
    dkr_trophy_round(current, &round_info);
    title();
    /* Keep every native 4-round/ceremony table index in bounds. The mod owns
     * the real 0..127 round index; retail only sees first/continuing/final. */
    *trophy_world = 1;
    *native_round = current ? 1 : 0;
    for (unsigned i = 0; i < 4; ++i)
        track_ids[i] = (S8)(round_info.carrier < 128 ? round_info.carrier : 0);
    *time_trial = 0;
    dkr_call4("set_time_trial_enabled", 0, 0, 0, 0);
    dkr_course_select(round_info.course, round_info.carrier);
}
DKR_CALLBACK(after_menu_trophy_race_round_init) void round_ready(void) {
    if (!active)
        return;
    *voice_delay = 0;
    for (unsigned i = 0; i < 4; ++i)
        vehicles[i] = (S8)round_info.vehicle;
    dkr_call4("set_level_default_vehicle", round_info.vehicle, 0, 0, 0);
}
DKR_CALLBACK(before_load_level_for_menu) void preview_begin(void) {
    if (!active || !*trophy_world)
        return;
    DkrGameCall call;
    dkr_game_context_read(&call);
    call.r[4] = round_info.carrier;
    dkr_game_context_write(&call);
    dkr_course_select(round_info.course, round_info.carrier);
}
DKR_CALLBACK(before_level_load) void scene_begin(DkrU32 course) {
    if (active && *trophy_world && course == round_info.carrier) {
        DkrGameCall call;
        dkr_game_context_read(&call);
        call.r[7] = round_info.vehicle;
        dkr_game_context_write(&call);
        dkr_course_select(round_info.course, round_info.carrier);
    }
}
DKR_CALLBACK(before_init_track) void race_begin(void) {
    if (!active || !*trophy_world || !*header)
        return;
    (*header)[0x4b] = (U8)round_info.laps;
    (*header)[0x4d] = (U8)round_info.vehicle;
    *time_trial = 0;
    zero(lap_times, sizeof(lap_times));
    dkr_race_laps(lap_times);
}
DKR_CALLBACK(after_track_setup_racers) void racers_ready(void) {
    if (!active || !*trophy_world)
        return;
    /* obj_init_racer runs INSIDE spawn_object, before track_setup_racers
     * assigns racerIndex. Register the completed roster, never spawn IDs.
     * The SDK also binds this roster at the first constructor settling tick. */
    DkrU32* objects = (DkrU32*)(*racers_by_port);
    for (int i = 0; objects && i < *racers_count && i < 8; ++i) {
        if (objects[i]) {
            DkrU32 racer = *(DkrU32*)(objects[i] + 0x64);
            if (racer)
                dkr_race_register((void*)racer);
        }
    }
}
DKR_CALLBACK(after_menu_trophy_race_round_loop) void round_leave(void) {
    if (!active)
        return;
    DkrGameCall call;
    dkr_game_context_read(&call);
    if ((DkrS32)call.r[2] > 0)
        *track_to_load = (DkrS32)round_info.carrier;
}
DKR_CALLBACK(before_trophyround_render) void render_round(void) {
    if (!active)
        return;
    dkr_game_skip_original();
    dkr_call4("set_text_background_colour", 0, 0, 0, 0);
    dkr_call4("set_text_font", 2, 0, 0, 0);
    call5("set_text_colour", 0, 0, 0, 255, 160);
    line(ptr(menu_dl), 35, cup_name);
    line(ptr(menu_dl), 67, "CUSTOM TROPHY RACE");
    call5("set_text_colour", 255, 255, 255, 0, 255);
    line(ptr(menu_dl), 32, cup_name);
    line(ptr(menu_dl), 64, "CUSTOM TROPHY RACE");
    line(ptr(menu_dl), 176, round_name);
    line(ptr(menu_dl), 208, round_info.name);
}
DKR_CALLBACK(before_menu_trophy_race_rankings_init) void rankings_begin(void) {
    if (!active)
        return;
    *native_round = current + 1 >= count ? 3 : 0;
}
DKR_CALLBACK(after_menu_trophy_race_rankings_init) void rankings_ready(void) {
    if (active) {
        ranked = 1;
        if (current + 1 < count)
            *result_option_count = 2;
    }
}
DKR_CALLBACK(before_menu_trophy_race_rankings_loop) void rankings_input(void) {
    if (active && current + 1 < count && *menu_stage == 3 && *menu_option == 1)
        quit_pending = 1;
}
DKR_CALLBACK(before_race_finish_time_trial) void finish_without_records(void) {
    if (!active || !*trophy_world)
        return;
    /* Settings.Racer only has THREE u16 lap times. Do not let retail's loop
     * write 4/5/99 laps through it or publish custom times as vanilla records. */
    dkr_game_skip_original();
    U8* settings = (U8*)(DkrU32)dkr_call4("get_settings", 0, 0, 0, 0);
    settings[0x114] = 0;
    settings[0x115] = 0;
    settings[0x116] = 0;
    settings[0x117] = 0;
    DkrU32* objects = (DkrU32*)(*racers_by_port);
    for (int i = 0; i < *racers_count && i < 8; ++i) {
        U8* racer = (U8*)(*(DkrU32*)(objects[i] + 0x64));
        unsigned index = racer[2];
        if (index >= 8)
            continue;
        U8* result = settings + 0x54 + index * 0x18;
        result[4] = 0;
        DkrU32 sum = 0;
        for (unsigned lap = 0; lap < round_info.laps; ++lap)
            sum += lap_times[index][lap];
        *(U16*)(result + 0x10) = (U16)(sum > 65535 ? 65535 : sum);
        for (unsigned lap = 0; lap < 3; ++lap)
            *(U16*)(result + 0x12 + lap * 2) = (U16)(lap < round_info.laps ? lap_times[index][lap] : 0);
    }
}
/* More than nine laps use two real digits, never an out-of-range sprite ID.
 * The second native draw is deferred to the C wrapper, outside the VM lease. */
static struct {
    DkrU32 address, stage;
    S16 digit;
    float x, offset;
} digits;
DKR_CALLBACK(before_hud_element_render) void hud_before(DkrU32 dl, DkrU32 mtx, DkrU32 vertex, DkrU32 element) {
    (void)dl;
    (void)mtx;
    (void)vertex;
    if (!active || !current_hud || !*current_hud || digits.stage)
        return;
    if (element != *current_hud + 4 * 32 && element != *current_hud + 6 * 32)
        return;
    S16 value = *(S16*)(element + 0x18);
    if (value < 10 || value > 99)
        return;
    digits.address = element;
    digits.stage = 1;
    digits.digit = value;
    digits.x = *(float*)(element + 0xc);
    digits.offset = 4.0f * *(float*)(element + 8);
    *(float*)(element + 0xc) = digits.x - digits.offset;
    *(S16*)(element + 0x18) = value / 10;
}
DKR_CALLBACK(after_hud_element_render) void hud_after(DkrU32 dl, DkrU32 mtx, DkrU32 vertex, DkrU32 element) {
    (void)dl;
    (void)mtx;
    (void)vertex;
    if (!digits.stage || digits.address != element)
        return;
    if (digits.stage == 1) {
        digits.stage = 2;
        *(float*)(element + 0xc) = digits.x + digits.offset;
        *(S16*)(element + 0x18) = digits.digit % 10;
        dkr_game_repeat_original();
    } else {
        *(float*)(element + 0xc) = digits.x;
        *(S16*)(element + 0x18) = digits.digit;
        digits.stage = 0;
    }
}
