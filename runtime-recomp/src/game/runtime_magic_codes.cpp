#include "runtime_magic_codes.hpp"

#include "magic_code_policy.hpp"
#include "magic_code_runtime_policy.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"

#include "recomp.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <system_error>

namespace {

const std::uint32_t& kActiveMagicCodesAddress = dkr::runtime::revision_addresses::ActiveMagicCodes;
const std::uint32_t& kUnlockedMagicCodesAddress = dkr::runtime::revision_addresses::UnlockedMagicCodes;

std::atomic<std::uint32_t> g_persistent_mask{0U};
std::atomic<std::uint32_t> g_queued_one_shot_mask{0U};
std::filesystem::path g_one_shot_path;
std::mutex g_queue_file_guard;
std::array<std::uint64_t, 32> g_queue_generations{};
std::array<std::uint64_t, 32> g_launch_generations{};
std::atomic<std::uint32_t> g_launch_mask{0U};
std::string g_queue_error;
// Only the emulated game thread accesses session state. It is reset on the
// main thread before that thread is created (and after the previous one joins).
dkr::runtime::magic_codes::MagicCodeSessionState g_session_state{};
std::uint64_t g_session_sequence = 0U;
std::chrono::steady_clock::time_point g_retry_after{};

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
bool ReplaceQueueFile(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool PersistOneShotQueueLocked(std::uint32_t mask, std::string& error) {
    if (g_one_shot_path.empty()) {
        error = "Magic Code launch queue is not configured yet.";
        return false;
    }
    std::error_code filesystem_error;
    if (mask == 0U) {
        std::filesystem::remove(g_one_shot_path, filesystem_error);
        if (filesystem_error) {
            error = "Could not clear the Magic Code launch queue: " + filesystem_error.message();
            return false;
        }
        return true;
    }

    std::filesystem::create_directories(g_one_shot_path.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the Magic Code settings folder: " + filesystem_error.message();
        return false;
    }
    const auto temporary = g_one_shot_path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        output << mask << '\n';
        output.flush();
        if (!output) {
            error = "Could not write the Magic Code launch queue.";
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    if (!ReplaceQueueFile(temporary, g_one_shot_path)) {
        error = "Could not safely replace the Magic Code launch queue.";
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    return true;
}

bool SetQueueLocked(std::uint32_t mask, std::string& error) {
    mask &= dkr::runtime::magic_codes::kOneShotMagicCodeMask;
    if (!PersistOneShotQueueLocked(mask, error)) {
        g_queue_error = error;
        return false;
    }
    const auto changed = g_queued_one_shot_mask.load(std::memory_order_relaxed) ^ mask;
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (changed & (1U << bit))
            ++g_queue_generations[bit];
    }
    g_queued_one_shot_mask.store(mask, std::memory_order_release);
    g_queue_error.clear();
    error.clear();
    return true;
}

bool AcknowledgeQueuedActions(std::uint32_t consumed, std::string& error) {
    std::lock_guard lock(g_queue_file_guard);
    const std::uint32_t current = g_queued_one_shot_mask.load(std::memory_order_acquire);
    // An overlay edit may have cancelled and requeued the same action for a
    // later launch. Completion of the old request must not erase the new one.
    std::uint32_t eligible = 0U;
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (g_launch_generations[bit] == g_queue_generations[bit]) {
            eligible |= consumed & (1U << bit);
        }
    }
    const std::uint32_t next = current & ~eligible;
    if (next == current) {
        return true;
    }
    return SetQueueLocked(next, error);
}

} // namespace

void dkr::runtime::magic_codes::configure(const std::filesystem::path& config_directory) {
    std::lock_guard lock(g_queue_file_guard);
    g_one_shot_path = config_directory / "magic-codes-next-launch.txt";
    std::ifstream input(g_one_shot_path);
    std::uint32_t mask = 0U;
    if (input) {
        unsigned long long parsed = 0U;
        input >> parsed;
        if (input && parsed <= 0xFFFFFFFFULL) {
            mask = static_cast<std::uint32_t>(parsed) & kOneShotMagicCodeMask;
        } else {
            std::fprintf(stderr, "[boot][magic-codes] ignored malformed one-shot queue\n");
        }
    }
    g_queued_one_shot_mask.store(mask, std::memory_order_release);
    g_queue_generations = {};
    g_launch_generations = {};
    g_queue_error.clear();
    g_launch_mask.store(0U, std::memory_order_release);
    g_session_state = {};
}

void dkr::runtime::magic_codes::begin_game_session(std::optional<std::uint32_t> online_mask) {
    std::lock_guard lock(g_queue_file_guard);
    const std::uint32_t selected = online_mask.value_or(selected_mask());
    g_session_state = begin_magic_code_session(selected & kPersistentMagicCodeMask, selected & kOneShotMagicCodeMask,
                                               online_mask.has_value());
    g_launch_generations = g_queue_generations;
    g_launch_mask.store(g_session_state.persistent_mask | g_session_state.deferred_action_mask,
                        std::memory_order_release);
    g_retry_after = {};
    ++g_session_sequence;
    std::fprintf(stderr, "[boot][magic-codes] session=%llu persistent=0x%08X deferred=0x%08X\n",
                 static_cast<unsigned long long>(g_session_sequence),
                 static_cast<unsigned>(g_session_state.persistent_mask),
                 static_cast<unsigned>(g_session_state.deferred_action_mask));
}

std::uint32_t dkr::runtime::magic_codes::persistent_mask() {
    return g_persistent_mask.load(std::memory_order_acquire);
}

std::uint32_t dkr::runtime::magic_codes::launch_mask() {
    return g_launch_mask.load(std::memory_order_acquire);
}

std::string dkr::runtime::magic_codes::queue_error() {
    std::lock_guard lock(g_queue_file_guard);
    return g_queue_error;
}

void dkr::runtime::magic_codes::set_persistent_mask(std::uint32_t mask) {
    g_persistent_mask.store(normalise_magic_code_mask(mask) & kPersistentMagicCodeMask, std::memory_order_release);
}

std::uint32_t dkr::runtime::magic_codes::queued_one_shot_mask() {
    return g_queued_one_shot_mask.load(std::memory_order_acquire);
}

bool dkr::runtime::magic_codes::set_queued_one_shot_mask(std::uint32_t mask, std::string& error) {
    std::lock_guard lock(g_queue_file_guard);
    return SetQueueLocked(mask, error);
}

std::uint32_t dkr::runtime::magic_codes::selected_mask() {
    return persistent_mask() | queued_one_shot_mask();
}

bool dkr::runtime::magic_codes::set_enabled(std::uint8_t internal_index, bool enabled, std::string& error) {
    const std::uint32_t bit = magic_code_bit(internal_index);
    if ((bit & kSelectableMagicCodeMask) == 0U) {
        error = "That Magic Code is not available in the retail game.";
        return false;
    }
    if ((bit & kOneShotMagicCodeMask) != 0U) {
        std::lock_guard lock(g_queue_file_guard);
        std::uint32_t next = queued_one_shot_mask();
        next = enabled ? enable_magic_code(next, internal_index) : disable_magic_code(next, internal_index);
        return SetQueueLocked(next, error);
    }

    std::uint32_t next = persistent_mask();
    next = enabled ? enable_magic_code(next, internal_index) : disable_magic_code(next, internal_index);
    set_persistent_mask(next);
    return true;
}

bool dkr::runtime::magic_codes::clear_all(std::string& error) {
    if (!set_queued_one_shot_mask(0U, error)) {
        return false;
    }
    set_persistent_mask(0U);
    return true;
}

extern "C" void dkr_apply_launch_magic_codes(std::uint8_t* rdram, recomp_context*) {
    if (g_session_state.applied)
        return;
    const gpr active_address = RdramAddress(kActiveMagicCodesAddress);
    const gpr unlocked_address = RdramAddress(kUnlockedMagicCodesAddress);
    const auto update = dkr::runtime::magic_codes::apply_magic_code_session(
        g_session_state, static_cast<std::uint32_t>(MEM_W(0, active_address)),
        static_cast<std::uint32_t>(MEM_W(0, unlocked_address)));
    g_session_state = update.state;
    MEM_W(0, active_address) = update.active;
    MEM_W(0, unlocked_address) = update.unlocked;
    std::fprintf(stderr, "[boot][magic-codes] applied session=%llu mask=0x%08X\n",
                 static_cast<unsigned long long>(g_session_sequence), static_cast<unsigned>(update.applied_mask));
}

// These hooks do not modify RDRAM or registers. They observe only the native
// credits-init return and the exclusive file-select balloon-award branch.
extern "C" void dkr_magic_code_credits_started(std::uint8_t*, recomp_context*) {
    g_session_state = dkr::runtime::magic_codes::complete_magic_code_action(
        g_session_state, 1U << 10, dkr::runtime::netplay::external_side_effects_allowed());
}

extern "C" void dkr_magic_code_balloon_awarded(std::uint8_t*, recomp_context*) {
    g_session_state = dkr::runtime::magic_codes::complete_magic_code_action(
        g_session_state, 1U << 26, dkr::runtime::netplay::external_side_effects_allowed());
}

extern "C" void dkr_magic_codes_frame_complete(std::uint8_t*, recomp_context*) {
    const auto completed = g_session_state.completed_action_mask;
    if (completed == 0U || !dkr::runtime::netplay::external_side_effects_allowed())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_retry_after)
        return;
    std::string error;
    if (!AcknowledgeQueuedActions(completed, error)) {
        g_retry_after = now + std::chrono::seconds(5);
        std::fprintf(stderr, "[boot][magic-codes] queue acknowledgement failed: %s\n", error.c_str());
        return;
    }
    g_session_state = dkr::runtime::magic_codes::acknowledge_magic_code_actions(g_session_state, completed);
    std::fprintf(stderr, "[boot][magic-codes] native action completed mask=0x%08X\n", static_cast<unsigned>(completed));
}
