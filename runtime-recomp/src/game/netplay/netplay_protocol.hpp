#pragma once

#include "netplay_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <vector>

namespace dkr::runtime::netplay::protocol {

inline constexpr std::uint32_t kMagic = 0x444B524EU; // DKRN
// Leave headroom for DKR-R's 44-byte authenticated envelope and virtual-LAN
// tunnel headers.  Keeping the encrypted application datagram below 1044
// bytes avoids path fragmentation on conservative VPN/IPv6 routes.
inline constexpr std::size_t kMaximumDatagramBytes = 1000U;
// A Quick Join data channel is message-framed by SCTP and does not inherit
// the raw UDP path-MTU constraint. This limit fits one maximum authoritative
// DKR-R state plus its protocol headers while remaining far below WebRTC's
// practical message-size limits.
inline constexpr std::size_t kMaximumQuickJoinDatagramBytes = 14400U;

enum class MessageType : std::uint8_t {
    Hello = 1,
    HelloAck,
    Input,
    InputAck,
    StateHash,
    Ping,
    Pong,
    Disconnect,
    LobbyState,
    Ready,
    Loaded,
    Start,
    Reject,
    JoinPending,
    FrameCommit,
    StateSnapshot,
    StateRequest,
    RecoveryBegin,
    RecoveryAck,
    RecoveryResume,
    TransitionBarrier,
    StateAcknowledge,
    RollbackData,
    GameplayBarrier,
    GameplayHandoff,
    RollbackRecoveryRequest,
    FrameCorrectionAck,
    // Latest-wins Player 1 race-state stream. Retransmits use StateSnapshot
    // with the tagged frame id so lost fragments remain recoverable without
    // allowing stale live snapshots to build an outbound queue.
    LiveReplicaSnapshot,
    // A guest sends this when the next hash-chained authoritative commit is
    // missing. The host answers from its bounded commit history; without this
    // request a single lost UDP commit can park both simulations permanently.
    FrameCommitRequest,
    // Lockstep keeps its ordinary controller stream latency-first, but a
    // missing exact frame cannot be repaired over a disposable channel. The
    // host requests the first contiguous hole over reliable control traffic.
    InputRepairRequest,
    // A bounded contiguous controller-history batch returned over the
    // reliable/unordered authoritative lane. It uses InputBatch's payload
    // contract but remains distinguishable from best-effort live input.
    InputRepair,
    // Latest-wins report of the last retail simulation frame that actually
    // returned on a racer. Player 1 uses this advisory watermark to stop a
    // short scheduler/GPU hitch from becoming unbounded client frame debt.
    SimulationProgress,
    // Compact post-frame Player-1 camera/visual-heading sample. Guests use
    // their matching local frame history to remove plane heading drift without
    // blocking input, rewinding gameplay, or transmitting renderer state.
    RacerOrientation,
    // Idempotent lobby-control transactions. These are appended so existing
    // on-wire message identifiers remain stable for recorded diagnostics.
    ReadyRequest,
    ReadyAck,
    CountdownAck,
    LaunchPrepare,
    LaunchPrepareAck,
    LaunchCommit,
    LaunchCancel,
    // Terminal launch handshake. A racer arms an immutable launch on Commit,
    // acknowledges that durable state, and leaves the lobby only after the
    // matching Release. Release acknowledgements make packet loss harmless
    // while the host's network worker continues through game startup.
    LaunchCommitAck,
    LaunchRelease,
    LaunchReleaseAck,
    PreflightBegin,
    PreflightControlProbe,
    PreflightAuthorityProbe,
    PreflightRealtimeProbe,
    PreflightReplicaProbe,
    PreflightResult,
    OnlineSaveReady,
    OnlineSaveReadyAck,
    PreflightCheckpointProbe,
    // Advisory roster status; appended so existing gameplay IDs stay stable.
    // Older peers safely discard this unknown message and retain Ready/Ack.
    OnlineSaveStatus,
    // Authenticated, backend-specific lanes. Never enter the stable commit,
    // snapshot or transition ledgers. IDs are appended, not renumbered.
    ExperimentalControl,
    ExperimentalInput,
    ExperimentalRepair,
    // Match lifecycle only; never feed this into the rollback simulation.
    ExperimentalMatchEnd,
    ExperimentalCheckpoint,
    ExperimentalLoaded,
    ExperimentalSaveOffer,
    ExperimentalSaveRequest,
    ModOffer,
    ModControl,
    ModData,
};

struct ModOfferPayload {
    std::string manifest_hash;
    std::uint32_t manifest_bytes=0;
    bool operator==(const ModOfferPayload&)const=default;
};
std::vector<std::uint8_t> encode_mod_offer(const ModOfferPayload& payload);
bool decode_mod_offer(std::span<const std::uint8_t> bytes,ModOfferPayload& payload);

enum class MatchEndStage : std::uint8_t { Request, Stop, Returned, Resume, ResumeAck };
struct MatchEndPayload {
    std::uint64_t launch_hash = 0;
    MatchEndStage stage = MatchEndStage::Request;
    std::uint8_t player_slot = 0;
    std::string message;
    std::uint32_t save_generation = 0;
    std::uint64_t save_hash = 0;
    std::vector<std::uint8_t> save;
};
std::vector<std::uint8_t> encode_match_end(const MatchEndPayload& payload);
bool decode_match_end(std::span<const std::uint8_t> bytes, MatchEndPayload& payload);

struct Header {
    MessageType type = MessageType::Hello;
    std::uint64_t match_id = 0U;
    std::uint64_t sequence = 0U;
    std::uint32_t frame = 0U;
};

struct Datagram {
    Header header{};
    std::vector<std::uint8_t> payload;
};

struct InputBatch {
    std::uint32_t epoch = 0U;
    std::uint8_t player_slot = 0U;
    std::uint32_t first_frame = 0U;
    std::vector<PackedInput> inputs;
    // A guest's newest completed authored boundary rides with its ordinary
    // rolling input history.  Input packets are already sent redundantly and
    // at authored cadence, so this removes a separate pacing packet as a
    // single point of delay without changing the standalone progress
    // heartbeat used during a genuinely parked input stream.
    bool simulation_progress_present = false;
    // Zero identifies the current frontend/post-race input epoch. A nonzero
    // value identifies an exact gameplay scene. Protocol 41 introduced this
    // scoped meaning; older peers are rejected during compatibility setup.
    std::uint32_t simulation_scene_epoch = 0U;
    std::uint32_t simulation_completed_frame = 0U;
    // Per epoch/slot/frame revisions. Live and reliable repair can arrive in
    // either order; packet sequence alone cannot order overlapping histories.
    // An empty vector encodes initial revision 1 for every supplied sample.
    std::vector<std::uint32_t> revisions;
};

// Reports both the newest guest input observed and the first input frame that
// Player 1 has not accepted contiguously. The latter is the loss-recovery
// cursor: a later datagram can never hide an earlier hole in a racer's input
// stream.
struct InputAcknowledgePayload {
    std::uint32_t epoch = 0U;
    std::uint8_t player_slot = 0U;
    std::uint32_t newest_frame = 0U;
    std::uint32_t first_missing_frame = 0U;
};

struct InputRepairRequestPayload {
    std::uint32_t epoch = 0U;
    std::uint8_t player_slot = 0U;
    std::uint32_t first_missing_frame = 0U;
};

enum class GameplayHandoffStage : std::uint8_t {
    Request,
    Suspend,
};

// Frontend animation and asset-loading work is intentionally not part of the
// deterministic race state. This authenticated control edge stops the strict
// frontend input timeline before the first racer enters load_level_game, then
// assigns a fresh input epoch after every peer installs Player 1's baseline.
struct GameplayHandoffPayload {
    std::uint32_t current_input_epoch = 0U;
    std::uint32_t resume_input_epoch = 0U;
    std::uint32_t boundary_frame = 0U;
    std::uint32_t map = 0U;
    std::uint8_t player_slot = 0U;
    GameplayHandoffStage stage = GameplayHandoffStage::Request;
};

// GekkoNet's adapter payload is transported inside DirectSession's existing
// authenticated channel. The logical slots let Player 1 relay packets across
// the star topology without exposing a second UDP endpoint.
struct RollbackPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t transport_epoch = 0U;
    std::uint8_t source_slot = 0U;
    std::uint8_t target_slot = 0U;
    std::vector<std::uint8_t> bytes;
};

