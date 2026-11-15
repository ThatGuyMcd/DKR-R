#pragma once
#include "recomp.h"

namespace dkr::runtime {

// A context to hand to a recompiled guest function from native code.
//
// recomp_context::f_odd points inside the context that owns it. A plain copy
// keeps pointing at the source: the callee's writes to odd float registers land
// in the live CPU, and double arithmetic in the callee reads stale high words
// from the copy (notably in the retail sin/cos projection routines). Repair the
// pointer on every copy; never copy a context for a guest call any other way.
inline void repair_float_odd_pointer(recomp_context& context) noexcept {
    context.f_odd = context.mips3_float_mode ? &context.f1.u32l : &context.f0.u32h;
}

[[nodiscard]] inline recomp_context guest_call_context(const recomp_context& source) noexcept {
    recomp_context call = source;
    repair_float_odd_pointer(call);
    return call;
}

} // namespace dkr::runtime
