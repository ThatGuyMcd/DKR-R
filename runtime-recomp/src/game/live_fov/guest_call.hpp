#pragma once
#include <cstdint>
#include "../guest_call_context.hpp"

namespace dkr::runtime::live_fov {
inline void apply_guest_fov(std::uint8_t* rdram, const recomp_context& context, float target, recomp_func_t* setter) {
    GuestCallContext call(context);
    call->f12.fl = target;
    setter(rdram, call.get());
}
} // namespace dkr::runtime::live_fov
