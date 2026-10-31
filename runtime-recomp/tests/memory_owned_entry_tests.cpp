// Exercise the actual native dispatch prefix; no guest tick or ROM is run.
#include "recomp.h"
extern "C" int dkr_memory_owned_entry_test(const char*, uint8_t*, struct recomp_context*, int*);
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
bool delegated = false;
unsigned checks = 0, checkpoints = 0, dispatches = 0;
void check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Owned heap entry assertion " + std::to_string(checks));
}
} // namespace
extern "C" void dkr_probe_checkpoint() {
    ++checkpoints;
}
extern "C" int dkr_probe_mod_dispatch(const char*, uint8_t*, struct recomp_context* context, const uint64_t*, unsigned,
                                      const uint32_t*, unsigned, uint64_t* result) {
    ++dispatches;
    if (!delegated)
        return 0;
    context->r15 = (int32_t)0x80800000U;
    *result = 17;
    return 1;
}
int main() try {
    uint8_t sentinel = 0;
    recomp_context context{};
    int handled = 0;
    for (uint32_t end : {0x80400000U, 0x80400010U, 0x807e0000U, 0x80800000U}) {
        context.r15 = (int32_t)end;
        check(dkr_memory_owned_entry_test("dkr_legacy_heap_capacity", &sentinel, &context, &handled) == 0 && handled);
        check((uint32_t)context.r15 == (end == 0x80400000U ? 0x807e0000U : end));
    }
    context.r15 = (int32_t)0x80400000U;
    check(dkr_memory_owned_entry_test("unhandled-operation", &sentinel, &context, &handled) == 0 && !handled);
    check((uint32_t)context.r15 == 0x80400000U);
    delegated = true;
    check(dkr_memory_owned_entry_test("dkr_legacy_heap_capacity", &sentinel, &context, &handled) == 17 && handled);
    check((uint32_t)context.r15 == 0x80800000U);
    check(checkpoints == 6 && dispatches == 6 && sentinel == 0);
    std::cout << checks << " owned heap entry checks passed, v" << DKR_MEMORY_TEST_REVISION
              << "; synthetic state, delegated admission and unhandled operations covered.\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