enum class GameplayBarrierStage : std::uint8_t {
    Prepare,
    Ready,
    Baseline,
    Arm,
    Armed,
    Go,
};

struct GameplayBarrierPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t map = 0U;
    std::uint32_t racer_count = 0U;
    std::uint8_t player_slot = 0U;
    GameplayBarrierStage stage = GameplayBarrierStage::Ready;
};

struct FrameCommitPayload {
    std::uint32_t epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint16_t revision = 0U;
    std::uint8_t occupied_mask = 0U;
    std::uint8_t predicted_mask = 0U;
    FrameInputs inputs{};
    std::uint64_t previous_hash = 0U;
    std::uint64_t commit_hash = 0U;

    bool operator==(const FrameCommitPayload&) const = default;
};

struct FrameCommitBatch {
    std::uint32_t generation = 0U;
    std::uint32_t correction_first = 0U;
    std::uint32_t correction_last = 0U;
    bool correction = false;
    std::vector<FrameCommitPayload> commits;
};

struct FrameCommitRequestPayload {
    std::uint32_t epoch = 0U;
    std::uint8_t player_slot = 0U;
    std::uint32_t first_missing_frame = 0U;
};

struct FrameCorrectionAcknowledgePayload {
    std::uint32_t epoch = 0U;
    std::uint32_t generation = 0U;
    std::uint8_t player_slot = 0U;
};

