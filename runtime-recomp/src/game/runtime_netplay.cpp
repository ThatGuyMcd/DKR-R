#include "runtime_netplay.hpp"
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "replay_probe_capture.hpp"
#endif

#include "determinism_hash_policy.hpp"
#include "determinism_state_hash.hpp"
#include "netplay/authoritative_state.hpp"
#include "netplay/authoritative_state_codec.hpp"
#include "netplay/failure_recorder.hpp"
#include "netplay/gekko_direct_adapter.hpp"
#include "netplay/netplay_pacing_policy.hpp"
#include "netplay/two_player_adventure_policy.hpp"
#include "netplay/progress_budget.hpp"
#include "netplay/online_input_broker.hpp"
#include "netplay/rollback_simulation_state.hpp"
#include "netplay/experimental_runtime_admission.hpp"
#include "netplay/rollback_state_store.hpp"
#include "netplay/runtime_state.hpp"
#include "online_roster_policy.hpp"
#include "magic_code_policy.hpp"
#include "presentation_random_policy.hpp"
#include "revision_addresses.hpp"
#include "runtime_magic_codes.hpp"
#include "runtime_platform.hpp"
#include "runtime_input.hpp"
#include "runtime_save_routing.hpp"
#include "game_payload.hpp"
#include "vehicle_context_policy.hpp"
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include "gekkonet.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

extern "C" void dkr_presentation_scene_begin(std::uint8_t*, recomp_context*);

DirectSession g_owned_session;
DirectSession* g_external_session = nullptr;
DirectSession& active_session() {
    return g_external_session != nullptr ? *g_external_session
                                         : g_owned_session;
}
// Keep the existing call sites readable while allowing tests and embedded
// session owners to provide the active transport state.
#define g_session (active_session())
RuntimeState g_runtime_state;
std::atomic<std::uint32_t> g_authored_frame{0U};
std::atomic<std::uint32_t> g_authored_input_epoch{0U};
std::atomic<FrameDebtPhase> g_frame_debt_phase{FrameDebtPhase::Inactive};
OnlineWaitState g_online_wait_state{};
std::atomic<TwoPlayerAdventurePhase> g_two_player_adventure_phase{
    TwoPlayerAdventurePhase::GuidedMenus};
std::atomic<bool> g_loaded_announced{false};
std::atomic<bool> g_assigned_ports_released{false};
std::atomic<bool> g_simulation_halted{false};
std::atomic<std::uint32_t> g_first_determinism_hash_frame{
    kDeterminismNotArmed};
std::atomic<bool> g_rollback_gameplay_ready{false};
std::atomic<bool> g_rollback_advancing{false};
std::atomic<bool> g_external_side_effects{true};
std::atomic<bool> g_skip_next_si_physical_poll{false};
std::atomic<std::uint32_t> g_authored_pacing_scale_milli{1000U};
std::atomic<std::uint32_t> g_authored_pacing_target_hz{30U};
std::atomic<ClientCatchUpState> g_client_catch_up_state{
    ClientCatchUpState::Normal};
std::atomic<FrameDebtPhase> g_last_logged_frame_debt_phase{
    FrameDebtPhase::Inactive};
ClientCatchUpController g_client_catch_up_controller{};
constexpr std::uint32_t kNoPreparedAuthoredFrame = UINT32_MAX;
std::atomic<std::uint32_t> g_prepared_authored_frame{
    kNoPreparedAuthoredFrame};
thread_local std::uint32_t g_presentation_random_scope_depth = 0U;
thread_local PresentationRandomStream g_presentation_random_stream{};

void reset_client_catch_up_pacing() {
    g_client_catch_up_controller.reset();
    g_client_catch_up_state.store(ClientCatchUpState::Normal,
                                  std::memory_order_release);
    g_authored_pacing_scale_milli.store(1000U,
                                        std::memory_order_release);
    g_authored_pacing_target_hz.store(30U, std::memory_order_release);
}

void update_client_catch_up_pacing(const ClientCatchUpSample& sample) {
    const ClientCatchUpDecision decision =
        g_client_catch_up_controller.update(sample);
    g_client_catch_up_state.store(decision.state,
                                  std::memory_order_release);
    g_authored_pacing_scale_milli.store(decision.pacing_scale_milli,
                                        std::memory_order_release);
    g_authored_pacing_target_hz.store(decision.target_simulation_hz,
                                      std::memory_order_release);
}

struct RollbackToken {
    std::uint64_t magic = 0U;
    std::uint64_t checksum = 0U;
    std::int32_t frame = -1;
    std::uint32_t reserved = 0U;
};
static_assert(sizeof(RollbackToken) == 24U);
constexpr std::uint64_t kRollbackTokenMagic = 0x444B52524F4C4C31ULL;
// DKR advances an NTSC 30 Hz simulation with two 60 Hz logic quanta. Retail
// normally derives this value from fb_update(), which reflects local renderer
// timing and is therefore not deterministic between network peers.
constexpr std::int32_t kRollbackLogicUpdateRate = 2;
// Per-frame controller commits remain 30 Hz. Complete portable race state is
// corrective data, not a video stream. It uses a dedicated latest-wins lane,
// so it cannot hold the input ledger or lifecycle control behind stale state.
// Keep a small authored-frame jitter margin in rollback. Normal dispatch still
// advances exactly one game tick. If a guest accumulates excess debt, the
// recovery path installs a complete authenticated host snapshot and validates
// every skipped immutable commit before advancing its session cursor. This
// avoids hidden nested game loops and never changes simulation speed.
constexpr std::uint32_t kClientMaximumCatchUpTicks = 3U;
constexpr std::uint32_t kLockstepClientTargetFrameDebt = 0U;
// A reliable full-state anchor limits how long a burst of disposable replica
// loss can leave a rollback guest relying on local deterministic simulation.
// Keep this sparse: reliable anchors share an ordered transport lane with
// lifecycle control, so sending the complete portable state five times per
// second can head-of-line block a race-start, recovery or finish barrier on a
// congested link. Intermediate corrections remain latest-wins at the bounded
// cadence in netplay_pacing_policy.hpp, and a missing scheduled frame can
// still request a reliable repair from retained state.
constexpr std::uint32_t kLiveReplicaKeyframeInterval =
    kAuthoritativeDeltaKeyframeInterval;

struct RollbackCoordinator {
    struct DigestRecord {
        std::int32_t frame = -2;
        std::uint32_t folded = 0U;
        GameplayStateDigest digest{};
    };

    GekkoSession* gekko = nullptr;
    bool host_authoritative = false;
    GekkoDirectAdapter adapter{};
    std::unique_ptr<RollbackStateStore> states;
    std::vector<std::uint8_t> capture;
    std::vector<std::uint8_t> restore;
    std::array<int, kMaximumPlayers> handles{-1, -1, -1, -1};
    int local_handle = -1;
    std::uint32_t rollback_count = 0U;
    std::uint32_t replayed_frames = 0U;
    std::uint32_t largest_rollback = 0U;
    std::uint32_t latest_frame = 0U;
    std::uint32_t first_frame = 0U;
    float frames_ahead = 0.0F;
    std::array<DigestRecord, 64U> digests{};
};

RollbackCoordinator g_rollback;
FrameInputs g_rollback_frame_inputs{};
std::uint32_t g_rollback_event_frame = 0U;
std::uint32_t g_rollback_authored_base = 0U;

struct LiveReplicaCoordinator {
    std::uint32_t published_frame = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t published_checksum = 0U;
    std::uint32_t installed_frame = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t discarded_frame = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint8_t> capture;
    std::vector<std::uint8_t> received;

    void reset() {
        published_frame = std::numeric_limits<std::uint32_t>::max();
        published_checksum = 0U;
        installed_frame = std::numeric_limits<std::uint32_t>::max();
        discarded_frame = std::numeric_limits<std::uint32_t>::max();
        capture.clear();
        received.clear();
    }
};

LiveReplicaCoordinator g_live_replica;

struct RacerOrientationCoordinator {
    struct HistoricalSample {
        std::uint32_t frame = 0U;
        std::vector<std::uint8_t> state;
    };

    std::deque<HistoricalSample> local_history;
    std::vector<std::uint8_t> capture;
    std::vector<std::uint8_t> received;
    std::uint32_t last_applied_frame =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t last_reported_failure =
        std::numeric_limits<std::uint32_t>::max();

    void reset() {
        local_history.clear();
        capture.clear();
        received.clear();
        last_applied_frame = std::numeric_limits<std::uint32_t>::max();
        last_reported_failure = std::numeric_limits<std::uint32_t>::max();
    }
};

RacerOrientationCoordinator g_racer_orientation;

enum class GameplayStartStage : std::uint8_t {
    Idle,
    AwaitingPeers,
    AwaitingBaseline,
    AwaitingAcknowledgements,
    AwaitingGo,
};

struct GameplayStartCoordinator {
    GameplayStartStage stage = GameplayStartStage::Idle;
    std::optional<std::uint32_t> requested_map;
    std::optional<int> requested_race_type;
    std::uint32_t resolved_map = 0U;
    std::uint32_t racer_count = 0U;
    bool empty_cutscene = false;
    std::vector<std::uint8_t> local_baseline;
    std::vector<std::uint8_t> host_baseline;
    ProgressBudget deadline{};

    bool active() const { return stage != GameplayStartStage::Idle; }
    void reset() {
        stage = GameplayStartStage::Idle;
        requested_map.reset();
        requested_race_type.reset();
        resolved_map = 0U;
        racer_count = 0U;
        empty_cutscene = false;
        local_baseline.clear();
        host_baseline.clear();
        deadline = {};
    }
};

GameplayStartCoordinator g_gameplay_start;

enum class FinishStage : std::uint8_t {
    Idle,
    AwaitingAnnouncement,
    AwaitingTransition,
};

struct FinishCoordinator {
    FinishStage stage = FinishStage::Idle;
    std::uint32_t frame = 0U;
    std::uint8_t transition_kind = 0U;
    ProgressBudget deadline{};

    bool active() const { return stage != FinishStage::Idle; }
    void reset() {
        stage = FinishStage::Idle;
        frame = 0U;
        transition_kind = 0U;
        deadline = {};
    }
};

FinishCoordinator g_finish;

enum class RecoveryCoordinatorStage : std::uint8_t {
    Idle,
    AwaitingBaseline,
    AwaitingResume,
};

struct RecoveryCoordinator {
    RecoveryCoordinatorStage stage = RecoveryCoordinatorStage::Idle;
    std::uint32_t frame = 0U;
    std::vector<std::uint8_t> local_state;
    std::vector<std::uint8_t> host_state;
    ProgressBudget deadline{};

    bool active() const { return stage != RecoveryCoordinatorStage::Idle; }
    void reset() {
        stage = RecoveryCoordinatorStage::Idle;
        frame = 0U;
        local_state.clear();
        host_state.clear();
        deadline = {};
    }
};

RecoveryCoordinator g_recovery;

// Gekko detects divergence inside its relative rollback timeline. Do not tear
// down the race on that first signal: park at a future authored-frame boundary,
// install Player 1's portable simulation state, then rebuild rollback history
// from that verified point. A bounded retry budget prevents an irreparably
// nondeterministic scene from entering an endless repair loop.
struct RollbackRepairCoordinator {
    bool requested = false;
    std::uint32_t mismatch_frame = 0U;
    std::uint32_t attempts = 0U;
    std::chrono::steady_clock::time_point window_started{};
    std::chrono::steady_clock::time_point request_deadline{};

    void reset_scene() {
        requested = false;
        mismatch_frame = 0U;
        attempts = 0U;
        window_started = {};
        request_deadline = {};
    }

    void complete_repair() {
        requested = false;
        mismatch_frame = 0U;
        request_deadline = {};
    }
};

RollbackRepairCoordinator g_rollback_repair;

void destroy_rollback_coordinator() {
    g_external_side_effects.store(true, std::memory_order_release);
    g_rollback_advancing.store(false, std::memory_order_release);
    g_presentation_random_scope_depth = 0U;
    if (g_rollback.gekko != nullptr) {
        gekko_destroy(&g_rollback.gekko);
    }
    g_rollback.adapter.deactivate();
    g_rollback.host_authoritative = false;
    g_rollback.states.reset();
    g_rollback.capture.clear();
    g_rollback.restore.clear();
    g_rollback.handles.fill(-1);
    g_rollback.local_handle = -1;
    g_rollback.rollback_count = 0U;
    g_rollback.replayed_frames = 0U;
    g_rollback.largest_rollback = 0U;
    g_rollback.latest_frame = 0U;
    g_rollback.first_frame = 0U;
    g_rollback.frames_ahead = 0.0F;
    g_rollback.digests = {};
}

const RollbackCoordinator::DigestRecord* rollback_digest_for_frame(
    std::int32_t frame) {
    if (frame < -1) return nullptr;
    const std::size_t index = static_cast<std::size_t>(frame + 1) %
                              g_rollback.digests.size();
    const auto& record = g_rollback.digests[index];
    return record.frame == frame ? &record : nullptr;
}

bool host_authority_requested(const RuntimeSessionView& view) {
    return view.running && view.launch_descriptor &&
           g_rollback_gameplay_ready.load(std::memory_order_acquire);
}

void fail_rollback(std::string message);
gpr rdram_address(std::uint32_t address);

bool capture_host_authoritative_snapshot(std::uint8_t* rdram,
                                         std::uint32_t frame) {
    if (rdram == nullptr || !g_rollback.states) return false;
    std::uint64_t checksum = 0U;
    std::string error;
    if (!capture_rollback_simulation_state(
            rdram, 0x00800000U, frame, g_rollback.capture,
            checksum, error) ||
        !g_rollback.states->save(frame, g_rollback.capture, checksum)) {
        fail_rollback(error.empty()
            ? "The host-authoritative rollback snapshot could not be saved."
            : "The host-authoritative rollback snapshot could not be saved: " +
                  error);
        return false;
    }
    return true;
}

bool initialise_host_authoritative_rollback(
    std::uint8_t* rdram, const RuntimeSessionView& view) {
    if (g_rollback.host_authoritative) return true;
    if (!view.launch_descriptor || rdram == nullptr) return false;
    destroy_rollback_coordinator();
    g_rollback.first_frame =
        g_authored_frame.load(std::memory_order_acquire);
    g_rollback.latest_frame = g_rollback.first_frame;
    g_rollback.host_authoritative = true;
    std::fprintf(
        stderr,
        "[netplay][rollback] Player 1 authoritative timeline active "
        "start=%u window=%u delay=%u\n",
        g_rollback.first_frame, view.launch_descriptor->rollback_window,
        view.launch_descriptor->input_delay_frames);
    return true;
}

bool advance_host_authoritative_frame(
    std::uint8_t* rdram, recomp_context* context, std::uint32_t frame,
    const FrameInputs& inputs, bool publish) {
    if (rdram == nullptr || context == nullptr) {
        return false;
    }
    g_rollback_frame_inputs = inputs;
    g_rollback_event_frame = frame;
    g_rollback_authored_base = 0U;
    g_external_side_effects.store(publish, std::memory_order_release);
    g_rollback_advancing.store(true, std::memory_order_release);
    MEM_W(0, rdram_address(revision_addresses::LogicUpdateRate)) =
        kRollbackLogicUpdateRate;
    dkr::runtime::invoke_main_game_loop(rdram, context);
    MEM_W(0, rdram_address(revision_addresses::LogicUpdateRate)) =
        kRollbackLogicUpdateRate;
    // The outer authored thread3 tick owns the simulation-frame cursor.
    // input_update normally advances this cursor from its nested hook, but
    // retail DKR legitimately bypasses input_update on some mode/transition
    // passes.  Relying on that optional hook left the local cursor on `frame`
    // after DirectSession had already committed it, so the following tick
    // requested the same frame again and failed one frame behind Player 1.
    // Seal the cursor at the one canonical post-tick value regardless of
    // which retail branches ran.  This is idempotent on ordinary gameplay
    // frames and keeps both Lockstep and Rollback on the immutable commit
    // ledger without weakening its ordering checks.
    const std::uint32_t next_frame =
        frame == std::numeric_limits<std::uint32_t>::max()
            ? frame
            : frame + 1U;
    const std::uint32_t hook_frame =
        g_authored_frame.load(std::memory_order_acquire);
    if (hook_frame != next_frame) {
        failure_recorder().record(
            FailureEventKind::ProgressWatchdog, frame, hook_frame, next_frame,
            "outer authored tick sealed a bypassed input_update cursor");
    }
    g_authored_frame.store(next_frame, std::memory_order_release);
    g_rollback_advancing.store(false, std::memory_order_release);
    g_external_side_effects.store(true, std::memory_order_release);
    // The active authored pipeline never rewinds native simulation. Retaining
    // a complete portable snapshot after every frame therefore served no
    // consumer, but still copied and delta-compressed the race state on every
    // host and client tick. Exact full-state capture is reserved for the
    // explicit recovery boundary in commit_authoritative_gameplay_frame().
    g_rollback.latest_frame = frame + 1U;
    return true;
}

