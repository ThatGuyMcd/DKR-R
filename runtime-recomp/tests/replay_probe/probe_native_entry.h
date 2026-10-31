#pragma once
#include "recomp.h"
#include "probe_bridge.h"
#include "../../src/game/primary_heap.h"
#include <string.h>

// Common checkpointed dispatch prefix of the real native entry. Unknown
// operations continue in probe_native.c; this helper never admits them.
static inline int dkr_probe_native_entry(const char* name, uint8_t* ram, struct recomp_context* context, int* handled) {
    *handled = 1;
    dkr_probe_checkpoint();
    uint64_t mod_result = 0;
    if (dkr_probe_mod_dispatch(name, ram, context, 0, 0, 0, 0, &mod_result))
        return (int)mod_result;
    // A handled custom admission always keeps precedence over the stock pool.
    if (strcmp(name, "dkr_legacy_heap_capacity") == 0) {
        context->r15 = (int32_t)dkr_primary_heap_end((uint32_t)context->r15, 0x800000U);
        return 0;
    }
    *handled = 0;
    return 0;
}
