#include "guest_call_context.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("guest call context regression failed");
}

// The access a recompiled function makes for an odd float register.
void odd_write(recomp_context& context, unsigned reg, std::uint32_t value) {
    context.f_odd[(reg - 1) * 2] = value;
}
} // namespace

int main() try {
    for (unsigned mode : {0U, 1U}) {
        recomp_context cpu{};
        cpu.mips3_float_mode = mode;
        cpu.f_odd = mode ? &cpu.f1.u32l : &cpu.f0.u32h;
        cpu.r4 = 42;
        cpu.r29 = 0x80110000U;
        cpu.status_reg = 0x12345678U;
        cpu.f18.d = -3.0;
        const recomp_context before = cpu;

        recomp_context call = dkr::runtime::guest_call_context(cpu);
        // The copy carries the registers and points at its own odd words.
        check(call.r4 == 42 && call.r29 == 0x80110000U && call.status_reg == 0x12345678U);
        check(call.f_odd == (mode ? &call.f1.u32l : &call.f0.u32h));
        check(call.f_odd != cpu.f_odd);
        // A callee writing an odd register changes only the copy.
        recomp_context probe = dkr::runtime::guest_call_context(cpu);
        odd_write(probe, 19, 0x3FE00000U);
        check(std::memcmp(&before, &cpu, sizeof(cpu)) == 0);
        if (mode == 0) {
            // Paired float mode: the odd register is the double's high word.
            // The callee sees its own, and the live double is not disturbed.
            call.f18.u32l = 0;
            odd_write(call, 19, 0x3FE00000U);
            check(call.f18.d == 0.5);
            check(cpu.f18.d == -3.0);
            // What a plain copy does instead: the write lands in the live CPU
            // and the copy keeps its stale high word.
            recomp_context shallow = cpu;
            shallow.f18.u32l = 0;
            odd_write(shallow, 19, 0x3FE00000U);
            check(shallow.f18.d == -3.0);
            check(cpu.f18.d == 0.5);
        }
    }
    // Repairing in place is idempotent and follows the float mode.
    recomp_context context{};
    dkr::runtime::repair_float_odd_pointer(context);
    check(context.f_odd == &context.f0.u32h);
    context.mips3_float_mode = 1;
    dkr::runtime::repair_float_odd_pointer(context);
    check(context.f_odd == &context.f1.u32l);
    std::cout << "guest call context checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
