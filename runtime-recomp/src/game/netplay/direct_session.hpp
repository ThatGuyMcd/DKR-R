#pragma once

#include "experimental_network.hpp"

#include "datagram_socket.hpp"
#include "../atomic_snapshot.hpp"
#include "session_transport.hpp"
#include "latency_metrics.hpp"
#include "netplay_lobby.hpp"
#include "netplay_protocol.hpp"
#include "netplay_timeline.hpp"
#include "replay_recorder.hpp"
#include "social_executor.hpp"
#include "sequence_window.hpp"
#include "secure_channel.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dkr::runtime::netplay {

enum class ConnectionState : std::uint8_t {
    Offline,
    Hosting,
    Connecting,
    AwaitingApproval,
    Lobby,
    Loading,
    Running,
    Failed,
};

enum class LaunchStage : std::uint8_t {
    Idle,
    Countdown,
    Preparing,
    Committing,
    Releasing,
};

struct PendingJoinView {
    std::uint64_t request_id = 0U;
    std::string display_name;
    bool compatible = false;
    std::string compatibility;
};

struct ConnectionTestResultView {
    bool valid = false;
    std::uint8_t player_slot = 0U;
    std::uint8_t score = 0U;
    std::uint16_t p95_rtt_ms = 0U;
    std::uint16_t jitter_ms = 0U;
    float loss_percent = 0.0F;
    float late_percent = 0.0F;
    bool queues_drained = false;
};

struct SessionView {
    ConnectionState state = ConnectionState::Offline;
    Room room{};
    bool host = false;
    std::uint8_t local_slot = 0U;
    std::uint16_t local_port = 0U;
    std::string invite;
    std::string status;
    bool rollback_certified = false;
    std::uint32_t last_verified_frame = 0U;
    std::uint32_t last_authoritative_frame = 0U;
    std::uint32_t authoritative_corrections = 0U;
    std::uint8_t input_delay_frames = 0U;
    ConnectionMethod method = ConnectionMethod::Lan;
    std::uint64_t match_id = 0U;
    bool lobby_locked = false;
    std::uint16_t network_rtt_ms = 0U;
    std::uint16_t network_jitter_ms = 0U;
    float network_loss_percent = 0.0F;
    std::uint32_t input_stalls = 0U;
    std::uint32_t longest_input_stall_ms = 0U;
    std::uint64_t packets_sent = 0U;
    std::uint64_t packets_received = 0U;
    std::uint64_t join_packets_recognized = 0U;
    std::uint64_t join_packets_stale = 0U;
    std::uint64_t join_packets_auth_rejected = 0U;
    std::uint64_t join_packets_manifest_rejected = 0U;
    std::uint64_t join_packets_accepted = 0U;
    std::size_t admission_packets_queued = 0U;
    std::size_t outbound_queue_high_water = 0U;
    std::size_t rollback_queue_high_water = 0U;
    std::uint64_t transport_queue_failures = 0U;
    std::size_t control_transport_buffered_bytes = 0U;
    std::size_t authority_transport_buffered_bytes = 0U;
    std::size_t realtime_transport_buffered_bytes = 0U;
    std::size_t replica_transport_buffered_bytes = 0U;
    std::uint64_t host_backpressure_events = 0U;
    std::uint32_t maximum_peer_frame_debt = 0U;
    std::uint32_t recovering_peer_count = 0U;
    bool peer_progress_known = false;
    std::uint32_t oldest_peer_progress_age_ms = 0U;
    std::uint64_t commit_repair_requests_sent = 0U;
    std::uint64_t commit_repair_requests_received = 0U;
    std::uint64_t commit_repair_batches_sent = 0U;
    std::uint64_t late_inputs_discarded = 0U;
    std::uint64_t refreshed_input_samples = 0U;
    std::uint64_t stale_input_revisions = 0U;
    std::uint64_t prediction_limit_waits = 0U;
    std::uint32_t local_input_echo_ms = 0U;
    std::uint32_t local_input_consume_ms = 0U;
    std::uint64_t measured_input_echoes = 0U;
    std::uint64_t live_replica_requests_sent = 0U;
    std::uint64_t live_replica_request_misses = 0U;
    std::uint64_t live_replica_window_rejections = 0U;
    std::uint64_t live_replica_decode_failures = 0U;
    bool recovering = false;
    std::uint32_t local_input_submitted_frame = 0U;
    PackedInput local_input_submitted{};
    std::uint32_t host_input_accepted_frame = 0U;
    std::uint32_t authoritative_input_frame = 0U;
    FrameInputs authoritative_inputs{};
    bool authoritative_inputs_valid = false;
    std::uint16_t authoritative_input_revision = 0U;
    std::uint8_t authoritative_predicted_mask = 0U;
    std::uint32_t input_corrections = 0U;
    // Timeline identity for phase-aware diagnostics. A frame number from a
    // retired frontend/input epoch must never be compared with the active
    // gameplay or post-race cursor.
    std::uint32_t input_epoch = 0U;
    std::uint32_t scene_epoch = 0U;
    // Monotonic notification token for authored commits and completed-frame
    // watermarks.  The game thread can wait for a real change instead of
    // polling the session mutex on a fixed millisecond cadence.
    std::uint64_t simulation_wake_generation = 0U;
    bool launch_countdown_active = false;
    std::uint32_t launch_countdown_remaining_ms = 0U;
    std::uint32_t launch_countdown_generation = 0U;
    LaunchStage launch_stage = LaunchStage::Idle;
    bool connection_test_active = false;
    std::uint32_t connection_test_id = 0U;
    std::uint32_t connection_test_remaining_ms = 0U;
    std::uint32_t connection_test_result_generation = 0U;
    std::array<ConnectionTestResultView, kMaximumPlayers>
        connection_test_results{};
    std::optional<CompatibilityManifest> compatibility_sync_offer;
    bool local_online_save_ready = false;
    std::array<bool, kMaximumPlayers> online_save_ready{};
    std::vector<PendingJoinView> pending_joins;
    std::size_t checkpoint_transport_buffered_bytes = 0U;
    std::size_t pending_outbound_bytes = 0U;
    std::uint32_t oldest_outbound_age_ms = 0U;
    std::uint64_t duplicate_retries_coalesced = 0U;
    std::uint64_t checkpoint_encoding_cache_hits = 0U;
    std::uint64_t maximum_network_pump_us = 0U;
    LatencySummary input_echo_latency{}, input_consume_latency{}, input_enqueue_latency{};
    std::uint64_t predicted_local_samples = 0, mismatched_local_samples = 0;
    std::uint64_t superseded_local_samples = 0;
    // Rows are player slots, columns are the seven application send lanes.
    std::array<std::array<QueueLaneSummary, 7>, kMaximumPlayers> outbound_lanes{};
    bool owned_match_ending = false;
};

