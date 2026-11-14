#pragma once
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "experimental_runtime.hpp"
#endif

namespace dkr::runtime::netplay::experimental {

// True while the opt-in owned rollback runtime drives the frame. Always
// false in builds without DKR_EXPERIMENTAL_RACE_TEST, so UI call sites that
// defer a setting to the owned runtime need no #if of their own.
inline bool owned_runtime_active() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    return runtime_view().active;
#else
    return false;
#endif
}

} // namespace dkr::runtime::netplay::experimental
