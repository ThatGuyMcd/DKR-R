#ifndef DKR_AUDIO_EVENT_QUEUE_H
#define DKR_AUDIO_EVENT_QUEUE_H

#include "audio_event_queue_state.h"

/* Shared authored ABI for normal and owned audio. Include after recomp.h.
 * Event-pool ownership is registered by the alEvtqNew Patch Pipeline hook;
 * every field below belongs to the owned world's checkpoint, not a device. */
/* The sequence validator permits 4096 commands at a tick. Leave room for
 * envelope/note/player events too. The immediate work budget is NOT a
 * corruption test: the next event and all pending nodes survive a yield. */

typedef void (*dkr_audio_sequence_resume)(void*, uint8_t*, uint32_t);

static inline gpr dkr_audio_address(uint32_t value) {
    return (gpr)(int32_t)value;
}
static inline int dkr_audio_range(uint32_t value, uint32_t bytes) {
    /* Audio pools are retail KSEG0 allocations, never extended mod assets. */
    return !(value & 3U) && bytes && bytes <= 0x800000U && value >= 0x80000000U && value <= 0x80800000U - bytes;
}
static inline int dkr_audio_pool_item(const dkr_audio_queue_guard* guard, uint32_t item) {
    return dkr_audio_range(item, 28) &&
           (!guard->item_count || (item >= guard->items && (item - guard->items) / 28 < guard->item_count &&
                                   (item - guard->items) % 28 == 0));
}
static inline int dkr_audio_register_queue(dkr_audio_queue_guard* guard, uint32_t queue, uint32_t items,
                                           uint32_t count) {
    if (!dkr_audio_range(queue, 20) || count < 2 || count > 512 || !dkr_audio_range(items, count * 28) ||
        (items < queue + 20 && queue < items + count * 28))
        return 0;
    guard->queue = queue;
    guard->items = items;
    guard->item_count = count;
    guard->immediate_events = guard->logged_empty = guard->logged_corrupt = 0;
    return 1;
}
static inline void dkr_audio_empty_event(uint8_t* rdram, uint32_t destination) {
    for (unsigned b = 0; b < 16; b += 4)
        MEM_W(b, dkr_audio_address(destination)) = 0;
    MEM_H(0, dkr_audio_address(destination)) = -1;
}

/* Returns only an explicitly rooted native music/jingle owner, never an
 * inferred container behind an arbitrary queue or caller's scratch buffer. */
static inline uint32_t dkr_audio_sequence_owner(uint8_t* rdram, uint32_t globals, uint32_t queue,
                                                uint32_t destination) {
    if (!dkr_audio_range(globals, 8))
        return 0;
    for (unsigned b = 0; b < 8; b += 4) {
        const uint32_t player = (uint32_t)MEM_W(b, dkr_audio_address(globals));
        if (dkr_audio_range(player, 128) && queue == player + 0x48 && destination == player + 0x38 &&
            (uint32_t)MEM_W(0x2C, dkr_audio_address(player)) <= 2)
            return player;
    }
    return 0;
}

/* The owned world can begin from a warm native checkpoint, after alEvtqNew
 * already ran on the normal runtime. Adopt only a COMPLETE, disjoint native
 * free/allocated partition with the exact retail capacity and contiguous
 * 28-byte allocation. No guessed address or partial corrupt list is admitted. */
static inline void dkr_audio_adopt_sequence_pool(uint8_t* rdram, dkr_audio_queue_guard* guard, uint32_t destination,
                                                 uint32_t globals) {
    if (guard->item_count)
        return;
    const uint32_t player = dkr_audio_sequence_owner(rdram, globals, guard->queue, destination);
    if (!player)
        return;
    // ALLink sentinels have prev == NULL. They are not circular lists and
    // their prev fields never store a tail, even when events are pending.
    if ((uint32_t)MEM_W(4, dkr_audio_address(guard->queue)) || (uint32_t)MEM_W(12, dkr_audio_address(guard->queue)))
        return;
    const unsigned expected = (uint32_t)MEM_W(0, dkr_audio_address(globals)) == player ? 120U : 50U;
    uint32_t items[120], minimum = 0xFFFFFFFFU;
    unsigned count = 0;
    for (unsigned offset = 0; offset <= 8; offset += 8) {
        uint32_t previous = guard->queue + offset;
        uint32_t item = (uint32_t)MEM_W(offset, dkr_audio_address(guard->queue));
        while (item) {
            if (count >= expected || !dkr_audio_range(item, 28) ||
                (uint32_t)MEM_W(4, dkr_audio_address(item)) != previous)
                return;
            items[count++] = item;
            if (item < minimum)
                minimum = item;
            previous = item;
            item = (uint32_t)MEM_W(0, dkr_audio_address(item));
        }
    }
    if (count != expected || !dkr_audio_range(minimum, count * 28) ||
        (minimum < player + 128 && player < minimum + count * 28))
        return;
    uint8_t seen[120] = {0};
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t at = items[i] - minimum;
        if (at % 28 || at / 28 >= count || seen[at / 28])
            return;
        seen[at / 28] = 1;
    }
    dkr_audio_register_queue(guard, guard->queue, minimum, count);
}

