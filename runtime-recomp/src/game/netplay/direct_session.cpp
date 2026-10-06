#include "direct_session.hpp"
#include "experimental_runtime_admission.hpp"
#include "online_lobby_policy.hpp"

#include "authoritative_state_codec.hpp"

#include "failure_recorder.hpp"
#include "netplay_pacing_policy.hpp"

#include "authoritative_state.hpp"
#include "../determinism_hash_policy.hpp"
#include "../performance_trace.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <thread>

namespace dkr::runtime::netplay {
namespace {

constexpr std::uint32_t kLiveReplicaFrameTag = 0x80000000U;
// Player 1 advances while a rollback client repairs a transient route outage.
// Retain over one minute of the immutable commit chain at DKR's 30 Hz authored
// cadence, comfortably beyond the bounded 30-second repair timeout below.
// Commits are small (four packed controller samples plus hashes), so this is a
// much safer recovery budget than discarding the chain after roughly 2.1 s.
constexpr std::size_t kCommitHistoryCapacity = 2048U;
// Reliable repair normally closes a Lockstep hole in one RTT. After this
// interval the UI changes from an ordinary wait to an explicit recovery
// status, but the session remains alive: a transient Wi-Fi/VPN outage must not
// become a permanent disconnect.
constexpr auto kPendingCommitRepairTimeout = std::chrono::seconds(8);

constexpr bool is_live_replica_frame(std::uint32_t frame) {
    return (frame & kLiveReplicaFrameTag) != 0U;
}

constexpr std::uint32_t live_replica_wire_frame(std::uint32_t frame) {
    return frame | kLiveReplicaFrameTag;
}

constexpr std::uint32_t live_replica_logical_frame(std::uint32_t frame) {
    return frame & ~kLiveReplicaFrameTag;
}

std::uint64_t random_u64() {
    std::random_device random;
    std::uint64_t value = 0U;
    for (int index = 0; index < 4; ++index) {
        value = (value << 16U) ^ static_cast<std::uint64_t>(random() & 0xFFFFU);
    }
    return value != 0U ? value : 1U;
}

std::string hex64(std::uint64_t value) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(16U, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = alphabet[value & 0xFU];
        value >>= 4U;
    }
    return result;
}

std::uint64_t steady_microseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t unix_seconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<std::uint8_t> encode_probe(std::uint64_t token) {
    std::vector<std::uint8_t> payload(8U);
    for (int index = 7; index >= 0; --index) {
        payload[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(token);
        token >>= 8U;
    }
    return payload;
}

bool decode_probe(std::span<const std::uint8_t> payload, std::uint64_t& token) {
    if (payload.size() != 8U) return false;
    token = 0U;
    for (std::uint8_t byte : payload) token = (token << 8U) | byte;
    return token != 0U;
}

std::optional<std::size_t> preflight_lane(protocol::MessageType type) {
    switch (type) {
    case protocol::MessageType::PreflightControlProbe:
        return 0U;
    case protocol::MessageType::PreflightAuthorityProbe:
        return 1U;
    case protocol::MessageType::PreflightRealtimeProbe:
        return 2U;
    case protocol::MessageType::PreflightReplicaProbe:
        return 3U;
    case protocol::MessageType::PreflightCheckpointProbe:
        return 4U;
    default:
        return std::nullopt;
    }
}

std::size_t receive_sequence_lane(protocol::MessageType type) {
    switch (type) {
    case protocol::MessageType::FrameCommit:
        return 0U; // reliable/unordered Player 1 simulation ledger
    case protocol::MessageType::Input:
    case protocol::MessageType::InputAck:
    case protocol::MessageType::RollbackData:
    case protocol::MessageType::ExperimentalInput:
    case protocol::MessageType::SimulationProgress:
    case protocol::MessageType::RacerOrientation:
    case protocol::MessageType::PreflightRealtimeProbe:
        return 1U; // redundant latency-sensitive simulation traffic
    case protocol::MessageType::InputRepairRequest:
    case protocol::MessageType::FrameCommitRequest:
    case protocol::MessageType::FrameCorrectionAck:
    case protocol::MessageType::Disconnect:
    case protocol::MessageType::Loaded:
    case protocol::MessageType::Start:
    case protocol::MessageType::ReadyRequest:
    case protocol::MessageType::ReadyAck:
    case protocol::MessageType::CountdownAck:
    case protocol::MessageType::LaunchPrepare:
    case protocol::MessageType::LaunchPrepareAck:
    case protocol::MessageType::LaunchCommit:
    case protocol::MessageType::LaunchCommitAck:
    case protocol::MessageType::LaunchRelease:
    case protocol::MessageType::LaunchReleaseAck:
    case protocol::MessageType::LaunchCancel:
    case protocol::MessageType::RecoveryBegin:
    case protocol::MessageType::RecoveryAck:
    case protocol::MessageType::RecoveryResume:
    case protocol::MessageType::TransitionBarrier:
    case protocol::MessageType::GameplayBarrier:
    case protocol::MessageType::GameplayHandoff:
    case protocol::MessageType::RollbackRecoveryRequest:
    case protocol::MessageType::PreflightBegin:
    case protocol::MessageType::PreflightControlProbe:
    case protocol::MessageType::PreflightResult:
    case protocol::MessageType::OnlineSaveReady:
    case protocol::MessageType::OnlineSaveReadyAck:
    case protocol::MessageType::ExperimentalControl:
    case protocol::MessageType::ExperimentalMatchEnd:
    case protocol::MessageType::ExperimentalLoaded:
    case protocol::MessageType::ExperimentalSaveOffer:
    case protocol::MessageType::ExperimentalSaveRequest:
    case protocol::MessageType::ModOffer:
    case protocol::MessageType::ModControl:
        return 2U; // lifecycle/control traffic
    case protocol::MessageType::StateHash:
    case protocol::MessageType::StateSnapshot:
    case protocol::MessageType::LiveReplicaSnapshot:
    case protocol::MessageType::StateRequest:
    case protocol::MessageType::StateAcknowledge:
    case protocol::MessageType::InputRepair:
    case protocol::MessageType::ExperimentalRepair:
    case protocol::MessageType::ExperimentalCheckpoint:
    case protocol::MessageType::PreflightAuthorityProbe:
    case protocol::MessageType::PreflightCheckpointProbe:
        return 3U; // authority/recovery traffic
    case protocol::MessageType::PreflightReplicaProbe:
        return 4U;
    default:
        return 4U; // lobby, admission and route probes
    }
}

} // namespace

DirectSession::DirectSession()
    : transport_(make_udp_session_transport()),
      network_worker_(&DirectSession::network_loop, this) {}

DirectSession::~DirectSession() {
    disconnect("The racer application closed.");
    {
        std::scoped_lock lock(mutex_);
        worker_stop_.store(true, std::memory_order_release);
    }
    state_changed_.notify_all();
    if (network_worker_.joinable()) network_worker_.join();
    save_executor_.stop();
    replay_writer_.stop();
    transport_closer_.stop();
}

void DirectSession::configure_manifest(const CompatibilityManifest& manifest) {
    std::scoped_lock lock(mutex_);
    if (state_ == ConnectionState::Offline) manifest_ = manifest;
}

void DirectSession::configure_session_save(
    std::vector<std::uint8_t> canonical_save, SaveInstaller installer, SaveReader reader) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Offline) return;
    session_save_ = std::move(canonical_save);
    save_installer_ = std::move(installer);
    save_reader_ = std::move(reader);
    ++save_job_token_;save_jobs_.clear();host_start_verified_=false;last_save_request_={};
    if (session_save_.size() == 512U) {
        manifest_.session_save_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(session_save_.data()),
            session_save_.size()));
        ++session_save_generation_;
        if (session_save_generation_ == 0U) ++session_save_generation_;
    } else {
        manifest_.session_save_hash = 0U;
        session_save_generation_ = 0U;
    }
    local_online_save_ready_ = false;
    local_online_save_acknowledged_ = false;
    online_save_status_.reset();
}

void DirectSession::retire_transport_locked() {
    pending_invitation_.reset();
    if (!transport_->quick_join()) {
        transport_->close();
        return;
    }
    if (!transport_closer_started_) {
        transport_closer_.start([] {}, [] {});
        transport_closer_started_ = true;
    }
    // SDK teardown can drain callbacks. Retire ownership immediately, but do
    // not make frontend SessionView readers wait for that drain under mutex_.
    auto retired = std::shared_ptr<SessionTransport>(std::move(transport_));
    transport_ = make_udp_session_transport();
    transport_closer_.post([retired] { retired->close(); });
}

bool DirectSession::queue_save_locked(SaveJobKind kind,std::vector<std::uint8_t> bytes,
    std::uint32_t generation,std::uint64_t hash,std::uint8_t slot,std::uint64_t launch) {
    if(!match_id_||!generation||!hash||bytes.size()!=512)return false;
    for(const auto& job:save_jobs_)if(job.token==save_job_token_&&job.match==match_id_&&
        job.kind==kind&&job.generation==generation&&job.hash==hash&&job.launch==launch)return true;
    if(save_jobs_.size()>=4||(!save_installer_&&kind!=SaveJobKind::HostSeal)||
       (kind==SaveJobKind::HostSeal&&!save_reader_))return false;
    if(!save_executor_started_) {save_executor_.start([]{},[]{});save_executor_started_=true;}
    auto completion=std::make_shared<std::promise<SaveResult>>();auto future=completion->get_future();
    const auto installer=save_installer_;const auto reader=save_reader_;const auto match=match_id_;
    // The storage worker owns copies only: never a session pointer or mutex.
    // Its serialized writes finish before a newer generation is installed.
    if(!save_executor_.post([completion,installer,reader,match,kind,bytes=std::move(bytes)]() mutable {
        SaveResult result;result.bytes=std::move(bytes);
        try {
            if(kind==SaveJobKind::HostSeal)result.ok=reader(true,match,result.bytes,result.path,result.error);
            else result.ok=installer(match,result.bytes,result.path,result.error);
        }catch(const std::exception& e){result.error=e.what();}
         catch(...){result.error="Online save storage failed unexpectedly.";}
        completion->set_value(std::move(result));
    }))return false;
    save_jobs_.push_back({kind,save_job_token_,match_id_,hash,launch,generation,slot,std::chrono::steady_clock::now(),std::move(future)});
    worker_wake_=true;state_changed_.notify_all();return true;
}

void DirectSession::offer_online_save_locked(PeerRecord& peer) {
    if(!peer.active||!save_sync_available()||lobby_.room().rules.synchronization!=SynchronizationMode::ExperimentalRollback||
       (state_!=ConnectionState::Hosting&&state_!=ConnectionState::Lobby)||launch_countdown_active_||
       (owned_match_end_&&!owned_match_end_->resumed))return;
    const auto now=std::chrono::steady_clock::now();
    if(last_save_offer_[peer.slot].time_since_epoch().count()&&now-last_save_offer_[peer.slot]<std::chrono::milliseconds(500))return;
    protocol::MatchEndPayload offer{match_id_,protocol::MatchEndStage::Resume,peer.slot,{}};
    offer.save_generation=session_save_generation_;offer.save_hash=manifest_.session_save_hash;offer.save=session_save_;
    send_to(peer.address,protocol::MessageType::ExperimentalSaveOffer,protocol::encode_match_end(offer));
    last_save_offer_[peer.slot]=now;
}

void DirectSession::service_save_jobs_locked() {
    for(auto it=save_jobs_.begin();it!=save_jobs_.end();) {
        if(it->result.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
            if(it->token==save_job_token_&&state_!=ConnectionState::Offline&&state_!=ConnectionState::Failed&&
               std::chrono::steady_clock::now()-it->started>std::chrono::seconds(30)) {
                fail_locked("Online save storage did not finish within 30 seconds. Your single-player saves were not changed.");
            }
            ++it;continue;
        }
        SaveJob job=std::move(*it);it=save_jobs_.erase(it);auto result=job.result.get();
        if(job.token!=save_job_token_||job.match!=match_id_||state_==ConnectionState::Offline||state_==ConnectionState::Failed)continue;
        const auto actual=result.bytes.size()==512?stable_hash(std::string_view(reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size())):0;
        if(job.kind==SaveJobKind::HostSeal) {
            if(!is_host_||launch_stage_!=LaunchStage::Idle||launch_countdown_active_||
               (owned_match_end_&&!owned_match_end_->resumed)||
               (state_!=ConnectionState::Hosting&&state_!=ConnectionState::Lobby)||job.generation!=session_save_generation_||
               job.launch!=lobby_.room().generation) {
                if(is_host_&&launch_stage_==LaunchStage::Idle&&!launch_countdown_active_)
                    status_="The lobby changed during save verification. Ready and start again.";
                continue;
            }
            if(!result.ok||!actual) {local_online_save_ready_=false;status_="Player 1's online save could not be verified: "+result.error;continue;}
            if(actual!=job.hash) {
                session_save_=std::move(result.bytes);manifest_.session_save_hash=actual;
                ++session_save_generation_;if(!session_save_generation_)++session_save_generation_;
                lobby_.refresh_online_save_hash(actual);room_view_=lobby_.room();online_save_status_.reset();
                for(auto& peer:peers_)if(peer.active) {peer.online_save_ready=false;offer_online_save_locked(peer);}
                status_="The latest online save is being synchronized. Ready again once every racer verifies it.";
                broadcast_lobby();continue;
            }
            std::string reason;const bool peers_ready=std::all_of(peers_.begin(),peers_.end(),[](const auto& p){return !p.active||p.online_save_ready;});
            if(!peers_ready||!lobby_.can_start(&reason)) {status_=reason.empty()?"The roster changed during save verification. Ready and start again.":reason;continue;}
            if(pending_invitation_||connection_test_active_||pending_launch_prepare_) {
                status_="Another lobby operation began during save verification. Start again when it finishes.";
                continue;
            }
            bool routes_ready=true;
            for(const auto& peer:peers_)if(peer.active)
                for(const auto traffic:{TransportTrafficClass::Checkpoint,TransportTrafficClass::Control,
                    TransportTrafficClass::Authoritative,TransportTrafficClass::Realtime,TransportTrafficClass::Replica})
                    routes_ready=routes_ready&&transport_->traffic_ready(peer.address,traffic);
            if(!routes_ready) {status_="A racer's network lanes are still warming up. Start again shortly.";continue;}
            std::fprintf(stderr,"[online][save] host-sealed match=%llu generation=%u hash=%llu bytes=512\n",
                static_cast<unsigned long long>(match_id_),session_save_generation_,static_cast<unsigned long long>(actual));
            host_start_verified_=true;begin_countdown_locked();continue;
        }
        if(is_host_||(job.kind!=SaveJobKind::Join&&job.generation<session_save_generation_))continue;
        if(job.kind==SaveJobKind::Join&&!admission_pending(state_))continue;
        if(job.kind==SaveJobKind::Resync&&state_!=ConnectionState::Lobby)continue;
        if(job.kind==SaveJobKind::Resume&&(!owned_match_end_||owned_match_end_->hash!=job.launch||
           !owned_match_end_->runtime_returned||owned_match_end_->resumed))continue;
        if(!result.ok||actual!=job.hash) {fail_locked("The isolated online save could not be installed and verified: "+result.error);continue;}
        session_save_=std::move(result.bytes);session_save_generation_=job.generation;manifest_.session_save_hash=actual;
        room_view_.manifest=manifest_;local_slot_=job.slot;local_online_save_ready_=true;local_online_save_acknowledged_=false;online_save_status_.reset();
        if(job.kind==SaveJobKind::Join) {transport_->retain_peer_route(host_address_);state_=ConnectionState::Lobby;}
        if(job.kind==SaveJobKind::Resume) {
            owned_match_end_->resumed=true;
            send_to(host_address_,protocol::MessageType::ExperimentalMatchEnd,
                protocol::encode_match_end({job.launch,protocol::MatchEndStage::ResumeAck,local_slot_,{}}));
        }
        send_to(host_address_,protocol::MessageType::OnlineSaveReady,
            protocol::encode_online_save_ready({local_slot_,session_save_generation_,actual}));
        last_client_request_=std::chrono::steady_clock::now();status_="Isolated online save installed and verified; waiting for Player 1's acknowledgement.";
        std::fprintf(stderr,"[online][save] installed phase=%u match=%llu generation=%u hash=%llu bytes=512\n",
            unsigned(job.kind),static_cast<unsigned long long>(match_id_),session_save_generation_,static_cast<unsigned long long>(actual));
        worker_wake_=true;state_changed_.notify_all();
    }
}

void DirectSession::configure_artifact_directory(std::filesystem::path directory) {
    std::scoped_lock lock(mutex_);
    artifact_directory_ = directory;
    replay_.configure(std::move(directory));
}

bool DirectSession::host(std::uint16_t port, std::string advertised_host,
                         std::string room_name, ConnectionMethod method,
                         std::string player_name, const Rules& rules,
                         std::string& error) {
    if (const char* reason = experimental::runtime_admission_error(rules.synchronization, owned_backend_available())) {
        error = reason;
        return false;
    }
    std::scoped_lock lock(mutex_);
    local_online_save_ready_ = false;
    local_online_save_acknowledged_ = false;
    online_save_status_.reset();
    local_runtime_save_hash_ = 0U;
    local_runtime_save_generation_ = 0U;
    runtime_save_hashes_ = {};
    runtime_save_generations_ = {};
    runtime_save_present_ = {};
    retire_transport_locked();
    quick_join_bootstrap_pending_ = false;
    critical_outbound_.clear();
    repair_outbound_.clear();
    high_priority_outbound_.clear();
    commit_outbound_.clear();
    authority_outbound_.clear();
    normal_priority_outbound_.clear();
    bulk_outbound_.clear();
    peers_ = {};
    completed_releases_.clear();
    checkpoint_encoding_.reset();
    duplicate_retries_coalesced_ = 0U;
    checkpoint_encoding_cache_hits_ = 0U;
    maximum_network_pump_us_ = 0U;
    pending_joins_ = {};
    mod_offer_.reset();mod_inbound_.clear();mod_inbound_bytes_=0;
    next_ready_request_id_ = 1U;
    pending_ready_request_id_ = 0U;
    desired_ready_.reset();
    reset_launch_transaction_locked();
    reset_connection_test_locked();
    terminal_connection_test_id_ = 0U;
    last_admission_response_ = {};
    connection_test_results_ = {};
    compatibility_sync_offer_.reset();
    launch_countdown_generation_ = 0U;
    next_launch_epoch_ = 1U;
    committed_launch_epoch_ = 0U;
    launch_commit_rebroadcast_until_ = {};
    last_launch_commit_send_ = {};
    clear_friend_admissions_locked();
    client_friend_admission_ = {};
    received_sequences_.clear();
    blocked_senders_.clear();
    blocked_sources_.clear();
    next_sequence_ = 1U;
    packets_sent_ = 0U;
    packets_received_ = 0U;
    outbound_queue_high_water_ = 0U;
    rollback_queue_high_water_ = 0U;
    transport_queue_failures_ = 0U;
    failure_recorder().clear();
    bytes_sent_ = 0U;
    bytes_received_ = 0U;
    join_packets_recognized_ = 0U;
    join_packets_stale_ = 0U;
    join_packets_auth_rejected_ = 0U;
    join_packets_manifest_rejected_ = 0U;
    join_packets_accepted_ = 0U;
    connect_started_ = {};
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    input_epoch_ = 1U;
    retired_authority_epoch_ = 0U;
    pending_authority_epoch_ = 0U;
    stale_epoch_packets_ = 0U;
    future_epoch_packets_ = 0U;
    host_input_accepted_frame_ = 0U;
    host_input_first_missing_frame_ = 0U;
    peer_input_first_missing_frame_ = {};
    reset_pending_commit_wait_locked();
    prediction_failsafe_active_ = false;
    commit_repair_requests_sent_ = 0U;
    commit_repair_requests_received_ = 0U;
    commit_repair_batches_sent_ = 0U;
    forced_prediction_frames_ = 0U;
    late_inputs_discarded_ = 0U;
    live_replica_requests_sent_ = 0U;
    live_replica_request_misses_ = 0U;
    live_replica_window_rejections_ = 0U;
    live_replica_decode_failures_ = 0U;
    frame_correction_generation_ = 0U;
    applied_frame_correction_generation_ = 0U;
    frame_correction_assembly_.reset();
    frame_correction_transmission_.reset();
    deferred_frame_commits_.clear();
    if (!valid_display_name(player_name) || !valid_room_name(room_name) ||
        !valid_rules(rules)) {
        error = "The room name, racer name or rules are invalid.";
        return false;
    }
    if (method == ConnectionMethod::QuickJoin) {
        transport_ = make_quick_join_session_transport(true, {}, error);
        if (!transport_) return false;
    } else {
        transport_ = make_udp_session_transport();
    }
    transport_->set_receive_signal(receive_signal_);
    if (!transport_->open(port, error)) return false;
    host_key_pair_ = secure::generate_key_pair();
    invitation_capability_ = secure::generate_key();
    match_id_ = random_u64();
    sender_id_ = random_u64();
    host_sender_id_ = sender_id_;
    local_name_ = std::move(player_name);
    const std::string room_id = hex64(match_id_);
    const std::string join_code = secure::encode_key(invitation_capability_).substr(0U, 8U);
    const Visibility visibility = method == ConnectionMethod::Lan
        ? Visibility::Lan : Visibility::Private;
    if (!lobby_.create(room_id, join_code, std::move(room_name), visibility,
                       std::to_string(sender_id_), local_name_, manifest_, rules,
                       error)) {
        retire_transport_locked();
        return false;
    }
    is_host_ = true;
    local_online_save_ready_ = save_sync_available();
    local_online_save_acknowledged_ = local_online_save_ready_;
    method_ = method;
    advertised_host_ = advertised_host.empty() ? "127.0.0.1" : advertised_host;
    lobby_locked_ = false;
    local_slot_ = 0U;
    input_delay_ = rules.automatic_input_delay ? 2U : rules.manual_input_delay;
    room_view_ = lobby_.room();
    state_ = ConnectionState::Hosting;
    worker_wake_ = true;
    state_changed_.notify_all();
    if (method_ == ConnectionMethod::QuickJoin) {
        invite_ = transport_->quick_join_code();
        transport_->set_quick_join_bootstrap(make_quick_join_bootstrap());
        status_ = "Quick Join lobby created. Share the five-character code with your racers.";
    } else {
        invite_ = make_invite(advertised_host_);
        status_ = "Lobby created. Share the encrypted invite with your racers.";
    }
    error.clear();
    return true;
}

bool DirectSession::join(std::string_view invite, std::string player_name,
                         std::string& error) {
    return join_friend_invite(invite, std::move(player_name), {}, error);
}

bool DirectSession::join_friend_invite(std::string_view invite,
                                       std::string player_name,
                                       const secure::Key& admission,
                                       std::string& error) {
    std::scoped_lock lock(mutex_);
    local_online_save_ready_ = false;
    local_online_save_acknowledged_ = false;
    online_save_status_.reset();
    local_runtime_save_hash_ = 0U;
    local_runtime_save_generation_ = 0U;
    runtime_save_hashes_ = {};
    runtime_save_generations_ = {};
    runtime_save_present_ = {};
    quick_join_bootstrap_pending_ = false;
    if (!valid_display_name(player_name)) {
        error = "Choose a racer name between 1 and 24 characters.";
        return false;
    }
    const bool quick_join = valid_quick_join_code(invite);
    PeerAddress address{};
    std::uint64_t match_id = 0U;
    std::uint64_t host_sender_id = 0U;
    secure::Key host_public{};
    secure::Key invitation_capability{};
    ConnectionMethod method = ConnectionMethod::Lan;
    if (quick_join) {
        method = ConnectionMethod::QuickJoin;
    } else if (!parse_invite(invite, address, match_id, host_sender_id, method,
                             host_public, invitation_capability, error)) {
        return false;
    }
    retire_transport_locked();
    critical_outbound_.clear();
    repair_outbound_.clear();
    high_priority_outbound_.clear();
    commit_outbound_.clear();
    authority_outbound_.clear();
    normal_priority_outbound_.clear();
    bulk_outbound_.clear();
    peers_ = {};
    pending_joins_ = {};
    mod_offer_.reset();mod_inbound_.clear();mod_inbound_bytes_=0;
    mod_manifest_bytes_=0;manifest_.mod_manifest_hash.clear();
    // Completion/encoding caches belong to this authenticated session only.
    // A new room can reuse the same numeric scene and boundary identifiers.
    completed_releases_.clear();
    checkpoint_encoding_.reset();
    duplicate_retries_coalesced_ = 0U;
    checkpoint_encoding_cache_hits_ = 0U;
    maximum_network_pump_us_ = 0U;
    next_ready_request_id_ = 1U;
    pending_ready_request_id_ = 0U;
    desired_ready_.reset();
    reset_launch_transaction_locked();
    reset_connection_test_locked();
    terminal_connection_test_id_ = 0U;
    last_admission_response_ = {};
    connection_test_results_ = {};
    compatibility_sync_offer_.reset();
    launch_countdown_generation_ = 0U;
    next_launch_epoch_ = 1U;
    committed_launch_epoch_ = 0U;
    launch_commit_rebroadcast_until_ = {};
    last_launch_commit_send_ = {};
    received_sequences_.clear();
    blocked_senders_.clear();
    blocked_sources_.clear();
    next_sequence_ = 1U;
    packets_sent_ = 0U;
    packets_received_ = 0U;
    outbound_queue_high_water_ = 0U;
    rollback_queue_high_water_ = 0U;
    transport_queue_failures_ = 0U;
    failure_recorder().clear();
    bytes_sent_ = 0U;
    bytes_received_ = 0U;
    join_packets_recognized_ = 0U;
    join_packets_stale_ = 0U;
    join_packets_auth_rejected_ = 0U;
    join_packets_manifest_rejected_ = 0U;
    join_packets_accepted_ = 0U;
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    input_epoch_ = 1U;
    retired_authority_epoch_ = 0U;
    pending_authority_epoch_ = 0U;
    stale_epoch_packets_ = 0U;
    future_epoch_packets_ = 0U;
    host_input_accepted_frame_ = 0U;
    host_input_first_missing_frame_ = 0U;
    peer_input_first_missing_frame_ = {};
    reset_pending_commit_wait_locked();
    prediction_failsafe_active_ = false;
    commit_repair_requests_sent_ = 0U;
    commit_repair_requests_received_ = 0U;
    commit_repair_batches_sent_ = 0U;
    forced_prediction_frames_ = 0U;
    late_inputs_discarded_ = 0U;
    live_replica_requests_sent_ = 0U;
    live_replica_request_misses_ = 0U;
    live_replica_window_rejections_ = 0U;
    live_replica_decode_failures_ = 0U;
    frame_correction_generation_ = 0U;
    applied_frame_correction_generation_ = 0U;
    frame_correction_assembly_.reset();
    frame_correction_transmission_.reset();
    deferred_frame_commits_.clear();
    if (quick_join) {
        transport_ = make_quick_join_session_transport(false, invite, error);
        if (!transport_) return false;
    } else {
        transport_ = make_udp_session_transport();
    }
    transport_->set_receive_signal(receive_signal_);
    if (!transport_->open(0U, error)) return false;
    is_host_ = false;
    method_ = method;
    host_address_ = quick_join ? PeerAddress{} : address;
    match_id_ = quick_join ? 0U : match_id;
    host_sender_id_ = quick_join ? 0U : host_sender_id;
    host_public_ = quick_join ? secure::Key{} : host_public;
    invitation_capability_ = quick_join ? secure::Key{} : invitation_capability;
    client_friend_admission_ = admission;
    client_key_pair_ = secure::generate_key_pair();
    sender_id_ = random_u64();
    local_name_ = std::move(player_name);
    local_slot_ = 0U;
    room_view_ = {};
    room_view_.room_id = quick_join ? std::string{} : hex64(match_id_);
    room_view_.manifest = manifest_;
    state_ = ConnectionState::Connecting;
    worker_wake_ = true;
    state_changed_.notify_all();
    invite_ = std::string(invite);
    quick_join_bootstrap_pending_ = quick_join;
    status_ = quick_join
        ? "Contacting the Quick Join host..."
        : "Contacting the host...";
    if (!quick_join && !send_join_request()) {
        error = "The authenticated join request could not be created.";
        retire_transport_locked();
        state_ = ConnectionState::Failed;
        return false;
    }
    connect_started_ = std::chrono::steady_clock::now();
    last_connect_request_ = connect_started_;
    error.clear();
    return true;
}

void DirectSession::disconnect(std::string_view reason) {
    std::scoped_lock lock(mutex_);
    if (transport_->is_open() && state_ != ConnectionState::Offline) {
        // An empty disconnect used to erase the only useful evidence when a
        // guest cleared a locally failed session.  The host then reported an
        // unexplained departure and could not distinguish a transport fault
        // from a deliberate leave. Preserve the terminal cause when one is
        // known and give orderly process shutdown an explicit meaning.
        const std::string notification = !reason.empty()
            ? std::string(reason)
            : state_ == ConnectionState::Failed && !status_.empty()
                ? status_
                : "The racer application closed.";
        const std::vector<std::uint8_t> payload(notification.begin(),
                                                notification.end());
        if (is_host_) broadcast(protocol::MessageType::Disconnect, payload);
        else if (host_address_) send_to(host_address_, protocol::MessageType::Disconnect, payload);
        // Unlike normal traffic, the final control packet cannot be left on
        // the worker queue because the socket is about to close.
        flush_outbound_locked();
    }
    if (replay_.active()) {
        if (!replay_writer_started_) {
            replay_writer_.start([] {}, [] {});
            replay_writer_started_ = true;
        }
        auto recording = std::make_shared<ReplayRecorder>(replay_.detach());
        auto result = std::make_shared<std::promise<std::string>>();
        replay_writes_.push_back(result->get_future());
        if (!replay_writer_.post([recording, result] {
                std::string error;
                try { recording->finalize(error); }
                catch (...) { error = "Replay finalization failed unexpectedly."; }
                result->set_value(std::move(error));
            })) result->set_value("Replay storage queue is unavailable. The recording could not be saved.");
    }
    retire_transport_locked();
    transport_ = make_udp_session_transport();
    quick_join_bootstrap_pending_ = false;
    lobby_ = {};
    room_view_ = {};
    timeline_.reset();
    peers_ = {};
    completed_releases_.clear();
    checkpoint_encoding_.reset();
    duplicate_retries_coalesced_ = 0U;
    checkpoint_encoding_cache_hits_ = 0U;
    maximum_network_pump_us_ = 0U;
    pending_joins_ = {};
    mod_offer_.reset();mod_inbound_.clear();mod_inbound_bytes_=0;
    mod_manifest_bytes_=0;manifest_.mod_manifest_hash.clear();
    received_sequences_.clear();
    blocked_senders_.clear();
    blocked_sources_.clear();
    state_hashes_.clear();
    bootstrap_hashes_ = {};
    bootstrap_hash_present_ = {};
    runtime_save_hashes_ = {};
    runtime_save_generations_ = {};
    runtime_save_present_ = {};
    clear_input_history_locked();
    frame_commits_.clear();
    commit_history_.clear();
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    critical_outbound_.clear();
    repair_outbound_.clear();
    high_priority_outbound_.clear();
    commit_outbound_.clear();
    authority_outbound_.clear();
    normal_priority_outbound_.clear();
    bulk_outbound_.clear();
    rollback_inbound_.clear();
    owned_inbound_.clear();
    owned_inbound_bytes_ = 0;
    owned_match_end_.reset();
    ++save_job_token_;save_jobs_.clear();host_start_verified_=false;
    last_save_offer_={};
    retired_owned_generation_ = 0;
    owned_last_host_packet_ = {};
    authoritative_phase_active_ = false;
    authority_lifecycle_ = AuthorityLifecycle::Inactive;
    finish_seal_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    scene_epoch_ = 0U;
    last_host_scene_epoch_ = 0U;
    recovery_frame_.reset();
    completed_recovery_frame_.reset();
    completed_recovery_epoch_ = 0U;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    last_recovery_broadcast_ = {};
    recovery_resume_until_ = {};
    transition_barrier_.reset();
    last_transition_broadcast_ = {};
    transition_resume_until_ = {};
    gameplay_barrier_.reset();
    last_gameplay_barrier_broadcast_ = {};
    gameplay_resume_until_ = {};
    last_gameplay_ready_announcement_ = {};
    last_gameplay_resume_ack_ = {};
    gameplay_resume_ack_until_ = {};
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    last_authoritative_request_ = {};
    last_authoritative_request_frame_ = 0U;
    state_ = ConnectionState::Offline;
    is_host_ = false;
    lobby_locked_ = false;
    launch_requested_ = false;
    reset_launch_transaction_locked();
    reset_connection_test_locked();
    terminal_connection_test_id_ = 0U;
    last_admission_response_ = {};
    connection_test_results_ = {};
    compatibility_sync_offer_.reset();
    launch_countdown_generation_ = 0U;
    next_launch_epoch_ = 1U;
    committed_launch_epoch_ = 0U;
    launch_commit_rebroadcast_until_ = {};
    last_launch_commit_send_ = {};
    local_loaded_ = false;
    local_bootstrap_hash_ = 0U;
    local_runtime_save_hash_ = 0U;
    local_runtime_save_generation_ = 0U;
    local_online_save_ready_ = false;
    local_online_save_acknowledged_ = false;
    run_signal_sent_ = false;
    online_save_status_.reset();
    launch_descriptor_.reset();
    last_verified_frame_ = 0U;
    last_authoritative_frame_ = 0U;
    authoritative_corrections_ = 0U;
    consecutive_authoritative_frames_ = 0U;
    next_commit_frame_ = 0U;
    native_completed_frame_.reset();
    last_consumed_input_frame_.reset();
    last_consumed_inputs_ = {};
    input_epoch_ = 1U;
    retired_authority_epoch_ = 0U;
    pending_authority_epoch_ = 0U;
    stale_epoch_packets_ = 0U;
    future_epoch_packets_ = 0U;
    frame_correction_generation_ = 0U;
    applied_frame_correction_generation_ = 0U;
    frame_correction_assembly_.reset();
    frame_correction_transmission_.reset();
    deferred_frame_commits_.clear();
    last_consumed_commit_hash_ = 0U;
    pending_input_correction_.reset();
    local_input_submitted_frame_ = 0U;
    local_input_submitted_ = {};
    pending_local_submission_frame_.reset();
    last_input_history_send_ = {};
    reset_pending_commit_wait_locked();
    prediction_failsafe_active_ = false;
    host_input_accepted_frame_ = 0U;
    host_input_first_missing_frame_ = 0U;
    peer_input_first_missing_frame_ = {};
    authoritative_input_frame_ = 0U;
    authoritative_inputs_ = {};
    authoritative_inputs_valid_ = false;
    authoritative_input_revision_ = 0U;
    authoritative_predicted_mask_ = 0U;
    input_corrections_ = 0U;
    commit_repair_requests_sent_ = 0U;
    commit_repair_requests_received_ = 0U;
    commit_repair_batches_sent_ = 0U;
    forced_prediction_frames_ = 0U;
    late_inputs_discarded_ = 0U;
    live_replica_requests_sent_ = 0U;
    live_replica_request_misses_ = 0U;
    live_replica_window_rejections_ = 0U;
    live_replica_decode_failures_ = 0U;
    desired_ready_.reset();
    next_ready_request_id_ = 1U;
    pending_ready_request_id_ = 0U;
    last_connect_request_ = {};
    connect_started_ = {};
    last_client_request_ = {};
    last_lobby_broadcast_ = {};
    failure_broadcast_until_ = {};
    last_failure_broadcast_ = {};
    failure_payload_.clear();
    next_sequence_ = 1U;
    packets_sent_ = 0U;
    packets_received_ = 0U;
    bytes_sent_ = 0U;
    bytes_received_ = 0U;
    authority_checkpoints_sent_ = 0U;
    authority_checkpoints_acknowledged_ = 0U;
    authority_wait_timeouts_ = 0U;
    input_stalls_ = 0U;
    longest_input_stall_ms_ = 0U;
    network_rtt_ms_ = 0U;
    network_jitter_ms_ = 0U;
    network_loss_percent_ = 0.0F;
    outbound_packets_dropped_ = 0U;
    outbound_queue_high_water_ = 0U;
    rollback_queue_high_water_ = 0U;
    transport_queue_failures_ = 0U;
    invite_.clear();
    advertised_host_.clear();
    client_friend_admission_ = {};
    clear_friend_admissions_locked();
    status_ = reason.empty() ? "Offline." : std::string(reason);
    presentation_view_.store(std::make_shared<const SessionView>(view_locked()), std::memory_order_release);
    worker_wake_ = true;
    state_changed_.notify_all();
}

void DirectSession::pump() {
    {
        std::scoped_lock lock(mutex_);
        if (state_ == ConnectionState::Offline && replay_writes_.empty()) return;
        worker_wake_ = true;
    }
    state_changed_.notify_all();
}

void DirectSession::network_loop() {
    std::unique_lock lock(mutex_);
    std::uint32_t observed_commit_frame = next_commit_frame_;
    auto last_commit_progress = std::chrono::steady_clock::now();
    bool progress_stall_reported = false;
    bool worker_fault = false;
    unsigned consecutive_receive_drains = 0;
    while (!worker_stop_.load(std::memory_order_acquire)) {
        const auto received_generation = receive_signal_->generation.load(std::memory_order_acquire);
        for (auto it = replay_writes_.begin(); it != replay_writes_.end();) {
            if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
            const auto error = it->get();
            if (!error.empty()) replay_write_error_ = error;
            it = replay_writes_.erase(it);
        }
        const auto pump_started = std::chrono::steady_clock::now();
        if (state_ != ConnectionState::Failed) worker_fault = false;
        if (!worker_fault) {
            try {
                service_save_jobs_locked();
                if (transport_->is_open()) pump_locked();
                else if ((launch_descriptor_ && launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback) ||
                         (owned_match_end_ && !owned_match_end_->resumed))
                    fail_locked("The experimental connection closed. The game ended; return to Online to reconnect.");
            } catch (const std::exception& exception) {
                worker_fault = true;
                fail_locked(std::string("The online transport failed safely: ") + exception.what());
            } catch (...) {
                worker_fault = true;
                fail_locked("The online transport encountered an unexpected failure. Leave and reconnect; your separate saves are retained.");
            }
        }
        const auto pump_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - pump_started).count();
        maximum_network_pump_us_ = (std::max)(maximum_network_pump_us_, static_cast<std::uint64_t>(pump_us));
        const auto now = std::chrono::steady_clock::now();
        if (now - last_view_publication_ >= std::chrono::milliseconds(50)) {
            presentation_view_.store(std::make_shared<const SessionView>(view_locked()), std::memory_order_release);
            last_view_publication_ = now;
        }
        if (state_ == ConnectionState::Running &&
            (!launch_descriptor_ || launch_descriptor_->synchronization !=
                SynchronizationMode::ExperimentalRollback)) {
            if (next_commit_frame_ != observed_commit_frame) {
                observed_commit_frame = next_commit_frame_;
                last_commit_progress = now;
                progress_stall_reported = false;
            } else if (!progress_stall_reported &&
                       now - last_commit_progress >=
                           std::chrono::seconds(3)) {
                failure_recorder().record(
                    FailureEventKind::ProgressWatchdog, next_commit_frame_,
                    static_cast<std::uint32_t>(commit_outbound_.size()),
                    static_cast<std::uint32_t>(authority_outbound_.size()),
                    is_host_ ? "host authored timeline stopped"
                             : "client authored timeline stopped");
                queue_diagnostics_locked();
                progress_stall_reported = true;
            }
        } else {
            observed_commit_frame = next_commit_frame_;
            last_commit_progress = now;
            progress_stall_reported = false;
        }
        worker_wake_ = false;
        state_changed_.notify_all();
        // DKR authors input at 30 Hz. A 2 ms unconditional socket poll woke
        // this thread roughly 500 times per second even in an idle lobby and
        // was a measurable source of CPU/thermal load. Local sends still wake
        // the worker immediately; the bounded receive poll remains frequent
        // enough to service several times within one authored frame.
        const auto receive_poll =
            state_ == ConnectionState::Running ||
                    state_ == ConnectionState::Loading ||
                    state_ == ConnectionState::Connecting ||
                    state_ == ConnectionState::AwaitingApproval
                ? std::chrono::milliseconds(4)
                : std::chrono::milliseconds(12);
        const auto wake = [this, received_generation] {
            return worker_stop_.load(std::memory_order_acquire) || worker_wake_ ||
                receive_signal_->generation.load(std::memory_order_acquire) != received_generation;
        };
        if (receive_signal_->generation.load(std::memory_order_acquire) != received_generation &&
            ++consecutive_receive_drains >= 4U) {
            // Under sustained receive load the predicate can remain true for
            // every pass. Release mutex_ for gameplay/UI instead of repeatedly
            // draining while owning it. Only shutdown/local work can interrupt
            // this short fairness window; incoming bytes remain in the inbox.
            state_changed_.wait_for(lock, std::chrono::microseconds(250), [this] {
                return worker_stop_.load(std::memory_order_acquire) || worker_wake_;
            });
            consecutive_receive_drains = 0;
        } else if (state_ == ConnectionState::Offline && replay_writes_.empty() && save_jobs_.empty()) {
            // No socket, heartbeat or replay-completion deadline exists here.
            // host/join/disconnect/quit publish the wake under the same mutex.
            state_changed_.wait(lock, wake);
        } else {
            // Callback notifications do not take mutex_: no transport/session
            // lock inversion. Keep the bounded fallback for UDP and a notify
            // racing the condition-variable wait itself (at most 4 ms in-game).
            state_changed_.wait_for(lock, receive_poll, wake);
        }
        if (receive_signal_->generation.load(std::memory_order_acquire) == received_generation)
            consecutive_receive_drains = 0;
    }
}

bool DirectSession::set_ready(bool ready, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (owned_match_end_ && !owned_match_end_->resumed) {
        error = "Wait for every remaining racer to return to the lobby and verify the online save.";
        return false;
    }
    if (connection_test_active_) {
        error = "Wait for the connection pre-flight check to finish.";
        return false;
    }
    if (state_ != ConnectionState::Hosting && state_ != ConnectionState::Lobby) {
        error = "Join or create a lobby before changing ready state.";
        return false;
    }
    if (ready && (is_host_ ? !local_online_save_ready_
                           : !local_online_save_acknowledged_)) {
        error = "The isolated online save has not been verified yet.";
        return false;
    }
    if (is_host_) {
        if (!lobby_.set_ready(std::to_string(sender_id_), ready, error)) return false;
        room_view_ = lobby_.room();
        broadcast_lobby();
    } else {
        const std::uint32_t request_id = next_ready_request_id_++;
        if (next_ready_request_id_ == 0U) next_ready_request_id_ = 1U;
        const protocol::ReadyRequestPayload request{
            request_id, local_slot_, ready};
        if (!send_to(host_address_, protocol::MessageType::ReadyRequest,
                     protocol::encode_ready_request(request))) {
            error = status_;
            return false;
        }
        desired_ready_ = ready;
        pending_ready_request_id_ = request_id;
        last_client_request_ = std::chrono::steady_clock::now();
    }
    error.clear();
    return true;
}

bool DirectSession::approve_join(std::uint64_t request_id, std::string& error) {
    std::scoped_lock lock(mutex_);
    return approve_join_locked(request_id, error);
}

bool DirectSession::approve_join_locked(std::uint64_t request_id,
                                        std::string& error) {
    if (owned_match_end_ && !owned_match_end_->resumed) {
        error = "Wait for the previous match to return to the lobby."; return false;
    }
    if (connection_test_active_) {
        error = "Wait for the connection pre-flight check to finish.";
        return false;
    }
    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)) {
        error = "Only the host can approve racers while waiting in the lobby.";
        return false;
    }
    PendingRecord* pending = pending_by_request(request_id);
    if (pending == nullptr) {
        error = "That join request has expired.";
        return false;
    }
    if (!manifest_.mod_manifest_hash.empty() && !pending->mods_verified) {
        if (!mod_manifest_bytes_) {
            error = "The host's immutable mod offer has not been prepared."; return false;
        }
        if (!pending->mods_approved) {
            const auto reserved=std::count_if(pending_joins_.begin(),pending_joins_.end(),
                [](const auto& p){return p.active && p.mods_approved;});
            if (occupied_players()+reserved>=room_view_.rules.maximum_players) {
                error = "This lobby has no unreserved racer slot."; return false;
            }
            pending->mods_approved=true;
            transport_->retain_peer_route(pending->address);
            pending->mod_route_retained=true;
        }
        send_mod_offer_locked(*pending);
        status_=pending->display_name+" is reviewing/preparing the host's mods.";
        error.clear(); return true;
    }
    const std::string peer_id = std::to_string(pending->sender_id);
    CompatibilityManifest synchronized_manifest = pending->manifest;
    const bool requires_save_sync = true;
    if (!save_sync_available() || session_save_generation_ == 0U) {
        error = "Player 1 does not have a validated session save to synchronize.";
        return false;
    }
    synchronized_manifest.session_save_hash = manifest_.session_save_hash;
    const auto slot = lobby_.join(peer_id, pending->display_name,
                                  synchronized_manifest, error);
    if (!slot) return false;
    peers_[*slot] = {true, pending->sender_id, pending->address, *slot,
                     pending->key};
    peers_[*slot].next_sequence = pending->next_sequence;
    peers_[*slot].requires_save_sync = requires_save_sync;
    peers_[*slot].online_save_ready = false;
    if (!pending->mod_route_retained) transport_->retain_peer_route(pending->address);
    room_view_ = lobby_.room();
    send_with_key(pending->address, pending->key,
                  protocol::MessageType::HelloAck,
                  protocol::encode_hello_ack({
                      true, *slot,
                      "The host approved you and supplied an isolated online save.",
                      session_save_, {}, session_save_generation_,
                      manifest_.session_save_hash}));
    status_ = pending->display_name + " joined as Player " +
              std::to_string(*slot + 1U) + ".";
    *pending = {};
    broadcast_lobby();
    error.clear();
    return true;
}

bool DirectSession::create_friend_admission(secure::Key& admission,
                                            std::chrono::seconds lifetime,
                                            std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby) || lobby_locked_ ||
        invite_.empty()) {
        error = "Open an unlocked online lobby before inviting a friend.";
        return false;
    }
    const auto clamped = std::clamp(
        lifetime, std::chrono::seconds(30),
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::minutes(10)));
    const auto now = std::chrono::steady_clock::now();
    for (FriendAdmissionRecord& record : friend_admissions_) {
        if (record.active && record.expires <= now) record = {};
    }
    const auto available = std::find_if(friend_admissions_.begin(),
        friend_admissions_.end(), [](const FriendAdmissionRecord& record) {
            return !record.active;
        });
    if (available == friend_admissions_.end()) {
        error = "Too many friend invitations are already awaiting a response.";
        return false;
    }
    do {
        admission = secure::generate_key();
    } while (admission == secure::Key{} ||
             std::any_of(friend_admissions_.begin(), friend_admissions_.end(),
                 [&](const FriendAdmissionRecord& record) {
                     return record.active && record.capability == admission;
                 }));
    *available = {admission, now + clamped, true};
    error.clear();
    return true;
}

void DirectSession::revoke_friend_admission(const secure::Key& admission) {
    std::scoped_lock lock(mutex_);
    for (FriendAdmissionRecord& record : friend_admissions_) {
        if (record.active && record.capability == admission) {
            record = {};
            return;
        }
    }
}

bool DirectSession::consume_friend_admission_locked(
    const secure::Key& admission) {
    if (admission == secure::Key{}) return false;
    const auto now = std::chrono::steady_clock::now();
    for (FriendAdmissionRecord& record : friend_admissions_) {
        if (record.active && record.expires <= now) record = {};
        if (record.active && record.capability == admission) {
            record = {};
            return true;
        }
    }
    return false;
}

void DirectSession::clear_friend_admissions_locked() {
    friend_admissions_ = {};
}

bool DirectSession::reject_join(std::uint64_t request_id,
                                bool block_for_session,
                                std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!is_host_) {
        error = "Only the host can decline racers.";
        return false;
    }
    PendingRecord* pending = pending_by_request(request_id);
    if (pending == nullptr) {
        error = "That join request has expired.";
        return false;
    }
    send_with_key(pending->address, pending->key,
                  protocol::MessageType::HelloAck,
                  protocol::encode_hello_ack({false, 0U,
                      block_for_session ? "The host blocked this request for the session."
                                        : "The host declined this join request."}));
    if (block_for_session) {
        blocked_senders_.insert(pending->sender_id);
        const std::string source_host = DatagramSocket::describe_host(pending->address);
        if (!source_host.empty()) blocked_sources_.insert(source_host);
    }
    status_ = pending->display_name + " was not admitted.";
    clear_pending_mod_route_locked(*pending);
    *pending = {};
    error.clear();
    return true;
}

bool DirectSession::set_lobby_locked(bool locked, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (connection_test_active_) {
        error = "Wait for the connection pre-flight check to finish.";
        return false;
    }
    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)) {
        error = "Only the host can lock the lobby before the race starts.";
        return false;
    }
    lobby_locked_ = locked;
    if (locked) {
        clear_friend_admissions_locked();
        for (PendingRecord& pending : pending_joins_) {
            if (!pending.active) continue;
            send_with_key(pending.address, pending.key,
                protocol::MessageType::HelloAck,
                protocol::encode_hello_ack({false, 0U, "The host locked this lobby."}));
            clear_pending_mod_route_locked(pending);
            pending = {};
        }
    }
    status_ = locked ? "Lobby locked. New join requests are paused."
                     : "Lobby unlocked. New racers may request to join.";
    error.clear();
    return true;
}

bool DirectSession::kick_player(std::uint8_t slot, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (connection_test_active_) {
        error = "Wait for the connection pre-flight check to finish.";
        return false;
    }
    if (!is_host_ || slot == 0U || slot >= peers_.size() ||
        !peers_[slot].active) {
        error = "Choose the occupied Player 2 slot to remove.";
        return false;
    }
    PeerRecord departing = peers_[slot];
    const std::string name = lobby_.room().players[slot].display_name;
    const std::string message = "The host removed you from the lobby.";
    send_with_key(departing.address, departing.key,
                  protocol::MessageType::Disconnect,
                  std::span<const std::uint8_t>(
                      reinterpret_cast<const std::uint8_t*>(message.data()), message.size()));
    lobby_.leave(std::to_string(departing.sender_id));
    transport_->release_peer_route(peers_[slot].address);
    peers_[slot] = {};
    synchronize_peer_slots();
    room_view_ = lobby_.room();
    broadcast_lobby();
    status_ = name + " left the starting grid.";
    error.clear();
    return true;
}

bool DirectSession::revoke_invitation(std::string& error) {
    std::scoped_lock lock(mutex_);
    if (pending_invitation_) {
        error = "A replacement code is still registering. The existing code remains valid.";
        return false;
    }
    if (connection_test_active_ || launch_countdown_active_ || launch_stage_ != LaunchStage::Idle) {
        error = "Finish or cancel the connection test/countdown before replacing the invitation.";
        return false;
    }
    if (!is_host_ || advertised_host_.empty()) {
        error = "Only the host can replace an active invitation.";
        return false;
    }
    if (method_ == ConnectionMethod::QuickJoin) {
        const secure::Key previous_capability = invitation_capability_;
        const secure::KeyPair previous_host_key_pair = host_key_pair_;
        invitation_capability_ = secure::generate_key();
        host_key_pair_ = secure::generate_key_pair();
        const PendingInvitation staged{invitation_capability_, host_key_pair_};
        const std::string bootstrap = make_quick_join_bootstrap();
        invitation_capability_ = previous_capability;
        host_key_pair_ = previous_host_key_pair;
        if (!transport_->rekey_quick_join(bootstrap, error)) return false;
        pending_invitation_ = staged;
        status_ = "Registering a replacement code. The existing code remains valid until confirmed.";
        error.clear();
        return true;
    } else {
        invitation_capability_ = secure::generate_key();
        host_key_pair_ = secure::generate_key_pair();
        invite_ = make_invite(advertised_host_);
    }
    clear_friend_admissions_locked();
    for(auto& pending:pending_joins_)clear_pending_mod_route_locked(pending);
    pending_joins_ = {};
    status_ = method_ == ConnectionMethod::QuickJoin
        ? "Quick Join code replaced. The previous code can no longer join."
        : "Invitation replaced. The previous invitation can no longer join.";
    error.clear();
    return true;
}

bool DirectSession::request_start(std::string& error) {
    std::scoped_lock lock(mutex_);
    if (owned_match_end_ && !owned_match_end_->resumed) {
        error = "Wait for every remaining racer to return to the lobby and verify the online save.";
        return false;
    }
    if (const char* reason = experimental::runtime_admission_error(lobby_.room().rules.synchronization, owned_backend_available_)) {
        error = reason;
        return false;
    }
    if (pending_invitation_) {
        error = "Wait for the code replacement to finish before starting.";
        return false;
    }
    if (connection_test_active_) {
        error = "Wait for the connection pre-flight check to finish.";
        return false;
    }
    if (!is_host_) {
        error = "Only Player 1 can start the lobby.";
        return false;
    }
    if (!local_online_save_ready_) {
        error = "Player 1's isolated online save is not ready.";
        return false;
    }
    for (const PeerRecord& peer : peers_) {
        if (peer.active && !peer.online_save_ready) {
            error = "Every racer must verify the isolated online save before starting.";
            return false;
        }
    }
    // Quick Join negotiates independent SCTP lanes. Never publish an immutable
    // launch descriptor until every admitted racer can receive control,
    // authority, realtime input and corrective replicas. UDP reports its one
    // shared datagram route ready for every class.
    constexpr std::array<TransportTrafficClass, 5U> required_traffic{{
        TransportTrafficClass::Checkpoint,
        TransportTrafficClass::Control,
        TransportTrafficClass::Authoritative,
        TransportTrafficClass::Realtime,
        TransportTrafficClass::Replica}};
    for (const PeerRecord& peer : peers_) {
        if (!peer.active) continue;
        for (const TransportTrafficClass traffic : required_traffic) {
            if (transport_->traffic_ready(peer.address, traffic)) continue;
            status_ = "A racer's network lanes are still warming up.";
            error = "A racer's network lanes are still warming up. Try starting again in a moment.";
            return false;
        }
    }
    if (launch_stage_ != LaunchStage::Idle || launch_countdown_active_ ||
        pending_launch_prepare_) {
        error = "The synchronized start transaction is already running.";
        return false;
    }
    if (!lobby_.can_start(&error)) return false;
    if(lobby_.room().rules.synchronization==SynchronizationMode::ExperimentalRollback&&save_reader_&&!host_start_verified_) {
        if(!queue_save_locked(SaveJobKind::HostSeal,session_save_,session_save_generation_,manifest_.session_save_hash,local_slot_,lobby_.room().generation)) {
            error="The online save verification worker is busy. Try starting again shortly.";return false;
        }
        status_="Verifying Player 1's online save before the countdown...";
        error.clear();return true;
    }
    begin_countdown_locked();error.clear();return true;
}

void DirectSession::begin_countdown_locked() {
    host_start_verified_=false;
    reset_launch_transaction_locked();
    committed_launch_epoch_ = 0U;
    launch_commit_rebroadcast_until_ = {};
    last_launch_commit_send_ = {};
    launch_countdown_active_ = true;
    launch_stage_ = LaunchStage::Countdown;
    const auto now = std::chrono::steady_clock::now();
    launch_countdown_deadline_ = now + std::chrono::seconds(5);
    ++launch_countdown_generation_;
    if (launch_countdown_generation_ == 0U) ++launch_countdown_generation_;
    countdown_acks_[local_slot_] = true;
    status_ = "DKR-R Online starts in five seconds.";
    broadcast_lobby();
}

bool DirectSession::request_connection_test(std::string& error) {
    std::scoped_lock lock(mutex_);
    if (pending_invitation_) {
        error = "Wait for the code replacement to finish before testing.";
        return false;
    }
    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)) {
        error = "Only Player 1 can run a connection pre-flight check from the lobby.";
        return false;
    }
    if (connection_test_active_ || launch_stage_ != LaunchStage::Idle) {
        error = "A synchronized lobby operation is already running.";
        return false;
    }
    if (occupied_players() < 2U || occupied_players() > kSupportedOnlinePlayers) {
        error = "At least two racers must be in the lobby before testing the session.";
        return false;
    }
    constexpr std::array<TransportTrafficClass, 5U> required_traffic{{
        TransportTrafficClass::Checkpoint,
        TransportTrafficClass::Control,
        TransportTrafficClass::Authoritative,
        TransportTrafficClass::Realtime,
        TransportTrafficClass::Replica}};
    for (const PeerRecord& peer : peers_) {
        if (!peer.active) continue;
        for (const TransportTrafficClass traffic : required_traffic) {
            if (transport_->traffic_ready(peer.address, traffic)) continue;
            error = "A racer's network lanes are still warming up. Try the test again in a moment.";
            return false;
        }
    }

    reset_connection_test_locked();
    connection_test_results_ = {};
    connection_test_active_ = true;
    connection_test_id_ = next_connection_test_id_++;
    if (connection_test_id_ == 0U) connection_test_id_ = next_connection_test_id_++;
    if (next_connection_test_id_ == 0U) next_connection_test_id_ = 1U;
    const auto now = std::chrono::steady_clock::now();
    connection_test_started_ = now;
    connection_test_measurement_end_ = now + std::chrono::seconds(6);
    connection_test_drain_end_ = connection_test_measurement_end_ +
                                 std::chrono::seconds(1);
    connection_test_queue_failures_at_start_ = transport_queue_failures_;
    for (std::uint32_t& sequence : connection_test_next_sequence_) sequence = 1U;
    broadcast(protocol::MessageType::PreflightBegin,
              protocol::encode_preflight_begin(
                  {connection_test_id_, 7000U}));
    status_ = "Connection pre-flight is applying representative online traffic.";
    worker_wake_ = true;
    state_changed_.notify_all();
    error.clear();
    return true;
}

void DirectSession::reset_connection_test_locked() {
    if (connection_test_id_) terminal_connection_test_id_ = connection_test_id_;
    connection_test_active_ = false;
    connection_test_draining_ = false;
    connection_test_id_ = 0U;
    connection_test_started_ = {};
    connection_test_measurement_end_ = {};
    connection_test_drain_end_ = {};
    connection_test_last_send_ = {};
    connection_test_next_sequence_ = {};
    connection_test_sent_ = {};
    connection_test_received_ = {};
    for (auto& peer : connection_test_rtt_samples_)
        for (auto& samples : peer) samples.clear();
    for (auto& peer : connection_test_echoes_)
        for (auto& echoes : peer) echoes.clear();
    connection_test_queue_failures_at_start_ = transport_queue_failures_;
    connection_test_result_acks_ = {};
    connection_test_result_retry_until_ = {};
    connection_test_result_last_send_ = {};
}

void DirectSession::service_connection_test_locked(
    std::chrono::steady_clock::time_point now) {
    if (!connection_test_active_ || !is_host_) {
        if (connection_test_active_ && !is_host_ && now >= connection_test_drain_end_) {
            terminal_connection_test_id_ = connection_test_id_;
            connection_test_active_ = false;
            ++connection_test_result_generation_;
            status_ = "Connection pre-flight was inconclusive: final results did not arrive in time. You can retry the test or change Ready.";
        }
        if (is_host_ && !connection_test_active_ && now < connection_test_result_retry_until_ &&
            now - connection_test_result_last_send_ >= std::chrono::milliseconds(500)) {
            for (const auto& peer : peers_) {
                if (!peer.active) continue;
                for (const auto& result : connection_test_results_) {
                    if (!result.valid || (connection_test_result_acks_[peer.slot] & (1U << result.player_slot))) continue;
                    send_to(peer.address, protocol::MessageType::PreflightResult,
                        protocol::encode_preflight_result({connection_test_id_, result.player_slot, result.score,
                            result.p95_rtt_ms, result.jitter_ms,
                            static_cast<std::uint16_t>(result.loss_percent * 10.0F),
                            static_cast<std::uint16_t>(result.late_percent * 10.0F), result.queues_drained}));
                }
            }
            connection_test_result_last_send_ = now;
        }
        return;
    }

    constexpr std::array<protocol::MessageType, 5U> message_types{{
        protocol::MessageType::PreflightControlProbe,
        protocol::MessageType::PreflightAuthorityProbe,
        protocol::MessageType::PreflightRealtimeProbe,
        protocol::MessageType::PreflightReplicaProbe,
        protocol::MessageType::PreflightCheckpointProbe}};
    constexpr std::array<std::chrono::milliseconds, 5U> intervals{{
        std::chrono::milliseconds(100), std::chrono::milliseconds(33),
        std::chrono::milliseconds(33), std::chrono::milliseconds(200),
        std::chrono::milliseconds(100)}};
    constexpr std::array<std::size_t, 5U> padding_sizes{{
        16U, 192U, 24U, 880U, 880U}};

    if (now < connection_test_measurement_end_) {
        for (std::size_t lane = 0U; lane < message_types.size(); ++lane) {
            if (connection_test_last_send_[lane].time_since_epoch().count() != 0 &&
                now - connection_test_last_send_[lane] < intervals[lane]) {
                continue;
            }
            std::uint32_t sequence = connection_test_next_sequence_[lane]++;
            if (sequence == 0U) sequence = connection_test_next_sequence_[lane]++;
            if (connection_test_next_sequence_[lane] == 0U) {
                connection_test_next_sequence_[lane] = 1U;
            }
            protocol::PreflightProbePayload probe{
                connection_test_id_, sequence, steady_microseconds(), false,
                std::vector<std::uint8_t>(padding_sizes[lane],
                                          static_cast<std::uint8_t>(lane + 1U))};
            const auto payload = protocol::encode_preflight_probe(probe);
            for (const PeerRecord& peer : peers_) {
                if (!peer.active) continue;
                if (send_to(peer.address, message_types[lane], payload)) {
                    ++connection_test_sent_[peer.slot][lane];
                }
            }
            connection_test_last_send_[lane] = now;
        }
        return;
    }

    if (!connection_test_draining_) {
        connection_test_draining_ = true;
        status_ = "Connection pre-flight is draining and measuring its queues.";
    }
    if (now < connection_test_drain_end_) return;

    connection_test_results_ = {};
    connection_test_results_[local_slot_] = {
        true, local_slot_, 10U, 0U, 0U, 0.0F, 0.0F, true};
    int overall_score = 10;
    for (const PeerRecord& peer : peers_) {
    if (!peer.active) continue;
    std::vector<double> all_rtt;
    std::uint64_t total_sent = 0U;
    std::uint64_t total_received = 0U;
    std::uint64_t late_samples = 0U;
    double jitter_sum = 0.0;
    std::uint64_t jitter_samples = 0U;
    for (std::size_t lane = 0U; lane < connection_test_rtt_samples_[peer.slot].size();
         ++lane) {
        total_sent += connection_test_sent_[peer.slot][lane];
        total_received += connection_test_received_[peer.slot][lane];
        const auto& samples = connection_test_rtt_samples_[peer.slot][lane];
        all_rtt.insert(all_rtt.end(), samples.begin(), samples.end());
        for (std::size_t sample = 0U; sample < samples.size(); ++sample) {
            if (samples[sample] > 100.0) ++late_samples;
            if (sample != 0U) {
                jitter_sum += std::abs(samples[sample] - samples[sample - 1U]);
                ++jitter_samples;
            }
        }
    }
    std::sort(all_rtt.begin(), all_rtt.end());
    const double p95 = all_rtt.empty()
        ? 65535.0
        : all_rtt[static_cast<std::size_t>(std::ceil(
              static_cast<double>(all_rtt.size()) * 0.95)) - 1U];
    const double jitter = jitter_samples == 0U
        ? 0.0 : jitter_sum / static_cast<double>(jitter_samples);
    const double loss = total_sent == 0U
        ? 100.0
        : static_cast<double>(total_sent - (std::min)(total_sent, total_received)) *
              100.0 / static_cast<double>(total_sent);
    const double late = all_rtt.empty()
        ? 100.0
        : static_cast<double>(late_samples) * 100.0 /
              static_cast<double>(all_rtt.size());
    const bool queues_drained =
        critical_outbound_.empty() && repair_outbound_.empty() &&
        high_priority_outbound_.empty() && commit_outbound_.empty() &&
        authority_outbound_.empty() && normal_priority_outbound_.empty() &&
        bulk_outbound_.empty() &&
        transport_->buffered_bytes(TransportTrafficClass::Control) < 4096U &&
        transport_->buffered_bytes(TransportTrafficClass::Authoritative) < 4096U &&
        transport_->buffered_bytes(TransportTrafficClass::Checkpoint) < 4096U &&
        transport_->buffered_bytes(TransportTrafficClass::Realtime) < 4096U &&
        transport_->buffered_bytes(TransportTrafficClass::Replica) < 4096U &&
        transport_queue_failures_ == connection_test_queue_failures_at_start_;

    int score = 10;
    if (p95 > 60.0) --score;
    if (p95 > 100.0) score -= 2;
    if (p95 > 180.0) score -= 2;
    if (jitter > 8.0) --score;
    if (jitter > 12.0) --score;
    if (jitter > 30.0) --score;
    if (loss > 0.2) --score;
    if (loss > 0.5) --score;
    if (loss > 2.0) --score;
    if (late > 2.0) --score;
    if (late > 8.0) --score;
    if (!queues_drained) score -= 3;
    score = std::clamp(score, 1, 10);

        overall_score = (std::min)(overall_score, score);
        ConnectionTestResultView result{
            true, peer.slot, static_cast<std::uint8_t>(score),
            static_cast<std::uint16_t>(std::clamp(p95, 0.0, 65535.0)),
            static_cast<std::uint16_t>(std::clamp(jitter, 0.0, 65535.0)),
            static_cast<float>(loss), static_cast<float>(late), queues_drained};
        connection_test_results_[peer.slot] = result;
    }
    ++connection_test_result_generation_;
    if (connection_test_result_generation_ == 0U) {
        ++connection_test_result_generation_;
    }
    for (const ConnectionTestResultView& result : connection_test_results_) {
        if (!result.valid) continue;
        const auto payload = protocol::encode_preflight_result({
            connection_test_id_, result.player_slot, result.score,
            result.p95_rtt_ms, result.jitter_ms,
            static_cast<std::uint16_t>(std::clamp(
                std::lround(result.loss_percent * 10.0F), 0L, 1000L)),
            static_cast<std::uint16_t>(std::clamp(
                std::lround(result.late_percent * 10.0F), 0L, 1000L)),
            result.queues_drained});
        for (int copy = 0; copy < 3; ++copy) {
            broadcast(protocol::MessageType::PreflightResult, payload);
        }
    }
    connection_test_active_ = false;
    connection_test_draining_ = false;
    terminal_connection_test_id_ = connection_test_id_;
    connection_test_result_retry_until_ = now + std::chrono::seconds(15);
    status_ = overall_score >= 8
        ? "Connection pre-flight passed in the green band."
        : overall_score >= 5
            ? "Connection pre-flight completed in the orange band."
            : "Connection pre-flight found a red-band route.";
    state_changed_.notify_all();
}

bool DirectSession::all_active_players_acknowledged_locked(
    const std::array<bool, kMaximumPlayers>& acknowledgements) const {
    const Room& room = is_host_ ? lobby_.room() : room_view_;
    for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
        if (room.players[slot].occupied && !acknowledgements[slot]) return false;
    }
    return true;
}

std::chrono::milliseconds DirectSession::launch_control_timeout_locked(
    std::chrono::milliseconds minimum,
    std::chrono::milliseconds maximum) const {
    double worst_route_budget_ms = 0.0;
    for (const PeerRecord& peer : peers_) {
        if (!peer.active) continue;
        // One control stage is a host->guest message plus its acknowledgement.
        // Retried packets need additional jitter and loss headroom, but the
        // transaction must remain bounded if a racer has actually disappeared.
        const double route_budget_ms = peer.rtt_ms * 2.0 +
            peer.jitter_ms * 6.0 +
            static_cast<double>(peer.loss_percent) * 150.0;
        worst_route_budget_ms = (std::max)(worst_route_budget_ms,
                                           route_budget_ms);
    }
    const auto adaptive = minimum + std::chrono::milliseconds(
        static_cast<std::int64_t>(std::ceil(worst_route_budget_ms)));
    return std::clamp(adaptive, minimum, maximum);
}

void DirectSession::reset_launch_transaction_locked() {
    launch_stage_ = LaunchStage::Idle;
    launch_released_ = false;
    launch_countdown_active_ = false;
    launch_countdown_deadline_ = {};
    remote_countdown_remaining_ms_ = 0U;
    remote_countdown_received_at_ = {};
    last_countdown_ack_send_ = {};
    countdown_acks_ = {};
    pending_launch_epoch_ = 0U;
    pending_launch_prepare_.reset();
    launch_prepare_acks_ = {};
    launch_prepare_deadline_ = {};
    last_launch_prepare_send_ = {};
    remote_launch_prepare_.reset();
    launch_commit_acks_ = {};
    launch_commit_deadline_ = {};
    launch_commit_rebroadcast_until_ = {};
    last_launch_commit_send_ = {};
    launch_release_acks_ = {};
    launch_release_rebroadcast_until_ = {};
    last_launch_release_send_ = {};
}

void DirectSession::cancel_launch_locked(std::string_view reason) {
    const std::uint32_t cancelled_epoch = pending_launch_epoch_ != 0U
        ? pending_launch_epoch_
        : (remote_launch_prepare_ ? remote_launch_prepare_->launch_epoch
                                  : committed_launch_epoch_);
    if (is_host_ && cancelled_epoch != 0U) {
        const protocol::LaunchCancelPayload cancel{cancelled_epoch};
        broadcast(protocol::MessageType::LaunchCancel,
                  protocol::encode_launch_cancel(cancel));
    }
    reset_launch_transaction_locked();
    committed_launch_epoch_ = 0U;
    launch_descriptor_.reset();
    status_ = reason.empty()
        ? "The synchronized start was cancelled safely."
        : std::string(reason);
    if (is_host_) broadcast_lobby();
}

bool DirectSession::prepare_launch_locked(std::string& error) {
    if (owned_match_end_ && !owned_match_end_->resumed) {
        error = "The previous experimental match is still returning to the lobby.";
        return false;
    }
    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)) {
        error = "Only Player 1 can begin a synchronized lobby start.";
        return false;
    }
    if (!lobby_.can_start(&error)) return false;

    launch_countdown_active_ = false;
    launch_stage_ = LaunchStage::Idle;
    launch_countdown_deadline_ = {};
    remote_countdown_remaining_ms_ = 0U;
    remote_countdown_received_at_ = {};
    last_countdown_ack_send_ = {};
    countdown_acks_ = {};
    if (!lobby_.begin_loading(std::to_string(sender_id_), error)) return false;
    clear_friend_admissions_locked();
    room_view_ = lobby_.room();
    launch_descriptor_ = make_launch_descriptor();
    if (launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback) {
        const auto now = std::chrono::steady_clock::now();
        for (auto& peer : peers_) if (peer.active) peer.owned_last_packet = now;
        owned_last_host_packet_ = now;
        owned_inbound_.clear(); owned_inbound_bytes_ = 0;
    }
    if (!valid_launch_descriptor(*launch_descriptor_)) {
        lobby_.return_to_waiting();
        room_view_ = lobby_.room();
        launch_descriptor_.reset();
        error = "The synchronized launch roster is invalid.";
        return false;
    }
    state_ = ConnectionState::Loading;
    local_loaded_ = false;
    local_bootstrap_hash_ = 0U;
    local_runtime_save_hash_ = 0U;
    local_runtime_save_generation_ = 0U;
    bootstrap_hashes_ = {};
    bootstrap_hash_present_ = {};
    runtime_save_hashes_ = {};
    runtime_save_generations_ = {};
    runtime_save_present_ = {};
    timeline_.reset();
    clear_input_history_locked();
    frame_commits_.clear();
    commit_history_.clear();
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    rollback_inbound_.clear();
    authoritative_phase_active_ = false;
    authority_lifecycle_ = AuthorityLifecycle::Inactive;
    finish_seal_frame_.reset();
    state_hashes_.clear();
    gameplay_barrier_.reset();
    last_gameplay_barrier_broadcast_ = {};
    gameplay_resume_until_ = {};
    last_gameplay_ready_announcement_ = {};
    last_gameplay_resume_ack_ = {};
    gameplay_resume_ack_until_ = {};
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    last_authoritative_request_ = {};
    last_authoritative_request_frame_ = 0U;
    last_verified_frame_ = 0U;
    last_authoritative_frame_ = 0U;
    authoritative_corrections_ = 0U;
    consecutive_authoritative_frames_ = 0U;
    next_commit_frame_ = 0U;
    last_consumed_input_frame_.reset();
    last_consumed_inputs_ = {};
    input_epoch_ = 1U;
    retired_authority_epoch_ = 0U;
    pending_authority_epoch_ = 0U;
    stale_epoch_packets_ = 0U;
    future_epoch_packets_ = 0U;
    last_consumed_commit_hash_ = initial_commit_hash();
    reset_input_delivery_tracking_locked(0U);
    host_backpressure_events_ = 0U;
    maximum_peer_frame_debt_ = 0U;
    recovering_peer_count_ = 0U;
    launch_requested_ = true;
    const protocol::StartPayload start{
        0U, *launch_descriptor_, launch_descriptor_hash(*launch_descriptor_)};
    broadcast(protocol::MessageType::Start, protocol::encode_start(start));
    broadcast_lobby();
    status_ = "Launching every racer; waiting at the synchronized start gate.";
    replay_.begin(lobby_.room());
    error.clear();
    return true;
}

bool DirectSession::commit_launch_locked(const protocol::StartPayload& start,
                                         std::uint32_t launch_epoch,
                                         std::string& error) {
    if (launch_epoch == 0U) {
        error = "The synchronized launch epoch is invalid.";
        return false;
    }
    if (committed_launch_epoch_ == launch_epoch) {
        error.clear();
        return true;
    }
    if (committed_launch_epoch_ != 0U &&
        committed_launch_epoch_ != launch_epoch) {
        error = "A conflicting synchronized launch commit was received.";
        return false;
    }
    if (is_host_) {
        if (!pending_launch_prepare_ ||
            pending_launch_prepare_->launch_epoch != launch_epoch ||
            pending_launch_prepare_->start != start) {
            error = "The synchronized launch commit does not match Player 1's prepared roster.";
            return false;
        }
    } else if (!remote_launch_prepare_ ||
               remote_launch_prepare_->launch_epoch != launch_epoch ||
               remote_launch_prepare_->start != start) {
        error = "Player 1 committed a launch that this racer did not prepare.";
        return false;
    }
    if (!accept_start_descriptor(start, error)) {
        return false;
    }
    committed_launch_epoch_ = launch_epoch;
    const auto now = std::chrono::steady_clock::now();
    launch_stage_ = LaunchStage::Committing;
    launch_commit_acks_ = {};
    launch_commit_acks_[local_slot_] = true;
    launch_commit_deadline_ = now + launch_control_timeout_locked(
        std::chrono::seconds(5), std::chrono::seconds(15));
    launch_commit_rebroadcast_until_ = launch_commit_deadline_;
    last_launch_commit_send_ = {};
    launch_countdown_active_ = false;
    pending_launch_epoch_ = 0U;
    pending_launch_prepare_.reset();
    launch_prepare_acks_ = {};
    launch_prepare_deadline_ = {};
    last_launch_prepare_send_ = {};
    status_ = "Every racer validated the launch; arming the synchronized start.";
    if (is_host_) {
        const protocol::LaunchCommitPayload commit{launch_epoch};
        broadcast(protocol::MessageType::LaunchCommit,
                  protocol::encode_launch_commit(commit));
        last_launch_commit_send_ = now;
    }
    error.clear();
    return true;
}

bool DirectSession::release_launch_locked(std::uint32_t launch_epoch,
                                          std::string& error) {
    if (launch_epoch == 0U || committed_launch_epoch_ != launch_epoch) {
        error = "The synchronized launch release does not match the armed start.";
        return false;
    }
    if (launch_released_) {
        error.clear();
        return true;
    }
    if (is_host_) {
        if (!lobby_.begin_loading(std::to_string(sender_id_), error)) {
            return false;
        }
        clear_friend_admissions_locked();
        room_view_ = lobby_.room();
    }

    launch_released_ = true;
    launch_stage_ = LaunchStage::Releasing;
    state_ = ConnectionState::Loading;
    local_loaded_ = false;
    local_bootstrap_hash_ = 0U;
    local_runtime_save_hash_ = 0U;
    local_runtime_save_generation_ = 0U;
    bootstrap_hashes_ = {};
    bootstrap_hash_present_ = {};
    runtime_save_hashes_ = {};
    runtime_save_generations_ = {};
    runtime_save_present_ = {};
    timeline_.reset();
    clear_input_history_locked();
    frame_commits_.clear();
    commit_history_.clear();
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    rollback_inbound_.clear();
    authoritative_phase_active_ = false;
    authority_lifecycle_ = AuthorityLifecycle::Inactive;
    finish_seal_frame_.reset();
    state_hashes_.clear();
    gameplay_barrier_.reset();
    last_gameplay_barrier_broadcast_ = {};
    gameplay_resume_until_ = {};
    last_gameplay_ready_announcement_ = {};
    last_gameplay_resume_ack_ = {};
    gameplay_resume_ack_until_ = {};
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    last_authoritative_request_ = {};
    last_authoritative_request_frame_ = 0U;
    last_verified_frame_ = 0U;
    last_authoritative_frame_ = 0U;
    authoritative_corrections_ = 0U;
    consecutive_authoritative_frames_ = 0U;
    next_commit_frame_ = 0U;
    last_consumed_input_frame_.reset();
    last_consumed_inputs_ = {};
    input_epoch_ = 1U;
    retired_authority_epoch_ = 0U;
    pending_authority_epoch_ = 0U;
    stale_epoch_packets_ = 0U;
    future_epoch_packets_ = 0U;
    last_consumed_commit_hash_ = initial_commit_hash();
    reset_input_delivery_tracking_locked(0U);
    host_backpressure_events_ = 0U;
    maximum_peer_frame_debt_ = 0U;
    recovering_peer_count_ = 0U;
    launch_requested_ = true;
    if (is_host_) {
        broadcast_lobby();
    }
    status_ = "Launching every racer; waiting at the synchronized start gate.";
    replay_.begin(is_host_ ? lobby_.room() : room_view_);
    error.clear();
    return true;
}

bool DirectSession::consume_launch_request() {
    std::scoped_lock lock(mutex_);
    const bool result = launch_requested_;
    launch_requested_ = false;
    return result;
}

void DirectSession::mark_game_loaded(std::uint64_t bootstrap_hash,
                                     std::uint32_t online_save_generation,
                                     std::uint64_t online_save_hash,std::uint64_t expected_launch_hash) {
    std::scoped_lock lock(mutex_);
    if(expected_launch_hash&&(!launch_descriptor_||launch_descriptor_hash(*launch_descriptor_)!=expected_launch_hash||
       state_!=ConnectionState::Loading||(owned_match_end_&&!owned_match_end_->resumed)))return;
    if (local_loaded_ || state_ == ConnectionState::Offline ||
        state_ == ConnectionState::Failed || bootstrap_hash == 0U) return;
    if(owned_backend_available_&&((owned_match_end_&&!owned_match_end_->resumed)||
       (launch_descriptor_&&launch_descriptor_->synchronization==SynchronizationMode::ExperimentalRollback&&state_!=ConnectionState::Loading)))return;
    if (online_save_generation != session_save_generation_ ||
        online_save_hash == 0U ||
        online_save_hash != manifest_.session_save_hash ||
        !local_online_save_ready_) {
        fail_locked(
            "The runtime opened an online save that does not match the lobby. "
            "The session was halted before simulation frame 0.");
        return;
    }
    local_loaded_ = true;
    local_bootstrap_hash_ = bootstrap_hash;
    local_runtime_save_hash_ = online_save_hash;
    local_runtime_save_generation_ = online_save_generation;
    bootstrap_hashes_[local_slot_] = bootstrap_hash;
    bootstrap_hash_present_[local_slot_] = true;
    runtime_save_hashes_[local_slot_] = online_save_hash;
    runtime_save_generations_[local_slot_] = online_save_generation;
    runtime_save_present_[local_slot_] = true;
    state_ = ConnectionState::Loading;
    if (is_host_) {
        lobby_.set_loaded(std::to_string(sender_id_), true);
        room_view_ = lobby_.room();
        broadcast_lobby();
    } else {
        send_loaded_locked();
        last_client_request_ = std::chrono::steady_clock::now();
    }
}

void DirectSession::send_loaded_locked() {
    const protocol::LoadedPayload loaded{local_slot_,local_bootstrap_hash_,local_runtime_save_generation_,local_runtime_save_hash_};
    if(launch_descriptor_&&launch_descriptor_->synchronization==SynchronizationMode::ExperimentalRollback) {
        send_to(host_address_,protocol::MessageType::ExperimentalLoaded,
            protocol::encode_experimental_loaded({launch_descriptor_hash(*launch_descriptor_),loaded}));
    }else send_to(host_address_,protocol::MessageType::Loaded,protocol::encode_loaded(loaded));
}

void DirectSession::fail_runtime_start(std::string reason) {
    std::scoped_lock lock(mutex_);
    fail_locked(std::move(reason));
}

bool DirectSession::wait_until_running(std::chrono::milliseconds timeout) {
    const auto wait_started = std::chrono::steady_clock::now();
    const auto deadline = wait_started + timeout;
    std::unique_lock lock(mutex_);
    worker_wake_ = true;
    state_changed_.notify_all();
    state_changed_.wait_until(lock, deadline, [this] {
        return state_ == ConnectionState::Running ||
               state_ == ConnectionState::Failed ||
               state_ == ConnectionState::Offline;
    });
    if (state_ == ConnectionState::Running) return true;
    if (state_ == ConnectionState::Failed || state_ == ConnectionState::Offline) {
        return false;
    }
    fail_locked("Timed out waiting for every racer to load.");
    return false;
}

bool DirectSession::synchronize_inputs(std::uint32_t frame, PackedInput local,
                                       FrameInputs& inputs,
                                       std::chrono::milliseconds timeout) {
    const InputSynchronizationResult result =
        synchronize_inputs_result(frame, local, inputs, timeout);
    return result == InputSynchronizationResult::Committed ||
           result == InputSynchronizationResult::AlreadyCommitted;
}

InputSynchronizationResult DirectSession::synchronize_inputs_result(
    std::uint32_t frame, PackedInput local, FrameInputs& inputs,
    std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (state_ != ConnectionState::Running) {
        return InputSynchronizationResult::Failed;
    }
    if (gameplay_handoff_ && gameplay_handoff_->suspended &&
        frame >= gameplay_handoff_->local_suspend_frame) {
        inputs = {};
        return InputSynchronizationResult::Suspended;
    }
    // A late recovery announcement may race the outer coordinator, but not
    // this lock: complete its native boundary once, then park admission.
    if (recovery_frame_ && !recovery_resumed_ && frame > *recovery_frame_) {
        inputs = {};
        return InputSynchronizationResult::Suspended;
    }
    if (frame != next_commit_frame_) {
        // DKR can invoke its retail input boundary once more after the outer
        // authored tick has already committed the same simulation frame. This
        // is a read-after-commit, not a second tick. Return only the exact
        // sample consumed locally and leave every session cursor/hash/history
        // untouched. Any older, future, or unverifiable request remains fatal.
        if (next_commit_frame_ > 0U && frame == next_commit_frame_ - 1U &&
            last_consumed_input_frame_ &&
            *last_consumed_input_frame_ == frame) {
            inputs = last_consumed_inputs_;
            failure_recorder().record(
                FailureEventKind::ProgressWatchdog, frame,
                next_commit_frame_, 0U,
                "idempotent duplicate authored-input boundary");
            return InputSynchronizationResult::AlreadyCommitted;
        }
        fail_locked("The local simulation requested frame " +
                    std::to_string(frame) + " while Player 1's authoritative "
                    "timeline expected frame " +
                    std::to_string(next_commit_frame_) + ".");
        return InputSynchronizationResult::Failed;
    }
    std::uint32_t target = frame >
            std::numeric_limits<std::uint32_t>::max() - input_delay_
        ? std::numeric_limits<std::uint32_t>::max()
        : frame + input_delay_;
    const bool client_host_timed = !is_host_ && launch_descriptor_ &&
        authoritative_inputs_valid_;
    if (client_host_timed) {
        // A guest's physical-input clock must follow Player 1's newest commit,
        // not the guest's potentially parked presentation frame. This is
        // essential in strict lockstep too: otherwise a guest waiting for a
        // commit repeatedly fills the same future slot, Player 1 exhausts the
        // input-delay runway, and a 30 Hz game advances only every other host
        // tick. Any skipped slots retain the previous pad sample, while the
        // fresh sample begins at the next useful host-relative target.
        const std::uint32_t host_next = authoritative_input_frame_ ==
                std::numeric_limits<std::uint32_t>::max()
            ? authoritative_input_frame_
            : authoritative_input_frame_ + 1U;
        const std::uint32_t host_target = host_next >
                std::numeric_limits<std::uint32_t>::max() - input_delay_
            ? std::numeric_limits<std::uint32_t>::max()
            : host_next + input_delay_;
        target = (std::max)(target, host_target);
    }
    std::optional<std::uint32_t> newly_filled_first_frame;
    if (!pending_local_submission_frame_ ||
        *pending_local_submission_frame_ != target ||
        local_input_submitted_ != local) {
        if (!local_history_.empty() &&
            local_input_submitted_frame_ < target -
                static_cast<std::uint32_t>(target > 0U)) {
            // Only the retained repair window needs explicit hold-last
            // samples. If a client has just recovered from a very large debt,
            // do not manufacture and enqueue thousands of obsolete entries.
            const std::uint32_t retained_first = target > 255U
                ? target - 255U : 0U;
            const std::uint32_t first_missing = (std::max)(
                local_input_submitted_frame_ + 1U, retained_first);
            newly_filled_first_frame = first_missing;
            for (std::uint32_t missing = first_missing;
                 missing < target; ++missing) {
                store_local_input_locked(missing, local_input_submitted_, false);
            }
        }
        store_local_input_locked(target, local);
        pending_local_submission_frame_ = target;
        local_input_submitted_frame_ = target;
        local_input_submitted_ = local;
        if (is_host_) host_input_accepted_frame_ = target;
        // InputAck owns retention. A fixed frame-count window used to discard
        // the exact samples a reliable repair needed after a long Wi-Fi/VPN
        // pause. Healthy sessions remain small because acknowledged history is
        // pruned immediately in the InputAck handler below.
        if (is_host_ && frame == 0U) {
            const std::uint8_t players = occupied_players();
            for (std::uint32_t initial = 0; initial < input_delay_; ++initial) {
                for (std::uint8_t slot = 0; slot < players; ++slot) {
                    if (slot == local_slot_) timeline_.set_local(slot, initial, {});
                    else timeline_.set_remote(slot, initial, {});
                }
            }
        }
        send_local_history(target);
        if (newly_filled_first_frame) {
            // Host-relative catch-up can advance a guest's physical-input
            // target by more than the live packet's sixteen-frame rolling
            // window. Send the newly synthesized contiguous range on the
            // repair path immediately; otherwise Player 1 retains an old hole
            // until it eventually stalls and asks for it, defeating the very
            // catch-up policy that created the jump.
            send_local_history(target, true, *newly_filled_first_frame);
        }
        last_input_history_send_ = std::chrono::steady_clock::now();
    }
    worker_wake_ = true;
    state_changed_.notify_all();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const std::uint8_t occupied_mask = launch_descriptor_
        ? launch_descriptor_->occupied_mask : 0U;
    const SynchronizationMode synchronization = launch_descriptor_
        ? launch_descriptor_->synchronization
        : SynchronizationMode::Lockstep;
    const bool host_owned_prediction = host_may_predict_input(
        authoritative_phase_active_, synchronization);
    const bool host_owned_rollback = authoritative_phase_active_ &&
        synchronization == SynchronizationMode::Rollback;
    const auto contiguous_input_window_exhausted = [&] {
        if (!is_host_ || !host_owned_rollback || !launch_descriptor_) {
            return false;
        }
        const std::uint32_t window = launch_descriptor_->rollback_window;
        for (std::uint8_t slot = 0U; slot < kMaximumPlayers; ++slot) {
            if (slot == local_slot_ ||
                (occupied_mask & static_cast<std::uint8_t>(1U << slot)) ==
                    0U) {
                continue;
            }
            const std::uint32_t missing =
                peer_input_first_missing_frame_[slot];
            if (frame >= missing && frame - missing >= window) return true;
        }
        return false;
    };
    const auto awaiting_commit = [&] {
        return (is_host_ && !host_owned_prediction &&
            !timeline_.confirmed_mask(frame, occupied_mask)) ||
           (is_host_ && host_owned_prediction &&
            authoritative_phase_active_ && launch_descriptor_ &&
            (timeline_.unconfirmed_runway(frame, occupied_mask) >
                 launch_descriptor_->rollback_window ||
             contiguous_input_window_exhausted())) ||
           (!is_host_ && !frame_commits_.contains(frame));
    };
    const auto begin_or_continue_pending_wait = [&](const auto now) {
        if (!pending_commit_wait_frame_ || *pending_commit_wait_frame_ != frame) {
            pending_commit_wait_frame_ = frame;
            pending_commit_wait_started_ = now;
            last_commit_repair_request_ = {};
            ++input_stalls_;
        }
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(now - pending_commit_wait_started_);
        const std::uint64_t elapsed_ms = elapsed.count() > 0
            ? static_cast<std::uint64_t>(elapsed.count())
            : std::uint64_t{0};
        longest_input_stall_ms_ = (std::max)(
            longest_input_stall_ms_,
            static_cast<std::uint32_t>((std::min)(
                elapsed_ms,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()))));
    };
    const auto service_pending_repair = [&](const auto now) {
        if (last_input_history_send_.time_since_epoch().count() == 0 ||
            now - last_input_history_send_ >= std::chrono::milliseconds(100)) {
            send_local_history(target);
            if (!is_host_ && host_input_first_missing_frame_ <= target) {
                send_local_history(target, true);
            }
            if (is_host_ && frame > 0U) send_commit_history(frame - 1U, 8U);
            last_input_history_send_ = now;
        }
        if (last_commit_repair_request_.time_since_epoch().count() == 0 ||
            now - last_commit_repair_request_ >=
                std::chrono::milliseconds(50)) {
            if (is_host_) advertise_missing_inputs_locked();
            else request_missing_commit_locked(frame);
            last_commit_repair_request_ = now;
        }
        worker_wake_ = true;
        state_changed_.notify_all();
    };
    bool waiting = awaiting_commit();
    if (!waiting) {
        const bool recovery_was_visible = prediction_failsafe_active_ ||
            status_.find("reliable recovery is still active") !=
                std::string::npos ||
            status_.find("being predicted while its history is repaired") !=
                std::string::npos;
        reset_pending_commit_wait_locked();
        prediction_failsafe_active_ = false;
        if (recovery_was_visible) {
            status_ = "The synchronized race recovered cleanly.";
        }
    }
    if (waiting && timeout <= std::chrono::milliseconds::zero()) {
        const auto now = std::chrono::steady_clock::now();
        begin_or_continue_pending_wait(now);
        service_pending_repair(now);

        // The native world is not rewindable. Once the prediction window is
        // exhausted, publishing more immutable guesses loses client controls.
        // Yield immediately; never block the UI thread or bypass the window.
        if (is_host_ && host_owned_prediction) {
            if (!prediction_failsafe_active_) {
                prediction_failsafe_active_ = true;
                ++prediction_limit_waits_;
                status_ = "Waiting for a racer's inputs at the prediction limit; recovery is active.";
            }
        }
        {
            // A latency spike is a soft simulation stall, not proof of a
            // divergent session. Keep the UI/network worker responsive and
            // continue reliable repair until the route recovers or the player
            // explicitly leaves. The old eight-second failure converted an
            // ordinary VPN/Wi-Fi outage into a permanent disconnect.
            if (now - pending_commit_wait_started_ >=
                kPendingCommitRepairTimeout) {
                status_ = is_host_
                    ? "Waiting for a racer's authored inputs; reliable recovery is still active."
                    : "Waiting for Player 1's frame commits; reliable recovery is still active.";
            }
            if (now - pending_commit_wait_started_ >= std::chrono::seconds(90)) {
                fail_locked("No authenticated input progress for 90 seconds. The connection could not recover; leave the session and reconnect. Your separate saves are retained.");
                return InputSynchronizationResult::Failed;
            }
            return InputSynchronizationResult::Pending;
        }
    }
    while (waiting && awaiting_commit()) {
        if (state_ != ConnectionState::Running) {
            return InputSynchronizationResult::Failed;
        }
        if (gameplay_handoff_ && gameplay_handoff_->suspended &&
            frame >= gameplay_handoff_->local_suspend_frame) {
            inputs = {};
            return InputSynchronizationResult::Suspended;
        }
        if (recovery_frame_ && !recovery_resumed_ && frame > *recovery_frame_) {
            inputs = {};
            return InputSynchronizationResult::Suspended;
        }
        const auto now = std::chrono::steady_clock::now();
        begin_or_continue_pending_wait(now);
        service_pending_repair(now);
        if (now >= deadline) {
            fail_locked("A racer stopped responding. The synchronized session was halted.");
            return InputSynchronizationResult::Failed;
        }
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    // The wait releases mutex_: a commit and a boundary can arrive together.
    // Re-check admission even when awaiting_commit() has just become false.
    if (state_ != ConnectionState::Running) {
        return InputSynchronizationResult::Failed;
    }
    if ((gameplay_handoff_ && gameplay_handoff_->suspended &&
         frame >= gameplay_handoff_->local_suspend_frame) ||
        (recovery_frame_ && !recovery_resumed_ && frame > *recovery_frame_)) {
        inputs = {};
        return InputSynchronizationResult::Suspended;
    }
    reset_pending_commit_wait_locked();
    if (is_host_) {
        protocol::FrameCommitPayload commit =
            make_frame_commit_locked(frame, 0U);
        frame_commits_[frame] = commit;
        commit_history_.push_back(commit);
        while (commit_history_.size() > kCommitHistoryCapacity) {
            commit_history_.pop_front();
        }
        // Every authored frame carries the current commit plus two immediate
        // predecessors. The wider eight-frame repair window is reserved for
        // a parked/retransmit path above. This preserves loss recovery while
        // avoiding the old eightfold steady-state packet amplification on
        // every client and every frame.
        // Quick Join carries an overlapping authenticated history on its
        // dedicated reliable/unordered authority lane. UDP retains two
        // predecessors as inexpensive forward-error recovery and both routes
        // can service exact repair requests from the longer retained chain.
        send_commit_history(
            frame, steady_commit_history_count(transport_->quick_join()));
        if (commit.predicted_mask != 0U || frame % 30U == 0U) {
            failure_recorder().record(
                FailureEventKind::FrameCommitted, frame,
                commit.predicted_mask,
                static_cast<std::uint32_t>(commit.commit_hash ^
                                           (commit.commit_hash >> 32U)),
                commit.predicted_mask != 0U ? "predicted host commit"
                                            : "sampled host commit");
        }
    }

    const auto found = frame_commits_.find(frame);
    if (found == frame_commits_.end()) {
        fail_locked("Player 1 did not publish the authoritative frame commit.");
        return InputSynchronizationResult::Failed;
    }
    std::string commit_error;
    if (!validate_commit(found->second, commit_error) ||
        found->second.previous_hash != last_consumed_commit_hash_) {
        fail_locked(commit_error.empty()
            ? "Player 1's authoritative frame chain is discontinuous at frame " +
                  std::to_string(frame) + "."
            : std::move(commit_error));
        return InputSynchronizationResult::Failed;
    }
    inputs = found->second.inputs;
    record_input_latency_locked(found->second, true);
    last_consumed_commit_hash_ = found->second.commit_hash;
    last_consumed_input_frame_ = frame;
    last_consumed_inputs_ = inputs;
    // This is the newest host commit received/produced, not merely the commit
    // consumed by the local simulation. A client can receive several commits
    // in one network pump while its renderer is behind. Moving this watermark
    // backwards to the just-consumed frame hid that debt from the pacing
    // policy, prevented bounded catch-up, and let clients drift seconds behind
    // Player 1 during a race.
    authoritative_input_frame_ = (std::max)(
        authoritative_input_frame_, frame);
    authoritative_inputs_ = inputs;
    authoritative_inputs_valid_ = true;
    authoritative_input_revision_ = found->second.revision;
    authoritative_predicted_mask_ = found->second.predicted_mask;
    pending_local_submission_frame_.reset();
    ++next_commit_frame_;
    if (is_host_) replay_.record(frame, inputs);
    if (frame > 128U) frame_commits_.erase(frame - 128U);
    return InputSynchronizationResult::Committed;
}

bool DirectSession::authoritative_inputs_for(
    std::uint32_t frame, FrameInputs& inputs, std::uint16_t& revision) const {
    std::scoped_lock lock(mutex_);
    const auto found = frame_commits_.find(frame);
    if (found == frame_commits_.end()) return false;
    inputs = found->second.inputs;
    revision = found->second.revision;
    return true;
}

std::uint32_t DirectSession::contiguous_authoritative_commits(
    std::uint32_t first_frame, std::uint32_t maximum_count) const {
    std::scoped_lock lock(mutex_);
    std::uint32_t count = 0U;
    while (count < maximum_count &&
           first_frame <=
               std::numeric_limits<std::uint32_t>::max() - count &&
           frame_commits_.contains(first_frame + count)) {
        ++count;
    }
    return count;
}

bool DirectSession::fast_forward_authoritative_commits(
    std::uint32_t next_frame, std::string& error) {
    return install_fast_forward(next_frame, [] { return true; }, error) ==
        SessionPollResult::Ready;
}

SessionPollResult DirectSession::install_fast_forward(
    std::uint32_t next_frame, const std::function<bool()>& installer,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (is_host_ || state_ != ConnectionState::Running ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing ||
        !launch_descriptor_ ||
        launch_descriptor_->synchronization !=
            SynchronizationMode::Rollback) {
        error = "Only a running rollback guest can fast-forward to Player 1's state.";
        return SessionPollResult::Failed;
    }
    // A portable snapshot must not skip a native lifecycle/capture hook.
    if (gameplay_handoff_ || (gameplay_barrier_ && !gameplay_barrier_->go_released) || transition_barrier_ ||
        recovery_frame_ || finish_seal_frame_) {
        error.clear();
        return SessionPollResult::Pending;
    }
    if (next_frame < next_commit_frame_) {
        error = "The Player 1 state fast-forward boundary moved backwards.";
        return SessionPollResult::Failed;
    }
    if (next_frame == next_commit_frame_) {
        error.clear();
        return SessionPollResult::Ready;
    }

    // Validate the complete immutable chain first. Nothing below mutates the
    // consumed cursor until every skipped commit is present and linked to the
    // exact hash of the last frame this guest actually simulated.
    std::uint64_t previous_hash = last_consumed_commit_hash_;
    FrameInputs last_inputs{};
    std::uint16_t last_revision = 0U;
    std::uint8_t last_predicted_mask = 0U;
    for (std::uint32_t frame = next_commit_frame_; frame < next_frame;
         ++frame) {
        const auto found = frame_commits_.find(frame);
        if (found == frame_commits_.end()) {
            request_missing_commit_locked(frame);
            worker_wake_ = true;
            state_changed_.notify_all();
            error.clear();
            return SessionPollResult::Pending;
        }
        std::string commit_error;
        if (!validate_commit(found->second, commit_error) ||
            found->second.previous_hash != previous_hash) {
            error = commit_error.empty()
                ? "Player 1's state fast-forward chain is discontinuous at frame " +
                      std::to_string(frame) + "."
                : std::move(commit_error);
            return SessionPollResult::Failed;
        }
        previous_hash = found->second.commit_hash;
        last_inputs = found->second.inputs;
        last_revision = found->second.revision;
        last_predicted_mask = found->second.predicted_mask;
    }

    // The lock pins the complete chain and epoch across installation. The
    // installer stages all RDRAM writes, returning false without mutation.
    if (!installer()) {
        error.clear();
        return SessionPollResult::Pending;
    }
    const std::uint32_t last_frame = next_frame - 1U;
    last_consumed_commit_hash_ = previous_hash;
    last_consumed_input_frame_ = last_frame;
    last_consumed_inputs_ = last_inputs;
    authoritative_input_revision_ = last_revision;
    authoritative_predicted_mask_ = last_predicted_mask;
    pending_local_submission_frame_.reset();
    pending_input_correction_.reset();
    next_commit_frame_ = next_frame;
    if (next_frame > 128U) {
        const std::uint32_t oldest = next_frame - 128U;
        std::erase_if(frame_commits_, [oldest](const auto& entry) {
            return entry.first < oldest;
        });
    }
    ++simulation_wake_generation_;
    state_changed_.notify_all();
    error.clear();
    native_completed_frame_ = last_frame;
    return SessionPollResult::Ready;
}

std::optional<std::uint32_t> DirectSession::native_completed_frame() const {
    std::scoped_lock lock(mutex_);
    return native_completed_frame_;
}

std::uint64_t DirectSession::synchronization_progress() const {
    std::scoped_lock lock(mutex_);
    std::uint64_t marker = 14695981039346656037ULL;
    const auto add = [&](std::uint64_t value) { marker = (marker ^ value) * 1099511628211ULL; };
    add(input_epoch_); add(scene_epoch_); add(next_commit_frame_);
    for (std::size_t slot = 0; slot < kMaximumPlayers; ++slot) {
        if (simulation_progress_present_[slot]) add(simulation_completed_frame_[slot]);
        add(recovery_acks_[slot]);
        if (gameplay_barrier_) { add(gameplay_barrier_->ready[slot]); add(gameplay_barrier_->armed[slot]); }
        if (transition_barrier_) add(transition_barrier_->acknowledgements[slot]);
    }
    if (gameplay_barrier_) {
        add(gameplay_barrier_->epoch_synchronized); add(gameplay_barrier_->baseline_released);
        add(gameplay_barrier_->arm_announced); add(gameplay_barrier_->go_released);
    }
    add(recovery_resumed_);
    // Commutative aggregate: unordered-map iteration order is not progress.
    std::uint64_t pieces = 0;
    for (const auto& [frame, assembly] : snapshot_assemblies_)
        pieces += (static_cast<std::uint64_t>(frame) + 1U) *
            (1U + std::count(assembly.present.begin(), assembly.present.end(), true));
    add(pieces); add(authoritative_states_.size());
    std::uint64_t acknowledgements = 0;
    for (const auto& [frame, present] : authoritative_acknowledgements_)
        acknowledgements += (static_cast<std::uint64_t>(frame) + 1U) *
            std::count(present.begin(), present.end(), true);
    add(acknowledgements);
    return marker;
}

void DirectSession::report_simulation_progress(
    std::uint32_t completed_frame, TimelineProgressScope scope) {
    std::scoped_lock lock(mutex_);
    const bool gameplay = scope == TimelineProgressScope::Gameplay;
    const bool gameplay_timeline =
        authoritative_phase_active_ &&
        authority_lifecycle_ == AuthorityLifecycle::Racing &&
        scene_epoch_ != 0U;
    const bool frontend_timeline =
        !authoritative_phase_active_ &&
        (authority_lifecycle_ == AuthorityLifecycle::Inactive ||
         authority_lifecycle_ == AuthorityLifecycle::PostRace) &&
        (!gameplay_handoff_ || !gameplay_handoff_->suspended);
    if (state_ != ConnectionState::Running || !launch_descriptor_ ||
        (gameplay ? !gameplay_timeline : !frontend_timeline) ||
        completed_frame >= next_commit_frame_) {
        return;
    }
    const std::uint32_t progress_scene_epoch = gameplay ? scene_epoch_ : 0U;
    if (!native_completed_frame_ || completed_frame > *native_completed_frame_)
        native_completed_frame_ = completed_frame;
    const auto now = std::chrono::steady_clock::now();
    if (simulation_progress_present_[local_slot_] &&
        completed_frame < simulation_completed_frame_[local_slot_]) {
        return;
    }
    const bool advanced = !simulation_progress_present_[local_slot_] ||
        completed_frame > simulation_completed_frame_[local_slot_];
    if (!advanced &&
        now - simulation_progress_time_[local_slot_] <
            std::chrono::milliseconds(50)) {
        return;
    }
    simulation_completed_frame_[local_slot_] = completed_frame;
    simulation_progress_present_[local_slot_] = true;
    simulation_progress_time_[local_slot_] = now;
    // Normal client input batches carry this same watermark. Keep a sparse
    // standalone heartbeat while the route is healthy, but report promptly
    // when Player 1 is more than two completed frames ahead. That gives the
    // host's bounded lead controller fresh evidence during a guest hitch
    // without adding a permanent 30 Hz control stream.
    const bool frame_debt_active = !is_host_ &&
        last_authoritative_frame_ > completed_frame &&
        last_authoritative_frame_ - completed_frame > 2U;
    const auto heartbeat_interval = frame_debt_active
        ? std::chrono::milliseconds(50)
        : std::chrono::milliseconds(200);
    const bool progress_heartbeat_due =
        last_simulation_progress_datagram_ ==
            std::chrono::steady_clock::time_point{} ||
        now - last_simulation_progress_datagram_ >=
            heartbeat_interval;
    if (!is_host_ && progress_heartbeat_due) {
        const auto payload = protocol::encode_simulation_progress({
            progress_scene_epoch, authority_epoch(), completed_frame,
            local_slot_});
        if (!payload.empty()) {
            send_to(host_address_, protocol::MessageType::SimulationProgress,
                    payload, completed_frame);
            last_simulation_progress_datagram_ = now;
            worker_wake_ = true;
            state_changed_.notify_all();
        }
    }
}

bool DirectSession::host_should_backpressure(
    std::uint32_t next_frame, std::uint32_t maximum_lead,
    TimelineProgressScope scope) const {
    std::scoped_lock lock(mutex_);
    const bool gameplay = scope == TimelineProgressScope::Gameplay;
    const bool gameplay_timeline =
        authoritative_phase_active_ &&
        authority_lifecycle_ == AuthorityLifecycle::Racing &&
        scene_epoch_ != 0U;
    const bool frontend_timeline =
        !authoritative_phase_active_ &&
        (authority_lifecycle_ == AuthorityLifecycle::Inactive ||
         authority_lifecycle_ == AuthorityLifecycle::PostRace) &&
        (!gameplay_handoff_ || !gameplay_handoff_->suspended);
    if (!is_host_ || state_ != ConnectionState::Running ||
        !launch_descriptor_ ||
        (gameplay ? !gameplay_timeline : !frontend_timeline)) {
        host_backpressure_active_ = false;
        maximum_peer_frame_debt_ = 0U;
        recovering_peer_count_ = 0U;
        return false;
    }
    const Room& room = lobby_.room();
    const SynchronizationMode mode =
        launch_descriptor_->synchronization;
    const std::uint32_t effective_limit = gameplay
        ? effective_host_authority_lead_limit(mode, maximum_lead)
        : maximum_lead;
    const std::uint32_t release_limit =
        host_backpressure_release_limit(effective_limit);
    std::uint32_t current_maximum_debt = 0U;
    std::uint32_t current_recovering_peers = 0U;
    bool occupied_remote_peer = false;
    bool hard_limit_reached = false;
    for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
        if (slot == local_slot_ || !room.players[slot].occupied) {
            continue;
        }
        occupied_remote_peer = true;
        if (!simulation_progress_present_[slot]) continue;
        const std::uint32_t debt = next_frame >
                simulation_completed_frame_[slot]
            ? next_frame - simulation_completed_frame_[slot] : 0U;
        current_maximum_debt = (std::max)(current_maximum_debt, debt);
        if (debt > maximum_lead) ++current_recovering_peers;
        hard_limit_reached = hard_limit_reached || host_backpressure_required(
            next_frame, simulation_completed_frame_[slot], effective_limit);
    }
    maximum_peer_frame_debt_ = current_maximum_debt;
    recovering_peer_count_ = current_recovering_peers;
    if (!occupied_remote_peer) {
        host_backpressure_active_ = false;
        return false;
    }
    if (!host_backpressure_active_) {
        host_backpressure_active_ = hard_limit_reached;
    } else if (current_maximum_debt <= release_limit) {
        host_backpressure_active_ = false;
    }
    if (host_backpressure_active_) ++host_backpressure_events_;
    return host_backpressure_active_;
}

void DirectSession::wait_for_simulation_progress(
    std::uint64_t observed_generation,
    std::chrono::milliseconds timeout) {
    if (timeout <= std::chrono::milliseconds::zero()) return;
    std::unique_lock lock(mutex_);
    state_changed_.wait_for(lock, timeout, [this, observed_generation] {
        return worker_stop_.load(std::memory_order_acquire) ||
               state_ != ConnectionState::Running ||
               simulation_wake_generation_ != observed_generation;
    });
}

std::optional<std::uint32_t>
DirectSession::take_authoritative_input_correction() {
    std::scoped_lock lock(mutex_);
    const auto correction = pending_input_correction_;
    pending_input_correction_.reset();
    return correction;
}

bool DirectSession::adopt_gameplay_handoff_locked(
    const protocol::GameplayHandoffPayload& handoff,
    bool local_load_started, std::string& error) {
    const bool request_timeline_matches =
        handoff.resume_input_epoch == 0U &&
        handoff.boundary_frame <= next_commit_frame_;
    const bool authoritative_timeline_matches =
        handoff.resume_input_epoch != 0U &&
        handoff.boundary_frame >= next_commit_frame_;
    if (state_ != ConnectionState::Running || !launch_descriptor_ ||
        handoff.current_input_epoch != input_epoch_ ||
        (!request_timeline_matches && !authoritative_timeline_matches)) {
        error = "The gameplay handoff did not match the active input timeline.";
        return false;
    }
    if (gameplay_handoff_ &&
        (gameplay_handoff_->current_input_epoch !=
             handoff.current_input_epoch ||
         gameplay_handoff_->map != handoff.map)) {
        error = "The racers attempted to load conflicting tracks.";
        return false;
    }
    if (!gameplay_handoff_) {
        // An authoritative Suspend names the first frame that must not be
        // consumed in the old frontend epoch.  A guest can receive it while
        // one committed menu frame behind Player 1, so parking at the local
        // cursor would discard the very confirm press that started the race.
        // Drain every authenticated commit below the host boundary first.
        const std::uint32_t local_suspend_frame =
            handoff.resume_input_epoch != 0U
                ? handoff.boundary_frame
                : (local_load_started ? handoff.boundary_frame
                                      : next_commit_frame_);
        gameplay_handoff_ = GameplayHandoffState{
            handoff.current_input_epoch,
            handoff.resume_input_epoch,
            handoff.boundary_frame,
            local_suspend_frame,
            handoff.map,
            local_load_started,
            true,
            false,
            std::chrono::steady_clock::now() + std::chrono::seconds(90)};
    } else {
        gameplay_handoff_->suspended = true;
        if (handoff.resume_input_epoch != 0U) {
            if (gameplay_handoff_->resume_input_epoch != 0U &&
                gameplay_handoff_->resume_input_epoch !=
                    handoff.resume_input_epoch) {
                error = "Player 1 assigned conflicting gameplay input epochs.";
                return false;
            }
            gameplay_handoff_->resume_input_epoch =
                handoff.resume_input_epoch;
            gameplay_handoff_->boundary_frame = handoff.boundary_frame;
            if (!gameplay_handoff_->local_load_started) {
                gameplay_handoff_->local_suspend_frame =
                    handoff.boundary_frame;
            }
        }
        if (local_load_started) {
            gameplay_handoff_->local_load_started = true;
            // A locally initiated request may park at its own load boundary
            // until Player 1 answers.  Once an authoritative boundary is
            // installed, a delayed local lifecycle callback must not move it
            // backward and erase a still-pending host menu commit.
            if (gameplay_handoff_->resume_input_epoch == 0U) {
                gameplay_handoff_->local_suspend_frame = (std::min)(
                    gameplay_handoff_->local_suspend_frame,
                    handoff.boundary_frame);
            }
        }
    }
    if (gameplay_handoff_->resume_input_epoch != 0U) {
        pending_authority_epoch_ = authority_epoch_for_input_epoch(
            gameplay_handoff_->resume_input_epoch);
    }
    error.clear();
    state_changed_.notify_all();
    return true;
}

void DirectSession::send_gameplay_handoff_locked(
    protocol::GameplayHandoffStage stage) {
    if (!gameplay_handoff_) return;
    const protocol::GameplayHandoffPayload payload{
        gameplay_handoff_->current_input_epoch,
        stage == protocol::GameplayHandoffStage::Suspend
            ? gameplay_handoff_->resume_input_epoch : 0U,
        gameplay_handoff_->boundary_frame,
        gameplay_handoff_->map,
        local_slot_,
        stage};
    const auto bytes = protocol::encode_gameplay_handoff(payload);
    if (bytes.empty()) return;
    if (is_host_) {
        if (stage != protocol::GameplayHandoffStage::Suspend) return;
        for (int copy = 0; copy < 3; ++copy) {
            broadcast(protocol::MessageType::GameplayHandoff, bytes,
                      gameplay_handoff_->boundary_frame);
        }
    } else {
        if (stage != protocol::GameplayHandoffStage::Request) return;
        send_to(host_address_, protocol::MessageType::GameplayHandoff, bytes,
                gameplay_handoff_->boundary_frame);
    }
    last_gameplay_handoff_send_ = std::chrono::steady_clock::now();
    worker_wake_ = true;
}

bool DirectSession::begin_gameplay_handoff(std::uint32_t boundary_frame,
                                           std::uint32_t map,
                                           std::string& error) {
    std::scoped_lock lock(mutex_);
    protocol::GameplayHandoffPayload handoff{
        input_epoch_,
        is_host_ ? input_epoch_ + 1U : 0U,
        boundary_frame,
        map,
        local_slot_,
        is_host_ ? protocol::GameplayHandoffStage::Suspend
                 : protocol::GameplayHandoffStage::Request};
    if (handoff.resume_input_epoch == 0U && is_host_) {
        handoff.resume_input_epoch = 1U;
    }
    if (!adopt_gameplay_handoff_locked(handoff, true, error)) return false;
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, boundary_frame, map,
        static_cast<std::uint32_t>(handoff.stage),
        is_host_ ? "host began gameplay handoff"
                 : "guest requested gameplay handoff");
    send_gameplay_handoff_locked(handoff.stage);
    status_ = "Loading the selected track at a synchronized input boundary.";
    return true;
}

bool DirectSession::gameplay_handoff_suspended(
    std::uint32_t frame) const {
    std::scoped_lock lock(mutex_);
    return state_ == ConnectionState::Running && gameplay_handoff_ &&
           gameplay_handoff_->suspended &&
           frame >= gameplay_handoff_->local_suspend_frame;
}

void DirectSession::reset_pending_input_locked() {
    // Every racer has installed the same baseline before this is called, so
    // no pre-handoff retransmit is useful anymore. Purging the uncommitted
    // target frame prevents a delayed menu input from becoming frame one of
    // the race after the fresh epoch opens.
    timeline_.reset(next_commit_frame_);
    clear_input_history_locked();
    frame_commits_.clear();
    commit_history_.clear();
    pending_input_correction_.reset();
    authoritative_input_revision_ = 0U;
    authoritative_predicted_mask_ = 0U;
    authoritative_inputs_ = {};
    authoritative_inputs_valid_ = false;
    pending_local_submission_frame_.reset();
    last_input_history_send_ = {};
    reset_pending_commit_wait_locked();
    prediction_failsafe_active_ = false;
    // send_with_key() encrypts immediately, so an old-epoch input/commit
    // cannot be rewritten after it reaches this queue. Discard precisely the
    // simulation lane at the epoch boundary; lifecycle and recovery packets
    // remain reliable.
    high_priority_outbound_.clear();
    repair_outbound_.clear();
    commit_outbound_.clear();
    rollback_inbound_.clear();
    frame_correction_generation_ = 0U;
    applied_frame_correction_generation_ = 0U;
    frame_correction_assembly_.reset();
    frame_correction_transmission_.reset();
    deferred_frame_commits_.clear();
}

bool DirectSession::record_simulation_progress_locked(
    std::uint8_t player_slot, std::uint32_t progress_scene_epoch,
    std::uint32_t progress_input_epoch, std::uint32_t completed_frame) {
    const bool gameplay_progress = progress_scene_epoch != 0U;
    const bool gameplay_timeline =
        authoritative_phase_active_ &&
        authority_lifecycle_ == AuthorityLifecycle::Racing &&
        scene_epoch_ != 0U && progress_scene_epoch == scene_epoch_;
    const bool frontend_timeline =
        !authoritative_phase_active_ &&
        (authority_lifecycle_ == AuthorityLifecycle::Inactive ||
         authority_lifecycle_ == AuthorityLifecycle::PostRace) &&
        (!gameplay_handoff_ || !gameplay_handoff_->suspended);
    if (!is_host_ || player_slot >= kMaximumPlayers || !launch_descriptor_ ||
        progress_input_epoch != authority_epoch() ||
        completed_frame >= next_commit_frame_ ||
        (gameplay_progress ? !gameplay_timeline : !frontend_timeline)) {
        return false;
    }
    if (simulation_progress_present_[player_slot] &&
        completed_frame < simulation_completed_frame_[player_slot]) {
        return false;
    }
    if (!simulation_progress_present_[player_slot] ||
        completed_frame > simulation_completed_frame_[player_slot]) {
        simulation_progress_time_[player_slot] =
            std::chrono::steady_clock::now();
    }
    simulation_completed_frame_[player_slot] = completed_frame;
    simulation_progress_present_[player_slot] = true;
    ++simulation_wake_generation_;
    state_changed_.notify_all();
    return true;
}

void DirectSession::reset_input_delivery_tracking_locked(
    std::uint32_t first_frame) {
    native_completed_frame_.reset();
    const std::uint32_t first_authored_input =
        first_frame >
                std::numeric_limits<std::uint32_t>::max() - input_delay_
            ? std::numeric_limits<std::uint32_t>::max()
            : first_frame + input_delay_;
    host_input_accepted_frame_ =
        first_authored_input == 0U ? 0U : first_authored_input - 1U;
    host_input_first_missing_frame_ = first_authored_input;
    peer_input_first_missing_frame_.fill(first_authored_input);
    host_backpressure_active_ = false;
}

bool DirectSession::arm_gameplay_handoff_locked(
    std::uint32_t& resume_frame, std::string& error) {
    if (state_ != ConnectionState::Running || !gameplay_handoff_ ||
        !gameplay_handoff_->local_load_started ||
        gameplay_handoff_->resume_input_epoch == 0U ||
        !gameplay_barrier_ || !gameplay_barrier_->arm_announced) {
        error = "The gameplay input handoff is not ready to arm.";
        return false;
    }
    if (gameplay_handoff_->armed) {
        resume_frame = gameplay_handoff_->boundary_frame;
        error.clear();
        return true;
    }
    retired_authority_epoch_ = authority_epoch();
    input_epoch_ = gameplay_handoff_->resume_input_epoch;
    pending_authority_epoch_ = 0U;
    next_commit_frame_ = gameplay_handoff_->boundary_frame;
    last_consumed_input_frame_.reset();
    last_consumed_inputs_ = {};
    resume_frame = next_commit_frame_;
    reset_pending_input_locked();
    reset_input_delivery_tracking_locked(resume_frame);
    simulation_completed_frame_.fill(
        resume_frame == 0U ? 0U : resume_frame - 1U);
    simulation_progress_present_.fill(false);
    simulation_progress_time_.fill({});
    last_simulation_progress_datagram_ = {};
    const Room& progress_room = is_host_ ? lobby_.room() : room_view_;
    const auto progress_now = std::chrono::steady_clock::now();
    for (std::size_t slot = 0U; slot < progress_room.players.size(); ++slot) {
        simulation_progress_present_[slot] =
            progress_room.players[slot].occupied;
        if (simulation_progress_present_[slot]) {
            simulation_progress_time_[slot] = progress_now;
        }
    }
    // The host may have consumed a few pre-buffered frontend frames after a
    // client entered loading, while that client was already parked. Start a
    // new authenticated commit chain from shared handoff data instead of
    // inheriting either machine's now-legitimately-different frontend tail.
    std::uint64_t epoch_chain = initial_commit_hash();
    epoch_chain ^= static_cast<std::uint64_t>(input_epoch_) << 32U;
    epoch_chain ^= static_cast<std::uint64_t>(resume_frame);
    epoch_chain ^= 0x47414D45504C4159ULL; // "GAMEPLAY"
    epoch_chain ^= epoch_chain >> 30U;
    epoch_chain *= 0xBF58476D1CE4E5B9ULL;
    epoch_chain ^= epoch_chain >> 27U;
    epoch_chain *= 0x94D049BB133111EBULL;
    epoch_chain ^= epoch_chain >> 31U;
    last_consumed_commit_hash_ = epoch_chain != 0U ? epoch_chain : 1U;
    // A fresh epoch has no historical delay window. Seed exactly the
    // negotiated input-delay frames with neutral pads, just as cold boot does
    // at frame zero, so the first race frame cannot wait for inputs that were
    // intentionally purged with the frontend epoch.
    const std::uint8_t players = occupied_players();
    for (std::uint32_t frame = resume_frame;
         frame < resume_frame + input_delay_; ++frame) {
        for (std::uint8_t slot = 0U; slot < players; ++slot) {
            if (slot == local_slot_) timeline_.set_local(slot, frame, {});
            else timeline_.set_remote(slot, frame, {});
        }
    }
    gameplay_handoff_->armed = true;
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, resume_frame,
        gameplay_handoff_->map, input_epoch_,
        "gameplay input epoch armed");
    gameplay_barrier_->armed[local_slot_] = true;
    if (!is_host_) {
        // Arm is deliberately distinct from Go. The client installs the fresh
        // input epoch and acknowledges it while its authored tick remains
        // parked. Only Player 1's later Go packet can release simulation.
        const auto payload = protocol::encode_gameplay_barrier({
            gameplay_barrier_->scene_epoch,
            gameplay_barrier_->map,
            gameplay_barrier_->racer_count,
            local_slot_,
            protocol::GameplayBarrierStage::Armed});
        for (int copy = 0; copy < 5; ++copy) {
            send_to(host_address_, protocol::MessageType::GameplayBarrier,
                    payload);
        }
        const auto now = std::chrono::steady_clock::now();
        last_gameplay_resume_ack_ = now;
        gameplay_resume_ack_until_ = now + std::chrono::seconds(2);
        worker_wake_ = true;
    }
    status_ = "The synchronized race input epoch is armed; waiting for Player 1's start signal.";
    error.clear();
    state_changed_.notify_all();
    return true;
}

bool DirectSession::complete_gameplay_handoff(std::uint32_t& resume_frame,
                                              std::string& error) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running || !gameplay_handoff_ ||
        !gameplay_handoff_->armed || !gameplay_barrier_ ||
        !gameplay_barrier_->go_released) {
        error = "Player 1 has not released the armed gameplay input epoch.";
        return false;
    }
    resume_frame = gameplay_handoff_->boundary_frame;
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, resume_frame,
        gameplay_handoff_->map, input_epoch_,
        "gameplay input epoch released");
    gameplay_handoff_.reset();
    last_gameplay_handoff_send_ = {};
    status_ = "The synchronized race input epoch is running.";
    error.clear();
    state_changed_.notify_all();
    return true;
}

bool DirectSession::send_rollback_packet(
    std::uint8_t target_slot, std::span<const std::uint8_t> bytes) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running || !launch_descriptor_ ||
        !authoritative_phase_active_ || scene_epoch_ == 0U ||
        !gameplay_barrier_ || !gameplay_barrier_->go_released ||
        launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
        target_slot >= kMaximumPlayers || target_slot == local_slot_ ||
        !launch_descriptor_->occupied(target_slot)) {
        return false;
    }
    const auto payload = protocol::encode_rollback_payload(
        {scene_epoch_, rollback_transport_epoch_, local_slot_, target_slot,
         std::vector<std::uint8_t>(bytes.begin(), bytes.end())});
    if (payload.empty()) return false;
    if (!is_host_) {
        return send_to(host_address_, protocol::MessageType::RollbackData,
                       payload);
    }
    const PeerRecord& peer = peers_[target_slot];
    return peer.active &&
           send_with_key(peer.address, peer.key,
                         protocol::MessageType::RollbackData, payload);
}

bool DirectSession::enable_owned_backend(bool available) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Offline) return false;
    owned_backend_available_ = available;
    owned_inbound_.clear(); owned_inbound_bytes_ = 0;
    return true;
}

bool DirectSession::owned_backend_available() const {
    std::scoped_lock lock(mutex_);
    return owned_backend_available_;
}

bool DirectSession::owned_game_active() const {
    std::scoped_lock lock(mutex_);
    return launch_descriptor_ && launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback &&
        (state_ == ConnectionState::Loading || state_ == ConnectionState::Running);
}

PeerAddress DirectSession::owned_peer_address(std::uint8_t slot) {
    PeerAddress address{};
    address.size = 4;
    address.storage[0] = 'D'; address.storage[1] = 'K';
    address.storage[2] = 'X'; address.storage[3] = slot;
    return address;
}

DatagramSendStatus DirectSession::send_owned_packet(std::uint8_t target,
    std::span<const std::uint8_t> bytes, TransportTrafficClass traffic,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!owned_backend_available_ || !launch_descriptor_ ||
        launch_descriptor_->synchronization != SynchronizationMode::ExperimentalRollback ||
        (state_ != ConnectionState::Loading && state_ != ConnectionState::Running) ||
        target >= kMaximumPlayers || target == local_slot_ || !launch_descriptor_->occupied(target) ||
        (!is_host_ && target != 0) || bytes.empty() || bytes.size() > 12352) {
        error = "The authenticated experimental route is unavailable.";
        return DatagramSendStatus::Error;
    }
    const auto& address = is_host_ ? peers_[target].address : host_address_;
    if ((is_host_ && !peers_[target].active) || !transport_ ||
        !transport_->traffic_ready(address, traffic)) return DatagramSendStatus::WouldBlock;
    // Retain backpressure in the owned sender rather than overflowing a stable
    // outbound queue. These are complete packets, never fragments to discard.
    const auto type = traffic == TransportTrafficClass::Realtime
        ? protocol::MessageType::ExperimentalInput
        : traffic == TransportTrafficClass::Authoritative
            ? protocol::MessageType::ExperimentalRepair
            : traffic == TransportTrafficClass::Checkpoint ? protocol::MessageType::ExperimentalCheckpoint
            : protocol::MessageType::ExperimentalControl;
    const auto& queue=traffic==TransportTrafficClass::Checkpoint?bulk_outbound_:
        traffic==TransportTrafficClass::Realtime?high_priority_outbound_:
        traffic==TransportTrafficClass::Authoritative?repair_outbound_:critical_outbound_;
    if(std::count_if(queue.begin(),queue.end(),[&](const auto& p){return p.destination==address&&p.type==type;})>=16)
        return DatagramSendStatus::WouldBlock;
    // SCTP can still deliver old, already-transmitted packets after a match
    // retires. Bind every owned lane to this launch, before admission sees it.
    std::vector<std::uint8_t> scoped_bytes;
    scoped_bytes.reserve(8U+bytes.size());
    const auto launch_hash=launch_descriptor_hash(*launch_descriptor_);
    for(int shift=56;shift>=0;shift-=8)scoped_bytes.push_back(static_cast<std::uint8_t>(launch_hash>>shift));
    scoped_bytes.insert(scoped_bytes.end(),bytes.begin(),bytes.end());
    if (!send_with_key(address, is_host_ ? peers_[target].key : key_, type, scoped_bytes)) {
        error = "Could not queue the authenticated experimental packet.";
        return DatagramSendStatus::Error;
    }
    worker_wake_ = true; state_changed_.notify_all(); error.clear();
    return DatagramSendStatus::Sent;
}

void DirectSession::begin_owned_match_end_locked(std::string reason) {
    if (!launch_descriptor_ || launch_descriptor_->synchronization != SynchronizationMode::ExperimentalRollback ||
        (state_ != ConnectionState::Loading && state_ != ConnectionState::Running)) return;
    for (char& c : reason) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    if (reason.empty()) reason = "A racer stopped the game. The match has ended.";
    reason.resize((std::min)(reason.size(), std::size_t(240)));
    const auto hash = launch_descriptor_hash(*launch_descriptor_);
    retired_owned_generation_ = (std::max)(retired_owned_generation_, launch_descriptor_->lobby_generation);
    owned_match_end_ = OwnedMatchEnd{hash, reason, false, false, false, std::chrono::steady_clock::now(), {}};
    for (auto& peer : peers_) { peer.owned_runtime_returned = false; peer.owned_lobby_resumed = false; }
    reset_launch_transaction_locked(); committed_launch_epoch_ = 0;
    launch_requested_ = false; launch_descriptor_.reset();
    local_loaded_ = false; run_signal_sent_ = false;
    local_bootstrap_hash_ = 0; bootstrap_hashes_ = {}; bootstrap_hash_present_ = {};
    runtime_save_present_ = {}; local_runtime_save_generation_ = 0; local_runtime_save_hash_ = 0;
    desired_ready_.reset(); pending_ready_request_id_ = 0;
    owned_inbound_.clear(); owned_inbound_bytes_ = 0;
    // Only retire the experimental packet lanes. Keep lobby authentication,
    // receive sequence windows, the invitation and the independent transport.
    for (auto* queue : {&critical_outbound_, &repair_outbound_, &high_priority_outbound_,
                       &commit_outbound_,&authority_outbound_,&normal_priority_outbound_,&bulk_outbound_}) {
        std::erase_if(*queue, [](const OutboundPacket& p) {
            return p.type == protocol::MessageType::ExperimentalControl ||
                p.type == protocol::MessageType::ExperimentalInput ||
                p.type == protocol::MessageType::ExperimentalRepair ||
                p.type == protocol::MessageType::ExperimentalCheckpoint || p.type == protocol::MessageType::ExperimentalLoaded ||
                p.type == protocol::MessageType::ExperimentalSaveOffer || p.type == protocol::MessageType::ExperimentalSaveRequest ||
                p.type == protocol::MessageType::Start || p.type == protocol::MessageType::Loaded ||
                p.type == protocol::MessageType::LaunchPrepare || p.type == protocol::MessageType::LaunchCommit ||
                p.type == protocol::MessageType::LaunchRelease || p.type == protocol::MessageType::LaunchPrepareAck ||
                p.type == protocol::MessageType::LaunchCommitAck || p.type == protocol::MessageType::LaunchReleaseAck ||
                p.type == protocol::MessageType::LaunchCancel;
        });
    }
    if (is_host_) { lobby_.return_to_waiting(); room_view_ = lobby_.room(); }
    else for (auto& player : room_view_.players) { player.ready = false; player.loaded = false; }
    state_ = is_host_ ? ConnectionState::Hosting : ConnectionState::Lobby;
    status_ = reason + " Returning to the lobby...";
    std::fprintf(stderr, "[rollback][match-end] begin host=%d launch=%llu reason=%s\n", is_host_,
        static_cast<unsigned long long>(hash), reason.c_str());
    worker_wake_ = true; state_changed_.notify_all();
}

void DirectSession::request_owned_match_end(std::string reason) {
    std::scoped_lock lock(mutex_);
    begin_owned_match_end_locked(std::move(reason));
    service_owned_match_end_locked(std::chrono::steady_clock::now());
}

bool DirectSession::complete_owned_match_end(std::uint64_t hash,
    std::span<const std::uint8_t> confirmed_save, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!owned_match_end_ || owned_match_end_->hash != hash) {
        error = "The experimental match-end identity no longer matches."; return false;
    }
    if (state_ == ConnectionState::Offline || state_ == ConnectionState::Failed) {
        error = "The lobby closed while the experimental match was ending."; return false;
    }
    if (owned_match_end_->runtime_returned) { error.clear(); return true; }
    if (is_host_) {
        // The owned worker has already committed this checksum-valid confirmed
        // EEPROM. Do not touch a single-player save or speculative game state.
        if (confirmed_save.size() != 512) { error = "The confirmed online save is incomplete."; return false; }
        session_save_.assign(confirmed_save.begin(), confirmed_save.end());
        manifest_.session_save_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(session_save_.data()), session_save_.size()));
        ++session_save_generation_; if (!session_save_generation_) ++session_save_generation_;
        lobby_.refresh_online_save_hash(manifest_.session_save_hash); room_view_ = lobby_.room();
        local_online_save_ready_ = true; local_online_save_acknowledged_ = true;
        online_save_status_.reset();
        for (auto& peer : peers_) if (peer.active) peer.online_save_ready = false;
    }
    owned_match_end_->runtime_returned = true;
    owned_match_end_->last_send = {};
    service_owned_match_end_locked(std::chrono::steady_clock::now());
    error.clear(); return true;
}

void DirectSession::remove_owned_peer_locked(std::uint64_t sender, std::string reason) {
    auto* peer = peer_by_sender(sender);
    if (!is_host_ || !peer) return;
    begin_owned_match_end_locked(std::move(reason));
    const auto address = peer->address;
    // Keep survivor slots stable until the next immutable launch. Compact N64
    // ownership is rebuilt there; moving a live lobby slot invalidates Ready
    // and save acknowledgements already in transit from the other racers.
    lobby_.leave(std::to_string(sender), false);
    transport_->release_peer_route(address); *peer = {};
    for (auto* queue : {&critical_outbound_, &repair_outbound_, &high_priority_outbound_,
                       &commit_outbound_, &authority_outbound_, &normal_priority_outbound_, &bulk_outbound_})
        std::erase_if(*queue, [&](const OutboundPacket& p) { return p.destination == address; });
    synchronize_peer_slots(); room_view_ = lobby_.room();
    broadcast_lobby(); worker_wake_ = true; state_changed_.notify_all();
}

void DirectSession::service_owned_match_end_locked(std::chrono::steady_clock::time_point now) {
    // Only negotiated experimental games use this liveness policy. Ordinary
    // packet loss and temporary ICE recovery still have 30 seconds to recover.
    if (launch_descriptor_ && launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback &&
        (state_ == ConnectionState::Loading || state_ == ConnectionState::Running)) {
        if (is_host_) {
            for (const auto& peer : peers_) if (peer.active && peer.owned_last_packet.time_since_epoch().count() &&
                now - peer.owned_last_packet >= std::chrono::seconds(30)) {
                const auto name = lobby_.room().players[peer.slot].display_name;
                remove_owned_peer_locked(peer.sender_id, name + " stopped responding. The match has ended.");
                break;
            }
        } else if (owned_last_host_packet_.time_since_epoch().count() &&
                   now - owned_last_host_packet_ >= std::chrono::seconds(30)) {
            fail_locked("The host stopped responding. The lobby has closed; return to Online to reconnect."); return;
        }
    }
    if (!owned_match_end_ || owned_match_end_->resumed || state_ == ConnectionState::Failed || state_ == ConnectionState::Offline) return;
    auto& end = *owned_match_end_;
    if (is_host_) {
        // Never let a killed process strand the remaining launchers waiting for
        // its runtime-return acknowledgement. Removal is by authenticated ID,
        // not by a slot that may move when the lobby compacts its roster.
        if (now - end.started >= std::chrono::seconds(30)) {
            for (const auto& peer : peers_) if (peer.active &&
                (!peer.owned_runtime_returned || (end.resume_prepared && !peer.owned_lobby_resumed))) {
                if (now - end.started >= std::chrono::seconds(60) ||
                    !peer.owned_last_packet.time_since_epoch().count() ||
                    now - peer.owned_last_packet >= std::chrono::seconds(30)) {
                    remove_owned_peer_locked(peer.sender_id, {}); break;
                }
            }
        }
        const bool returned = end.runtime_returned && std::all_of(peers_.begin(), peers_.end(),
            [](const PeerRecord& p) { return !p.active || p.owned_runtime_returned; });
        if (returned && !end.resume_prepared) {
            end.resume_prepared = true; end.last_send = {};
            status_ = "Match ended. Verifying the latest online save before the next start...";
            broadcast_lobby();
        }
        if (end.resume_prepared && std::all_of(peers_.begin(), peers_.end(),
            [](const PeerRecord& p) { return !p.active || (p.owned_lobby_resumed && p.online_save_ready); })) {
            end.resumed = true; status_ = "Match ended. The remaining racers can Ready and start again.";
            broadcast_lobby(); state_changed_.notify_all(); return;
        }
    } else if (now - end.started >= std::chrono::seconds(30) &&
               now - owned_last_host_packet_ >= std::chrono::seconds(30)) {
        fail_locked("The host left while returning to the lobby. Create or join another lobby."); return;
    }
    if (end.last_send.time_since_epoch().count() && now - end.last_send < std::chrono::milliseconds(250)) return;
    end.last_send = now;
    if (is_host_) {
        for (const auto& peer : peers_) if (peer.active && !peer.owned_lobby_resumed) {
            protocol::MatchEndPayload payload{end.hash,
                end.resume_prepared ? protocol::MatchEndStage::Resume : protocol::MatchEndStage::Stop,
                peer.slot, end.message};
            if (end.resume_prepared) {
                payload.save_generation = session_save_generation_; payload.save_hash = manifest_.session_save_hash;
                payload.save = session_save_;
            }
            send_to(peer.address, protocol::MessageType::ExperimentalMatchEnd, protocol::encode_match_end(payload));
        }
    } else {
        send_to(host_address_, protocol::MessageType::ExperimentalMatchEnd,
            protocol::encode_match_end({end.hash, end.runtime_returned ? protocol::MatchEndStage::Returned :
                protocol::MatchEndStage::Request, local_slot_, {}}));
    }
}

bool DirectSession::handle_owned_match_end_locked(std::uint64_t sender, const protocol::Datagram& packet) {
    if (packet.header.type != protocol::MessageType::ExperimentalMatchEnd) return false;
    protocol::MatchEndPayload payload;
    if (!owned_backend_available_ || !protocol::decode_match_end(packet.payload, payload)) return true;
    const bool current = launch_descriptor_ && launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback &&
        launch_descriptor_hash(*launch_descriptor_) == payload.launch_hash;
    const bool ending = owned_match_end_ && owned_match_end_->hash == payload.launch_hash;
    if (launch_descriptor_ && !current) return true;
    if (!current && !ending) return true; // delayed previous-match traffic cannot stop a new match
    if (is_host_) {
        auto* peer = peer_by_sender(sender); if (!peer) return true;
        if (payload.stage == protocol::MatchEndStage::Request || payload.stage == protocol::MatchEndStage::Returned) {
            if (current) begin_owned_match_end_locked(lobby_.room().players[peer->slot].display_name +
                " stopped the game. The match has ended.");
            if (payload.stage == protocol::MatchEndStage::Returned) peer->owned_runtime_returned = true;
        } else if (payload.stage == protocol::MatchEndStage::ResumeAck && ending && owned_match_end_->resume_prepared) {
            peer->owned_lobby_resumed = true;
        }
    } else if (payload.stage == protocol::MatchEndStage::Stop) {
        if (current) begin_owned_match_end_locked(payload.message);
    } else if (payload.stage == protocol::MatchEndStage::Resume && ending && owned_match_end_->runtime_returned) {
        if (!owned_match_end_->resumed) {
            const auto hash = stable_hash(std::string_view(reinterpret_cast<const char*>(payload.save.data()), payload.save.size()));
            if(hash!=payload.save_hash||!queue_save_locked(SaveJobKind::Resume,payload.save,payload.save_generation,hash,payload.player_slot,payload.launch_hash)) {
                fail_locked("The updated isolated online save could not be queued safely.");return true;
            }
            local_online_save_ready_=false;local_online_save_acknowledged_=false;
            status_="Match ended. Installing and verifying the latest online save...";return true;
        } else if (payload.save_hash != manifest_.session_save_hash || payload.save_generation != session_save_generation_) return true;
        send_to(host_address_, protocol::MessageType::ExperimentalMatchEnd,
            protocol::encode_match_end({payload.launch_hash, protocol::MatchEndStage::ResumeAck, local_slot_, {}}));
        send_to(host_address_, protocol::MessageType::OnlineSaveReady,
            protocol::encode_online_save_ready({local_slot_, session_save_generation_, manifest_.session_save_hash}));
        last_client_request_ = std::chrono::steady_clock::now();
    }
    worker_wake_ = true; state_changed_.notify_all(); return true;
}

bool DirectSession::receive_owned_packet_locked(std::uint8_t source,
    const protocol::Datagram& packet) {
    const auto type = packet.header.type;
    if (type != protocol::MessageType::ExperimentalControl &&
        type != protocol::MessageType::ExperimentalInput &&
        type != protocol::MessageType::ExperimentalRepair &&
        type != protocol::MessageType::ExperimentalCheckpoint) return false;
    if (!owned_backend_available_ || !launch_descriptor_ ||
        launch_descriptor_->synchronization != SynchronizationMode::ExperimentalRollback ||
        (state_ != ConnectionState::Loading && state_ != ConnectionState::Running) ||
        source >= kMaximumPlayers || !launch_descriptor_->occupied(source) || source == local_slot_ ||
        packet.payload.size() <= 8U || packet.payload.size() > 12360U) return true;
    std::uint64_t launch_hash=0;
    for(std::size_t index=0;index<8U;++index)launch_hash=(launch_hash<<8U)|packet.payload[index];
    if(launch_hash!=launch_descriptor_hash(*launch_descriptor_))return true;
    const auto payload_size=packet.payload.size()-8U;
    if (owned_inbound_.size() >= 256 || owned_inbound_bytes_ + payload_size > 2U*1024U*1024U) {
        fail_locked("The experimental packet consumer stopped servicing its bounded receive queue.");
        return true;
    }
    const auto traffic = type == protocol::MessageType::ExperimentalInput ? TransportTrafficClass::Realtime
        : type == protocol::MessageType::ExperimentalRepair ? TransportTrafficClass::Authoritative
        : type == protocol::MessageType::ExperimentalCheckpoint ? TransportTrafficClass::Checkpoint
        : TransportTrafficClass::Control;
    owned_inbound_.push_back({source, traffic,
        std::vector<std::uint8_t>(packet.payload.begin()+8,packet.payload.end())});
    owned_inbound_bytes_ += payload_size;
    return true;
}

bool DirectSession::take_owned_packet(OwnedPacket& packet) {
    std::scoped_lock lock(mutex_);
    if (owned_inbound_.empty()) return false;
    packet = std::move(owned_inbound_.front()); owned_inbound_.pop_front();
    owned_inbound_bytes_ -= packet.bytes.size();
    return true;
}

bool DirectSession::owned_network_configuration(const secure::Key& incarnation,
    experimental::NetworkConfiguration& configuration, std::string& error) const {
    std::scoped_lock lock(mutex_);
    configuration = {};
    if (!owned_backend_available_ || !launch_descriptor_ ||
        launch_descriptor_->synchronization != SynchronizationMode::ExperimentalRollback ||
        (state_ != ConnectionState::Loading && state_ != ConnectionState::Running)) {
        error = "No negotiated experimental launch is active."; return false;
    }
    configuration.match_id = match_id_; configuration.local_sender_id = sender_id_;
    configuration.incarnation = incarnation;
    if (!is_host_) configuration.peers[0] = {owned_peer_address(0),host_sender_id_,key_};
    else for (std::uint8_t slot=1; slot<kMaximumPlayers; ++slot)
        if (launch_descriptor_->occupied(slot) && peers_[slot].active)
            configuration.peers[slot] = {owned_peer_address(slot),peers_[slot].sender_id,peers_[slot].key};
    error.clear(); return true;
}

std::vector<RollbackPacket> DirectSession::take_rollback_packets() {
    std::vector<RollbackPacket> result;
    take_rollback_packets(result);
    return result;
}

void DirectSession::take_rollback_packets(
    std::vector<RollbackPacket>& result) {
    std::scoped_lock lock(mutex_);
    result.clear();
    if (result.capacity() < rollback_inbound_.size()) {
        result.reserve(rollback_inbound_.size());
    }
    while (!rollback_inbound_.empty()) {
        result.push_back(std::move(rollback_inbound_.front()));
        rollback_inbound_.pop_front();
    }
}

bool DirectSession::publish_authoritative_state(
    std::uint32_t frame, std::span<const std::uint8_t> state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!is_host_ || state_ != ConnectionState::Running ||
        !authoritative_phase_active_) {
        error = "Only Player 1 can publish authoritative gameplay state.";
        return false;
    }
    if (state.empty() || state.size() > kMaximumAuthoritativeStateBytes) {
        error = "The authoritative gameplay state is outside protocol limits.";
        return false;
    }
    std::vector<std::uint8_t> owned(state.begin(), state.end());
    const auto existing = authoritative_states_.find(frame);
    if (existing != authoritative_states_.end() && existing->second != owned) {
        error = "Player 1 attempted to replace an already published state frame.";
        return false;
    }
    authoritative_states_[frame] = std::move(owned);
    auto& acknowledgements = authoritative_acknowledgements_[frame];
    acknowledgements[local_slot_] = true;
    bool already_complete = true;
    const Room& room = lobby_.room();
    for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
        if (room.players[slot].occupied && !acknowledgements[slot]) {
            already_complete = false;
            break;
        }
    }
    if (already_complete) {
        fully_acknowledged_states_.insert(frame);
    }
    if (!send_authoritative_state(nullptr, frame, authoritative_states_[frame])) {
        error = "The authoritative gameplay state could not be queued reliably.";
        return false;
    }
    ++authority_checkpoints_sent_;
    if (frame > 64U) {
        const std::uint32_t oldest = frame - 64U;
        std::erase_if(authoritative_states_, [oldest](const auto& entry) {
            return entry.first < oldest;
        });
        std::erase_if(authoritative_acknowledgements_,
                      [oldest](const auto& entry) {
            return entry.first < oldest;
        });
    }
    error.clear();
    return true;
}

SessionPollResult DirectSession::poll_authoritative_state(
    std::uint32_t frame, std::vector<std::uint8_t>& state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!authoritative_phase_active_ || state_ != ConnectionState::Running) {
        error = status_.empty()
            ? "The authoritative gameplay phase is not running."
            : status_;
        return SessionPollResult::Failed;
    }
    const auto found = authoritative_states_.find(frame);
    if (found != authoritative_states_.end()) {
        state = found->second;
        if (frame > 64U) {
            const std::uint32_t oldest = frame - 64U;
            std::erase_if(authoritative_states_, [oldest](const auto& entry) {
                return entry.first < oldest;
            });
            std::erase_if(authoritative_acknowledgements_,
                          [oldest](const auto& entry) {
                return entry.first < oldest;
            });
        }
        error.clear();
        return SessionPollResult::Ready;
    }
    if (is_host_) {
        error = "Player 1 has not published that authoritative state yet.";
        return SessionPollResult::Pending;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::uint16_t measured_rtt = local_slot_ < room_view_.players.size()
        ? room_view_.players[local_slot_].ping_ms : 0U;
    const std::uint16_t measured_jitter =
        local_slot_ < room_view_.players.size()
            ? room_view_.players[local_slot_].jitter_ms : 0U;
    const auto retry_interval = std::chrono::milliseconds(
        checkpoint_retry_milliseconds(measured_rtt, measured_jitter));
    if (last_authoritative_request_frame_ != frame) {
        last_authoritative_request_frame_ = frame;
        last_authoritative_request_ = now;
    }
    // Give Player 1's proactive transfer one route-adjusted interval before
    // requesting only missing fragments. The game thread returns immediately.
    if (now - last_authoritative_request_ >= retry_interval) {
        std::uint16_t chunk_count = 16U;
        std::uint16_t missing = 0xFFFFU;
        const auto assembly = snapshot_assemblies_.find(frame);
        if (assembly != snapshot_assemblies_.end() &&
            assembly->second.chunk_count > 0U) {
            chunk_count = assembly->second.chunk_count;
            missing = 0U;
            for (std::uint16_t index = 0U; index < chunk_count; ++index) {
                if (!assembly->second.present[index]) {
                    missing |= static_cast<std::uint16_t>(1U << index);
                }
            }
        }
        send_to(host_address_, protocol::MessageType::StateRequest,
                protocol::encode_state_request(
                    {scene_epoch_, frame, chunk_count, missing}), frame);
        last_authoritative_request_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    error.clear();
    return SessionPollResult::Pending;
}

bool DirectSession::publish_live_replica(
    std::uint32_t frame, std::span<const std::uint8_t> state,
    bool reliable_keyframe, std::string& error) {
    std::unique_lock lock(mutex_);
    if (!is_host_ || state_ != ConnectionState::Running ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = "Only Player 1 can publish live race state.";
        return false;
    }
    if (is_live_replica_frame(frame) || state.empty() ||
        state.size() > kMaximumAuthoritativeStateBytes) {
        error = "The live race state is outside protocol limits.";
        return false;
    }

    // Encoding/checksum read owned bytes, never live RDRAM or native state.
    // No background job queue: one bounded encode on the publishing thread.
    std::vector<std::uint8_t> owned(state.begin(), state.end());
    const auto generation = input_epoch_;
    const auto scene = scene_epoch_;
    const auto match = match_id_;
    const auto base_frame = live_replica_keyframe_frame_;
    std::vector<std::uint8_t> base;
    if (!reliable_keyframe && base_frame && *base_frame < frame)
        base = live_replica_keyframe_state_;
    lock.unlock();
    PreparedStateWire prepared;
    {
        performance_trace::Scope profile(performance_trace::Region::OnlineStateEncode, owned.size());
        if (!base.empty()) prepared.bytes = encode_authoritative_state_delta_wire(owned, base, *base_frame);
        if (prepared.bytes.empty()) prepared.bytes = encode_authoritative_state_wire(owned);
        prepared.checksum = authoritative_state_checksum(prepared.bytes);
    }
    lock.lock();
    if (generation != input_epoch_ || scene != scene_epoch_ || match != match_id_ ||
        state_ != ConnectionState::Running || !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        // A transition retired this optional publication while it encoded.
        error.clear();
        return true;
    }
    if (!base.empty() && (base_frame != live_replica_keyframe_frame_ ||
        base != live_replica_keyframe_state_)) {
        // Never publish a delta referencing a replaced baseline.
        error.clear();
        return true;
    }
    live_replica_states_[frame] = std::move(owned);
    if (reliable_keyframe) {
        live_replica_keyframe_frame_ = frame;
        live_replica_keyframe_state_ = live_replica_states_[frame];
    }
    const protocol::MessageType message_type = reliable_keyframe
        ? protocol::MessageType::StateSnapshot
        : protocol::MessageType::LiveReplicaSnapshot;
    if (!send_authoritative_state(
            nullptr, frame, live_replica_states_[frame], 0xFFFFU,
            message_type, true, &prepared)) {
        error = "Player 1 could not queue the live race state.";
        return false;
    }
    if (frame > kLiveReplicaRecoveryHistoryFrames) {
        const std::uint32_t oldest =
            live_replica_oldest_retained_frame(frame);
        std::erase_if(live_replica_states_, [oldest](const auto& entry) {
            return entry.first < oldest;
        });
    }
    error.clear();
    return true;
}

SessionPollResult DirectSession::poll_live_replica(
    std::uint32_t frame, std::vector<std::uint8_t>& state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!authoritative_phase_active_ || state_ != ConnectionState::Running ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = status_.empty() ? "The live race-state stream is not running."
                                : status_;
        return SessionPollResult::Failed;
    }
    const auto found = live_replica_states_.find(frame);
    if (found != live_replica_states_.end()) {
        state = found->second;
        if (frame > kLiveReplicaRecoveryHistoryFrames) {
            const std::uint32_t oldest =
                live_replica_oldest_retained_frame(frame);
            std::erase_if(live_replica_states_, [oldest](const auto& entry) {
                return entry.first < oldest;
            });
            std::erase_if(live_replica_assemblies_,
                          [oldest](const auto& entry) {
                return entry.first < oldest;
            });
        }
        error.clear();
        return SessionPollResult::Ready;
    }
    if (is_host_) {
        error.clear();
        return SessionPollResult::Pending;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::uint16_t measured_rtt = local_slot_ < room_view_.players.size()
        ? room_view_.players[local_slot_].ping_ms : 0U;
    const std::uint16_t measured_jitter =
        local_slot_ < room_view_.players.size()
            ? room_view_.players[local_slot_].jitter_ms : 0U;
    const auto retry_interval = std::chrono::milliseconds(std::clamp(
        20 + static_cast<int>(measured_rtt / 3U) +
            static_cast<int>(measured_jitter),
        20, 150));
    if (last_live_replica_request_frame_ != frame) {
        last_live_replica_request_frame_ = frame;
        last_live_replica_request_ = now;
    }
    if (now - last_live_replica_request_ >= retry_interval) {
        std::uint16_t chunk_count = 16U;
        std::uint16_t missing = 0xFFFFU;
        const auto assembly = live_replica_assemblies_.find(frame);
        if (assembly != live_replica_assemblies_.end() &&
            assembly->second.chunk_count > 0U) {
            chunk_count = assembly->second.chunk_count;
            missing = 0U;
            for (std::uint16_t index = 0U; index < chunk_count; ++index) {
                if (!assembly->second.present[index]) {
                    missing |= static_cast<std::uint16_t>(1U << index);
                }
            }
        }
        const std::uint32_t wire_frame = live_replica_wire_frame(frame);
        send_to(host_address_, protocol::MessageType::StateRequest,
                protocol::encode_state_request(
                    {scene_epoch_, wire_frame, chunk_count, missing}),
                wire_frame);
        ++live_replica_requests_sent_;
        last_live_replica_request_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    error.clear();
    return SessionPollResult::Pending;
}

SessionPollResult DirectSession::poll_latest_live_replica(
    std::uint32_t minimum_frame, std::uint32_t maximum_frame,
    std::uint32_t& state_frame, std::vector<std::uint8_t>& state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!authoritative_phase_active_ || state_ != ConnectionState::Running ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = status_.empty() ? "The live race-state stream is not running."
                                : status_;
        return SessionPollResult::Failed;
    }
    if (minimum_frame > maximum_frame) {
        error = "The live race-state window is invalid.";
        return SessionPollResult::Failed;
    }

    auto newest = live_replica_states_.end();
    for (auto iterator = live_replica_states_.begin();
         iterator != live_replica_states_.end(); ++iterator) {
        if (iterator->first < minimum_frame ||
            iterator->first > maximum_frame) {
            continue;
        }
        if (newest == live_replica_states_.end() ||
            iterator->first > newest->first) {
            newest = iterator;
        }
    }
    if (newest != live_replica_states_.end()) {
        state_frame = newest->first;
        state = std::move(newest->second);
        // A replica is never useful after a newer host frame has been
        // installed. Retiring it here also bounds memory under packet reorder.
        std::erase_if(live_replica_states_, [state_frame](const auto& entry) {
            return entry.first <= state_frame;
        });
        std::erase_if(live_replica_assemblies_, [state_frame](const auto& entry) {
            return entry.first <= state_frame;
        });
        error.clear();
        return SessionPollResult::Ready;
    }
    if (is_host_) {
        error.clear();
        return SessionPollResult::Pending;
    }

    // Repair only the first missing usable frame. Newer complete replicas may
    // supersede it at any time, so this request never stalls the authored tick.
    const auto now = std::chrono::steady_clock::now();
    const std::uint16_t measured_rtt = local_slot_ < room_view_.players.size()
        ? room_view_.players[local_slot_].ping_ms : 0U;
    const std::uint16_t measured_jitter =
        local_slot_ < room_view_.players.size()
            ? room_view_.players[local_slot_].jitter_ms : 0U;
    const auto retry_interval = std::chrono::milliseconds(std::clamp(
        20 + static_cast<int>(measured_rtt / 3U) +
            static_cast<int>(measured_jitter),
        20, 150));
    if (last_live_replica_request_frame_ != minimum_frame) {
        last_live_replica_request_frame_ = minimum_frame;
        last_live_replica_request_ = now;
    }
    if (now - last_live_replica_request_ >= retry_interval) {
        std::uint16_t chunk_count = 16U;
        std::uint16_t missing = 0xFFFFU;
        const auto assembly = live_replica_assemblies_.find(minimum_frame);
        if (assembly != live_replica_assemblies_.end() &&
            assembly->second.chunk_count > 0U) {
            chunk_count = assembly->second.chunk_count;
            missing = 0U;
            for (std::uint16_t index = 0U; index < chunk_count; ++index) {
                if (!assembly->second.present[index]) {
                    missing |= static_cast<std::uint16_t>(1U << index);
                }
            }
        }
        const std::uint32_t wire_frame =
            live_replica_wire_frame(minimum_frame);
        send_to(host_address_, protocol::MessageType::StateRequest,
                protocol::encode_state_request(
                    {scene_epoch_, wire_frame, chunk_count, missing}),
                wire_frame);
        ++live_replica_requests_sent_;
        last_live_replica_request_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    error.clear();
    return SessionPollResult::Pending;
}

bool DirectSession::publish_racer_orientation(
    std::uint32_t frame, std::span<const std::uint8_t> state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!is_host_ || state_ != ConnectionState::Running ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = "Only Player 1 can publish racer orientation.";
        return false;
    }
    if (state.empty() ||
        state.size() > protocol::kMaximumRacerOrientationBytes) {
        error = "The racer orientation sample is outside protocol limits.";
        return false;
    }
    const auto payload = protocol::encode_racer_orientation(
        {scene_epoch_, frame,
         std::vector<std::uint8_t>(state.begin(), state.end())});
    if (payload.empty()) {
        error = "The racer orientation sample could not be encoded.";
        return false;
    }
    racer_orientation_states_[frame] =
        std::vector<std::uint8_t>(state.begin(), state.end());
    broadcast(protocol::MessageType::RacerOrientation, payload, frame);
    if (frame > 64U) {
        const std::uint32_t oldest = frame - 64U;
        std::erase_if(racer_orientation_states_,
                      [oldest](const auto& entry) {
                          return entry.first < oldest;
                      });
    }
    error.clear();
    return true;
}

SessionPollResult DirectSession::poll_latest_racer_orientation(
    std::uint32_t minimum_frame, std::uint32_t maximum_frame,
    std::uint32_t& state_frame, std::vector<std::uint8_t>& state,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!authoritative_phase_active_ || state_ != ConnectionState::Running ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = status_.empty() ? "The racer orientation stream is not running."
                                : status_;
        return SessionPollResult::Failed;
    }
    if (minimum_frame > maximum_frame) {
        error = "The racer orientation history window is invalid.";
        return SessionPollResult::Failed;
    }
    std::erase_if(racer_orientation_states_,
                  [minimum_frame](const auto& entry) {
                      return entry.first < minimum_frame;
                  });
    auto newest = racer_orientation_states_.end();
    for (auto iterator = racer_orientation_states_.begin();
         iterator != racer_orientation_states_.end(); ++iterator) {
        if (iterator->first < minimum_frame ||
            iterator->first > maximum_frame) {
            continue;
        }
        if (newest == racer_orientation_states_.end() ||
            iterator->first > newest->first) {
            newest = iterator;
        }
    }
    if (newest == racer_orientation_states_.end()) {
        error.clear();
        return SessionPollResult::Pending;
    }
    state_frame = newest->first;
    state = std::move(newest->second);
    std::erase_if(racer_orientation_states_,
                  [state_frame](const auto& entry) {
                      return entry.first <= state_frame;
                  });
    error.clear();
    return SessionPollResult::Ready;
}

bool DirectSession::wait_authoritative_state(
    std::uint32_t frame, std::vector<std::uint8_t>& state,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string error;
    while (std::chrono::steady_clock::now() < deadline) {
        const SessionPollResult result =
            poll_authoritative_state(frame, state, error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    std::scoped_lock lock(mutex_);
    ++authority_wait_timeouts_;
    fail_locked("Timed out waiting for Player 1's authoritative state at frame " +
                std::to_string(frame) + ".");
    return false;
}

void DirectSession::begin_authoritative_phase() {
    std::scoped_lock lock(mutex_);
    // A Quick Join replica is explicitly latest-wins and meaningful only in
    // the scene that produced it. Drop any callback-delivered tail from the
    // previous scene before opening the new authority epoch. Reliable control
    // and frame-ledger lanes are intentionally left untouched.
    transport_->discard_received(TransportTrafficClass::Replica);
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    authority_outbound_.clear();
    rollback_inbound_.clear();
    rollback_transport_epoch_ = 1U;
    pending_input_correction_.reset();
    authoritative_input_revision_ = 0U;
    authoritative_predicted_mask_ = 0U;
    authoritative_inputs_ = {};
    authoritative_inputs_valid_ = false;
    input_corrections_ = 0U;
    state_hashes_.clear();
    consecutive_authoritative_frames_ = 0U;
    authoritative_phase_active_ = state_ == ConnectionState::Running;
    authority_lifecycle_ = authoritative_phase_active_
        ? AuthorityLifecycle::Racing : AuthorityLifecycle::Inactive;
    finish_seal_frame_.reset();
    if (authoritative_phase_active_) {
        ++scene_epoch_;
        if (scene_epoch_ == 0U) scene_epoch_ = 1U;
        failure_recorder().record(
            FailureEventKind::LifecycleBoundary, next_commit_frame_,
            scene_epoch_, input_epoch_, "race authority began");
    }
    recovery_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    completed_recovery_frame_.reset();
    completed_recovery_epoch_ = 0U;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    last_recovery_broadcast_ = {};
    recovery_resume_until_ = {};
    transition_barrier_.reset();
    last_transition_broadcast_ = {};
    transition_resume_until_ = {};
    gameplay_barrier_.reset();
    last_gameplay_barrier_broadcast_ = {};
    gameplay_resume_until_ = {};
    last_gameplay_ready_announcement_ = {};
    last_gameplay_resume_ack_ = {};
    gameplay_resume_ack_until_ = {};
    last_authoritative_request_ = {};
    last_authoritative_request_frame_ = 0U;
    simulation_completed_frame_.fill(0U);
    simulation_progress_present_.fill(false);
    simulation_progress_time_.fill({});
    last_simulation_progress_datagram_ = {};
}

SessionPollResult DirectSession::poll_gameplay_ready(
    std::uint32_t requested_map, std::uint32_t resolved_map,
    std::uint32_t racer_count, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing ||
        scene_epoch_ == 0U ||
        racer_count > 10U) {
        error = "The synchronized gameplay-start barrier is not available.";
        return SessionPollResult::Failed;
    }
    if (!gameplay_handoff_ || !gameplay_handoff_->local_load_started ||
        !gameplay_handoff_->suspended ||
        gameplay_handoff_->current_input_epoch != input_epoch_ ||
        gameplay_handoff_->boundary_frame < next_commit_frame_ ||
        gameplay_handoff_->map != requested_map) {
        error = "The track load did not enter the synchronized input handoff.";
        return SessionPollResult::Failed;
    }
    if (gameplay_handoff_->resume_input_epoch == 0U) {
        error.clear();
        return SessionPollResult::Pending;
    }
    if (!gameplay_barrier_) {
        gameplay_barrier_ = GameplayBarrierState{
            scene_epoch_, resolved_map, racer_count};
        gameplay_barrier_->epoch_synchronized = is_host_;
    }
    if (gameplay_barrier_->map != resolved_map ||
        gameplay_barrier_->racer_count != racer_count) {
        fail_locked("The racers reached conflicting gameplay-start boundaries.");
        error = status_;
        return SessionPollResult::Failed;
    }

    gameplay_barrier_->ready[local_slot_] = true;
    const auto all_ready = [this]() {
        const Room& room = is_host_ ? lobby_.room() : room_view_;
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied &&
                !gameplay_barrier_->ready[slot]) {
                return false;
            }
        }
        return true;
    };

    const auto now = std::chrono::steady_clock::now();
    if (is_host_ && !all_ready() &&
        (last_gameplay_barrier_broadcast_.time_since_epoch().count() == 0 ||
         now - last_gameplay_barrier_broadcast_ >=
             std::chrono::milliseconds(100))) {
        // Announce Player 1's scene identity before accepting Ready. This
        // removes the circular dependency where a client previously had to
        // guess the host's independently advanced epoch before the host would
        // send any barrier response.
        const auto payload = protocol::encode_gameplay_barrier({
            scene_epoch_, resolved_map, racer_count, local_slot_,
            protocol::GameplayBarrierStage::Prepare});
        for (int copy = 0; copy < 3; ++copy) {
            broadcast(protocol::MessageType::GameplayBarrier, payload);
        }
        last_gameplay_barrier_broadcast_ = now;
    }
    if (is_host_ && all_ready()) {
        if (!gameplay_barrier_->baseline_released) {
            gameplay_barrier_->baseline_released = true;
            const auto payload = protocol::encode_gameplay_barrier({
                scene_epoch_, resolved_map, racer_count, local_slot_,
                protocol::GameplayBarrierStage::Baseline});
            for (int copy = 0; copy < 3; ++copy) {
                broadcast(protocol::MessageType::GameplayBarrier, payload);
            }
            last_gameplay_barrier_broadcast_ = now;
        }
        status_ = "Every racer reached the same track; synchronizing Player 1's baseline.";
        error.clear();
        return SessionPollResult::Ready;
    }
    if (!is_host_ && gameplay_barrier_->epoch_synchronized &&
        (last_gameplay_ready_announcement_.time_since_epoch().count() == 0 ||
         now - last_gameplay_ready_announcement_ >=
             std::chrono::milliseconds(100))) {
        send_to(host_address_, protocol::MessageType::GameplayBarrier,
                protocol::encode_gameplay_barrier({
                    scene_epoch_, resolved_map, racer_count, local_slot_,
                    protocol::GameplayBarrierStage::Ready}));
        last_gameplay_ready_announcement_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    if (!is_host_ && gameplay_barrier_->baseline_released) {
        status_ = "Player 1 released the synchronized track baseline.";
        error.clear();
        return SessionPollResult::Ready;
    }
    error.clear();
    return SessionPollResult::Pending;
}

bool DirectSession::wait_gameplay_ready(
    std::uint32_t requested_map, std::uint32_t resolved_map,
    std::uint32_t racer_count,
    std::chrono::milliseconds timeout, std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const SessionPollResult result =
            poll_gameplay_ready(requested_map, resolved_map, racer_count,
                                error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    std::scoped_lock lock(mutex_);
    fail_locked("A racer did not reach the synchronized gameplay-start boundary.");
    error = status_;
    return false;
}

void DirectSession::remember_release_locked(protocol::MessageType type,
    std::uint32_t epoch, std::uint32_t boundary, std::uint32_t detail) {
    if (completed_release_locked(type, epoch, boundary, detail)) return;
    CompletedRelease completed{type, epoch, boundary, detail};
    for (const auto& peer : peers_) {
        if (peer.active) completed.recipients[peer.slot] = peer.sender_id;
    }
    completed.expires = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    constexpr std::size_t maximum_completed = 16U;
    if (completed_releases_.size() == maximum_completed) completed_releases_.pop_front();
    completed_releases_.push_back(completed);
}

bool DirectSession::completed_release_locked(protocol::MessageType type,
    std::uint32_t epoch, std::uint32_t boundary, std::uint32_t detail,
    std::uint64_t recipient) {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(completed_releases_, [now](const auto& entry) { return entry.expires <= now; });
    return std::any_of(completed_releases_.begin(), completed_releases_.end(),
        [&](const auto& entry) {
            return entry.type == type && entry.epoch == epoch &&
                entry.boundary == boundary && entry.detail == detail &&
                (recipient == 0U || std::find(entry.recipients.begin(),
                    entry.recipients.end(), recipient) != entry.recipients.end());
        });
}

SessionPollResult DirectSession::poll_gameplay_resume(
    std::uint32_t map, std::uint32_t racer_count, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running || !authoritative_phase_active_ ||
        !gameplay_barrier_ ||
        !gameplay_barrier_->epoch_synchronized ||
        gameplay_barrier_->scene_epoch != scene_epoch_ ||
        gameplay_barrier_->map != map ||
        gameplay_barrier_->racer_count != racer_count ||
        !gameplay_barrier_->baseline_released) {
        error = "The synchronized gameplay baseline is not ready to resume.";
        return SessionPollResult::Failed;
    }
    if (is_host_) {
        if (!fully_acknowledged_states_.contains(0U)) {
            error.clear();
            return SessionPollResult::Pending;
        }
        if (!gameplay_barrier_->arm_announced) {
            gameplay_barrier_->arm_announced = true;
            std::uint32_t resume_frame = 0U;
            if (!arm_gameplay_handoff_locked(resume_frame, error)) {
                return SessionPollResult::Failed;
            }
            const auto payload = protocol::encode_gameplay_barrier({
                scene_epoch_, map, racer_count, local_slot_,
                protocol::GameplayBarrierStage::Arm});
            for (int copy = 0; copy < 5; ++copy) {
                broadcast(protocol::MessageType::GameplayBarrier, payload);
            }
            const auto now = std::chrono::steady_clock::now();
            last_gameplay_barrier_broadcast_ = now;
            gameplay_resume_until_ = now + std::chrono::seconds(2);
        }
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied &&
                !gameplay_barrier_->armed[slot]) {
                status_ = "Waiting for every racer to arm the gameplay input epoch.";
                error.clear();
                return SessionPollResult::Pending;
            }
        }
        if (!gameplay_barrier_->go_released) {
            gameplay_barrier_->go_released = true;
            remember_release_locked(protocol::MessageType::GameplayBarrier,
                scene_epoch_, map, racer_count);
            const auto payload = protocol::encode_gameplay_barrier({
                scene_epoch_, map, racer_count, local_slot_,
                protocol::GameplayBarrierStage::Go});
            for (int copy = 0; copy < 5; ++copy) {
                broadcast(protocol::MessageType::GameplayBarrier, payload);
            }
            const auto now = std::chrono::steady_clock::now();
            last_gameplay_barrier_broadcast_ = now;
            gameplay_resume_until_ = now + std::chrono::seconds(2);
        }
        status_ = "Every racer armed the synchronized race epoch. Go!";
        error.clear();
        return SessionPollResult::Ready;
    }

    if (gameplay_barrier_->arm_announced &&
        !gameplay_barrier_->armed[local_slot_]) {
        std::uint32_t resume_frame = 0U;
        if (!arm_gameplay_handoff_locked(resume_frame, error)) {
            return SessionPollResult::Failed;
        }
    }
    if (!gameplay_barrier_->go_released) {
        error.clear();
        return SessionPollResult::Pending;
    }
    status_ = "The synchronized race baseline is armed. Go!";
    remember_release_locked(protocol::MessageType::GameplayBarrier,
        scene_epoch_, map, racer_count);
    error.clear();
    return SessionPollResult::Ready;
}

bool DirectSession::synchronize_gameplay_resume(
    std::uint32_t map, std::uint32_t racer_count,
    std::chrono::milliseconds timeout, std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const SessionPollResult result =
            poll_gameplay_resume(map, racer_count, error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    std::scoped_lock lock(mutex_);
    fail_locked("Player 1 did not release the synchronized gameplay baseline.");
    error = status_;
    return false;
}

bool DirectSession::begin_finish_seal(std::uint32_t& frame,
                                      std::uint8_t transition_kind,
                                      std::string& error) {
    std::unique_lock lock(mutex_);
    if (transition_kind == 0U) {
        error = "The final race-state boundary has no transition kind.";
        return false;
    }
    if (state_ != ConnectionState::Running || !authoritative_phase_active_) {
        error.clear();
        return false;
    }
    if (authority_lifecycle_ == AuthorityLifecycle::PostRace) {
        error.clear();
        return false;
    }
    if (authority_lifecycle_ == AuthorityLifecycle::SealingFinish) {
        if (finish_seal_frame_ && *finish_seal_frame_ == frame) {
            error.clear();
            return true;
        }
        error = "A conflicting final race-state boundary was requested.";
        return false;
    }
    if (authority_lifecycle_ != AuthorityLifecycle::Racing) {
        error = "The race authority lifecycle is not ready to seal a finish.";
        return false;
    }
    // Player 1 announces the finish before the retail transition. This lets a
    // client parked at an older recovery boundary release that obsolete
    // recovery and continue to the finish hook. No second snapshot is sent:
    // the immutable Player-1 frame ledger is the only normal race authority.
    if (!is_host_ && !transition_barrier_) {
        // The authored frame coordinator will poll again after Player 1's
        // announcement arrives. Never sleep inside DKR's finish hook.
        worker_wake_ = true;
        state_changed_.notify_all();
        error.clear();
        return false;
    }
    if (is_host_) {
        if (!transition_barrier_) {
            transition_barrier_ = TransitionBarrierState{
                scene_epoch_, frame, transition_kind};
            transition_barrier_->acknowledgements[local_slot_] = true;
        } else if (transition_barrier_->scene_epoch != scene_epoch_ ||
                   transition_barrier_->frame != frame ||
                   transition_barrier_->kind != transition_kind) {
            error = "A conflicting final race transition was already announced.";
            return false;
        }
    } else if (transition_barrier_) {
        if (transition_barrier_->scene_epoch != scene_epoch_ ||
            transition_barrier_->kind != transition_kind) {
            error = "Player 1 announced a different final race transition.";
            return false;
        }
        frame = transition_barrier_->frame;
    }

    // The finish hook executes while DKR's racer array is still intact. Any
    // recovery scheduled from an older digest is superseded by this lifecycle
    // boundary; carrying it into post-race teardown would target racer objects
    // that DKR is about to delete and compact.
    recovery_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    completed_recovery_frame_.reset();
    completed_recovery_epoch_ = 0U;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    recovery_resume_until_ = {};
    transport_->discard_received(TransportTrafficClass::Replica);
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    authority_outbound_.clear();
    rollback_inbound_.clear();
    authority_lifecycle_ = AuthorityLifecycle::SealingFinish;
    finish_seal_frame_ = frame;
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, frame, transition_kind,
        scene_epoch_, "race finish seal began");
    if (is_host_) {
        const auto payload = protocol::encode_transition_barrier({
            scene_epoch_, frame, transition_kind, local_slot_,
            protocol::TransitionBarrierStage::Begin});
        for (int copy = 0; copy < 3; ++copy) {
            broadcast(protocol::MessageType::TransitionBarrier,
                      payload, frame);
        }
        last_transition_broadcast_ = std::chrono::steady_clock::now();
    }
    error.clear();
    return true;
}

SessionPollResult DirectSession::poll_authoritative_acknowledgements(
    std::uint32_t frame, std::string& error) {
    std::scoped_lock lock(mutex_);
    const bool finish_boundary =
        authority_lifecycle_ == AuthorityLifecycle::SealingFinish &&
        finish_seal_frame_ && *finish_seal_frame_ == frame;
    const bool gameplay_baseline =
        authority_lifecycle_ == AuthorityLifecycle::Racing && frame == 0U &&
        gameplay_barrier_ && gameplay_barrier_->scene_epoch == scene_epoch_ &&
        gameplay_barrier_->baseline_released &&
        !gameplay_barrier_->arm_announced;
    std::uint32_t baseline_retry_ms = 100U;
    for (const auto& peer : peers_) {
        if (peer.active) baseline_retry_ms = (std::max)(baseline_retry_ms,
            checkpoint_retry_milliseconds(
                static_cast<std::uint16_t>((std::clamp)(peer.rtt_ms, 0.0, 65535.0)),
                static_cast<std::uint16_t>((std::clamp)(peer.jitter_ms, 0.0, 65535.0))));
    }
    if (!is_host_ || state_ != ConnectionState::Running ||
        (!finish_boundary && !gameplay_baseline)) {
        error = status_.empty()
            ? "The authoritative acknowledgement barrier is unavailable."
            : status_;
        return SessionPollResult::Failed;
    }
    if (fully_acknowledged_states_.contains(frame)) {
        error.clear();
        return SessionPollResult::Ready;
    }
    if (state_ != ConnectionState::Running ||
        (authority_lifecycle_ != AuthorityLifecycle::SealingFinish &&
         authority_lifecycle_ != AuthorityLifecycle::Racing)) {
        error = status_;
        return SessionPollResult::Failed;
    }
    const auto now = std::chrono::steady_clock::now();
    if (finish_boundary && transition_barrier_ &&
        (last_transition_broadcast_.time_since_epoch().count() == 0 ||
         now - last_transition_broadcast_ >=
             std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::TransitionBarrier,
                  protocol::encode_transition_barrier({
                      transition_barrier_->scene_epoch,
                      transition_barrier_->frame,
                      transition_barrier_->kind,
                      local_slot_,
                      protocol::TransitionBarrierStage::Begin}),
                  transition_barrier_->frame);
        last_transition_broadcast_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    } else if (gameplay_baseline && gameplay_barrier_ &&
               (last_gameplay_barrier_broadcast_.time_since_epoch().count() == 0 ||
                now - last_gameplay_barrier_broadcast_ >=
                    std::chrono::milliseconds(baseline_retry_ms))) {
        broadcast(protocol::MessageType::GameplayBarrier,
                  protocol::encode_gameplay_barrier({
                      gameplay_barrier_->scene_epoch,
                      gameplay_barrier_->map,
                      gameplay_barrier_->racer_count,
                      local_slot_,
                      protocol::GameplayBarrierStage::Baseline}));
        const auto state = authoritative_states_.find(frame);
        if (state != authoritative_states_.end()) {
            send_authoritative_state(nullptr, frame, state->second);
        }
        last_gameplay_barrier_broadcast_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    error.clear();
    return SessionPollResult::Pending;
}

bool DirectSession::wait_authoritative_acknowledgements(
    std::uint32_t frame, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string error;
    while (std::chrono::steady_clock::now() < deadline) {
        const SessionPollResult result =
            poll_authoritative_acknowledgements(frame, error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
    std::scoped_lock lock(mutex_);
    ++authority_wait_timeouts_;
    const bool gameplay_baseline =
        authority_lifecycle_ == AuthorityLifecycle::Racing && frame == 0U;
    fail_locked(gameplay_baseline
        ? "A racer did not acknowledge Player 1's gameplay-start baseline."
        : "A racer did not acknowledge the final synchronized race state at frame " +
              std::to_string(frame) + ".");
    return false;
}

bool DirectSession::complete_finish_seal(std::uint32_t frame,
                                         std::string& error) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running ||
        authority_lifecycle_ != AuthorityLifecycle::SealingFinish ||
        !finish_seal_frame_ || *finish_seal_frame_ != frame) {
        error = "The final race-state boundary was not active.";
        return false;
    }
    authoritative_phase_active_ = false;
    authority_lifecycle_ = AuthorityLifecycle::PostRace;
    // The finish barrier proves that every occupied peer installed and
    // acknowledged this exact completed frame. Seed the continuing post-race
    // ledger from that shared boundary so host lead limiting is valid from the
    // first results-screen tick rather than waiting for a fresh heartbeat.
    simulation_completed_frame_.fill(frame);
    simulation_progress_present_.fill(false);
    simulation_progress_time_.fill({});
    const Room& progress_room = is_host_ ? lobby_.room() : room_view_;
    const auto progress_now = std::chrono::steady_clock::now();
    for (std::size_t slot = 0U; slot < progress_room.players.size(); ++slot) {
        if (!progress_room.players[slot].occupied) continue;
        simulation_progress_present_[slot] = true;
        simulation_progress_time_[slot] = progress_now;
    }
    last_simulation_progress_datagram_ = {};
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, frame, scene_epoch_,
        input_epoch_, "race finish seal completed");
    state_hashes_.clear();
    recovery_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    completed_recovery_frame_.reset();
    completed_recovery_epoch_ = 0U;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    recovery_resume_until_ = {};
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    authority_outbound_.clear();
    rollback_inbound_.clear();
    error.clear();
    return true;
}

void DirectSession::end_authoritative_phase() {
    std::scoped_lock lock(mutex_);
    if (authority_lifecycle_ == AuthorityLifecycle::Racing &&
        recovery_frame_ && !recovery_resumed_) {
        fail_locked(
            "The level attempted to unload before its synchronized recovery completed.");
        return;
    }
    failure_recorder().record(
        FailureEventKind::LifecycleBoundary, next_commit_frame_, scene_epoch_,
        input_epoch_, "race authority ended");
    authoritative_phase_active_ = false;
    authority_lifecycle_ = AuthorityLifecycle::Inactive;
    finish_seal_frame_.reset();
    transport_->discard_received(TransportTrafficClass::Replica);
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    fully_acknowledged_states_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    authority_outbound_.clear();
    rollback_inbound_.clear();
    consecutive_authoritative_frames_ = 0U;
    recovery_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    completed_recovery_frame_.reset();
    completed_recovery_epoch_ = 0U;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    recovery_resume_until_ = {};
    // Keep Player 1's completed finish barrier alive for its existing Resume
    // retransmission window. Race teardown follows the first Resume
    // immediately, so clearing the barrier here cancelled the worker's retry
    // path precisely when a delayed/lost datagram needed it most. The next
    // authoritative phase resets this tombstone after advancing scene_epoch_.
    const bool preserve_finish_resume =
        is_host_ && transition_barrier_ && transition_barrier_->resumed &&
        std::chrono::steady_clock::now() < transition_resume_until_;
    if (!preserve_finish_resume) {
        transition_barrier_.reset();
        last_transition_broadcast_ = {};
        transition_resume_until_ = {};
    }
    gameplay_barrier_.reset();
    last_gameplay_barrier_broadcast_ = {};
    gameplay_resume_until_ = {};
    last_gameplay_ready_announcement_ = {};
    last_gameplay_resume_ack_ = {};
    gameplay_resume_ack_until_ = {};
    last_authoritative_request_ = {};
    last_authoritative_request_frame_ = 0U;
    // The authored input epoch continues through results and all frontend
    // menus. Preserve its completed-frame watermarks here; clearing them made
    // Player 1 blind to accumulated menu debt until the next race handoff.
}

void DirectSession::confirm_authoritative_state(std::uint32_t frame,
                                                bool corrected) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running) return;
    if (consecutive_authoritative_frames_ == 0U ||
        frame > last_authoritative_frame_) {
        ++consecutive_authoritative_frames_;
    } else if (frame != last_authoritative_frame_) {
        consecutive_authoritative_frames_ = 1U;
    }
    last_authoritative_frame_ = (std::max)(last_authoritative_frame_, frame);
    if (corrected) ++authoritative_corrections_;
    const bool recovery_checkpoint = recovery_frame_ &&
                                     frame == *recovery_frame_;
    if (!recovery_checkpoint) {
        if (is_host_) {
            authoritative_acknowledgements_[frame][local_slot_] = true;
        } else {
            const auto state = authoritative_states_.find(frame);
            if (state != authoritative_states_.end()) {
                authoritative_acknowledgements_[frame][local_slot_] = true;
                const protocol::StateAcknowledgePayload acknowledgement{
                    scene_epoch_, frame,
                    authoritative_state_checksum(state->second), local_slot_};
                send_to(host_address_,
                        protocol::MessageType::StateAcknowledge,
                        protocol::encode_state_acknowledge(acknowledgement),
                        frame);
            }
        }
        return;
    }
    recovery_acks_[local_slot_] = true;
    recovery_stage_ = RecoveryStage::AwaitingResume;
    if (is_host_) {
        bool complete = true;
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied && !recovery_acks_[slot]) {
                complete = false;
            }
        }
        if (complete && !recovery_resumed_) {
            const auto payload = protocol::encode_recovery(
                {scene_epoch_, frame, 0U});
            for (int copy = 0; copy < 3; ++copy) {
                broadcast(protocol::MessageType::RecoveryResume,
                          payload, frame);
            }
            recovery_resumed_ = true;
            recovery_stage_ = RecoveryStage::Completed;
            completed_recovery_frame_ = frame;
            remember_release_locked(protocol::MessageType::RecoveryResume,
                scene_epoch_, frame, 0U);
            completed_recovery_epoch_ = scene_epoch_;
            recovery_resume_until_ = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(1);
        }
    } else {
        const auto payload = protocol::encode_recovery(
            {scene_epoch_, frame, local_slot_});
        for (int copy = 0; copy < 3; ++copy) {
            send_to(host_address_, protocol::MessageType::RecoveryAck,
                    payload, frame);
        }
    }
}

std::optional<std::uint32_t> DirectSession::recovery_frame() const {
    std::scoped_lock lock(mutex_);
    return authoritative_phase_active_ ? recovery_frame_ : std::nullopt;
}

bool DirectSession::wait_recovery_complete(
    std::uint32_t frame, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        std::string error;
        const SessionPollResult result = poll_recovery_complete(frame, error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        if (std::chrono::steady_clock::now() >= deadline) {
            std::scoped_lock lock(mutex_);
            fail_locked("The synchronized recovery timed out at simulation frame " +
                        std::to_string(frame) + ".");
            return false;
        }
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(20));
    }
}

SessionPollResult DirectSession::poll_recovery_complete(
    std::uint32_t frame, std::string& error) {
    std::scoped_lock lock(mutex_);
    if (!recovery_frame_) {
        error.clear();
        return SessionPollResult::Ready;
    }
    if (*recovery_frame_ != frame) {
        error = "A different synchronized recovery boundary is active.";
        return SessionPollResult::Failed;
    }
    if (state_ != ConnectionState::Running ||
        !authoritative_phase_active_) {
        error = status_.empty()
            ? "The online session ended during synchronized recovery."
            : status_;
        return SessionPollResult::Failed;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!is_host_ && recovery_acks_[local_slot_] &&
        (last_recovery_broadcast_.time_since_epoch().count() == 0 ||
         now - last_recovery_broadcast_ >= std::chrono::milliseconds(100))) {
        send_to(host_address_, protocol::MessageType::RecoveryAck,
                protocol::encode_recovery(
                    {scene_epoch_, frame, local_slot_}), frame);
        last_recovery_broadcast_ = now;
    }
    if (!recovery_resumed_) {
        worker_wake_ = true;
        state_changed_.notify_all();
        error.clear();
        return SessionPollResult::Pending;
    }
    remember_release_locked(protocol::MessageType::RecoveryResume,
        scene_epoch_, frame, 0U);
    recovery_frame_.reset();
    recovery_stage_ = RecoveryStage::Idle;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    rollback_inbound_.clear();
    if (launch_descriptor_ &&
        launch_descriptor_->synchronization == SynchronizationMode::Rollback) {
        ++rollback_transport_epoch_;
        if (rollback_transport_epoch_ == 0U) rollback_transport_epoch_ = 1U;
    }
    authoritative_states_.clear();
    authoritative_acknowledgements_.clear();
    snapshot_assemblies_.clear();
    live_replica_states_.clear();
    racer_orientation_states_.clear();
    live_replica_assemblies_.clear();
    live_replica_keyframe_frame_.reset();
    live_replica_keyframe_state_.clear();
    authority_outbound_.clear();
    error.clear();
    return SessionPollResult::Ready;
}

SessionPollResult DirectSession::poll_transition(
    std::uint32_t frame, std::uint8_t transition_kind,
    std::string& error) {
    std::scoped_lock lock(mutex_);
    // These Patch Pipeline hooks also execute during normal offline play.
    // A barrier is only meaningful while an online authored-gameplay epoch is
    // active; outside that scope it must remain a transparent no-op.
    if (state_ != ConnectionState::Running || !authoritative_phase_active_) {
        error.clear();
        return SessionPollResult::Ready;
    }
    if (transition_kind == 0U) {
        error = "The synchronized transition has no transition kind.";
        return SessionPollResult::Failed;
    }
    if (is_host_) {
        if (!transition_barrier_) {
            transition_barrier_ = TransitionBarrierState{
                scene_epoch_, frame, transition_kind};
            transition_barrier_->acknowledgements[local_slot_] = true;
        } else if (transition_barrier_->scene_epoch != scene_epoch_ ||
                   transition_barrier_->frame != frame ||
                   transition_barrier_->kind != transition_kind) {
            fail_locked(
                "A conflicting host-authoritative race transition was requested.");
            error = status_;
            return SessionPollResult::Failed;
        }
    }
    if (!transition_barrier_) {
        error.clear();
        return SessionPollResult::Pending;
    }
    if (transition_barrier_->scene_epoch != scene_epoch_ ||
        transition_barrier_->frame != frame ||
        transition_barrier_->kind != transition_kind) {
        fail_locked("The racers reached different race-result transitions.");
        error = status_;
        return SessionPollResult::Failed;
    }
    transition_barrier_->acknowledgements[local_slot_] = true;
    bool complete = true;
    const Room& room = is_host_ ? lobby_.room() : room_view_;
    for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
        if (room.players[slot].occupied &&
            !transition_barrier_->acknowledgements[slot]) {
            complete = false;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (is_host_ && complete && !transition_barrier_->resumed) {
        transition_barrier_->resumed = true;
        const auto payload = protocol::encode_transition_barrier({
            scene_epoch_, frame, transition_kind, local_slot_,
            protocol::TransitionBarrierStage::Resume});
        for (int copy = 0; copy < 3; ++copy) {
            broadcast(protocol::MessageType::TransitionBarrier,
                      payload, frame);
        }
        last_transition_broadcast_ = now;
        transition_resume_until_ = now + std::chrono::seconds(1);
    }
    if (transition_barrier_->resumed) {
        remember_release_locked(protocol::MessageType::TransitionBarrier,
            scene_epoch_, frame, transition_kind);
        error.clear();
        return SessionPollResult::Ready;
    }
    if (last_transition_broadcast_.time_since_epoch().count() == 0 ||
        now - last_transition_broadcast_ >= std::chrono::milliseconds(100)) {
        const auto stage = is_host_
            ? protocol::TransitionBarrierStage::Begin
            : protocol::TransitionBarrierStage::Acknowledge;
        const auto payload = protocol::encode_transition_barrier({
            scene_epoch_, frame, transition_kind, local_slot_, stage});
        if (is_host_) {
            broadcast(protocol::MessageType::TransitionBarrier,
                      payload, frame);
        } else {
            send_to(host_address_, protocol::MessageType::TransitionBarrier,
                    payload, frame);
        }
        last_transition_broadcast_ = now;
        worker_wake_ = true;
        state_changed_.notify_all();
    }
    error.clear();
    return SessionPollResult::Pending;
}

bool DirectSession::synchronize_transition(
    std::uint32_t frame, std::uint8_t transition_kind,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string error;
    while (std::chrono::steady_clock::now() < deadline) {
        const SessionPollResult result =
            poll_transition(frame, transition_kind, error);
        if (result == SessionPollResult::Ready) return true;
        if (result == SessionPollResult::Failed) return false;
        std::unique_lock lock(mutex_);
        state_changed_.wait_for(lock, std::chrono::milliseconds(10));
    }
    std::scoped_lock lock(mutex_);
    fail_locked(
        "A racer did not reach the host-authoritative race transition at frame " +
        std::to_string(frame) + ".");
    return false;
}

void DirectSession::fail_authoritative_state(std::string reason) {
    std::scoped_lock lock(mutex_);
    if (state_ == ConnectionState::Running) {
        fail_locked(std::move(reason));
    }
}

void DirectSession::submit_state_hash(std::uint32_t frame, std::uint64_t hash) {
    submit_state_digest(frame, hash, 0U, 0U, 0U);
}

void DirectSession::submit_state_digest(
    std::uint32_t frame, std::uint64_t hash, std::uint64_t globals_hash,
    std::uint64_t roster_hash, std::uint64_t racers_hash,
    std::uint32_t racer_count,
    std::array<std::uint64_t, 10U> racer_hashes) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running) return;
    HashFrame& hashes = state_hashes_[frame];
    hashes.values[local_slot_] = hash;
    hashes.globals[local_slot_] = globals_hash;
    hashes.roster[local_slot_] = roster_hash;
    hashes.racers[local_slot_] = racers_hash;
    hashes.racer_counts[local_slot_] = racer_count;
    hashes.racer_details[local_slot_] = racer_hashes;
    hashes.present[local_slot_] = true;
    if (!is_host_ && hashes.present[0] &&
        hashes.values[local_slot_] == hashes.values[0]) {
        last_verified_frame_ = (std::max)(last_verified_frame_, frame);
    }
    const auto payload = protocol::encode_state_hash(
        {scene_epoch_, local_slot_, hash, globals_hash, roster_hash, racers_hash,
         racer_count, racer_hashes});
    if (is_host_) broadcast(protocol::MessageType::StateHash, payload, frame);
    else send_to(host_address_, protocol::MessageType::StateHash, payload, frame);
    if (is_host_) evaluate_state_hash(frame);
    if (frame > 300U) {
        const std::uint32_t oldest = frame - 300U;
        for (auto iterator = state_hashes_.begin(); iterator != state_hashes_.end();) {
            if (iterator->first < oldest) iterator = state_hashes_.erase(iterator);
            else ++iterator;
        }
    }
}

bool DirectSession::request_rollback_recovery(
    std::uint32_t mismatch_frame, std::string_view subsystem) {
    std::scoped_lock lock(mutex_);
    if (state_ != ConnectionState::Running || !launch_descriptor_ ||
        launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing ||
        scene_epoch_ == 0U) {
        return false;
    }
    if (recovery_frame_) return true;
    if (is_host_) {
        schedule_recovery_locked(mismatch_frame, std::string(subsystem));
        return recovery_frame_.has_value();
    }
    const auto payload = protocol::encode_recovery(
        {scene_epoch_, mismatch_frame, local_slot_});
    for (int copy = 0; copy < 3; ++copy) {
        send_to(host_address_, protocol::MessageType::RollbackRecoveryRequest,
                payload, mismatch_frame);
    }
    worker_wake_ = true;
    state_changed_.notify_all();
    return true;
}

SessionView DirectSession::view() const {
    std::scoped_lock lock(mutex_);
    return view_locked();
}

SessionPacingView DirectSession::pacing_view() const {
    performance_trace::Scope profile(performance_trace::Region::OnlinePacingView);
    std::scoped_lock lock(mutex_);
    SessionPacingView result{state_, 0, 0, authoritative_input_frame_,
        input_epoch_, simulation_wake_generation_};
    if (is_host_) {
        for (const auto& peer : peers_) {
            if (!peer.active) continue;
            result.network_rtt_ms = (std::max)(result.network_rtt_ms,
                static_cast<std::uint16_t>(std::clamp(peer.rtt_ms, 0.0, 65535.0)));
            result.network_jitter_ms = (std::max)(result.network_jitter_ms,
                static_cast<std::uint16_t>(std::clamp(peer.jitter_ms, 0.0, 65535.0)));
        }
    } else if (local_slot_ < room_view_.players.size()) {
        result.network_rtt_ms = room_view_.players[local_slot_].ping_ms;
        result.network_jitter_ms = room_view_.players[local_slot_].jitter_ms;
    }
    return result;
}

SessionView DirectSession::presentation_view() const {
    const auto snapshot = presentation_view_.load(std::memory_order_acquire);
    return snapshot ? *snapshot : SessionView{};
}

bool DirectSession::presentation_active() const {
    const auto snapshot = presentation_view_.load(std::memory_order_acquire);
    return snapshot && snapshot->state != ConnectionState::Offline && snapshot->state != ConnectionState::Failed;
}

SessionView DirectSession::view_locked() const {
    SessionView result{state_, is_host_ ? lobby_.room() : room_view_, is_host_,
                       local_slot_, transport_->local_port(), invite_, status_,
                       last_verified_frame_ > 0U,
                       last_verified_frame_, last_authoritative_frame_,
                       authoritative_corrections_, input_delay_, method_,
                       match_id_, lobby_locked_};
    result.owned_match_ending = owned_match_end_ && !owned_match_end_->resumed;
    result.input_stalls = input_stalls_;
    result.longest_input_stall_ms = longest_input_stall_ms_;
    result.packets_sent = packets_sent_;
    result.packets_received = packets_received_;
    result.join_packets_recognized = join_packets_recognized_;
    result.join_packets_stale = join_packets_stale_;
    result.join_packets_auth_rejected = join_packets_auth_rejected_;
    result.join_packets_manifest_rejected = join_packets_manifest_rejected_;
    result.join_packets_accepted = join_packets_accepted_;
    result.admission_packets_queued = static_cast<std::size_t>(std::count_if(
        normal_priority_outbound_.begin(), normal_priority_outbound_.end(),
        [](const OutboundPacket& packet) {
            return packet.type == protocol::MessageType::Hello ||
                   packet.type == protocol::MessageType::JoinPending ||
                   packet.type == protocol::MessageType::HelloAck;
        }));
    result.outbound_queue_high_water = outbound_queue_high_water_;
    result.rollback_queue_high_water = rollback_queue_high_water_;
    result.transport_queue_failures = transport_queue_failures_;
    result.control_transport_buffered_bytes =
        transport_->buffered_bytes(TransportTrafficClass::Control);
    result.authority_transport_buffered_bytes =
        transport_->buffered_bytes(TransportTrafficClass::Authoritative);
    result.realtime_transport_buffered_bytes =
        transport_->buffered_bytes(TransportTrafficClass::Realtime);
    result.replica_transport_buffered_bytes =
        transport_->buffered_bytes(TransportTrafficClass::Replica);
    result.checkpoint_transport_buffered_bytes = transport_->buffered_bytes(TransportTrafficClass::Checkpoint);
    result.duplicate_retries_coalesced = duplicate_retries_coalesced_;
    result.checkpoint_encoding_cache_hits = checkpoint_encoding_cache_hits_;
    result.maximum_network_pump_us = maximum_network_pump_us_;
    const auto queue_now = std::chrono::steady_clock::now();
    std::size_t lane_index = 0;
    for (const auto* queue : {&critical_outbound_, &repair_outbound_, &commit_outbound_,
             &high_priority_outbound_, &authority_outbound_, &normal_priority_outbound_, &bulk_outbound_}) {
        for (const auto& packet : *queue) {
            result.pending_outbound_bytes += packet.bytes.size();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(queue_now - packet.enqueued).count();
            result.oldest_outbound_age_ms = (std::max)(result.oldest_outbound_age_ms,
                static_cast<std::uint32_t>((std::clamp<std::int64_t>)(age, 0, UINT32_MAX)));
            // No peer addresses or identities are exported by diagnostics.
            std::size_t slot = 0;
            if (is_host_) {
                for (const auto& peer : peers_)
                    if (peer.active && peer.address == packet.destination) { slot = peer.slot; break; }
            }
            if (slot < kMaximumPlayers) {
                auto& lane = result.outbound_lanes[slot][lane_index];
                ++lane.packets;
                lane.bytes += packet.bytes.size();
                lane.oldest_ms = (std::max)(lane.oldest_ms,
                    static_cast<std::uint32_t>((std::clamp<std::int64_t>)(age, 0, UINT32_MAX)));
            }
        }
        ++lane_index;
    }
    result.host_backpressure_events = host_backpressure_events_;
    result.maximum_peer_frame_debt = maximum_peer_frame_debt_;
    result.recovering_peer_count = recovering_peer_count_;
    if (is_host_) {
        const Room& progress_room = lobby_.room();
        const auto now = std::chrono::steady_clock::now();
        for (std::size_t slot = 0U; slot < progress_room.players.size(); ++slot) {
            if (slot == local_slot_ || !progress_room.players[slot].occupied ||
                !simulation_progress_present_[slot]) {
                continue;
            }
            result.peer_progress_known = true;
            const auto age = std::chrono::duration_cast<
                std::chrono::milliseconds>(now - simulation_progress_time_[slot]);
            if (age.count() > 0) {
                result.oldest_peer_progress_age_ms = (std::max)(
                    result.oldest_peer_progress_age_ms,
                    static_cast<std::uint32_t>((std::min)(
                        static_cast<std::uint64_t>(age.count()),
                        static_cast<std::uint64_t>(UINT32_MAX))));
            }
        }
    }
    result.commit_repair_requests_sent = commit_repair_requests_sent_;
    result.commit_repair_requests_received = commit_repair_requests_received_;
    result.commit_repair_batches_sent = commit_repair_batches_sent_;
    result.late_inputs_discarded = late_inputs_discarded_;
    result.refreshed_input_samples = refreshed_input_samples_;
    result.stale_input_revisions = stale_input_revisions_;
    result.prediction_limit_waits = prediction_limit_waits_;
    result.local_input_echo_ms = local_input_echo_ms_;
    result.local_input_consume_ms = local_input_consume_ms_;
    result.measured_input_echoes = measured_input_echoes_;
    result.input_echo_latency = input_latency_->echo.summary();
    result.input_consume_latency = input_latency_->consume.summary();
    result.input_enqueue_latency = input_latency_->enqueue.summary();
    result.predicted_local_samples = predicted_local_samples_;
    result.mismatched_local_samples = mismatched_local_samples_;
    result.superseded_local_samples = superseded_local_samples_;
    result.live_replica_requests_sent = live_replica_requests_sent_;
    result.live_replica_request_misses = live_replica_request_misses_;
    result.live_replica_window_rejections = live_replica_window_rejections_;
    result.live_replica_decode_failures = live_replica_decode_failures_;
    result.recovering = recovery_frame_.has_value() && !recovery_resumed_;
    result.local_input_submitted_frame = local_input_submitted_frame_;
    result.local_input_submitted = local_input_submitted_;
    result.host_input_accepted_frame = host_input_accepted_frame_;
    result.authoritative_input_frame = authoritative_input_frame_;
    result.authoritative_inputs = authoritative_inputs_;
    result.authoritative_inputs_valid = authoritative_inputs_valid_;
    result.authoritative_input_revision = authoritative_input_revision_;
    result.authoritative_predicted_mask = authoritative_predicted_mask_;
    result.input_corrections = input_corrections_;
    result.input_epoch = input_epoch_;
    result.scene_epoch = scene_epoch_;
    result.simulation_wake_generation = simulation_wake_generation_;
    result.launch_countdown_active = launch_countdown_active_;
    result.launch_countdown_generation = launch_countdown_generation_;
    result.launch_stage = launch_stage_;
    result.connection_test_active = connection_test_active_;
    result.connection_test_id = connection_test_id_;
    result.connection_test_result_generation =
        connection_test_result_generation_;
    result.connection_test_results = connection_test_results_;
    result.compatibility_sync_offer = compatibility_sync_offer_;
    result.local_online_save_ready = is_host_
        ? local_online_save_ready_ : local_online_save_acknowledged_;
    if (!is_host_ && local_online_save_ready_) {
        // Installing the host's validated HelloAck proves its source save.
        // Other racers require the host's matching roster confirmation.
        result.online_save_ready[0] = true;
        if (online_save_status_ && online_save_status_->room_generation == room_view_.generation) {
            for (std::size_t slot = 0; slot < result.online_save_ready.size(); ++slot)
                result.online_save_ready[slot] = room_view_.players[slot].occupied &&
                    (online_save_status_->verified_mask & (1U << slot));
        }
    }
    if (local_slot_ < result.online_save_ready.size()) {
        result.online_save_ready[local_slot_] =
            result.local_online_save_ready;
    }
    if (is_host_) {
        for (const PeerRecord& peer : peers_) {
            if (peer.active && peer.slot < result.online_save_ready.size()) {
                result.online_save_ready[peer.slot] = peer.online_save_ready;
            }
        }
    }
    if (connection_test_active_) {
        const auto deadline = is_host_ ? connection_test_drain_end_
                                       : connection_test_drain_end_;
        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(deadline -
                                       std::chrono::steady_clock::now());
        result.connection_test_remaining_ms = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(remaining.count(), 0, 15000));
    }
    if (launch_countdown_active_) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = is_host_
            ? launch_countdown_deadline_ - now
            : std::chrono::milliseconds(remote_countdown_remaining_ms_) -
                  (now - remote_countdown_received_at_);
        result.launch_countdown_remaining_ms = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
                    .count(),
                0, 5000));
    }
    if (is_host_) {
        for (const PeerRecord& peer : peers_) {
            if (!peer.active) continue;
            result.network_rtt_ms = (std::max)(
                result.network_rtt_ms,
                static_cast<std::uint16_t>(std::clamp(
                    peer.rtt_ms, 0.0, 65535.0)));
            result.network_jitter_ms = (std::max)(
                result.network_jitter_ms,
                static_cast<std::uint16_t>(std::clamp(
                    peer.jitter_ms, 0.0, 65535.0)));
            result.network_loss_percent = (std::max)(
                result.network_loss_percent, peer.loss_percent);
        }
    } else if (local_slot_ < result.room.players.size()) {
        const Player& local = result.room.players[local_slot_];
        result.network_rtt_ms = local.ping_ms;
        result.network_jitter_ms = local.jitter_ms;
        result.network_loss_percent = local.packet_loss_percent;
    }
    result.mod_offer=mod_offer_;
    if (is_host_) {
        for (const PendingRecord& pending : pending_joins_) {
            if (!pending.active) continue;
            const std::string reason = admission_incompatibility(
                pending.manifest);
            const bool compatible = reason.empty();
            const bool save_sync = compatible &&
                pending.manifest.session_save_hash != manifest_.session_save_hash;
            result.pending_joins.push_back({
                pending.sender_id, pending.display_name, compatible,
                pending.mods_approved
                    ? "Host approved. Awaiting client mod consent and verification."
                    : compatible
                    ? (save_sync
                        ? "Build, ROM and settings match. Player 1's session save will be synchronized after approval."
                        : "Build, ROM, save and gameplay settings match.")
                    : reason, pending.mods_approved});
        }
    }
    return result;
}

RuntimeSessionView DirectSession::runtime_view() const {
    std::scoped_lock lock(mutex_);
    RuntimeSessionView result{};
    result.state = state_;
    result.active = state_ != ConnectionState::Offline &&
                    state_ != ConnectionState::Failed;
    result.running = state_ == ConnectionState::Running;
    result.host = is_host_;
    result.local_slot = local_slot_;
    result.host_control = is_host_ ? lobby_.room().rules.host_control
                                   : room_view_.rules.host_control;
    result.launch_descriptor = launch_descriptor_;
    result.input_epoch = input_epoch_;
    result.scene_epoch = scene_epoch_;
    result.online_save_generation = session_save_generation_;
    result.online_save_hash = manifest_.session_save_hash;
    result.status = status_;
    result.owned_match_ending = owned_match_end_ && !owned_match_end_->resumed;
    if (!replay_write_error_.empty()) result.status += " Replay: " + replay_write_error_;
    return result;
}

std::optional<LaunchDescriptor> DirectSession::launch_descriptor() const {
    std::scoped_lock lock(mutex_);
    return launch_descriptor_;
}

bool DirectSession::active() const {
    std::scoped_lock lock(mutex_);
    return state_ != ConnectionState::Offline && state_ != ConnectionState::Failed;
}

bool DirectSession::running() const {
    std::scoped_lock lock(mutex_);
    return state_ == ConnectionState::Running;
}

std::string DirectSession::make_quick_join_bootstrap() const {
    return "dkr-r-quick-v1\n" + hex64(match_id_) + "\n" +
           hex64(sender_id_) + "\n" +
           secure::encode_key(host_key_pair_.public_key) + "\n" +
           secure::encode_key(invitation_capability_) + "\n" +
           std::to_string(unix_seconds() + 21600U);
}

bool DirectSession::activate_quick_join_locked(std::string& error) {
    if (!quick_join_bootstrap_pending_) return true;
    std::string bootstrap;
    PeerAddress address{};
    if (!transport_->take_quick_join_bootstrap(bootstrap, address)) {
        error.clear();
        return false;
    }
    std::array<std::string_view, 6U> fields{};
    std::string_view remaining(bootstrap);
    for (std::size_t index = 0U; index < fields.size(); ++index) {
        const std::size_t newline = remaining.find('\n');
        if (index + 1U == fields.size()) {
            if (newline != std::string_view::npos) {
                error = "The Quick Join host sent malformed session metadata.";
                return false;
            }
            fields[index] = remaining;
        } else {
            if (newline == std::string_view::npos) {
                error = "The Quick Join host sent incomplete session metadata.";
                return false;
            }
            fields[index] = remaining.substr(0U, newline);
            remaining.remove_prefix(newline + 1U);
        }
    }
    if (fields[0] != "dkr-r-quick-v1") {
        error = "The Quick Join host uses an incompatible bootstrap protocol.";
        return false;
    }
    std::uint64_t match_id = 0U;
    std::uint64_t host_sender_id = 0U;
    std::uint64_t expiry = 0U;
    const auto match_result = std::from_chars(
        fields[1].data(), fields[1].data() + fields[1].size(), match_id, 16);
    const auto sender_result = std::from_chars(
        fields[2].data(), fields[2].data() + fields[2].size(),
        host_sender_id, 16);
    const auto expiry_result = std::from_chars(
        fields[5].data(), fields[5].data() + fields[5].size(), expiry);
    secure::Key host_public{};
    secure::Key invitation_capability{};
    if (match_id == 0U || host_sender_id == 0U ||
        match_result.ec != std::errc{} ||
        match_result.ptr != fields[1].data() + fields[1].size() ||
        sender_result.ec != std::errc{} ||
        sender_result.ptr != fields[2].data() + fields[2].size() ||
        expiry_result.ec != std::errc{} ||
        expiry_result.ptr != fields[5].data() + fields[5].size() ||
        expiry < unix_seconds() || expiry > unix_seconds() + 86400U ||
        !secure::decode_key(fields[3], host_public) ||
        !secure::decode_key(fields[4], invitation_capability)) {
        error = "The Quick Join host sent invalid or expired session metadata.";
        return false;
    }
    host_address_ = address;
    match_id_ = match_id;
    host_sender_id_ = host_sender_id;
    host_public_ = host_public;
    invitation_capability_ = invitation_capability;
    room_view_.room_id = hex64(match_id_);
    quick_join_bootstrap_pending_ = false;
    if (!send_join_request()) {
        error = "The authenticated Quick Join request could not be created.";
        return false;
    }
    status_ = "Waiting for Player 1 to approve this racer...";
    last_connect_request_ = std::chrono::steady_clock::now();
    error.clear();
    return true;
}

bool DirectSession::parse_invite(std::string_view invite, PeerAddress& address,
                                 std::uint64_t& match_id,
                                 std::uint64_t& host_sender_id,
                                 ConnectionMethod& method,
                                 secure::Key& host_public,
                                 secure::Key& invitation_capability,
                                 std::string& error) const {
    constexpr std::string_view prefix = "dkr-r://v7/";
    if (!invite.starts_with(prefix)) {
        error = "The invite must begin with dkr-r://.";
        return false;
    }
    invite.remove_prefix(prefix.size());
    const std::size_t method_slash = invite.find('/');
    if (method_slash == std::string_view::npos) {
        error = "The invitation does not identify its connection method.";
        return false;
    }
    const std::string_view method_text = invite.substr(0U, method_slash);
    if (method_text == "lan") method = ConnectionMethod::Lan;
    else if (method_text == "virtual") method = ConnectionMethod::VirtualLan;
    else if (method_text == "direct") method = ConnectionMethod::DirectInternet;
    else {
        error = "The invitation uses an unsupported connection method.";
        return false;
    }
    invite.remove_prefix(method_slash + 1U);
    const std::size_t first_slash = invite.find('/');
    const std::size_t second_slash = first_slash == std::string_view::npos
        ? std::string_view::npos : invite.find('/', first_slash + 1U);
    const std::size_t third_slash = second_slash == std::string_view::npos
        ? std::string_view::npos : invite.find('/', second_slash + 1U);
    const std::size_t fourth_slash = third_slash == std::string_view::npos
        ? std::string_view::npos : invite.find('/', third_slash + 1U);
    const std::size_t fifth_slash = fourth_slash == std::string_view::npos
        ? std::string_view::npos : invite.find('/', fourth_slash + 1U);
    const std::size_t colon = first_slash == std::string_view::npos
        ? std::string_view::npos : invite.substr(0U, first_slash).rfind(':');
    if (first_slash == std::string_view::npos || second_slash == std::string_view::npos ||
        third_slash == std::string_view::npos || fourth_slash == std::string_view::npos ||
        fifth_slash == std::string_view::npos ||
        colon == std::string_view::npos) {
        error = "The encrypted invite is incomplete.";
        return false;
    }
    std::string host(invite.substr(0U, colon));
    if (host.size() >= 2U && host.front() == '[' && host.back() == ']') {
        host = host.substr(1U, host.size() - 2U);
    }
    const std::string_view port_text = invite.substr(colon + 1U, first_slash - colon - 1U);
    const std::string_view match_text = invite.substr(first_slash + 1U,
                                                       second_slash - first_slash - 1U);
    const std::string_view host_sender_text = invite.substr(second_slash + 1U,
        third_slash - second_slash - 1U);
    const std::string_view host_public_text = invite.substr(
        third_slash + 1U, fourth_slash - third_slash - 1U);
    const std::string_view capability_text = invite.substr(
        fourth_slash + 1U, fifth_slash - fourth_slash - 1U);
    const std::string_view expiry_text = invite.substr(fifth_slash + 1U);
    unsigned int port = 0U;
    std::uint64_t expiry = 0U;
    const auto port_result = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    const auto match_result = std::from_chars(match_text.data(), match_text.data() + match_text.size(),
                                               match_id, 16);
    const auto host_sender_result = std::from_chars(
        host_sender_text.data(), host_sender_text.data() + host_sender_text.size(),
        host_sender_id, 16);
    const auto expiry_result = std::from_chars(
        expiry_text.data(), expiry_text.data() + expiry_text.size(), expiry);
    if (host.empty() || port == 0U || port > 65535U ||
        port_result.ec != std::errc{} || port_result.ptr != port_text.data() + port_text.size() ||
        match_result.ec != std::errc{} || match_result.ptr != match_text.data() + match_text.size() ||
        host_sender_result.ec != std::errc{} ||
        host_sender_result.ptr != host_sender_text.data() + host_sender_text.size() ||
        expiry_result.ec != std::errc{} ||
        expiry_result.ptr != expiry_text.data() + expiry_text.size() ||
        expiry < unix_seconds() || expiry > unix_seconds() + 86400U ||
        !secure::decode_key(host_public_text, host_public) ||
        !secure::decode_key(capability_text, invitation_capability)) {
        error = "The encrypted invite contains an invalid host, port, match or key.";
        return false;
    }
    return DatagramSocket::resolve(host, static_cast<std::uint16_t>(port), address, error);
}

std::string DirectSession::make_invite(std::string_view advertised_host) const {
    const std::string_view method = method_ == ConnectionMethod::VirtualLan
        ? "virtual" : method_ == ConnectionMethod::DirectInternet ? "direct" : "lan";
    std::string host(advertised_host);
    if (host.find(':') != std::string::npos && !(host.starts_with('[') && host.ends_with(']'))) {
        host = "[" + host + "]";
    }
    return "dkr-r://v7/" + std::string(method) + "/" +
           host + ":" +
           std::to_string(transport_->local_port()) + "/" + hex64(match_id_) + "/" +
           hex64(sender_id_) + "/" + secure::encode_key(host_key_pair_.public_key) +
           "/" + secure::encode_key(invitation_capability_) + "/" +
           std::to_string(unix_seconds() + 21600U);
}

bool DirectSession::send_to(const PeerAddress& address,
                            protocol::MessageType type,
                            std::span<const std::uint8_t> payload,
                            std::uint32_t frame) {
    if (!is_host_) return send_with_key(address, key_, type, payload, frame);
    PeerRecord* peer = peer_by_address(address);
    if (peer == nullptr) {
        status_ = "Refused to send to an unapproved racer.";
        return false;
    }
    return send_with_key(address, peer->key, type, payload, frame);
}

bool DirectSession::send_with_key(const PeerAddress& address,
                                  const secure::Key& key,
                                  protocol::MessageType type,
                                  std::span<const std::uint8_t> payload,
                                  std::uint32_t frame) {
    // Coalesce only identical, still-unsent idempotent requests. Once a packet
    // leaves the application queue a retry must get a fresh authenticated
    // sequence/nonce and remain eligible for transmission.
    const bool coalescible = type == protocol::MessageType::StateSnapshot ||
        type == protocol::MessageType::StateRequest ||
        type == protocol::MessageType::StateAcknowledge ||
        type == protocol::MessageType::GameplayBarrier ||
        type == protocol::MessageType::TransitionBarrier ||
        type == protocol::MessageType::RecoveryBegin ||
        type == protocol::MessageType::RecoveryAck ||
        type == protocol::MessageType::RecoveryResume ||
        type == protocol::MessageType::GameplayHandoff ||
        type == protocol::MessageType::InputRepairRequest ||
        type == protocol::MessageType::Start ||
        type == protocol::MessageType::ReadyRequest ||
        type == protocol::MessageType::ReadyAck ||
        type == protocol::MessageType::CountdownAck ||
        type == protocol::MessageType::LaunchPrepare ||
        type == protocol::MessageType::LaunchPrepareAck ||
        type == protocol::MessageType::LaunchCommit ||
        type == protocol::MessageType::LaunchCommitAck ||
        type == protocol::MessageType::LaunchRelease ||
        type == protocol::MessageType::LaunchReleaseAck ||
        type == protocol::MessageType::Disconnect ||
        type == protocol::MessageType::ExperimentalMatchEnd ||
        type == protocol::MessageType::ExperimentalCheckpoint || type == protocol::MessageType::ExperimentalLoaded ||
        type == protocol::MessageType::ExperimentalSaveOffer || type == protocol::MessageType::ExperimentalSaveRequest ||
        type == protocol::MessageType::ExperimentalControl ||
        type == protocol::MessageType::ModOffer ||
        type == protocol::MessageType::ModControl ||
        type == protocol::MessageType::ModData ||
        type == protocol::MessageType::FrameCommitRequest;
    if (coalescible) {
        for (const auto* queue : {&critical_outbound_, &repair_outbound_, &authority_outbound_, &normal_priority_outbound_, &bulk_outbound_}) {
            if (std::any_of(queue->begin(), queue->end(), [&](const auto& queued) {
                    return queued.type == type && queued.destination == address &&
                        queued.frame == frame && queued.retry_identity.size() == payload.size() &&
                        std::equal(queued.retry_identity.begin(), queued.retry_identity.end(), payload.begin());
                })) {
                ++duplicate_retries_coalesced_;
                return true;
            }
        }
    }
    std::uint64_t& sequence = outbound_sequence(address, key);
    protocol::Datagram datagram{{type, match_id_, sequence, frame},
                                std::vector<std::uint8_t>(payload.begin(), payload.end())};
    const auto plain = protocol::encode(
        datagram, transport_->maximum_plaintext_datagram_bytes());
    if (plain.empty()) return false;
    const std::uint64_t packet_sequence = sequence++;
    const auto encrypted = secure::seal(plain, key, sender_id_,
                                        packet_sequence, match_id_);
    return enqueue_outbound(address, type, std::move(encrypted), frame,
        coalescible ? std::vector<std::uint8_t>(payload.begin(), payload.end()) : std::vector<std::uint8_t>{});
}

bool DirectSession::enqueue_outbound(PeerAddress destination,
                                     protocol::MessageType type,
                                      std::vector<std::uint8_t> bytes,
                                      std::uint32_t frame,
                                      std::vector<std::uint8_t> retry_identity) {
    constexpr std::size_t maximum_critical = 512U;
    constexpr std::size_t maximum_repair = 2048U;
    constexpr std::size_t maximum_high = 2048U;
    // One encrypted ledger packet is queued per active guest. Preserve the
    // full retained recovery horizon for every possible remote racer rather
    // than dividing it implicitly by player count during a transport stall.
    constexpr std::size_t maximum_commits =
        kCommitHistoryCapacity * (kMaximumPlayers - 1U);
    constexpr std::size_t maximum_authority = 512U;
    constexpr std::size_t maximum_normal = 256U;
    constexpr std::size_t maximum_bulk = 96U;
    OutboundPacket packet{destination, type, frame, std::move(bytes), std::move(retry_identity)};
    const bool critical = type == protocol::MessageType::Disconnect ||
        type == protocol::MessageType::ExperimentalMatchEnd ||
        type == protocol::MessageType::ExperimentalLoaded ||
        type == protocol::MessageType::ExperimentalSaveOffer || type == protocol::MessageType::ExperimentalSaveRequest ||
        type == protocol::MessageType::Start ||
        type == protocol::MessageType::ReadyRequest ||
        type == protocol::MessageType::ReadyAck ||
        type == protocol::MessageType::CountdownAck ||
        type == protocol::MessageType::LaunchPrepare ||
        type == protocol::MessageType::LaunchPrepareAck ||
        type == protocol::MessageType::LaunchCommit ||
        type == protocol::MessageType::LaunchCommitAck ||
        type == protocol::MessageType::LaunchRelease ||
        type == protocol::MessageType::LaunchReleaseAck ||
        type == protocol::MessageType::LaunchCancel ||
        type == protocol::MessageType::PreflightBegin ||
        type == protocol::MessageType::PreflightResult ||
        type == protocol::MessageType::OnlineSaveReady ||
        type == protocol::MessageType::OnlineSaveReadyAck ||
        type == protocol::MessageType::RecoveryBegin ||
        type == protocol::MessageType::RecoveryAck ||
        type == protocol::MessageType::RecoveryResume ||
        type == protocol::MessageType::TransitionBarrier ||
        type == protocol::MessageType::GameplayBarrier ||
        type == protocol::MessageType::GameplayHandoff ||
        type == protocol::MessageType::RollbackRecoveryRequest ||
        type == protocol::MessageType::InputRepairRequest ||
        type == protocol::MessageType::FrameCorrectionAck ||
        type == protocol::MessageType::FrameCommitRequest ||
        type == protocol::MessageType::StateRequest ||
        type == protocol::MessageType::StateAcknowledge ||
        type == protocol::MessageType::ExperimentalControl;
    const bool reliable_repair =
        type == protocol::MessageType::InputRepair ||
        type == protocol::MessageType::ExperimentalRepair;
    const bool high_priority = type == protocol::MessageType::Input ||
        type == protocol::MessageType::InputAck ||
        type == protocol::MessageType::RollbackData ||
        type == protocol::MessageType::ExperimentalInput ||
        type == protocol::MessageType::SimulationProgress ||
        type == protocol::MessageType::RacerOrientation ||
        type == protocol::MessageType::PreflightRealtimeProbe;
    const bool authoritative_commit =
        type == protocol::MessageType::FrameCommit;
    if (critical) {
        // Leave one terminal slot per remote racer. An overflow must still
        // be able to notify the other instances, without evicting unique work.
        const auto critical_limit = type == protocol::MessageType::Disconnect
            ? maximum_critical + kMaximumPlayers - 1U : maximum_critical;
        if (critical_outbound_.size() >= critical_limit) {
            ++outbound_packets_dropped_;
            ++transport_queue_failures_;
            failure_recorder().record(FailureEventKind::QueueOverflow,
                                      next_commit_frame_,
                                      static_cast<std::uint32_t>(
                                          critical_outbound_.size()),
                                      static_cast<std::uint32_t>(type),
                                      "critical outbound");
            fail_locked("The reliable multiplayer control queue overflowed.");
            return false;
        }
        critical_outbound_.push_back(std::move(packet));
    } else if (reliable_repair) {
        // InputRepair batches are cumulative from Player 1's oldest
        // contiguous hole.  While a reliable WebRTC channel is flow
        // controlled, a newer batch for the same peer therefore supersedes
        // every unsent older batch: it either begins at the same hole and
        // contains newer samples, or begins later because Player 1 has
        // already acknowledged the earlier frames.  Retaining every retry
        // used to turn a recoverable long Wi-Fi/VPN pause into a queue
        // overflow and a forced session halt.
        std::erase_if(repair_outbound_, [&](const OutboundPacket& queued) {
            return queued.type == protocol::MessageType::InputRepair &&
                   queued.destination == destination;
        });
        if (repair_outbound_.size() >= maximum_repair) {
            ++outbound_packets_dropped_;
            ++transport_queue_failures_;
            failure_recorder().record(
                FailureEventKind::QueueOverflow, next_commit_frame_,
                static_cast<std::uint32_t>(repair_outbound_.size()),
                static_cast<std::uint32_t>(type),
                "reliable input-repair outbound");
            fail_locked(
                "The reliable authored-input repair queue overflowed.");
            return false;
        }
        repair_outbound_.push_back(std::move(packet));
    } else if (authoritative_commit) {
        if (commit_outbound_.size() >= maximum_commits) {
            ++outbound_packets_dropped_;
            ++transport_queue_failures_;
            failure_recorder().record(
                FailureEventKind::QueueOverflow, next_commit_frame_,
                static_cast<std::uint32_t>(commit_outbound_.size()),
                static_cast<std::uint32_t>(type),
                "authoritative commit outbound");
            fail_locked(
                "The Player 1 frame ledger exceeded its bounded recovery queue.");
            return false;
        }
        commit_outbound_.push_back(std::move(packet));
    } else if (high_priority) {
        if (type == protocol::MessageType::Input ||
            type == protocol::MessageType::InputAck ||
            type == protocol::MessageType::SimulationProgress ||
            type == protocol::MessageType::RacerOrientation) {
            // Live input is rolling redundancy, not the repair ledger. Exact
            // older samples remain in local_history_ for reliable repair.
            // Epoch changes purge this lane; within it a newer batch wins.
            // These are advisory/latest-wins frame samples, not ledgers. A
            // newer unsent value fully supersedes an older one for the same
            // peer, keeping a congested route from accumulating stale pacing
            // or orientation reports that arrive after they can be useful.
            const auto before = high_priority_outbound_.size();
            std::erase_if(
                high_priority_outbound_,
                [&](const OutboundPacket& queued) {
                    return queued.type == type &&
                           queued.destination == destination;
                });
            duplicate_retries_coalesced_ += before - high_priority_outbound_.size();
        }
        if (high_priority_outbound_.size() >= maximum_high) {
            ++outbound_packets_dropped_;
            ++transport_queue_failures_;
            failure_recorder().record(FailureEventKind::QueueOverflow,
                                      next_commit_frame_,
                                      static_cast<std::uint32_t>(
                                          high_priority_outbound_.size()),
                                      static_cast<std::uint32_t>(type),
                                      "simulation outbound");
            fail_locked(
                "The authored input/rollback queue overflowed; the session "
                "stopped before silently dropping simulation data.");
            return false;
        }
        high_priority_outbound_.push_back(std::move(packet));
    } else if (type == protocol::MessageType::LiveReplicaSnapshot) {
        // A live replica is meaningful only for its exact simulation frame.
        // Keep every fragment of the newest frame, but evict queued fragments
        // for older frames to prevent a slow VPN route from accumulating
        // seconds of stale authoritative state.
        const std::size_t before = authority_outbound_.size();
        std::erase_if(authority_outbound_, [&](const OutboundPacket& queued) {
            return queued.type == protocol::MessageType::LiveReplicaSnapshot &&
                   queued.destination == destination && queued.frame != frame;
        });
        outbound_packets_dropped_ += before - authority_outbound_.size();
        if (authority_outbound_.size() >= maximum_authority) {
            const auto stale = std::find_if(
                authority_outbound_.begin(), authority_outbound_.end(),
                [](const OutboundPacket& queued) {
                    return queued.type ==
                           protocol::MessageType::LiveReplicaSnapshot;
                });
            if (stale != authority_outbound_.end()) {
                authority_outbound_.erase(stale);
                ++outbound_packets_dropped_;
            } else {
                ++transport_queue_failures_;
                fail_locked("The reliable Player 1 checkpoint queue overflowed.");
                return false;
            }
        }
        authority_outbound_.push_back(std::move(packet));
    } else if(type==protocol::MessageType::ExperimentalCheckpoint || type==protocol::MessageType::ModData) {
        if(bulk_outbound_.size()>=maximum_bulk)return false;
        bulk_outbound_.push_back(std::move(packet));
    } else if (type == protocol::MessageType::StateSnapshot) {
        if (authority_outbound_.size() >= maximum_authority) {
            ++outbound_packets_dropped_;
            ++transport_queue_failures_;
            failure_recorder().record(FailureEventKind::QueueOverflow,
                                      next_commit_frame_,
                                      static_cast<std::uint32_t>(
                                          authority_outbound_.size()),
                                      static_cast<std::uint32_t>(type),
                                      "authority outbound");
            fail_locked("The reliable authority checkpoint queue overflowed.");
            return false;
        }
        authority_outbound_.push_back(std::move(packet));
    } else {
        if (type == protocol::MessageType::OnlineSaveStatus) {
            std::erase_if(normal_priority_outbound_, [&](const OutboundPacket& queued) {
                return queued.type == type && queued.destination == destination;
            });
        }
        if (normal_priority_outbound_.size() >= maximum_normal) {
            normal_priority_outbound_.pop_front();
            ++outbound_packets_dropped_;
        }
        normal_priority_outbound_.push_back(std::move(packet));
    }
    const std::size_t queued = critical_outbound_.size() +
        repair_outbound_.size() + high_priority_outbound_.size() +
        commit_outbound_.size() +
        authority_outbound_.size() +
        normal_priority_outbound_.size() + bulk_outbound_.size();
    outbound_queue_high_water_ = (std::max)(outbound_queue_high_water_, queued);
    worker_wake_ = true;
    state_changed_.notify_all();
    return true;
}

void DirectSession::flush_outbound_locked() {
    // Reliable lifecycle/recovery control leaves first, followed by a lane
    // containing only authored input/rollback traffic. State hashes and lobby
    // telemetry are deliberately normal priority: a diagnostic burst must
    // never evict the packets that advance racer simulation. Player 1
    // checkpoints retain their own non-evicting lane behind real-time input.
    struct BlockedRoute {
        PeerAddress destination{};
        TransportTrafficClass traffic = TransportTrafficClass::Control;
    };
    std::vector<BlockedRoute> blocked_routes;
    blocked_routes.reserve(kMaximumPlayers * 5U);
    struct RouteBudget { PeerAddress destination; std::size_t bytes; };
    std::vector<RouteBudget> route_budgets;
    route_budgets.reserve(kMaximumPlayers);
    const auto flush_started = std::chrono::steady_clock::now();
    const auto traffic_class = [](protocol::MessageType type) {
        return
            (type == protocol::MessageType::StateSnapshot ||
             type == protocol::MessageType::ExperimentalCheckpoint ||
             type == protocol::MessageType::ModData ||
             type == protocol::MessageType::PreflightCheckpointProbe)
                ? TransportTrafficClass::Checkpoint
                :
            type == protocol::MessageType::LiveReplicaSnapshot
                ? TransportTrafficClass::Replica
                : type == protocol::MessageType::PreflightReplicaProbe
                    ? TransportTrafficClass::Replica
                : (type == protocol::MessageType::FrameCommit ||
                   type == protocol::MessageType::InputRepair ||
                   type == protocol::MessageType::ExperimentalRepair ||
                   type == protocol::MessageType::PreflightAuthorityProbe)
                    ? TransportTrafficClass::Authoritative
                : (type == protocol::MessageType::Input ||
                   type == protocol::MessageType::InputAck ||
                   type == protocol::MessageType::RollbackData ||
                   type == protocol::MessageType::ExperimentalInput ||
                   type == protocol::MessageType::SimulationProgress ||
                   type == protocol::MessageType::RacerOrientation ||
                   type == protocol::MessageType::PreflightRealtimeProbe)
                    ? TransportTrafficClass::Realtime
                    : TransportTrafficClass::Control;
    };
    const auto route_blocked = [&](const PeerAddress& destination,
                                   TransportTrafficClass traffic) {
        return std::any_of(
            blocked_routes.begin(), blocked_routes.end(),
            [&](const BlockedRoute& route) {
                return route.destination == destination &&
                       route.traffic == traffic;
            });
    };
    const auto flush_queue = [&](std::deque<OutboundPacket>& queue,
                                 std::size_t budget, std::size_t byte_budget) {
        // Examine only packets present at entry. Rotated blocked packets retain
        // per-route order and are retried by the worker's next flush, while a
        // congested peer/channel cannot prevent healthy routes from draining.
        const std::size_t attempts = (std::min)(budget, queue.size());
        std::size_t sent_bytes = 0;
        for (std::size_t attempt = 0U; attempt < attempts; ++attempt) {
            // Bound the shared session-lock hold. Remaining reliable data is
            // retained for the next service pass, not dropped or re-encoded.
            if (std::chrono::steady_clock::now() - flush_started >= std::chrono::milliseconds(2)) break;
            OutboundPacket packet = std::move(queue.front());
            queue.pop_front();
            // Only redundant disposable traffic expires. Ledger, repair,
            // save and lifecycle packets are never discarded on age.
            if ((packet.type == protocol::MessageType::Input ||
                 packet.type == protocol::MessageType::InputAck ||
                 packet.type == protocol::MessageType::SimulationProgress ||
                 packet.type == protocol::MessageType::RacerOrientation) &&
                std::chrono::steady_clock::now() - packet.enqueued >
                    std::chrono::milliseconds(250)) {
                ++outbound_packets_dropped_;
                continue;
            }
            const TransportTrafficClass traffic = traffic_class(packet.type);
            if (route_blocked(packet.destination, traffic)) {
                queue.push_back(std::move(packet));
                continue;
            }
            auto route = std::find_if(route_budgets.begin(), route_budgets.end(),
                [&](const RouteBudget& candidate) { return candidate.destination == packet.destination; });
            if (route == route_budgets.end()) {
                route_budgets.push_back({packet.destination, 0});
                route = std::prev(route_budgets.end());
            }
            if (sent_bytes + packet.bytes.size() > byte_budget ||
                route->bytes + packet.bytes.size() > (64U << 10U)) {
                blocked_routes.push_back({packet.destination, traffic});
                queue.push_back(std::move(packet));
                continue;
            }
            std::string error;
            const DatagramSendStatus result = transport_->send_status(
                packet.destination, packet.bytes, traffic, error);
            if (result == DatagramSendStatus::WouldBlock) {
                blocked_routes.push_back({packet.destination, traffic});
                queue.push_back(std::move(packet));
                worker_wake_ = true;
                continue;
            }
            if (result == DatagramSendStatus::Error) {
                status_ = error;
                ++transport_queue_failures_;
                // A socket/ICE route can fail between readiness and send.
                // Retain the authenticated packet exactly as WouldBlock does;
                // dropping a lifecycle barrier or frame-ledger packet here
                // makes a short network transition an unrecoverable desync.
                blocked_routes.push_back({packet.destination, traffic});
                queue.push_back(std::move(packet));
                worker_wake_ = true;
                continue;
            }
            ++packets_sent_;
            bytes_sent_ += packet.bytes.size();
            sent_bytes += packet.bytes.size();
            route->bytes += packet.bytes.size();
        }
    };

    flush_queue(critical_outbound_, 16U, 16U << 10U);
    flush_queue(high_priority_outbound_, 32U, 16U << 10U);
    flush_queue(repair_outbound_, 32U, 16U << 10U);
    flush_queue(commit_outbound_, 64U, 24U << 10U);
    flush_queue(authority_outbound_, 8U, 32U << 10U);
    flush_queue(normal_priority_outbound_, 8U, 16U << 10U);
    flush_queue(bulk_outbound_, 8U, 64U << 10U);
}

std::uint64_t& DirectSession::outbound_sequence(
    const PeerAddress& address, const secure::Key& key) {
    if (!is_host_) return next_sequence_;
    for (PeerRecord& peer : peers_) {
        if (peer.active && peer.address == address && peer.key == key) {
            return peer.next_sequence;
        }
    }
    for (PendingRecord& pending : pending_joins_) {
        if (pending.active && pending.address == address && pending.key == key) {
            return pending.next_sequence;
        }
    }
    // Rejections sent before a pending record is allocated use the host's
    // control sequence. Their derived key is invitation-scoped and never
    // becomes an approved gameplay channel.
    return next_sequence_;
}

bool DirectSession::send_join_request() {
    const protocol::HelloPayload hello{local_name_, manifest_,
                                       client_friend_admission_};
    const auto payload = protocol::encode_hello(hello);
    secure::Key peer_key{};
    const auto encrypted = secure::seal_join_request(
        payload, client_key_pair_.secret, client_key_pair_.public_key,
        host_public_, invitation_capability_, sender_id_, match_id_, peer_key);
    if (encrypted.empty()) return false;
    key_ = peer_key;
    return enqueue_outbound(host_address_, protocol::MessageType::Hello,
                            std::move(encrypted));
}

void DirectSession::broadcast(protocol::MessageType type,
                              std::span<const std::uint8_t> payload,
                              std::uint32_t frame) {
    for (const PeerRecord& peer : peers_) {
        if (peer.active) send_with_key(peer.address, peer.key, type, payload, frame);
    }
}

void DirectSession::pump_locked() {
    PeerAddress departed{};
    while(transport_->take_peer_departure(departed)) {
        if(is_host_)for(auto& pending:pending_joins_)if(pending.active && pending.mod_route_retained && pending.address==departed) {
            clear_pending_mod_route_locked(pending);pending={};
        }
        // Only the experimental backend changes departure policy. Use the
        // authenticated admitted roster, never an unapproved incoming route.
        const bool owned_live=launch_descriptor_&&
            launch_descriptor_->synchronization==SynchronizationMode::ExperimentalRollback&&
            (state_==ConnectionState::Loading||state_==ConnectionState::Running);
        if(!owned_live&&(!owned_match_end_||owned_match_end_->resumed))continue;
        if(is_host_) {
            for(const auto& peer:peers_)if(peer.active&&peer.address==departed) {
                const auto name=room_view_.players[peer.slot].display_name;
                std::fprintf(stderr,"[rollback][match-end] admitted client transport closed slot=%u\n",unsigned(peer.slot));
                remove_owned_peer_locked(peer.sender_id,name+" disconnected. The match has ended.");break;
            }
        } else if(departed==host_address_) {
            std::fprintf(stderr,"[rollback][match-end] admitted host transport closed\n");
            fail_locked("The host disconnected. The lobby has closed.");return;
        }
    }
    transport_->service();
    if(is_host_)for(auto& peer:peers_)if(peer.active&&!peer.online_save_ready)offer_online_save_locked(peer);
    if (pending_invitation_) {
        const auto rekey = transport_->rekey_status();
        if (rekey == QuickJoinRekeyStatus::Committed) {
            invitation_capability_ = pending_invitation_->capability;
            host_key_pair_ = pending_invitation_->key_pair;
            invite_ = transport_->quick_join_code();
            clear_friend_admissions_locked();
            for(auto& pending:pending_joins_)clear_pending_mod_route_locked(pending);
            pending_joins_ = {};
            pending_invitation_.reset();
            status_ = "Quick Join code replaced. Existing racers remain connected.";
        } else if (rekey == QuickJoinRekeyStatus::Failed) {
            pending_invitation_.reset();
            status_ = "Replacement registration failed. The previous code and lobby are unchanged.";
        }
    }
    if (quick_join_bootstrap_pending_) {
        std::string bootstrap_error;
        if (!activate_quick_join_locked(bootstrap_error) &&
            !bootstrap_error.empty()) {
            fail_locked(std::move(bootstrap_error));
            return;
        }
    }
    flush_outbound_locked();
    PeerAddress source{};
    std::vector<std::uint8_t> encrypted;
    std::string error;
    const auto receive_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(1);
    for (int packet_count = 0; packet_count < 64; ++packet_count) {
        if (packet_count > 0 &&
            std::chrono::steady_clock::now() >= receive_deadline) break;
        if (!transport_->receive(source, encrypted, error)) {
            if (!error.empty()) {
                fail_locked(std::move(error));
                return;
            }
            break;
        }
        ++packets_received_;
        bytes_received_ += encrypted.size();
        if (is_host_ && secure::is_join_request(encrypted)) {
            ++join_packets_recognized_;
            std::uint64_t join_sender = 0U;
            std::uint64_t join_match = 0U;
            if (!secure::inspect_join_request(encrypted, join_sender, join_match) ||
                join_match != match_id_) {
                ++join_packets_stale_;
                status_ =
                    "A racer used an expired lobby invitation. Copy the current invitation again.";
                continue;
            }
            handle_join_request(source, encrypted);
            continue;
        }
        std::uint64_t packet_sender = 0U;
        std::uint64_t packet_match = 0U;
        if (!secure::inspect_packet(encrypted, packet_sender, packet_match) ||
            packet_match != match_id_) continue;
        const secure::Key* receive_key = &key_;
        bool provisional_mod_peer=false;
        if (is_host_) {
            PeerRecord* peer = peer_by_sender(packet_sender);
            if (peer && peer->address==source) receive_key = &peer->key;
            else {
                auto* pending=pending_by_sender(packet_sender);
                if(!pending || !pending->mods_approved || !(pending->address==source))continue;
                receive_key=&pending->key;provisional_mod_peer=true;
            }
        } else if (packet_sender != host_sender_id_) {
            continue;
        }
        std::uint64_t opened_sender = 0U;
        std::uint64_t packet_sequence = 0U;
        std::vector<std::uint8_t> plain;
        if (!secure::open(encrypted, *receive_key, match_id_, opened_sender,
                          packet_sequence, plain) || opened_sender != packet_sender) continue;
        protocol::Datagram packet{};
        if (!protocol::decode(
                plain, packet, error,
                transport_->maximum_plaintext_datagram_bytes()) ||
            packet.header.sequence != packet_sequence) continue;
        // Provisional keys have mod-transfer privileges only. A guessed Ready,
        // input, save or launch packet must never allocate a racer slot.
        if(provisional_mod_peer && packet.header.type!=protocol::MessageType::ModControl &&
           packet.header.type!=protocol::MessageType::ModData)continue;
        // A fragmented authority snapshot must not age a delayed rollback
        // input out of the replay window. Keep authenticated sequence windows
        // per traffic lane while retaining one globally unique encryption
        // sequence/nonce on the sender.
        auto& receive_windows = received_sequences_[packet_sender];
        if (!receive_windows[receive_sequence_lane(packet.header.type)]
                 .accept(packet_sequence)) {
            continue;
        }
        handle_packet(source, packet_sender, packet);
    }
    flush_outbound_locked();
    state_changed_.notify_all();
    const auto now = std::chrono::steady_clock::now();
    if(is_host_)for(auto& pending:pending_joins_)if(pending.active && pending.mods_approved &&
        !pending.mods_verified && now-pending.last_acknowledgement>=std::chrono::seconds(1))send_mod_offer_locked(pending);
    constexpr auto control_retry = std::chrono::milliseconds(250);
    service_owned_match_end_locked(now);
    // The outer runtime can apply lead backpressure before requesting input,
    // so its pending-input timer is not necessarily armed. Expire genuine
    // lack of native peer progress here on the independent network worker.
    // Loading/recovery/finish have their own staged deadlines.
    if (is_host_ && state_ == ConnectionState::Running &&
        host_backpressure_active_ && !gameplay_handoff_ &&
        (!gameplay_barrier_ || gameplay_barrier_->go_released) &&
        !recovery_frame_ && !transition_barrier_ &&
        authority_lifecycle_ != AuthorityLifecycle::SealingFinish) {
        for (const auto& peer : peers_) {
            if (!peer.active || !simulation_progress_present_[peer.slot] ||
                next_commit_frame_ <= simulation_completed_frame_[peer.slot] + 1U) continue;
            if (now - simulation_progress_time_[peer.slot] >= std::chrono::seconds(90)) {
                fail_locked("Player " + std::to_string(peer.slot + 1U) +
                    " stopped making simulation progress for 90 seconds. The online session ended safely; your separate saves are retained.");
                return;
            }
        }
    }
    service_connection_test_locked(now);
    if (is_host_ && launch_countdown_active_) {
        std::string countdown_error;
        // A room owner remains in Hosting while its admitted guests report
        // Lobby. Both represent the same waiting-grid phase; rejecting
        // Hosting here cancelled every valid host countdown on its first
        // worker pump.
        const bool waiting_at_grid = state_ == ConnectionState::Hosting ||
                                     state_ == ConnectionState::Lobby;
        if (!waiting_at_grid ||
            !lobby_.can_start(&countdown_error)) {
            cancel_launch_locked(countdown_error.empty()
                ? "The online start countdown was cancelled."
                : "The online start countdown was cancelled: " + countdown_error);
        } else if (now >= launch_countdown_deadline_) {
            // Return to the proven launch path: publish the immutable start
            // descriptor immediately after the countdown, then let the
            // existing cold-boot checkpoint hold simulation frame zero until
            // every racer has loaded the same state.
            if (!prepare_launch_locked(countdown_error)) {
                cancel_launch_locked(countdown_error.empty()
                    ? "The synchronized launch could not be prepared."
                    : countdown_error);
            }
        }
    }
    if (!is_host_ && state_ == ConnectionState::Lobby &&
        launch_countdown_active_ && host_address_ &&
        (last_countdown_ack_send_.time_since_epoch().count() == 0 ||
         now - last_countdown_ack_send_ >= control_retry)) {
        send_to(host_address_, protocol::MessageType::CountdownAck,
                protocol::encode_countdown_ack(
                    {launch_countdown_generation_, local_slot_}));
        last_countdown_ack_send_ = now;
    }
    if (is_host_ && pending_launch_prepare_) {
        std::string launch_error;
        const bool waiting_at_grid = state_ == ConnectionState::Hosting ||
                                     state_ == ConnectionState::Lobby;
        if (!waiting_at_grid || !lobby_.can_start(&launch_error) ||
            pending_launch_prepare_->lobby_generation !=
                lobby_.room().generation) {
            cancel_launch_locked(launch_error.empty()
                ? "The lobby changed while the synchronized start was being validated."
                : launch_error);
        } else if (all_active_players_acknowledged_locked(
                       launch_prepare_acks_)) {
            const auto prepared = *pending_launch_prepare_;
            if (!commit_launch_locked(prepared.start, prepared.launch_epoch,
                                      launch_error)) {
                cancel_launch_locked(launch_error.empty()
                    ? "The synchronized launch commit failed safely."
                    : launch_error);
            }
        } else if (now >= launch_prepare_deadline_) {
            cancel_launch_locked(
                "A racer did not validate Player 1's starting grid in time.");
        } else if (last_launch_prepare_send_.time_since_epoch().count() == 0 ||
                   now - last_launch_prepare_send_ >= control_retry) {
            broadcast(protocol::MessageType::LaunchPrepare,
                      protocol::encode_launch_prepare(
                          *pending_launch_prepare_));
            last_launch_prepare_send_ = now;
        }
    }
    if (is_host_ && launch_stage_ == LaunchStage::Committing &&
        committed_launch_epoch_ != 0U) {
        if (all_active_players_acknowledged_locked(launch_commit_acks_)) {
            std::string release_error;
            const std::uint32_t epoch = committed_launch_epoch_;
            if (!release_launch_locked(epoch, release_error)) {
                cancel_launch_locked(release_error.empty()
                    ? "The synchronized launch could not enter its release gate."
                    : release_error);
            } else {
                launch_release_acks_ = {};
                launch_release_acks_[local_slot_] = true;
                launch_release_rebroadcast_until_ =
                    now + launch_control_timeout_locked(
                        std::chrono::seconds(10),
                        std::chrono::seconds(20));
                broadcast(protocol::MessageType::LaunchRelease,
                          protocol::encode_launch_release({epoch}));
                last_launch_release_send_ = now;
                status_ = "Every racer armed the same launch; releasing the synchronized start.";
            }
        } else if (now >= launch_commit_deadline_) {
            cancel_launch_locked(
                "A racer did not arm the synchronized start in time. "
                "The lobby was preserved safely; check the connection and try again.");
        } else if (now < launch_commit_rebroadcast_until_ &&
                   (last_launch_commit_send_.time_since_epoch().count() == 0 ||
                    now - last_launch_commit_send_ >= control_retry)) {
            broadcast(protocol::MessageType::LaunchCommit,
                      protocol::encode_launch_commit({committed_launch_epoch_}));
            last_launch_commit_send_ = now;
        }
    }
    if (is_host_ && launch_stage_ == LaunchStage::Releasing &&
        committed_launch_epoch_ != 0U) {
        if (all_active_players_acknowledged_locked(launch_release_acks_)) {
            launch_stage_ = LaunchStage::Idle;
            launch_release_rebroadcast_until_ = {};
            status_ = "Every racer received the synchronized start release.";
        } else if (now < launch_release_rebroadcast_until_ &&
                   (last_launch_release_send_.time_since_epoch().count() == 0 ||
                    now - last_launch_release_send_ >= control_retry)) {
            broadcast(protocol::MessageType::LaunchRelease,
                      protocol::encode_launch_release({committed_launch_epoch_}));
            last_launch_release_send_ = now;
        } else if (launch_release_rebroadcast_until_.time_since_epoch().count() != 0 &&
                   now >= launch_release_rebroadcast_until_) {
            launch_stage_ = LaunchStage::Idle;
            status_ = "The game started, but a racer's final launch acknowledgement was not received.";
        }
    }
    if (is_host_ && frame_correction_transmission_ &&
        (frame_correction_transmission_->last_send.time_since_epoch().count() == 0 ||
         now - frame_correction_transmission_->last_send >=
             std::chrono::milliseconds(80))) {
        send_frame_correction_locked();
    }
    if (state_ == ConnectionState::Failed &&
        !failure_payload_.empty() && now < failure_broadcast_until_ &&
        now - last_failure_broadcast_ >= std::chrono::milliseconds(100)) {
        if (is_host_) {
            broadcast(protocol::MessageType::Disconnect, failure_payload_);
        } else if (host_address_) {
            send_to(host_address_, protocol::MessageType::Disconnect,
                    failure_payload_);
        }
        last_failure_broadcast_ = now;
    }
    if (!is_host_ && (state_ == ConnectionState::Connecting ||
                      state_ == ConnectionState::AwaitingApproval) &&
        !quick_join_bootstrap_pending_ &&
        now - last_connect_request_ >= control_retry) {
        send_join_request();
        last_connect_request_ = now;
    }
    if (!is_host_ && state_ == ConnectionState::Connecting &&
        connect_started_.time_since_epoch().count() != 0 &&
        now - connect_started_ >= std::chrono::seconds(20)) {
        fail_locked(method_ == ConnectionMethod::QuickJoin
            ? "Quick Join could not establish an approved peer route. Confirm the five-character code and verify that both racers can reach the signaling service. Some restrictive networks cannot establish a direct peer connection."
            : "The host did not answer the encrypted join request. Confirm the invitation address, allow DKR-R through the host firewall and verify that the host UDP port is reachable.");
        return;
    }
    if (!is_host_ && state_ == ConnectionState::AwaitingApproval &&
        save_jobs_.empty() &&
        last_admission_response_.time_since_epoch().count() != 0 &&
        now - last_admission_response_ >= std::chrono::seconds(20)) {
        fail_locked("The host stopped responding while approval was pending. Leave and retry when the host is reachable.");
        return;
    }
    if (!is_host_ && state_ == ConnectionState::Lobby &&
        local_online_save_ready_ && !local_online_save_acknowledged_ &&
        now - last_client_request_ >= control_retry) {
        send_to(host_address_, protocol::MessageType::OnlineSaveReady,
                protocol::encode_online_save_ready({
                    local_slot_, session_save_generation_,
                    manifest_.session_save_hash}));
        last_client_request_ = now;
    }
    if (!is_host_ && desired_ready_.has_value() &&
        pending_ready_request_id_ != 0U &&
        state_ == ConnectionState::Lobby &&
        now - last_client_request_ >= control_retry) {
        send_to(host_address_, protocol::MessageType::ReadyRequest,
                protocol::encode_ready_request({
                    pending_ready_request_id_, local_slot_, *desired_ready_}));
        last_client_request_ = now;
    }
    if (!is_host_ && local_loaded_ && state_ == ConnectionState::Loading &&
        (local_slot_ >= room_view_.players.size() ||
         !room_view_.players[local_slot_].loaded) &&
        now - last_client_request_ >= control_retry) {
        send_loaded_locked();
        last_client_request_ = now;
    }
    if (is_host_) expire_pending_joins();
    if (is_host_) send_connection_probes();
    if (state_ == ConnectionState::Running && gameplay_handoff_) {
        if (now >= gameplay_handoff_->deadline) {
            fail_locked(
                "A racer did not complete the synchronized track-loading handoff.");
            return;
        }
        if (last_gameplay_handoff_send_.time_since_epoch().count() == 0 ||
            now - last_gameplay_handoff_send_ >=
                std::chrono::milliseconds(100)) {
            if (is_host_) {
                send_gameplay_handoff_locked(
                    protocol::GameplayHandoffStage::Suspend);
            } else if (gameplay_handoff_->resume_input_epoch == 0U) {
                send_gameplay_handoff_locked(
                    protocol::GameplayHandoffStage::Request);
            }
        }
    }
    if (is_host_ && recovery_frame_ && !recovery_resumed_ &&
        next_commit_frame_ <= *recovery_frame_ + 1U &&
        (last_recovery_broadcast_.time_since_epoch().count() == 0 ||
         now - last_recovery_broadcast_ >= std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::RecoveryBegin,
                  protocol::encode_recovery(
                      {scene_epoch_, *recovery_frame_, 0U}),
                  *recovery_frame_);
        last_recovery_broadcast_ = now;
    }
    if (is_host_ && completed_recovery_frame_ &&
        now < recovery_resume_until_ &&
        (last_recovery_broadcast_.time_since_epoch().count() == 0 ||
         now - last_recovery_broadcast_ >= std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::RecoveryResume,
                  protocol::encode_recovery(
                      {completed_recovery_epoch_,
                       *completed_recovery_frame_, 0U}),
                  *completed_recovery_frame_);
        last_recovery_broadcast_ = now;
    }
    if (is_host_ && transition_barrier_ &&
        transition_barrier_->resumed && now < transition_resume_until_ &&
        (last_transition_broadcast_.time_since_epoch().count() == 0 ||
         now - last_transition_broadcast_ >= std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::TransitionBarrier,
                  protocol::encode_transition_barrier({
                      transition_barrier_->scene_epoch,
                      transition_barrier_->frame,
                      transition_barrier_->kind,
                      local_slot_,
                      protocol::TransitionBarrierStage::Resume}),
                  transition_barrier_->frame);
        last_transition_broadcast_ = now;
    }
    bool gameplay_arm_ack_pending = false;
    if (is_host_ && gameplay_barrier_ &&
        gameplay_barrier_->arm_announced &&
        !gameplay_barrier_->go_released) {
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied &&
                !gameplay_barrier_->armed[slot]) {
                gameplay_arm_ack_pending = true;
                break;
            }
        }
    }
    if (is_host_ && gameplay_barrier_ &&
        gameplay_barrier_->arm_announced &&
        !gameplay_barrier_->go_released && gameplay_arm_ack_pending &&
        (last_gameplay_barrier_broadcast_.time_since_epoch().count() == 0 ||
         now - last_gameplay_barrier_broadcast_ >=
              std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::GameplayBarrier,
                  protocol::encode_gameplay_barrier({
                      gameplay_barrier_->scene_epoch,
                      gameplay_barrier_->map,
                      gameplay_barrier_->racer_count,
                      local_slot_,
                      protocol::GameplayBarrierStage::Arm}));
        last_gameplay_barrier_broadcast_ = now;
    }
    if (is_host_ && gameplay_barrier_ && gameplay_barrier_->go_released &&
        now < gameplay_resume_until_ &&
        (last_gameplay_barrier_broadcast_.time_since_epoch().count() == 0 ||
         now - last_gameplay_barrier_broadcast_ >=
              std::chrono::milliseconds(100))) {
        broadcast(protocol::MessageType::GameplayBarrier,
                  protocol::encode_gameplay_barrier({
                      gameplay_barrier_->scene_epoch,
                      gameplay_barrier_->map,
                      gameplay_barrier_->racer_count,
                      local_slot_,
                      protocol::GameplayBarrierStage::Go}));
        last_gameplay_barrier_broadcast_ = now;
    }
    if (!is_host_ && gameplay_barrier_ &&
        gameplay_barrier_->arm_announced &&
        gameplay_barrier_->armed[local_slot_] &&
        !gameplay_barrier_->go_released &&
        // Continue asking until Go arrives or the runtime's handoff deadline
        // ends the transaction. A two-second packet blackout is recoverable.
        (last_gameplay_resume_ack_.time_since_epoch().count() == 0 ||
         now - last_gameplay_resume_ack_ >= std::chrono::milliseconds(100))) {
        send_to(host_address_, protocol::MessageType::GameplayBarrier,
                protocol::encode_gameplay_barrier({
                    gameplay_barrier_->scene_epoch,
                    gameplay_barrier_->map,
                    gameplay_barrier_->racer_count,
                    local_slot_,
                    protocol::GameplayBarrierStage::Armed}));
        last_gameplay_resume_ack_ = now;
    }
    // The delay is part of the immutable launch descriptor. A guest learns the
    // newest Player-1 frame and then sends future input back to Player 1. Since
    // a committed prediction is deliberately not rewound, both modes need a
    // deadline sized against that complete feedback journey. Freeze the worst
    // peer's measured result for the launch so all racers share one timeline.
    if (is_host_ && !launch_descriptor_ &&
        lobby_.room().rules.automatic_input_delay) {
        std::uint8_t calculated_delay = 2U;
        for (const PeerRecord& peer : peers_) {
            if (!peer.active) continue;
            std::array<double, 32U> ordered{};
            std::size_t fresh_count = 0;
            for (std::size_t i = 0; i < peer.rtt_sample_count; ++i)
                if (peer.rtt_sample_times[i].time_since_epoch().count() &&
                    now - peer.rtt_sample_times[i] <= std::chrono::seconds(20))
                    ordered[fresh_count++] = peer.rtt_samples[i];
            std::sort(ordered.begin(),
                      ordered.begin() +
                          static_cast<std::ptrdiff_t>(fresh_count));
            const double p99 = fresh_count == 0U ? peer.rtt_ms
                : ordered[(fresh_count * 99U + 99U) / 100U - 1U];
            const double p50 = fresh_count == 0U ? peer.rtt_ms
                : ordered[(fresh_count - 1U) / 2U];
            if (fresh_count < 4U && method_ != ConnectionMethod::Lan)
                calculated_delay = (std::max<std::uint8_t>)(calculated_delay, 3U);
            calculated_delay = (std::max)(
                calculated_delay,
                host_authoritative_input_delay_frames(
                    p99, (std::max)(peer.jitter_ms, p99 - p50), peer.loss_percent,
                    method_ == ConnectionMethod::Lan));
        }
        input_delay_ = calculated_delay;
    }
    if (is_host_ && state_ != ConnectionState::Offline &&
        state_ != ConnectionState::Failed &&
        now - last_lobby_broadcast_ >= control_retry) {
        if (state_ == ConnectionState::Loading) {
            if (launch_descriptor_) {
                protocol::StartPayload start{
                    0U, *launch_descriptor_,
                    launch_descriptor_hash(*launch_descriptor_)};
                broadcast(protocol::MessageType::Start,
                          protocol::encode_start(start));
            }
        }
        broadcast_lobby();
        last_lobby_broadcast_ = now;
    }
    if (is_host_ && state_ == ConnectionState::Loading && local_loaded_ &&
        !run_signal_sent_) {
        bool all_loaded = true;
        for (const Player& player : lobby_.room().players) {
            if (player.occupied && !player.loaded) all_loaded = false;
        }
        if (all_loaded) {
            std::uint64_t expected_hash = 0U;
            bool checkpoint_mismatch = false;
            for (std::size_t slot = 0U; slot < lobby_.room().players.size(); ++slot) {
                if (!lobby_.room().players[slot].occupied) continue;
                if (!bootstrap_hash_present_[slot]) {
                    checkpoint_mismatch = true;
                    break;
                }
                if (!runtime_save_present_[slot] ||
                    runtime_save_generations_[slot] !=
                        session_save_generation_ ||
                    runtime_save_hashes_[slot] !=
                        manifest_.session_save_hash) {
                    checkpoint_mismatch = true;
                    break;
                }
                if (expected_hash == 0U) expected_hash = bootstrap_hashes_[slot];
                else if (bootstrap_hashes_[slot] != expected_hash) {
                    checkpoint_mismatch = true;
                    break;
                }
            }
            if (checkpoint_mismatch || expected_hash == 0U) {
                fail_locked(
                    "The synchronized cold-boot or online-save checkpoint did not match. "
                    "The session was halted before simulation frame 0.");
                return;
            }
            std::string start_error;
            if (lobby_.begin_running(std::to_string(sender_id_), start_error)) {
                room_view_ = lobby_.room();
                state_ = ConnectionState::Running;
                run_signal_sent_ = true;
                protocol::StartPayload start{
                    1U, *launch_descriptor_,
                    launch_descriptor_hash(*launch_descriptor_)};
                broadcast(protocol::MessageType::Start,
                          protocol::encode_start(start));
                broadcast_lobby();
                status_ = "Every racer is synchronized. Go!";
            }
        }
    }
}

void DirectSession::handle_packet(const PeerAddress& source,
                                  std::uint64_t packet_sender,
                                  const protocol::Datagram& packet) {
    if(handle_mod_packet_locked(packet_sender,packet))return;
    if (is_host_) handle_host_packet(source, packet_sender, packet);
    else handle_client_packet(packet);
}

void DirectSession::handle_join_request(
    const PeerAddress& source, std::span<const std::uint8_t> encrypted) {
    std::uint64_t packet_sender = 0U;
    secure::Key client_public{};
    secure::Key peer_key{};
    std::vector<std::uint8_t> plain;
    if (!secure::open_join_request(encrypted, host_key_pair_.secret,
            invitation_capability_, match_id_, packet_sender, client_public,
            peer_key, plain)) {
        ++join_packets_auth_rejected_;
        status_ =
            "A racer used invitation keys that are no longer active. Copy the current invitation again.";
        return;
    }
    if (blocked_senders_.contains(packet_sender) ||
        blocked_sources_.contains(DatagramSocket::describe_host(source))) return;
    protocol::HelloPayload hello{};
    std::string error;
    if (!protocol::decode_hello(plain, hello, error)) {
        ++join_packets_manifest_rejected_;
        status_ = "An authenticated join request contained an invalid compatibility manifest.";
        return;
    }
    ++join_packets_accepted_;
    if (PeerRecord* existing = peer_by_sender(packet_sender)) {
        if (!(existing->address == source)) return;
        send_with_key(existing->address, existing->key,
            protocol::MessageType::HelloAck,
            protocol::encode_hello_ack({
                true, existing->slot,
                "Welcome back. Player 1 supplied the isolated online save.",
                session_save_, {}, session_save_generation_,
                manifest_.session_save_hash}));
        broadcast_lobby();
        return;
    }
    if (lobby_locked_ || (state_ != ConnectionState::Lobby && state_ != ConnectionState::Hosting)) {
        send_with_key(source, peer_key, protocol::MessageType::HelloAck,
            protocol::encode_hello_ack({false, 0U, "The host is not accepting new racers right now."}));
        return;
    }
    const std::string incompatibility = admission_incompatibility(
        hello.manifest);
    if (!incompatibility.empty()) {
        send_with_key(source, peer_key, protocol::MessageType::HelloAck,
            protocol::encode_hello_ack({false, 0U,
                incompatibility, {}, manifest_}));
        return;
    }
    PendingRecord* pending = pending_by_sender(packet_sender);
    if(pending && pending->mods_approved && (!(pending->address==source) ||
       pending->key!=peer_key || pending->client_public!=client_public))return;
    if (pending == nullptr) {
        const auto available = std::find_if(pending_joins_.begin(), pending_joins_.end(),
            [](const PendingRecord& record) { return !record.active; });
        if (available == pending_joins_.end()) {
            send_with_key(source, peer_key, protocol::MessageType::HelloAck,
                protocol::encode_hello_ack({false, 0U,
                    "The host already has too many pending join requests."}));
            return;
        }
        pending = &*available;
        pending->active = true;
        pending->sender_id = packet_sender;
        pending->first_seen = std::chrono::steady_clock::now();
    }
    pending->address = source;
    pending->key = peer_key;
    pending->client_public = client_public;
    pending->display_name = std::move(hello.display_name);
    pending->manifest = std::move(hello.manifest);
    pending->last_seen = std::chrono::steady_clock::now();
    if(pending->mods_approved) {
        send_mod_offer_locked(*pending);return;
    }
    if (consume_friend_admission_locked(hello.friend_admission)) {
        const PeerAddress pending_address = pending->address;
        const secure::Key pending_key = pending->key;
        std::string admission_error;
        if (!approve_join_locked(pending->sender_id, admission_error)) {
            send_with_key(pending_address, pending_key,
                protocol::MessageType::HelloAck,
                protocol::encode_hello_ack({false, 0U,
                    admission_error.empty()
                        ? "The invited place is no longer available."
                        : admission_error}));
            clear_pending_mod_route_locked(*pending);
            *pending = {};
            status_ = admission_error;
        }
        return;
    }
    send_with_key(source, peer_key, protocol::MessageType::JoinPending, {});
    status_ = pending->display_name + " is waiting for host approval.";
}

void DirectSession::handle_host_packet(const PeerAddress& source,
                                       std::uint64_t packet_sender,
                                       const protocol::Datagram& packet) {
    std::string error;
    PeerRecord* peer = peer_by_sender(packet_sender);
    if (peer == nullptr || !(peer->address == source)) return;
    peer->owned_last_packet = std::chrono::steady_clock::now();
    if (handle_owned_match_end_locked(packet_sender, packet)) return;
    if (receive_owned_packet_locked(peer->slot, packet)) return;
    const std::string peer_id = std::to_string(packet_sender);
    if(packet.header.type==protocol::MessageType::ExperimentalSaveRequest) {
        offer_online_save_locked(*peer);return;
    } else if (packet.header.type == protocol::MessageType::OnlineSaveReady) {
        protocol::OnlineSaveReadyPayload ready{};
        if (!protocol::decode_online_save_ready(packet.payload, ready, error) ||
            ready.player_slot != peer->slot) {
            return;
        }
        const bool matches = ready.generation == session_save_generation_ &&
                             ready.hash == manifest_.session_save_hash &&
                             save_sync_available();
        if (matches) {
            peer->online_save_ready = true;
            status_ = lobby_.room().players[peer->slot].display_name +
                      " verified the isolated online save.";
            send_to(peer->address, protocol::MessageType::OnlineSaveReadyAck,
                    protocol::encode_online_save_ready({
                        peer->slot, session_save_generation_,
                        manifest_.session_save_hash}));
        } else {
            // Never turn a stale ACK into a failure for the newer generation.
            // A genuine missing/current save is repaired while still in lobby.
            offer_online_save_locked(*peer);
        }
        state_changed_.notify_all();
    } else if (packet.header.type == protocol::MessageType::ReadyRequest) {
        if (owned_match_end_ && !owned_match_end_->resumed) return;
        protocol::ReadyRequestPayload request{};
        if (!protocol::decode_ready_request(packet.payload, request, error) ||
            request.player_slot != peer->slot || request.request_id == 0U) {
            return;
        }
        if (request.request_id < peer->last_ready_request_id) return;
        const bool is_new_request =
            request.request_id > peer->last_ready_request_id;
        const std::uint64_t previous_generation = lobby_.room().generation;
        if (request.ready && !peer->online_save_ready) {
            send_to(peer->address, protocol::MessageType::ReadyAck,
                    protocol::encode_ready_ack({
                        request.request_id, peer->slot, false,
                        room_view_.generation}));
            return;
        }
        if (is_new_request &&
            !lobby_.set_ready(peer_id, request.ready, error)) {
            return;
        }
        peer->last_ready_request_id = request.request_id;
        room_view_ = lobby_.room();
        send_to(peer->address, protocol::MessageType::ReadyAck,
                protocol::encode_ready_ack({
                    request.request_id, peer->slot,
                    room_view_.players[peer->slot].ready,
                    room_view_.generation}));
        if (room_view_.generation != previous_generation) {
            room_view_ = lobby_.room();
            broadcast_lobby();
        }
    } else if (packet.header.type == protocol::MessageType::CountdownAck) {
        protocol::CountdownAckPayload acknowledgement{};
        if (!protocol::decode_countdown_ack(packet.payload, acknowledgement,
                                            error) ||
            acknowledgement.player_slot != peer->slot ||
            acknowledgement.countdown_generation == 0U ||
            !launch_countdown_active_ ||
            acknowledgement.countdown_generation !=
                launch_countdown_generation_) {
            return;
        }
        countdown_acks_[peer->slot] = true;
        state_changed_.notify_all();
    } else if (packet.header.type ==
               protocol::MessageType::LaunchPrepareAck) {
        protocol::LaunchPrepareAckPayload acknowledgement{};
        if (!protocol::decode_launch_prepare_ack(packet.payload,
                                                 acknowledgement, error) ||
            acknowledgement.player_slot != peer->slot ||
            acknowledgement.launch_epoch == 0U ||
            !pending_launch_prepare_ ||
            acknowledgement.launch_epoch !=
                pending_launch_prepare_->launch_epoch) {
            return;
        }
        if (!acknowledgement.accepted) {
            cancel_launch_locked(
                "A racer rejected Player 1's synchronized starting grid. "
                "Confirm that every game, save and gameplay setting matches.");
            return;
        }
        launch_prepare_acks_[peer->slot] = true;
        state_changed_.notify_all();
    } else if (packet.header.type == protocol::MessageType::LaunchCommitAck) {
        protocol::LaunchCommitAckPayload acknowledgement{};
        if (!protocol::decode_launch_commit_ack(packet.payload,
                                                acknowledgement, error) ||
            acknowledgement.player_slot != peer->slot ||
            acknowledgement.launch_epoch != committed_launch_epoch_ ||
            launch_stage_ != LaunchStage::Committing) {
            return;
        }
        launch_commit_acks_[peer->slot] = true;
        state_changed_.notify_all();
    } else if (packet.header.type == protocol::MessageType::PreflightResult) {
        protocol::PreflightResultPayload result{};
        if (!protocol::decode_preflight_result(packet.payload, result, error) ||
            result.test_id != connection_test_id_ || !connection_test_results_[result.player_slot].valid) return;
        connection_test_result_acks_[peer->slot] |= static_cast<std::uint8_t>(1U << result.player_slot);
    } else if (packet.header.type == protocol::MessageType::LaunchReleaseAck) {
        protocol::LaunchReleaseAckPayload acknowledgement{};
        if (!protocol::decode_launch_release_ack(packet.payload,
                                                 acknowledgement, error) ||
            acknowledgement.player_slot != peer->slot ||
            acknowledgement.launch_epoch != committed_launch_epoch_ ||
            !launch_released_) {
            return;
        }
        launch_release_acks_[peer->slot] = true;
        state_changed_.notify_all();
    } else if (const auto lane = preflight_lane(packet.header.type);
               lane.has_value()) {
        protocol::PreflightProbePayload probe{};
        if (!protocol::decode_preflight_probe(packet.payload, probe, error) ||
            !connection_test_active_ ||
            probe.test_id != connection_test_id_ || !probe.echo) {
            return;
        }
        const std::uint64_t now_us = steady_microseconds();
        if (now_us < probe.sent_time_us) return;
        const double rtt_ms = static_cast<double>(now_us - probe.sent_time_us) /
                              1000.0;
        if (probe.sequence >= connection_test_next_sequence_[*lane] ||
            connection_test_echoes_[peer->slot][*lane].size() >= 2048U ||
            !connection_test_echoes_[peer->slot][*lane].insert(probe.sequence).second) return;
        ++connection_test_received_[peer->slot][*lane];
        connection_test_rtt_samples_[peer->slot][*lane].push_back(rtt_ms);
    } else if (packet.header.type == protocol::MessageType::Loaded || packet.header.type==protocol::MessageType::ExperimentalLoaded) {
        protocol::LoadedPayload loaded{};
        const bool experimental=lobby_.room().rules.synchronization==SynchronizationMode::ExperimentalRollback;
        if(experimental) {
            protocol::ExperimentalLoadedPayload proof;
            if(packet.header.type!=protocol::MessageType::ExperimentalLoaded||state_!=ConnectionState::Loading||
               !launch_descriptor_||(owned_match_end_&&!owned_match_end_->resumed)||
               !protocol::decode_experimental_loaded(packet.payload,proof)||
               proof.launch_hash!=launch_descriptor_hash(*launch_descriptor_))return;
            loaded=proof.loaded;
        }else if(packet.header.type!=protocol::MessageType::Loaded||!protocol::decode_loaded(packet.payload,loaded,error))return;
        if(loaded.player_slot!=peer->slot)return;
        if (!peer->online_save_ready ||
            loaded.online_save_generation != session_save_generation_ ||
            loaded.online_save_hash != manifest_.session_save_hash) {
            fail_locked(
                "A racer loaded an online save that does not match Player 1. "
                "The session was halted before simulation frame 0.");
            return;
        }
        bootstrap_hashes_[peer->slot] = loaded.bootstrap_hash;
        bootstrap_hash_present_[peer->slot] = true;
        runtime_save_hashes_[peer->slot] = loaded.online_save_hash;
        runtime_save_generations_[peer->slot] =
            loaded.online_save_generation;
        runtime_save_present_[peer->slot] = true;
        lobby_.set_loaded(peer_id, true);
        room_view_ = lobby_.room();
        broadcast_lobby();
    } else if (packet.header.type == protocol::MessageType::Input ||
               packet.header.type == protocol::MessageType::InputRepair) {
        protocol::InputBatch batch{};
        if (!protocol::decode_input_batch(packet.payload, batch, error) ||
            batch.player_slot != peer->slot) return;
        if (batch.epoch != authority_epoch()) {
            if (batch.epoch == retired_authority_epoch_) ++stale_epoch_packets_;
            else ++future_epoch_packets_;
            return;
        }
        const auto batch_newest = batch.first_frame +
            static_cast<std::uint32_t>(batch.inputs.size() - 1U);
        if ((batch_newest > next_commit_frame_ &&
             batch_newest - next_commit_frame_ > 256U) ||
            (batch_newest < next_commit_frame_ &&
             next_commit_frame_ - batch_newest > 512U)) return;
        if (batch.simulation_progress_present) {
            (void)record_simulation_progress_locked(
                peer->slot, batch.simulation_scene_epoch, batch.epoch,
                batch.simulation_completed_frame);
        }
        for (std::size_t index = 0; index < batch.inputs.size(); ++index) {
            const std::uint32_t input_frame =
                batch.first_frame + static_cast<std::uint32_t>(index);
            // Bound both history and future indexing before touching the
            // ring. A delayed old packet must not evict a useful newer slot.
            if ((input_frame < next_commit_frame_ &&
                 next_commit_frame_ - input_frame > 512U) ||
                (input_frame > next_commit_frame_ &&
                 input_frame - next_commit_frame_ > 256U)) continue;
            auto& revisions = received_input_revisions_[batch.player_slot];
            auto& accepted = revisions[input_frame];
            if (batch.revisions[index] <= accepted) {
                ++stale_input_revisions_;
                continue;
            }
            accepted = batch.revisions[index];
            const auto mismatch = timeline_.set_remote(
                batch.player_slot, input_frame, batch.inputs[index]);
            const auto committed = frame_commits_.find(input_frame);
            const bool missed_published_deadline = committed != frame_commits_.end() &&
                committed->second.inputs[batch.player_slot] != batch.inputs[index];
            if (mismatch || missed_published_deadline) {
                // Player 1 is the canonical simulation. Once a frame commit
                // has been published, a late guest sample cannot rewrite it:
                // rewinding only the old racer/RNG subset while track actors,
                // collisions and object lifecycles stayed live was the source
                // of long-session freezes and divergent moving obstacles.
                // Input delay is the deadline; a genuinely late sample is
                // retained only for diagnostics and later input prediction.
                ++late_inputs_discarded_;
                failure_recorder().record(
                    FailureEventKind::LateInputDiscarded, input_frame,
                    batch.player_slot, next_commit_frame_,
                    "late sample arrived after immutable host commit");
            }
        }
        auto& revisions = received_input_revisions_[batch.player_slot];
        std::erase_if(revisions, [&](const auto& entry) {
            return entry.first < next_commit_frame_ &&
                   next_commit_frame_ - entry.first > 512U;
        });
        const std::uint32_t newest = batch.first_frame +
            static_cast<std::uint32_t>(batch.inputs.size() - 1U);
        host_input_accepted_frame_ =
            (std::max)(host_input_accepted_frame_, newest);
        std::uint32_t& first_missing =
            peer_input_first_missing_frame_[batch.player_slot];
        while (timeline_.slot_confirmed(batch.player_slot, first_missing) &&
               first_missing != std::numeric_limits<std::uint32_t>::max()) {
            ++first_missing;
        }
        send_with_key(peer->address, peer->key,
                      protocol::MessageType::InputAck,
                      protocol::encode_input_acknowledge(
                          {batch.epoch, batch.player_slot, newest,
                           first_missing}), newest);
    } else if (packet.header.type ==
               protocol::MessageType::FrameCommitRequest) {
        protocol::FrameCommitRequestPayload request{};
        const bool gameplay_commit_timeline =
            authoritative_phase_active_ &&
            authority_lifecycle_ == AuthorityLifecycle::Racing;
        const bool frontend_commit_timeline =
            !authoritative_phase_active_ &&
            (authority_lifecycle_ == AuthorityLifecycle::Inactive ||
             authority_lifecycle_ == AuthorityLifecycle::PostRace);
        const bool sealed_commit_timeline =
            authority_lifecycle_ == AuthorityLifecycle::SealingFinish &&
            finish_seal_frame_.has_value();
        // Repair replays immutable, already-published history; it does not
        // authorize a new simulation tick. A peer must be able to reach the
        // sealed boundary even while the host is parked there.
        if ((!gameplay_commit_timeline && !frontend_commit_timeline &&
             !sealed_commit_timeline) ||
            !launch_descriptor_ ||
            !protocol::decode_frame_commit_request(packet.payload, request) ||
            request.epoch != authority_epoch() ||
            request.player_slot != peer->slot ||
            request.first_missing_frame != packet.header.frame) {
            return;
        }
        ++commit_repair_requests_received_;
        if (request.first_missing_frame >= next_commit_frame_) return;
        if (sealed_commit_timeline &&
            request.first_missing_frame > *finish_seal_frame_) return;
        if (gameplay_handoff_ && gameplay_handoff_->suspended &&
            !gameplay_handoff_->armed &&
            request.first_missing_frame >= gameplay_handoff_->boundary_frame) return;
        if (commit_history_.empty() ||
            request.first_missing_frame < commit_history_.front().frame) {
            // The requested hash-chain prefix has aged out. Use the existing
            // future-boundary state transaction rather than terminating or
            // allowing the guest to simulate from a discontinuous chain.
            if (gameplay_commit_timeline) {
                schedule_recovery_locked(request.first_missing_frame,
                                         "authoritative commit delivery");
            } else {
                fail_locked("Required menu input history is no longer available. The session cannot safely resume; leave and reconnect. Your separate saves are retained.");
            }
            return;
        }
        std::uint32_t repair_end = next_commit_frame_;
        if (sealed_commit_timeline && *finish_seal_frame_ < repair_end) {
            repair_end = *finish_seal_frame_ + 1U;
        }
        if (gameplay_handoff_ && gameplay_handoff_->suspended &&
            !gameplay_handoff_->armed) {
            repair_end = (std::min)(repair_end, gameplay_handoff_->boundary_frame);
        }
        send_commit_history_to_peer(*peer, request.first_missing_frame,
            (std::min<std::uint32_t>)(64U, repair_end - request.first_missing_frame));
    } else if (packet.header.type ==
               protocol::MessageType::FrameCorrectionAck) {
        protocol::FrameCorrectionAcknowledgePayload acknowledgement{};
        if (!protocol::decode_frame_correction_acknowledge(
                packet.payload, acknowledgement) ||
            !frame_correction_transmission_ ||
            acknowledgement.epoch != authority_epoch() ||
            acknowledgement.generation !=
                frame_correction_transmission_->generation ||
            acknowledgement.player_slot != peer->slot) {
            return;
        }
        frame_correction_transmission_->acknowledgements[peer->slot] = true;
        bool complete = true;
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied &&
                !frame_correction_transmission_->acknowledgements[slot]) {
                complete = false;
                break;
            }
        }
        if (complete) frame_correction_transmission_.reset();
    } else if (packet.header.type == protocol::MessageType::GameplayHandoff) {
        protocol::GameplayHandoffPayload request{};
        if (!protocol::decode_gameplay_handoff(packet.payload, request) ||
            request.stage != protocol::GameplayHandoffStage::Request ||
            request.player_slot != peer->slot ||
            request.current_input_epoch != input_epoch_ ||
            request.boundary_frame > next_commit_frame_) {
            return;
        }
        protocol::GameplayHandoffPayload authoritative = request;
        authoritative.boundary_frame = next_commit_frame_;
        authoritative.resume_input_epoch = input_epoch_ + 1U;
        if (authoritative.resume_input_epoch == 0U) {
            authoritative.resume_input_epoch = 1U;
        }
        authoritative.player_slot = local_slot_;
        authoritative.stage = protocol::GameplayHandoffStage::Suspend;
        if (!adopt_gameplay_handoff_locked(authoritative, false, error)) {
            fail_locked(error.empty()
                ? "The gameplay handoff request was inconsistent."
                : std::move(error));
            return;
        }
        send_gameplay_handoff_locked(
            protocol::GameplayHandoffStage::Suspend);
        status_ = "A racer reached track loading; parking the frontend input timeline.";
    } else if (packet.header.type == protocol::MessageType::RollbackData) {
        protocol::RollbackPayload rollback{};
        if (!authoritative_phase_active_ || scene_epoch_ == 0U ||
            !launch_descriptor_ || !gameplay_barrier_ ||
            !gameplay_barrier_->go_released ||
            launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
            !protocol::decode_rollback_payload(packet.payload, rollback, error) ||
            rollback.scene_epoch != scene_epoch_ ||
            rollback.transport_epoch != rollback_transport_epoch_ ||
            rollback.source_slot != peer->slot ||
            !launch_descriptor_->occupied(rollback.target_slot) ||
            rollback.target_slot == rollback.source_slot) {
            return;
        }
        if (rollback.target_slot == local_slot_) {
            if (rollback_inbound_.size() >= 2048U) {
                ++transport_queue_failures_;
                failure_recorder().record(
                    FailureEventKind::QueueOverflow, packet.header.frame,
                    static_cast<std::uint32_t>(rollback_inbound_.size()),
                    rollback.source_slot, "host rollback inbound");
                fail_locked(
                    "The rollback receive queue overflowed; the session "
                    "stopped before silently losing an authored packet.");
                return;
            }
            rollback_inbound_.push_back(
                {rollback.scene_epoch, rollback.source_slot,
                 std::move(rollback.bytes)});
            rollback_queue_high_water_ = (std::max)(
                rollback_queue_high_water_, rollback_inbound_.size());
        } else {
            const PeerRecord& target = peers_[rollback.target_slot];
            if (target.active) {
                send_with_key(target.address, target.key,
                              protocol::MessageType::RollbackData,
                              packet.payload, packet.header.frame);
            }
        }
    } else if (packet.header.type ==
               protocol::MessageType::RollbackRecoveryRequest) {
        protocol::RecoveryPayload recovery{};
        if (!authoritative_phase_active_ || !launch_descriptor_ ||
            launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
            authority_lifecycle_ != AuthorityLifecycle::Racing ||
            !protocol::decode_recovery(packet.payload, recovery) ||
            recovery.scene_epoch != scene_epoch_ ||
            recovery.player_slot != peer->slot ||
            recovery.frame != packet.header.frame) {
            return;
        }
        schedule_recovery_locked(recovery.frame,
                                 "rollback gameplay state");
    } else if (packet.header.type == protocol::MessageType::GameplayBarrier) {
        protocol::GameplayBarrierPayload barrier{};
        if (!protocol::decode_gameplay_barrier(packet.payload, barrier) ||
            barrier.player_slot != peer->slot ||
            (barrier.stage != protocol::GameplayBarrierStage::Ready &&
             barrier.stage != protocol::GameplayBarrierStage::Armed)) return;
        if (completed_release_locked(protocol::MessageType::GameplayBarrier,
                barrier.scene_epoch, barrier.map, barrier.racer_count, peer->sender_id)) {
            send_to(peer->address, protocol::MessageType::GameplayBarrier,
                protocol::encode_gameplay_barrier({barrier.scene_epoch,
                    barrier.map, barrier.racer_count, local_slot_,
                    protocol::GameplayBarrierStage::Go}));
            return;
        }
        if (!authoritative_phase_active_ || !gameplay_barrier_ ||
            barrier.scene_epoch != scene_epoch_ ||
            barrier.scene_epoch != gameplay_barrier_->scene_epoch ||
            barrier.map != gameplay_barrier_->map ||
            barrier.racer_count != gameplay_barrier_->racer_count ||
            barrier.player_slot != peer->slot ||
            (barrier.stage != protocol::GameplayBarrierStage::Ready &&
             barrier.stage != protocol::GameplayBarrierStage::Armed)) {
            return;
        }
        if (barrier.stage == protocol::GameplayBarrierStage::Ready) {
            if (gameplay_barrier_->baseline_released) return;
            gameplay_barrier_->ready[peer->slot] = true;
        } else {
            if (!gameplay_barrier_->baseline_released ||
                !gameplay_barrier_->arm_announced ||
                gameplay_barrier_->go_released) {
                return;
            }
            gameplay_barrier_->armed[peer->slot] = true;
        }
        state_changed_.notify_all();
    } else if (packet.header.type ==
               protocol::MessageType::SimulationProgress) {
        protocol::SimulationProgressPayload progress{};
        if (!protocol::decode_simulation_progress(packet.payload, progress) ||
            progress.player_slot != peer->slot ||
            progress.completed_frame != packet.header.frame) {
            return;
        }
        (void)record_simulation_progress_locked(
            peer->slot, progress.scene_epoch, progress.input_epoch,
            progress.completed_frame);
    } else if (packet.header.type == protocol::MessageType::StateHash) {
        protocol::StateHashPayload state_hash{};
        if (!protocol::decode_state_hash(packet.payload, state_hash, error) ||
            state_hash.player_slot != peer->slot ||
            state_hash.scene_epoch != scene_epoch_) return;
        HashFrame& hashes = state_hashes_[packet.header.frame];
        hashes.values[peer->slot] = state_hash.hash;
        hashes.globals[peer->slot] = state_hash.globals_hash;
        hashes.roster[peer->slot] = state_hash.roster_hash;
        hashes.racers[peer->slot] = state_hash.racers_hash;
        hashes.racer_counts[peer->slot] = state_hash.racer_count;
        hashes.racer_details[peer->slot] = state_hash.racer_hashes;
        hashes.present[peer->slot] = true;
        evaluate_state_hash(packet.header.frame);
    } else if (packet.header.type == protocol::MessageType::StateRequest) {
        if (!authoritative_phase_active_) return;
        protocol::StateRequestPayload request{};
        if (!protocol::decode_state_request(packet.payload, request) ||
            request.scene_epoch != scene_epoch_) return;
        if (is_live_replica_frame(request.frame)) {
            const std::uint32_t logical_frame =
                live_replica_logical_frame(request.frame);
            const auto state = live_replica_states_.find(logical_frame);
            if (state != live_replica_states_.end()) {
                // Requests are placed on the reliable checkpoint lane, while
                // retaining the tagged frame identity for the guest's live
                // assembly map.
                send_authoritative_state(
                    &peer->address, logical_frame, state->second,
                    request.missing_chunks,
                    protocol::MessageType::StateSnapshot, true);
            } else {
                ++live_replica_request_misses_;
                failure_recorder().record(
                    FailureEventKind::ReplicaUnavailable, logical_frame,
                    live_replica_oldest_retained_frame(
                        last_authoritative_frame_),
                    last_authoritative_frame_,
                    "requested live replica was outside retained history");
            }
        } else {
            const auto state = authoritative_states_.find(request.frame);
            if (state != authoritative_states_.end()) {
                send_authoritative_state(&peer->address, request.frame,
                                         state->second,
                                         request.missing_chunks);
            }
        }
    } else if (packet.header.type ==
               protocol::MessageType::StateAcknowledge) {
        if (!authoritative_phase_active_) return;
        protocol::StateAcknowledgePayload acknowledgement{};
        if (!protocol::decode_state_acknowledge(packet.payload,
                                                 acknowledgement) ||
            acknowledgement.scene_epoch != scene_epoch_ ||
            acknowledgement.player_slot != peer->slot) {
            return;
        }
        const auto state = authoritative_states_.find(acknowledgement.frame);
        auto acknowledgements = authoritative_acknowledgements_.find(
            acknowledgement.frame);
        if (state == authoritative_states_.end() ||
            acknowledgements == authoritative_acknowledgements_.end() ||
            authoritative_state_checksum(state->second) !=
                acknowledgement.checksum) {
            return;
        }
        acknowledgements->second[peer->slot] = true;
        bool complete = true;
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied &&
                !acknowledgements->second[slot]) {
                complete = false;
            }
        }
        if (complete) {
            ++authority_checkpoints_acknowledged_;
            fully_acknowledged_states_.insert(acknowledgement.frame);
            authoritative_acknowledgements_.erase(acknowledgements);
            state_changed_.notify_all();
        }
    } else if (packet.header.type == protocol::MessageType::RecoveryAck) {
        protocol::RecoveryPayload recovery{};
        if (!protocol::decode_recovery(packet.payload, recovery) ||
            recovery.player_slot != peer->slot) return;
        if (completed_release_locked(protocol::MessageType::RecoveryResume,
                recovery.scene_epoch, recovery.frame, 0U, peer->sender_id)) {
            send_to(peer->address, protocol::MessageType::RecoveryResume,
                protocol::encode_recovery({recovery.scene_epoch, recovery.frame, 0U}), recovery.frame);
            return;
        }
        if (!recovery_frame_ || recovery.frame != *recovery_frame_ ||
            recovery.scene_epoch != scene_epoch_) return;
        recovery_acks_[peer->slot] = true;
        bool complete = true;
        const Room& room = lobby_.room();
        for (std::size_t slot = 0U; slot < room.players.size(); ++slot) {
            if (room.players[slot].occupied && !recovery_acks_[slot]) {
                complete = false;
            }
        }
        if (complete && !recovery_resumed_) {
            const protocol::RecoveryPayload resume{
                scene_epoch_, *recovery_frame_, 0U};
            const auto payload = protocol::encode_recovery(resume);
            // Redundant control copies make recovery release resilient to an
            // isolated UDP loss without turning it into another bulk stream.
            for (int copy = 0; copy < 3; ++copy) {
                broadcast(protocol::MessageType::RecoveryResume, payload,
                          *recovery_frame_);
            }
            recovery_resumed_ = true;
            completed_recovery_frame_ = recovery.frame;
            remember_release_locked(protocol::MessageType::RecoveryResume,
                recovery.scene_epoch, recovery.frame, 0U);
            completed_recovery_epoch_ = recovery.scene_epoch;
            recovery_resume_until_ = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(1);
            status_ = "The synchronized race recovered cleanly.";
        }
    } else if (packet.header.type == protocol::MessageType::TransitionBarrier) {
        protocol::TransitionBarrierPayload transition{};
        if (!protocol::decode_transition_barrier(packet.payload, transition) ||
            transition.stage !=
                protocol::TransitionBarrierStage::Acknowledge ||
            transition.player_slot != peer->slot) return;
        if (completed_release_locked(protocol::MessageType::TransitionBarrier,
                transition.scene_epoch, transition.frame, transition.transition_kind, peer->sender_id)) {
            send_to(peer->address, protocol::MessageType::TransitionBarrier,
                protocol::encode_transition_barrier({transition.scene_epoch,
                    transition.frame, transition.transition_kind, local_slot_,
                    protocol::TransitionBarrierStage::Resume}), transition.frame);
            return;
        }
        if (transition.scene_epoch != scene_epoch_) return;
        if (!transition_barrier_) {
            // UDP may deliver a duplicate Acknowledge after a completed
            // transition has already entered post-race teardown. It is stale,
            // not evidence that the live race diverged.
            if (authority_lifecycle_ != AuthorityLifecycle::SealingFinish) return;
            fail_locked(
                "A racer acknowledged a transition that Player 1 did not authorize.");
            return;
        }
        if (transition_barrier_->frame != transition.frame ||
            transition_barrier_->kind != transition.transition_kind) {
            // Once Resume has been issued, late/reordered acknowledgements are
            // idempotent control traffic. Retain strict mismatch detection only
            // while Player 1 is still sealing the active finish boundary.
            if (authority_lifecycle_ != AuthorityLifecycle::SealingFinish ||
                transition_barrier_->resumed) return;
            fail_locked(
                "A racer acknowledged a transition that Player 1 did not authorize.");
            return;
        }
        transition_barrier_->acknowledgements[peer->slot] = true;
        if (transition_barrier_->resumed) {
            // A client repeats Acknowledge until it sees Resume. Answer that
            // retry directly even after the normal broadcast grace window, so
            // losing every initial Resume cannot strand it at race teardown.
            send_to(peer->address, protocol::MessageType::TransitionBarrier,
                    protocol::encode_transition_barrier({
                        transition_barrier_->scene_epoch,
                        transition_barrier_->frame,
                        transition_barrier_->kind,
                        local_slot_,
                        protocol::TransitionBarrierStage::Resume}),
                    transition_barrier_->frame);
        }
        state_changed_.notify_all();
    } else if (packet.header.type == protocol::MessageType::Pong) {
        std::uint64_t token = 0U;
        if (decode_probe(packet.payload, token)) update_peer_metrics(*peer, token);
    } else if (packet.header.type == protocol::MessageType::Disconnect) {
        const std::string departed = lobby_.room().players[peer->slot].display_name;
        if ((launch_descriptor_ && launch_descriptor_->synchronization == SynchronizationMode::ExperimentalRollback) ||
            (owned_match_end_ && !owned_match_end_->resumed)) {
            remove_owned_peer_locked(packet_sender, departed + " left the session. The match has ended.");
            return;
        }
        if (state_ == ConnectionState::Loading || state_ == ConnectionState::Running) {
            const std::string remote_reason = packet.payload.empty()
                ? "The racer disconnected without a diagnostic reason."
                : std::string(packet.payload.begin(), packet.payload.end());
            fail_locked(departed + ": " + remote_reason);
            return;
        }
        lobby_.leave(peer_id);
        transport_->release_peer_route(peer->address);
        *peer = {};
        synchronize_peer_slots();
        room_view_ = lobby_.room();
        broadcast_lobby();
    }
}

void DirectSession::handle_client_packet(const protocol::Datagram& packet) {
    owned_last_host_packet_ = std::chrono::steady_clock::now();
    if (state_ != ConnectionState::Failed && handle_owned_match_end_locked(host_sender_id_, packet)) return;
    if (receive_owned_packet_locked(0, packet)) return;
    // Failed is terminal until the player explicitly leaves the session.
    // Delayed LobbyState/Start retries must never revive a halted client into
    // Loading or Running after a determinism or bootstrap failure.
    if (state_ == ConnectionState::Failed) return;
    std::string error;
    if (packet.header.type == protocol::MessageType::JoinPending) {
        if (!admission_pending(state_)) return;
        last_admission_response_ = std::chrono::steady_clock::now();
        state_ = ConnectionState::AwaitingApproval;
        status_ = "Waiting for the host to approve your join request...";
    } else if (packet.header.type == protocol::MessageType::HelloAck) {
        if (!admission_pending(state_)) return;
        protocol::HelloAckPayload acknowledgement{};
        if (!protocol::decode_hello_ack(packet.payload, acknowledgement, error)) return;
        if (!acknowledgement.accepted) {
            compatibility_sync_offer_ = acknowledgement.compatibility_offer;
            state_ = ConnectionState::Failed;
            status_ = acknowledgement.message;
            std::fprintf(stderr,"[online][admission] rejected reason=%s settings-sync-offered=%u\n",
                status_.c_str(),unsigned(compatibility_sync_offer_.has_value()));
            std::fflush(stderr);
            return;
        }
        compatibility_sync_offer_.reset();
        if(mod_offer_ && manifest_.mod_manifest_hash!=mod_offer_->manifest_hash) {
            fail_locked("The host admitted this request before local mod preparation was verified.");return;
        }
        if (acknowledgement.synchronized_save.size() != 512U ||
            acknowledgement.online_save_generation == 0U ||
            acknowledgement.online_save_hash == 0U || !save_installer_) {
            state_ = ConnectionState::Failed;
            status_ = "The host did not supply a complete isolated online save.";
            return;
        }
        const std::uint64_t supplied_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(
                acknowledgement.synchronized_save.data()),
            acknowledgement.synchronized_save.size()));
        if (supplied_hash != acknowledgement.online_save_hash) {
            state_ = ConnectionState::Failed;
            status_ = "The authenticated online save hash did not match its payload.";
            return;
        }
        local_slot_ = acknowledgement.player_slot;
        if(!queue_save_locked(SaveJobKind::Join,acknowledgement.synchronized_save,acknowledgement.online_save_generation,supplied_hash,local_slot_)) {
            fail_locked("The host online save could not be queued safely.");return;
        }
        local_online_save_ready_=false;local_online_save_acknowledged_=false;
        last_admission_response_=std::chrono::steady_clock::now();state_=ConnectionState::AwaitingApproval;
        status_="Host approved the join. Installing and verifying the isolated online save...";
    } else if(packet.header.type==protocol::MessageType::ExperimentalSaveOffer) {
        protocol::MatchEndPayload offer;
        if(state_!=ConnectionState::Lobby||!protocol::decode_match_end(packet.payload,offer)||
           (owned_match_end_&&!owned_match_end_->resumed)||
           offer.launch_hash!=match_id_||offer.stage!=protocol::MatchEndStage::Resume||offer.player_slot!=local_slot_||
           offer.save_generation<session_save_generation_)return;
        if(offer.save_generation==session_save_generation_&&offer.save_hash==manifest_.session_save_hash&&local_online_save_ready_) {
            send_to(host_address_,protocol::MessageType::OnlineSaveReady,protocol::encode_online_save_ready({local_slot_,session_save_generation_,manifest_.session_save_hash}));return;
        }
        if(offer.save_generation==session_save_generation_&&offer.save_hash!=manifest_.session_save_hash) {
            fail_locked("The host supplied conflicting online saves for the same generation. Leave and rejoin; your single-player saves were not changed.");return;
        }
        if(launch_countdown_active_||launch_stage_!=LaunchStage::Idle)return;
        const auto hash=stable_hash(std::string_view(reinterpret_cast<const char*>(offer.save.data()),offer.save.size()));
        if(hash!=offer.save_hash||!queue_save_locked(SaveJobKind::Resync,offer.save,offer.save_generation,hash,local_slot_)) {
            fail_locked("The host online save resynchronization could not be verified.");return;
        }
        local_online_save_ready_=false;local_online_save_acknowledged_=false;
        desired_ready_.reset();pending_ready_request_id_=0;
        status_="Synchronizing and verifying the host's latest online save...";
    } else if (packet.header.type == protocol::MessageType::OnlineSaveReadyAck) {
        protocol::OnlineSaveReadyPayload ready{};
        if (!local_online_save_ready_ || !protocol::decode_online_save_ready(packet.payload, ready, error) ||
            ready.player_slot != local_slot_ ||
            ready.generation != session_save_generation_ ||
            ready.hash != manifest_.session_save_hash) {
            return;
        }
        local_online_save_acknowledged_ = true;
        status_ = "Player 1 confirmed the isolated online save.";
    } else if (packet.header.type == protocol::MessageType::OnlineSaveStatus) {
        protocol::OnlineSaveStatusPayload payload{};
        if (protocol::decode_online_save_status(packet.payload, payload, error))
            apply_online_save_status_locked(payload);
    } else if (packet.header.type == protocol::MessageType::LobbyState) {
        protocol::LobbyStatePayload payload{};
        if (protocol::decode_lobby_state(packet.payload, payload, error)) apply_lobby_payload(payload);
    } else if (packet.header.type == protocol::MessageType::PreflightBegin) {
        protocol::PreflightBeginPayload begin{};
        if (!protocol::decode_preflight_begin(packet.payload, begin, error) ||
            state_ != ConnectionState::Lobby || !newer_preflight(begin.test_id,
                terminal_connection_test_id_, connection_test_active_, connection_test_id_)) {
            return;
        }
        reset_connection_test_locked();
        connection_test_results_ = {};
        connection_test_active_ = true;
        connection_test_id_ = begin.test_id;
        connection_test_started_ = std::chrono::steady_clock::now();
        connection_test_drain_end_ = connection_test_started_ +
            std::chrono::milliseconds(begin.duration_ms) + std::chrono::seconds(5);
        status_ = "Player 1 is testing the real online traffic lanes.";
    } else if (const auto lane = preflight_lane(packet.header.type);
               lane.has_value()) {
        protocol::PreflightProbePayload probe{};
        if (!protocol::decode_preflight_probe(packet.payload, probe, error) ||
            probe.echo) {
            return;
        }
        if (!connection_test_active_ && state_ == ConnectionState::Lobby &&
            newer_preflight(probe.test_id, terminal_connection_test_id_, false, connection_test_id_)) {
            reset_connection_test_locked();
            connection_test_results_ = {};
            connection_test_active_ = true;
            connection_test_id_ = probe.test_id;
            connection_test_started_ = std::chrono::steady_clock::now();
            connection_test_drain_end_ = connection_test_started_ +
                                         std::chrono::seconds(12);
        }
        if (!connection_test_active_ || probe.test_id != connection_test_id_) return;
        probe.echo = true;
        send_to(host_address_, packet.header.type,
                protocol::encode_preflight_probe(probe));
    } else if (packet.header.type == protocol::MessageType::PreflightResult) {
        protocol::PreflightResultPayload result{};
        if (!protocol::decode_preflight_result(packet.payload, result, error) ||
            result.test_id != connection_test_id_) {
            return;
        }
        // Echo the authenticated result as an ACK, including harmless retries
        // after completion. No additional gameplay protocol message is needed.
        send_to(host_address_, protocol::MessageType::PreflightResult, packet.payload);
        if (!connection_test_active_) return;
        connection_test_results_[result.player_slot] = {
            true, result.player_slot, result.score, result.p95_rtt_ms,
            result.jitter_ms,
            static_cast<float>(result.loss_tenths_percent) / 10.0F,
            static_cast<float>(result.late_tenths_percent) / 10.0F,
            result.queues_drained};
        bool complete = true;
        for (std::size_t slot = 0U; slot < room_view_.players.size(); ++slot) {
            if (room_view_.players[slot].occupied &&
                !connection_test_results_[slot].valid) {
                complete = false;
            }
        }
        if (complete) {
            connection_test_active_ = false;
            terminal_connection_test_id_ = connection_test_id_;
            ++connection_test_result_generation_;
            if (connection_test_result_generation_ == 0U) {
                ++connection_test_result_generation_;
            }
            status_ = "Connection pre-flight results are ready.";
        }
    } else if (packet.header.type == protocol::MessageType::ReadyAck) {
        protocol::ReadyAckPayload acknowledgement{};
        if (!protocol::decode_ready_ack(packet.payload, acknowledgement,
                                        error) ||
            acknowledgement.player_slot != local_slot_) {
            return;
        }
        if (acknowledgement.request_id == pending_ready_request_id_ &&
            desired_ready_.has_value() &&
            acknowledgement.ready == *desired_ready_) {
            pending_ready_request_id_ = 0U;
            desired_ready_.reset();
            status_ = acknowledgement.ready
                ? "Ready state confirmed by Player 1."
                : "Not-ready state confirmed by Player 1.";
        }
    } else if (packet.header.type == protocol::MessageType::LaunchPrepare) {
        protocol::LaunchPreparePayload preparation{};
        const bool decoded = protocol::decode_launch_prepare(
            packet.payload, preparation, error);
        if (!decoded || preparation.launch_epoch == 0U ||
            (preparation.start.descriptor.synchronization == SynchronizationMode::ExperimentalRollback &&
             (preparation.start.descriptor.lobby_generation <= retired_owned_generation_ ||
              (owned_match_end_ && !owned_match_end_->resumed)))) return;

        // LobbyState and lifecycle traffic use separate receive lanes. A
        // LaunchPrepare can therefore arrive just before the LobbyState that
        // carries its generation. Do not reject a valid launch merely because
        // the local lobby view is one packet behind; Player 1 retransmits the
        // preparation after the monotonic lobby update has arrived.
        if (state_ != ConnectionState::Lobby ||
            preparation.lobby_generation != room_view_.generation ||
            preparation.countdown_generation !=
                launch_countdown_generation_) {
            return;
        }
        const bool valid_envelope = true;
        const bool accepted = valid_envelope &&
            validate_start_descriptor(preparation.start, error);
        if (accepted) {
            remote_launch_prepare_ = preparation;
            launch_countdown_active_ = false;
            launch_stage_ = LaunchStage::Preparing;
            remote_countdown_remaining_ms_ = 0U;
            remote_countdown_received_at_ = {};
            status_ = "Player 1's synchronized starting grid is ready.";
        } else if (error.empty()) {
            error = "Player 1's synchronized starting grid did not match this lobby.";
        }
        send_to(host_address_, protocol::MessageType::LaunchPrepareAck,
                protocol::encode_launch_prepare_ack({
                    preparation.launch_epoch, local_slot_, accepted}));
        if (!accepted) status_ = error;
    } else if (packet.header.type == protocol::MessageType::LaunchCommit) {
        protocol::LaunchCommitPayload commit{};
        if (!protocol::decode_launch_commit(packet.payload, commit, error) ||
            commit.launch_epoch == 0U) {
            return;
        }
        if (committed_launch_epoch_ == commit.launch_epoch) {
            send_to(host_address_, protocol::MessageType::LaunchCommitAck,
                    protocol::encode_launch_commit_ack(
                        {commit.launch_epoch, local_slot_}));
            return;
        }
        if (!remote_launch_prepare_ ||
            remote_launch_prepare_->launch_epoch != commit.launch_epoch) {
            status_ =
                "Player 1 committed a start before this racer validated the starting grid.";
            return;
        }
        const auto prepared = remote_launch_prepare_->start;
        if (!commit_launch_locked(prepared, commit.launch_epoch, error)) {
            fail_locked(error.empty()
                ? "Player 1's synchronized launch commit was rejected safely."
                : std::move(error));
            return;
        }
        send_to(host_address_, protocol::MessageType::LaunchCommitAck,
                protocol::encode_launch_commit_ack(
                    {commit.launch_epoch, local_slot_}));
    } else if (packet.header.type == protocol::MessageType::LaunchRelease) {
        protocol::LaunchReleasePayload release{};
        if (!protocol::decode_launch_release(packet.payload, release, error) ||
            release.launch_epoch == 0U ||
            release.launch_epoch != committed_launch_epoch_) {
            return;
        }
        if (!release_launch_locked(release.launch_epoch, error)) {
            fail_locked(error.empty()
                ? "Player 1's synchronized launch release was rejected safely."
                : std::move(error));
            return;
        }
        send_to(host_address_, protocol::MessageType::LaunchReleaseAck,
                protocol::encode_launch_release_ack(
                    {release.launch_epoch, local_slot_}));
        launch_stage_ = LaunchStage::Idle;
    } else if (packet.header.type == protocol::MessageType::LaunchCancel) {
        protocol::LaunchCancelPayload cancellation{};
        if (!protocol::decode_launch_cancel(packet.payload, cancellation,
                                            error) ||
            cancellation.launch_epoch == 0U) {
            return;
        }
        if ((remote_launch_prepare_ &&
             remote_launch_prepare_->launch_epoch == cancellation.launch_epoch) ||
            (!launch_released_ &&
             committed_launch_epoch_ == cancellation.launch_epoch)) {
            reset_launch_transaction_locked();
            committed_launch_epoch_ = 0U;
            launch_descriptor_.reset();
            state_ = ConnectionState::Lobby;
            status_ = "Player 1 cancelled the synchronized start safely.";
        }
    } else if (packet.header.type == protocol::MessageType::Start) {
        protocol::StartPayload start{};
        if (protocol::decode_start(packet.payload, start, error) &&
            start.descriptor.synchronization == SynchronizationMode::ExperimentalRollback &&
            (start.descriptor.lobby_generation <= retired_owned_generation_ ||
             (owned_match_end_ && !owned_match_end_->resumed))) return;
        if (!protocol::decode_start(packet.payload, start, error) ||
            !accept_start_descriptor(start, error)) {
            fail_locked(error.empty() ? "The synchronized start roster was rejected."
                                      : std::move(error));
            return;
        }
        if (start.stage == 0U) {
            launch_countdown_active_ = false;
            launch_stage_ = LaunchStage::Idle;
            remote_countdown_remaining_ms_ = 0U;
            remote_countdown_received_at_ = {};
            if (state_ != ConnectionState::Loading &&
                state_ != ConnectionState::Running) {
                state_ = ConnectionState::Loading;
                launch_requested_ = true;
                status_ = "Player 1 started the game. Loading together...";
            }
        } else if (start.stage == 1U) {
            state_ = ConnectionState::Running;
            status_ = "Every racer is synchronized. Go!";
        }
    } else if (packet.header.type == protocol::MessageType::GameplayHandoff) {
        protocol::GameplayHandoffPayload handoff{};
        if (!protocol::decode_gameplay_handoff(packet.payload, handoff) ||
            handoff.stage != protocol::GameplayHandoffStage::Suspend ||
            handoff.player_slot != 0U ||
            handoff.current_input_epoch != input_epoch_ ||
            handoff.boundary_frame < next_commit_frame_) {
            return;
        }
        if (!adopt_gameplay_handoff_locked(handoff, false, error)) {
            fail_locked(error.empty()
                ? "Player 1's gameplay handoff was inconsistent."
                : std::move(error));
            return;
        }
        status_ = "Player 1 parked the frontend timeline for track loading.";
    } else if (packet.header.type == protocol::MessageType::RollbackData) {
        protocol::RollbackPayload rollback{};
        if (!authoritative_phase_active_ || scene_epoch_ == 0U ||
            !launch_descriptor_ || !gameplay_barrier_ ||
            !gameplay_barrier_->go_released ||
            launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
            !protocol::decode_rollback_payload(packet.payload, rollback, error) ||
            rollback.scene_epoch != scene_epoch_ ||
            rollback.transport_epoch != rollback_transport_epoch_ ||
            rollback.target_slot != local_slot_ || rollback.source_slot == local_slot_ ||
            !launch_descriptor_->occupied(rollback.source_slot)) {
            return;
        }
        if (rollback_inbound_.size() >= 2048U) {
            ++transport_queue_failures_;
            failure_recorder().record(
                FailureEventKind::QueueOverflow, packet.header.frame,
                static_cast<std::uint32_t>(rollback_inbound_.size()),
                rollback.source_slot, "client rollback inbound");
            fail_locked(
                "The rollback receive queue overflowed; the session stopped "
                "before silently losing an authored packet.");
            return;
        }
        rollback_inbound_.push_back(
            {rollback.scene_epoch, rollback.source_slot,
             std::move(rollback.bytes)});
        rollback_queue_high_water_ = (std::max)(
            rollback_queue_high_water_, rollback_inbound_.size());
    } else if (packet.header.type == protocol::MessageType::GameplayBarrier) {
        protocol::GameplayBarrierPayload barrier{};
        if (!authoritative_phase_active_ || !gameplay_barrier_ ||
            !protocol::decode_gameplay_barrier(packet.payload, barrier) ||
            barrier.player_slot != 0U ||
            barrier.stage == protocol::GameplayBarrierStage::Ready ||
            barrier.stage == protocol::GameplayBarrierStage::Armed) {
            return;
        }
        if (barrier.map != gameplay_barrier_->map ||
            barrier.racer_count != gameplay_barrier_->racer_count) {
            const bool current = gameplay_barrier_->epoch_synchronized
                ? barrier.scene_epoch == gameplay_barrier_->scene_epoch
                : gameplay_handoff_ && gameplay_handoff_->local_load_started &&
                    barrier.stage == protocol::GameplayBarrierStage::Prepare &&
                    barrier.scene_epoch > last_host_scene_epoch_;
            if (current) {
                fail_locked("Loaded scene disagreement: host map " + std::to_string(barrier.map) +
                    ", racers " + std::to_string(barrier.racer_count) + "; local map " +
                    std::to_string(gameplay_barrier_->map) + ", racers " +
                    std::to_string(gameplay_barrier_->racer_count) + ", scene epoch " +
                    std::to_string(barrier.scene_epoch) + ", input epoch " +
                    std::to_string(input_epoch_) + ". The session cannot safely continue.");
            }
            return; // A stale scene remains harmless, not a new failure.
        }
        if (barrier.stage == protocol::GameplayBarrierStage::Prepare) {
            // This is the one legal point at which a client adopts a scene
            // epoch. The packet is authenticated as Player 1 and its map and
            // racer topology already match the locally loaded level.
            if (barrier.scene_epoch == 0U ||
                (gameplay_barrier_->epoch_synchronized &&
                 barrier.scene_epoch != gameplay_barrier_->scene_epoch) ||
                (!gameplay_barrier_->epoch_synchronized &&
                 last_host_scene_epoch_ != 0U &&
                 barrier.scene_epoch <= last_host_scene_epoch_)) {
                return;
            }
            scene_epoch_ = barrier.scene_epoch;
            gameplay_barrier_->scene_epoch = barrier.scene_epoch;
            gameplay_barrier_->epoch_synchronized = true;
            last_host_scene_epoch_ = barrier.scene_epoch;
            last_gameplay_ready_announcement_ = {};
            state_changed_.notify_all();
            return;
        }
        if (!gameplay_barrier_->epoch_synchronized ||
            barrier.scene_epoch != scene_epoch_ ||
            barrier.scene_epoch != gameplay_barrier_->scene_epoch) {
            return;
        }
        if (barrier.stage == protocol::GameplayBarrierStage::Baseline) {
            gameplay_barrier_->baseline_released = true;
        } else if (barrier.stage == protocol::GameplayBarrierStage::Arm &&
                   gameplay_barrier_->baseline_released) {
            gameplay_barrier_->arm_announced = true;
        } else if (barrier.stage == protocol::GameplayBarrierStage::Go &&
                   gameplay_barrier_->baseline_released &&
                   gameplay_barrier_->arm_announced &&
                   gameplay_barrier_->armed[local_slot_]) {
            gameplay_barrier_->go_released = true;
        }
        state_changed_.notify_all();
    } else if (packet.header.type ==
               protocol::MessageType::InputRepairRequest) {
        protocol::InputRepairRequestPayload request{};
        if (!protocol::decode_input_repair_request(packet.payload, request) ||
            request.player_slot != local_slot_ ||
            request.epoch != authority_epoch() ||
            request.first_missing_frame != packet.header.frame) {
            return;
        }
        // The host's exact hole is authoritative. In either mode the guest
        // game thread may itself be parked waiting for the commit containing
        // this input, so the missing sample is not guaranteed to have been
        // authored yet. Merely asking the parked game thread to resend it
        // creates a circular wait: host waits for input N while the guest waits
        // for host commit N. Complete that one uncommitted sample with the last
        // physical pad state and answer on the reliable repair lane. This is
        // equivalent to the hold-last sample the next local poll would have
        // produced, without polling SDL from the network worker.
        host_input_first_missing_frame_ = (std::max)(
            host_input_first_missing_frame_, request.first_missing_frame);
        const bool gameplay_handoff_parked = gameplay_handoff_ &&
            gameplay_handoff_->suspended;
        if (!gameplay_handoff_parked &&
             ensure_local_input_for_repair_locked(
                 request.first_missing_frame)) {
            send_local_history(local_input_submitted_frame_, true,
                               request.first_missing_frame);
            last_input_history_send_ = std::chrono::steady_clock::now();
        }
    } else if (packet.header.type == protocol::MessageType::InputAck) {
        protocol::InputAcknowledgePayload acknowledgement{};
        if (!protocol::decode_input_acknowledge(packet.payload,
                                                 acknowledgement) ||
            acknowledgement.player_slot != local_slot_) {
            return;
        }
        if (acknowledgement.epoch != authority_epoch()) {
            if (acknowledgement.epoch == retired_authority_epoch_)
                ++stale_epoch_packets_;
            else
                ++future_epoch_packets_;
            return;
        }
        const std::uint32_t maximum_acknowledge =
            local_input_submitted_frame_ ==
                    std::numeric_limits<std::uint32_t>::max()
                ? local_input_submitted_frame_
                : local_input_submitted_frame_ + 1U;
        if (acknowledgement.first_missing_frame > maximum_acknowledge) {
            return;
        }
        host_input_accepted_frame_ = (std::max)(
            host_input_accepted_frame_, acknowledgement.newest_frame);
        host_input_first_missing_frame_ = (std::max)(
            host_input_first_missing_frame_,
            acknowledgement.first_missing_frame);
        // Keep a small acknowledged tail as overlap between a cumulative ACK
        // and a concurrently queued repair request. Only history beyond that
        // overlap is reclaimed; every still-unacknowledged sample is retained.
        constexpr std::size_t kAcknowledgedHistoryOverlap = 256U;
        while (local_history_.size() > kAcknowledgedHistoryOverlap &&
               local_history_.front().first <
                   host_input_first_missing_frame_) {
            local_input_metadata_.erase(local_history_.front().first);
            local_history_.pop_front();
        }
    } else if (packet.header.type == protocol::MessageType::FrameCommit) {
        protocol::FrameCommitBatch batch{};
        if (!protocol::decode_frame_commit_batch(packet.payload, batch, error)) return;
        if (batch.correction &&
            batch.generation <= applied_frame_correction_generation_) {
            const auto acknowledgement =
                protocol::encode_frame_correction_acknowledge(
                    {authority_epoch(), batch.generation, local_slot_});
            send_to(host_address_, protocol::MessageType::FrameCorrectionAck,
                    acknowledgement, packet.header.frame);
            return;
        }
        if (!batch.correction &&
            batch.generation < applied_frame_correction_generation_) {
            return;
        }
        for (const protocol::FrameCommitPayload& commit : batch.commits) {
            if (commit.epoch != authority_epoch()) {
                if (commit.epoch == retired_authority_epoch_) {
                    ++stale_epoch_packets_;
                } else {
                    // A commit for the announced resume epoch may race the
                    // final handoff acknowledgement. It will be retransmitted
                    // from bounded history after the client resumes.
                    ++future_epoch_packets_;
                }
                continue;
            }
            if (!validate_commit(commit, error)) {
                fail_locked(std::move(error));
                return;
            }
            if (batch.correction) {
                if (commit.frame < batch.correction_first ||
                    commit.frame > batch.correction_last ||
                    batch.correction_last - batch.correction_first >= 128U) {
                    fail_locked("Player 1 sent an invalid rollback correction range.");
                    return;
                }
                if (!frame_correction_assembly_ ||
                    batch.generation >
                        frame_correction_assembly_->generation) {
                    frame_correction_assembly_ = FrameCorrectionAssembly{
                        commit.epoch, batch.generation,
                        batch.correction_first, batch.correction_last};
                } else if (batch.generation <
                           frame_correction_assembly_->generation) {
                    continue;
                }
                FrameCorrectionAssembly& assembly =
                    *frame_correction_assembly_;
                if (assembly.epoch != commit.epoch ||
                    assembly.first_frame != batch.correction_first ||
                    assembly.last_frame != batch.correction_last) {
                    fail_locked("Player 1 sent conflicting rollback correction metadata.");
                    return;
                }
                assembly.commits[commit.frame] = commit;
                assembly.last_seen = std::chrono::steady_clock::now();
            } else if (batch.generation >
                       applied_frame_correction_generation_) {
                deferred_frame_commits_[commit.frame] = {
                    batch.generation, commit};
            } else if (!apply_frame_commit_locked(commit, error)) {
                fail_locked(std::move(error));
                return;
            }
        }
        if (batch.correction && frame_correction_assembly_) {
            const std::uint64_t expected = static_cast<std::uint64_t>(
                frame_correction_assembly_->last_frame) -
                frame_correction_assembly_->first_frame + 1U;
            if (frame_correction_assembly_->commits.size() == expected &&
                !apply_frame_correction_locked(error)) {
                fail_locked(std::move(error));
                return;
            }
        }
        if (next_commit_frame_ > 0U) {
            const auto latest_consumed = frame_commits_.find(
                next_commit_frame_ - 1U);
            if (latest_consumed != frame_commits_.end()) {
                last_consumed_commit_hash_ =
                    latest_consumed->second.commit_hash;
            }
        }
    } else if (packet.header.type ==
               protocol::MessageType::RacerOrientation) {
        if (!authoritative_phase_active_ ||
            authority_lifecycle_ != AuthorityLifecycle::Racing) {
            return;
        }
        protocol::RacerOrientationPayload orientation{};
        if (!protocol::decode_racer_orientation(packet.payload,
                                                orientation) ||
            orientation.scene_epoch != scene_epoch_ ||
            orientation.frame != packet.header.frame ||
            orientation.frame > next_commit_frame_ + 64U ||
            (next_commit_frame_ > 128U &&
             orientation.frame < next_commit_frame_ - 128U)) {
            return;
        }
        racer_orientation_states_[orientation.frame] =
            std::move(orientation.state);
        if (racer_orientation_states_.size() > 128U) {
            const auto oldest = std::min_element(
                racer_orientation_states_.begin(),
                racer_orientation_states_.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });
            if (oldest != racer_orientation_states_.end()) {
                racer_orientation_states_.erase(oldest);
            }
        }
    } else if (packet.header.type == protocol::MessageType::StateHash) {
        protocol::StateHashPayload state_hash{};
        if (!protocol::decode_state_hash(packet.payload, state_hash, error) ||
            state_hash.player_slot != 0U ||
            state_hash.scene_epoch != scene_epoch_) return;
        HashFrame& hashes = state_hashes_[packet.header.frame];
        hashes.values[0] = state_hash.hash;
        hashes.globals[0] = state_hash.globals_hash;
        hashes.roster[0] = state_hash.roster_hash;
        hashes.racers[0] = state_hash.racers_hash;
        hashes.racer_counts[0] = state_hash.racer_count;
        hashes.racer_details[0] = state_hash.racer_hashes;
        hashes.present[0] = true;
        // Player 1 is the only recovery coordinator. A client records the
        // host digest for diagnostics but never starts an independent repair.
        if (hashes.present[local_slot_] &&
            hashes.values[local_slot_] == hashes.values[0]) {
            last_verified_frame_ = (std::max)(last_verified_frame_,
                                              packet.header.frame);
        }
    } else if (packet.header.type == protocol::MessageType::RecoveryBegin) {
        protocol::RecoveryPayload recovery{};
        if (!protocol::decode_recovery(packet.payload, recovery) ||
            recovery.player_slot != 0U || !authoritative_phase_active_ ||
            authority_lifecycle_ != AuthorityLifecycle::Racing ||
            recovery.scene_epoch != scene_epoch_) return;
        if (completed_release_locked(protocol::MessageType::RecoveryResume,
                recovery.scene_epoch, recovery.frame, 0U)) return;
        if (completed_recovery_frame_ &&
            completed_recovery_epoch_ == recovery.scene_epoch &&
            *completed_recovery_frame_ == recovery.frame) return;
        if (recovery_frame_) {
            if (*recovery_frame_ != recovery.frame) {
                fail_locked("Player 1 sent conflicting synchronized recovery boundaries.");
            }
            return;
        }
        if ((native_completed_frame_ && *native_completed_frame_ > recovery.frame) ||
            (next_commit_frame_ > recovery.frame &&
             next_commit_frame_ - recovery.frame > 1U)) {
            fail_locked("Player 1 requested recovery after this racer had already passed its boundary.");
            return;
        }
        recovery_frame_ = recovery.frame;
        recovery_stage_ = RecoveryStage::Scheduled;
        recovery_acks_ = {};
        recovery_resumed_ = false;
        status_ = "Connection variation detected; preparing a synchronized recovery.";
    } else if (packet.header.type == protocol::MessageType::RecoveryResume) {
        protocol::RecoveryPayload recovery{};
        if (!protocol::decode_recovery(packet.payload, recovery) ||
            recovery.player_slot != 0U || !recovery_frame_ ||
            recovery.frame != *recovery_frame_ ||
            recovery.scene_epoch != scene_epoch_) return;
        recovery_resumed_ = true;
        recovery_stage_ = RecoveryStage::Completed;
        completed_recovery_frame_ = recovery.frame;
        completed_recovery_epoch_ = recovery.scene_epoch;
        recovery_resume_until_ = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(1);
        status_ = "The synchronized race recovered cleanly.";
    } else if (packet.header.type == protocol::MessageType::StateSnapshot ||
               packet.header.type ==
                   protocol::MessageType::LiveReplicaSnapshot) {
        if (!authoritative_phase_active_) return;
        protocol::StateSnapshotChunk chunk{};
        if (!protocol::decode_state_snapshot_chunk(packet.payload, chunk,
                                                   error) ||
            chunk.scene_epoch != scene_epoch_) return;
        const bool live_replica = is_live_replica_frame(chunk.frame);
        if (packet.header.type == protocol::MessageType::LiveReplicaSnapshot &&
            !live_replica) {
            return;
        }
        const std::uint32_t logical_frame = live_replica
            ? live_replica_logical_frame(chunk.frame) : chunk.frame;
        // The gameplay-start baseline is an out-of-band bootstrap snapshot,
        // not an ordinary authored-timeline recovery. Frontend/menu input has
        // already advanced next_commit_frame_ by the time a track is loaded,
        // while the portable race baseline is deliberately identified as
        // frame zero. Accept it only while the matching gameplay barrier is
        // parked between Baseline and Resume; all in-race snapshots retain
        // the strict rolling-window checks below.
        const bool gameplay_bootstrap =
            authority_lifecycle_ == AuthorityLifecycle::Racing &&
            gameplay_barrier_ &&
            gameplay_barrier_->scene_epoch == scene_epoch_ &&
            gameplay_barrier_->baseline_released &&
            !gameplay_barrier_->arm_announced && logical_frame == 0U &&
            !live_replica;
        const bool recovery_checkpoint = recovery_frame_ &&
                                         logical_frame == *recovery_frame_ &&
                                         !live_replica;
        if (!live_replica && recovery_frame_ &&
            logical_frame > *recovery_frame_) return;
        // Live replicas use the same history span retained by Player 1, so a
        // guest at the hard lead boundary can still accept the repair it asks
        // for. Bootstrap and synchronized recovery snapshots keep their
        // tighter input-delay/64-frame rules.
        if (live_replica) {
            if (!live_replica_within_recovery_window(
                    next_commit_frame_, logical_frame)) {
                ++live_replica_window_rejections_;
                failure_recorder().record(
                    FailureEventKind::ReplicaUnavailable, logical_frame,
                    next_commit_frame_, kLiveReplicaRecoveryHistoryFrames,
                    "live replica rejected outside coherent recovery window");
                return;
            }
        } else {
            const std::uint32_t maximum_lead =
                static_cast<std::uint32_t>(input_delay_) + 2U;
            if (!gameplay_bootstrap && !recovery_checkpoint &&
                (logical_frame > next_commit_frame_ + maximum_lead ||
                 (next_commit_frame_ > 64U &&
                  logical_frame < next_commit_frame_ - 64U))) {
                return;
            }
        }
        auto& assemblies = live_replica ? live_replica_assemblies_
                                        : snapshot_assemblies_;
        auto& completed_states = live_replica ? live_replica_states_
                                              : authoritative_states_;
        if (!assemblies.contains(logical_frame) &&
            assemblies.size() >= 16U) {
            const auto oldest = std::min_element(
                assemblies.begin(), assemblies.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });
            if (oldest != assemblies.end()) {
                assemblies.erase(oldest);
            }
        }
        SnapshotAssembly& assembly = assemblies[logical_frame];
        if (assembly.chunk_count == 0U) {
            assembly.scene_epoch = chunk.scene_epoch;
            assembly.checksum = chunk.checksum;
            assembly.total_size = chunk.total_size;
            assembly.chunk_count = chunk.chunk_count;
            assembly.chunks.resize(chunk.chunk_count);
            assembly.present.assign(chunk.chunk_count, false);
        }
        if (assembly.scene_epoch != chunk.scene_epoch ||
            assembly.checksum != chunk.checksum ||
            assembly.total_size != chunk.total_size ||
            assembly.chunk_count != chunk.chunk_count) {
            if (!live_replica) {
                fail_locked(
                    "Player 1 sent conflicting authoritative snapshot metadata.");
                return;
            }
            // A late guest input can make Player 1 revise an unconsumed live
            // frame. Drop the partial older revision and assemble the newest
            // checksum atomically.
            assembly = {};
            assembly.scene_epoch = chunk.scene_epoch;
            assembly.checksum = chunk.checksum;
            assembly.total_size = chunk.total_size;
            assembly.chunk_count = chunk.chunk_count;
            assembly.chunks.resize(chunk.chunk_count);
            assembly.present.assign(chunk.chunk_count, false);
        }
        if (!assembly.present[chunk.chunk_index]) {
            assembly.chunks[chunk.chunk_index] = std::move(chunk.bytes);
            assembly.present[chunk.chunk_index] = true;
        }
        if (std::all_of(assembly.present.begin(), assembly.present.end(),
                        [](bool value) { return value; })) {
            std::vector<std::uint8_t> complete;
            complete.reserve(assembly.total_size);
            for (const auto& part : assembly.chunks) {
                complete.insert(complete.end(), part.begin(), part.end());
            }
            if (complete.size() != assembly.total_size ||
                authoritative_state_checksum(complete) != assembly.checksum) {
                if (live_replica) {
                    // Continuous actor state is intentionally latest-wins.
                    // Corruption or mixed fragments invalidate this one sample;
                    // a later host sample (or a missing-fragment request) heals
                    // it. Never tear down a healthy race for disposable state.
                    ++live_replica_decode_failures_;
                    assemblies.erase(logical_frame);
                    return;
                }
                fail_locked("Player 1's authoritative snapshot failed its checksum.");
                return;
            }
            std::vector<std::uint8_t> decoded_state;
            const bool delta_wire =
                authoritative_state_wire_is_delta(complete);
            const bool decoded = delta_wire
                ? (live_replica_keyframe_frame_ &&
                   decode_authoritative_state_delta_wire(
                       complete, live_replica_keyframe_state_,
                       *live_replica_keyframe_frame_,
                       kMaximumAuthoritativeStateBytes,
                       decoded_state, error))
                : decode_authoritative_state_wire(
                      complete, kMaximumAuthoritativeStateBytes,
                      decoded_state, error);
            if (!decoded) {
                if (live_replica) {
                    ++live_replica_decode_failures_;
                    assemblies.erase(logical_frame);
                    return;
                }
                fail_locked("Player 1's authoritative snapshot compression is invalid.");
                return;
            }
            if (live_replica && !delta_wire &&
                logical_frame % kAuthoritativeDeltaKeyframeInterval == 0U &&
                (!live_replica_keyframe_frame_ ||
                 logical_frame >= *live_replica_keyframe_frame_)) {
                live_replica_keyframe_frame_ = logical_frame;
                live_replica_keyframe_state_ = decoded_state;
            }
            completed_states[logical_frame] = std::move(decoded_state);
            if (recovery_checkpoint) {
                recovery_stage_ = RecoveryStage::SnapshotReady;
            }
            assemblies.erase(logical_frame);
            const std::uint32_t retention = live_replica
                ? kLiveReplicaRecoveryHistoryFrames : 64U;
            if (logical_frame > retention) {
                const std::uint32_t oldest = logical_frame - retention;
                for (auto iterator = completed_states.begin();
                     iterator != completed_states.end();) {
                    if (iterator->first < oldest) {
                        iterator = completed_states.erase(iterator);
                    } else {
                        ++iterator;
                    }
                }
                for (auto iterator = assemblies.begin();
                     iterator != assemblies.end();) {
                    if (iterator->first < oldest) {
                        iterator = assemblies.erase(iterator);
                    } else {
                        ++iterator;
                    }
                }
            }
        }
    } else if (packet.header.type == protocol::MessageType::TransitionBarrier) {
        protocol::TransitionBarrierPayload transition{};
        if (!protocol::decode_transition_barrier(packet.payload, transition) ||
            transition.player_slot != 0U ||
            transition.scene_epoch != scene_epoch_) return;
        if (completed_release_locked(protocol::MessageType::TransitionBarrier,
                transition.scene_epoch, transition.frame, transition.transition_kind)) return;
        if (!transition_barrier_) {
            transition_barrier_ = TransitionBarrierState{
                transition.scene_epoch, transition.frame,
                transition.transition_kind};
        } else if (transition_barrier_->frame != transition.frame ||
                   transition_barrier_->kind != transition.transition_kind) {
            fail_locked("Player 1 sent conflicting race transitions.");
            return;
        }
        if (transition.stage == protocol::TransitionBarrierStage::Begin &&
            authority_lifecycle_ == AuthorityLifecycle::Racing &&
            recovery_frame_ && !recovery_resumed_) {
            // Player 1's final snapshot supersedes an older scheduled repair.
            // Wake the authored frame boundary so this peer can reach the
            // finish hook and acknowledge the exact final state instead.
            recovery_resumed_ = true;
            recovery_stage_ = RecoveryStage::Completed;
        }
        transition_barrier_->acknowledgements[0] = true;
        if (transition.stage ==
            protocol::TransitionBarrierStage::Resume) {
            transition_barrier_->resumed = true;
        }
        state_changed_.notify_all();
    } else if (packet.header.type == protocol::MessageType::Ping) {
        std::uint64_t token = 0U;
        if (decode_probe(packet.payload, token)) {
            send_to(host_address_, protocol::MessageType::Pong, packet.payload);
        }
    } else if (packet.header.type == protocol::MessageType::Disconnect) {
        state_ = ConnectionState::Failed;
        status_ = packet.payload.empty()
            ? "The host ended the online session."
            : std::string(packet.payload.begin(), packet.payload.end());
    }
}

void DirectSession::broadcast_lobby() {
    broadcast(protocol::MessageType::LobbyState,
              protocol::encode_lobby_state(lobby_payload()));
    if (is_host_ && local_online_save_ready_ && save_sync_available() &&
        (state_ == ConnectionState::Hosting || state_ == ConnectionState::Lobby)) {
        std::uint8_t verified = 1U;
        for (const auto& peer : peers_)
            if (peer.active && peer.online_save_ready && peer.slot < kMaximumPlayers &&
                lobby_.room().players[peer.slot].occupied)
                verified |= static_cast<std::uint8_t>(1U << peer.slot);
        broadcast(protocol::MessageType::OnlineSaveStatus,
            protocol::encode_online_save_status({lobby_.room().generation,
                session_save_generation_, manifest_.session_save_hash, verified}));
    }
}

void DirectSession::apply_online_save_status_locked(const protocol::OnlineSaveStatusPayload& payload) {
    // Recover a missed save offer in an idle lobby. Never replace a running
    // game's EEPROM or turn a stale status into a new save generation.
    if(!is_host_&&state_==ConnectionState::Lobby&&
       room_view_.rules.synchronization==SynchronizationMode::ExperimentalRollback&&
       payload.room_generation>=room_view_.generation&&payload.save_generation>session_save_generation_&&
       !launch_countdown_active_&&launch_stage_==LaunchStage::Idle&&
       (!owned_match_end_||owned_match_end_->resumed)) {
        const auto now=std::chrono::steady_clock::now();
        if(!last_save_request_.time_since_epoch().count()||now-last_save_request_>=std::chrono::milliseconds(500)) {
            send_to(host_address_,protocol::MessageType::ExperimentalSaveRequest,{});
            last_save_request_=now;
        }
    }
    if (is_host_ || state_ != ConnectionState::Lobby || !local_online_save_ready_ ||
        payload.save_generation != session_save_generation_ || payload.save_hash != manifest_.session_save_hash ||
        payload.room_generation < room_view_.generation ||
        (online_save_status_ && payload.room_generation < online_save_status_->room_generation)) return;
    auto updated = payload;
    if (online_save_status_ && online_save_status_->room_generation == payload.room_generation)
        updated.verified_mask |= online_save_status_->verified_mask;
    online_save_status_ = updated;
    // A status packet may arrive before its LobbyState. Retain it but never
    // apply it to an older roster, particularly when a slot has been reused.
    if (updated.room_generation == room_view_.generation && local_slot_ < kMaximumPlayers &&
        room_view_.players[local_slot_].occupied && (updated.verified_mask & (1U << local_slot_))) {
        local_online_save_acknowledged_ = true;
    }
}

protocol::LobbyStatePayload DirectSession::lobby_payload() const {
    protocol::LobbyStatePayload payload{};
    const Room& room = lobby_.room();
    payload.generation = room.generation;
    payload.phase = room.phase;
    payload.room_name = room.name;
    payload.visibility = room.visibility;
    payload.rules = room.rules;
    payload.input_delay_frames = input_delay_;
    payload.countdown_active = launch_countdown_active_;
    payload.countdown_generation = launch_countdown_generation_;
    if (launch_countdown_active_) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            launch_countdown_deadline_ - std::chrono::steady_clock::now()).count();
        payload.countdown_remaining_ms = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(remaining, 0, 5000));
    }
    for (std::size_t index = 0; index < room.players.size(); ++index) {
        const Player& source = room.players[index];
        payload.players[index] = {source.occupied, source.ready, source.loaded,
                                  static_cast<std::uint8_t>(index), source.display_name,
                                  source.route, source.ping_ms, source.jitter_ms,
                                  source.packet_loss_percent};
    }
    return payload;
}

void DirectSession::apply_lobby_payload(const protocol::LobbyStatePayload& payload) {
    if (retired_owned_generation_ && payload.generation <= retired_owned_generation_) return;
    if (payload.generation < room_view_.generation) return;
    const bool semantic_update = payload.generation > room_view_.generation;
    if (semantic_update) {
        room_view_.generation = payload.generation;
        room_view_.phase = payload.phase;
        room_view_.name = payload.room_name;
        room_view_.visibility = payload.visibility;
        room_view_.rules = payload.rules;
        input_delay_ = payload.input_delay_frames;
    }
    for (std::size_t index = 0; index < payload.players.size(); ++index) {
        const auto& source = payload.players[index];
        Player& destination = room_view_.players[index];
        if (semantic_update) {
            destination = {};
            destination.occupied = source.occupied;
            destination.ready = source.ready;
            destination.loaded = source.loaded;
            destination.slot = static_cast<std::uint8_t>(index);
            destination.host = index == 0U && source.occupied;
            destination.display_name = source.display_name;
        }
        if (semantic_update || destination.occupied == source.occupied) {
            destination.route = source.route;
            destination.ping_ms = source.ping_ms;
            destination.jitter_ms = source.jitter_ms;
            destination.packet_loss_percent = source.packet_loss_percent;
        }
    }
    if (online_save_status_) apply_online_save_status_locked(*online_save_status_);
    const auto now = std::chrono::steady_clock::now();
    if (payload.countdown_generation > launch_countdown_generation_) {
        launch_countdown_active_ = payload.countdown_active;
        launch_countdown_generation_ = payload.countdown_generation;
        remote_countdown_remaining_ms_ = payload.countdown_remaining_ms;
        remote_countdown_received_at_ = now;
        launch_stage_ = payload.countdown_active ? LaunchStage::Countdown
                                                 : LaunchStage::Idle;
    } else if (payload.countdown_generation == launch_countdown_generation_) {
        launch_countdown_active_ = payload.countdown_active;
        if (payload.countdown_active) {
            if (launch_stage_ == LaunchStage::Idle) {
                launch_stage_ = LaunchStage::Countdown;
            }
            remote_countdown_remaining_ms_ = payload.countdown_remaining_ms;
            remote_countdown_received_at_ = now;
        } else {
            if (launch_stage_ == LaunchStage::Countdown) {
                launch_stage_ = LaunchStage::Idle;
            }
            remote_countdown_remaining_ms_ = 0U;
            remote_countdown_received_at_ = {};
        }
    }
    if (!is_host_ && semantic_update && payload.phase == RoomPhase::Running &&
               local_loaded_) {
        state_ = ConnectionState::Running;
        status_ = "Every racer is synchronized. Go!";
    }
}

DirectSession::PeerRecord* DirectSession::peer_by_sender(std::uint64_t sender) {
    const auto found = std::find_if(peers_.begin(), peers_.end(),
                                    [sender](const PeerRecord& peer) {
                                        return peer.active && peer.sender_id == sender;
                                    });
    return found != peers_.end() ? &*found : nullptr;
}

DirectSession::PeerRecord* DirectSession::peer_by_address(
    const PeerAddress& address) {
    const auto found = std::find_if(peers_.begin(), peers_.end(),
        [&address](const PeerRecord& peer) {
            return peer.active && peer.address == address;
        });
    return found != peers_.end() ? &*found : nullptr;
}

DirectSession::PendingRecord* DirectSession::pending_by_sender(
    std::uint64_t sender) {
    const auto found = std::find_if(pending_joins_.begin(), pending_joins_.end(),
        [sender](const PendingRecord& pending) {
            return pending.active && pending.sender_id == sender;
        });
    return found != pending_joins_.end() ? &*found : nullptr;
}

DirectSession::PendingRecord* DirectSession::pending_by_request(
    std::uint64_t request_id) {
    return pending_by_sender(request_id);
}

void DirectSession::expire_pending_joins() {
    constexpr auto lifetime = std::chrono::seconds(30);
    const auto now = std::chrono::steady_clock::now();
    for (PendingRecord& pending : pending_joins_) {
        if (pending.active && (now - pending.last_seen > lifetime ||
            (pending.mods_approved && now-pending.first_seen>std::chrono::minutes(20)))) {
            clear_pending_mod_route_locked(pending);pending = {};
        }
    }
}

std::uint8_t DirectSession::occupied_players() const {
    const Room& room = is_host_ ? lobby_.room() : room_view_;
    return static_cast<std::uint8_t>(std::count_if(
        room.players.begin(), room.players.end(),
        [](const Player& player) { return player.occupied; }));
}

void DirectSession::clear_input_history_locked() {
    local_history_.clear();
    local_input_metadata_.clear();
    for (auto& revisions : received_input_revisions_) revisions.clear();
    local_input_echo_ms_ = local_input_consume_ms_ = 0U;
    measured_input_echoes_ = 0U;
    *input_latency_ = {};
    predicted_local_samples_ = mismatched_local_samples_ = superseded_local_samples_ = 0;
    refreshed_input_samples_ = stale_input_revisions_ = prediction_limit_waits_ = 0U;
}

void DirectSession::store_local_input_locked(std::uint32_t frame,
                                            PackedInput input,
                                            bool physical_sample) {
    // Unique sorted history is shared by live transmission and exact repair.
    // A pending target may be refreshed, but an already committed target may
    // not be edited, even while a client is consuming an older presentation.
    if (physical_sample && ((is_host_ && frame < next_commit_frame_) ||
        (!is_host_ && authoritative_inputs_valid_ &&
         frame <= authoritative_input_frame_))) return;
    const auto position = std::lower_bound(local_history_.begin(), local_history_.end(),
        frame, [](const auto& entry, std::uint32_t value) { return entry.first < value; });
    auto& metadata = local_input_metadata_[frame];
    if (position != local_history_.end() && position->first == frame) {
        if (position->second == input) return;
        if (metadata.revision == UINT32_MAX) {
            fail_locked("The input sample revision limit was exceeded.");
            return;
        }
        position->second = input;
        if (metadata.sampled_at.time_since_epoch().count() && !metadata.consumed)
            ++superseded_local_samples_;
        ++metadata.revision;
        ++refreshed_input_samples_;
    } else {
        local_history_.insert(position, {frame, input});
    }
    metadata.sampled_at = physical_sample ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
    metadata.echoed = metadata.consumed = metadata.enqueued = false;
    timeline_.set_local(local_slot_, frame, input);
    // The host never needs to answer its own repair requests.
    while (is_host_ && local_history_.size() > 256U) {
        local_input_metadata_.erase(local_history_.front().first);
        local_history_.pop_front();
    }
}

void DirectSession::record_input_latency_locked(
    const protocol::FrameCommitPayload& commit, bool consumed) {
    const auto metadata = local_input_metadata_.find(commit.frame);
    if (metadata == local_input_metadata_.end() ||
        metadata->second.sampled_at.time_since_epoch().count() == 0) return;
    bool& recorded = consumed ? metadata->second.consumed : metadata->second.echoed;
    if (recorded) return;
    recorded = true;
    if ((commit.predicted_mask & (1U << local_slot_)) != 0) {
        if (consumed) ++predicted_local_samples_;
        return;
    }
    const auto sample = std::lower_bound(local_history_.begin(), local_history_.end(),
        commit.frame, [](const auto& entry, std::uint32_t value) { return entry.first < value; });
    if (sample == local_history_.end() || sample->first != commit.frame ||
        sample->second != commit.inputs[local_slot_]) {
        if (consumed) ++mismatched_local_samples_;
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - metadata->second.sampled_at).count();
    const auto ms = static_cast<std::uint32_t>((std::clamp<std::int64_t>)(elapsed, 0, UINT32_MAX));
    if (consumed) { local_input_consume_ms_ = ms; input_latency_->consume.observe(ms); }
    else { local_input_echo_ms_ = ms; ++measured_input_echoes_; input_latency_->echo.observe(ms); }
    // No cross-machine clock subtraction. The existing bounded recorder
    // samples once per second; no file I/O in this input path.
    if (commit.frame % 30U == 0U) failure_recorder().record(
        FailureEventKind::ProgressWatchdog, commit.frame, ms, local_slot_,
        consumed ? "local sample to commit consumption (ms)"
                 : "local sample to host commit echo (ms)");
}

protocol::InputBatch DirectSession::make_local_history_batch(
    std::uint32_t newest_frame, bool reliable_repair,
    std::optional<std::uint32_t> requested_first) const {
    protocol::InputBatch batch{};
    if (is_host_ || local_history_.empty()) return batch;
    batch.epoch = authority_epoch();
    batch.player_slot = local_slot_;
    if (!is_host_ && local_slot_ < kMaximumPlayers &&
        simulation_progress_present_[local_slot_]) {
        batch.simulation_progress_present = true;
        batch.simulation_scene_epoch =
            authoritative_phase_active_ &&
                    authority_lifecycle_ == AuthorityLifecycle::Racing
                ? scene_epoch_ : 0U;
        batch.simulation_completed_frame =
            simulation_completed_frame_[local_slot_];
    }
    // Always include a rolling recent window, even when an acknowledgement
    // says Player 1 was caught up. That makes an out-of-order/lost ACK benign
    // and guarantees that a later host stall can recover without relying on a
    // new physical sample from the parked simulation frame.
    constexpr std::uint32_t kRollingRedundancy = 16U;
    constexpr std::uint32_t kMaximumBatchInputs = 64U;
    const std::uint32_t rolling_oldest = newest_frame >= kRollingRedundancy - 1U
        ? newest_frame - (kRollingRedundancy - 1U) : 0U;
    const std::uint32_t retained_oldest = local_history_.front().first;
    const std::uint32_t requested_oldest = (std::min)(
        requested_first.value_or(host_input_first_missing_frame_),
        newest_frame);
    // Live packets carry the newest rolling samples and are intentionally
    // disposable. A repair packet instead starts at Player 1's exact
    // contiguous hole. Starting at newest-63 here used to skip an old missing
    // frame forever after a longer Wi-Fi burst, even though the requested
    // sample was still retained locally.
    const std::uint32_t oldest = reliable_repair
        ? (std::max)(retained_oldest, requested_oldest)
        : (std::max)(retained_oldest, rolling_oldest);
    for (const auto& [frame, input] : local_history_) {
        if (frame < oldest || frame > newest_frame) continue;
        if (batch.inputs.empty()) batch.first_frame = frame;
        if (frame != batch.first_frame + batch.inputs.size()) continue;
        batch.inputs.push_back(input);
        const auto metadata = local_input_metadata_.find(frame);
        batch.revisions.push_back(metadata == local_input_metadata_.end()
            ? 1U : metadata->second.revision);
        if (batch.inputs.size() == kMaximumBatchInputs) break;
    }
    return batch;
}

void DirectSession::send_local_history(std::uint32_t newest_frame,
                                       bool reliable_repair,
                                       std::optional<std::uint32_t>
                                           requested_first) {
    const protocol::InputBatch batch = make_local_history_batch(
        newest_frame, reliable_repair, requested_first);
    const auto payload = protocol::encode_input_batch(batch);
    if (payload.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < batch.inputs.size(); ++index) {
        const auto found = local_input_metadata_.find(batch.first_frame + static_cast<std::uint32_t>(index));
        if (found == local_input_metadata_.end()) continue;
        auto& sample = found->second;
        if (sample.enqueued || !sample.sampled_at.time_since_epoch().count()) continue;
        sample.enqueued = true;
        input_latency_->enqueue.observe(static_cast<std::uint32_t>(
            (std::clamp<std::int64_t>)(std::chrono::duration_cast<std::chrono::milliseconds>(
                now - sample.sampled_at).count(), 0, UINT32_MAX)));
    }
    send_to(host_address_, reliable_repair
            ? protocol::MessageType::InputRepair
            : protocol::MessageType::Input,
            payload, batch.first_frame);
}

bool DirectSession::ensure_local_input_for_repair_locked(
    std::uint32_t frame) {
    if (is_host_ || local_slot_ >= kMaximumPlayers ||
        state_ != ConnectionState::Running ||
        (gameplay_handoff_ && gameplay_handoff_->suspended)) {
        return false;
    }

    const auto position = std::lower_bound(
        local_history_.begin(), local_history_.end(), frame,
        [](const auto& entry, std::uint32_t requested) {
            return entry.first < requested;
        });
    if (position != local_history_.end() && position->first == frame) {
        return true;
    }

    // InputRepairRequest is authenticated as Player 1 and names the host's
    // first uncommitted hole. Never manufacture a large future timeline from a
    // malformed request; one retained repair batch is the maximum acceptable
    // forward distance. A normal lockstep starvation request is exactly one
    // frame beyond the newest local sample.
    constexpr std::uint32_t kMaximumRepairAdvance = 64U;
    if (frame > local_input_submitted_frame_ &&
        frame - local_input_submitted_frame_ > kMaximumRepairAdvance) {
        return false;
    }

    PackedInput held = local_input_submitted_;
    if (position != local_history_.begin()) {
        held = std::prev(position)->second;
    }
    store_local_input_locked(frame, held, false);
    if (frame > local_input_submitted_frame_) {
        local_input_submitted_frame_ = frame;
        local_input_submitted_ = held;
        pending_local_submission_frame_ = frame;
    }
    return true;
}

void DirectSession::request_missing_commit_locked(std::uint32_t frame) {
    if (is_host_ || !host_address_ || state_ != ConnectionState::Running) return;
    const auto payload = protocol::encode_frame_commit_request(
        {authority_epoch(), local_slot_, frame});
    if (payload.empty()) return;
    // Two small authenticated copies are cheaper than one full state fragment
    // and prevent an isolated loss from extending the visible pause by another
    // repair interval.
    for (int copy = 0; copy < 2; ++copy) {
        send_to(host_address_, protocol::MessageType::FrameCommitRequest,
                payload, frame);
    }
    ++commit_repair_requests_sent_;
}

void DirectSession::advertise_missing_inputs_locked() {
    if (!is_host_ || state_ != ConnectionState::Running) return;
    for (PeerRecord& peer : peers_) {
        if (!peer.active || peer.slot >= kMaximumPlayers) continue;
        const std::uint32_t first_missing =
            peer_input_first_missing_frame_[peer.slot];
        const auto payload = protocol::encode_input_repair_request(
            {authority_epoch(), peer.slot, first_missing});
        if (!payload.empty()) {
            send_with_key(peer.address, peer.key,
                          protocol::MessageType::InputRepairRequest, payload,
                          first_missing);
        }
    }
}

void DirectSession::reset_pending_commit_wait_locked() {
    if (pending_commit_wait_frame_) {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                      pending_commit_wait_started_);
        // Zero-duration availability checks occur on virtually every authored
        // tick and used to evict the useful lifecycle history from the bounded
        // flight recorder in under a minute. Retain only waits that crossed a
        // scheduler quantum; the progress watchdog separately records a frame
        // which remains parked.
        if (elapsed >= std::chrono::milliseconds(2)) {
            failure_recorder().record(
                FailureEventKind::FrameWaitEnd, *pending_commit_wait_frame_,
                static_cast<std::uint32_t>((std::min<std::int64_t>)(
                    elapsed.count(),
                    std::numeric_limits<std::uint32_t>::max())),
                0U, "authoritative input commit became available");
        }
    }
    pending_commit_wait_frame_.reset();
    pending_commit_wait_started_ = {};
    last_commit_repair_request_ = {};
}

protocol::FrameCommitPayload DirectSession::make_frame_commit_locked(
    std::uint32_t frame, std::uint16_t revision) {
    protocol::FrameCommitPayload commit{};
    commit.epoch = authority_epoch();
    commit.frame = frame;
    commit.revision = revision;
    commit.occupied_mask = launch_descriptor_
        ? launch_descriptor_->occupied_mask : 0U;
    commit.inputs = timeline_.inputs_for(frame);
    commit.predicted_mask = timeline_.predicted_mask(
        frame, commit.occupied_mask);
    for (std::size_t slot = 0U; slot < commit.inputs.size(); ++slot) {
        if ((commit.occupied_mask & static_cast<std::uint8_t>(1U << slot)) ==
            0U) {
            commit.inputs[slot] = {};
        }
    }
    commit.previous_hash = last_consumed_commit_hash_;
    commit.commit_hash = protocol::frame_commit_hash(match_id_, commit);
    return commit;
}

void DirectSession::rebuild_frame_commits_locked(
    std::uint32_t first_frame) {
    if (!is_host_ || !launch_descriptor_ ||
        launch_descriptor_->synchronization != SynchronizationMode::Rollback ||
        first_frame >= next_commit_frame_) {
        return;
    }
    // If another late input arrives before every peer acknowledges the prior
    // correction, supersede it with one transaction covering the union. A
    // client that lost generation N can therefore install generation N+1
    // directly without needing an unavailable intermediate hash-chain base.
    if (frame_correction_transmission_) {
        first_frame = (std::min)(
            first_frame, frame_correction_transmission_->first_frame);
    }
    auto first = frame_commits_.find(first_frame);
    if (first == frame_commits_.end()) {
        fail_locked("A late racer input fell outside Player 1's rollback history.");
        return;
    }

    std::uint64_t previous_hash = first->second.previous_hash;
    for (std::uint32_t frame = first_frame; frame < next_commit_frame_; ++frame) {
        auto found = frame_commits_.find(frame);
        if (found == frame_commits_.end()) {
            fail_locked("Player 1's rollback commit history has a missing frame.");
            return;
        }
        protocol::FrameCommitPayload revised{};
        revised.epoch = authority_epoch();
        revised.frame = frame;
        revised.revision = static_cast<std::uint16_t>(
            found->second.revision == UINT16_MAX
                ? UINT16_MAX : found->second.revision + 1U);
        revised.occupied_mask = launch_descriptor_->occupied_mask;
        revised.inputs = timeline_.inputs_for(frame);
        revised.predicted_mask = timeline_.predicted_mask(
            frame, revised.occupied_mask);
        for (std::size_t slot = 0U; slot < revised.inputs.size(); ++slot) {
            if (!launch_descriptor_->occupied(slot)) revised.inputs[slot] = {};
        }
        revised.previous_hash = previous_hash;
        revised.commit_hash = protocol::frame_commit_hash(match_id_, revised);
        found->second = revised;
        previous_hash = revised.commit_hash;
    }

    for (protocol::FrameCommitPayload& commit : commit_history_) {
        const auto found = frame_commits_.find(commit.frame);
        if (found != frame_commits_.end()) commit = found->second;
    }
    last_consumed_commit_hash_ = previous_hash;
    pending_input_correction_ = pending_input_correction_
        ? (std::min)(*pending_input_correction_, first_frame)
        : std::optional<std::uint32_t>{first_frame};
    ++input_corrections_;
    ++frame_correction_generation_;
    if (frame_correction_generation_ == 0U) ++frame_correction_generation_;
    FrameCorrectionTransmission transmission{};
    transmission.epoch = authority_epoch();
    transmission.generation = frame_correction_generation_;
    transmission.first_frame = first_frame;
    transmission.last_frame = next_commit_frame_ - 1U;
    transmission.acknowledgements[local_slot_] = true;
    transmission.commits.reserve(
        static_cast<std::size_t>(next_commit_frame_ - first_frame));
    for (std::uint32_t frame = first_frame; frame < next_commit_frame_;
         ++frame) {
        transmission.commits.push_back(frame_commits_.at(frame));
    }
    frame_correction_transmission_ = std::move(transmission);
    send_frame_correction_locked();
    worker_wake_ = true;
    state_changed_.notify_all();
}

void DirectSession::send_frame_correction_locked() {
    if (!is_host_ || !frame_correction_transmission_) return;
    FrameCorrectionTransmission& transmission =
        *frame_correction_transmission_;
    constexpr std::size_t kCommitsPerBatch = 16U;
    // Retransmit only to racers that have not acknowledged this generation.
    // A healthy client must not receive every correction burst repeatedly
    // because a different VPN route lost its acknowledgement.
    for (const PeerRecord& peer : peers_) {
        if (!peer.active || peer.slot >= kMaximumPlayers ||
            transmission.acknowledgements[peer.slot]) {
            continue;
        }
        for (std::size_t first = 0U; first < transmission.commits.size();
             first += kCommitsPerBatch) {
            const std::size_t last = (std::min)(
                first + kCommitsPerBatch, transmission.commits.size());
            protocol::FrameCommitBatch batch{};
            batch.generation = transmission.generation;
            batch.correction_first = transmission.first_frame;
            batch.correction_last = transmission.last_frame;
            batch.correction = true;
            batch.commits.assign(
                transmission.commits.begin() +
                    static_cast<std::ptrdiff_t>(first),
                transmission.commits.begin() +
                    static_cast<std::ptrdiff_t>(last));
            const auto payload = protocol::encode_frame_commit_batch(batch);
            if (!payload.empty()) {
                send_with_key(peer.address, peer.key,
                              protocol::MessageType::FrameCommit, payload,
                              batch.commits.back().frame);
            }
        }
    }
    transmission.last_send = std::chrono::steady_clock::now();
}

void DirectSession::send_commit_history(std::uint32_t newest_frame,
                                        std::size_t history_count) {
    if (!is_host_ || history_count == 0U) return;
    protocol::FrameCommitBatch batch{};
    batch.generation = frame_correction_generation_;
    history_count = (std::min<std::size_t>)(history_count, 64U);
    const std::uint32_t distance = static_cast<std::uint32_t>(
        history_count - 1U);
    const std::uint32_t oldest = newest_frame > distance
        ? newest_frame - distance : 0U;
    for (const protocol::FrameCommitPayload& commit : commit_history_) {
        if (commit.frame >= oldest && commit.frame <= newest_frame) {
            batch.commits.push_back(commit);
        }
    }
    const auto payload = protocol::encode_frame_commit_batch(batch);
    if (!payload.empty()) {
        broadcast(protocol::MessageType::FrameCommit, payload, newest_frame);
    }
}

void DirectSession::send_commit_history_to_peer(
    PeerRecord& peer, std::uint32_t first_frame,
    std::size_t maximum_count) {
    if (!is_host_ || !peer.active || maximum_count == 0U) return;
    maximum_count = (std::min<std::size_t>)(maximum_count, 64U);
    std::vector<protocol::FrameCommitPayload> repair;
    repair.reserve(maximum_count);
    std::uint32_t expected = first_frame;
    for (const protocol::FrameCommitPayload& commit : commit_history_) {
        if (commit.frame < first_frame) continue;
        if (commit.frame != expected || repair.size() >= maximum_count) break;
        repair.push_back(commit);
        if (expected == std::numeric_limits<std::uint32_t>::max()) break;
        ++expected;
    }
    if (repair.empty()) return;

    // Keep each authenticated UDP datagram below the protocol MTU. Sixteen
    // commits fit with ample envelope headroom and repeated requests naturally
    // walk a client through a longer missing range.
    constexpr std::size_t kCommitsPerBatch = 16U;
    for (std::size_t first = 0U; first < repair.size();
         first += kCommitsPerBatch) {
        const std::size_t last = (std::min)(
            first + kCommitsPerBatch, repair.size());
        protocol::FrameCommitBatch batch{};
        batch.generation = frame_correction_generation_;
        batch.commits.assign(
            repair.begin() + static_cast<std::ptrdiff_t>(first),
            repair.begin() + static_cast<std::ptrdiff_t>(last));
        const auto payload = protocol::encode_frame_commit_batch(batch);
        if (payload.empty()) continue;
        send_with_key(peer.address, peer.key,
                      protocol::MessageType::FrameCommit, payload,
                      batch.commits.back().frame);
        ++commit_repair_batches_sent_;
    }
}

bool DirectSession::apply_frame_commit_locked(
    const protocol::FrameCommitPayload& commit, std::string& error) {
    if (!validate_commit(commit, error)) return false;
    const auto existing = frame_commits_.find(commit.frame);
    if (existing != frame_commits_.end()) {
        if (existing->second == commit ||
            commit.revision < existing->second.revision) {
            error.clear();
            return true;
        }
        if (commit.revision == existing->second.revision) {
            error = "Player 1 published conflicting data for simulation frame " +
                    std::to_string(commit.frame) + ".";
            return false;
        }
        if (commit.frame < next_commit_frame_) {
            pending_input_correction_ = pending_input_correction_
                ? (std::min)(*pending_input_correction_, commit.frame)
                : std::optional<std::uint32_t>{commit.frame};
            ++input_corrections_;
        }
    }
    frame_commits_[commit.frame] = commit;
    record_input_latency_locked(commit, false);
    authoritative_input_frame_ = (std::max)(
        authoritative_input_frame_, commit.frame);
    authoritative_input_revision_ = commit.revision;
    authoritative_predicted_mask_ = commit.predicted_mask;
    ++simulation_wake_generation_;
    state_changed_.notify_all();
    error.clear();
    return true;
}

bool DirectSession::apply_frame_correction_locked(std::string& error) {
    if (!frame_correction_assembly_) {
        error = "The rollback correction transaction is missing.";
        return false;
    }
    FrameCorrectionAssembly& assembly = *frame_correction_assembly_;
    const std::uint64_t count = static_cast<std::uint64_t>(
        assembly.last_frame) - assembly.first_frame + 1U;
    if (count == 0U || count > 128U ||
        assembly.commits.size() != count) {
        error = "The rollback correction transaction is incomplete.";
        return false;
    }
    std::vector<protocol::FrameCommitPayload> ordered;
    ordered.reserve(static_cast<std::size_t>(count));
    // A client can receive a complete corrected generation before the
    // superseded normal datagram for its first not-yet-consumed frame. That
    // is legitimate UDP reordering. Anchor against the preceding local frame
    // when available; otherwise the authenticated transaction's own first
    // link is the only base needed because every following link is checked.
    std::uint64_t previous_hash =
        assembly.commits.at(assembly.first_frame).previous_hash;
    if (assembly.first_frame > 0U) {
        const auto predecessor = frame_commits_.find(
            assembly.first_frame - 1U);
        if (predecessor != frame_commits_.end()) {
            previous_hash = predecessor->second.commit_hash;
        }
    }
    for (std::uint32_t frame = assembly.first_frame;; ++frame) {
        const auto incoming = assembly.commits.find(frame);
        const auto existing = frame_commits_.find(frame);
        if (incoming == assembly.commits.end() ||
            incoming->second.previous_hash != previous_hash ||
            !validate_commit(incoming->second, error)) {
            if (error.empty()) {
                error = "Player 1's atomic rollback correction chain is invalid at frame " +
                        std::to_string(frame) + ".";
            }
            return false;
        }
        if (existing != frame_commits_.end() &&
            incoming->second.revision <= existing->second.revision) {
            error = "Player 1's rollback correction did not advance the revision at frame " +
                    std::to_string(frame) + ".";
            return false;
        }
        if (existing == frame_commits_.end() && frame < next_commit_frame_) {
            error = "The rollback correction fell outside local consumed commit history.";
            return false;
        }
        ordered.push_back(incoming->second);
        previous_hash = incoming->second.commit_hash;
        if (frame == assembly.last_frame) break;
    }

    // Nothing mutates until the complete range, hash chain and revisions have
    // all been validated. This is the atomic commit point.
    for (const protocol::FrameCommitPayload& commit : ordered) {
        frame_commits_[commit.frame] = commit;
    }
    for (protocol::FrameCommitPayload& commit : commit_history_) {
        const auto revised = frame_commits_.find(commit.frame);
        if (revised != frame_commits_.end()) commit = revised->second;
    }
    if (assembly.first_frame < next_commit_frame_) {
        pending_input_correction_ = pending_input_correction_
            ? (std::min)(*pending_input_correction_, assembly.first_frame)
            : std::optional<std::uint32_t>{assembly.first_frame};
        ++input_corrections_;
    }
    if (next_commit_frame_ > 0U) {
        const auto consumed = frame_commits_.find(next_commit_frame_ - 1U);
        if (consumed != frame_commits_.end()) {
            last_consumed_commit_hash_ = consumed->second.commit_hash;
        }
    }
    const std::uint32_t applied_generation = assembly.generation;
    applied_frame_correction_generation_ = applied_generation;
    frame_correction_assembly_.reset();

    std::vector<std::uint32_t> deferred_frames;
    deferred_frames.reserve(deferred_frame_commits_.size());
    for (const auto& [frame, deferred] : deferred_frame_commits_) {
        if (deferred.first <= applied_generation) deferred_frames.push_back(frame);
    }
    std::sort(deferred_frames.begin(), deferred_frames.end());
    for (const std::uint32_t frame : deferred_frames) {
        auto deferred = deferred_frame_commits_.find(frame);
        if (deferred == deferred_frame_commits_.end()) continue;
        if (!apply_frame_commit_locked(deferred->second.second, error)) return false;
        deferred_frame_commits_.erase(deferred);
    }

    const auto acknowledgement = protocol::encode_frame_correction_acknowledge(
        {authority_epoch(), applied_generation, local_slot_});
    for (int copy = 0; copy < 2; ++copy) {
        send_to(host_address_, protocol::MessageType::FrameCorrectionAck,
                acknowledgement, next_commit_frame_);
    }
    error.clear();
    return true;
}

bool DirectSession::send_authoritative_state(
    const PeerAddress* destination, std::uint32_t frame,
    std::span<const std::uint8_t> state, std::uint16_t requested_chunks,
    protocol::MessageType message_type, bool live_replica,
    const PreparedStateWire* prepared) {
    if (!is_host_ || state.empty() ||
        state.size() > kMaximumAuthoritativeStateBytes) return false;
    const std::size_t maximum_datagram_bytes =
        transport_->maximum_plaintext_datagram_bytes();
    constexpr std::size_t snapshot_headers = 32U + 24U;
    if (maximum_datagram_bytes <= snapshot_headers) return false;
    const std::size_t chunk_bytes = transport_->quick_join()
        ? (std::min)(kMaximumAuthoritativeStateBytes,
                     maximum_datagram_bytes - snapshot_headers)
        : 900U;
    std::vector<std::uint8_t> wire_state;
    if (prepared) wire_state = prepared->bytes;
    if (!prepared && !live_replica && checkpoint_encoding_ &&
        checkpoint_encoding_->epoch == scene_epoch_ && checkpoint_encoding_->frame == frame &&
        checkpoint_encoding_->source.size() == state.size() &&
        std::equal(state.begin(), state.end(), checkpoint_encoding_->source.begin())) {
        wire_state = checkpoint_encoding_->wire;
        ++checkpoint_encoding_cache_hits_;
    }
    if (!prepared && live_replica && live_replica_keyframe_frame_ &&
        *live_replica_keyframe_frame_ < frame &&
        !live_replica_keyframe_state_.empty()) {
        wire_state = encode_authoritative_state_delta_wire(
            state, live_replica_keyframe_state_,
            *live_replica_keyframe_frame_);
    }
    if (wire_state.empty()) {
        wire_state = encode_authoritative_state_wire(state);
        if (!live_replica && !wire_state.empty()) {
            checkpoint_encoding_ = CheckpointEncoding{scene_epoch_, frame,
                std::vector<std::uint8_t>(state.begin(), state.end()), wire_state};
        }
    }
    if (wire_state.empty() ||
        wire_state.size() > kMaximumAuthoritativeStateBytes) return false;
    const std::size_t count_size = (wire_state.size() + chunk_bytes - 1U) /
                                   chunk_bytes;
    if (count_size == 0U || count_size > 16U) return false;
    const auto count = static_cast<std::uint16_t>(count_size);
    const std::uint64_t checksum = prepared ? prepared->checksum : authoritative_state_checksum(wire_state);
    const std::uint32_t wire_frame = live_replica
        ? live_replica_wire_frame(frame) : frame;
    for (std::uint16_t index = 0U; index < count; ++index) {
        if ((requested_chunks & static_cast<std::uint16_t>(1U << index)) == 0U) {
            continue;
        }
        const std::size_t first = static_cast<std::size_t>(index) * chunk_bytes;
        const std::size_t length = (std::min)(chunk_bytes,
                                              wire_state.size() - first);
        protocol::StateSnapshotChunk chunk{};
        chunk.scene_epoch = scene_epoch_;
        chunk.frame = wire_frame;
        chunk.checksum = checksum;
        chunk.total_size = static_cast<std::uint32_t>(wire_state.size());
        chunk.chunk_index = index;
        chunk.chunk_count = count;
        chunk.bytes.assign(
            wire_state.begin() + static_cast<std::ptrdiff_t>(first),
            wire_state.begin() + static_cast<std::ptrdiff_t>(first + length));
        const auto payload = protocol::encode_state_snapshot_chunk(
            chunk, maximum_datagram_bytes);
        if (payload.empty()) return false;
        if (destination != nullptr) {
            if (!send_to(*destination, message_type, payload, wire_frame)) {
                return false;
            }
        } else {
            // Unlike generic lobby broadcasts, a checkpoint is required for
            // correctness. Surface any per-peer queue failure immediately
            // instead of reporting success after silently skipping a racer.
            for (const PeerRecord& peer : peers_) {
                if (!peer.active) continue;
                const auto acknowledged = authoritative_acknowledgements_.find(frame);
                if (!live_replica && acknowledged != authoritative_acknowledgements_.end() &&
                    acknowledged->second[peer.slot]) continue;
                if (!send_with_key(peer.address, peer.key,
                                   message_type, payload, wire_frame)) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::uint32_t DirectSession::authority_epoch_for_input_epoch(
    std::uint32_t input_epoch) const {
    std::uint32_t epoch = static_cast<std::uint32_t>(match_id_) ^
                          static_cast<std::uint32_t>(match_id_ >> 32U);
    if (launch_descriptor_) {
        epoch ^= static_cast<std::uint32_t>(
            launch_descriptor_->lobby_generation);
    }
    epoch ^= input_epoch * 0x9E3779B9U;
    return epoch != 0U ? epoch : 1U;
}

std::uint32_t DirectSession::authority_epoch() const {
    return authority_epoch_for_input_epoch(input_epoch_);
}

std::uint64_t DirectSession::initial_commit_hash() const {
    std::uint64_t seed = launch_descriptor_
        ? launch_descriptor_hash(*launch_descriptor_) : match_id_;
    seed ^= 0x444B522D4652414DULL; // "DKR-FRAM"
    return seed != 0U ? seed : 1U;
}

bool DirectSession::validate_commit(
    const protocol::FrameCommitPayload& commit, std::string& error) const {
    const std::uint8_t expected_mask = launch_descriptor_
        ? launch_descriptor_->occupied_mask : 0U;
    if (commit.epoch != authority_epoch() ||
        commit.occupied_mask != expected_mask ||
        (commit.predicted_mask & ~expected_mask) != 0U ||
        commit.commit_hash != protocol::frame_commit_hash(match_id_, commit)) {
        error = "Player 1's authoritative frame commit failed validation at frame " +
                std::to_string(commit.frame) + ".";
        return false;
    }
    for (std::size_t slot = 0U; slot < commit.inputs.size(); ++slot) {
        if ((expected_mask & static_cast<std::uint8_t>(1U << slot)) == 0U &&
            commit.inputs[slot] != PackedInput{}) {
            error = "Player 1's frame commit contains input for an unoccupied port.";
            return false;
        }
    }
    error.clear();
    return true;
}

void DirectSession::evaluate_state_hash(std::uint32_t frame) {
    const auto found = state_hashes_.find(frame);
    if (found == state_hashes_.end()) return;
    const HashFrame& hashes = found->second;
    const Room& room = is_host_ ? lobby_.room() : room_view_;
    if (!hashes.present[0]) return;
    bool complete = true;
    for (std::size_t slot = 0; slot < room.players.size(); ++slot) {
        if (!room.players[slot].occupied) continue;
        if (!hashes.present[slot]) {
            complete = false;
            continue;
        }
        if (hashes.values[slot] != hashes.values[0]) {
            std::string subsystem = "combined gameplay state";
            if (hashes.globals[0] != 0U &&
                hashes.globals[slot] != hashes.globals[0]) {
                subsystem = "global simulation/RNG state";
            } else if (hashes.roster[0] != 0U &&
                       hashes.roster[slot] != hashes.roster[0]) {
                subsystem = "player roster state";
            } else if (hashes.racers[0] != 0U &&
                       hashes.racers[slot] != hashes.racers[0]) {
                subsystem = "racer physics and race-progress state";
            }
            write_determinism_artifact_locked(frame, slot, subsystem, hashes);
            if (authoritative_phase_active_) {
                // Normal gameplay has exactly one authority: Player 1's
                // immutable frame-commit ledger. A digest mismatch proves that
                // some non-input authored state escaped that ledger. Schedule
                // one future checkpoint rather than pretending a removed live
                // replica stream will repair it or rewriting committed history.
                ++authoritative_corrections_;
                schedule_recovery_locked(frame, subsystem);
            } else {
                // Frontend presentation owns asynchronous music, animation
                // and menu objects that are intentionally outside the
                // portable gameplay state contract. A digest difference here
                // is useful diagnostics, but it is not grounds to destroy an
                // otherwise healthy lobby: the next track load installs
                // Player 1's complete authenticated gameplay baseline before
                // frame zero is allowed to run.
                status_ = "A frontend state difference was observed for Player " +
                          std::to_string(slot + 1U) +
                          "; Player 1 will resynchronize the next track baseline.";
            }
            return;
        }
    }
    if (complete) last_verified_frame_ = (std::max)(last_verified_frame_, frame);
}

void DirectSession::write_determinism_artifact_locked(
    std::uint32_t frame, std::size_t mismatched_slot,
    std::string_view subsystem, const HashFrame& hashes) {
    if (artifact_directory_.empty()) return;
    const std::filesystem::path path = artifact_directory_ /
        ("determinism-frame-" + std::to_string(frame) + "-player-" +
         std::to_string(mismatched_slot + 1U) + ".txt");
    std::ostringstream stream;
    stream << "protocol=" << kProtocolVersion << '\n'
           << "scene_epoch=" << scene_epoch_ << '\n'
           << "frame=" << frame << '\n'
           << "next_commit_frame=" << next_commit_frame_ << '\n'
           << "subsystem=" << subsystem << '\n'
           << "host_combined=" << hashes.values[0] << '\n'
           << "peer_combined=" << hashes.values[mismatched_slot] << '\n'
           << "host_globals=" << hashes.globals[0] << '\n'
           << "peer_globals=" << hashes.globals[mismatched_slot] << '\n'
           << "host_roster=" << hashes.roster[0] << '\n'
           << "peer_roster=" << hashes.roster[mismatched_slot] << '\n'
           << "host_racers=" << hashes.racers[0] << '\n'
           << "peer_racers=" << hashes.racers[mismatched_slot] << '\n'
           << "host_racer_count=" << hashes.racer_counts[0] << '\n'
           << "peer_racer_count=" << hashes.racer_counts[mismatched_slot]
           << '\n';
    const std::size_t comparable_racers = (std::min)(
        static_cast<std::size_t>(hashes.racer_counts[0]),
        static_cast<std::size_t>(hashes.racer_counts[mismatched_slot]));
    for (std::size_t racer = 0U;
         racer < comparable_racers && racer < 10U; ++racer) {
        if (hashes.racer_details[0][racer] ==
            hashes.racer_details[mismatched_slot][racer]) continue;
        stream << "first_different_racer=" << racer << '\n'
               << "host_racer_detail="
               << hashes.racer_details[0][racer] << '\n'
               << "peer_racer_detail="
               << hashes.racer_details[mismatched_slot][racer] << '\n';
        break;
    }
    if (replay_writes_.size() >= 8U) {
        replay_write_error_ = "The diagnostic writer is busy; an additional determinism report was not queued.";
        return;
    }
    if (!replay_writer_started_) {
        replay_writer_.start([] {}, [] {});
        replay_writer_started_ = true;
    }
    auto result = std::make_shared<std::promise<std::string>>();
    replay_writes_.push_back(result->get_future());
    if (!replay_writer_.post([path, text = stream.str(), result] {
            try {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::trunc);
                output << text;
                output.close();
                result->set_value(output ? std::string{} : "Could not write the determinism diagnostic.");
            } catch (...) {
                result->set_value("Could not write the determinism diagnostic.");
            }
        })) result->set_value("The determinism diagnostic writer is unavailable.");
}

void DirectSession::schedule_recovery_locked(std::uint32_t mismatch_frame,
                                             std::string subsystem) {
    if (!is_host_ || recovery_frame_ || state_ != ConnectionState::Running ||
        !authoritative_phase_active_ ||
        authority_lifecycle_ != AuthorityLifecycle::Racing) return;
    const std::uint32_t recovery_runway_frames =
        (std::max)(6U, static_cast<std::uint32_t>(input_delay_) + 3U);
    const std::uint32_t earliest = (std::max)(
        next_commit_frame_ + recovery_runway_frames,
        mismatch_frame + recovery_runway_frames);
    const std::uint32_t boundary =
        ((earliest + kAuthorityCheckpointInterval - 1U) /
         kAuthorityCheckpointInterval) * kAuthorityCheckpointInterval;
    recovery_frame_ = boundary;
    recovery_stage_ = RecoveryStage::Scheduled;
    recovery_acks_ = {};
    recovery_resumed_ = false;
    last_recovery_broadcast_ = {};
    status_ = "A " + subsystem +
              " mismatch was detected; preparing a synchronized recovery.";
    failure_recorder().record(
        FailureEventKind::RecoveryRequested, boundary, mismatch_frame,
        scene_epoch_, subsystem);
    const auto payload = protocol::encode_recovery(
        {scene_epoch_, boundary, 0U});
    for (int copy = 0; copy < 3; ++copy) {
        broadcast(protocol::MessageType::RecoveryBegin, payload, boundary);
    }
    worker_wake_ = true;
    state_changed_.notify_all();
}

void DirectSession::send_connection_probes() {
    const auto now = std::chrono::steady_clock::now();
    const auto now_us = steady_microseconds();
    for (PeerRecord& peer : peers_) {
        if (!peer.active) continue;
        for (auto& pending : peer.pending_ping_tokens) {
            // This measures expired application probes, not wire packet loss.
            // Keep earlier probes alive across the one-second sampling tick.
            if (pending != 0U && now_us >= pending &&
                now_us - pending >= 3000000U) {
                pending = 0U;
                peer.loss_percent = peer.loss_percent * 0.8F + 20.0F;
            }
        }
        if (peer.last_ping.time_since_epoch().count() != 0 &&
            now - peer.last_ping < std::chrono::seconds(1)) continue;
        auto available = std::find(peer.pending_ping_tokens.begin(),
                                   peer.pending_ping_tokens.end(), 0U);
        if (available == peer.pending_ping_tokens.end()) continue;
        peer.last_ping = now;
        peer.ping_token = (std::max)(now_us, peer.ping_token + 1U);
        if (send_to(peer.address, protocol::MessageType::Ping,
                    encode_probe(peer.ping_token))) {
            *available = peer.ping_token;
            ++peer.pings_sent;
        }
    }
}

void DirectSession::update_peer_metrics(PeerRecord& peer, std::uint64_t token) {
    if (token == 0U) return;
    auto pending = std::find(peer.pending_ping_tokens.begin(),
                             peer.pending_ping_tokens.end(), token);
    if (pending == peer.pending_ping_tokens.end()) return;
    const std::uint64_t now = steady_microseconds();
    if (now < token) return;
    *pending = 0U; // Duplicate replies cannot produce another sample.
    if (now - token >= 3000000U) {
        peer.loss_percent = peer.loss_percent * 0.8F + 20.0F;
        return;
    }
    peer.loss_percent *= 0.8F;
    const double measured = static_cast<double>(now - token) / 1000.0;
    const double delta = peer.pings_received == 0U
        ? 0.0 : std::abs(measured - peer.rtt_ms);
    peer.rtt_ms = measured;
    peer.jitter_ms = peer.pings_received == 0U
        ? 0.0 : peer.jitter_ms * 0.75 + delta * 0.25;
    peer.rtt_samples[peer.rtt_sample_cursor] = measured;
    peer.rtt_sample_times[peer.rtt_sample_cursor] = std::chrono::steady_clock::now();
    peer.rtt_sample_cursor =
        (peer.rtt_sample_cursor + 1U) % peer.rtt_samples.size();
    peer.rtt_sample_count = (std::min)(peer.rtt_sample_count + 1U,
                                      peer.rtt_samples.size());
    ++peer.pings_received;
    lobby_.set_network_metrics(std::to_string(peer.sender_id), Route::Direct,
        static_cast<std::uint16_t>(std::clamp(peer.rtt_ms, 0.0, 65535.0)),
        static_cast<std::uint16_t>(std::clamp(peer.jitter_ms, 0.0, 65535.0)),
        peer.loss_percent);
    room_view_ = lobby_.room();
    broadcast_lobby();
}

void DirectSession::queue_diagnostics_locked(std::string_view reason) {
    if (replay_writes_.size() >= 8U) return;
    if (!replay_writer_started_) {
        replay_writer_.start([] {}, [] {});
        replay_writer_started_ = true;
    }
    auto result = std::make_shared<std::promise<std::string>>();
    auto text = failure_recorder().snapshot_text();
    // Bounded, privacy-safe statistics at the existing diagnostic boundary,
    // never per-packet disk I/O or timestamps from different machines.
    std::ostringstream metrics;
    const auto append_latency = [&metrics](const char* name, const LatencyHistogram& histogram) {
        const auto value = histogram.summary();
        metrics << "[netplay][latency] " << name << " samples=" << value.samples
                << " p50/p95/p99/max-ms=" << value.p50_ms << '/' << value.p95_ms
                << '/' << value.p99_ms << '/' << value.maximum_ms << '\n';
    };
    append_latency("sample-to-application-enqueue", input_latency_->enqueue);
    append_latency("sample-to-commit-echo", input_latency_->echo);
    append_latency("sample-to-local-consumption", input_latency_->consume);
    metrics << "[netplay][latency] predicted=" << predicted_local_samples_
            << " mismatched=" << mismatched_local_samples_
            << " superseded=" << superseded_local_samples_
            << " maximum-pump-us=" << maximum_network_pump_us_ << '\n';
    const auto queue_now = std::chrono::steady_clock::now();
    unsigned lane = 0;
    for (const auto* queue : {&critical_outbound_, &repair_outbound_, &commit_outbound_,
             &high_priority_outbound_, &authority_outbound_, &normal_priority_outbound_, &bulk_outbound_}) {
        std::size_t bytes = 0;
        std::int64_t age_ms = 0;
        for (const auto& packet : *queue) {
            bytes += packet.bytes.size();
            age_ms = (std::max)(age_ms, static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(queue_now - packet.enqueued).count()));
        }
        metrics << "[netplay][queue] lane=" << lane++ << " packets=" << queue->size()
                << " bytes=" << bytes << " oldest-ms=" << age_ms << '\n';
    }
    text += metrics.str();
    if (!reason.empty()) text = "[netplay][failure] " + std::string(reason) + "\n" + text;
    replay_writes_.push_back(result->get_future());
    if (!replay_writer_.post([text = std::move(text), result] {
        std::fwrite(text.data(), 1U, text.size(), stderr);
        std::fflush(stderr);
        result->set_value({});
    })) result->set_value("Diagnostic writer queue was full.");
}

void DirectSession::fail_locked(std::string message) {
    if (state_ == ConnectionState::Failed) return;
    failure_recorder().record(FailureEventKind::SessionFailure,
                              next_commit_frame_,
                              static_cast<std::uint32_t>(state_),
                              scene_epoch_, message);
    queue_diagnostics_locked(message);
    failure_payload_.assign(message.begin(), message.end());
    // Mark failure before the best-effort disconnect broadcast. If the queue
    // itself caused this failure, enqueue_outbound must not recurse back into
    // a second failure transition.
    state_ = ConnectionState::Failed;
    status_ = message;
    if (is_host_) broadcast(protocol::MessageType::Disconnect, failure_payload_);
    else if (host_address_) {
        send_to(host_address_, protocol::MessageType::Disconnect,
                failure_payload_);
    }
    const auto now = std::chrono::steady_clock::now();
    last_failure_broadcast_ = now;
    failure_broadcast_until_ = now + std::chrono::seconds(2);
    // Replay compression and filesystem I/O are finalized by disconnect(),
    // outside packet dispatch. Doing that work while fail_locked() owns the
    // session mutex can starve the render/overlay thread and turn a recoverable
    // network error into an apparent hard black-screen hang.
    state_changed_.notify_all();
}

void DirectSession::synchronize_peer_slots() {
    std::array<PeerRecord, kMaximumPlayers> normalized{};
    for (const PeerRecord& peer : peers_) {
        if (!peer.active) continue;
        const std::string peer_id = std::to_string(peer.sender_id);
        const auto found = std::find_if(lobby_.room().players.begin(),
            lobby_.room().players.end(), [&](const Player& player) {
                return player.occupied && player.peer_id == peer_id;
            });
        if (found == lobby_.room().players.end() || found->slot == 0U) continue;
        PeerRecord moved = peer;
        moved.slot = found->slot;
        normalized[moved.slot] = moved;
    }
    peers_ = normalized;
}

LaunchDescriptor DirectSession::make_launch_descriptor() const {
    const Room& room = lobby_.room();
    LaunchDescriptor descriptor{};
    descriptor.match_id = match_id_;
    descriptor.lobby_generation = room.generation;
    descriptor.compatibility_hash = manifest_hash(manifest_);
    descriptor.input_delay_frames = input_delay_;
    descriptor.synchronization = room.rules.synchronization;
    descriptor.rollback_window =
        synchronization_has_prediction_window(room.rules.synchronization)
            ? room.rules.rollback_window
            : 0U;
    descriptor.host_control = room.rules.host_control;
    for (std::size_t slot = 0; slot < room.players.size(); ++slot) {
        if (!room.players[slot].occupied) continue;
        descriptor.occupied_mask |= static_cast<std::uint8_t>(1U << slot);
        ++descriptor.player_count;
    }
    return descriptor;
}

bool DirectSession::validate_start_descriptor(
    const protocol::StartPayload& payload, std::string& error) const {
    const LaunchDescriptor& descriptor = payload.descriptor;
    if (const char* reason = experimental::runtime_admission_error(descriptor.synchronization, owned_backend_available_)) {
        error = reason;
        return false;
    }
    if (!valid_launch_descriptor(descriptor) ||
        payload.descriptor_hash != launch_descriptor_hash(descriptor) ||
        descriptor.match_id != match_id_ ||
        descriptor.compatibility_hash != manifest_hash(manifest_) ||
        descriptor.synchronization != room_view_.rules.synchronization ||
        descriptor.rollback_window !=
            (synchronization_has_prediction_window(room_view_.rules.synchronization)
                 ? room_view_.rules.rollback_window
                 : 0U) ||
        local_slot_ >= kMaximumPlayers || !descriptor.occupied(local_slot_)) {
        error = "The synchronized start does not match this racer or build.";
        return false;
    }
    std::uint8_t room_mask = 0U;
    for (std::size_t slot = 0; slot < room_view_.players.size(); ++slot) {
        if (room_view_.players[slot].occupied) {
            room_mask |= static_cast<std::uint8_t>(1U << slot);
        }
    }
    if (room_mask != descriptor.occupied_mask) {
        error = "The lobby roster changed before the synchronized start.";
        return false;
    }
    if (launch_descriptor_ && *launch_descriptor_ != descriptor) {
        error = "Player 1 sent conflicting synchronized start rosters.";
        return false;
    }
    error.clear();
    return true;
}

bool DirectSession::accept_start_descriptor(
    const protocol::StartPayload& payload, std::string& error) {
    if (!validate_start_descriptor(payload, error)) return false;
    const LaunchDescriptor& descriptor = payload.descriptor;
    const bool first_descriptor = !launch_descriptor_.has_value();
    launch_descriptor_ = descriptor;
    input_delay_ = descriptor.input_delay_frames;
    if (first_descriptor) {
        if (descriptor.synchronization == SynchronizationMode::ExperimentalRollback) {
            owned_last_host_packet_ = std::chrono::steady_clock::now();
            owned_inbound_.clear(); owned_inbound_bytes_ = 0;
        }
        timeline_.reset();
        clear_input_history_locked();
        frame_commits_.clear();
        commit_history_.clear();
        authoritative_states_.clear();
        authoritative_acknowledgements_.clear();
        snapshot_assemblies_.clear();
        live_replica_states_.clear();
        racer_orientation_states_.clear();
        live_replica_assemblies_.clear();
        live_replica_keyframe_frame_.reset();
        live_replica_keyframe_state_.clear();
        authoritative_phase_active_ = false;
        state_hashes_.clear();
        last_verified_frame_ = 0U;
        last_authoritative_frame_ = 0U;
        authoritative_corrections_ = 0U;
        consecutive_authoritative_frames_ = 0U;
        next_commit_frame_ = 0U;
        last_consumed_input_frame_.reset();
        last_consumed_inputs_ = {};
        gameplay_handoff_.reset();
        last_gameplay_handoff_send_ = {};
        input_epoch_ = 1U;
        retired_authority_epoch_ = 0U;
        pending_authority_epoch_ = 0U;
        stale_epoch_packets_ = 0U;
        future_epoch_packets_ = 0U;
        last_consumed_commit_hash_ = initial_commit_hash();
        reset_input_delivery_tracking_locked(0U);
    }
    error.clear();
    return true;
}

bool DirectSession::save_sync_available() const {
    if (session_save_.size() != 512U || session_save_generation_ == 0U) {
        return false;
    }
    return manifest_.session_save_hash == stable_hash(std::string_view(
        reinterpret_cast<const char*>(session_save_.data()),
        session_save_.size()));
}

std::string DirectSession::admission_incompatibility(
    const CompatibilityManifest& candidate) const {
    // This comparison only establishes eligibility to review/download. The
    // actual slot allocation still requires complete_mod_admission's proof.
    const std::string other = session_content_sync_incompatibility(manifest_,candidate,
        mod_manifest_bytes_ && !manifest_.mod_manifest_hash.empty());
    if (!other.empty()) return other;
    if (candidate.session_save_hash != manifest_.session_save_hash &&
        !save_sync_available()) {
        return "The host session save differs and Player 1 has no validated save to synchronize.";
    }
    return {};
}

} // namespace dkr::runtime::netplay
