#ifndef DKR_CUSTOM_MAGIC_CODES_H
#define DKR_CUSTOM_MAGIC_CODES_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __mips__
#error "Custom Magic Codes use the portable MIPS O32 target, not a host DLL target."
#endif
#if __SIZEOF_POINTER__ != 4
#error "The DKR SDK requires 32-bit guest pointers."
#endif

#define DKR_SDK_API 1U
#define DKR_IMPORT __attribute__((noinline, weak, used, section(".recomp_import.*")))
#define DKR_CALLBACK(event) __attribute__((used, retain, section(".recomp_callback.*:" #event)))
#define DKR_EXPORT __attribute__((used, retain, section(".recomp_export")))
#define DKR_HOOK(function) __attribute__((used, retain, section(".recomp_hook." #function)))
#define DKR_HOOK_RETURN(function) __attribute__((used, retain, section(".recomp_hook_return." #function)))
#define DKR_REPLACE __attribute__((used, retain, section(".recomp_patch")))

typedef unsigned int DkrU32;
typedef unsigned long long DkrU64;
typedef int DkrS32;
/* O32 native calls are explicit. Stack contains 16 outgoing argument words;
 * arguments 5+ begin at stack[4]. Pointer fields are guest addresses only. */
typedef struct DkrGameCall {
    DkrU64 r[32], f[32], hi, lo;
    DkrU32 fcsr, padding;
} DkrGameCall;
DKR_IMPORT void* dkr_game_symbol(const char* name) {
    (void)name;
    return (void*)0;
}
DKR_IMPORT DkrU32 dkr_game_symbol_size(const char* name) {
    (void)name;
    return 0;
}
DKR_IMPORT DkrU32 dkr_game_call(const char* name, DkrGameCall* call) {
    (void)name;
    (void)call;
    return 0;
}
DKR_IMPORT DkrU32 dkr_game_context_read(DkrGameCall* call) {
    (void)call;
    return 0;
}
DKR_IMPORT DkrU32 dkr_game_context_write(const DkrGameCall* call) {
    (void)call;
    return 0;
}
DKR_IMPORT DkrU32 dkr_game_skip_original(void) {
    return 0;
}
/* after_hud_element_render only: request another original draw outside the
 * VM/native lease. At most four original draws are permitted per wrapper. */
DKR_IMPORT DkrU32 dkr_game_repeat_original(void) {
    return 0;
}
/* racing v1 / exclusive system race-laps: 8 racers, 100 timing words each,
 * including a finish sentinel. Register the COMPLETED roster after
 * track_setup_racers, not obj_init_racer (racer IDs are assigned later).
 * The SDK binds the live completed roster on a first settling-tick access.
 * Pass NULL when leaving the custom race. Table and registrations rewind.
 * Native Time Trial must be off; its record/colour arrays are fixed-size.
 * A custom race must also bound/replace fixed native result lap arrays (see
 * custom-trophy-races), not merely increase LevelHeader.laps. */
DKR_IMPORT DkrU32 dkr_race_laps(DkrU32 table[8][100]) {
    (void)table;
    return 0;
}
DKR_IMPORT DkrU32 dkr_race_register(void* racer) {
    (void)racer;
    return 0;
}
typedef struct DkrTrophyRound {
    DkrU32 carrier, laps, vehicle;
    char name[164], course[164];
    DkrU32 reserved;
} DkrTrophyRound;
/* trophies v1: returns the admitted round count and optional 164-byte cup
 * name for the explicitly selected custom-cup tile, or zero for a vanilla
 * trophy/no selection. All admitted cups/order/assets are immutable; the
 * chosen menu identity is checkpointed with the owning game world. */
DKR_IMPORT DkrU32 dkr_trophy_info(char* name, DkrU32 capacity) {
    (void)name;
    (void)capacity;
    return 0;
}
DKR_IMPORT DkrU32 dkr_trophy_round(DkrU32 index, DkrTrophyRound* round) {
    (void)index;
    (void)round;
    return 0;
}
DKR_IMPORT DkrU32 dkr_course_select(const char* course, DkrU32 carrier) {
    (void)course;
    (void)carrier;
    return 0;
}
/* Module game: named callbacks use DKR_CALLBACK(before_function) and
 * DKR_CALLBACK(after_function). They preserve port hooks and work across the
 * supported ROM revisions. System/scheduler callbacks are not exposed.
 * Unknown symbols return NULL; unavailable/protected native calls stop safely.
 * Use dkr_game_context_* only within these callbacks, never on_frame/on_boot. */
static inline DkrU64 dkr_call4(const char* name, DkrU32 a, DkrU32 b, DkrU32 c, DkrU32 d) {
#ifdef __cplusplus
    DkrGameCall call = {};
#else
    DkrGameCall call = {0};
#endif
    DkrU32 stack[16] __attribute__((aligned(16))) = {0};
    call.r[4] = (DkrU64)(long long)(DkrS32)a;
    call.r[5] = (DkrU64)(long long)(DkrS32)b;
    call.r[6] = (DkrU64)(long long)(DkrS32)c;
    call.r[7] = (DkrU64)(long long)(DkrS32)d;
    call.r[29] = (DkrU64)(long long)(DkrS32)(DkrU32)stack;
    dkr_game_call(name, &call);
    return call.r[2];
}

/* These weak bodies are link metadata consumed by RecompModTool. The loader
 * resolves every import before activation; these bodies are never fallbacks. */
DKR_IMPORT DkrU32 dkr_sdk_version(void) {
    return 0;
}
DKR_IMPORT DkrU32 dkr_random_u32(void) {
    return 0;
}
DKR_IMPORT void* dkr_alloc(DkrU32 bytes) {
    (void)bytes;
    return (void*)0;
}
DKR_IMPORT void dkr_free(void* address) {
    (void)address;
}
DKR_IMPORT double dkr_setting_number(const char* key) {
    (void)key;
    return 0;
}
/* Returns required bytes, INCLUDING NUL. Null destination queries length.
 * A non-null destination must fit the entire value. Maximum 4096 bytes. */
DKR_IMPORT DkrU32 dkr_setting_string(const char* key, char* destination, DkrU32 capacity) {
    (void)key;
    (void)destination;
    (void)capacity;
    return 0;
}
DKR_IMPORT DkrU32 dkr_resource_size(const char* name) {
    (void)name;
    return 0;
}
/* Frozen structured configuration (canonical UTF-8 JSON, not a host file).
 * Unlike scalar String settings, a document may contain a complete cup.
 * Missing documents have size zero. Read in bounded 4096-byte chunks. */
DKR_IMPORT DkrU32 dkr_data_size(const char* name) {
    (void)name;
    return 0;
}
DKR_IMPORT DkrU32 dkr_data_read(const char* name, DkrU32 offset, void* destination, DkrU32 bytes) {
    (void)name;
    (void)offset;
    (void)destination;
    (void)bytes;
    return 0;
}
/* Reads at most 4096 bytes per call. Resource names are logical IDs declared
 * in dkr.json; they are not host paths. Imported files are read-only. */
DKR_IMPORT DkrU32 dkr_resource_read(const char* name, DkrU32 offset, void* destination, DkrU32 bytes) {
    (void)name;
    (void)offset;
    (void)destination;
    (void)bytes;
    return 0;
}
/* Logs are queued effects. The host must publish them only after confirmation
 * online. A message is at most 512 UTF-8 bytes; the queue holds 128 effects. */
DKR_IMPORT void dkr_log(const char* message) {
    (void)message;
}

#ifdef __cplusplus
}
#endif
#endif