bool replay_host_authoritative_correction(
    std::uint8_t* rdram, recomp_context* context,
    std::uint32_t correction_frame) {
    if (!g_rollback.host_authoritative || !g_rollback.states ||
        correction_frame < g_rollback.first_frame ||
        correction_frame >= g_rollback.latest_frame) {
        return true;
    }
    std::uint64_t checksum = 0U;
    std::string error;
    if (!g_rollback.states->load(correction_frame, g_rollback.restore,
                                  &checksum) ||
        !restore_rollback_simulation_state(
            rdram, 0x00800000U, g_rollback.restore, correction_frame,
            checksum, error)) {
        fail_rollback(error.empty()
            ? "The corrected Player 1 input frame fell outside rollback history."
            : "The corrected Player 1 input frame could not be restored: " +
                  error);
        return false;
    }

    const std::uint32_t replay_end = g_rollback.latest_frame;
    g_rollback.states->discard_after(correction_frame);
    g_authored_frame.store(correction_frame, std::memory_order_release);
    dkr_presentation_scene_begin(rdram, context);
    const std::uint32_t distance = replay_end - correction_frame;
    ++g_rollback.rollback_count;
    g_rollback.replayed_frames += distance;
    g_rollback.largest_rollback = (std::max)(
        g_rollback.largest_rollback, distance);
    for (std::uint32_t frame = correction_frame;
         frame < replay_end; ++frame) {
        FrameInputs inputs{};
        std::uint16_t revision = 0U;
        if (!g_session.authoritative_inputs_for(frame, inputs, revision) ||
            !advance_host_authoritative_frame(
                rdram, context, frame, inputs, false)) {
            if (g_session.view().state != ConnectionState::Failed) {
                fail_rollback(
                    "A corrected Player 1 input bundle was unavailable during replay.");
            }
            return false;
        }
        (void)revision;
    }
    failure_recorder().record(
        FailureEventKind::RollbackLoad, correction_frame, replay_end,
        distance, "Player 1 input correction replay");
    return true;
}

void fail_rollback(std::string message) {
    failure_recorder().record(
        FailureEventKind::SessionFailure,
        g_authored_frame.load(std::memory_order_acquire),
        g_rollback.rollback_count, g_rollback.replayed_frames, message);
    std::fprintf(stderr, "[netplay][rollback] %s\n", message.c_str());
    g_session.fail_authoritative_state(std::move(message));
    g_rollback_gameplay_ready.store(false, std::memory_order_release);
}

bool initialise_rollback(const RuntimeSessionView& view) {
    if (g_rollback.gekko != nullptr) return true;
    if (!host_authority_requested(view) || !view.launch_descriptor) return false;
    const LaunchDescriptor& descriptor = *view.launch_descriptor;
    if (!g_rollback.adapter.activate(g_session) ||
        !gekko_create(&g_rollback.gekko, GekkoGameSession)) {
        fail_rollback("The rollback engine could not be created safely.");
        return false;
    }

    GekkoConfig config{};
    config.num_players = descriptor.player_count;
    config.max_spectators = 0U;
    config.input_prediction_window = descriptor.rollback_window;
    config.spectator_delay = 0U;
    config.input_size = sizeof(PackedInput);
    config.state_size = sizeof(RollbackToken);
    config.limited_saving = false;
    config.desync_detection = true;
    config.check_distance = (std::max)(15U,
        static_cast<unsigned int>(descriptor.rollback_window) * 2U);
    gekko_start(g_rollback.gekko, &config);
    gekko_net_adapter_set(g_rollback.gekko, g_rollback.adapter.adapter());
    gekko_set_disconnect_timeout(g_rollback.gekko, 15000U);

    for (std::size_t slot = 0U; slot < kMaximumPlayers; ++slot) {
        if (!descriptor.occupied(slot)) continue;
        int handle = -1;
        if (slot == view.local_slot) {
            handle = gekko_add_actor(g_rollback.gekko, GekkoLocalPlayer,
                                     nullptr);
            g_rollback.local_handle = handle;
        } else {
            std::uint8_t address_byte = static_cast<std::uint8_t>(slot);
            GekkoNetAddress address{&address_byte, 1U};
            handle = gekko_add_actor(g_rollback.gekko, GekkoRemotePlayer,
                                     &address);
        }
        if (handle < 0 || handle >= static_cast<int>(kMaximumPlayers)) {
            fail_rollback("The rollback player roster could not be installed.");
            destroy_rollback_coordinator();
            return false;
        }
        g_rollback.handles[static_cast<std::size_t>(handle)] =
            static_cast<int>(slot);
    }
    if (g_rollback.local_handle < 0) {
        fail_rollback("This machine has no rollback player assignment.");
        destroy_rollback_coordinator();
        return false;
    }
    gekko_set_local_delay(g_rollback.gekko, g_rollback.local_handle,
                          descriptor.input_delay_frames);
    gekko_set_runahead(g_rollback.gekko, 0U);

    const std::size_t snapshot_bytes = kRollbackSimulationStateBytes;
    const std::size_t capacity = static_cast<std::size_t>(
        (std::max)(descriptor.rollback_window + 8U, 16U));
    g_rollback.states = std::make_unique<RollbackStateStore>(
        snapshot_bytes, capacity, 4U);
    g_rollback.capture.resize(snapshot_bytes);
    g_rollback.restore.resize(snapshot_bytes);
    std::fprintf(stderr,
                 "[netplay][rollback] active players=%u window=%u delay=%u state=%zu\n",
                 descriptor.player_count, descriptor.rollback_window,
                 descriptor.input_delay_frames, snapshot_bytes);
    return true;
}

gpr rdram_address(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

int current_level_race_type(std::uint8_t* rdram) {
    constexpr std::uint32_t kLevelHeaderRaceTypeOffset = 0x4CU;
    const std::uint32_t header_address = static_cast<std::uint32_t>(
        MEM_W(0, rdram_address(revision_addresses::CurrentLevelHeader)));
    if (header_address < 0x80000000U || header_address >= 0x80800000U) {
        return -1;
    }
    return static_cast<std::int8_t>(MEM_BU(
        kLevelHeaderRaceTypeOffset, rdram_address(header_address)));
}

bool install_and_verify_authoritative_state(
    std::uint8_t* rdram, std::span<const std::uint8_t> snapshot,
    std::uint32_t frame, std::string_view description, std::string& error) {
    std::string detail;
    if (!apply_authoritative_state(rdram, 0x00800000U, snapshot, frame,
                                   detail)) {
        error = std::string(description) +
                " could not be applied safely: " + detail;
        return false;
    }

    std::vector<std::uint8_t> verified;
    if (!capture_authoritative_state(rdram, 0x00800000U, frame, verified,
                                     detail)) {
        error = std::string(description) +
                " could not be verified after installation: " + detail;
        return false;
    }
    if (verified.size() == snapshot.size() &&
        std::equal(verified.begin(), verified.end(), snapshot.begin())) {
        error.clear();
        return true;
    }

    const std::size_t compared = (std::min)(verified.size(), snapshot.size());
    std::size_t mismatch = 0U;
    while (mismatch < compared && verified[mismatch] == snapshot[mismatch]) {
        ++mismatch;
    }
    if (mismatch < compared) {
        char bytes[96]{};
        std::snprintf(bytes, sizeof(bytes),
                      " First mismatch: byte %zu expected %02X but read %02X.",
                      mismatch, static_cast<unsigned>(snapshot[mismatch]),
                      static_cast<unsigned>(verified[mismatch]));
        error = std::string(description) +
                " did not verify exactly after installation." + bytes;
    } else {
        error = std::string(description) +
                " did not verify exactly after installation. Expected " +
                std::to_string(snapshot.size()) + " bytes but captured " +
                std::to_string(verified.size()) + ".";
    }
    return false;
}

bool install_recovery_authoritative_state(
    std::uint8_t* rdram, std::span<const std::uint8_t> snapshot,
    std::uint32_t frame, bool tolerate_actor_lifecycle_lag,
    std::uint32_t& unmatched_actors, std::string& error) {
    unmatched_actors = 0U;
    if (!tolerate_actor_lifecycle_lag) {
        return install_and_verify_authoritative_state(
            rdram, snapshot, frame,
            "Player 1's synchronized recovery state", error);
    }

    std::string detail;
    if (!apply_live_authoritative_state(
            rdram, 0x00800000U, snapshot, frame, unmatched_actors, detail)) {
        error = "Player 1's synchronized recovery state could not be "
                "applied safely: " + detail;
        return false;
    }

    // Rollback guests receive recurring host-authored state corrections after
    // this boundary. A map-backed actor can legitimately be spawned, collected or
    // retired one local tick either side of this parked repair boundary. The
    // relaxed installer validates the complete packet atomically, restores all
    // globals/racers and every actor with a stable local identity, and consumes
    // unmatched actor records without following a stale pointer. Treating that
    // disposable topology race as a fatal session error caused the old
    // water/log/minigame disconnects. Track-start baselines and Lockstep remain
    // byte-exact and therefore retain their stronger compatibility guarantee.
    error.clear();
    return true;
}

SessionPollResult synchronize_live_replica(
    std::uint8_t* rdram, const RuntimeSessionView& view,
    std::uint32_t frame, std::string& error) {
    if (rdram == nullptr || !view.launch_descriptor) {
        error = "The live Player 1 race state has no valid game memory.";
        return SessionPollResult::Failed;
    }
    // Inputs and immutable frame commits still advance at the native 30 Hz.
    // The complete portable race state is only a periodic correction.  On
    // boundaries without a scheduled correction both peers continue the same
    // deterministic simulation; importantly, the guest also avoids issuing a
    // repair request for a snapshot Player 1 intentionally did not publish.
    if (!live_replica_correction_due(frame)) {
        error.clear();
        return SessionPollResult::Ready;
    }
    if (view.host) {
        // The client installs a correction only at its matching authored
        // boundary. Non-sample frames deliberately have no packet and never
        // park either simulation; the immutable input/commit timeline remains
        // the source of every intervening tick.
        if (g_live_replica.published_frame == frame) {
            error.clear();
            return SessionPollResult::Ready;
        }
        if (!capture_authoritative_state(
                rdram, 0x00800000U, frame, g_live_replica.capture, error)) {
            return SessionPollResult::Failed;
        }
        // Authored host frames are immutable once published. The frame number
        // is therefore the exact duplicate key; hashing the whole portable
        // state a second time on every tick only repeated work already covered
        // by the authenticated wire checksum. Retain the diagnostic hash on
        // sparse reliable keyframes where it is actually recorded.
        if (g_live_replica.published_frame != frame) {
            const bool reliable_keyframe =
                frame % kLiveReplicaKeyframeInterval == 0U;
            const std::uint64_t checksum = reliable_keyframe
                ? authoritative_state_checksum(g_live_replica.capture) : 0U;
            if (!g_session.publish_live_replica(
                    frame, g_live_replica.capture, reliable_keyframe, error)) {
                return SessionPollResult::Failed;
            }
            if (reliable_keyframe) {
                failure_recorder().record(
                    FailureEventKind::ReplicaPublished, frame,
                    static_cast<std::uint32_t>(g_live_replica.capture.size()),
                    static_cast<std::uint32_t>(checksum ^ (checksum >> 32U)),
                    "reliable Player 1 live keyframe");
            }
            g_live_replica.published_frame = frame;
            g_live_replica.published_checksum = checksum;
        }
        error.clear();
        return SessionPollResult::Ready;
    }

    if (g_live_replica.installed_frame == frame) {
        error.clear();
        return SessionPollResult::Ready;
    }
    std::uint32_t replica_frame = frame;
    // The portable DKR state contract is a whole-race correction, rather than
    // an independently keyed actor snapshot. Install it only at the client's
    // current scheduled boundary. A lost sample is harmless because a later
    // correction supersedes it without blocking the 30 Hz input timeline.
    const SessionPollResult result = g_session.poll_latest_live_replica(
        frame, frame, replica_frame, g_live_replica.received, error);
    if (result != SessionPollResult::Ready) {
        if (result == SessionPollResult::Pending && frame % 30U == 0U) {
            failure_recorder().record(
                FailureEventKind::ReplicaUnavailable, frame, 0U, 0U,
                "exact Player 1 live sample not available");
        }
        return result;
    }
    std::string detail;
    std::uint32_t unmatched_actors = 0U;
    if (!apply_live_authoritative_state(
            rdram, 0x00800000U, g_live_replica.received, replica_frame,
            unmatched_actors, detail)) {
        // Live replicas are disposable actor-state samples. A topology change
        // can race a final packet from the previous scene; strict track-start
        // and transition checkpoints remain responsible for validation.
        if (!detail.empty() && g_live_replica.discarded_frame != replica_frame) {
            std::fprintf(stderr,
                         "[netplay][replica] discarded frame=%u: %s\n",
                         replica_frame, detail.c_str());
            g_live_replica.discarded_frame = replica_frame;
        }
        error.clear();
        return SessionPollResult::Pending;
    }
    if (unmatched_actors != 0U &&
        g_live_replica.discarded_frame != replica_frame) {
        std::fprintf(stderr,
                     "[netplay][replica] frame=%u deferred-actors=%u\n",
                     replica_frame, unmatched_actors);
        g_live_replica.discarded_frame = replica_frame;
    }
    // Replace local authored state with the state Player 1 actually produced
    // at this exact boundary. The active pipeline never rewinds a native call
    // stack, so this correction must not create the retired rollback-history
    // snapshots here.
    g_rollback.latest_frame = replica_frame;
    g_live_replica.installed_frame = replica_frame;
    if (replica_frame % kLiveReplicaKeyframeInterval == 0U ||
        unmatched_actors != 0U) {
        failure_recorder().record(
            FailureEventKind::ReplicaInstalled, replica_frame,
            unmatched_actors,
            static_cast<std::uint32_t>(g_live_replica.received.size()),
            "Player 1 live state installed");
    }
    error.clear();
    return SessionPollResult::Ready;
}

void synchronize_racer_orientation(std::uint8_t* rdram,
                                   const RuntimeSessionView& view,
                                   std::uint32_t completed_frame) {
    if (rdram == nullptr || !view.launch_descriptor) return;

    std::string error;
    if (!capture_racer_orientation_state(
            rdram, 0x00800000U, completed_frame,
            g_racer_orientation.capture, error)) {
        if (g_racer_orientation.last_reported_failure ==
            std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr,
                         "[netplay][orientation] capture frame=%u: %s\n",
                         completed_frame, error.c_str());
            g_racer_orientation.last_reported_failure = completed_frame;
        }
        return;
    }

    if (view.host) {
        if (!g_session.publish_racer_orientation(
                completed_frame, g_racer_orientation.capture, error) &&
            g_racer_orientation.last_reported_failure ==
                std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr,
                         "[netplay][orientation] publish frame=%u: %s\n",
                         completed_frame, error.c_str());
            g_racer_orientation.last_reported_failure = completed_frame;
        } else if (error.empty()) {
            g_racer_orientation.last_reported_failure =
                std::numeric_limits<std::uint32_t>::max();
        }
        return;
    }

    auto& history = g_racer_orientation.local_history;
    if (!history.empty() && history.back().frame == completed_frame) {
        history.back().state = g_racer_orientation.capture;
    } else {
        history.push_back({completed_frame, g_racer_orientation.capture});
    }
    while (history.size() > 128U) history.pop_front();
    if (history.empty()) return;

    std::uint32_t minimum_frame = history.front().frame;
    if (g_racer_orientation.last_applied_frame !=
        std::numeric_limits<std::uint32_t>::max()) {
        minimum_frame = (std::max)(
            minimum_frame, g_racer_orientation.last_applied_frame + 1U);
    }
    if (minimum_frame > completed_frame) return;

    std::uint32_t sample_frame = minimum_frame;
    const SessionPollResult sample =
        g_session.poll_latest_racer_orientation(
            minimum_frame, completed_frame, sample_frame,
            g_racer_orientation.received, error);
    if (sample != SessionPollResult::Ready) {
        // Orientation traffic is deliberately non-blocking. Input commits,
        // recovery and transition barriers remain the session authority.
        return;
    }
    const auto matching = std::find_if(
        history.begin(), history.end(),
        [sample_frame](const RacerOrientationCoordinator::HistoricalSample& value) {
            return value.frame == sample_frame;
        });
    if (matching == history.end()) return;

    std::uint32_t corrected_racers = 0U;
    if (!apply_racer_orientation_correction(
            rdram, 0x00800000U, g_racer_orientation.received,
            matching->state, sample_frame, corrected_racers, error)) {
        if (g_racer_orientation.last_reported_failure ==
            std::numeric_limits<std::uint32_t>::max()) {
            std::fprintf(stderr,
                         "[netplay][orientation] discard frame=%u: %s\n",
                         sample_frame, error.c_str());
            g_racer_orientation.last_reported_failure = sample_frame;
        }
        return;
    }
    g_racer_orientation.last_applied_frame = sample_frame;
    g_racer_orientation.last_reported_failure =
        std::numeric_limits<std::uint32_t>::max();

    // The current history entry must describe the corrected state. A later
    // host sample for this frame will then measure only new drift instead of
    // reapplying an already-consumed angular difference.
    if (corrected_racers != 0U &&
        capture_racer_orientation_state(
            rdram, 0x00800000U, completed_frame,
            g_racer_orientation.capture, error)) {
        history.back().state = g_racer_orientation.capture;
    }
    while (!history.empty() && history.front().frame <= sample_frame) {
        history.pop_front();
    }
}