struct StateSnapshotChunk {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint64_t checksum = 0U;
    std::uint32_t total_size = 0U;
    std::uint16_t chunk_index = 0U;
    std::uint16_t chunk_count = 0U;
    std::vector<std::uint8_t> bytes;
};

struct StateRequestPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint16_t chunk_count = 0U;
    std::uint16_t missing_chunks = 0U;
};

struct StateAcknowledgePayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint64_t checksum = 0U;
    std::uint8_t player_slot = 0U;
};

struct RecoveryPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint8_t player_slot = 0U;
};

enum class TransitionBarrierStage : std::uint8_t {
    Begin,
    Acknowledge,
    Resume,
};

struct TransitionBarrierPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint8_t transition_kind = 0U;
    std::uint8_t player_slot = 0U;
    TransitionBarrierStage stage = TransitionBarrierStage::Begin;
};

struct SimulationProgressPayload {
    // Zero identifies frontend/post-race progress in input_epoch. A nonzero
    // value identifies progress in that exact authoritative gameplay scene.
    std::uint32_t scene_epoch = 0U;
    std::uint32_t input_epoch = 0U;
    std::uint32_t completed_frame = 0U;
    std::uint8_t player_slot = 0U;
};

inline constexpr std::size_t kMaximumRacerOrientationBytes = 160U;

struct RacerOrientationPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint32_t frame = 0U;
    std::vector<std::uint8_t> state;
};

struct HelloPayload {
    std::string display_name;
    CompatibilityManifest manifest{};
    std::array<std::uint8_t, 32U> friend_admission{};
};

struct HelloAckPayload {
    bool accepted = false;
    std::uint8_t player_slot = 0U;
    std::string message;
    // Every approved guest receives Player 1's isolated online Adventure
    // EEPROM. The containing datagram is peer-authenticated.
    std::vector<std::uint8_t> synchronized_save;
    // Authenticated host-owned settings offered only after a rejected join.
    // The client may use this metadata to select an already-imported ROM and
    // mirror Magic Codes; no ROM or executable content is transferred.
    std::optional<CompatibilityManifest> compatibility_offer;
    // A nonzero generation/hash identifies the isolated online EEPROM carried
    // by an accepted acknowledgement. It is never a single-player save path.
    std::uint32_t online_save_generation = 0U;
    std::uint64_t online_save_hash = 0U;
};

struct OnlineSaveReadyPayload {
    std::uint8_t player_slot = 0U;
    std::uint32_t generation = 0U;
    std::uint64_t hash = 0U;
};

struct OnlineSaveStatusPayload {
    std::uint64_t room_generation = 0U;
    std::uint32_t save_generation = 0U;
    std::uint64_t save_hash = 0U;
    std::uint8_t verified_mask = 0U;
};

struct LobbyPlayerPayload {
    bool occupied = false;
    bool ready = false;
    bool loaded = false;
    std::uint8_t slot = 0U;
    std::string display_name;
    Route route = Route::Unknown;
    std::uint16_t ping_ms = 0U;
    std::uint16_t jitter_ms = 0U;
    float packet_loss_percent = 0.0F;
};

