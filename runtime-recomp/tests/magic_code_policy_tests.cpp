#include "magic_code_policy.hpp"
#include "magic_code_runtime_policy.hpp"

#include <cassert>
#include <cstdio>

using namespace dkr::runtime::magic_codes;

static_assert(kMagicCodeDefinitions.size() == 24U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(0)) == 0U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(9)) == 0U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(28)) != 0U);
static_assert((kOneShotMagicCodeMask & magic_code_bit(10)) != 0U);
static_assert((kOneShotMagicCodeMask & magic_code_bit(26)) != 0U);
static_assert((kPersistentMagicCodeMask & magic_code_bit(10)) == 0U);

int main() {
    std::uint32_t mask = 0U;
    mask = enable_magic_code(mask, 4);
    mask = enable_magic_code(mask, 5);
    assert(!magic_code_enabled(mask, 4));
    assert(magic_code_enabled(mask, 5));

    mask = enable_magic_code(mask, 12);
    mask = enable_magic_code(mask, 14);
    assert(!magic_code_enabled(mask, 12));
    assert(magic_code_enabled(mask, 14));

    mask = enable_magic_code(mask, 11);
    mask = enable_magic_code(mask, 15);
    assert(!magic_code_enabled(mask, 11));
    assert(magic_code_enabled(mask, 15));
    mask = enable_magic_code(mask, 19);
    assert(!magic_code_enabled(mask, 15));
    assert(magic_code_enabled(mask, 19));
    mask = enable_magic_code(mask, 20);
    assert(magic_code_enabled(mask, 19));
    assert(magic_code_enabled(mask, 20));

    const auto normalised = normalise_magic_code_mask(0xFFFFFFFFU);
    assert((normalised & ~kSelectableMagicCodeMask) == 0U);
    assert(!(magic_code_enabled(normalised, 4) && magic_code_enabled(normalised, 5)));
    assert(!(magic_code_enabled(normalised, 11) &&
             (normalised & (magic_code_bit(15) | magic_code_bit(16) | magic_code_bit(17) | magic_code_bit(18) |
                            magic_code_bit(19) | magic_code_bit(20))) != 0U));

    const auto queued = magic_code_bit(10) | magic_code_bit(26);
    const auto progression = magic_code_bit(0) | magic_code_bit(1);
    const auto start = begin_magic_code_session(magic_code_bit(7), queued);
    const auto applied = apply_magic_code_session(start, progression, progression);
    assert(applied.active == (progression | magic_code_bit(7) | queued));
    assert(applied.unlocked == applied.active);
    // Neither a native toggle nor clear-all is an executed one-shot action.
    const auto cleared = apply_magic_code_session(applied.state, 0U, progression);
    assert(cleared.active == 0U);
    assert(cleared.state.completed_action_mask == 0U);
    assert(cleared.state.armed_action_mask == queued);
    // Replay and unsolicited native codes cannot acknowledge launcher queues.
    auto completed = complete_magic_code_action(applied.state, queued, false);
    assert(completed.completed_action_mask == 0U);
    completed = complete_magic_code_action(completed, magic_code_bit(7), true);
    assert(completed.completed_action_mask == 0U);
    completed = complete_magic_code_action(completed, magic_code_bit(26), true);
    assert(completed.completed_action_mask == magic_code_bit(26));
    completed = acknowledge_magic_code_actions(completed, magic_code_bit(26));
    assert(completed.armed_action_mask == magic_code_bit(10));
    assert(completed.deferred_action_mask == queued); // immutable launch identity
    completed = complete_magic_code_action(completed, magic_code_bit(26), true);
    assert(completed.completed_action_mask == 0U); // duplicate completion
    const auto online = begin_magic_code_session(magic_code_bit(24), queued, true);
    assert(online.deferred_action_mask == magic_code_bit(26));
    assert(online.persistent_mask == magic_code_bit(24));
    assert(!begin_magic_code_session(0U, 0U).applied);
    assert(!magic_code_manifest_needs_refresh(false, queued, queued));
    assert(magic_code_manifest_needs_refresh(false, 0U, queued));
    assert(magic_code_manifest_needs_refresh(false, queued, 0U));
    assert(!magic_code_manifest_needs_refresh(true, queued, 0U));

    std::puts("[test][magic-code-policy] PASS");
    return 0;
}
