#pragma once

#include "netplay_types.hpp"

namespace dkr::runtime::netplay::experimental {

// Persist the experimental opt-in separately from the original 0/1 setting.
// Older builds ignore the new key and recover the user's actual stable choice.
constexpr SynchronizationMode selected_mode(int stable_choice, bool experimental_enabled) {
    return experimental_enabled ? SynchronizationMode::ExperimentalRollback :
        (stable_choice == 1 ? SynchronizationMode::Lockstep : SynchronizationMode::Rollback);
}
constexpr bool select_mode(int choice, int& stable_choice, bool& experimental_enabled) {
    if (choice < 0 || choice > 2) return false;
    experimental_enabled = choice == 2;
    if (!experimental_enabled) stable_choice = choice;
    return true;
}

// A working generic driver is not a working DKR adapter. Keep this explicit
// until the game has a synchronous replay tick, complete owned state capture,
// and transactional audio/save/scene effects. In particular, never substitute
// capture_authoritative_state() or RuntimeState's raw native-thread RAM dump.
// This is checked BEFORE creating a profile/transport/countdown, and again on
// received start descriptors. There is no environment-variable bypass.
inline constexpr const char* runtime_admission_error(SynchronizationMode mode) {
    if (mode != SynchronizationMode::ExperimentalRollback) return nullptr;
    return "Experimental rollback is still in development: the complete game-state replay adapter is not ready. "
           "Select Host prediction or Strict input sync to play using the existing implementation.";
}

} // namespace dkr::runtime::netplay::experimental