// Fresh O(players) state for the authored scheduler, deliberately excluding
// strings, room copies, transport locks, and outbound queue scans.
struct SessionPacingView {
    ConnectionState state = ConnectionState::Offline;
    std::uint16_t network_rtt_ms = 0, network_jitter_ms = 0;
    std::uint32_t authoritative_input_frame = 0, input_epoch = 0;
    std::uint64_t simulation_wake_generation = 0;
};

struct RuntimeSessionView {
    ConnectionState state = ConnectionState::Offline;
    bool active = false;
    bool running = false;
    bool host = false;
    std::uint8_t local_slot = 0U;
    HostControlPolicy host_control =
        HostControlPolicy::GuidedUntilCharacterSelect;
    std::optional<LaunchDescriptor> launch_descriptor;
    std::uint32_t input_epoch = 0U;
    std::uint32_t scene_epoch = 0U;
    std::uint32_t online_save_generation = 0U;
    std::uint64_t online_save_hash = 0U;
    std::string status;
    bool owned_match_ending = false;
};

struct RollbackPacket {
    std::uint32_t scene_epoch = 0U;
    std::uint8_t source_slot = 0U;
    std::vector<std::uint8_t> bytes;
};

// Real-time session barriers are polled by DKR's authored frame boundary.
// They must never sleep the game thread: doing so starves presentation and
// audio while the network worker is waiting for another peer.
enum class SessionPollResult : std::uint8_t {
    Pending,
    Ready,
    Failed,
};

enum class InputSynchronizationResult : std::uint8_t {
    Pending,
    Committed,
    AlreadyCommitted,
    Suspended,
    Failed,
};

// Completed-frame watermarks are scoped independently from the asynchronous
// presentation state. Frontend uses only the current authenticated input
// epoch; gameplay additionally requires an exact nonzero scene epoch.
enum class TimelineProgressScope : std::uint8_t {
    Frontend,
    Gameplay,
};

class DirectSession final {
public:
    using SaveInstaller = std::function<bool(
        std::uint64_t, std::span<const std::uint8_t>,
        std::filesystem::path&, std::string&)>;
    using SaveReader = std::function<bool(bool, std::uint64_t,
        std::vector<std::uint8_t>&, std::filesystem::path&, std::string&)>;
    DirectSession();
    ~DirectSession();
    DirectSession(const DirectSession&) = delete;
    DirectSession& operator=(const DirectSession&) = delete;

    void configure_manifest(const CompatibilityManifest& manifest);
    void configure_session_save(std::vector<std::uint8_t> canonical_save,
                                SaveInstaller installer, SaveReader reader = {});
    void configure_artifact_directory(std::filesystem::path directory);
    bool host(std::uint16_t port, std::string advertised_host,
              std::string room_name, ConnectionMethod method,
              std::string player_name, const Rules& rules,
              std::string& error);
    bool join(std::string_view invite, std::string player_name,
              std::string& error);
    bool join_friend_invite(std::string_view invite, std::string player_name,
                            const secure::Key& admission,
                            std::string& error);
    bool create_friend_admission(secure::Key& admission,
                                 std::chrono::seconds lifetime,
                                 std::string& error);
    void revoke_friend_admission(const secure::Key& admission);
    void disconnect(std::string_view reason = {});
    void pump();

