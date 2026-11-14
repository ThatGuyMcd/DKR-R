#include "funcs.h"
#include "probe_audio.h"
#include "../../src/game/audio_event_queue.h"
#include <string.h>

// Same shared queue policy as runtime_stubs.cpp. No mutex/thread/logger. Guards belong
// to the private audio world and are restored with RAM; they MUST NOT be shared
// with a future live runtime. Malformed pointers remain fences, not recovery
// successes which conceal an unowned memory access.
static dkr_probe_audio_guard guards[8];
void dkr_probe_audio_guards_initialize(void) {
    memset(guards, 0, sizeof(guards));
}
void dkr_probe_audio_guards_reset(void) {
    for (unsigned i = 0; i < 8; ++i)
        guards[i].immediate_events = guards[i].logged_empty = guards[i].logged_corrupt = 0;
}
void dkr_probe_audio_guards_capture(dkr_probe_audio_guard out[8]) {
    memcpy(out, guards, sizeof(guards));
}
void dkr_probe_audio_guards_restore(const dkr_probe_audio_guard in[8]) {
    memcpy(guards, in, sizeof(guards));
}
static gpr address(uint32_t value) {
    return (gpr)(int32_t)value;
}
static uint32_t physical(uint32_t value) {
    return value & 0x1FFFFFFFU;
}
static void range(uint8_t* rdram, uint32_t value, unsigned bytes) {
    if ((value & 3) || value < 0x80000000U || value > 0x81000000U - bytes)
        dkr_probe_block("invalid-owned-audio-event-pointer");
    dkr_probe_memory(rdram, address(value), 4, 0);
    dkr_probe_memory(rdram, address(value) + bytes - 4, 4, 0);
}
static dkr_probe_audio_guard* guard_for(uint32_t queue) {
    for (unsigned i = 0; i < 8; ++i)
        if (physical(guards[i].queue) == physical(queue))
            return &guards[i];
    for (unsigned i = 0; i < 8; ++i)
        if (!guards[i].queue) {
            guards[i].queue = queue;
            return &guards[i];
        }
    unsigned quietest = 0;
    for (unsigned i = 1; i < 8; ++i)
        if (guards[i].immediate_events < guards[quietest].immediate_events)
            quietest = i;
    guards[quietest] = (dkr_probe_audio_guard){queue, 0, 0, 0, 0, 0};
    return &guards[quietest];
}
void dkr_probe_audio_queue_initialize(uint8_t* rdram, recomp_context* ctx) {
    if (!dkr_probe_audio_enabled())
        dkr_probe_block("unowned-audio-event-initialize");
    const uint32_t queue = (uint32_t)ctx->r4, items = (uint32_t)ctx->r5, count = (uint32_t)ctx->r6;
    if (count < 2 || count > 512 || !dkr_audio_range(queue, 20) || !dkr_audio_range(items, count * 28))
        dkr_probe_block("invalid-owned-audio-pool");
    range(rdram, queue, 20);
    range(rdram, items, count * 28);
    dkr_probe_audio_guard* guard = guard_for(queue);
    if (!dkr_audio_register_queue(guard, queue, items, count))
        dkr_probe_block("overlapping-owned-audio-pool");
}
static void resume_sequence(void* user, uint8_t* rdram, uint32_t player) {
    recomp_context next = *(recomp_context*)user;
    next.f_odd = next.mips3_float_mode ? &next.f1.u32l : &next.f0.u32h;
    next.r4 = address(player);
    __CSPPostNextSeqEvent(rdram, &next); /* isolated, checked native closure */
}
uint32_t dkr_probe_audio_event_next(uint8_t* rdram, recomp_context* ctx) {
    if (!dkr_probe_audio_enabled())
        dkr_probe_block("unowned-audio-event-queue");
    const uint32_t queue = (uint32_t)ctx->r4, destination = (uint32_t)ctx->r5;
    range(rdram, queue, 0x14);
    range(rdram, destination, 0x10);
    dkr_probe_audio_guard value = {queue, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 8; ++i)
        if (guards[i].queue == queue) {
            value = guards[i];
            break;
        }
    unsigned status = 0;
    const uint32_t globals = DKR_PROBE_REVISION == 80 ? 0x800DCBA0U : 0x800DC630U;
    const uint32_t delay = dkr_audio_queue_next(rdram, &value, destination, globals, resume_sequence, ctx, &status);
    if (status == DKR_AUDIO_QUEUE_INVALID)
        dkr_probe_block("malformed-owned-audio-event-list");
    if (status == DKR_AUDIO_QUEUE_EMPTY)
        dkr_audio_empty_event(rdram, destination);
    value.logged_corrupt |= status;
    *guard_for(queue) = value;
    return delay;
}
static int retail_pointer(uint32_t value) {
    return value >= 0x80000000U && value < 0x80800000U;
}
int dkr_probe_audio_voice_guard(uint8_t* rdram, recomp_context* ctx) {
    if (!dkr_probe_audio_enabled())
        dkr_probe_block("unowned-audio-voice-guard");
    const uint32_t synth = (uint32_t)ctx->r4, voice = (uint32_t)ctx->r5;
    if (!retail_pointer(synth) || !retail_pointer(voice) || (uint16_t)ctx->r6 > 1)
        return 1;
    // Same three lists, 128-node bound and normalization as runtime_stubs.cpp.
    // Checked memory adds a fence for alignment/range errors instead of UB.
    for (unsigned list = 4; list <= 20; list += 8) {
        uint32_t node = (uint32_t)MEM_W(list, address(synth));
        for (unsigned count = 0; node && count < 128; ++count) {
            if (!retail_pointer(node))
                break;
            if (node == voice) {
                if (MEM_BU(0xDC, address(voice)) > 1)
                    MEM_B(0xDC, address(voice)) = 0;
                return 0;
            }
            node = (uint32_t)MEM_W(0, address(node));
        }
    }
    return 1;
}
int dkr_probe_audio_bus_guard(uint8_t* rdram, recomp_context* ctx) {
    if (!dkr_probe_audio_enabled())
        dkr_probe_block("unowned-audio-bus-guard");
    const uint32_t bus = (uint32_t)ctx->r4;
    if (!retail_pointer(bus) || (uint32_t)MEM_W(0x14, address(bus)) > 64 ||
        !retail_pointer((uint32_t)MEM_W(0x1C, address(bus)))) {
        ctx->r2 = 0;
        return 1;
    }
    return 0;
}