struct LobbyStatePayload {
    std::uint64_t generation = 0U;
    RoomPhase phase = RoomPhase::Waiting;
    std::string room_name;
    Visibility visibility = Visibility::Private;
    Rules rules{};
    std::uint8_t input_delay_frames = 2U;
    bool countdown_active = false;
    std::uint32_t countdown_remaining_ms = 0U;
    std::uint32_t countdown_generation = 0U;
    std::array<LobbyPlayerPayload, kMaximumPlayers> players{};
};

struct StateHashPayload {
    std::uint32_t scene_epoch = 0U;
    std::uint8_t player_slot = 0U;
    std::uint64_t hash = 0U;
    std::uint64_t globals_hash = 0U;
    std::uint64_t roster_hash = 0U;
    std::uint64_t racers_hash = 0U;
    std::uint32_t racer_count = 0U;
    std::array<std::uint64_t, 10U> racer_hashes{};
};

struct LoadedPayload {
    std::uint8_t player_slot = 0U;
    std::uint64_t bootstrap_hash = 0U;
    std::uint32_t online_save_generation = 0U;
    std::uint64_t online_save_hash = 0U;
};

// Experimental-only extensions. Stable Loaded and save packet layouts retain
// their original wire contract. Every proof names its immutable launch.
struct ExperimentalLoadedPayload {
    std::uint64_t launch_hash = 0;
    LoadedPayload loaded{};
};
std::vector<std::uint8_t> encode_experimental_loaded(const ExperimentalLoadedPayload& payload);
bool decode_experimental_loaded(std::span<const std::uint8_t> bytes, ExperimentalLoadedPayload& payload);

struct StartPayload {
    std::uint8_t stage = 0U;
    LaunchDescriptor descriptor{};
    std::uint64_t descriptor_hash = 0U;

    bool operator==(const StartPayload&) const = default;
};

struct ReadyRequestPayload {
    std::uint32_t request_id = 0U;
    std::uint8_t player_slot = 0U;
    bool ready = false;
};

struct ReadyAckPayload {
    std::uint32_t request_id = 0U;
    std::uint8_t player_slot = 0U;
    bool ready = false;
    std::uint64_t lobby_generation = 0U;
};

struct CountdownAckPayload {
    std::uint32_t countdown_generation = 0U;
    std::uint8_t player_slot = 0U;
};

struct LaunchPreparePayload {
    std::uint32_t launch_epoch = 0U;
    std::uint64_t lobby_generation = 0U;
    std::uint32_t countdown_generation = 0U;
    StartPayload start{};
};

struct LaunchPrepareAckPayload {
    std::uint32_t launch_epoch = 0U;
    std::uint8_t player_slot = 0U;
    bool accepted = false;
};

struct LaunchCommitPayload {
    std::uint32_t launch_epoch = 0U;
};

struct LaunchCommitAckPayload {
    std::uint32_t launch_epoch = 0U;
    std::uint8_t player_slot = 0U;
};

struct LaunchReleasePayload {
    std::uint32_t launch_epoch = 0U;
};

struct LaunchReleaseAckPayload {
    std::uint32_t launch_epoch = 0U;
    std::uint8_t player_slot = 0U;
};

struct LaunchCancelPayload {
    std::uint32_t launch_epoch = 0U;
};

struct PreflightBeginPayload {
    std::uint32_t test_id = 0U;
    std::uint16_t duration_ms = 0U;
};

struct PreflightProbePayload {
    std::uint32_t test_id = 0U;
    std::uint32_t sequence = 0U;
    std::uint64_t sent_time_us = 0U;
    bool echo = false;
    std::vector<std::uint8_t> padding;
};

struct PreflightResultPayload {
    std::uint32_t test_id = 0U;
    std::uint8_t player_slot = 0U;
    std::uint8_t score = 0U;
    std::uint16_t p95_rtt_ms = 0U;
    std::uint16_t jitter_ms = 0U;
    std::uint16_t loss_tenths_percent = 0U;
    std::uint16_t late_tenths_percent = 0U;
    bool queues_drained = false;
};