    bool set_ready(bool ready, std::string& error);
    bool approve_join(std::uint64_t request_id, std::string& error);
    bool reject_join(std::uint64_t request_id, bool block_for_session,
                     std::string& error);
    bool set_lobby_locked(bool locked, std::string& error);
    bool kick_player(std::uint8_t slot, std::string& error);
    bool revoke_invitation(std::string& error);
    bool request_start(std::string& error);
    bool request_connection_test(std::string& error);
    bool consume_launch_request();
    void mark_game_loaded(std::uint64_t bootstrap_hash,
                          std::uint32_t online_save_generation,
                          std::uint64_t online_save_hash,std::uint64_t expected_launch_hash=0);
    void fail_runtime_start(std::string reason);
    bool wait_until_running(std::chrono::milliseconds timeout);
    bool synchronize_inputs(std::uint32_t frame, PackedInput local,
                            FrameInputs& inputs,
                            std::chrono::milliseconds timeout);
    InputSynchronizationResult synchronize_inputs_result(
        std::uint32_t frame, PackedInput local, FrameInputs& inputs,
        std::chrono::milliseconds timeout);
    bool authoritative_inputs_for(std::uint32_t frame, FrameInputs& inputs,
                                  std::uint16_t& revision) const;
    std::uint32_t contiguous_authoritative_commits(
        std::uint32_t first_frame, std::uint32_t maximum_count) const;
    // Advance a guest across already-received immutable commits after a
    // complete Player-1 start-of-frame snapshot has replaced the skipped
    // simulation state. Every skipped hash link is verified before the
    // session cursor moves.
    bool fast_forward_authoritative_commits(
        std::uint32_t next_frame, std::string& error);
    // Pins epoch/ledger across a transactional game-thread installer. The
    // installer must not call DirectSession; Pending never calls it.
    SessionPollResult install_fast_forward(
        std::uint32_t next_frame, const std::function<bool()>& installer,
        std::string& error);
    std::optional<std::uint32_t> native_completed_frame() const;
    std::uint64_t synchronization_progress() const;
    // Records a frame only after the retail game loop has returned. Guests
    // send this authenticated latest-wins watermark to Player 1; the host can
    // then pause before a short local hitch becomes unbounded simulation debt.
    void report_simulation_progress(
        std::uint32_t completed_frame,
        TimelineProgressScope scope = TimelineProgressScope::Gameplay);
    bool host_should_backpressure(std::uint32_t next_frame,
                                  std::uint32_t maximum_lead,
                                  TimelineProgressScope scope =
                                      TimelineProgressScope::Gameplay) const;
    // A parked authored tick waits briefly on the network worker's existing
    // condition variable instead of hot-spinning the outer game thread.
    // Notifications wake this early; the timeout only bounds lost wakeups.
    void wait_for_simulation_progress(
        std::uint64_t observed_generation,
        std::chrono::milliseconds timeout);
    std::optional<std::uint32_t> take_authoritative_input_correction();
    bool begin_gameplay_handoff(std::uint32_t boundary_frame,
                                std::uint32_t map, std::string& error);
    bool gameplay_handoff_suspended(std::uint32_t frame) const;
    bool complete_gameplay_handoff(std::uint32_t& resume_frame,
                                   std::string& error);
    bool send_rollback_packet(std::uint8_t target_slot,
                              std::span<const std::uint8_t> bytes);
    std::vector<RollbackPacket> take_rollback_packets();
    void take_rollback_packets(std::vector<RollbackPacket>& packets);
    bool publish_authoritative_state(std::uint32_t frame,
                                     std::span<const std::uint8_t> state,
                                     std::string& error);
    bool wait_authoritative_state(std::uint32_t frame,
                                  std::vector<std::uint8_t>& state,
                                  std::chrono::milliseconds timeout);
    SessionPollResult poll_authoritative_state(
        std::uint32_t frame, std::vector<std::uint8_t>& state,
        std::string& error);
    // During an active race Player 1 publishes portable authored-state samples
    // at the bounded live-replica cadence. Guests consume the newest usable
    // sample without blocking the renderer/audio thread. This is a lossy,
    // latest-wins stream; strict reliability is reserved for lifecycle state.
    bool publish_live_replica(std::uint32_t frame,
                              std::span<const std::uint8_t> state,
                              bool reliable_keyframe,
                              std::string& error);
    SessionPollResult poll_live_replica(
        std::uint32_t frame, std::vector<std::uint8_t>& state,
        std::string& error);
    SessionPollResult poll_latest_live_replica(
        std::uint32_t minimum_frame, std::uint32_t maximum_frame,
        std::uint32_t& state_frame, std::vector<std::uint8_t>& state,
        std::string& error);
    bool publish_racer_orientation(std::uint32_t frame,
                                   std::span<const std::uint8_t> state,
                                   std::string& error);
    SessionPollResult poll_latest_racer_orientation(
        std::uint32_t minimum_frame, std::uint32_t maximum_frame,
        std::uint32_t& state_frame, std::vector<std::uint8_t>& state,
        std::string& error);
    void begin_authoritative_phase();
    bool wait_gameplay_ready(std::uint32_t requested_map,
                             std::uint32_t resolved_map,
                             std::uint32_t racer_count,
                             std::chrono::milliseconds timeout,
                             std::string& error);
    SessionPollResult poll_gameplay_ready(
        std::uint32_t requested_map, std::uint32_t resolved_map,
        std::uint32_t racer_count, std::string& error);
    bool synchronize_gameplay_resume(std::uint32_t map,
                                     std::uint32_t racer_count,
                                     std::chrono::milliseconds timeout,
                                     std::string& error);
    SessionPollResult poll_gameplay_resume(
        std::uint32_t map, std::uint32_t racer_count, std::string& error);
    bool begin_finish_seal(std::uint32_t& frame,
                           std::uint8_t transition_kind,
                           std::string& error);
    bool wait_authoritative_acknowledgements(
        std::uint32_t frame, std::chrono::milliseconds timeout);
    SessionPollResult poll_authoritative_acknowledgements(
        std::uint32_t frame, std::string& error);
    bool complete_finish_seal(std::uint32_t frame, std::string& error);
    void end_authoritative_phase();
    void confirm_authoritative_state(std::uint32_t frame, bool corrected);
    void fail_authoritative_state(std::string reason);
    void submit_state_hash(std::uint32_t frame, std::uint64_t hash);
    void submit_state_digest(std::uint32_t frame, std::uint64_t hash,
                             std::uint64_t globals_hash,
                             std::uint64_t roster_hash,
                             std::uint64_t racers_hash,
                             std::uint32_t racer_count = 0U,
                             std::array<std::uint64_t, 10U> racer_hashes = {});
    bool request_rollback_recovery(std::uint32_t mismatch_frame,
                                   std::string_view subsystem);
    std::optional<std::uint32_t> recovery_frame() const;
    bool wait_recovery_complete(std::uint32_t frame,
                                std::chrono::milliseconds timeout);
    SessionPollResult poll_recovery_complete(std::uint32_t frame,
                                              std::string& error);
    bool synchronize_transition(std::uint32_t frame,
                                std::uint8_t transition_kind,
                                std::chrono::milliseconds timeout);
    SessionPollResult poll_transition(std::uint32_t frame,
                                      std::uint8_t transition_kind,
                                      std::string& error);

    SessionView view() const;
    SessionPacingView pacing_view() const;
    // UI-only, immutable worker publication: never waits on session/SDK locks.
    SessionView presentation_view() const;
    bool presentation_active() const;
    RuntimeSessionView runtime_view() const;
    std::optional<LaunchDescriptor> launch_descriptor() const;
    bool active() const;
    bool running() const;

    // Explicit application capability, installed before lobby admission. A
    // generic driver or an old executable cannot enable a live experiment.
    bool enable_owned_backend(bool available);
    bool owned_backend_available() const;
    bool owned_game_active() const;
    struct OwnedPacket {
        std::uint8_t source = 0;
        TransportTrafficClass traffic = TransportTrafficClass::Control;
        std::vector<std::uint8_t> bytes;
    };
    DatagramSendStatus send_owned_packet(std::uint8_t target,
        std::span<const std::uint8_t> bytes, TransportTrafficClass traffic,
        std::string& error);
    bool take_owned_packet(OwnedPacket& packet);
    bool owned_network_configuration(const secure::Key& incarnation,
        experimental::NetworkConfiguration& configuration, std::string& error) const;
    static PeerAddress owned_peer_address(std::uint8_t slot);
    // Retire an immutable experimental roster, not the retained lobby/keys.
    void request_owned_match_end(std::string reason);
    bool complete_owned_match_end(std::uint64_t launch_hash,
        std::span<const std::uint8_t> confirmed_save, std::string& error);

private:
    friend struct DirectSessionTestAccess;
    bool owned_backend_available_ = false;
    struct OwnedMatchEnd {
        std::uint64_t hash = 0;
        std::string message;
        bool runtime_returned = false;
        bool resume_prepared = false;
        bool resumed = false;
        std::chrono::steady_clock::time_point started{}, last_send{};
    };
    std::optional<OwnedMatchEnd> owned_match_end_;
    std::uint64_t retired_owned_generation_ = 0;
    std::chrono::steady_clock::time_point owned_last_host_packet_{};
    void begin_owned_match_end_locked(std::string reason);
    void service_owned_match_end_locked(std::chrono::steady_clock::time_point now);
    bool handle_owned_match_end_locked(std::uint64_t sender, const protocol::Datagram& packet);
    void remove_owned_peer_locked(std::uint64_t sender, std::string reason);
    std::deque<OwnedPacket> owned_inbound_;
    std::size_t owned_inbound_bytes_ = 0;
    bool receive_owned_packet_locked(std::uint8_t source, const protocol::Datagram& packet);
    void send_loaded_locked();
    void begin_countdown_locked();
    enum class SaveJobKind { Join, Resume, Resync, HostSeal };
    struct PeerRecord;
    struct SaveResult {bool ok=false;std::vector<std::uint8_t> bytes;std::filesystem::path path;std::string error;};
    struct SaveJob {
        SaveJobKind kind;std::uint64_t token,match,hash,launch;std::uint32_t generation;std::uint8_t slot;
        std::chrono::steady_clock::time_point started;
        std::future<SaveResult> result;
    };
    bool queue_save_locked(SaveJobKind kind, std::vector<std::uint8_t> bytes,
        std::uint32_t generation,std::uint64_t hash,std::uint8_t slot,std::uint64_t launch=0);
    void service_save_jobs_locked();
    void offer_online_save_locked(PeerRecord& peer);
    SessionView view_locked() const;
    enum class RecoveryStage : std::uint8_t {
        Idle,
        Scheduled,
        SnapshotReady,
        AwaitingResume,
        Completed,
    };

