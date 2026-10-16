#include <dkr/magic_codes.h>

/* A small developer example for the implemented portable-core boundary.
 * This is not the Custom Trophy Races example or a ready-to-install release. */
static DkrU32 boot_random;

DKR_CALLBACK(on_boot) void core_boot(void) {
    boot_random = dkr_random_u32();
    dkr_log("Custom Magic Code core initialized.");
}

DKR_EXPORT DkrU32 core_boot_random(void) {
    return boot_random;
}