std::vector<std::uint8_t> encode(
    const Datagram& datagram,
    std::size_t maximum_datagram_bytes = kMaximumDatagramBytes);
bool decode(std::span<const std::uint8_t> bytes, Datagram& datagram,
            std::string& error,
            std::size_t maximum_datagram_bytes = kMaximumDatagramBytes);
std::vector<std::uint8_t> encode_input_batch(const InputBatch& batch);
bool decode_input_batch(std::span<const std::uint8_t> bytes,
                        InputBatch& batch, std::string& error);
std::vector<std::uint8_t> encode_input_acknowledge(
    const InputAcknowledgePayload& acknowledgement);
bool decode_input_acknowledge(std::span<const std::uint8_t> bytes,
                              InputAcknowledgePayload& acknowledgement);
std::vector<std::uint8_t> encode_input_repair_request(
    const InputRepairRequestPayload& request);
bool decode_input_repair_request(
    std::span<const std::uint8_t> bytes,
    InputRepairRequestPayload& request);
std::vector<std::uint8_t> encode_gameplay_handoff(
    const GameplayHandoffPayload& handoff);
bool decode_gameplay_handoff(std::span<const std::uint8_t> bytes,
                             GameplayHandoffPayload& handoff);
std::vector<std::uint8_t> encode_rollback_payload(
    const RollbackPayload& payload);
bool decode_rollback_payload(std::span<const std::uint8_t> bytes,
                             RollbackPayload& payload,
                             std::string& error);
std::vector<std::uint8_t> encode_gameplay_barrier(
    const GameplayBarrierPayload& barrier);
bool decode_gameplay_barrier(std::span<const std::uint8_t> bytes,
                             GameplayBarrierPayload& barrier);
std::uint64_t frame_commit_hash(std::uint64_t match_id,
                                const FrameCommitPayload& commit);
std::vector<std::uint8_t> encode_frame_commit_batch(
    const FrameCommitBatch& batch);
bool decode_frame_commit_batch(std::span<const std::uint8_t> bytes,
                               FrameCommitBatch& batch,
                               std::string& error);
std::vector<std::uint8_t> encode_frame_commit_request(
    const FrameCommitRequestPayload& request);
bool decode_frame_commit_request(std::span<const std::uint8_t> bytes,
                                 FrameCommitRequestPayload& request);
std::vector<std::uint8_t> encode_frame_correction_acknowledge(
    const FrameCorrectionAcknowledgePayload& acknowledgement);
bool decode_frame_correction_acknowledge(
    std::span<const std::uint8_t> bytes,
    FrameCorrectionAcknowledgePayload& acknowledgement);
std::vector<std::uint8_t> encode_state_snapshot_chunk(
    const StateSnapshotChunk& chunk,
    std::size_t maximum_datagram_bytes = kMaximumDatagramBytes);
bool decode_state_snapshot_chunk(std::span<const std::uint8_t> bytes,
                                 StateSnapshotChunk& chunk,
                                 std::string& error);
std::vector<std::uint8_t> encode_state_request(
    const StateRequestPayload& request);
bool decode_state_request(std::span<const std::uint8_t> bytes,
                          StateRequestPayload& request);
std::vector<std::uint8_t> encode_state_acknowledge(
    const StateAcknowledgePayload& acknowledgement);
bool decode_state_acknowledge(std::span<const std::uint8_t> bytes,
                              StateAcknowledgePayload& acknowledgement);
std::vector<std::uint8_t> encode_recovery(const RecoveryPayload& recovery);
bool decode_recovery(std::span<const std::uint8_t> bytes,
                     RecoveryPayload& recovery);
std::vector<std::uint8_t> encode_transition_barrier(
    const TransitionBarrierPayload& transition);
bool decode_transition_barrier(std::span<const std::uint8_t> bytes,
                               TransitionBarrierPayload& transition);
std::vector<std::uint8_t> encode_simulation_progress(
    const SimulationProgressPayload& progress);
bool decode_simulation_progress(std::span<const std::uint8_t> bytes,
                                SimulationProgressPayload& progress);
std::vector<std::uint8_t> encode_racer_orientation(
    const RacerOrientationPayload& orientation);
bool decode_racer_orientation(std::span<const std::uint8_t> bytes,
                              RacerOrientationPayload& orientation);
