#pragma once
#include "recomp.h"

namespace dkr::runtime {

// recomp_context::f_odd points inside the context that owns it. A plain copy
// keeps pointing at the source: the callee's writes to odd float registers land
// in the live CPU, and double arithmetic in the callee reads stale high words
// from the copy (notably in the retail sin/cos projection routines).
inline void repair_float_odd_pointer(recomp_context& context) noexcept {
    context.f_odd = context.mips3_float_mode ? &context.f1.u32l : &context.f0.u32h;
}

// The context for one native-to-guest call, built in place from the live one.
//
// The pointer is only right while the struct stays where it was repaired, so
// this type cannot be copied, moved or returned by value (a named local is not
// guaranteed to be elided, and a moved-from struct would point into a dead
// object). Construct it where the call is made, and reset() it to reuse it:
//
//     GuestCallContext guest(*context);
//     guest->r4 = argument;
//     function(rdram, guest.get());
class GuestCallContext {
  public:
    explicit GuestCallContext(const recomp_context& source) noexcept {
        reset(source);
    }
    GuestCallContext(const GuestCallContext&) = delete;
    GuestCallContext& operator=(const GuestCallContext&) = delete;

    // Start the next call from the live context again.
    void reset(const recomp_context& source) noexcept {
        context = source;
        repair_float_odd_pointer(context);
    }

    recomp_context* get() noexcept {
        return &context;
    }
    recomp_context* operator->() noexcept {
        return &context;
    }

    recomp_context context;
};

} // namespace dkr::runtime