SessionPollResult fast_forward_to_live_replica(
    std::uint8_t* rdram, const RuntimeSessionView& view,
    std::uint32_t current_frame, std::uint32_t authoritative_frame,
    std::uint32_t target_debt, std::uint32_t& resumed_frame,
    std::string& error) {
    resumed_frame = current_frame;
    if (rdram == nullptr || view.host || !view.launch_descriptor ||
        view.launch_descriptor->synchronization !=
            SynchronizationMode::Rollback) {
        error.clear();
        return SessionPollResult::Ready;
    }

    std::uint32_t first_replica_frame = 0U;
    std::uint32_t last_replica_frame = 0U;
    if (!replica_fast_forward_window(
            current_frame, authoritative_frame, target_debt,
            first_replica_frame, last_replica_frame)) {
        error.clear();
        return SessionPollResult::Ready;
    }

    std::uint32_t replica_frame = first_replica_frame;
    const SessionPollResult result = g_session.poll_latest_live_replica(
        first_replica_frame, last_replica_frame, replica_frame,
        g_live_replica.received, error);
    if (result == SessionPollResult::Failed) {
        return result;
    }
    if (result == SessionPollResult::Pending) {
        // The ordinary immutable commit stream remains usable while a newer
        // complete state is in flight. Never park gameplay waiting for this
        // optional debt-recovery boundary.
        error.clear();
        return SessionPollResult::Ready;
    }

    std::string detail;
    std::uint32_t unmatched_actors = 0U;
    const auto installed = g_session.install_fast_forward(replica_frame, [&] {
        // Validate the immutable commit chain before changing RDRAM, then
        // commit both state and cursor under the same session admission lock.
        return apply_catch_up_authoritative_state(
            rdram, 0x00800000U, g_live_replica.received, replica_frame, detail);
    }, error);
    if (installed == SessionPollResult::Failed) return installed;
    if (installed == SessionPollResult::Pending) {
        // A disposable state can race an actor spawn/retirement. Keep
        // simulating the immutable ledger and wait for the next complete
        // correction instead of damaging the session cursor.
        if (g_live_replica.discarded_frame != replica_frame) {
            std::fprintf(stderr,
                         "[netplay][replica-catch-up] discarded frame=%u: %s\n",
                         replica_frame, detail.c_str());
            g_live_replica.discarded_frame = replica_frame;
        }
        error.clear();
        return SessionPollResult::Ready;
    }

    g_authored_frame.store(replica_frame, std::memory_order_release);
    if (replica_frame != 0U) {
        g_session.report_simulation_progress(replica_frame - 1U);
    }
    g_rollback.latest_frame = replica_frame;
    g_live_replica.installed_frame = replica_frame;
    resumed_frame = replica_frame;
    failure_recorder().record(
        FailureEventKind::CatchUpBatch, current_frame, replica_frame,
        unmatched_actors,
        "installed Player 1 state instead of hidden native ticks");
    error.clear();
    return SessionPollResult::Ready;
}

std::uint64_t bootstrap_checkpoint_hash(std::uint8_t* rdram,
                                        const LaunchDescriptor& descriptor) {
    // The launch descriptor covers the authoritative roster, input delay,
    // match seed and compatibility manifest.  The canonical state hash adds
    // DKR's authored cold-boot globals while excluding host pointers, display
    // lists, audio queues and other presentation-only memory.
    const std::uint64_t state =
        canonical_gameplay_state_hash(rdram, 0x00800000U);
    std::uint64_t combined = state ^ launch_descriptor_hash(descriptor);
    combined ^= combined >> 30U;
    combined *= 0xBF58476D1CE4E5B9ULL;
    combined ^= combined >> 27U;
    combined *= 0x94D049BB133111EBULL;
    combined ^= combined >> 31U;
    return combined != 0U ? combined : 1U;
}

void halt_failed_simulation() {
    if (g_simulation_halted.exchange(true, std::memory_order_acq_rel)) return;
    // A failed online authority is terminal for networking, but it must not
    // park the only authored game thread: the renderer and in-game overlay are
    // driven from that thread too. Tear down rollback exactly once and let the
    // local presentation continue without online input routing so the failure
    // reason, Restart and Exit controls remain usable.
    g_rollback_gameplay_ready.store(false, std::memory_order_release);
    g_frame_debt_phase.store(FrameDebtPhase::Inactive,
                             std::memory_order_release);
    g_online_wait_state.leave();
    g_last_logged_frame_debt_phase.store(FrameDebtPhase::Inactive,
                                         std::memory_order_release);
    g_rollback_advancing.store(false, std::memory_order_release);
    g_external_side_effects.store(true, std::memory_order_release);
    destroy_rollback_coordinator();
    dkr::runtime::platform::set_online_input_routing(false);
    std::fprintf(stderr,
                 "[netplay][failure] online authority stopped; local UI remains responsive\n");
}

void bootstrap_character_select(std::uint8_t* rdram,
                                const LaunchDescriptor& descriptor) {
    if (rdram == nullptr || !valid_launch_descriptor(descriptor)) return;
    std::array<bool, kOnlineControllerPorts> occupied{};
    std::array<std::int8_t, kDkrCharacterSlots> existing{};
    existing.fill(-1);
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
        occupied[slot] = descriptor.occupied(slot);
    }
    const gpr character_array = rdram_address(
        revision_addresses::PlayersCharacterArray);
    // Never inherit frontend-local character cursors here. A client may have
    // reached this menu from a different local presentation history, and that
    // history is deliberately outside the online determinism contract. Start
    // every occupied network slot from the same descriptor-derived seed.
    const CharacterSelectSeed seed =
        make_character_select_seed(occupied, existing);
    MEM_W(0, rdram_address(revision_addresses::NumberOfActivePlayers)) =
        static_cast<std::int32_t>(seed.active_player_count);
    MEM_W(0, rdram_address(revision_addresses::NumberOfReadyPlayers)) = 0;
    const gpr active_array = rdram_address(
        revision_addresses::ActivePlayersArray);
    const gpr status_array = rdram_address(
        revision_addresses::CharacterSelectStatus);
    const gpr player_id_map = rdram_address(revision_addresses::PlayerIdMap);
    for (std::size_t slot = 0; slot < seed.active_players.size(); ++slot) {
        MEM_B(slot, active_array) = seed.active_players[slot];
        MEM_B(slot, status_array) = 0;
    }
    for (std::size_t slot = 0; slot < seed.characters.size(); ++slot) {
        MEM_B(slot, character_array) = seed.characters[slot];
    }
    for (std::size_t slot = 0; slot < seed.player_ids.size(); ++slot) {
        MEM_B(slot, player_id_map) = seed.player_ids[slot];
    }
}

void lock_character_select_membership(std::uint8_t* rdram,
                                      const LaunchDescriptor& descriptor) {
    if (rdram == nullptr || !valid_launch_descriptor(descriptor)) return;
    const gpr buttons = rdram_address(revision_addresses::MenuButtons);
    const gpr status = rdram_address(revision_addresses::CharacterSelectStatus);
    for (std::size_t slot = 0; slot < kOnlineControllerPorts; ++slot) {
        const std::uint32_t current = static_cast<std::uint32_t>(
            MEM_W(slot * sizeof(std::uint32_t), buttons));
        const std::int8_t current_status = static_cast<std::int8_t>(
            MEM_B(slot, status));
        MEM_W(slot * sizeof(std::uint32_t), buttons) =
            static_cast<std::int32_t>(locked_character_select_buttons(
                descriptor.occupied(slot), current_status, current));
    }
}

void refresh_input_vehicle_context(std::uint8_t* rdram,
                                   const RuntimeSessionView& view) {
    if (rdram == nullptr) return;
    const gpr vehicles = rdram_address(
        revision_addresses::PlayerSelectVehicle);
    std::array<int, kOnlineControllerPorts> resolved_vehicles{};
    for (std::size_t slot = 0U; slot < kOnlineControllerPorts; ++slot) {
        const int menu_vehicle = static_cast<std::int8_t>(MEM_B(slot, vehicles));
        const int vehicle = dkr::runtime::vehicle_context::resolve_for_port(
            rdram, dkr::runtime::vehicle_context::kRdramBytes,
            revision_addresses::RacersByPort, slot, menu_vehicle);
        resolved_vehicles[slot] = vehicle;
        dkr::runtime::input::set_active_vehicle(slot, vehicle);
    }
    // Online play may route a lobby slot through any local control profile.
    // Apply that racer's vehicle policy to the selected physical profile before
    // its SDL state becomes an authored packet.
    if (view.active && view.local_slot < kOnlineControllerPorts) {
        dkr::runtime::input::set_active_vehicle(
            dkr::runtime::platform::online_input_profile(),
            resolved_vehicles[view.local_slot]);
    }
}

gpr call_payload_query(RecompiledEntrypoint function, std::uint8_t* rdram,
                       const recomp_context& source) {
    if (function == nullptr || rdram == nullptr) return 0;
    recomp_context call = source;
    function(rdram, &call);
    return call.r2;
}

gpr call_payload_query_with_argument(RecompiledEntrypoint function,
                                     std::uint8_t* rdram,
                                     const recomp_context& source,
                                     gpr argument) {
    if (function == nullptr || rdram == nullptr) return 0;
    recomp_context call = source;
    call.r4 = argument;
    function(rdram, &call);
    return call.r2;
}

PackedInput poll_authored_local_input(const RuntimeSessionView& view) {
    if (view.launch_descriptor) {
        dkr::runtime::platform::set_online_input_routing(
            true, view.launch_descriptor->occupied_mask, view.local_slot);
    }
    dkr::runtime::platform::poll_input();
    std::uint16_t buttons = 0U;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    const int profile = static_cast<int>(
        dkr::runtime::platform::online_input_profile());
    dkr::runtime::platform::get_physical_input(
        profile, &buttons, &stick_x, &stick_y);
    PackedInput local{buttons, pack_input_axis(stick_x),
                      pack_input_axis(stick_y)};
    if (view.local_slot != 0U &&
        view.host_control != HostControlPolicy::EveryAssignedPort &&
        !g_assigned_ports_released.load(std::memory_order_acquire)) {
        local = {};
    }
    return local;
}

TwoPlayerAdventurePhase observe_two_player_adventure_phase(
    std::uint8_t* rdram, const recomp_context* context,
    const RuntimeSessionView& view) {
    const FrameDebtPhase debt_phase =
        g_frame_debt_phase.load(std::memory_order_acquire);
    const bool ports_released =
        g_assigned_ports_released.load(std::memory_order_acquire);
    bool adventure = false;
    bool player_two_is_lead = false;
    if (view.running && ports_released && context != nullptr &&
        (debt_phase == FrameDebtPhase::Frontend ||
         debt_phase == FrameDebtPhase::PostRace)) {
        const GamePayload* payload = active_payload();
        if (payload != nullptr &&
            payload->is_in_two_player_adventure != nullptr &&
            payload->is_player_two_in_control != nullptr) {
            adventure = call_payload_query(
                payload->is_in_two_player_adventure, rdram, *context) != 0;
            if (adventure) {
                player_two_is_lead = call_payload_query(
                    payload->is_player_two_in_control, rdram, *context) != 0;
            }
        }
    }
    return two_player_adventure_phase(
        view.running, ports_released, adventure, player_two_is_lead,
        debt_phase);
}

} // namespace

DirectSession& session() { return active_session(); }

void bind_external_session(DirectSession* external_session) {
    g_external_session = external_session;
}

void reset_runtime_state() {
    destroy_rollback_coordinator();
    g_live_replica.reset();
    g_racer_orientation.reset();
    g_gameplay_start.reset();
    g_finish.reset();
    g_recovery.reset();
    g_rollback_repair.reset_scene();
    g_rollback_gameplay_ready.store(false, std::memory_order_release);
    g_runtime_state.reset();
    g_authored_frame.store(0U, std::memory_order_release);
    g_authored_input_epoch.store(0U, std::memory_order_release);
    g_prepared_authored_frame.store(kNoPreparedAuthoredFrame,
                                    std::memory_order_release);
    reset_client_catch_up_pacing();
    g_frame_debt_phase.store(FrameDebtPhase::Inactive,
                             std::memory_order_release);
    g_online_wait_state.leave();
    g_last_logged_frame_debt_phase.store(FrameDebtPhase::Inactive,
                                         std::memory_order_release);
    g_two_player_adventure_phase.store(
        TwoPlayerAdventurePhase::GuidedMenus, std::memory_order_release);
    g_loaded_announced.store(false, std::memory_order_release);
    g_assigned_ports_released.store(false, std::memory_order_release);
    g_simulation_halted.store(false, std::memory_order_release);
    g_skip_next_si_physical_poll.store(false, std::memory_order_release);
    g_first_determinism_hash_frame.store(kDeterminismNotArmed,
                                         std::memory_order_release);
    // A lobby launch descriptor exists before the recompiled entrypoint runs.
    // Preserve that immutable topology so DKR's subsequent osContInit sees
    // the same virtual controller bitpattern on every machine.
    if (const auto descriptor = g_session.launch_descriptor();
        descriptor && g_session.active()) {
        const SessionView view = g_session.view();
        dkr::runtime::platform::set_online_input_routing(
            true, descriptor->occupied_mask, view.local_slot);
    } else {
        dkr::runtime::platform::set_online_input_routing(false);
    }
    online_input_broker().begin_epoch();
}

