#pragma once
#include "netplay/netplay_types.hpp"

namespace dkr::runtime::netplay::rollback_policy {
// Public Rollback is the existing owned replay engine, wire value 2. Do not
// reuse the legacy enum named Rollback (wire value 0, host prediction).
inline constexpr auto mode = SynchronizationMode::ExperimentalRollback;
inline constexpr int maximum_players = 4;
inline constexpr int prediction_window = 14;
inline constexpr int input_delay = 6;
inline constexpr bool automatic_delay = false;
inline constexpr bool record_replay = false;
inline constexpr bool development = false;
inline constexpr bool spectators = true;
inline constexpr const char* unsupported_lobby =
    "This lobby uses a sync mode that is no longer supported. Ask the host to update DKR-R and create a Rollback lobby.";
static_assert(static_cast<unsigned>(mode) == 2);

constexpr bool supported(SynchronizationMode value) { return value == mode; }
constexpr const char* name(SynchronizationMode value) {
    return supported(value) ? "Rollback" : "Unsupported sync mode";
}
constexpr const char* admission_error(SynchronizationMode value,
                                    bool adapter_available = false) {
    if (!supported(value))
        return unsupported_lobby;
    if (!adapter_available)
        return "Rollback is unavailable in this build. Install the complete DKR-R package on every player's machine.";
    return nullptr;
}
constexpr Rules default_rules() {
    Rules result;
    result.host_control = HostControlPolicy::GuidedUntilCharacterSelect;
    result.maximum_players = maximum_players;
    result.synchronization = mode;
    result.rollback_window = prediction_window;
    result.automatic_input_delay = automatic_delay;
    result.manual_input_delay = input_delay;
    result.record_replay = record_replay;
    result.development = development;
    return result;
}
static_assert(default_rules().synchronization == mode);
static_assert(default_rules().host_control == HostControlPolicy::GuidedUntilCharacterSelect);
static_assert(default_rules().maximum_players == 4);
static_assert(default_rules().rollback_window == 14);
static_assert(default_rules().manual_input_delay == 6 && kMaximumInputDelayFrames >= 6);
static_assert(!default_rules().automatic_input_delay && !default_rules().record_replay);
static_assert(!default_rules().development);
}
