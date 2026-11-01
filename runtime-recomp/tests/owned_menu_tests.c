// Compile the real native dispatch body with synthetic owner/context inputs.
#include "recomp.h"
#include "replay_probe/probe_bridge.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#ifdef DKR_MENU_OLD_CONTROL
static int dkr_menu_notification(unsigned event) {
    return event <= 17;
}
#else
#include "../src/game/netplay/owned_menu_policy.h"
#endif
static int offline = 1, canonical_presentation = 1, owner = 0;
static struct {
    unsigned scene_unload_phase;
} state;
static jmp_buf rejected;
void dkr_probe_checkpoint(void) {}
int dkr_probe_mod_dispatch(const char* name, uint8_t* ram, struct recomp_context* context, const uint64_t* args,
                           unsigned count, const uint32_t* fields, unsigned event, uint64_t* result) {
    (void)name;
    (void)ram;
    (void)context;
    (void)args;
    (void)count;
    (void)fields;
    (void)event;
    if (owner) {
        *result = 73;
        return 1;
    }
    return 0;
}
DKR_PROBE_NORETURN void dkr_probe_block(const char* name) {
    (void)name;
    longjmp(rejected, 1);
}
#include "replay_probe/probe_native_fields.h"
static int call(unsigned event) {
    if (setjmp(rejected))
        return -1;
    return dkr_probe_native_fields("dkr_legacy_track_menu", 0, 0, event, 0);
}
int main(void) {
    unsigned checks = 0;
    for (unsigned event = 0; event < 256; ++event) {
        const int expected = (event <= 23 || (event >= 100 && event <= 105)) ? 0 : -1;
        if (call(event) != expected)
            return 1;
        ++checks;
        owner = 1;
        if (call(event) != 73)
            return 2;
        ++checks;
        owner = 0;
    }
    offline = 0;
    if (call(100) != -1)
        return 3;
    ++checks;
    offline = 1;
    canonical_presentation = 0;
    if (call(18) != -1)
        return 4;
    ++checks;
    printf("%u actual menu-dispatch checks passed, v%d\n", checks, DKR_PROBE_REVISION);
    return 0;
}
