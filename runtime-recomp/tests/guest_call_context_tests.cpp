#include "guest_call_context.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

// The helper must not be copyable or movable: a copy or a by-value return would
// leave f_odd pointing into another object, and a compiler is not required to
// elide a named local (MSVC does not).
static_assert(!std::is_copy_constructible_v<dkr::runtime::GuestCallContext>);
static_assert(!std::is_move_constructible_v<dkr::runtime::GuestCallContext>);
static_assert(!std::is_copy_assignable_v<dkr::runtime::GuestCallContext>);
static_assert(!std::is_move_assignable_v<dkr::runtime::GuestCallContext>);

namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("guest call context regression failed");
}

// The access a recompiled function makes for an odd float register.
void odd_write(recomp_context& context, unsigned reg, std::uint32_t value) {
    context.f_odd[(reg - 1) * 2] = value;
}

// In place: f_odd points inside the struct, so it is never returned by value.
void init_live_cpu(recomp_context& cpu, unsigned mode) {
    cpu = recomp_context{};
    cpu.mips3_float_mode = mode;
    cpu.f_odd = mode ? &cpu.f1.u32l : &cpu.f0.u32h;
    cpu.r4 = 42;
    cpu.r29 = 0x80110000U;
    cpu.status_reg = 0x12345678U;
    cpu.f18.d = -3.0;
}

bool points_into_itself(const dkr::runtime::GuestCallContext& call) {
    const auto& c = call.context;
    return c.f_odd == (c.mips3_float_mode ? &c.f1.u32l : &c.f0.u32h);
}

// A construction path that crosses a function boundary, as call sites do.
void call_through(const recomp_context& live, unsigned mode) {
    dkr::runtime::GuestCallContext call(live);
    check(points_into_itself(call));
    check(call.get() == &call.context && call.operator->() == &call.context);
    check(call->mips3_float_mode == mode);
}
} // namespace

int main() try {
    for (unsigned mode : {0U, 1U}) {
        recomp_context cpu;
        init_live_cpu(cpu, mode);
        const recomp_context before = cpu;

        dkr::runtime::GuestCallContext call(cpu);
        // The copy carries the registers and points at its own odd words.
        check(call->r4 == 42 && call->r29 == 0x80110000U && call->status_reg == 0x12345678U);
        check(points_into_itself(call) && call->f_odd != cpu.f_odd);
        call_through(cpu, mode);

        // A callee writing an odd register changes only the copy.
        odd_write(call.context, 19, 0x3FE00000U);
        check(std::memcmp(&before, &cpu, sizeof(cpu)) == 0);

        // reset() starts again from the live context and repairs again, also
        // after the live context changed float mode.
        recomp_context other;
        init_live_cpu(other, mode ^ 1U);
        call.reset(other);
        check(points_into_itself(call) && call->mips3_float_mode == (mode ^ 1U));
        call.reset(cpu);
        check(points_into_itself(call) && call->mips3_float_mode == mode);

        if (mode == 0) {
            // Paired float mode: the odd register is the double's high word.
            // The callee sees its own, and the live double is not disturbed.
            call->f18.u32l = 0;
            odd_write(call.context, 19, 0x3FE00000U);
            check(call->f18.d == 0.5);
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
    // Repairing in place follows the float mode.
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