void prepare_controller_init() {
    const auto descriptor = g_session.launch_descriptor();
    if (!descriptor || !g_session.active()) {
        dkr::runtime::platform::set_online_input_routing(false);
        return;
    }
    const SessionView view = g_session.view();
    dkr::runtime::platform::set_online_input_routing(
        true, descriptor->occupied_mask, view.local_slot);
}

void register_runtime_context(std::uint8_t*, recomp_context* context) {
    if (!g_runtime_state.register_context(context)) {
        std::fprintf(stderr,
                     "[netplay][state] context registry is full; rollback remains disabled\n");
    }
}

void unregister_runtime_context(std::uint8_t*, recomp_context* context) {
    g_runtime_state.unregister_context(context);
}

void on_frame_boundary(std::uint8_t* rdram, recomp_context* context) {
    register_runtime_context(rdram, context);
}

void resolve_authored_input_frame(std::uint8_t* rdram,
                                  recomp_context* context) {
    register_runtime_context(rdram, context);
    RuntimeSessionView boundary_view = g_session.runtime_view();
    g_authored_input_epoch.store(boundary_view.input_epoch,
                                 std::memory_order_release);
    if (boundary_view.running &&
        g_frame_debt_phase.load(std::memory_order_acquire) ==
            FrameDebtPhase::Inactive) {
        g_frame_debt_phase.store(FrameDebtPhase::Frontend,
                                 std::memory_order_release);
    }
    refresh_input_vehicle_context(rdram, boundary_view);
    if (g_rollback_advancing.load(std::memory_order_acquire)) {
        online_input_broker().publish(g_rollback_event_frame,
                                      InputFrameSource::Rollback,
                                      g_rollback_frame_inputs);
        g_authored_frame.store(g_rollback_authored_base +
                                   g_rollback_event_frame + 1U,
                               std::memory_order_release);
        return;
    }
    if (boundary_view.state == ConnectionState::Failed) {
        halt_failed_simulation();
        return;
    }
    if (!boundary_view.active) {
        dkr::runtime::platform::set_online_input_routing(false);
        return;
    }

    if (const auto& descriptor = boundary_view.launch_descriptor) {
        dkr::runtime::platform::set_online_input_routing(
            true, descriptor->occupied_mask, boundary_view.local_slot);
    }

    if (!g_loaded_announced.exchange(true, std::memory_order_acq_rel)) {
        const auto& descriptor = boundary_view.launch_descriptor;
        if (!descriptor || !valid_launch_descriptor(*descriptor)) {
            std::fprintf(stderr,
                         "[netplay][start] launch descriptor is unavailable\n");
            return;
        }
        const auto active_save =
            dkr::runtime::saves::runtime_online_save_status();
        if (!active_save.active || !active_save.verified) {
            g_session.fail_runtime_start(
                "The isolated online save was not active at the cold-boot checkpoint.");
            return;
        }
        g_session.mark_game_loaded(
            bootstrap_checkpoint_hash(rdram, *descriptor),
            boundary_view.online_save_generation, active_save.hash);
        g_online_wait_state.enter(OnlineWaitReason::Loading);
        if (!g_session.wait_until_running(std::chrono::seconds(90))) {
            boundary_view = g_session.runtime_view();
            std::fprintf(stderr, "[netplay][start] %s\n",
                         boundary_view.status.c_str());
            if (boundary_view.state == ConnectionState::Failed) {
                halt_failed_simulation();
            }
            return;
        }
    }
    boundary_view = g_session.runtime_view();
    if (boundary_view.running) g_online_wait_state.leave();
    if (!boundary_view.running) return;

    const std::uint32_t frame = g_authored_frame.load(
        std::memory_order_acquire);
    const bool prepared = g_prepared_authored_frame.exchange(
        kNoPreparedAuthoredFrame, std::memory_order_acq_rel) == frame;
    if (!prepared) {
        // The initial launch boundary can reach input_update before the
        // running session is visible to thread3_main. Normal frontend ticks
        // are pre-admitted by drive_authored_tick and therefore never poll or
        // wait twice here.
        (void)poll_authored_local_input(boundary_view);
        g_skip_next_si_physical_poll.store(true, std::memory_order_release);
    }

    // DKR's native swap_lead_player/input_swap_id path selects whether stable
    // controller slot 0 or 1 drives JOINTVENTURE's one visible hub racer.
    // Observe that ownership without adding a second netplay remap or epoch.
    g_two_player_adventure_phase.store(
        observe_two_player_adventure_phase(rdram, context, boundary_view),
        std::memory_order_release);

    std::uint16_t buttons = 0U;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    const int local_profile = static_cast<int>(
        dkr::runtime::platform::online_input_profile());
    dkr::runtime::platform::get_physical_input(
        local_profile, &buttons, &stick_x, &stick_y);
    PackedInput local{buttons, pack_input_axis(stick_x),
                      pack_input_axis(stick_y)};
    if (boundary_view.local_slot != 0U &&
        boundary_view.host_control != HostControlPolicy::EveryAssignedPort &&
        !g_assigned_ports_released.load(std::memory_order_acquire)) {
        local = {};
    }
    FrameInputs synchronized{};
    if (g_session.gameplay_handoff_suspended(frame)) {
        // The selected track is being constructed asynchronously on each
        // machine. Keep retail SI reads deterministic and neutral without
        // consuming an authored frame until every racer installs Player 1's
        // baseline and opens the next authenticated input epoch.
        online_input_broker().publish(
            frame, InputFrameSource::FrontendLockstep, FrameInputs{});
        return;
    }
    if (!prepared) {
        // Cold boot may precede outer-loop admission. Keep this required wait
        // visible without allowing SI to consume an uncommitted input frame.
        g_online_wait_state.enter(OnlineWaitReason::Transition);
    }
    const InputSynchronizationResult input_result =
        g_session.synchronize_inputs_result(
            frame, local, synchronized,
            prepared ? std::chrono::milliseconds(0)
                     : std::chrono::seconds(90));
    if (input_result == InputSynchronizationResult::AlreadyCommitted) {
        // This is a redundant retail input read after the authored outer tick
        // already simulated `frame`. Re-publish the immutable consumed pads so
        // SI sees the same values, but do not hash or simulate the frame twice.
        const bool gameplay_lockstep =
            boundary_view.launch_descriptor &&
            boundary_view.launch_descriptor->synchronization ==
                SynchronizationMode::Lockstep &&
            g_first_determinism_hash_frame.load(std::memory_order_acquire) !=
                kDeterminismNotArmed;
        online_input_broker().publish(
            frame,
            gameplay_lockstep ? InputFrameSource::GameplayLockstep
                              : InputFrameSource::FrontendLockstep,
            synchronized);
        const std::uint32_t next_frame =
            frame == std::numeric_limits<std::uint32_t>::max()
                ? frame
                : frame + 1U;
        g_authored_frame.store(next_frame, std::memory_order_release);
        return;
    }
    if (input_result == InputSynchronizationResult::Suspended) {
        online_input_broker().publish(
            frame, InputFrameSource::FrontendLockstep, FrameInputs{});
        return;
    }
    if (input_result == InputSynchronizationResult::Failed) {
        boundary_view = g_session.runtime_view();
        std::fprintf(stderr, "[netplay][input] frame=%u %s\n", frame,
                     boundary_view.status.c_str());
        if (boundary_view.state == ConnectionState::Failed) {
            halt_failed_simulation();
        }
        return;
    }
    const bool gameplay_lockstep =
        boundary_view.launch_descriptor &&
        boundary_view.launch_descriptor->synchronization ==
            SynchronizationMode::Lockstep &&
        g_first_determinism_hash_frame.load(std::memory_order_acquire) !=
            kDeterminismNotArmed;
    online_input_broker().publish(
        frame,
        gameplay_lockstep ? InputFrameSource::GameplayLockstep
                          : InputFrameSource::FrontendLockstep,
        synchronized);
    const std::uint32_t first_hash_frame =
        g_first_determinism_hash_frame.load(std::memory_order_acquire);
    if (rdram != nullptr &&
        should_submit_determinism_hash(frame, first_hash_frame)) {
        const GameplayStateDigest digest =
            canonical_gameplay_state_digest(rdram, 0x00800000U);
        g_session.submit_state_digest(frame, digest.combined, digest.globals,
                                      digest.roster, digest.racers,
                                      digest.racer_count,
                                      digest.racer_details);
    }
    g_authored_frame.store(frame + 1U, std::memory_order_release);
}

void release_assigned_ports() {
    g_assigned_ports_released.store(true, std::memory_order_release);
}

void enter_character_select(std::uint8_t* rdram) {
    g_live_replica.reset();
    g_racer_orientation.reset();
    if (const auto descriptor = g_session.launch_descriptor()) {
        bootstrap_character_select(rdram, *descriptor);
        if (g_session.running() && rdram != nullptr) {
            // Verify the authored frontend roster immediately after every
            // peer has installed the same descriptor-derived character seed.
            g_session.submit_state_hash(
                g_authored_frame.load(std::memory_order_acquire),
                canonical_frontend_state_hash(rdram, 0x00800000U));
        }
    }
    // Character select owns asynchronous music, model and menu presentation.
    // It is synchronized by authored inputs, but is deliberately not part of
    // the gameplay determinism contract.
    g_first_determinism_hash_frame.store(kDeterminismNotArmed,
                                         std::memory_order_release);
    g_session.end_authoritative_phase();
    g_frame_debt_phase.store(FrameDebtPhase::Frontend,
                             std::memory_order_release);
    release_assigned_ports();
}

void enforce_character_select_roster(std::uint8_t* rdram) {
    if (const auto descriptor = g_session.launch_descriptor()) {
        lock_character_select_membership(rdram, *descriptor);
    }
}

void seed_character_select_ai(std::uint8_t* rdram,
                              recomp_context* context) {
    const auto descriptor = g_session.launch_descriptor();
    if (rdram == nullptr || context == nullptr || !descriptor ||
        !g_session.running() || !valid_launch_descriptor(*descriptor)) {
        return;
    }
    // charselect_assign_ai is the sole retail point that randomizes the CPU
    // roster. Frontend animation/audio can consume rand_range differently on
    // each machine, so reset the authored stream at this exact boundary.
    std::uint64_t mixed = descriptor->match_id ^
                          descriptor->compatibility_hash ^
                          0x435055524F535445ULL;
    mixed ^= static_cast<std::uint64_t>(
                 static_cast<std::uint32_t>(context->r4)) *
             0x9E3779B97F4A7C15ULL;
    mixed ^= mixed >> 30U;
    mixed *= 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 27U;
    mixed *= 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31U;
    std::uint32_t seed = static_cast<std::uint32_t>(mixed ^ (mixed >> 32U));
    if (seed == 0U) seed = 0x43505552U;
    MEM_W(0, rdram_address(revision_addresses::CurrentRngSeed)) =
        static_cast<std::int32_t>(seed);
    MEM_W(0, rdram_address(revision_addresses::PreviousRngSeed)) =
        static_cast<std::int32_t>(seed);
}

std::uint32_t authored_frame() {
    return g_authored_frame.load(std::memory_order_acquire);
}

void begin_gameplay_level(std::uint8_t* rdram, recomp_context* context) {
    g_live_replica.reset();
    g_racer_orientation.reset();
    g_gameplay_start.reset();
    g_finish.reset();
    g_recovery.reset();
    g_rollback_repair.reset_scene();
    g_rollback_gameplay_ready.store(false, std::memory_order_release);
    g_frame_debt_phase.store(FrameDebtPhase::LoadingBarrier,
                             std::memory_order_release);
    g_first_determinism_hash_frame.store(kDeterminismNotArmed,
                                         std::memory_order_release);
    const auto descriptor = g_session.launch_descriptor();
    if (rdram == nullptr || context == nullptr || !descriptor ||
        !valid_launch_descriptor(*descriptor)) {
        g_session.end_authoritative_phase();
        g_frame_debt_phase.store(FrameDebtPhase::Frontend,
                                 std::memory_order_release);
        online_input_broker().begin_epoch();
        return;
    }

    // level_load_game's first two MIPS arguments are the chosen map and the
    // retail zero-based local-player count. Derive one peer-independent seed
    // before any track objects or racers are constructed, so frontend RNG use
    // cannot leak into the online simulation.
    const std::uint32_t level_id = static_cast<std::uint32_t>(context->r4);
    g_gameplay_start.requested_map = level_id;
    const GamePayload* payload = active_payload();
    if (payload != nullptr && payload->leveltable_type != nullptr) {
        g_gameplay_start.requested_race_type = static_cast<int>(
            static_cast<std::int32_t>(call_payload_query_with_argument(
                payload->leveltable_type, rdram, *context,
                static_cast<gpr>(level_id))));
    }
    if (g_session.running()) {
        std::string handoff_error;
        if (!g_session.begin_gameplay_handoff(
                g_authored_frame.load(std::memory_order_acquire), level_id,
                handoff_error)) {
            g_session.fail_authoritative_state(
                handoff_error.empty()
                    ? "The selected track could not enter the synchronized input handoff."
                    : std::move(handoff_error));
            halt_failed_simulation();
            return;
        }
    }
    g_session.end_authoritative_phase();
    online_input_broker().begin_epoch();
    std::uint64_t mixed = descriptor->match_id ^ descriptor->compatibility_hash;
    mixed ^= static_cast<std::uint64_t>(level_id) * 0x9E3779B97F4A7C15ULL;
    mixed ^= mixed >> 30U;
    mixed *= 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 27U;
    mixed *= 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31U;
    std::uint32_t seed = static_cast<std::uint32_t>(mixed ^ (mixed >> 32U));
    if (seed == 0U) seed = 0x444B5252U; // "DKRR"
    MEM_W(0, rdram_address(revision_addresses::CurrentRngSeed)) =
        static_cast<std::int32_t>(seed);
    MEM_W(0, rdram_address(revision_addresses::PreviousRngSeed)) =
        static_cast<std::int32_t>(seed);
}

