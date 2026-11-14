#include "runtime_magic_codes.hpp"
#include "magic_code_policy.hpp"
#include "revision_addresses.hpp"
#include "recomp.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

// The production callbacks are tested here, not a second implementation.
// Only the network replay gate is substituted; no sockets or user saves exist.
static bool allow_side_effects = true;
namespace dkr::runtime::netplay {
bool external_side_effects_allowed() {
    return allow_side_effects;
}
} // namespace dkr::runtime::netplay
extern "C" void dkr_apply_launch_magic_codes(std::uint8_t*, recomp_context*);
extern "C" void dkr_magic_code_credits_started(std::uint8_t*, recomp_context*);
extern "C" void dkr_magic_code_balloon_awarded(std::uint8_t*, recomp_context*);
extern "C" void dkr_magic_codes_frame_complete(std::uint8_t*, recomp_context*);

int main() {
    using namespace dkr::runtime::magic_codes;
    namespace addresses = dkr::runtime::revision_addresses;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("dkr-magic-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(directory));
    configure(directory);
    std::string error;
    const auto credits = magic_code_bit(10);
    const auto balloon = magic_code_bit(26);
    std::vector<std::uint8_t> memory(8U * 1024U * 1024U, 0U);
    auto* rdram = memory.data();
    recomp_context context{};
    context.r4 = 0x12345678U;
    const recomp_context before = context;
    for (const auto revision : {dkr::runtime::rom::Revision::UsV77, dkr::runtime::rom::Revision::UsV80}) {
        assert(addresses::select(revision));
        const gpr active = static_cast<std::int32_t>(addresses::ActiveMagicCodes);
        const gpr unlocked = static_cast<std::int32_t>(addresses::UnlockedMagicCodes);
        // All selectable bits, on repeated sessions in the same process.
        for (const auto& code : kMagicCodeDefinitions) {
            assert(clear_all(error));
            assert(set_enabled(code.internal_index, true, error));
            begin_game_session();
            MEM_W(0, active) = 3U;
            MEM_W(0, unlocked) = 3U;
            const auto selected = magic_code_bit(code.internal_index);
            dkr_apply_launch_magic_codes(rdram, &context);
            assert(static_cast<std::uint32_t>(MEM_W(0, active)) == (3U | selected));
            assert(static_cast<std::uint32_t>(MEM_W(0, unlocked)) == (3U | selected));
            MEM_W(0, active) = 0U; // native clear-all, not a completed action
            dkr_apply_launch_magic_codes(rdram, &context);
            dkr_magic_codes_frame_complete(rdram, &context);
            assert(MEM_W(0, active) == 0U);
            assert(queued_one_shot_mask() == (selected & kOneShotMagicCodeMask));
        }
        assert(clear_all(error));
        assert(set_queued_one_shot_mask(credits | balloon, error));
        begin_game_session();
        dkr_apply_launch_magic_codes(rdram, &context);
        const auto identity = launch_mask();
        allow_side_effects = false;
        dkr_magic_code_balloon_awarded(rdram, &context);
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == (credits | balloon));
        allow_side_effects = true;
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == (credits | balloon));
        dkr_magic_code_balloon_awarded(rdram, &context);
        assert(queued_one_shot_mask() == (credits | balloon)); // before mode returns
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == credits);
        assert(launch_mask() == identity);
        dkr_magic_code_balloon_awarded(rdram, &context);
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == credits);
        // Requeue while the old session still owns an action.
        assert(set_enabled(10, false, error));
        assert(set_enabled(10, true, error));
        dkr_magic_code_credits_started(rdram, &context);
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == credits);
        // Host manifest overrides local preferences; credits stay queued online.
        set_persistent_mask(magic_code_bit(4));
        begin_game_session(magic_code_bit(24) | credits);
        MEM_W(0, active) = MEM_W(0, unlocked) = 0U;
        dkr_apply_launch_magic_codes(rdram, &context);
        assert(MEM_W(0, active) == magic_code_bit(24));
        set_persistent_mask(0U);
        assert(launch_mask() == magic_code_bit(24));
        dkr_magic_code_credits_started(rdram, &context);
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == credits);
        begin_game_session();
        dkr_apply_launch_magic_codes(rdram, &context);
        dkr_magic_code_credits_started(rdram, &context);
        dkr_magic_codes_frame_complete(rdram, &context);
        assert(queued_one_shot_mask() == 0U);
    }
    assert(std::memcmp(&context, &before, sizeof(context)) == 0);
    // Queue setter transactions may not lose concurrent edits.
    for (int trial = 0; trial < 16; ++trial) {
        assert(set_queued_one_shot_mask(0U, error));
        std::thread a([] {
            std::string e;
            assert(set_enabled(10, true, e));
        });
        std::thread b([] {
            std::string e;
            assert(set_enabled(26, true, e));
        });
        a.join();
        b.join();
        assert(queued_one_shot_mask() == (credits | balloon));
    }
    // A failed disk replacement leaves memory unchanged and exposes an error.
    const auto temporary = directory / "magic-codes-next-launch.txt.tmp";
    assert(std::filesystem::create_directory(temporary));
    std::ofstream(temporary / "blocker") << "test";
    assert(!set_queued_one_shot_mask(balloon, error));
    assert(!queue_error().empty());
    assert(queued_one_shot_mask() == (credits | balloon));
    begin_game_session();
    dkr_apply_launch_magic_codes(rdram, &context);
    dkr_magic_code_balloon_awarded(rdram, &context);
    dkr_magic_codes_frame_complete(rdram, &context);
    assert(queued_one_shot_mask() == (credits | balloon));
    assert(std::filesystem::remove(temporary / "blocker"));
    assert(std::filesystem::remove(temporary));
    std::this_thread::sleep_for(std::chrono::milliseconds(5100));
    dkr_magic_codes_frame_complete(rdram, &context);
    assert(queued_one_shot_mask() == credits);
    assert(queue_error().empty());
    configure(directory); // persistence across process-equivalent configure
    assert(queued_one_shot_mask() == credits);
    assert(clear_all(error));
    assert(std::filesystem::remove(directory));
    std::puts(
        "[test][magic-code-runtime] PASS: both revisions, lifecycle, native completion, replay, immutable online selection, requeue, concurrency, IO retry");
}
