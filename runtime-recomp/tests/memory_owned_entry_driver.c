// Compile the production prefix as C, matching the owned native entry.
#include "replay_probe/probe_native_entry.h"
int dkr_memory_owned_entry_test(const char* name, uint8_t* ram, struct recomp_context* context, int* handled) {
    return dkr_probe_native_entry(name, ram, context, handled);
}