void complete_gameplay_level(std::uint8_t* rdram, recomp_context* context) {
    // A rollback replay may revisit authored hooks while rebuilding history.
    // Session lifecycle and barriers belong only to the one published advance;
    // replaying them would deadlock inside Gekko's update and retire the live
    // scene from underneath the coordinator.
    if (!external_side_effects_allowed()) return;
    const auto descriptor = g_session.launch_descriptor();
    if (rdram == nullptr || !descriptor || !valid_launch_descriptor(*descriptor)) {
        return;
    }
    const std::uint32_t active_players = static_cast<std::uint32_t>(
        MEM_W(0, rdram_address(revision_addresses::NumberOfActivePlayers)));
    const std::uint32_t racer_count = static_cast<std::uint32_t>(
        MEM_W(0, rdram_address(revision_addresses::NumberOfRacers)));
    const int race_type = current_level_race_type(rdram);
    const bool ordinary_gameplay_ready = determinism_race_ready(
        active_players, racer_count, descriptor->player_count);
    bool two_player_adventure = false;
    if (!ordinary_gameplay_ready && context != nullptr) {
        const GamePayload* payload = active_payload();
        if (payload != nullptr &&
            payload->is_in_two_player_adventure != nullptr) {
            two_player_adventure = call_payload_query(
                payload->is_in_two_player_adventure, rdram, *context) != 0;
        }
    }
    const bool established_shared_hub_ready =
        two_player_adventure_shared_hub_topology_ready(
            g_session.running(),
            g_assigned_ports_released.load(std::memory_order_acquire),
            two_player_adventure,
            g_gameplay_start.requested_race_type.value_or(-1), race_type,
            active_players, racer_count,
            descriptor->player_count);
    const bool dual_adventure_gameplay_ready =
        two_player_adventure_dual_gameplay_topology_ready(
            g_session.running(),
            g_assigned_ports_released.load(std::memory_order_acquire),
            two_player_adventure,
            g_gameplay_start.requested_race_type.value_or(-1), race_type,
            active_players, racer_count,
            descriptor->player_count);
    bool verified_boss_introduction = false;
    if (two_player_adventure && context != nullptr &&
        g_gameplay_start.requested_race_type == kBossRaceType &&
        race_type == kHubWorldRaceType && g_gameplay_start.requested_map) {
        const GamePayload* payload = active_payload();
        if (payload != nullptr && payload->get_misc_asset != nullptr) {
            // get_misc_asset is a read-only lookup of the already loaded
            // table. Use a copied MIPS context, exactly as other queries do.
            const auto address = static_cast<std::uint32_t>(
                call_payload_query_with_argument(payload->get_misc_asset,
                    rdram, *context, 67));
            if (address >= 0x80000000U && address <= 0x807FFFECU) {
                std::array<std::uint8_t, 20U> pairs{};
                for (std::size_t i = 0; i < pairs.size(); ++i) {
                    pairs[i] = static_cast<std::uint8_t>(
                        MEM_BU(i, rdram_address(address)));
                }
                verified_boss_introduction = boss_introduction_redirect_matches(
                    pairs, *g_gameplay_start.requested_map,
                    static_cast<std::uint32_t>(MEM_W(0,
                        rdram_address(revision_addresses::CurrentMapId))));
            }
        }
    }
    const bool boss_ready = two_player_adventure_boss_topology_ready(
        g_session.running(),
        g_assigned_ports_released.load(std::memory_order_acquire),
        two_player_adventure,
        g_gameplay_start.requested_race_type.value_or(-1), race_type,
        active_players, racer_count,
        descriptor->player_count, verified_boss_introduction);
    const bool jointventure_selected =
        magic_codes::magic_code_enabled(magic_codes::launch_mask(), 24U);
    const bool bootstrap_shared_hub_ready =
        !two_player_adventure &&
        two_player_adventure_bootstrap_hub_topology_ready(
            g_session.running(),
            g_assigned_ports_released.load(std::memory_order_acquire),
            jointventure_selected,
            g_gameplay_start.requested_race_type.value_or(-1), race_type,
            active_players, racer_count,
            descriptor->player_count);
    const bool shared_hub_ready = established_shared_hub_ready ||
                                  bootstrap_shared_hub_ready;
    const bool empty_cutscene_ready =
        two_player_adventure_empty_cutscene_ready(
            g_session.running(),
            g_assigned_ports_released.load(std::memory_order_acquire),
            two_player_adventure || jointventure_selected,
            g_gameplay_start.requested_race_type.value_or(-1), race_type,
            active_players, racer_count, descriptor->player_count);
    std::fprintf(stderr,
                 "[netplay][level-ready] frame=%u requested=%u resolved=%u requested_type=%d resolved_type=%d racers=%u empty_cutscene=%d boss_redirect=%d\n",
                 authored_frame(), g_gameplay_start.requested_map.value_or(~0U),
                 static_cast<std::uint32_t>(MEM_W(0, rdram_address(revision_addresses::CurrentMapId))),
                 g_gameplay_start.requested_race_type.value_or(-1), race_type,
                 racer_count, empty_cutscene_ready ? 1 : 0,
                 verified_boss_introduction ? 1 : 0);
    if (!ordinary_gameplay_ready && !shared_hub_ready &&
        !dual_adventure_gameplay_ready && !boss_ready && !empty_cutscene_ready) {
        std::fprintf(stderr,
                     "[netplay][determinism] gameplay validation remains disarmed: race_type=%d players=%u expected=%u racers=%u\n",
                     race_type, active_players, descriptor->player_count,
                     racer_count);
        if (g_session.running()) {
            g_session.fail_authoritative_state(
                "The loaded scene has an unsupported online topology (type=" +
                std::to_string(race_type) + ", racers=" +
                std::to_string(racer_count) + ", requested map=" +
                std::to_string(g_gameplay_start.requested_map.value_or(~0U)) +
                ", requested type=" + std::to_string(
                    g_gameplay_start.requested_race_type.value_or(-1)) +
                ", resolved map=" + std::to_string(static_cast<std::uint32_t>(
                    MEM_W(0, rdram_address(revision_addresses::CurrentMapId)))) +
                ", active players=" + std::to_string(active_players) + ").");
            halt_failed_simulation();
        }
        return;
    }
    if (shared_hub_ready) {
        std::fprintf(stderr,
                     "[netplay][jointventure] synchronized shared hub ready: players=%u assigned=%u racers=%u source=%s\n",
                     active_players, descriptor->player_count, racer_count,
                     bootstrap_shared_hub_ready ? "fresh-save bootstrap"
                                                : "retail state");
    }
    if (dual_adventure_gameplay_ready) {
        std::fprintf(stderr,
                     "[netplay][jointventure] synchronized dual-control race ready: race_type=%d players=%u assigned=%u racers=%u\n",
                     race_type, active_players, descriptor->player_count,
                     racer_count);
    }
    if (boss_ready) {
        std::fprintf(stderr,
                     "[netplay][jointventure] synchronized single-viewport boss scene ready: race_type=%d players=%u assigned=%u racers=%u\n",
                     race_type, active_players, descriptor->player_count,
                     racer_count);
    }
    // level_load_game has now created the racer roster and track simulation.
    // Begin a non-blocking barrier. The authored thread must return from this
    // hook immediately; sleeping here previously starved the renderer/audio
    // and caused both synchronization modes to black-screen at race start.
    g_session.begin_authoritative_phase();
    g_frame_debt_phase.store(FrameDebtPhase::GameplayStartBarrier,
                             std::memory_order_release);
    const std::uint32_t resolved_map = static_cast<std::uint32_t>(
        MEM_W(0, rdram_address(revision_addresses::CurrentMapId)));
    constexpr std::uint32_t kGameplayBaselineFrame = 0U;
    std::string error;
    const std::optional<std::uint32_t> requested_map =
        g_gameplay_start.requested_map;
    const std::optional<int> requested_race_type =
        g_gameplay_start.requested_race_type;
    if (!requested_map) {
        g_session.fail_authoritative_state(
            "The synchronized track-start gate lost its requested destination.");
        halt_failed_simulation();
        return;
    }
    g_gameplay_start.reset();
    g_finish.reset();
    g_gameplay_start.requested_map = requested_map;
    g_gameplay_start.requested_race_type = requested_race_type;
    g_gameplay_start.resolved_map = resolved_map;
    g_gameplay_start.racer_count = racer_count;
    g_gameplay_start.empty_cutscene = empty_cutscene_ready;
    g_gameplay_start.deadline.start(g_session.synchronization_progress());
    if (!capture_authoritative_state(rdram, 0x00800000U,
                                     kGameplayBaselineFrame,
                                     g_gameplay_start.local_baseline,
                                     error)) {
        g_gameplay_start.reset();
        g_session.fail_authoritative_state(
            "The local track baseline could not be captured safely: " + error);
        halt_failed_simulation();
        return;
    }
    g_gameplay_start.stage = GameplayStartStage::AwaitingPeers;
}

bool service_gameplay_start(std::uint8_t* rdram) {
    if (!g_gameplay_start.active()) return false;
    auto fail = [](std::string message) {
        g_gameplay_start.reset();
        if (g_session.view().state != ConnectionState::Failed) {
            g_session.fail_authoritative_state(std::move(message));
        }
        halt_failed_simulation();
    };
    if (rdram == nullptr) {
        fail("The synchronized track-start gate lost the game state.");
        return true;
    }
    if (g_gameplay_start.deadline.expired(g_session.synchronization_progress())) {
        fail("The synchronized track-start barrier timed out. The game was "
             "released from its parked online state safely.");
        return true;
    }
    const SessionView view = g_session.view();
    if (view.state == ConnectionState::Failed || !g_session.running()) {
        fail(view.status.empty()
            ? "The online session ended during track synchronization."
            : view.status);
        return true;
    }

    constexpr std::uint32_t kGameplayBaselineFrame = 0U;
    std::string error;
    switch (g_gameplay_start.stage) {
    case GameplayStartStage::AwaitingPeers: {
        const SessionPollResult result = g_session.poll_gameplay_ready(
            *g_gameplay_start.requested_map,
            g_gameplay_start.resolved_map,
            g_gameplay_start.racer_count, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty() ? "The synchronized track-start gate failed."
                               : error);
        } else if (result == SessionPollResult::Ready) {
            if (view.host) {
                // The capture was taken at the level-ready hook, while the
                // barrier is serviced from a later authored boundary. Always
                // reinstall it before publication so Player 1 is running the
                // exact same bytes that every client will acknowledge.
                if (!install_and_verify_authoritative_state(
                        rdram, g_gameplay_start.local_baseline,
                        kGameplayBaselineFrame,
                        "Player 1's local track baseline", error)) {
                    fail(error);
                    return true;
                }
                if (!g_session.publish_authoritative_state(
                        kGameplayBaselineFrame,
                        g_gameplay_start.local_baseline, error)) {
                    fail(error.empty()
                        ? "Player 1 could not publish the track baseline."
                        : error);
                } else {
                    g_gameplay_start.stage =
                        GameplayStartStage::AwaitingAcknowledgements;
                }
            } else {
                g_gameplay_start.stage =
                    GameplayStartStage::AwaitingBaseline;
            }
        }
        return true;
    }
    case GameplayStartStage::AwaitingBaseline: {
        const SessionPollResult result = g_session.poll_authoritative_state(
            kGameplayBaselineFrame, g_gameplay_start.host_baseline, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty()
                ? "Player 1's track baseline did not arrive."
                : error);
            return true;
        }
        if (result == SessionPollResult::Pending) return true;
        const bool corrected =
            g_gameplay_start.local_baseline !=
            g_gameplay_start.host_baseline;
        // Equality with the earlier local capture is not proof that live
        // RDRAM still matches it. Install Player 1's baseline unconditionally
        // before acknowledging the race epoch.
        if (!install_and_verify_authoritative_state(
                rdram, g_gameplay_start.host_baseline,
                kGameplayBaselineFrame, "Player 1's track baseline", error)) {
            fail(error);
            return true;
        }
        g_session.confirm_authoritative_state(kGameplayBaselineFrame,
                                              corrected);
        g_gameplay_start.stage = GameplayStartStage::AwaitingGo;
        return true;
    }
    case GameplayStartStage::AwaitingAcknowledgements: {
        const SessionPollResult result =
            g_session.poll_authoritative_acknowledgements(
                kGameplayBaselineFrame, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty()
                ? "A racer did not accept Player 1's track baseline."
                : error);
        } else if (result == SessionPollResult::Ready) {
            g_gameplay_start.stage = GameplayStartStage::AwaitingGo;
        }
        return true;
    }
    case GameplayStartStage::AwaitingGo: {
        const SessionPollResult result = g_session.poll_gameplay_resume(
            g_gameplay_start.resolved_map,
            g_gameplay_start.racer_count, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty() ? "The synchronized track did not resume."
                               : error);
            return true;
        }
        if (result == SessionPollResult::Pending) return true;
        const auto descriptor = g_session.launch_descriptor();
        if (!descriptor || !valid_launch_descriptor(*descriptor)) {
            fail("The synchronized track lost its launch descriptor.");
            return true;
        }
        std::uint32_t resume_frame = 0U;
        if (!g_session.complete_gameplay_handoff(resume_frame, error)) {
            fail(error.empty()
                ? "The synchronized track input epoch did not resume."
                : error);
            return true;
        }
        g_authored_frame.store(resume_frame, std::memory_order_release);
        const SessionView resumed_view = g_session.view();
        g_authored_input_epoch.store(resumed_view.input_epoch,
                                     std::memory_order_release);
        online_input_broker().begin_epoch();
        if (g_gameplay_start.empty_cutscene) {
            // Reuse the verified baseline + Arm/Go exchange, then release the
            // non-racer scene on the fixed-step frontend ledger. Never leave
            // LoadingBarrier active while its cutscene runs with neutral SI
            // reads and a frozen authored cursor. Each subsequent load gets
            // its own scene/input epoch, even if it requests the same hub.
            g_session.end_authoritative_phase();
            g_frame_debt_phase.store(FrameDebtPhase::Frontend,
                                     std::memory_order_release);
            std::fprintf(stderr,
                         "[netplay][cutscene] resumed frame=%u input_epoch=%u map=%u\n",
                         resume_frame, resumed_view.input_epoch,
                         g_gameplay_start.resolved_map);
            g_gameplay_start.reset();
            return true;
        }
        const std::uint32_t frame =
            g_authored_frame.load(std::memory_order_acquire);
        g_session.submit_state_hash(
            frame, canonical_gameplay_state_hash(rdram, 0x00800000U));
        g_first_determinism_hash_frame.store(
            first_determinism_hash_frame(frame),
            std::memory_order_release);
        // Both selectable network modes use Player 1's authored state stream.
        // Lockstep changes prediction policy, not who owns the simulation.
        g_rollback_gameplay_ready.store(true, std::memory_order_release);
        g_frame_debt_phase.store(FrameDebtPhase::Gameplay,
                                 std::memory_order_release);
        g_gameplay_start.reset();
        return true; // Start simulation cleanly on the next authored tick.
    }
    case GameplayStartStage::Idle:
        break;
    }
    return false;
}

void end_gameplay_level() {
    if (!external_side_effects_allowed()) return;
    reset_client_catch_up_pacing();
    g_gameplay_start.reset();
    g_finish.reset();
    g_recovery.reset();
    g_rollback_repair.reset_scene();
    g_live_replica.reset();
    g_racer_orientation.reset();
    g_rollback_gameplay_ready.store(false, std::memory_order_release);
    g_frame_debt_phase.store(FrameDebtPhase::Frontend,
                             std::memory_order_release);
    // Race authority normally closes at the first finish hook while DKR's
    // complete racer topology is still valid. This unload hook is the safety
    // close for pause exits, restarts and non-race level changes.
    g_first_determinism_hash_frame.store(kDeterminismNotArmed,
                                         std::memory_order_release);
    g_session.end_authoritative_phase();
    online_input_broker().begin_epoch();
    if (g_session.view().state == ConnectionState::Failed) {
        halt_failed_simulation();
    }
}

void seal_race_finish(std::uint8_t* rdram, std::uint8_t transition_kind) {
    if (!external_side_effects_allowed()) return;
    if (!g_session.running() || rdram == nullptr || transition_kind == 0U ||
        g_finish.active()) {
        return;
    }
    // A retail finish hook is a stronger lifecycle boundary than a recovery
    // requested for an older authored frame. Never strand the race inside a
    // stale recovery after DKR has begun its post-race object teardown.
    g_recovery.reset();
    const std::uint32_t next =
        g_authored_frame.load(std::memory_order_acquire);
    g_finish.reset();
    g_finish.frame = next == 0U ? 0U : next - 1U;
    g_finish.transition_kind = transition_kind;
    g_finish.deadline.start(g_session.synchronization_progress());
    g_finish.stage = FinishStage::AwaitingAnnouncement;
    g_frame_debt_phase.store(FrameDebtPhase::FinishBarrier,
                             std::memory_order_release);
}

