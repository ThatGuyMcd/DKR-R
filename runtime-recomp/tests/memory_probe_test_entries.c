#include "probe_bridge.h"

/* C-only entries: an owned failure uses longjmp inside dkr_probe_run. No C++
   object or native ModWorld adapter may be crossed by that failure boundary. */
#define MEMORY_PROBE_ENTRIES(revision)                                                                                 \
    extern uint64_t dkr_experimental_v##revision##_dkr_probe_native_args(                                              \
        const char*, uint8_t*, struct recomp_context*, const uint64_t*, unsigned);                                     \
    void dkr_memory_probe_fallback##revision(uint8_t* ram, struct recomp_context* context) {                           \
        const uint64_t args[4] = {9, revision, 928, 0x800c0c74U};                                                      \
        for (unsigned n = 0; n < 100; ++n)                                                                             \
            dkr_experimental_v##revision##_dkr_probe_native_args("dkr_legacy_memory_service", ram, context, args, 4);  \
    }                                                                                                                  \
    void dkr_memory_probe_required##revision(uint8_t* ram, struct recomp_context* context) {                           \
        const uint64_t args[4] = {10, revision, 928, 0x800c0c74U};                                                     \
        dkr_experimental_v##revision##_dkr_probe_native_args("dkr_legacy_memory_service", ram, context, args, 4);      \
    }                                                                                                                  \
    void dkr_memory_probe_queue##revision(uint8_t* ram, struct recomp_context* context) {                              \
        const uint64_t args[4] = {4, revision, 0x80550000U, 0};                                                        \
        dkr_experimental_v##revision##_dkr_probe_native_args("dkr_legacy_memory_service", ram, context, args, 4);      \
    }

MEMORY_PROBE_ENTRIES(77)
#if DKR_OWNED_EXPECT_V80
MEMORY_PROBE_ENTRIES(80)
#endif