static inline void dkr_audio_rebuild_pool(uint8_t* rdram, const dkr_audio_queue_guard* guard) {
    const gpr q = dkr_audio_address(guard->queue);
    MEM_W(0, q) = 0;
    MEM_W(4, q) = 0;
    MEM_W(8, q) = 0;
    MEM_W(12, q) = 0;
    /* Match alEvtqNew's reverse insertion order. No untrusted list traversal
     * and no lost nodes, even if the old links formed a cycle. */
    for (uint32_t i = 0; i < guard->item_count; ++i) {
        const uint32_t item = guard->items + i * 28;
        const uint32_t old = (uint32_t)MEM_W(0, q);
        MEM_W(0, dkr_audio_address(item)) = old;
        MEM_W(4, dkr_audio_address(item)) = guard->queue;
        if (old)
            MEM_W(4, dkr_audio_address(old)) = item;
        MEM_W(0, q) = item;
    }
}

/* An empty queue is the only case where no existing events are discarded.
 * A genuinely malformed queue may be rebuilt ONLY from its registered pool
 * and known sequence owner. Stop/release its voices through the native
 * handler; don't restart a corrupt stream or resurrect stale voice events. */
static inline int dkr_audio_recover_sequence(uint8_t* rdram, dkr_audio_queue_guard* guard, uint32_t destination,
                                             uint32_t globals, int malformed, dkr_audio_sequence_resume resume,
                                             void* user) {
    const uint32_t player = dkr_audio_sequence_owner(rdram, globals, guard->queue, destination);
    if (!player || guard->item_count < 2 || guard->item_count > 512 ||
        !dkr_audio_range(guard->items, guard->item_count * 28) ||
        (guard->items < player + 128 && player < guard->items + guard->item_count * 28))
        return 0;
    const gpr p = dkr_audio_address(player), q = dkr_audio_address(guard->queue);
    const uint32_t target = (uint32_t)MEM_W(0x18, p);
    const uint32_t state = (uint32_t)MEM_W(0x2C, p);
    if (!malformed && state == 1 && target && !dkr_audio_range(target, 0x158))
        return 0;
    uint32_t frame = (uint32_t)MEM_W(0x5C, p);
    if (!frame || frame > 1000000U)
        frame = DKR_AUDIO_FRAME_US;
    dkr_audio_rebuild_pool(rdram, guard);
    const int stop = state == 2 || (malformed && state == 1);
    if (stop)
        MEM_W(0x2C, p) = 2;
    /* Allocate an API heartbeat, plus an immediate native STOP when needed.
     * These pointer-free events never refer to a discarded voice allocation. */
    uint32_t tail = 0;
    for (unsigned i = 0; i < (stop ? 2U : 1U); ++i) {
        const uint32_t item = (uint32_t)MEM_W(0, q);
        const uint32_t free_next = (uint32_t)MEM_W(0, dkr_audio_address(item));
        MEM_W(0, q) = free_next;
        if (free_next)
            MEM_W(4, dkr_audio_address(free_next)) = guard->queue;
        MEM_W(0, dkr_audio_address(item)) = 0;
        MEM_W(4, dkr_audio_address(item)) = tail ? tail : guard->queue + 8;
        MEM_W(8, dkr_audio_address(item)) = stop && i == 0 ? 0 : frame;
        for (unsigned b = 12; b < 28; b += 4)
            MEM_W(b, dkr_audio_address(item)) = 0;
        MEM_H(12, dkr_audio_address(item)) = stop && i == 0 ? 16 : 9;
        if (tail)
            MEM_W(0, dkr_audio_address(tail)) = item;
        else
            MEM_W(8, q) = item;
        tail = item;
    }
    guard->immediate_events = 0;
    if (!malformed && state == 1 && target && resume)
        resume(user, rdram, player);
    return 1;
}