std::vector<std::uint8_t> encode_hello(const HelloPayload& payload);
bool decode_hello(std::span<const std::uint8_t> bytes, HelloPayload& payload,
                  std::string& error);
std::vector<std::uint8_t> encode_hello_ack(const HelloAckPayload& payload);
bool decode_hello_ack(std::span<const std::uint8_t> bytes,
                      HelloAckPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_online_save_ready(
    const OnlineSaveReadyPayload& payload);
bool decode_online_save_ready(std::span<const std::uint8_t> bytes,
                              OnlineSaveReadyPayload& payload,
                              std::string& error);
std::vector<std::uint8_t> encode_online_save_status(const OnlineSaveStatusPayload& payload);
bool decode_online_save_status(std::span<const std::uint8_t> bytes,
                               OnlineSaveStatusPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_lobby_state(const LobbyStatePayload& payload);
bool decode_lobby_state(std::span<const std::uint8_t> bytes,
                        LobbyStatePayload& payload, std::string& error);
std::vector<std::uint8_t> encode_state_hash(const StateHashPayload& payload);
bool decode_state_hash(std::span<const std::uint8_t> bytes,
                       StateHashPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_loaded(const LoadedPayload& payload);
bool decode_loaded(std::span<const std::uint8_t> bytes,
                   LoadedPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_start(const StartPayload& payload);
bool decode_start(std::span<const std::uint8_t> bytes, StartPayload& payload,
                  std::string& error);
std::vector<std::uint8_t> encode_ready_request(
    const ReadyRequestPayload& payload);
bool decode_ready_request(std::span<const std::uint8_t> bytes,
                          ReadyRequestPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_ready_ack(const ReadyAckPayload& payload);
bool decode_ready_ack(std::span<const std::uint8_t> bytes,
                      ReadyAckPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_countdown_ack(
    const CountdownAckPayload& payload);
bool decode_countdown_ack(std::span<const std::uint8_t> bytes,
                          CountdownAckPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_launch_prepare(
    const LaunchPreparePayload& payload);
bool decode_launch_prepare(std::span<const std::uint8_t> bytes,
                           LaunchPreparePayload& payload, std::string& error);
std::vector<std::uint8_t> encode_launch_prepare_ack(
    const LaunchPrepareAckPayload& payload);
bool decode_launch_prepare_ack(std::span<const std::uint8_t> bytes,
                               LaunchPrepareAckPayload& payload,
                               std::string& error);
std::vector<std::uint8_t> encode_launch_commit(
    const LaunchCommitPayload& payload);
bool decode_launch_commit(std::span<const std::uint8_t> bytes,
                          LaunchCommitPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_launch_commit_ack(
    const LaunchCommitAckPayload& payload);
bool decode_launch_commit_ack(std::span<const std::uint8_t> bytes,
                              LaunchCommitAckPayload& payload,
                              std::string& error);
std::vector<std::uint8_t> encode_launch_release(
    const LaunchReleasePayload& payload);
bool decode_launch_release(std::span<const std::uint8_t> bytes,
                           LaunchReleasePayload& payload, std::string& error);
std::vector<std::uint8_t> encode_launch_release_ack(
    const LaunchReleaseAckPayload& payload);
bool decode_launch_release_ack(std::span<const std::uint8_t> bytes,
                               LaunchReleaseAckPayload& payload,
                               std::string& error);
std::vector<std::uint8_t> encode_launch_cancel(
    const LaunchCancelPayload& payload);
bool decode_launch_cancel(std::span<const std::uint8_t> bytes,
                           LaunchCancelPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_preflight_begin(
    const PreflightBeginPayload& payload);
bool decode_preflight_begin(std::span<const std::uint8_t> bytes,
                            PreflightBeginPayload& payload, std::string& error);
std::vector<std::uint8_t> encode_preflight_probe(
    const PreflightProbePayload& payload);
bool decode_preflight_probe(std::span<const std::uint8_t> bytes,
                            PreflightProbePayload& payload, std::string& error);
std::vector<std::uint8_t> encode_preflight_result(
    const PreflightResultPayload& payload);
bool decode_preflight_result(std::span<const std::uint8_t> bytes,
                             PreflightResultPayload& payload,
                             std::string& error);

} // namespace dkr::runtime::netplay::protocol