bool service_race_finish(std::uint8_t* rdram) {
    if (!g_finish.active()) return false;
    auto fail = [](std::string message) {
        g_finish.reset();
        if (g_session.view().state != ConnectionState::Failed) {
            g_session.fail_authoritative_state(std::move(message));
        }
        halt_failed_simulation();
    };
    if (rdram == nullptr) {
        fail("The synchronized race finish lost the game state.");
        return true;
    }
    if (g_finish.deadline.expired(g_session.synchronization_progress())) {
        fail("The synchronized finish barrier timed out. The game was released "
             "from its parked online state safely.");
        return true;
    }
    const SessionView view = g_session.view();
    if (view.state == ConnectionState::Failed || !g_session.running()) {
        fail(view.status.empty()
            ? "The online session ended during the race finish."
            : view.status);
        return true;
    }
    std::string error;
    switch (g_finish.stage) {
    case FinishStage::AwaitingAnnouncement: {
        std::uint32_t frame = g_finish.frame;
        if (!g_session.begin_finish_seal(frame, g_finish.transition_kind,
                                         error)) {
            if (!error.empty()) fail(error);
            return true;
        }
        g_finish.frame = frame;
        // All racers have already consumed Player 1's immutable commits for
        // this authored epoch. Sending another portable snapshot here creates
        // a competing authority exactly while DKR is deleting and compacting
        // race objects. The transition barrier is the sole finish authority.
        g_finish.stage = FinishStage::AwaitingTransition;
        return true;
    }
    case FinishStage::AwaitingTransition: {
        const SessionPollResult result = g_session.poll_transition(
            g_finish.frame, g_finish.transition_kind, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty()
                ? "The synchronized race transition failed."
                : error);
            return true;
        }
        if (result == SessionPollResult::Pending) return true;
        if (!g_session.complete_finish_seal(g_finish.frame, error)) {
            fail(error.empty()
                ? "The synchronized race finish could not be sealed."
                : error);
            return true;
        }
        g_first_determinism_hash_frame.store(kDeterminismNotArmed,
                                             std::memory_order_release);
        g_rollback_gameplay_ready.store(false, std::memory_order_release);
        g_frame_debt_phase.store(FrameDebtPhase::PostRace,
                                 std::memory_order_release);
        online_input_broker().begin_epoch();
        g_finish.reset();
        return true; // Resume retail post-race flow on the next authored tick.
    }
    case FinishStage::Idle:
        break;
    }
    return false;
}

bool service_recovery(std::uint8_t* rdram) {
    if (!g_recovery.active()) return false;
    g_frame_debt_phase.store(FrameDebtPhase::RecoveryBarrier,
                             std::memory_order_release);
    auto fail = [](std::string message) {
        g_recovery.reset();
        if (g_session.view().state != ConnectionState::Failed) {
            g_session.fail_authoritative_state(std::move(message));
        }
        halt_failed_simulation();
    };
    if (rdram == nullptr) {
        fail("The synchronized recovery lost the game state.");
        return true;
    }
    if (g_recovery.deadline.expired(g_session.synchronization_progress())) {
        fail("The synchronized recovery timed out at simulation frame " +
             std::to_string(g_recovery.frame) +
             ". The game was released from its parked online state safely.");
        return true;
    }
    const SessionView view = g_session.view();
    if (view.state == ConnectionState::Failed || !g_session.running()) {
        fail(view.status.empty()
            ? "The online session ended during synchronized recovery."
            : view.status);
        return true;
    }

    std::string error;
    if (g_recovery.stage == RecoveryCoordinatorStage::AwaitingBaseline) {
        const SessionPollResult result = g_session.poll_authoritative_state(
            g_recovery.frame, g_recovery.host_state, error);
        if (result == SessionPollResult::Failed) {
            fail(error.empty()
                ? "Player 1's recovery state did not arrive."
                : error);
            return true;
        }
        if (result == SessionPollResult::Pending) return true;
        const bool corrected = g_recovery.local_state != g_recovery.host_state;
        const auto descriptor = g_session.launch_descriptor();
        const bool tolerate_actor_lifecycle_lag = descriptor &&
            descriptor->synchronization == SynchronizationMode::Rollback;
        std::uint32_t unmatched_actors = 0U;
        if (!install_recovery_authoritative_state(
                rdram, g_recovery.host_state, g_recovery.frame,
                tolerate_actor_lifecycle_lag, unmatched_actors, error)) {
            fail(error);
            return true;
        }
        if (unmatched_actors != 0U) {
            failure_recorder().record(
                FailureEventKind::ReplicaInstalled, g_recovery.frame,
                unmatched_actors,
                static_cast<std::uint32_t>(g_recovery.host_state.size()),
                "rollback recovery deferred actor lifecycle");
        }
        g_session.confirm_authoritative_state(g_recovery.frame, corrected);
        g_recovery.stage = RecoveryCoordinatorStage::AwaitingResume;
        return true;
    }

    const SessionPollResult result = g_session.poll_recovery_complete(
        g_recovery.frame, error);
    if (result == SessionPollResult::Failed) {
        fail(error.empty() ? "The synchronized recovery could not resume."
                           : error);
    } else if (result == SessionPollResult::Ready) {
        const auto descriptor = g_session.launch_descriptor();
        if (descriptor &&
            descriptor->synchronization == SynchronizationMode::Rollback) {
            // Every Gekko checkpoint and queued adapter packet belongs to the
            // discarded pre-repair history. The DirectSession transport epoch
            // has advanced, so a fresh coordinator can only consume packets
            // from the verified Player-1 baseline onward.
            destroy_rollback_coordinator();
            g_rollback_repair.complete_repair();
        }
        failure_recorder().record(
            FailureEventKind::RecoveryCompleted, g_recovery.frame,
            view.authoritative_corrections, 0U,
            "verified Player 1 recovery boundary");
        g_recovery.reset();
        g_frame_debt_phase.store(FrameDebtPhase::Gameplay,
                                 std::memory_order_release);
    }
    return true;
}

void begin_completed_recovery(std::uint8_t* rdram,
                              std::uint32_t completed_frame);

void commit_authoritative_gameplay_frame(std::uint8_t* rdram) {
    const std::uint32_t next = g_authored_frame.load(std::memory_order_acquire);
    const FrameDebtPhase debt_phase = g_frame_debt_phase.load(
        std::memory_order_acquire);
    if (g_session.running() && next != 0U &&
        external_side_effects_allowed() &&
        (debt_phase == FrameDebtPhase::Frontend ||
         debt_phase == FrameDebtPhase::PostRace)) {
        // This common post-mode hook runs only after the retail frame has
        // returned. Report the completed frontend cursor here rather than when
        // its commit merely arrives, so Player 1's lead limit reflects real
        // presentation work on every peer.
        g_session.report_simulation_progress(
            next - 1U, TimelineProgressScope::Frontend);
    }
    const std::uint32_t first_hash_frame =
        g_first_determinism_hash_frame.load(std::memory_order_acquire);
    if (rdram == nullptr || !g_session.running() ||
        first_hash_frame == kDeterminismNotArmed) {
        return;
    }
    if (next == 0U) return;
    const std::uint32_t completed_frame = next - 1U;
    if (external_side_effects_allowed())
        g_session.report_simulation_progress(completed_frame);
    const RuntimeSessionView live_view = g_session.runtime_view();
    if (external_side_effects_allowed()) {
        // Plane cameraYaw and steerVisualRotation are presentation
        // accumulators, not simulation authority. Correct them after the
        // completed tick on both synchronization modes without ever parking
        // the authored input timeline.
        synchronize_racer_orientation(rdram, live_view, completed_frame);
    }
    if (live_view.host && live_view.launch_descriptor &&
        live_view.launch_descriptor->synchronization ==
            SynchronizationMode::Rollback &&
        external_side_effects_allowed()) {
        // Publish the start state of `next` on the disposable replica lane.
        // Guests install only an exact-boundary sample; missing or superseded
        // samples never park gameplay and never share the frame-commit lane.
        std::string replica_error;
        const SessionPollResult replica = synchronize_live_replica(
            rdram, live_view, next, replica_error);
        if (replica == SessionPollResult::Failed) {
            std::fprintf(stderr,
                         "[netplay][replica] host frame=%u: %s\n", next,
                         replica_error.c_str());
        }
    }
    const std::optional<std::uint32_t> recovery = g_session.recovery_frame();
    const bool recovery_checkpoint = recovery &&
                                     completed_frame == *recovery;
    // Normal gameplay uses only compact asynchronous determinism digests.
    // A full portable state crosses the network exclusively at an explicit
    // future recovery boundary. This removes the
    // old 5 Hz RTT wait that presented as recurring frame and audio stutter.
    if (recovery && completed_frame > *recovery) {
        g_session.fail_authoritative_state(
            "The simulation passed its synchronized recovery boundary.");
        halt_failed_simulation();
        return;
    }
    if (!recovery_checkpoint) return;
    begin_completed_recovery(rdram, completed_frame);
}

void begin_completed_recovery(std::uint8_t* rdram,
                              std::uint32_t completed_frame) {
    if (g_recovery.active()) return;
    const SessionView view = g_session.view();
    std::string error;
    g_recovery.reset();
    g_recovery.frame = completed_frame;
    g_frame_debt_phase.store(FrameDebtPhase::RecoveryBarrier,
                             std::memory_order_release);
    g_recovery.deadline.start(g_session.synchronization_progress());
    if (!capture_authoritative_state(rdram, 0x00800000U,
                                     completed_frame,
                                     g_recovery.local_state, error)) {
        g_recovery.reset();
        g_session.fail_authoritative_state(
            "The local authored state could not be captured safely at frame " +
            std::to_string(completed_frame) + ": " + error);
        halt_failed_simulation();
        return;
    }
    if (view.host) {
        if (!g_session.publish_authoritative_state(
                completed_frame, g_recovery.local_state, error)) {
            std::fprintf(stderr,
                         "[netplay][authority] host frame=%u %s\n",
                         completed_frame, error.c_str());
            g_session.fail_authoritative_state(
                "Player 1 could not publish a safe authoritative state at frame " +
                std::to_string(completed_frame) + ".");
            halt_failed_simulation();
            g_recovery.reset();
            return;
        }
        // Mark Player 1 present in the recovery acknowledgement set. The old
        // blocking path omitted this and therefore waited for an impossible
        // host acknowledgement until its timeout expired.
        g_session.confirm_authoritative_state(completed_frame, false);
        g_recovery.stage = RecoveryCoordinatorStage::AwaitingResume;
        return;
    }
    g_recovery.stage = RecoveryCoordinatorStage::AwaitingBaseline;
}

bool rollback_replay_active() {
    return g_rollback_advancing.load(std::memory_order_acquire) &&
           !g_external_side_effects.load(std::memory_order_acquire);
}

bool external_side_effects_allowed() {
    return g_external_side_effects.load(std::memory_order_acquire);
}

std::uint32_t authored_simulation_pacing_scale_milli() {
    return g_authored_pacing_scale_milli.load(std::memory_order_acquire);
}

OnlineWaitReason online_wait_reason() {
    return g_online_wait_state.reason();
}

std::uint64_t online_wait_generation() {
    return g_online_wait_state.generation();
}
std::uint64_t online_wait_episode() { return g_online_wait_state.episode(); }

bool online_wait_active() { return g_online_wait_state.active(); }

bool physical_input_poll_allowed() {
    if (!external_side_effects_allowed()) return false;
    return !g_skip_next_si_physical_poll.exchange(
        false, std::memory_order_acq_rel);
}

void begin_presentation_random_scope() {
    const RuntimeSessionView view = g_session.runtime_view();
    if (host_authority_requested(view) &&
        g_rollback_gameplay_ready.load(std::memory_order_acquire)) {
        ++g_presentation_random_scope_depth;
    }
}

void end_presentation_random_scope() {
    if (g_presentation_random_scope_depth > 0U) {
        --g_presentation_random_scope_depth;
    }
}

bool override_presentation_random_range(recomp_context* context) {
    if (context == nullptr || g_presentation_random_scope_depth == 0U) {
        return false;
    }
    const auto minimum = static_cast<std::int32_t>(context->r4);
    const auto maximum = static_cast<std::int32_t>(context->r5);
    context->r2 = static_cast<gpr>(
        g_presentation_random_stream.range(minimum, maximum));
    return true;
}

RollbackMetrics rollback_metrics() {
    RollbackMetrics metrics{};
    const SessionView view = g_session.view();
    metrics.input_epoch = view.input_epoch;
    metrics.scene_epoch = view.scene_epoch;
    FrameDebtPhase phase = g_frame_debt_phase.load(
        std::memory_order_acquire);
    if (view.state != ConnectionState::Running) {
        phase = FrameDebtPhase::Inactive;
    }
    const bool gameplay_ready =
        g_rollback_gameplay_ready.load(std::memory_order_acquire);
    const bool coordinator_present = rollback_coordinator_present(
        g_rollback.host_authoritative, g_rollback.gekko != nullptr);
    metrics.active = frame_debt_uses_rollback_cursor(
        phase, gameplay_ready, coordinator_present);
    metrics.replaying = rollback_replay_active();
    metrics.simulation_frame = frame_debt_local_cursor(
        phase, gameplay_ready, coordinator_present, g_rollback.latest_frame,
        g_authored_frame.load(std::memory_order_acquire));
    metrics.rollback_count = g_rollback.rollback_count;
    metrics.replayed_frames = g_rollback.replayed_frames;
    metrics.largest_rollback = g_rollback.largest_rollback;
    metrics.frames_ahead = g_rollback.frames_ahead;
    metrics.catch_up_state = g_client_catch_up_state.load(
        std::memory_order_acquire);
    metrics.pacing_scale_milli = g_authored_pacing_scale_milli.load(
        std::memory_order_acquire);
    metrics.pacing_target_hz = g_authored_pacing_target_hz.load(
        std::memory_order_acquire);
    const auto descriptor = g_session.launch_descriptor();
    if (descriptor) {
        if (phase == FrameDebtPhase::Frontend ||
            phase == FrameDebtPhase::PostRace) {
            metrics.target_frame_debt = authored_timeline_target_debt(
                view.network_rtt_ms, view.network_jitter_ms,
                descriptor->input_delay_frames);
        } else if (phase == FrameDebtPhase::Gameplay) {
            metrics.target_frame_debt =
                descriptor->synchronization ==
                        SynchronizationMode::Rollback
                    ? rollback_replica_target_debt(
                          view.network_rtt_ms, view.network_jitter_ms,
                          descriptor->input_delay_frames)
                    : kLockstepClientTargetFrameDebt;
        }
    }

    const std::uint32_t local_epoch = g_authored_input_epoch.load(
        std::memory_order_acquire);
    metrics.frame_debt = measure_frame_debt(
        phase, local_epoch != 0U && local_epoch == view.input_epoch,
        view.authoritative_input_frame, metrics.simulation_frame);
    return metrics;
}

