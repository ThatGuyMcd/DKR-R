#ifndef DKR_DEVELOPMENT_H
#define DKR_DEVELOPMENT_H
#include "magic_codes.h"
#define DKR_SDK2_API 2U
#ifdef __cplusplus
extern "C" {
#endif
/* SDK 2 uses the same big-endian O32 ABI on every supported host. None of
 * these records contains a native process pointer or a platform-sized long. */
typedef enum Dkr2Status {
    DKR2_OK = 0,
    DKR2_END = 1,
    DKR2_INVALID = -1,
    DKR2_STALE = -2,
    DKR2_PHASE = -3,
    DKR2_AUTHORITY = -4,
    DKR2_UNAVAILABLE = -5,
    DKR2_BUDGET = -6,
    DKR2_CONFLICT = -7,
    DKR2_RELOAD = -8
} Dkr2Status;
typedef struct Dkr2Handle {
    unsigned slot, generation;
} Dkr2Handle;
typedef enum Dkr2ValueType { DKR2_FLOAT = 1, DKR2_SIGNED = 2, DKR2_UNSIGNED = 3 } Dkr2ValueType;
typedef union Dkr2Value {
    float real;
    int integer;
    unsigned bits;
} Dkr2Value;
typedef enum Dkr2Property {
    DKR2_X = 1,
    DKR2_Y,
    DKR2_Z,
    DKR2_PITCH,
    DKR2_YAW,
    DKR2_ROLL,
    DKR2_SCALE,
    DKR2_OPACITY,
    DKR2_VELOCITY_X,
    DKR2_VELOCITY_Y,
    DKR2_VELOCITY_Z,
    DKR2_BEHAVIOUR,
    DKR2_OBJECT_ID,
    DKR2_SEGMENT,
    DKR2_RACER_SPEED = 100,
    DKR2_RACER_LATERAL_SPEED,
    DKR2_RACER_BANANAS,
    DKR2_RACER_ITEM,
    DKR2_RACER_AMMO,
    DKR2_RACER_ITEM_LEVEL,
    DKR2_RACER_BOOST_TICKS,
    DKR2_RACER_SHIELD_TICKS,
    DKR2_RACER_AI_SKILL,
    DKR2_RACER_THROTTLE,
    DKR2_RACER_BRAKE,
    DKR2_RACER_BUOYANCY,
    DKR2_RACER_PLAYER,
    DKR2_RACER_INDEX,
    DKR2_RACER_CHARACTER,
    DKR2_RACER_VEHICLE,
    DKR2_RACER_LAP,
    DKR2_RACER_CHECKPOINT,
    DKR2_RACER_PLACE,
    DKR2_RACER_FINISHED,
    DKR2_RACER_SHIELD_TYPE,
    DKR2_AMBIENT = 200,
    DKR2_DIFFUSE,
    DKR2_LIGHT_R,
    DKR2_LIGHT_G,
    DKR2_LIGHT_B,
    DKR2_LIGHT_INTENSITY,
    DKR2_LIGHT_X,
    DKR2_LIGHT_Y,
    DKR2_LIGHT_Z,
    DKR2_TRIGGER_RADIUS = 300,
    DKR2_TRIGGER_VEHICLE
} Dkr2Property;
enum {
    DKR2_PROPERTY_WRITE = 1,
    DKR2_PROPERTY_RACER = 2,
    DKR2_PHASE_READ = 1,
    DKR2_PHASE_TICK = 2,
    DKR2_PHASE_DRAW = 4
};
typedef struct Dkr2PropertyInfo {
    unsigned size, id, type, flags;
    Dkr2Value minimum, maximum;
    unsigned phase, reserved;
    char name[48], unit[16];
} Dkr2PropertyInfo;
typedef struct Dkr2Entity {
    unsigned size;
    Dkr2Handle handle;
    unsigned behaviour, object_id, racer, reserved;
    float x, y, z;
} Dkr2Entity;
#define DKR2_ENTITIES_RACERS 0x80000000U
typedef struct Dkr2Read {
    Dkr2Handle handle;
    unsigned property;
    Dkr2Value value;
    int status;
} Dkr2Read;
/* A batch is all-or-nothing. Up to 32 edits, unique target/property pairs.
 * Mutations run at a deterministic tick callback, never a draw callback. */
typedef struct Dkr2Edit {
    Dkr2Handle handle;
    unsigned property;
    Dkr2Value value;
} Dkr2Edit;
typedef struct Dkr2Session {
    unsigned size, api, revision, online;
    unsigned scene_generation, tick, entities, racers;
    unsigned development, phase, instruction_budget, native_budget;
} Dkr2Session;
/* arguments[0..11] are user values; arguments[12] is the expected scene
 * generation inserted by the host. Do not use the final word as a parameter. */
typedef struct Dkr2Action {
    unsigned owner, id, action, arguments[13];
} Dkr2Action;
typedef struct Dkr2Cast {
    unsigned size, segment, flags;
    float radius, from[3], to[3];
} Dkr2Cast;
typedef struct Dkr2Hit {
    unsigned size, hit;
    float fraction, point[3], normal[3];
    unsigned segment, triangle, texture;
} Dkr2Hit;
enum { DKR2_CAST_INTERACTION_SPHERES = 1, DKR2_CAST_SKIP_STATIC = 2 };
typedef enum Dkr2EventType {
    DKR2_EVENT_SCENE = 1,
    DKR2_EVENT_SPAWN = 2,
    DKR2_EVENT_DESPAWN = 3,
    DKR2_EVENT_LAP = 4,
    DKR2_EVENT_CHECKPOINT = 5,
    DKR2_EVENT_ITEM = 6,
    DKR2_EVENT_RESULT = 7,
    DKR2_EVENT_OVERFLOW = 31
} Dkr2EventType;
typedef struct Dkr2Event {
    unsigned type, tick;
    Dkr2Handle handle;
    unsigned property, previous, value, auxiliary;
} Dkr2Event;
/* Subscribe with 1U << event_type. The bounded queue and previous racer values
 * are rewindable. Overflow is explicit, never an unbounded allocation. */
DKR_IMPORT int dkr2_events(unsigned mask) {
    (void)mask;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_event_next(Dkr2Event* event) {
    (void)event;
    return DKR2_UNAVAILABLE;
}
/* Segment UINT_MAX searches intersected segment bounds. Radius 0 is a ray;
 * positive radius sweeps a sphere against static scene triangles. Texture is
 * the render batch's texture ID, not a claimed physics surface type. */
DKR_IMPORT int dkr2_cast(const Dkr2Cast* query, Dkr2Hit* result) {
    (void)query;
    (void)result;
    return DKR2_UNAVAILABLE;
}
typedef enum Dkr2Operation {
    DKR2_TELEPORT = 1,
    DKR2_CLONE_SCENERY = 2,
    DKR2_DELETE_SPAWN = 3,
    DKR2_EDIT_CHECKPOINT = 4,
    DKR2_SET_FOG = 10,
    DKR2_SET_WEATHER = 11,
    DKR2_SOUND = 20,
    DKR2_MUSIC = 21,
    DKR2_STOP_MUSIC = 22,
    DKR2_FADE_MUSIC = 23,
    DKR2_RESTART_SCENE = 30,
    DKR2_WARP_COURSE = 31,
    DKR2_RETURN_MENU = 32,
    DKR2_SET_RACE_LAPS = 40
} Dkr2Operation;
/* on_tool is a retired-boundary callback, also delivered while gameplay is
 * paused. 0 resets controls, 1 pauses, 2 grants exactly one update, 3 sets
 * retail logic units per update (1..4; normal owned rate is 2). Audio mixing
 * and network time continue. New scenes/menus reset control ownership. */
DKR_IMPORT int dkr2_control(unsigned operation, unsigned value) {
    (void)operation;
    (void)value;
    return DKR2_UNAVAILABLE;
}
typedef struct Dkr2Route {
    unsigned size, index, count, main_count;
    Dkr2Handle object;
    float position[3], normal[3], width, distance;
    int alternate;
    unsigned reserved;
} Dkr2Route;
/* Native checkpoint/AI racing line, including alternate routes. Node edits use
 * EDIT_CHECKPOINT and rebuild all native planes/distances in one safe call.
 * This does not resize or silently overwrite the source track asset. */
DKR_IMPORT int dkr2_route(unsigned index, Dkr2Route* result) {
    (void)index;
    (void)result;
    return DKR2_UNAVAILABLE;
}
/* Scoped scene documents. Publish a bounded JSON payload (<=12000 bytes).
 * Export/import is a user action in the tools panel; imports are edition,
 * revision and logical-course checked, uploaded through ordered commands,
 * and delivered as the document field's import_action. Read in <=4096-byte
 * chunks, validate the complete payload before editing, then accept it once.
 * No import may silently overwrite a source track or an ordinary save. */
DKR_IMPORT int dkr2_document_publish(const char* id, const char* json, unsigned bytes) {
    (void)id;
    (void)json;
    (void)bytes;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_document_size(const char* id) {
    (void)id;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_document_read(const char* id, unsigned offset, void* output, unsigned bytes) {
    (void)id;
    (void)offset;
    (void)output;
    (void)bytes;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_document_accept(const char* id) {
    (void)id;
    return DKR2_UNAVAILABLE;
}
typedef struct Dkr2Debug {
    unsigned size, kind, rgba, view;
    float a[3], b[3];
    char label[48];
} Dkr2Debug;
enum { DKR2_DEBUG_LINE = 1, DKR2_DEBUG_BOX = 2, DKR2_DEBUG_LABEL = 3 };
/* At most 64 world-space primitives per package. Local retained-camera
 * projection and viewport clipping only; no new world render or GPU rays.
 * count=0 clears the local list. Labels are printable ASCII, max 48 bytes. */
DKR_IMPORT int dkr2_debug(const Dkr2Debug* primitives, unsigned count) {
    (void)primitives;
    (void)count;
    return DKR2_UNAVAILABLE;
}
typedef struct Dkr2Tune {
    unsigned size;
    Dkr2Handle handle;
    float weight, handling, acceleration, speed_cap;
    unsigned mask;
} Dkr2Tune;
enum { DKR2_TUNE_WEIGHT = 1, DKR2_TUNE_HANDLING = 2, DKR2_TUNE_ACCELERATION = 4, DKR2_TUNE_SPEED_CAP = 8 };
/* Per-racer scene overrides. Weight/handling/acceleration factors 0.1..4;
 * speed cap 0 (off)..100. Acceleration curves are reviewed for vehicles 0..2;
 * boss updates support the other fields. Zero mask resets that racer; a zero
 * handle with zero mask resets this owner's overrides. Native scratch is
 * restored before the next actor. Only one tuning provider may own a session. */
DKR_IMPORT int dkr2_tune(const Dkr2Tune* tuning) {
    (void)tuning;
    return DKR2_UNAVAILABLE;
}
/* Skips only the reviewed AI racer update, never object ownership, graphics,
 * audio, input, loader or scheduler work. Reset on a scene unload. */
DKR_IMPORT int dkr2_freeze_cpus(unsigned enabled) {
    (void)enabled;
    return DKR2_UNAVAILABLE;
}
/* Commands use 16 O32 words. See the reference for each reviewed operation.
 * Unavailable handlers report DKR2_UNAVAILABLE, never a silent no-op. */
DKR_IMPORT int dkr2_operation_available(unsigned operation) {
    (void)operation;
    return 0;
}
DKR_IMPORT int dkr2_operation(unsigned operation, const unsigned arguments[16], unsigned result[16]) {
    (void)operation;
    (void)arguments;
    (void)result;
    return DKR2_UNAVAILABLE;
}
/* tools-ui is an immutable schema resource. Published values are copied,
 * throttled, local and read-only. Action polling uses on_tool (also delivered
 * while paused), or an ordinary deterministic tick. Online copied inspector
 * targets are published only at agreed boundaries, never speculative births. */
DKR_IMPORT int dkr2_ui_publish(const char* json, unsigned bytes) {
    (void)json;
    (void)bytes;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_action_next(Dkr2Action* action) {
    (void)action;
    return DKR2_UNAVAILABLE;
}
/* Live numeric/enum overrides are checkpointed, package-local and reset on
 * relaunch. Frozen launch settings and resource dependency identity stay put. */
DKR_IMPORT int dkr2_setting_set(const char* id, const Dkr2Value* value) {
    (void)id;
    (void)value;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_setting_reset(const char* id) {
    (void)id;
    return DKR2_UNAVAILABLE;
}
/* Development tools can edit another admitted API 2 package only when that
 * author declares live-settings2 and lists the option in dkr.json live_options.
 * The all-settings catalogue supplies canonical package/option indices. NULL
 * resets an override. API 1 and non-opted-in options return RELOAD. An optional
 * owner-only on_live_setting(option,bits,tag) callback refreshes cached state;
 * tag 0 reset / 1 float bits / 2 enum. All changes and callback state rewind. */
DKR_IMPORT int dkr2_setting_apply(unsigned package_index, unsigned option_index, const Dkr2Value* value) {
    (void)package_index;
    (void)option_index;
    (void)value;
    return DKR2_UNAVAILABLE;
}
/* Track catalogues: scene, segments, triangles/<segment index>, ai-nodes,
 * spawn-points and triggers. Read-only, bounded current-scene metadata under
 * track-tools2. Triangle texture IDs do not claim universal physics materials.
 * SET_RACE_LAPS uses arguments[0] = 1..3 (0 reset), only on the first lap of
 * an ordinary race and without another race-laps owner. Longer races belong
 * to the existing extended-lap/trophy adapter. RESTART_SCENE accepts an
 * existing nonzero entrance only if it has anchors for every current racer.
 * Other courses can only use their admitted default entrance before loading.
 * JSON record copies. Returns required bytes including NUL, END when index
 * is past the list, or a negative status. Names: courses, packages, resources.
 * Course IDs are logical stock:/legacy:/dkrmap: identities, never menu rows. */
DKR_IMPORT int dkr2_catalogue(const char* list, unsigned index, char* output, unsigned capacity) {
    (void)list;
    (void)index;
    (void)output;
    (void)capacity;
    return DKR2_UNAVAILABLE;
}
typedef struct Dkr2SymbolInfo {
    unsigned size, address, bytes, flags;
    char name[96];
} Dkr2SymbolInfo;
enum { DKR2_SYMBOL_FUNCTION = 1, DKR2_SYMBOL_CALL = 2, DKR2_SYMBOL_HOOK = 4, DKR2_SYMBOL_PROTECTED = 8 };
DKR_IMPORT unsigned dkr2_capability(const char* module) {
    (void)module;
    return 0;
}
DKR_IMPORT int dkr2_session(Dkr2Session* result) {
    (void)result;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_property(unsigned index, Dkr2PropertyInfo* result) {
    (void)index;
    (void)result;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_entities(unsigned first, Dkr2Entity* result, unsigned capacity) {
    (void)first;
    (void)result;
    (void)capacity;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_read(Dkr2Read* values, unsigned count) {
    (void)values;
    (void)count;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_edit(const Dkr2Edit* values, unsigned count) {
    (void)values;
    (void)count;
    return DKR2_UNAVAILABLE;
}
/* Replaces this owner's persistent property set (0..32). Validates all targets
 * and cross-package field ownership first. Reapplies on simulation frames;
 * original object shading fields also apply after the native shading update.
 * count=0 resets the set. Scene unload clears it. Movement still requires the
 * teleport/route adapters; readonly position fields cannot become overrides. */
DKR_IMPORT int dkr2_override(const Dkr2Edit* values, unsigned count) {
    (void)values;
    (void)count;
    return DKR2_UNAVAILABLE;
}
DKR_IMPORT int dkr2_symbols(unsigned index, Dkr2SymbolInfo* result) {
    (void)index;
    (void)result;
    return DKR2_UNAVAILABLE;
}
/* Read-only advanced inspection. Output is a copy; executable addresses and
 * the ordinary API 1 expert path retain their existing trust boundary. */
DKR_IMPORT int dkr2_memory_read(unsigned address, void* destination, unsigned bytes) {
    (void)address;
    (void)destination;
    (void)bytes;
    return DKR2_UNAVAILABLE;
}
#ifdef __cplusplus
}
static_assert(sizeof(Dkr2Handle) == 8, "O32 handle ABI");
static_assert(sizeof(Dkr2PropertyInfo) == 96, "property ABI");
static_assert(sizeof(Dkr2Read) == 20, "bulk query ABI");
static_assert(sizeof(Dkr2Edit) == 16, "transaction ABI");
static_assert(sizeof(Dkr2Entity) == 40, "entity ABI");
static_assert(sizeof(Dkr2Action) == 64, "command ABI");
static_assert(sizeof(Dkr2Cast) == 40, "cast ABI");
static_assert(sizeof(Dkr2Hit) == 48, "hit ABI");
static_assert(sizeof(Dkr2Event) == 32, "event ABI");
static_assert(sizeof(Dkr2Route) == 64, "route ABI");
static_assert(sizeof(Dkr2Debug) == 88, "debug primitive ABI");
static_assert(sizeof(Dkr2Tune) == 32, "tuning ABI");
#endif
#endif
