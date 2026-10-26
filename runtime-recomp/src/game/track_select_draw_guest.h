#ifndef DKR_TRACK_SELECT_DRAW_GUEST_H
#define DKR_TRACK_SELECT_DRAW_GUEST_H
/* Draw-only Patch Pipeline ABI, shared by retail and isolated replay.
   No configuration, window, heap allocation or persistent simulation state.
   Signed packets replace three existing commands; display-list usage is unchanged. */
#include <stdint.h>
typedef struct dkr_track_draw_state {
    unsigned active, pending;
    int32_t rectangle[8];
} dkr_track_draw_state;
static inline uint32_t dkr_track_pair(int32_t a, int32_t b) {
    return ((uint32_t)(uint16_t)a << 16) | (uint16_t)b;
}
static inline int dkr_track_preview_bounds(uint8_t* rdram, recomp_context* ctx, uint32_t menu, int32_t* bounds) {
    if (menu < 0x80000000U || menu > 0x807FFFFCU || (menu & 3) || MEM_W(0, (gpr)(int32_t)menu) != 15)
        return 0;
    const uint32_t viewport = (uint32_t)ctx->r3;
    const uint32_t packet = (uint32_t)ctx->r2;
    if (viewport < 0x80000000U || viewport > 0x807FFFD0U || (viewport & 3) || packet < 0x80000000U ||
        packet > 0x807FFFF8U || (packet & 7))
        return 0;
    for (unsigned i = 0; i < 4; ++i) {
        bounds[i] = MEM_W(i * 4, (gpr)(int32_t)viewport);
        if (bounds[i] < -4096 || bounds[i] > 4096)
            return 0;
    }
    if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
        return 0;
    // The terminal full-screen state retains its existing cover policy.
    return bounds[0] != 0 || bounds[1] != 0 || bounds[2] != 320 || bounds[3] != 240;
}
static inline unsigned dkr_track_draw_guest(dkr_track_draw_state* state, uint8_t* rdram, recomp_context* ctx,
                                            unsigned event) {
    if (event == 0 || event == 1) {
        state->active = event == 0;
        state->pending = 0;
        return 0;
    }
    if (event == 2)
        return state->active;
    if (event == 10) {
        const uint32_t base = (uint32_t)ctx->r29 + 0x90U;
        if (base < 0x80000000U || base > 0x807FFF10U || (base & 7))
            return 0;
        for (unsigned i = 0; i < 240; i += 4)
            MEM_W(i, (gpr)(int32_t)base) = 0;
        return 0;
    }
    if (!state->active)
        return 0;
    int32_t* r = state->rectangle;
    if (event == 3) { // Scaled preview tile, before native left/top UV clipping.
        r[0] = (int32_t)ctx->r11;
        r[1] = (int32_t)ctx->r10;
        r[2] = (int32_t)ctx->r18;
        r[3] = (int32_t)ctx->r19;
        r[4] = (int32_t)ctx->r13;
        r[5] = (int32_t)ctx->r8;
        r[6] = (int32_t)ctx->r12;
        r[7] = (int32_t)ctx->r7;
    } else if (event == 5) { // Unscaled navigation arrow.
        r[0] = (int32_t)ctx->r6;
        r[1] = (int32_t)ctx->r7;
        r[2] = (int32_t)ctx->r11;
        r[3] = (int32_t)ctx->r12;
        r[4] = r[5] = 0;
        r[6] = r[7] = 1024;
    } else if (event == 7) { // Glyph: t5 is assigned in the upcoming delay slot.
        r[0] = (int32_t)ctx->r5;
        r[1] = (int32_t)ctx->r6;
        r[2] = (int32_t)ctx->r10;
        r[3] = (int32_t)ctx->r9;
        r[4] = (int32_t)ctx->r12;
        r[5] = (int32_t)ctx->r14;
        r[6] = r[7] = 1024;
    } else if (event == 4 || event == 6 || event == 8) {
        if (state->pending != event - 1)
            return 0;
        state->pending = 0;
        const gpr slot = event == 8 ? ctx->r17 : ctx->r16;
        const uint32_t end = (uint32_t)MEM_W(0, slot);
        if (end < 0x80000018U || end > 0x80800000U || (end & 7))
            return 0;
        for (unsigned i = 0; i < 8; ++i)
            if (r[i] < -32768 || r[i] > 32767)
                return 0;
        if (r[0] >= r[2] || r[1] >= r[3])
            return 0;
        const gpr packet = (gpr)(int32_t)(end - 24U);
        if (((uint32_t)MEM_W(0, packet) >> 24) != 0xE4U)
            return 0;
        /* Project bridge's scoped signed-rectangle ABI, full 10.2 bounds.
           Keep the original s/t and derivative signs, including flip-X. */
        MEM_W(0, packet) = 0xD5000001U;
        MEM_W(4, packet) = 0x54524354U; // TRCT, not a permissive RT64 extension stream.
        MEM_W(8, packet) = dkr_track_pair(r[0], r[1]);
        MEM_W(12, packet) = dkr_track_pair(r[2], r[3]);
        MEM_W(16, packet) = dkr_track_pair(r[4], r[5]);
        MEM_W(20, packet) = dkr_track_pair(r[6], r[7]);
        return 0;
    } else
        return 0;
    state->pending = event;
    return 0;
}
#endif