int drive_authored_tick(std::uint8_t* rdram, recomp_context* context) {
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
    dkr_private_replay_capture(rdram, context);
#endif
    const RuntimeSessionView view = g_session.runtime_view();
    if (view.active && view.launch_descriptor) {
        if (const char* reason = experimental::runtime_admission_error(view.launch_descriptor->synchronization)) {
            g_session.fail_runtime_start(reason);
            halt_failed_simulation();
            return 1;
        }
    }
    const FrameDebtPhase observed_phase = g_frame_debt_phase.load(
        std::memory_order_acquire);
    const FrameDebtPhase previous_phase =
        g_last_logged_frame_debt_phase.exchange(
            observed_phase, std::memory_order_acq_rel);
    if (previous_phase != observed_phase) {
        std::fprintf(
            stderr,
            "[netplay][timeline] phase=%u->%u input_epoch=%u scene_epoch=%u local=%u\n",
            static_cast<unsigned>(previous_phase),
            static_cast<unsigned>(observed_phase), view.input_epoch,
            view.scene_epoch,
            g_authored_frame.load(std::memory_order_acquire));
    }
    refresh_input_vehicle_context(rdram, view);
    if (view.state == ConnectionState::Failed) {
        reset_client_catch_up_pacing();
        halt_failed_simulation();
        return 0;
    }
    // Both lockstep and rollback use the same non-blocking track-start gate.
    // While it is pending, skip only the next authored simulation tick; the
    // host loop, renderer, overlay, input discovery and audio device remain
    // responsive instead of sleeping inside level_load_game.
    if (service_gameplay_start(rdram)) {
        const FrameDebtPhase phase = g_frame_debt_phase.load(
            std::memory_order_acquire);
        g_online_wait_state.enter(
            phase == FrameDebtPhase::LoadingBarrier
                ? OnlineWaitReason::Loading
                : OnlineWaitReason::RaceStart);
        reset_client_catch_up_pacing();
        return 1;
    }
    if (service_race_finish(rdram)) {
        g_online_wait_state.enter(OnlineWaitReason::RaceFinish);
        reset_client_catch_up_pacing();
        return 1;
    }
    // Begin may arrive after the post-native hook. The worker's admission
    // guard parks the following tick; capture the exact completed boundary
    // here instead of waiting for a post hook which has already executed.
    const auto recovery_boundary = g_session.recovery_frame();
    if (!g_recovery.active() && recovery_boundary &&
        g_session.native_completed_frame() == recovery_boundary) {
        begin_completed_recovery(rdram, *recovery_boundary);
    }
    if (service_recovery(rdram)) {
        g_online_wait_state.enter(OnlineWaitReason::Recovery);
        reset_client_catch_up_pacing();
        return 1;
    }
    if (g_rollback_repair.requested &&
        std::chrono::steady_clock::now() >=
            g_rollback_repair.request_deadline &&
        !g_session.recovery_frame()) {
        fail_rollback(
            "Player 1 did not establish the requested rollback recovery boundary.");
        return 1;
    }
    if (!host_authority_requested(view)) {
        if (rollback_coordinator_present(
                g_rollback.host_authoritative,
                g_rollback.gekko != nullptr)) {
            destroy_rollback_coordinator();
        }
        const FrameDebtPhase debt_phase = g_frame_debt_phase.load(
            std::memory_order_acquire);
        const bool frontend_timeline = view.running && view.launch_descriptor &&
            (debt_phase == FrameDebtPhase::Frontend ||
             debt_phase == FrameDebtPhase::PostRace);
        if (!frontend_timeline ||
            !frame_debt_phase_allows_catch_up(debt_phase)) {
            g_online_wait_state.leave();
            reset_client_catch_up_pacing();
            g_prepared_authored_frame.store(kNoPreparedAuthoredFrame,
                                            std::memory_order_release);
            return 0;
        }

        const SessionPacingView pacing_view = g_session.pacing_view();
        const std::uint32_t frame = g_authored_frame.load(
            std::memory_order_acquire);
        const std::uint32_t maximum_lead = host_authority_lead_limit(
            pacing_view.network_rtt_ms, pacing_view.network_jitter_ms,
            view.launch_descriptor->input_delay_frames);
        if (view.host) {
            reset_client_catch_up_pacing();
            if (frame_debt_phase_allows_host_backpressure(debt_phase) &&
                g_session.host_should_backpressure(
                    frame, maximum_lead, TimelineProgressScope::Frontend)) {
                g_prepared_authored_frame.store(kNoPreparedAuthoredFrame,
                                                std::memory_order_release);
                g_session.wait_for_simulation_progress(
                    pacing_view.simulation_wake_generation,
                    std::chrono::milliseconds(8));
                g_online_wait_state.enter(OnlineWaitReason::Transition);
                return 1;
            }
        } else {
            const std::uint32_t debt =
                pacing_view.authoritative_input_frame > frame
                    ? pacing_view.authoritative_input_frame - frame : 0U;
            const std::uint32_t target_debt =
                authored_timeline_target_debt(
                    pacing_view.network_rtt_ms,
                    pacing_view.network_jitter_ms,
                    view.launch_descriptor->input_delay_frames);
            const std::uint32_t contiguous =
                g_session.contiguous_authoritative_commits(frame, 1U);
            const std::uint32_t local_epoch = g_authored_input_epoch.load(
                std::memory_order_acquire);
            update_client_catch_up_pacing(ClientCatchUpSample{
                .eligible =
                    frame_debt_phase_allows_catch_up(debt_phase) &&
                    local_epoch != 0U &&
                    local_epoch == pacing_view.input_epoch,
                .frame_debt = debt,
                .target_debt = target_debt,
                .contiguous_commits = contiguous,
            });
        }

        // Pre-admit exactly one normal visible frontend tick. This submits the
        // local pad and services repair with a zero timeout; a missing commit
        // parks before main_game_loop, never inside input_update. The retail
        // hook then performs an idempotent read of this consumed commit.
        const PackedInput local = poll_authored_local_input(view);
        FrameInputs prepared_inputs{};
        const InputSynchronizationResult prepared =
            g_session.synchronize_inputs_result(
                frame, local, prepared_inputs, std::chrono::milliseconds(0));
        if (prepared == InputSynchronizationResult::Committed ||
            prepared == InputSynchronizationResult::AlreadyCommitted) {
            if (rdram != nullptr) {
                MEM_W(0, rdram_address(revision_addresses::LogicUpdateRate)) =
                    authored_logic_step(true, MEM_W(0, rdram_address(
                        revision_addresses::LogicUpdateRate)));
            }
            g_online_wait_state.leave();
            g_prepared_authored_frame.store(frame,
                                            std::memory_order_release);
            g_skip_next_si_physical_poll.store(true,
                                               std::memory_order_release);
            return 0;
        }
        g_prepared_authored_frame.store(kNoPreparedAuthoredFrame,
                                        std::memory_order_release);
        if (prepared == InputSynchronizationResult::Pending) {
            g_session.wait_for_simulation_progress(
                pacing_view.simulation_wake_generation,
                std::chrono::milliseconds(8));
            g_online_wait_state.enter(OnlineWaitReason::ClientCatchUp);
            return 1;
        }
        if (prepared == InputSynchronizationResult::Suspended) {
            g_online_wait_state.enter(OnlineWaitReason::Transition);
            reset_client_catch_up_pacing();
            return 1;
        }
        const RuntimeSessionView failed_view = g_session.runtime_view();
        std::fprintf(stderr, "[netplay][frontend-input] frame=%u %s\n",
                     frame, failed_view.status.c_str());
        if (failed_view.state == ConnectionState::Failed) {
            halt_failed_simulation();
        }
        return 1;
    }
    if (!g_rollback_gameplay_ready.load(std::memory_order_acquire)) {
        g_online_wait_state.leave();
        reset_client_catch_up_pacing();
        if (rollback_coordinator_present(
                g_rollback.host_authoritative,
                g_rollback.gekko != nullptr)) {
            destroy_rollback_coordinator();
        }
        return 0;
    }
    if (rdram == nullptr || context == nullptr) {
        reset_client_catch_up_pacing();
        fail_rollback("The authored rollback boundary had no game state.");
        return 1;
    }

    // Rollback and lockstep share one Player-1-owned simulation path. Their
    // only difference is whether DirectSession may predict an absent input;
    // neither mode permits a guest-created race state to drift independently.
    if (!initialise_host_authoritative_rollback(rdram, view)) {
        reset_client_catch_up_pacing();
        if (g_session.view().state == ConnectionState::Failed) {
            halt_failed_simulation();
        }
        return 1;
    }
    // Player 1 never rewinds a live native game loop. The former correction
    // path restored the portable racer/RNG subset and then replayed the whole
    // track, leaving object interactions, collision lists and lifecycle state
    // from a different time. Late samples now obey the committed host input
    // deadline; clients reconcile to the next complete Player 1 state.

    if (view.launch_descriptor) {
        dkr::runtime::platform::set_online_input_routing(
            true, view.launch_descriptor->occupied_mask, view.local_slot);
    }
    dkr::runtime::platform::poll_input();
    std::uint16_t authoritative_buttons = 0U;
    float authoritative_stick_x = 0.0F;
    float authoritative_stick_y = 0.0F;
    const int authoritative_profile = static_cast<int>(
        dkr::runtime::platform::online_input_profile());
    dkr::runtime::platform::get_physical_input(
        authoritative_profile, &authoritative_buttons,
        &authoritative_stick_x, &authoritative_stick_y);
    PackedInput authoritative_local{
        authoritative_buttons, pack_input_axis(authoritative_stick_x),
        pack_input_axis(authoritative_stick_y)};
    if (view.local_slot != 0U &&
        view.host_control != HostControlPolicy::EveryAssignedPort &&
        !g_assigned_ports_released.load(std::memory_order_acquire)) {
        authoritative_local = {};
    }

    std::uint32_t authoritative_frame =
        g_authored_frame.load(std::memory_order_acquire);
    bool committed_frame_available = true;
    const SessionPacingView pacing_view = g_session.pacing_view();
    if (view.host && view.launch_descriptor) {
        reset_client_catch_up_pacing();
        const std::uint32_t maximum_lead = host_authority_lead_limit(
            pacing_view.network_rtt_ms,
            pacing_view.network_jitter_ms,
            view.launch_descriptor->input_delay_frames);
        if (g_session.host_should_backpressure(authoritative_frame,
                                               maximum_lead)) {
            // Player 1 remains the sole simulation authority, but it does not
            // run seconds ahead while a client's native scheduler/GPU has a
            // short hitch. Network, audio and overlay work continue through
            // the ordinary outer loop until the completed-frame watermark
            // closes the bounded gap.
            g_session.wait_for_simulation_progress(
                pacing_view.simulation_wake_generation,
                std::chrono::milliseconds(8));
            g_online_wait_state.enter(OnlineWaitReason::ClientCatchUp);
            return 1;
        }
    }
    if (!view.host && view.launch_descriptor) {
        std::uint32_t debt =
            pacing_view.authoritative_input_frame > authoritative_frame
                ? pacing_view.authoritative_input_frame - authoritative_frame
                : 0U;
        const std::uint32_t target_debt =
            view.launch_descriptor->synchronization ==
                    SynchronizationMode::Rollback
                ? rollback_replica_target_debt(
                      pacing_view.network_rtt_ms,
                      pacing_view.network_jitter_ms,
                      view.launch_descriptor->input_delay_frames)
                : kLockstepClientTargetFrameDebt;
        if (view.launch_descriptor->synchronization ==
                SynchronizationMode::Rollback &&
            debt > target_debt) {
            std::uint32_t resumed_frame = authoritative_frame;
            std::string catch_up_error;
            const SessionPollResult catch_up = fast_forward_to_live_replica(
                rdram, view, authoritative_frame,
                pacing_view.authoritative_input_frame, target_debt,
                resumed_frame, catch_up_error);
            if (catch_up == SessionPollResult::Failed) {
                fail_rollback(catch_up_error.empty()
                    ? "The authenticated Player 1 catch-up boundary could not be installed."
                    : catch_up_error);
                return 1;
            }
            if (resumed_frame != authoritative_frame) {
                authoritative_frame = resumed_frame;
                debt = pacing_view.authoritative_input_frame >
                        authoritative_frame
                    ? pacing_view.authoritative_input_frame -
                          authoritative_frame
                    : 0U;
            }
        }
        const std::uint32_t contiguous =
            g_session.contiguous_authoritative_commits(
                authoritative_frame, kClientMaximumCatchUpTicks);
        const std::uint32_t local_epoch = g_authored_input_epoch.load(
            std::memory_order_acquire);
        update_client_catch_up_pacing(ClientCatchUpSample{
            .eligible =
                pacing_view.state == ConnectionState::Running &&
                g_frame_debt_phase.load(std::memory_order_acquire) ==
                    FrameDebtPhase::Gameplay &&
                local_epoch != 0U && local_epoch == pacing_view.input_epoch,
            .frame_debt = debt,
            .target_debt = target_debt,
            .contiguous_commits = contiguous,
        });
        committed_frame_available = authored_catch_up_budget(
            debt, target_debt, contiguous, kClientMaximumCatchUpTicks) != 0U;
        if (!committed_frame_available) {
            // No complete host commit is available. Let the network worker
            // continue without entering a partial simulation tick. Repeat the
            // last completed-frame watermark at a bounded heartbeat cadence:
            // if its first datagram was lost exactly when Player 1 applied
            // backpressure, this lets the host resume without either runtime
            // executing a speculative or hidden game tick.
            if (authoritative_frame != 0U) {
                g_session.report_simulation_progress(
                    authoritative_frame - 1U);
            }
            FrameInputs pending{};
            (void)g_session.synchronize_inputs_result(
                authoritative_frame, authoritative_local, pending,
                std::chrono::milliseconds(0));
            g_session.wait_for_simulation_progress(
                pacing_view.simulation_wake_generation,
                std::chrono::milliseconds(8));
            g_online_wait_state.enter(OnlineWaitReason::ClientCatchUp);
            return 1;
        }
    }

    const bool rollback_replica = view.launch_descriptor &&
        view.launch_descriptor->synchronization ==
            SynchronizationMode::Rollback;
    if (!view.host && rollback_replica) {
        std::string replica_error;
        const SessionPollResult replica = synchronize_live_replica(
            rdram, view, authoritative_frame, replica_error);
        if (replica == SessionPollResult::Failed) {
            fail_rollback(replica_error.empty()
                ? "Player 1's live race-state stream ended unexpectedly."
                : replica_error);
            return 1;
        }
    }
    FrameInputs authoritative_inputs{};
    const InputSynchronizationResult authoritative_result =
        g_session.synchronize_inputs_result(
            authoritative_frame, authoritative_local,
            authoritative_inputs, std::chrono::milliseconds(0));
    if (authoritative_result ==
        InputSynchronizationResult::AlreadyCommitted) {
        g_online_wait_state.leave();
        // The boundary was already simulated and committed. Repair the outer
        // cursor only; replaying main_game_loop here would advance physics,
        // RNG and lifecycle state twice on this machine.
        const std::uint32_t next_frame =
            authoritative_frame ==
                    std::numeric_limits<std::uint32_t>::max()
                ? authoritative_frame
                : authoritative_frame + 1U;
        g_authored_frame.store(next_frame, std::memory_order_release);
        return 1;
    }
    if (authoritative_result == InputSynchronizationResult::Pending) {
        // The network worker continues receiving and retransmitting while the
        // authored tick is parked. Never enter a hidden nested game loop.
        g_session.wait_for_simulation_progress(
            pacing_view.simulation_wake_generation,
            std::chrono::milliseconds(8));
        g_online_wait_state.enter(OnlineWaitReason::ClientCatchUp);
        return 1;
    }
    if (authoritative_result == InputSynchronizationResult::Suspended) {
        g_online_wait_state.enter(OnlineWaitReason::Transition);
        online_input_broker().publish(
            authoritative_frame, InputFrameSource::Rollback,
            FrameInputs{});
        return 1;
    }
    if (authoritative_result == InputSynchronizationResult::Failed) {
        const auto failed_view = g_session.runtime_view();
        std::fprintf(stderr, "[netplay][rollback-input] frame=%u %s\n",
                     authoritative_frame, failed_view.status.c_str());
        if (failed_view.state == ConnectionState::Failed) {
            halt_failed_simulation();
        }
        return 1;
    }

    // Exactly one retail game loop is permitted per outer authored dispatch.
    // This tick is always visible; no hidden renderer/audio/object-lifecycle
    // pass can become stranded waiting for the outer scheduler to resume.
    g_online_wait_state.leave();
    if (!advance_host_authoritative_frame(
            rdram, context, authoritative_frame,
            authoritative_inputs, true)) {
        halt_failed_simulation();
        return 1;
    }
    g_session.report_simulation_progress(authoritative_frame);
    return 1;

#if 0
    // Retired experimental Gekko replay path. Rewinding a portable emulated
    // state subset underneath a live native call stack could combine object,
    // renderer and lifecycle state from different frames. DKR-R now uses the
    // Player-1-owned authored simulation above plus bounded complete-state
    // reconciliation. Keep this excluded until/unless a future implementation
    // can prove a complete native-runtime snapshot contract.
    if (g_rollback.gekko == nullptr) {
        // Rollback deliberately never rewinds a live recomp_context, OS queue,
        // renderer allocation or audio allocation. Native call stacks cannot
        // be rewound by restoring emulated registers. Every checkpoint below
        // is the portable authored-state contract also used by recovery.
        g_rollback_authored_base =
            g_authored_frame.load(std::memory_order_acquire);
        if (!initialise_rollback(view)) {
            if (g_session.view().state == ConnectionState::Failed) {
                halt_failed_simulation();
            }
            return 1;
        }
    }

    // A scene/epoch transition may have invalidated the broker's committed
    // output frame. Reassert the immutable online topology before polling the
    // local physical lane; local input must never depend on an output frame
    // already having been published.
    if (view.launch_descriptor) {
        dkr::runtime::platform::set_online_input_routing(
            true, view.launch_descriptor->occupied_mask, view.local_slot);
    }
    dkr::runtime::platform::poll_input();
    std::uint16_t buttons = 0U;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    const int local_profile = static_cast<int>(
        dkr::runtime::platform::online_input_profile());
    dkr::runtime::platform::get_physical_input(
        local_profile, &buttons, &stick_x, &stick_y);
    const PackedInput local{buttons, pack_input_axis(stick_x),
                            pack_input_axis(stick_y)};
    gekko_add_local_input(g_rollback.gekko, g_rollback.local_handle,
                          const_cast<PackedInput*>(&local));

    int event_count = 0;
    GekkoGameEvent** events =
        gekko_update_session(g_rollback.gekko, &event_count);

    int session_event_count = 0;
    GekkoSessionEvent** session_events =
        gekko_session_events(g_rollback.gekko, &session_event_count);
    for (int index = 0; index < session_event_count; ++index) {
        const GekkoSessionEvent* event = session_events[index];
        if (event == nullptr) continue;
        if (event->type == GekkoDesyncDetected) {
            const auto& desync = event->data.desynced;
            failure_recorder().record(
                FailureEventKind::RollbackDesync,
                g_rollback_authored_base + static_cast<std::uint32_t>(
                    (std::max)(desync.frame, 0)),
                desync.local_checksum, desync.remote_checksum,
                "Gekko determinism mismatch");
            const auto* record = rollback_digest_for_frame(desync.frame);
            const std::uint32_t absolute_frame =
                g_rollback_authored_base +
                static_cast<std::uint32_t>((std::max)(desync.frame, 0));
            if (record != nullptr) {
                std::fprintf(
                    stderr,
                    "[netplay][rollback][desync] frame=%d peer=%d "
                    "local=%08X remote=%08X recorded=%08X "
                    "globals=%016llX roster=%016llX racers=%016llX count=%u\n",
                    desync.frame, desync.remote_handle,
                    desync.local_checksum, desync.remote_checksum,
                    record->folded,
                    static_cast<unsigned long long>(record->digest.globals),
                    static_cast<unsigned long long>(record->digest.roster),
                    static_cast<unsigned long long>(record->digest.racers),
                    record->digest.racer_count);
                for (std::uint32_t racer = 0U;
                     racer < record->digest.racer_count &&
                     racer < record->digest.racer_details.size();
                     ++racer) {
                    std::fprintf(
                        stderr,
                        "[netplay][rollback][desync] racer[%u]=%016llX\n",
                        racer,
                        static_cast<unsigned long long>(
                            record->digest.racer_details[racer]));
                }
            } else {
                std::fprintf(
                    stderr,
                    "[netplay][rollback][desync] frame=%d peer=%d "
                    "local=%08X remote=%08X digest=unavailable\n",
                    desync.frame, desync.remote_handle,
                    desync.local_checksum, desync.remote_checksum);
            }
            if (record != nullptr) {
                g_session.submit_state_digest(
                    absolute_frame, record->digest.combined,
                    record->digest.globals, record->digest.roster,
                    record->digest.racers, record->digest.racer_count,
                    record->digest.racer_details);
            }
            if (!g_rollback_repair.requested) {
                const auto now = std::chrono::steady_clock::now();
                if (g_rollback_repair.window_started.time_since_epoch().count() == 0 ||
                    now - g_rollback_repair.window_started >=
                        std::chrono::seconds(30)) {
                    g_rollback_repair.window_started = now;
                    g_rollback_repair.attempts = 0U;
                }
                if (++g_rollback_repair.attempts > 4U) {
                    fail_rollback(
                        "The race repeatedly diverged after four authenticated "
                        "Player 1 state repairs. The session stopped rather than "
                        "show different race results.");
                    return 1;
                }
                if (!g_session.request_rollback_recovery(
                        absolute_frame, "rollback gameplay state")) {
                    fail_rollback(
                        "The rollback mismatch could not request a safe Player 1 recovery.");
                    return 1;
                }
                g_rollback_repair.requested = true;
                g_rollback_repair.mismatch_frame = absolute_frame;
                g_rollback_repair.request_deadline =
                    now + std::chrono::seconds(5);
                std::fprintf(
                    stderr,
                    "[netplay][rollback][repair] requested absolute_frame=%u attempt=%u\n",
                    absolute_frame, g_rollback_repair.attempts);
                failure_recorder().record(
                    FailureEventKind::RecoveryRequested, absolute_frame,
                    g_rollback_repair.attempts,
                    static_cast<std::uint32_t>(desync.remote_handle),
                    "Player 1 state repair");
            }
        }
        if (event->type == GekkoPlayerDisconnected) {
            fail_rollback("A rollback peer disconnected from the race.");
            return 1;
        }
    }

    bool cycle_loaded = false;
    int last_advance = -1;
    for (int index = 0; index < event_count; ++index) {
        if (events[index] == nullptr) continue;
        cycle_loaded |= events[index]->type == GekkoLoadEvent;
        if (events[index]->type == GekkoAdvanceEvent) last_advance = index;
    }

    bool advanced = false;
    std::uint32_t loaded_frame = 0U;
    for (int index = 0; index < event_count; ++index) {
        GekkoGameEvent* event = events[index];
        if (event == nullptr) continue;
        switch (event->type) {
        case GekkoSaveEvent: {
            std::uint64_t checksum = 0U;
            std::string capture_error;
            if (event->data.save.state == nullptr ||
                event->data.save.state_len == nullptr ||
                event->data.save.checksum == nullptr ||
                event->data.save.frame < -1 || !g_rollback.states ||
                !capture_rollback_simulation_state(
                    rdram, 0x00800000U,
                    static_cast<std::uint32_t>(event->data.save.frame + 1),
                    g_rollback.capture, checksum, capture_error)) {
                fail_rollback(
                    capture_error.empty()
                        ? "A rollback checkpoint could not be captured."
                        : "A rollback checkpoint could not be captured: " +
                              capture_error);
                return 1;
            }
            const GameplayStateDigest digest = canonical_gameplay_state_digest(
                rdram, 0x00800000U);
            const auto storage_frame = static_cast<std::uint32_t>(
                event->data.save.frame + 1);
            if (!g_rollback.states->save(storage_frame, g_rollback.capture,
                                         checksum)) {
                fail_rollback("The bounded rollback checkpoint store rejected a frame.");
                return 1;
            }
            failure_recorder().record(
                FailureEventKind::RollbackSave, storage_frame,
                static_cast<std::uint32_t>(g_rollback.capture.size()),
                static_cast<std::uint32_t>(checksum ^ (checksum >> 32U)),
                "authored simulation state");
            const RollbackToken token{kRollbackTokenMagic, checksum,
                                      event->data.save.frame, 0U};
            std::memcpy(event->data.save.state, &token, sizeof(token));
            *event->data.save.state_len = sizeof(token);
            const std::uint32_t folded = static_cast<std::uint32_t>(
                checksum ^ (checksum >> 32U));
            *event->data.save.checksum = folded;
            const std::size_t digest_index = static_cast<std::size_t>(
                event->data.save.frame + 1) % g_rollback.digests.size();
            g_rollback.digests[digest_index] = {
                event->data.save.frame, folded, digest};
            break;
        }
        case GekkoLoadEvent: {
            RollbackToken token{};
            if (event->data.load.state == nullptr ||
                event->data.load.state_len != sizeof(token)) {
                fail_rollback("A rollback load token had an invalid size.");
                return 1;
            }
            std::memcpy(&token, event->data.load.state, sizeof(token));
            std::uint64_t stored_checksum = 0U;
            std::string restore_error;
            if (token.magic != kRollbackTokenMagic || token.frame < -1 ||
                !g_rollback.states ||
                !g_rollback.states->load(
                    static_cast<std::uint32_t>(token.frame + 1),
                    g_rollback.restore,
                                         &stored_checksum) ||
                stored_checksum != token.checksum ||
                !restore_rollback_simulation_state(
                    rdram, 0x00800000U, g_rollback.restore,
                    static_cast<std::uint32_t>(token.frame + 1),
                    token.checksum, restore_error)) {
                fail_rollback(
                    restore_error.empty()
                        ? "A rollback checkpoint could not be restored exactly."
                        : "A rollback checkpoint could not be restored exactly: " +
                              restore_error);
                return 1;
            }
            loaded_frame = static_cast<std::uint32_t>(token.frame + 1);
            failure_recorder().record(
                FailureEventKind::RollbackLoad, loaded_frame,
                g_rollback.latest_frame,
                static_cast<std::uint32_t>(stored_checksum ^
                                           (stored_checksum >> 32U)),
                "authored simulation state");
            g_rollback.states->discard_after(loaded_frame);
            ++g_rollback.rollback_count;
            if (token.frame >= 0 &&
                g_rollback.latest_frame >=
                    static_cast<std::uint32_t>(token.frame)) {
                g_rollback.largest_rollback = (std::max)(
                    g_rollback.largest_rollback,
                    g_rollback.latest_frame -
                        static_cast<std::uint32_t>(token.frame));
            }
            g_authored_frame.store(
                g_rollback_authored_base +
                    static_cast<std::uint32_t>(token.frame + 1),
                                   std::memory_order_release);
            // Presentation identities are host sidecars rather than emulated
            // memory. Drop future-timeline identities before replay rebuilds
            // them; replay graphics tasks themselves remain renderer-silent.
            dkr_presentation_scene_begin(rdram, context);
            break;
        }
        case GekkoAdvanceEvent: {
            const auto& advance = event->data.adv;
            if (advance.frame < 0 || advance.inputs == nullptr ||
                !view.launch_descriptor ||
                advance.input_len !=
                    sizeof(PackedInput) *
                        view.launch_descriptor->player_count) {
                fail_rollback("Rollback supplied an invalid authored input frame.");
                return 1;
            }
            g_rollback_frame_inputs = {};
            const auto* inputs =
                reinterpret_cast<const PackedInput*>(advance.inputs);
            for (std::size_t handle = 0U;
                 handle < view.launch_descriptor->player_count; ++handle) {
                const int slot = g_rollback.handles[handle];
                if (slot < 0 || slot >= static_cast<int>(kMaximumPlayers)) {
                    fail_rollback("Rollback input routing lost a lobby slot.");
                    return 1;
                }
                g_rollback_frame_inputs[static_cast<std::size_t>(slot)] =
                    inputs[handle];
            }
            g_rollback_event_frame =
                static_cast<std::uint32_t>(advance.frame);
            const bool publish = index == last_advance &&
                                 !advance.rolling_back &&
                                 !advance.running_ahead;
            g_external_side_effects.store(publish,
                                          std::memory_order_release);
            g_rollback_advancing.store(true, std::memory_order_release);
            // The original loop consumes the previous frame's framebuffer-
            // derived logic rate before fb_update() calculates the next one.
            // Pin both sides of the call so original and replayed frames use
            // the same 30 Hz simulation step on every machine, regardless of
            // local GPU load, window state or presentation refresh rate.
            MEM_W(0, rdram_address(revision_addresses::LogicUpdateRate)) =
                kRollbackLogicUpdateRate;
            dkr::runtime::invoke_main_game_loop(rdram, context);
            MEM_W(0, rdram_address(revision_addresses::LogicUpdateRate)) =
                kRollbackLogicUpdateRate;
            g_rollback_advancing.store(false, std::memory_order_release);
            g_external_side_effects.store(true, std::memory_order_release);
            advanced = true;
            g_rollback.latest_frame = g_rollback_event_frame;
            if (advance.rolling_back || cycle_loaded) {
                ++g_rollback.replayed_frames;
            }
            if (!g_rollback_gameplay_ready.load(std::memory_order_acquire)) {
                break;
            }
            if (g_recovery.active()) break;
            break;
        }
        default:
            break;
        }
        if (!g_rollback_gameplay_ready.load(std::memory_order_acquire) ||
            g_recovery.active()) break;
    }

    g_rollback_advancing.store(false, std::memory_order_release);
    g_external_side_effects.store(true, std::memory_order_release);
    if (g_rollback.gekko != nullptr) {
        g_rollback.frames_ahead = gekko_frames_ahead(g_rollback.gekko);
    }
    if (!g_rollback_gameplay_ready.load(std::memory_order_acquire)) {
        destroy_rollback_coordinator();
    } else if (!advanced) {
        gekko_network_poll(g_rollback.gekko);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    (void)loaded_frame;
#endif
    return 1;
}

} // namespace dkr::runtime::netplay

