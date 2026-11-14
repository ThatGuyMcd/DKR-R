#ifndef DKR_GAME_SDK_H
#define DKR_GAME_SDK_H
#ifdef __cplusplus
extern "C" {
#endif
#include "magic_codes.h"

/* Values are copied, not borrowed host pointers. Guest object/racer/camera
 * addresses are valid only in their current scene: reacquire after a load. */
typedef struct DkrRacerState {
    DkrU32 object, racer;
    DkrS32 player;
    DkrU32 index, character, vehicle, lap, count_lap, position, finished, bananas;
    float x, y, z, speed;
    DkrU32 reserved;
} DkrRacerState;
typedef struct DkrObjectState {
    DkrU32 object, behaviour, id;
    float x, y, z, scale;
    DkrU32 opacity, collision;
} DkrObjectState;
typedef struct DkrInput {
    DkrU32 held, pressed, released;
    DkrS32 stick_x, stick_y;
} DkrInput;
typedef struct DkrCameraState {
    DkrU32 camera;
    float x, y, z;
    DkrS32 yaw, pitch, roll;
    float boom, shake;
    DkrU32 mode;
} DkrCameraState;
typedef struct DkrFloorQuery {
    DkrS32 segment;
    float x, ceiling_y, z;
} DkrFloorQuery;
typedef struct DkrFloorHit {
    float y;
    DkrU32 segment, triangle;
    float normal_x, normal_y, normal_z;
} DkrFloorHit;
enum {
    DKR_FIELD_X = 0,
    DKR_FIELD_Y = 1,
    DKR_FIELD_Z = 2,
    DKR_RACER_SPEED = 3,
    DKR_RACER_BANANAS = 4,
    DKR_OBJECT_SCALE = 3,
    DKR_CAMERA_BOOM = 3,
    DKR_CAMERA_SHAKE = 4
};
/* modules racers/objects/input/cameras v1. Indices enumerate the CURRENT
 * native list, not a stable object identity. All state is ordinary guest RAM
 * and participates in online rewind. Non-finite writes are rejected. */
DKR_IMPORT DkrU32 dkr_racer_count(void) {
    return 0;
}
DKR_IMPORT DkrU32 dkr_racer_get(DkrU32 index, DkrRacerState* out) {
    (void)index;
    (void)out;
    return 0;
}
DKR_IMPORT DkrU32 dkr_racer_set(DkrU32 index, DkrU32 field, DkrU32 bits) {
    (void)index;
    (void)field;
    (void)bits;
    return 0;
}
DKR_IMPORT DkrU32 dkr_object_count(void) {
    return 0;
}
DKR_IMPORT DkrU32 dkr_object_get(DkrU32 index, DkrObjectState* out) {
    (void)index;
    (void)out;
    return 0;
}
DKR_IMPORT DkrU32 dkr_object_set(DkrU32 index, DkrU32 field, DkrU32 bits) {
    (void)index;
    (void)field;
    (void)bits;
    return 0;
}
DKR_IMPORT DkrU32 dkr_input_get(DkrU32 player, DkrInput* out) {
    (void)player;
    (void)out;
    return 0;
}
DKR_IMPORT DkrU32 dkr_camera_get(DkrU32 index, DkrCameraState* out) {
    (void)index;
    (void)out;
    return 0;
}
DKR_IMPORT DkrU32 dkr_camera_set(DkrU32 index, DkrU32 field, DkrU32 bits) {
    (void)index;
    (void)field;
    (void)bits;
    return 0;
}
/* collision v1: closest static collision floor below ceiling_y in ONE native
 * segment. Does not invent walls/physics or include moving object collisions.
 * Returns 1 hit, 0 none, 0xffffffff if the bounded query budget is exceeded.
 * The original native multi-height function has no output capacity parameter;
 * this service never lends it an undersized output array. */
DKR_IMPORT DkrU32 dkr_collision_floor(const DkrFloorQuery* query, DkrFloorHit* out) {
    (void)query;
    (void)out;
    return 0;
}
static inline DkrU32 dkr_float_bits(float value) {
    union {
        float f;
        DkrU32 bits;
    } v;
    v.f = value;
    return v.bits;
}
enum { DKR_AUDIO_SOUND = 0, DKR_AUDIO_MUSIC = 1, DKR_AUDIO_STOP_MUSIC = 2, DKR_AUDIO_FADE_MUSIC = 3 };
/* audio v1: routed into the owned guest audio engine. Online PCM/output is
 * still released only through the existing confirmed-frame audio journal.
 * Returns 0 before audio initialization or for an unavailable sound/sequence.
 * Fade value is signed (-4096..4096); sound handles are not lent to mods. */
DKR_IMPORT DkrU32 dkr_audio_command(DkrU32 command, DkrS32 value) {
    (void)command;
    (void)value;
    return 0;
}
typedef struct DkrHudText {
    DkrU32 target, font;
    DkrS32 x, y;
    DkrU32 rgba, alignment;
    const char* text;
} DkrHudText;
enum { DKR_TEXT_MENU = 0, DKR_TEXT_HUD = 1 };
/* hud v1: only in a before_/after_ draw callback, with an already loaded
 * font. target selects the current menu/HUD display-list owner; never pass a
 * fabricated display-list pointer. ASCII text, max 160 bytes; max 512 glyphs
 * per package/frame. rgba is RRGGBBAA. Returns 0 if the font/list is not ready.
 * Native font colour/position/background state is restored after drawing. */
DKR_IMPORT DkrU32 dkr_hud_text(const DkrHudText* text) {
    (void)text;
    return 0;
}
_Static_assert(sizeof(DkrRacerState) == 64, "Racer SDK ABI");
_Static_assert(sizeof(DkrObjectState) == 36, "Object SDK ABI");
_Static_assert(sizeof(DkrInput) == 20, "Input SDK ABI");
_Static_assert(sizeof(DkrCameraState) == 40, "Camera SDK ABI");
_Static_assert(sizeof(DkrFloorQuery) == 16 && sizeof(DkrFloorHit) == 24, "Collision SDK ABI");
_Static_assert(sizeof(DkrHudText) == 28, "HUD SDK ABI");
#ifdef __cplusplus
}
#endif
#endif