    enum class AuthorityLifecycle : std::uint8_t {
        Inactive,
        Racing,
        SealingFinish,
        PostRace,
    };

    struct PeerRecord {
        bool active = false;
        std::uint64_t sender_id = 0U;
        PeerAddress address{};
        std::uint8_t slot = 0U;
        secure::Key key{};
        std::uint64_t next_sequence = 1U;
        std::chrono::steady_clock::time_point last_ping{};
        std::uint64_t ping_token = 0U;
        std::array<std::uint64_t, 4U> pending_ping_tokens{};
        std::uint32_t pings_sent = 0U;
        std::uint32_t pings_received = 0U;
        double rtt_ms = 0.0;
        double jitter_ms = 0.0;
        float loss_percent = 0.0F;
        bool requires_save_sync = false;
        bool online_save_ready = false;
        bool owned_runtime_returned = false;
        bool owned_lobby_resumed = false;
        std::chrono::steady_clock::time_point owned_last_packet{};
        std::array<double, 32U> rtt_samples{};
        std::array<std::chrono::steady_clock::time_point, 32U> rtt_sample_times{};
        std::size_t rtt_sample_count = 0U;
        std::size_t rtt_sample_cursor = 0U;
        std::uint32_t last_ready_request_id = 0U;
    };

    struct PendingRecord {
        bool active = false;
        std::uint64_t sender_id = 0U;
        PeerAddress address{};
        secure::Key key{};
        std::uint64_t next_sequence = 1U;
        secure::Key client_public{};
        std::string display_name;
        CompatibilityManifest manifest{};
        std::chrono::steady_clock::time_point first_seen{};
        std::chrono::steady_clock::time_point last_seen{};
        std::chrono::steady_clock::time_point last_acknowledgement{};
    };

    struct FriendAdmissionRecord {
        secure::Key capability{};
        std::chrono::steady_clock::time_point expires{};
        bool active = false;
    };

    struct HashFrame {
        std::array<std::uint64_t, kMaximumPlayers> values{};
        std::array<std::uint64_t, kMaximumPlayers> globals{};
        std::array<std::uint64_t, kMaximumPlayers> roster{};
        std::array<std::uint64_t, kMaximumPlayers> racers{};
        std::array<std::uint32_t, kMaximumPlayers> racer_counts{};
        std::array<std::array<std::uint64_t, 10U>, kMaximumPlayers>
            racer_details{};
        std::array<bool, kMaximumPlayers> present{};
    };

    struct SnapshotAssembly {
        std::uint32_t scene_epoch = 0U;
        std::uint64_t checksum = 0U;
        std::uint32_t total_size = 0U;
        std::uint16_t chunk_count = 0U;
        std::vector<std::vector<std::uint8_t>> chunks;
        std::vector<bool> present;
    };

    struct FrameCorrectionAssembly {
        std::uint32_t epoch = 0U;
        std::uint32_t generation = 0U;
        std::uint32_t first_frame = 0U;
        std::uint32_t last_frame = 0U;
        std::unordered_map<std::uint32_t, protocol::FrameCommitPayload> commits;
        std::chrono::steady_clock::time_point last_seen{};
    };

    struct FrameCorrectionTransmission {
        std::uint32_t epoch = 0U;
        std::uint32_t generation = 0U;
        std::uint32_t first_frame = 0U;
        std::uint32_t last_frame = 0U;
        std::vector<protocol::FrameCommitPayload> commits;
        std::array<bool, kMaximumPlayers> acknowledgements{};
        std::chrono::steady_clock::time_point last_send{};
    };

    struct OutboundPacket {
        PeerAddress destination{};
        protocol::MessageType type = protocol::MessageType::Ping;
        std::uint32_t frame = 0U;
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> retry_identity;
        std::chrono::steady_clock::time_point enqueued = std::chrono::steady_clock::now();
    };

    struct TransitionBarrierState {
        std::uint32_t scene_epoch = 0U;
        std::uint32_t frame = 0U;
        std::uint8_t kind = 0U;
        std::array<bool, kMaximumPlayers> acknowledgements{};
        bool resumed = false;
    };

    struct GameplayBarrierState {
        std::uint32_t scene_epoch = 0U;
        std::uint32_t map = 0U;
        std::uint32_t racer_count = 0U;
        std::array<bool, kMaximumPlayers> ready{};
        std::array<bool, kMaximumPlayers> armed{};
        bool epoch_synchronized = false;
        bool baseline_released = false;
        bool arm_announced = false;
        bool go_released = false;
    };

    struct GameplayHandoffState {
        std::uint32_t current_input_epoch = 0U;
        std::uint32_t resume_input_epoch = 0U;
        std::uint32_t boundary_frame = 0U;
        std::uint32_t local_suspend_frame = 0U;
        std::uint32_t map = 0U;
        bool local_load_started = false;
        bool suspended = false;
        bool armed = false;
        std::chrono::steady_clock::time_point deadline{};
    };

    // Completion belongs to the authenticated session, not to native objects
    // which may already have been destroyed by level teardown.
    struct CompletedRelease {
        protocol::MessageType type{};
        std::uint32_t epoch = 0U;
        std::uint32_t boundary = 0U; // map for Go; frame for other releases
        std::uint32_t detail = 0U;   // racer count / transition kind
        std::array<std::uint64_t, kMaximumPlayers> recipients{};
        std::chrono::steady_clock::time_point expires{};
    };
    void remember_release_locked(protocol::MessageType type,
        std::uint32_t epoch, std::uint32_t boundary, std::uint32_t detail);
    bool completed_release_locked(protocol::MessageType type,
        std::uint32_t epoch, std::uint32_t boundary, std::uint32_t detail,
        std::uint64_t recipient = 0U);