extern "C" void dkr_netplay_character_select_enter(std::uint8_t* rdram,
                                                     recomp_context*) {
    dkr::runtime::netplay::enter_character_select(rdram);
}

extern "C" void dkr_netplay_character_select_lock(std::uint8_t* rdram,
                                                    recomp_context*) {
    dkr::runtime::netplay::enforce_character_select_roster(rdram);
}

extern "C" void dkr_netplay_character_select_ai_seed(
    std::uint8_t* rdram, recomp_context* context) {
    dkr::runtime::netplay::seed_character_select_ai(rdram, context);
}

extern "C" void dkr_custom_tracks_prepare_level(std::uint8_t*, recomp_context*);

extern "C" void dkr_netplay_gameplay_level_begin(std::uint8_t* rdram,
                                                    recomp_context* context) {
    // The only hook at load_level_game's entry, before alloc_displaylist_heap.
    // Custom tracks are offline-only, and retail levels keep the retail table.
    dkr_custom_tracks_prepare_level(rdram, context);
    dkr::runtime::netplay::begin_gameplay_level(rdram, context);
}

extern "C" void dkr_netplay_gameplay_level_ready(std::uint8_t* rdram,
                                                    recomp_context* context) {
    dkr::runtime::netplay::complete_gameplay_level(rdram, context);
}

extern "C" void dkr_netplay_gameplay_level_end(std::uint8_t*,
                                                  recomp_context*) {
    dkr::runtime::netplay::end_gameplay_level();
}

extern "C" void dkr_netplay_postrace_barrier(std::uint8_t* rdram,
                                                recomp_context*) {
    dkr::runtime::netplay::seal_race_finish(rdram, 1U);
}

extern "C" void dkr_netplay_adventure_finish_barrier(std::uint8_t* rdram,
                                                        recomp_context*) {
    dkr::runtime::netplay::seal_race_finish(rdram, 2U);
}

extern "C" void dkr_netplay_prepare_controller_init(std::uint8_t*,
                                                       recomp_context*) {
    dkr::runtime::netplay::prepare_controller_init();
}

extern "C" void dkr_netplay_resolve_authored_input_frame(
    std::uint8_t* rdram, recomp_context* context) {
    dkr::runtime::netplay::resolve_authored_input_frame(rdram, context);
}

extern "C" void dkr_netplay_authoritative_frame_commit(
    std::uint8_t* rdram, recomp_context*) {
    dkr::runtime::netplay::commit_authoritative_gameplay_frame(rdram);
}

extern "C" int dkr_netplay_drive_authored_tick(
    std::uint8_t* rdram, recomp_context* context) {
    return dkr::runtime::netplay::drive_authored_tick(rdram, context);
}

extern "C" int dkr_netplay_presentation_output_allowed() {
    return dkr::runtime::netplay::external_side_effects_allowed() ? 1 : 0;
}

extern "C" void dkr_netplay_presentation_random_begin(
    std::uint8_t*, recomp_context*) {
    dkr::runtime::netplay::begin_presentation_random_scope();
}

extern "C" void dkr_netplay_presentation_random_end(
    std::uint8_t*, recomp_context*) {
    dkr::runtime::netplay::end_presentation_random_scope();
}

extern "C" int dkr_netplay_presentation_random_range(
    std::uint8_t*, recomp_context* context) {
    return dkr::runtime::netplay::override_presentation_random_range(context)
               ? 1
               : 0;
}
