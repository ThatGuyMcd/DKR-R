#pragma once

#include "magic_code_policy.hpp"

#include <cstdint>

namespace dkr::runtime::magic_codes {

// Pure, unit-testable state for one emulated DKR session. The application may
// host several sessions without restarting, but every session receives fresh
// RDRAM and must therefore apply the selected codes independently.
struct MagicCodeSessionState {
    std::uint32_t persistent_mask = 0U;
    std::uint32_t deferred_action_mask = 0U;
    std::uint32_t armed_action_mask = 0U;
    std::uint32_t completed_action_mask = 0U;
    bool applied = false;
};

struct MagicCodeWordUpdate {
    MagicCodeSessionState state{};
    std::uint32_t active = 0U;
    std::uint32_t unlocked = 0U;
    std::uint32_t applied_mask = 0U;
};

constexpr MagicCodeSessionState begin_magic_code_session(std::uint32_t persistent, std::uint32_t deferred_actions,
                                                         bool online = false) {
    return {
        normalise_magic_code_mask(persistent) & kPersistentMagicCodeMask,
        deferred_actions & kOneShotMagicCodeMask & (online ? ~magic_code_bit(10) : 0xFFFFFFFFU),
        0U,
        0U,
        false,
    };
}

constexpr MagicCodeWordUpdate apply_magic_code_session(MagicCodeSessionState state, std::uint32_t active,
                                                       std::uint32_t unlocked) {
    if (state.applied) {
        return {state, active, unlocked, 0U};
    }

    const std::uint32_t selected = normalise_magic_code_mask(state.persistent_mask | state.deferred_action_mask);
    state.applied = true;
    state.armed_action_mask = selected & kOneShotMagicCodeMask;
    return {
        state,
        active | selected,
        unlocked | selected,
        selected,
    };
}

// Only branch-specific native success hooks may record completion. Clearing
// or toggling a bit in the native menu is NOT evidence of an executed action.
// Speculative/replayed ticks must not produce persistent host-side events.
constexpr MagicCodeSessionState complete_magic_code_action(MagicCodeSessionState state, std::uint32_t action,
                                                           bool authoritative) {
    if (authoritative) {
        state.completed_action_mask |= action & state.armed_action_mask;
    }
    return state;
}

constexpr MagicCodeSessionState acknowledge_magic_code_actions(MagicCodeSessionState state, std::uint32_t consumed) {
    state.armed_action_mask &= ~consumed;
    state.completed_action_mask &= ~consumed;
    // Keep the original launch selection immutable, including consumed codes.
    return state;
}

} // namespace dkr::runtime::magic_codes
