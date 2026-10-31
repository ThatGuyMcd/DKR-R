#pragma once
#include <cstdint>
#include "recomp.h"

namespace dkr::runtime::live_fov {
inline void apply_guest_fov(std::uint8_t* rdram, const recomp_context& context, float target, recomp_func_t* setter) {
    recomp_context call = context;
    // f_odd points inside its owning context. A plain copy leaves odd-register
    // writes targeting the live CPU, while double arithmetic reads stale high
    // words in the copy (notably in the retail sin/cos projection routines).
    call.f_odd = call.mips3_float_mode ? &call.f1.u32l : &call.f0.u32h;
    call.f12.fl = target;
    setter(rdram, &call);
}
} // namespace dkr::runtime::live_fov