static inline int dkr_audio_head_valid(uint8_t* rdram, const dkr_audio_queue_guard* guard) {
    const gpr q = dkr_audio_address(guard->queue);
    const uint32_t item = (uint32_t)MEM_W(8, q);
    if ((uint32_t)MEM_W(4, q) || (uint32_t)MEM_W(12, q))
        return 0;
    if (!item)
        return 1;
    if (!dkr_audio_pool_item(guard, item))
        return 0;
    const gpr p = dkr_audio_address(item);
    const uint32_t next = (uint32_t)MEM_W(0, p), free_head = (uint32_t)MEM_W(0, q);
    if ((uint32_t)MEM_W(4, p) != guard->queue + 8 || next == item || free_head == item ||
        (next && (!dkr_audio_pool_item(guard, next) || next == free_head ||
                  (uint32_t)MEM_W(4, dkr_audio_address(next)) != item)) ||
        (free_head &&
         (!dkr_audio_pool_item(guard, free_head) || (uint32_t)MEM_W(4, dkr_audio_address(free_head)) != guard->queue)))
        return 0;
    return 1;
}

static inline uint32_t dkr_audio_queue_next(uint8_t* rdram, dkr_audio_queue_guard* guard, uint32_t destination,
                                            uint32_t globals, dkr_audio_sequence_resume resume, void* user,
                                            unsigned* status) {
    *status = DKR_AUDIO_QUEUE_OK;
    if (!dkr_audio_range(guard->queue, 20) || !dkr_audio_range(destination, 16) ||
        (destination < guard->queue + 20 && guard->queue < destination + 16) ||
        (guard->item_count && destination < guard->items + guard->item_count * 28 && guard->items < destination + 16)) {
        *status = DKR_AUDIO_QUEUE_INVALID;
        return DKR_AUDIO_FRAME_US;
    }
    const gpr q = dkr_audio_address(guard->queue);
    dkr_audio_adopt_sequence_pool(rdram, guard, destination, globals);
    const int malformed = !dkr_audio_head_valid(rdram, guard);
    if (malformed || !(uint32_t)MEM_W(8, q)) {
        if (!dkr_audio_recover_sequence(rdram, guard, destination, globals, malformed, resume, user)) {
            /* Caller fences/logs invalid memory. No list or free-pool writes. */
            *status = malformed ? DKR_AUDIO_QUEUE_INVALID : DKR_AUDIO_QUEUE_EMPTY;
            if (!malformed)
                guard->immediate_events = 0;
            return DKR_AUDIO_FRAME_US;
        }
        *status = DKR_AUDIO_QUEUE_REPAIRED;
        if (!dkr_audio_head_valid(rdram, guard)) {
            *status = DKR_AUDIO_QUEUE_INVALID;
            return DKR_AUDIO_FRAME_US;
        }
    }
    const uint32_t item = (uint32_t)MEM_W(8, q);
    const gpr p = dkr_audio_address(item);
    const uint32_t next = (uint32_t)MEM_W(0, p), free_head = (uint32_t)MEM_W(0, q);
    const uint32_t delta = (uint32_t)MEM_W(8, p);
    MEM_W(8, q) = next;
    if (next)
        MEM_W(4, dkr_audio_address(next)) = guard->queue + 8;
    for (unsigned b = 0; b < 16; b += 4)
        MEM_W(b, dkr_audio_address(destination)) = MEM_W(12 + b, p);
    MEM_W(0, p) = free_head;
    MEM_W(4, p) = guard->queue;
    if (free_head)
        MEM_W(4, dkr_audio_address(free_head)) = item;
    MEM_W(0, q) = item;
    if (delta) {
        guard->immediate_events = 0;
        guard->logged_empty = 0;
        return delta;
    }
    if (++guard->immediate_events < DKR_AUDIO_IMMEDIATE_BUDGET)
        return 0;
    /* Yield with the actual event still in nextEvent; never replace it with
     * -1, lose note-off/STOP/API events, or detach the allocated list. */
    guard->immediate_events = 0;
    *status |= DKR_AUDIO_QUEUE_YIELDED;
    return DKR_AUDIO_FRAME_US;
}
#endif