    bool parse_invite(std::string_view invite, PeerAddress& address,
                      std::uint64_t& match_id, std::uint64_t& host_sender_id,
                      ConnectionMethod& method,
                      secure::Key& host_public,
                      secure::Key& invitation_capability,
                      std::string& error) const;
    std::string make_quick_join_bootstrap() const;
    bool activate_quick_join_locked(std::string& error);
    std::string make_invite(std::string_view advertised_host) const;
    bool send_to(const PeerAddress& address, protocol::MessageType type,
                 std::span<const std::uint8_t> payload,
                 std::uint32_t frame = 0U);
    bool send_with_key(const PeerAddress& address, const secure::Key& key,
                       protocol::MessageType type,
                       std::span<const std::uint8_t> payload,
                       std::uint32_t frame = 0U);
    bool send_join_request();
    void broadcast(protocol::MessageType type,
                   std::span<const std::uint8_t> payload,
                   std::uint32_t frame = 0U);
    void pump_locked();
    void network_loop();
    void queue_diagnostics_locked(std::string_view reason = {});
    void flush_outbound_locked();
    bool enqueue_outbound(PeerAddress destination,
                          protocol::MessageType type,
                          std::vector<std::uint8_t> bytes,
                          std::uint32_t frame = 0U,
                          std::vector<std::uint8_t> retry_identity = {});
    void handle_packet(const PeerAddress& source, std::uint64_t sender_id,
                       const protocol::Datagram& packet);
    void handle_host_packet(const PeerAddress& source, std::uint64_t sender_id,
                            const protocol::Datagram& packet);
    void handle_client_packet(const protocol::Datagram& packet);
    void broadcast_lobby();
    protocol::LobbyStatePayload lobby_payload() const;
    void apply_lobby_payload(const protocol::LobbyStatePayload& payload);
    void apply_online_save_status_locked(const protocol::OnlineSaveStatusPayload& payload);
    bool all_active_players_acknowledged_locked(
        const std::array<bool, kMaximumPlayers>& acknowledgements) const;
    std::chrono::milliseconds launch_control_timeout_locked(
        std::chrono::milliseconds minimum,
        std::chrono::milliseconds maximum) const;
    void reset_launch_transaction_locked();
    void cancel_launch_locked(std::string_view reason);
    bool prepare_launch_locked(std::string& error);
    bool commit_launch_locked(const protocol::StartPayload& start,
                               std::uint32_t launch_epoch,
                               std::string& error);
    bool release_launch_locked(std::uint32_t launch_epoch,
                               std::string& error);
    void service_connection_test_locked(
        std::chrono::steady_clock::time_point now);
    void reset_connection_test_locked();
    PeerRecord* peer_by_sender(std::uint64_t sender_id);
    PeerRecord* peer_by_address(const PeerAddress& address);
    PendingRecord* pending_by_sender(std::uint64_t sender_id);
    PendingRecord* pending_by_request(std::uint64_t request_id);
    void handle_join_request(const PeerAddress& source,
                             std::span<const std::uint8_t> encrypted);
    bool approve_join_locked(std::uint64_t request_id, std::string& error);
    bool consume_friend_admission_locked(const secure::Key& admission);
    void clear_friend_admissions_locked();
    void retire_transport_locked();
    void expire_pending_joins();
    std::uint8_t occupied_players() const;
    protocol::InputBatch make_local_history_batch(
        std::uint32_t newest_frame, bool reliable_repair,
        std::optional<std::uint32_t> requested_first = std::nullopt) const;
    void send_local_history(std::uint32_t newest_frame,
                            bool reliable_repair = false,
                            std::optional<std::uint32_t> requested_first =
                                std::nullopt);
    bool ensure_local_input_for_repair_locked(std::uint32_t frame);
    void clear_input_history_locked();
    void store_local_input_locked(std::uint32_t frame, PackedInput input,
                                  bool physical_sample = true);
    void record_input_latency_locked(const protocol::FrameCommitPayload& commit,
                                     bool consumed);
    void send_commit_history(std::uint32_t newest_frame,
                             std::size_t history_count);
    void send_commit_history_to_peer(PeerRecord& peer,
                                     std::uint32_t first_frame,
                                     std::size_t maximum_count);
    void request_missing_commit_locked(std::uint32_t frame);
    void advertise_missing_inputs_locked();
    void reset_pending_commit_wait_locked();
    void send_frame_correction_locked();
    bool apply_frame_commit_locked(
        const protocol::FrameCommitPayload& commit, std::string& error);
    bool apply_frame_correction_locked(std::string& error);
    protocol::FrameCommitPayload make_frame_commit_locked(
        std::uint32_t frame, std::uint16_t revision);
    void rebuild_frame_commits_locked(std::uint32_t first_frame);
    void send_gameplay_handoff_locked(
        protocol::GameplayHandoffStage stage);
    bool adopt_gameplay_handoff_locked(
        const protocol::GameplayHandoffPayload& handoff,
        bool local_load_started, std::string& error);
    void reset_pending_input_locked();
    bool record_simulation_progress_locked(
        std::uint8_t player_slot, std::uint32_t progress_scene_epoch,
        std::uint32_t progress_input_epoch,
        std::uint32_t completed_frame);
    bool arm_gameplay_handoff_locked(std::uint32_t& resume_frame,
                                     std::string& error);
    void reset_input_delivery_tracking_locked(std::uint32_t first_frame);
    struct PreparedStateWire {
        std::vector<std::uint8_t> bytes;
        std::uint64_t checksum = 0;
    };
    bool send_authoritative_state(
        const PeerAddress* destination, std::uint32_t frame,
        std::span<const std::uint8_t> state,
        std::uint16_t requested_chunks = 0xFFFFU,
        protocol::MessageType message_type =
            protocol::MessageType::StateSnapshot,
        bool live_replica = false,
        const PreparedStateWire* prepared = nullptr);
    std::uint32_t authority_epoch_for_input_epoch(
        std::uint32_t input_epoch) const;
    std::uint32_t authority_epoch() const;
    std::uint64_t initial_commit_hash() const;
    bool validate_commit(const protocol::FrameCommitPayload& commit,
                         std::string& error) const;
    void evaluate_state_hash(std::uint32_t frame);
    void schedule_recovery_locked(std::uint32_t mismatch_frame,
                                  std::string subsystem);
    void write_determinism_artifact_locked(
        std::uint32_t frame, std::size_t mismatched_slot,
        std::string_view subsystem, const HashFrame& hashes);
    void send_connection_probes();
    void update_peer_metrics(PeerRecord& peer, std::uint64_t token);
    void fail_locked(std::string message);
    void synchronize_peer_slots();
    LaunchDescriptor make_launch_descriptor() const;
    bool validate_start_descriptor(const protocol::StartPayload& payload,
                                   std::string& error) const;
    bool accept_start_descriptor(const protocol::StartPayload& payload,
                                 std::string& error);
    bool save_sync_available() const;
    std::string admission_incompatibility(
        const CompatibilityManifest& candidate) const;
    std::uint64_t& outbound_sequence(const PeerAddress& address,
                                     const secure::Key& key);

