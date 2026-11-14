#ifndef DKR_MAGIC_CODE_DISPATCH_H
#define DKR_MAGIC_CODE_DISPATCH_H
#include <stddef.h>
#include <stdint.h>
struct recomp_context;
#ifdef __cplusplus
extern "C" {
#if defined(__APPLE__)
// Darwin clang gives C++ thread_local storage internal linkage and exports only
// a TLS wrapper, so the C payloads could not link against it. __thread keeps
// the plain TLV symbol the C side expects.
#define DKR_CODE_TLS __thread
#else
#define DKR_CODE_TLS thread_local
#endif
#elif defined(_MSC_VER)
#define DKR_CODE_TLS __declspec(thread)
#else
#define DKR_CODE_TLS _Thread_local
#endif
// Bound by the owner of THIS CPU thread; never a global active-payload lookup.
extern DKR_CODE_TLS int dkr_code_callbacks_enabled;
extern DKR_CODE_TLS const uint8_t* dkr_code_hook_mask;
static inline int dkr_code_hook_active(uint32_t address) {
    return dkr_code_callbacks_enabled && dkr_code_hook_mask &&
           (dkr_code_hook_mask[(address & 0x7fffffU) >> 5] & (1U << ((address >> 2) & 7U)));
}
typedef void (*dkr_code_guest_function)(uint8_t*, struct recomp_context*);
typedef struct dkr_code_dispatch_entry {
    uint32_t address;
    dkr_code_guest_function function;
    int protected_function;
    int protected_hook;
    unsigned before_event, after_event;
    const char* name;
    int protected_call;
    int curated_call;
} dkr_code_dispatch_entry;
enum {
    DKR_CODE_EVENT_NONE = 0,
    DKR_CODE_EVENT_FRAME = 1,
    DKR_CODE_EVENT_FRAME_END = 2,
    DKR_CODE_EVENT_SCENE_UNLOAD = 3,
    DKR_CODE_EVENT_SCENE_LOADED = 4,
    DKR_CODE_EVENT_RACER_SPAWNED = 5,
    DKR_CODE_EVENT_RESULTS = 6,
    DKR_CODE_EVENT_MENU = 7
};
int dkr_code_hook_enter(uint32_t, uint8_t*, struct recomp_context*);
int dkr_code_hook_leave(uint32_t, uint8_t*, struct recomp_context*);
void dkr_code_event(unsigned, uint8_t*, struct recomp_context*);
int dkr_code_call_indirect(uint32_t, uint8_t*, struct recomp_context*);
uint32_t dkr_code_lap_address(uint32_t, uint8_t*, struct recomp_context*);
const dkr_code_dispatch_entry* dkr_v77_code_dispatch_table(size_t*);
const dkr_code_dispatch_entry* dkr_v80_code_dispatch_table(size_t*);
const dkr_code_dispatch_entry* dkr_experimental_v77_code_dispatch_table(size_t*);
const dkr_code_dispatch_entry* dkr_experimental_v80_code_dispatch_table(size_t*);
#ifdef __cplusplus
}
#endif
#undef DKR_CODE_TLS
#endif
