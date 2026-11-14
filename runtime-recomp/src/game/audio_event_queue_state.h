#ifndef DKR_AUDIO_EVENT_QUEUE_STATE_H
#define DKR_AUDIO_EVENT_QUEUE_STATE_H
#include <stdint.h>
/* Pointer-free, trivially copyable native checkpoint state. */
typedef struct dkr_audio_queue_guard {
    uint32_t queue, immediate_events, logged_empty, logged_corrupt;
    uint32_t items, item_count;
} dkr_audio_queue_guard;
enum {
    DKR_AUDIO_FRAME_US = 16667,
    DKR_AUDIO_IMMEDIATE_BUDGET = 8192,
    DKR_AUDIO_QUEUE_OK = 0,
    DKR_AUDIO_QUEUE_EMPTY = 1,
    DKR_AUDIO_QUEUE_INVALID = 2,
    DKR_AUDIO_QUEUE_REPAIRED = 4,
    DKR_AUDIO_QUEUE_YIELDED = 8
};
static inline int dkr_audio_guard_valid(const dkr_audio_queue_guard* g) {
    if (g->immediate_events >= DKR_AUDIO_IMMEDIATE_BUDGET || g->logged_empty > 1 || g->logged_corrupt > 15 ||
        (g->queue && ((g->queue & 3) || g->queue < 0x80000000U || g->queue > 0x807FFFECU)))
        return 0;
    if (!g->queue && (g->immediate_events || g->logged_empty || g->logged_corrupt || g->items || g->item_count))
        return 0;
    if (!g->item_count)
        return g->items == 0;
    return g->item_count >= 2 && g->item_count <= 512 && !(g->items & 3) && g->items >= 0x80000000U &&
           g->items <= 0x80800000U - g->item_count * 28 &&
           !(g->items < g->queue + 20 && g->queue < g->items + g->item_count * 28);
}
#endif