    mutable std::mutex mutex_;
    std::shared_ptr<TransportReceiveSignal> receive_signal_ = std::make_shared<TransportReceiveSignal>();
    std::condition_variable& state_changed_ = receive_signal_->changed;
    std::unique_ptr<SessionTransport> transport_;
    CompatibilityManifest manifest_{};
    Lobby lobby_{};
    Room room_view_{};
    InputTimeline timeline_{1024U};
    std::array<PeerRecord, kMaximumPlayers> peers_{};
    std::array<PendingRecord, 8U> pending_joins_{};
    std::unordered_map<std::uint64_t,
                       std::array<ReceiveSequenceWindow, 5U>>
        received_sequences_;
    std::unordered_set<std::uint64_t> blocked_senders_;
    std::unordered_set<std::string> blocked_sources_;
    std::unordered_map<std::uint32_t, HashFrame> state_hashes_;
    std::array<std::uint64_t, kMaximumPlayers> bootstrap_hashes_{};
    std::array<bool, kMaximumPlayers> bootstrap_hash_present_{};
    std::deque<std::pair<std::uint32_t, PackedInput>> local_history_;
    struct LocalInputMetadata {
        std::uint32_t revision = 1U;
        std::chrono::steady_clock::time_point sampled_at{};
        bool echoed = false, consumed = false, enqueued = false;
    };
    std::unordered_map<std::uint32_t, LocalInputMetadata> local_input_metadata_;
    std::array<std::unordered_map<std::uint32_t, std::uint32_t>, kMaximumPlayers>
        received_input_revisions_;
    std::uint64_t refreshed_input_samples_ = 0U;
    std::uint64_t stale_input_revisions_ = 0U;
    std::uint64_t prediction_limit_waits_ = 0U;
    std::uint32_t local_input_echo_ms_ = 0U;
    std::uint32_t local_input_consume_ms_ = 0U;
    std::uint64_t measured_input_echoes_ = 0U;
    // Allocate once per session, not per input. Keep DirectSession small enough
    // for existing stack-owned instances on Windows' default stack budget.
    struct InputLatencyMetrics {
        LatencyHistogram echo, consume, enqueue;
    };
    std::unique_ptr<InputLatencyMetrics> input_latency_ = std::make_unique<InputLatencyMetrics>();
    std::uint64_t predicted_local_samples_ = 0, mismatched_local_samples_ = 0;
    std::uint64_t superseded_local_samples_ = 0;
    std::unordered_map<std::uint32_t, protocol::FrameCommitPayload>
        frame_commits_;
    std::deque<protocol::FrameCommitPayload> commit_history_;
    std::optional<FrameCorrectionAssembly> frame_correction_assembly_;
    std::optional<FrameCorrectionTransmission> frame_correction_transmission_;
    std::unordered_map<std::uint32_t,
                       std::pair<std::uint32_t,
                                 protocol::FrameCommitPayload>>
        deferred_frame_commits_;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>
        authoritative_states_;
    std::unordered_map<std::uint32_t,
                       std::array<bool, kMaximumPlayers>>
        authoritative_acknowledgements_;
    std::unordered_set<std::uint32_t> fully_acknowledged_states_;
    std::unordered_map<std::uint32_t, SnapshotAssembly>
        snapshot_assemblies_;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>
        live_replica_states_;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>>
        racer_orientation_states_;
    std::unordered_map<std::uint32_t, SnapshotAssembly>
        live_replica_assemblies_;
    std::optional<std::uint32_t> live_replica_keyframe_frame_;
    std::vector<std::uint8_t> live_replica_keyframe_state_;
    std::deque<OutboundPacket> critical_outbound_;
    // Reliable authored-input repairs must never sit behind the lossy,
    // low-latency input channel. WebRTC exposes those as independent data
    // channels, so keep their software queues independent as well.
    std::deque<OutboundPacket> repair_outbound_;
    std::deque<OutboundPacket> high_priority_outbound_;
    std::deque<OutboundPacket> commit_outbound_;
    std::deque<OutboundPacket> authority_outbound_;
    std::deque<OutboundPacket> normal_priority_outbound_;
    std::deque<OutboundPacket> bulk_outbound_;
    std::deque<RollbackPacket> rollback_inbound_;
    secure::Key key_{};
    secure::KeyPair host_key_pair_{};
    secure::KeyPair client_key_pair_{};
    secure::Key host_public_{};
    secure::Key invitation_capability_{};
    struct PendingInvitation {
        secure::Key capability{};
        secure::KeyPair key_pair{};
    };
    std::optional<PendingInvitation> pending_invitation_;
    secure::Key client_friend_admission_{};
    std::array<FriendAdmissionRecord, 16U> friend_admissions_{};
    PeerAddress host_address_{};
    bool quick_join_bootstrap_pending_ = false;
    ConnectionState state_ = ConnectionState::Offline;
    bool is_host_ = false;
    ConnectionMethod method_ = ConnectionMethod::Lan;
    bool launch_requested_ = false;
    bool launch_released_ = false;
    LaunchStage launch_stage_ = LaunchStage::Idle;
    bool launch_countdown_active_ = false;
    std::uint32_t launch_countdown_generation_ = 0U;
    std::uint32_t remote_countdown_remaining_ms_ = 0U;
    std::array<bool, kMaximumPlayers> countdown_acks_{};
    std::chrono::steady_clock::time_point launch_countdown_deadline_{};
    std::chrono::steady_clock::time_point remote_countdown_received_at_{};
    std::chrono::steady_clock::time_point last_countdown_ack_send_{};
    std::uint32_t next_launch_epoch_ = 1U;
    std::uint32_t pending_launch_epoch_ = 0U;
    std::optional<protocol::LaunchPreparePayload> pending_launch_prepare_;
    std::array<bool, kMaximumPlayers> launch_prepare_acks_{};
    std::chrono::steady_clock::time_point launch_prepare_deadline_{};
    std::chrono::steady_clock::time_point last_launch_prepare_send_{};
    std::optional<protocol::LaunchPreparePayload> remote_launch_prepare_;
    std::uint32_t committed_launch_epoch_ = 0U;
    std::array<bool, kMaximumPlayers> launch_commit_acks_{};
    std::chrono::steady_clock::time_point launch_commit_deadline_{};
    std::chrono::steady_clock::time_point launch_commit_rebroadcast_until_{};
    std::chrono::steady_clock::time_point last_launch_commit_send_{};
    std::array<bool, kMaximumPlayers> launch_release_acks_{};
    std::chrono::steady_clock::time_point launch_release_rebroadcast_until_{};
    std::chrono::steady_clock::time_point last_launch_release_send_{};
    bool connection_test_active_ = false;
    bool connection_test_draining_ = false;
    std::uint32_t next_connection_test_id_ = 1U;
    std::uint32_t connection_test_id_ = 0U;
    std::uint32_t terminal_connection_test_id_ = 0U;
    std::array<std::uint8_t, kMaximumPlayers> connection_test_result_acks_{};
    std::chrono::steady_clock::time_point connection_test_result_retry_until_{};
    std::chrono::steady_clock::time_point connection_test_result_last_send_{};
    std::chrono::steady_clock::time_point last_admission_response_{};
    std::uint32_t connection_test_result_generation_ = 0U;
    std::chrono::steady_clock::time_point connection_test_started_{};
    std::chrono::steady_clock::time_point connection_test_measurement_end_{};
    std::chrono::steady_clock::time_point connection_test_drain_end_{};
    std::array<std::chrono::steady_clock::time_point, 5U>
        connection_test_last_send_{};
    std::array<std::uint32_t, 5U> connection_test_next_sequence_{};
    std::array<std::array<std::uint32_t, 5U>, kMaximumPlayers> connection_test_sent_{};
    std::array<std::array<std::uint32_t, 5U>, kMaximumPlayers> connection_test_received_{};
    std::array<std::array<std::vector<double>, 5U>, kMaximumPlayers> connection_test_rtt_samples_{};
    std::array<std::array<std::unordered_set<std::uint32_t>, 5U>, kMaximumPlayers> connection_test_echoes_{};
    std::uint64_t connection_test_queue_failures_at_start_ = 0U;
    std::array<ConnectionTestResultView, kMaximumPlayers>
        connection_test_results_{};
    std::optional<CompatibilityManifest> compatibility_sync_offer_;
    bool local_loaded_ = false;
    std::uint64_t local_bootstrap_hash_ = 0U;
    bool run_signal_sent_ = false;
    std::optional<LaunchDescriptor> launch_descriptor_;
    std::uint8_t local_slot_ = 0U;
    std::uint8_t input_delay_ = 2U;
    std::uint64_t match_id_ = 0U;
    std::uint64_t sender_id_ = 0U;
    std::uint64_t host_sender_id_ = 0U;
    std::uint64_t next_sequence_ = 1U;
    std::string local_name_;
    std::string advertised_host_;
    std::string invite_;
    std::string status_;
    bool lobby_locked_ = false;
    bool authoritative_phase_active_ = false;
    AuthorityLifecycle authority_lifecycle_ = AuthorityLifecycle::Inactive;
    std::optional<std::uint32_t> finish_seal_frame_;
    RecoveryStage recovery_stage_ = RecoveryStage::Idle;
    std::uint32_t scene_epoch_ = 0U;
    // Separates Gekko packet timelines within one track. An authenticated
    // state repair increments this value before a fresh rollback coordinator
    // starts, so delayed datagrams from the discarded history are ignored.
    std::uint32_t rollback_transport_epoch_ = 1U;
    // Highest gameplay epoch authenticated from Player 1 during this session.
    // It prevents a delayed UDP Prepare from an earlier race from rolling a
    // client back after it has already adopted the current race identity.
    std::uint32_t last_host_scene_epoch_ = 0U;
    std::optional<std::uint32_t> recovery_frame_;
    std::optional<std::uint32_t> native_completed_frame_;
    std::optional<std::uint32_t> completed_recovery_frame_;
    std::uint32_t completed_recovery_epoch_ = 0U;
    std::array<bool, kMaximumPlayers> recovery_acks_{};
    bool recovery_resumed_ = false;
    std::chrono::steady_clock::time_point last_recovery_broadcast_{};
    std::chrono::steady_clock::time_point recovery_resume_until_{};
    std::optional<TransitionBarrierState> transition_barrier_;
    std::deque<CompletedRelease> completed_releases_;
    struct CheckpointEncoding {
        std::uint32_t epoch = 0U;
        std::uint32_t frame = 0U;
        std::vector<std::uint8_t> source;
        std::vector<std::uint8_t> wire;
    };
    std::optional<CheckpointEncoding> checkpoint_encoding_;
    std::uint64_t duplicate_retries_coalesced_ = 0U;
    std::uint64_t checkpoint_encoding_cache_hits_ = 0U;
    std::uint64_t maximum_network_pump_us_ = 0U;
    std::chrono::steady_clock::time_point last_transition_broadcast_{};
    std::chrono::steady_clock::time_point transition_resume_until_{};
    std::optional<GameplayBarrierState> gameplay_barrier_;
    std::chrono::steady_clock::time_point last_gameplay_barrier_broadcast_{};
    std::chrono::steady_clock::time_point gameplay_resume_until_{};
    std::chrono::steady_clock::time_point last_gameplay_ready_announcement_{};
    std::chrono::steady_clock::time_point last_gameplay_resume_ack_{};
    std::chrono::steady_clock::time_point gameplay_resume_ack_until_{};
    std::optional<GameplayHandoffState> gameplay_handoff_;
    std::chrono::steady_clock::time_point last_gameplay_handoff_send_{};
    std::chrono::steady_clock::time_point last_authoritative_request_{};
    std::uint32_t last_authoritative_request_frame_ = 0U;
    std::chrono::steady_clock::time_point last_live_replica_request_{};
    std::uint32_t last_live_replica_request_frame_ = 0U;
    std::optional<bool> desired_ready_;
    std::uint32_t next_ready_request_id_ = 1U;
    std::uint32_t pending_ready_request_id_ = 0U;
    std::chrono::steady_clock::time_point last_connect_request_{};
    std::chrono::steady_clock::time_point connect_started_{};
    std::chrono::steady_clock::time_point last_client_request_{};
    std::chrono::steady_clock::time_point last_lobby_broadcast_{};
    std::chrono::steady_clock::time_point failure_broadcast_until_{};
    std::chrono::steady_clock::time_point last_failure_broadcast_{};
    std::vector<std::uint8_t> failure_payload_;
    ReplayRecorder replay_;
    SocialExecutor replay_writer_;
    SocialExecutor transport_closer_;
    SocialExecutor save_executor_;
    bool save_executor_started_ = false;
    std::deque<SaveJob> save_jobs_;
    std::uint64_t save_job_token_ = 1;
    bool host_start_verified_ = false;
    std::array<std::chrono::steady_clock::time_point,kMaximumPlayers> last_save_offer_{};
    std::chrono::steady_clock::time_point last_save_request_{};
    bool transport_closer_started_ = false;
    bool replay_writer_started_ = false;
    std::vector<std::future<std::string>> replay_writes_;
    std::string replay_write_error_;
    std::filesystem::path artifact_directory_;
    std::vector<std::uint8_t> session_save_;
    SaveInstaller save_installer_;
    SaveReader save_reader_;
    std::uint32_t session_save_generation_ = 0U;
    bool local_online_save_ready_ = false;
    bool local_online_save_acknowledged_ = false;
    std::optional<protocol::OnlineSaveStatusPayload> online_save_status_;
    std::uint64_t local_runtime_save_hash_ = 0U;
    std::uint32_t local_runtime_save_generation_ = 0U;
    std::array<std::uint64_t, kMaximumPlayers> runtime_save_hashes_{};
    std::array<std::uint32_t, kMaximumPlayers> runtime_save_generations_{};
    std::array<bool, kMaximumPlayers> runtime_save_present_{};
    std::uint32_t last_verified_frame_ = 0U;
    std::uint32_t last_authoritative_frame_ = 0U;
    std::uint32_t authoritative_corrections_ = 0U;
    std::uint32_t consecutive_authoritative_frames_ = 0U;
    std::uint32_t next_commit_frame_ = 0U;
    std::uint32_t input_epoch_ = 1U;
    // Gameplay loading is an explicit protocol epoch boundary. Delayed UDP
    // datagrams from the retired frontend epoch are authenticated but stale,
    // not evidence that the current race has diverged.
    std::uint32_t retired_authority_epoch_ = 0U;
    std::uint32_t pending_authority_epoch_ = 0U;
    std::uint64_t stale_epoch_packets_ = 0U;
    std::uint64_t future_epoch_packets_ = 0U;
    std::uint32_t frame_correction_generation_ = 0U;
    std::uint32_t applied_frame_correction_generation_ = 0U;
    std::uint64_t last_consumed_commit_hash_ = 0U;
    // The retail input hook can be reached redundantly at the boundary after
    // an outer authored tick has already consumed that frame. Keep the exact
    // locally-consumed sample separate from the revisable rollback ledger so
    // the duplicate read is idempotent and can never observe a later commit
    // revision that was not used by the simulation.
    std::optional<std::uint32_t> last_consumed_input_frame_;
    FrameInputs last_consumed_inputs_{};
    std::optional<std::uint32_t> pending_input_correction_;
    std::uint32_t local_input_submitted_frame_ = 0U;
    PackedInput local_input_submitted_{};
    std::optional<std::uint32_t> pending_local_submission_frame_;
    std::chrono::steady_clock::time_point last_input_history_send_{};
    std::chrono::steady_clock::time_point last_commit_repair_request_{};
    std::chrono::steady_clock::time_point pending_commit_wait_started_{};
    std::optional<std::uint32_t> pending_commit_wait_frame_;
    bool prediction_failsafe_active_ = false;
    std::uint32_t host_input_accepted_frame_ = 0U;
    std::uint32_t host_input_first_missing_frame_ = 0U;
    std::array<std::uint32_t, kMaximumPlayers>
        peer_input_first_missing_frame_{};
    std::uint32_t authoritative_input_frame_ = 0U;
    FrameInputs authoritative_inputs_{};
    bool authoritative_inputs_valid_ = false;
    std::uint16_t authoritative_input_revision_ = 0U;
    std::uint8_t authoritative_predicted_mask_ = 0U;
    std::uint32_t input_corrections_ = 0U;
    std::uint64_t commit_repair_requests_sent_ = 0U;
    std::uint64_t commit_repair_requests_received_ = 0U;
    std::uint64_t commit_repair_batches_sent_ = 0U;
    std::uint64_t forced_prediction_frames_ = 0U;
    std::array<std::uint32_t, kMaximumPlayers>
        simulation_completed_frame_{};
    std::array<bool, kMaximumPlayers> simulation_progress_present_{};
    std::array<std::chrono::steady_clock::time_point, kMaximumPlayers>
        simulation_progress_time_{};
    std::chrono::steady_clock::time_point
        last_simulation_progress_datagram_{};
    std::uint64_t simulation_wake_generation_ = 1U;
    std::uint64_t late_inputs_discarded_ = 0U;
    std::uint64_t live_replica_requests_sent_ = 0U;
    std::uint64_t live_replica_request_misses_ = 0U;
    std::uint64_t live_replica_window_rejections_ = 0U;
    std::uint64_t live_replica_decode_failures_ = 0U;
    mutable bool host_backpressure_active_ = false;
    mutable std::uint64_t host_backpressure_events_ = 0U;
    mutable std::uint32_t maximum_peer_frame_debt_ = 0U;
    mutable std::uint32_t recovering_peer_count_ = 0U;
    std::uint64_t packets_sent_ = 0U;
    std::uint64_t packets_received_ = 0U;
    std::uint64_t bytes_sent_ = 0U;
    std::uint64_t bytes_received_ = 0U;
    std::uint64_t join_packets_recognized_ = 0U;
    std::uint64_t join_packets_stale_ = 0U;
    std::uint64_t join_packets_auth_rejected_ = 0U;
    std::uint64_t join_packets_manifest_rejected_ = 0U;
    std::uint64_t join_packets_accepted_ = 0U;
    std::uint64_t authority_checkpoints_sent_ = 0U;
    std::uint64_t authority_checkpoints_acknowledged_ = 0U;
    std::uint64_t authority_wait_timeouts_ = 0U;
    std::uint32_t input_stalls_ = 0U;
    std::uint32_t longest_input_stall_ms_ = 0U;
    std::uint16_t network_rtt_ms_ = 0U;
    std::uint16_t network_jitter_ms_ = 0U;
    float network_loss_percent_ = 0.0F;
    std::uint64_t outbound_packets_dropped_ = 0U;
    std::size_t outbound_queue_high_water_ = 0U;
    std::size_t rollback_queue_high_water_ = 0U;
    std::uint64_t transport_queue_failures_ = 0U;
    // Keep the worker last: its constructor starts network_loop immediately,
    // so every object the loop can observe must already be fully constructed.
    // This ordering is required by C++ on every platform; Linux exposed the
    // previous ordering as a startup race in DatagramSocket::is_open().
    std::atomic<bool> worker_stop_{false};
    bool worker_wake_ = false;
    AtomicSnapshot<const SessionView> presentation_view_;
    std::chrono::steady_clock::time_point last_view_publication_{};
    std::thread network_worker_;
};

} // namespace dkr::runtime::netplay
