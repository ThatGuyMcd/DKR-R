#pragma once

#include "netplay_types.hpp"

namespace dkr::runtime::netplay::experimental {

// Persist the experimental opt-in separately from the original 0/1 setting.
// Older builds ignore the new key and recover the user's actual stable choice.
constexpr SynchronizationMode selected_mode(int stable_choice, bool experimental_enabled) {
    return experimental_enabled ? SynchronizationMode::ExperimentalRollback
                                : (stable_choice == 1 ? SynchronizationMode::Lockstep : SynchronizationMode::Rollback);
}
constexpr bool select_mode(int choice, int& stable_choice, bool& experimental_enabled) {
    if (choice < 0 || choice > 2)
        return false;
    experimental_enabled = choice == 2;
    if (!experimental_enabled)
        stable_choice = choice;
    return true;
}

// Auto is a local host preference, not a fourth wire protocol/backend. Resolve
// it before constructing lobby Rules so all peers negotiate a concrete mode.
constexpr int host_backend_choice(int stable_choice, bool experimental_enabled, bool automatic) {
    return automatic ? 0 : static_cast<int>(selected_mode(stable_choice, experimental_enabled)) + 1;
}
constexpr bool select_host_backend(int choice, int& stable_choice, bool& experimental_enabled, bool& automatic) {
    if (choice < 0 || choice > 3)
        return false;
    automatic = choice == 0;
    // Retain the last explicit backend when selecting Auto.
    return automatic || select_mode(choice - 1, stable_choice, experimental_enabled);
}
constexpr SynchronizationMode resolved_host_backend(int stable_choice, bool experimental_enabled, bool automatic) {
    return automatic ? SynchronizationMode::ExperimentalRollback : selected_mode(stable_choice, experimental_enabled);
}
constexpr bool migrated_automatic_backend(bool has_automatic_setting, bool saved_automatic,
                                          bool has_legacy_backend_setting) {
    return has_automatic_setting ? saved_automatic : !has_legacy_backend_setting;
}

// A working generic driver is not a working DKR adapter. Keep this explicit
// until the game has a synchronous replay tick, complete owned state capture,
// and transactional audio/save/scene effects. In particular, never substitute
// capture_authoritative_state() or RuntimeState's raw native-thread RAM dump.
// This is checked BEFORE creating a profile/transport/countdown, and again on
// received start descriptors. There is no environment-variable bypass.
inline constexpr const char* runtime_admission_error(SynchronizationMode mode, bool owned_adapter_available = false) {
    if (mode != SynchronizationMode::ExperimentalRollback || owned_adapter_available)
        return nullptr;
    return "Experimental rollback is still in development: the complete game-state replay adapter is not ready. "
           "Select Host prediction or Strict input sync to play using the existing implementation.";
}

} // namespace dkr::runtime::netplay::experimental
