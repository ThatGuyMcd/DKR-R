#include "direct_session.hpp"
#include "online_input_broker.hpp"
#include "progress_budget.hpp"
#include "rollback_policy.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#define DKR_TEST_NOINLINE __declspec(noinline)
#else
#define DKR_TEST_NOINLINE __attribute__((noinline))
#endif

namespace dkr::runtime::netplay {

class BlockingRealtimeTransport : public SessionTransport {
public:
    bool open(std::uint16_t, std::string&) override { return true; }
    void close() override {}
    bool is_open() const override { return true; }
    std::uint16_t local_port() const override { return 0U; }
    DatagramSendStatus send_status(
        const PeerAddress&, std::span<const std::uint8_t>,
        TransportTrafficClass traffic, std::string&) override {
        if (traffic == TransportTrafficClass::Realtime) {
            ++realtime_attempts;
            return DatagramSendStatus::WouldBlock;
        }
        if (traffic == TransportTrafficClass::Authoritative) {
            ++authoritative_sends;
        }
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress&, std::vector<std::uint8_t>&,
                 std::string&) override {
        return false;
    }

    std::size_t realtime_attempts = 0U;
    std::size_t authoritative_sends = 0U;
};

struct DirectSessionTestAccess {
    static bool launch_pending(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.launch_requested_;
    }
    static bool resume_arm_announced(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.gameplay_barrier_ && session.gameplay_barrier_->arm_announced;
    }

    // Fixtures that inject state without a real transport or peers must keep
    // the live network worker from observing that deliberately incomplete
    // session; otherwise it runs gameplay and timeout paths against it.
    static void stop_worker(DirectSession& session) {
        if (!session.network_worker_.joinable()) return;
        {
            // Match production shutdown: change the wait predicate under its
            // mutex so a notification cannot slip between predicate and wait.
            std::scoped_lock lock(session.mutex_);
            session.worker_stop_.store(true, std::memory_order_release);
        }
        session.state_changed_.notify_all();
        session.network_worker_.join();
    }
    DKR_TEST_NOINLINE static void owned_lane_isolation() {
        DirectSession receiver;
        stop_worker(receiver);
        assert(receiver.enable_owned_backend(true));
        assert(receiver.owned_backend_available());
        {
            std::scoped_lock lock(receiver.mutex_);
            receiver.state_=ConnectionState::Loading;receiver.local_slot_=0;receiver.is_host_=true;
            LaunchDescriptor descriptor{};descriptor.occupied_mask=3;descriptor.player_count=2;
            descriptor.synchronization=SynchronizationMode::Lockstep;receiver.launch_descriptor_=descriptor;
            protocol::Datagram packet{};packet.header.type=protocol::MessageType::ExperimentalInput;packet.payload={1,2,3};
            assert(receiver.receive_owned_packet_locked(1,packet)&&receiver.owned_inbound_.empty());
            receiver.launch_descriptor_->synchronization=SynchronizationMode::ExperimentalRollback;
            assert(receiver.receive_owned_packet_locked(2,packet)&&receiver.owned_inbound_.empty());
            assert(receiver.receive_owned_packet_locked(255,packet)&&receiver.owned_inbound_.empty());
            assert(receiver.receive_owned_packet_locked(0,packet)&&receiver.owned_inbound_.empty());
            const auto launch_hash=launch_descriptor_hash(*receiver.launch_descriptor_);
            packet.payload.clear();
            for(int shift=56;shift>=0;shift-=8)packet.payload.push_back(std::uint8_t(launch_hash>>shift));
            packet.payload.insert(packet.payload.end(),{1,2,3});
            for(auto type:{protocol::MessageType::ExperimentalInput,protocol::MessageType::ExperimentalControl,protocol::MessageType::ExperimentalRepair}) {
                packet.header.type=type;assert(receiver.receive_owned_packet_locked(1,packet));
            }
            assert(receiver.owned_inbound_.size()==3&&receiver.owned_inbound_bytes_==9);
        }
        DirectSession::OwnedPacket taken;
        assert(receiver.take_owned_packet(taken)&&taken.source==1&&taken.traffic==TransportTrafficClass::Realtime);
        assert(receiver.take_owned_packet(taken)&&taken.traffic==TransportTrafficClass::Control);
        assert(receiver.take_owned_packet(taken)&&taken.traffic==TransportTrafficClass::Authoritative);
        assert(!receiver.take_owned_packet(taken));
        std::string error;
        assert(receiver.send_owned_packet(255,std::array<std::uint8_t,1>{1},TransportTrafficClass::Realtime,error)==DatagramSendStatus::Error);
        assert(!receiver.enable_owned_backend(false)); // capability is immutable during a launch
        receiver.disconnect();assert(!receiver.take_owned_packet(taken));
        assert(receiver.enable_owned_backend(false)&&!receiver.owned_backend_available());
    }
    DKR_TEST_NOINLINE static void experimental_admission_isolation() {
        DirectSession receiver;
        protocol::StartPayload start{};
        start.descriptor.synchronization = SynchronizationMode::ExperimentalRollback;
        std::string error;
        std::scoped_lock lock(receiver.mutex_);
        assert(!receiver.accept_start_descriptor(start, error));
        // v1.0.11 (rollback-only): Rollback is the only mode; without the
        // owned engines it reports the build as incomplete.
        assert(error.find("Rollback is unavailable in this build") != std::string::npos);
        assert(!receiver.launch_descriptor_);
        assert(receiver.launch_stage_ == LaunchStage::Idle);
    }
    DKR_TEST_NOINLINE static void experimental_host_preserves_lobby(DirectSession& host,
                                                                   const Rules& rules) {
        // Keep these large view temporaries out of main's existing multi-peer
        // fixture stack frame, particularly on Windows' 1 MiB default stack.
        const auto before = host.view();
        // v1.0.11 (rollback-only): the inadmissible request is a legacy sync
        // mode now; Rollback itself is admitted once the owned engines exist.
        Rules experiment = rules;
        experiment.synchronization = SynchronizationMode::Lockstep;
        experiment.rollback_window = 0U;
        std::string error;
        assert(!host.host(0U, "127.0.0.1", "Experiment", ConnectionMethod::Lan,
                          "Host", experiment, error));
        assert(error == rollback_policy::unsupported_lobby);
        const auto after = host.view();
        assert(after.state == before.state);
        assert(after.match_id == before.match_id);
        assert(after.invite == before.invite);
        assert(after.local_port == before.local_port);
        assert(after.room.rules == before.room.rules);
        assert(after.online_save_ready == before.online_save_ready);
    }
    static std::uint32_t authority_epoch(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.authority_epoch();
    }
    static void input_freshness_regression() {
        DirectSession client;
        stop_worker(client);
        prepare_fast_forward_guest(client, 100U, 1U);
        client.frame_commits_.clear();
        client.input_delay_ = 1U;
        client.authoritative_inputs_valid_ = true;
        client.authoritative_input_frame_ = 100U;
        FrameInputs inputs{};
        assert(client.synchronize_inputs_result(100U, {}, inputs,
            std::chrono::milliseconds(0)) == InputSynchronizationResult::Pending);
        const PackedInput pressed{0x8000U, 40, -20};
        assert(client.synchronize_inputs_result(100U, pressed, inputs,
            std::chrono::milliseconds(0)) == InputSynchronizationResult::Pending);
        assert(client.local_history_.size() == 1U);
        for (bool repair : {false, true}) {
            const auto batch = client.make_local_history_batch(102U, repair, 102U);
            assert(batch.inputs.size() == 1U && batch.inputs.front() == pressed);
        }
        PeerAddress destination{}; destination.size = 1U; destination.storage[0] = 1U;
        client.high_priority_outbound_.clear();
        for (unsigned i = 0; i < 90U; ++i)
            client.enqueue_outbound(destination, protocol::MessageType::Input, {1U}, i);
        assert(client.high_priority_outbound_.size() == 1U);
        assert(client.high_priority_outbound_.front().frame == 89U);
    }
    static void online_save_status_regression() {
        DirectSession client;
        stop_worker(client); // drives *_locked and the outbound queues directly
        client.is_host_ = false;
        client.state_ = ConnectionState::Lobby;
        client.local_slot_ = 1U;
        client.session_save_generation_ = 9U;
        client.manifest_.session_save_hash = 1234U;
        client.room_view_.generation = 7U;
        for (auto& player : client.room_view_.players) player.occupied = true;
        protocol::OnlineSaveStatusPayload status{7U, 9U, 1234U, 15U};
        client.apply_online_save_status_locked(status);
        assert(!client.local_online_save_acknowledged_ && !client.online_save_status_); // not installed
        client.local_online_save_ready_ = true;
        for (const auto invalid : {protocol::OnlineSaveStatusPayload{7U, 8U, 1234U, 15U},
                 {7U, 9U, 4567U, 15U}, {6U, 9U, 1234U, 15U}}) {
            client.apply_online_save_status_locked(invalid);
            assert(!client.local_online_save_acknowledged_ && !client.online_save_status_);
        }
        // Lost individual ReadyAck is repaired by the same authenticated
        // host confirmation in a periodic roster status packet.
        client.apply_online_save_status_locked(status);
        assert(client.local_online_save_acknowledged_);
        for (const bool ready : client.view().online_save_ready) assert(ready);
        status.verified_mask = 1U;
        client.apply_online_save_status_locked(status); // reordered older mask
        for (const bool ready : client.view().online_save_ready) assert(ready);
        // A leave/rejoin uses a new roster generation. The former player's
        // verification cannot be inherited by a new occupant of the slot.
        protocol::LobbyStatePayload roster{};
        roster.generation = 8U;
        roster.phase = RoomPhase::Waiting;
        for (std::size_t i = 0; i < roster.players.size(); ++i) {
            roster.players[i].occupied = true;
            roster.players[i].slot = static_cast<std::uint8_t>(i);
            roster.players[i].display_name = "Racer";
        }
        client.apply_lobby_payload(roster);
        assert(!client.view().online_save_ready[2]);
        status.verified_mask = 15U;
        client.apply_online_save_status_locked(status); // stale generation 7
        assert(!client.view().online_save_ready[2]);
        status.room_generation = 8U; status.verified_mask = 11U;
        client.apply_online_save_status_locked(status);
        assert(!client.view().online_save_ready[2] && client.view().online_save_ready[3]);
        // Future status can precede LobbyState; never display its bits early.
        client.local_online_save_acknowledged_ = false;
        status.room_generation = 9U; status.verified_mask = 15U;
        client.apply_online_save_status_locked(status);
        assert(!client.local_online_save_acknowledged_ && !client.view().online_save_ready[2]);
        roster.generation = 9U;
        client.apply_lobby_payload(roster);
        for (const bool ready : client.view().online_save_ready) assert(ready);
        // Status remains advisory and latest-wins under send backpressure.
        PeerAddress destination{}; destination.size = 1U; destination.storage[0] = 1U;
        for (unsigned i = 0; i < 1000; ++i)
            client.enqueue_outbound(destination, protocol::MessageType::OnlineSaveStatus, {1U});
        assert(client.normal_priority_outbound_.size() == 1U && client.critical_outbound_.empty());
        client.disconnect();
        assert(!client.online_save_status_);
    }
    static void remaining_gaps_regression() {
        DirectSession host;
        {
            std::scoped_lock lock(host.mutex_);
            host.is_host_ = true;
            host.state_ = ConnectionState::Running;
            host.match_id_ = 123U;
            host.scene_epoch_ = 7U;
            host.next_commit_frame_ = 101U;
            host.authoritative_phase_active_ = true;
            host.authority_lifecycle_ = DirectSession::AuthorityLifecycle::Racing;
            host.launch_descriptor_ = LaunchDescriptor{};
            host.launch_descriptor_->occupied_mask = 3U;
            host.launch_descriptor_->player_count = 2U;
            PeerAddress address{}; address.size = 1U; address.storage[0] = 1U;
            host.peers_[1] = {true, 2U, address, 1U, secure::generate_key()};
            protocol::FrameCommitPayload commit{};
            commit.epoch = host.authority_epoch(); commit.frame = 100U;
            commit.occupied_mask = 3U;
            commit.commit_hash = protocol::frame_commit_hash(host.match_id_, commit);
            host.commit_history_.push_back(commit);
            protocol::Datagram request{};
            request.header.type = protocol::MessageType::FrameCommitRequest;
            request.header.frame = 100U;
            request.payload = protocol::encode_frame_commit_request({host.authority_epoch(), 1U, 100U});
            for (auto lifecycle : {DirectSession::AuthorityLifecycle::Racing,
                                   DirectSession::AuthorityLifecycle::SealingFinish}) {
                host.authority_lifecycle_ = lifecycle;
                host.finish_seal_frame_ = 100U;
                host.commit_outbound_.clear();
                host.handle_host_packet(address, 2U, request);
                assert(host.commit_outbound_.size() == 1U);
            }
            host.authoritative_phase_active_ = false;
            host.authority_lifecycle_ = DirectSession::AuthorityLifecycle::Inactive;
            host.gameplay_handoff_ = DirectSession::GameplayHandoffState{
                host.input_epoch_, host.input_epoch_ + 1U, 101U, 101U, 12U, true, true, false,
                std::chrono::steady_clock::now() + std::chrono::seconds(90)};
            host.commit_outbound_.clear();
            host.handle_host_packet(address, 2U, request);
            assert(host.commit_outbound_.size() == 1U);
            host.critical_outbound_.clear();
            for (unsigned i = 0; i < 1000; ++i)
                host.send_gameplay_handoff_locked(protocol::GameplayHandoffStage::Suspend);
            assert(host.state_ == ConnectionState::Running);
            assert(host.critical_outbound_.size() == 1U);
            struct InertTransport : BlockingRealtimeTransport {
                bool is_open() const override { return false; }
            };
            host.transport_ = std::make_unique<InertTransport>();
            host.gameplay_handoff_.reset();
            host.host_backpressure_active_ = true;
            host.simulation_progress_present_[1] = true;
            host.simulation_completed_frame_[1] = 1U;
            const auto old = std::chrono::steady_clock::now() - std::chrono::hours(1);
            host.simulation_progress_time_[1] = old;
            assert(host.record_simulation_progress_locked(1U, 0U, host.authority_epoch(), 1U));
            assert(host.simulation_progress_time_[1] == old); // stale heartbeat is not progress
            host.pump_locked();
            assert(host.state_ == ConnectionState::Failed);
            assert(host.status_.find("90 seconds") != std::string::npos);
        }
        // Snapshot-before-history must not even invoke the memory installer.
        DirectSession guest;
        prepare_fast_forward_guest(guest, 100U, 4U);
        protocol::FrameCommitPayload missing;
        {
            std::scoped_lock lock(guest.mutex_);
            missing = guest.frame_commits_.at(102U);
            guest.frame_commits_.erase(102U);
        }
        unsigned installations = 0;
        std::string error;
        auto install = [&] { ++installations; return true; };
        assert(guest.install_fast_forward(104U, install, error) == SessionPollResult::Pending);
        assert(installations == 0 && next_commit_frame(guest) == 100U);
        {
            std::scoped_lock lock(guest.mutex_);
            guest.frame_commits_[102U] = missing;
            guest.recovery_frame_ = 103U;
        }
        assert(guest.install_fast_forward(104U, install, error) == SessionPollResult::Pending);
        assert(installations == 0 && next_commit_frame(guest) == 100U);
        {
            std::scoped_lock lock(guest.mutex_); guest.recovery_frame_.reset();
        }
        assert(guest.install_fast_forward(104U, [] { return false; }, error) == SessionPollResult::Pending);
        assert(next_commit_frame(guest) == 100U);
        assert(guest.install_fast_forward(104U, install, error) == SessionPollResult::Ready);
        assert(installations == 1 && next_commit_frame(guest) == 104U);
        assert(guest.install_fast_forward(104U, install, error) == SessionPollResult::Ready);
        assert(installations == 1);
        prepare_fast_forward_guest(guest, 200U, 4U, true);
        assert(guest.install_fast_forward(204U, install, error) == SessionPollResult::Failed);
        assert(installations == 1 && next_commit_frame(guest) == 200U);

        for (unsigned completed : {99U, 100U, 101U}) {
            DirectSession client;
            {
                std::scoped_lock lock(client.mutex_);
                client.state_ = ConnectionState::Running;
                client.authoritative_phase_active_ = true;
                client.authority_lifecycle_ = DirectSession::AuthorityLifecycle::Racing;
                client.scene_epoch_ = 7U;
                client.next_commit_frame_ = completed == 101U ? 102U : 101U;
                client.native_completed_frame_ = completed;
                protocol::Datagram begin{};
                begin.header.type = protocol::MessageType::RecoveryBegin;
                begin.payload = protocol::encode_recovery({7U, 100U, 0U});
                client.handle_client_packet(begin);
                assert((client.state_ == ConnectionState::Failed) == (completed > 100U));
            }
            if (completed <= 100U) {
                FrameInputs input{};
                assert(client.synchronize_inputs_result(101U, {}, input, std::chrono::milliseconds(0)) ==
                       InputSynchronizationResult::Suspended);
            }
        }
        ProgressBudget budget;
        const auto origin = ProgressBudget::Clock::now();
        budget.start(1U, origin);
        assert(!budget.expired(1U, origin + std::chrono::seconds(44)));
        assert(budget.expired(1U, origin + std::chrono::seconds(45)));
        budget.start(1U, origin);
        assert(!budget.expired(2U, origin + std::chrono::seconds(40)));
        assert(!budget.expired(3U, origin + std::chrono::seconds(80)));
        assert(budget.expired(4U, origin + std::chrono::seconds(90)));

        // Each guest receives its own score, with required checkpoint probes
        // included. A slow third/fourth peer must not pollute a healthy peer's RTT.
        DirectSession preflight;
        {
            std::scoped_lock lock(preflight.mutex_);
            preflight.is_host_ = true;
            preflight.connection_test_active_ = true;
            preflight.connection_test_draining_ = true;
            preflight.connection_test_id_ = 7U;
            for (unsigned slot = 1; slot < 4; ++slot) {
                PeerAddress address{}; address.size = 1U; address.storage[0] = slot;
                preflight.peers_[slot] = {true, slot + 1U, address,
                    static_cast<std::uint8_t>(slot), secure::generate_key()};
                for (unsigned lane = 0; lane < 5; ++lane) {
                    preflight.connection_test_sent_[slot][lane] = 10U;
                    preflight.connection_test_received_[slot][lane] = 10U;
                    preflight.connection_test_rtt_samples_[slot][lane].assign(10U,
                        slot == 1U ? 10.0 : slot == 2U ? 120.0 : 500.0);
                }
            }
            // Superseded in v1.0.11 (rollback-connection): the legacy per-guest
            // score is never computed; the isolated Rollback check in
            // assessment.hpp rates connections instead. It must stay gone.
            preflight.service_connection_test_locked(std::chrono::steady_clock::now());
            for (unsigned slot = 1; slot < 4; ++slot) assert(preflight.connection_test_results_[slot].score == 0U);
        }
    }
    static void stale_boundary_and_history_regression() {
        DirectSession client;
        {
            std::scoped_lock lock(client.mutex_);
            client.state_ = ConnectionState::Running;
            client.scene_epoch_ = 7U;
            client.authoritative_phase_active_ = true;
            client.authority_lifecycle_ = DirectSession::AuthorityLifecycle::Racing;
            client.remember_release_locked(protocol::MessageType::RecoveryResume, 7U, 60U, 0U);
            client.remember_release_locked(protocol::MessageType::TransitionBarrier, 7U, 80U, 1U);
            for (unsigned repeat = 0; repeat < 1000U; ++repeat) {
                protocol::Datagram packet{};
                packet.header.type = protocol::MessageType::RecoveryBegin;
                packet.payload = protocol::encode_recovery({7U, 60U, 0U});
                client.handle_client_packet(packet);
                assert(!client.recovery_frame_);
                packet.header.type = protocol::MessageType::TransitionBarrier;
                packet.payload = protocol::encode_transition_barrier({7U, 80U, 1U, 0U, protocol::TransitionBarrierStage::Begin});
                client.handle_client_packet(packet);
                assert(!client.transition_barrier_);
                assert(client.state_ == ConnectionState::Running);
            }
        }
        client.disconnect();
        {
            std::scoped_lock lock(client.mutex_);
            assert(client.completed_releases_.empty());
            assert(!client.checkpoint_encoding_);
            client.remember_release_locked(protocol::MessageType::RecoveryResume, 7U, 60U, 0U);
        }
        // Starting another valid join must reset session-scoped completion
        // history even if the caller did not explicitly disconnect first.
        std::string join_error;
        DirectSession reset_host;
        // v1.0.11 (rollback-only): lobbies are Rollback lobbies, which need the
        // owned engines; a unit test declares them available.
        assert(reset_host.enable_owned_backend(true));
        assert(reset_host.host(0U, "127.0.0.1", "Reset test",
                               ConnectionMethod::Lan, "Host", rollback_policy::default_rules(), join_error));
        assert(client.join(reset_host.view().invite, "Reset test", join_error));
        {
            std::scoped_lock lock(client.mutex_);
            assert(client.completed_releases_.empty());
            assert(!client.checkpoint_encoding_);
        }
        client.disconnect();
        DirectSession host;
        std::scoped_lock lock(host.mutex_);
        host.is_host_ = true;
        host.state_ = ConnectionState::Running;
        host.next_commit_frame_ = 100U;
        host.launch_descriptor_ = LaunchDescriptor{};
        PeerAddress source{}; source.size = 1U; source.storage[0] = 1U;
        host.peers_[1] = {true, 2U, source, 1U, secure::generate_key()};
        protocol::Datagram request{};
        request.header.type = protocol::MessageType::FrameCommitRequest;
        request.header.frame = 1U;
        request.payload = protocol::encode_frame_commit_request({host.authority_epoch(), 1U, 1U});
        host.handle_host_packet(source, 2U, request);
        assert(host.state_ == ConnectionState::Failed);
        assert(host.status_.find("Required menu input history") != std::string::npos);
    }
    static void recovery_budget_regression() {
        struct CheckpointBlockedTransport : BlockingRealtimeTransport {
            bool blocked = true;
            unsigned controls = 0U, checkpoints = 0U;
            DatagramSendStatus send_status(const PeerAddress&,
                std::span<const std::uint8_t>, TransportTrafficClass traffic,
                std::string&) override {
                if (traffic == TransportTrafficClass::Checkpoint) {
                    ++checkpoints;
                    if (blocked) return DatagramSendStatus::WouldBlock;
                } else if (traffic == TransportTrafficClass::Control) ++controls;
                return DatagramSendStatus::Sent;
            }
        };
        DirectSession host;
        std::unique_lock lock(host.mutex_);
        auto transport = std::make_unique<CheckpointBlockedTransport>();
        auto* raw = transport.get();
        host.transport_ = std::move(transport);
        host.is_host_ = true;
        PeerAddress address{}; address.size = 1U; address.storage[0] = 1U;
        host.peers_[1] = {true, 2U, address, 1U, secure::generate_key()};
        const std::vector<std::uint8_t> payload{1U, 2U, 3U};
        for (unsigned i = 0; i < 1000U; ++i)
            assert(host.send_to(address, protocol::MessageType::StateSnapshot, payload, 60U));
        assert(host.authority_outbound_.size() == 1U);
        assert(host.duplicate_retries_coalesced_ == 999U);
        const auto first_wire = host.authority_outbound_.front().bytes;
        assert(host.send_to(address, protocol::MessageType::RecoveryResume, payload, 60U));
        host.flush_outbound_locked();
        assert(raw->controls == 1U && raw->checkpoints == 1U);
        assert(host.critical_outbound_.empty() && host.authority_outbound_.size() == 1U);
        raw->blocked = false;
        host.flush_outbound_locked();
        assert(host.authority_outbound_.empty());
        assert(host.send_to(address, protocol::MessageType::StateSnapshot, payload, 60U));
        assert(first_wire != host.authority_outbound_.front().bytes);
        // UI reads may run while the session worker owns this mutex.
        auto ui = std::async(std::launch::async, [&] { return host.presentation_view(); });
        assert(ui.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready);
        (void)ui.get();
        for (unsigned i = 1U; i <= 1000U; ++i) {
            host.remember_release_locked(protocol::MessageType::RecoveryResume, 7U, i, 0U);
            assert(host.completed_releases_.size() <= 16U);
            assert(host.completed_release_locked(protocol::MessageType::RecoveryResume, 7U, i, 0U, 2U));
            assert(!host.completed_release_locked(protocol::MessageType::RecoveryResume, 7U, i, 0U, 3U));
            const auto expiry = host.completed_releases_.back().expires;
            host.remember_release_locked(protocol::MessageType::RecoveryResume, 7U, i, 0U);
            assert(host.completed_releases_.back().expires == expiry);
        }
        host.completed_releases_.back().expires = std::chrono::steady_clock::now();
        assert(!host.completed_release_locked(protocol::MessageType::RecoveryResume, 7U, 1000U, 0U));
    }
    static void delayed_probe_regression() {
        DirectSession host;
        std::scoped_lock lock(host.mutex_);
        auto& peer = host.peers_[1];
        peer.active = true;
        peer.sender_id = 2U;
        peer.address.size = 1U;
        peer.address.storage[0] = 1U;
        host.send_connection_probes();
        const auto first_token = peer.ping_token;
        peer.last_ping -= std::chrono::seconds(2);
        host.send_connection_probes();
        host.update_peer_metrics(peer, first_token);
        assert(peer.pings_received == 1U);
        assert(peer.loss_percent == 0.0F);
        host.update_peer_metrics(peer, first_token);
        host.update_peer_metrics(peer, first_token - 1U);
        assert(peer.pings_received == 1U);
    }
    static void terminal_release_regression() {
        DirectSession host;
        std::string error;
        PeerAddress source{};
        source.size = 1U; source.storage[0] = 1U;
        {
            std::scoped_lock lock(host.mutex_);
            host.is_host_ = true; host.state_ = ConnectionState::Running;
            host.local_slot_ = 0U; host.sender_id_ = 1U; host.match_id_ = 1U;
            host.scene_epoch_ = 7U; host.authoritative_phase_active_ = true;
            host.peers_[1] = {true, 2U, source, 1U, secure::generate_key()};
            host.gameplay_barrier_ = DirectSession::GameplayBarrierState{7U, 42U, 2U};
            host.gameplay_barrier_->epoch_synchronized = true;
            host.gameplay_barrier_->baseline_released = true;
            host.gameplay_barrier_->arm_announced = true;
            host.fully_acknowledged_states_.insert(0U);
        }
        assert(host.poll_gameplay_resume(42U, 2U, error) == SessionPollResult::Ready);
        {
            std::scoped_lock lock(host.mutex_);
            host.gameplay_resume_until_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
            host.critical_outbound_.clear();
            protocol::Datagram packet{};
            packet.header.type = protocol::MessageType::GameplayBarrier;
            packet.payload = protocol::encode_gameplay_barrier({7U, 42U, 2U, 1U, protocol::GameplayBarrierStage::Armed});
            host.handle_host_packet(source, 2U, packet);
            assert(!host.critical_outbound_.empty());
        }
        {
            std::scoped_lock lock(host.mutex_);
            host.recovery_frame_ = 60U;
            host.recovery_acks_[0] = true;
            protocol::Datagram packet{};
            packet.header.type = protocol::MessageType::RecoveryAck;
            packet.payload = protocol::encode_recovery({7U, 60U, 1U});
            host.handle_host_packet(source, 2U, packet);
        }
        assert(host.poll_recovery_complete(60U, error) == SessionPollResult::Ready);
        {
            std::scoped_lock lock(host.mutex_);
            host.recovery_resume_until_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
            host.critical_outbound_.clear();
            protocol::Datagram packet{};
            packet.header.type = protocol::MessageType::RecoveryAck;
            packet.payload = protocol::encode_recovery({7U, 60U, 1U});
            host.handle_host_packet(source, 2U, packet);
            assert(!host.critical_outbound_.empty());
        }
        assert(host.poll_transition(80U, 1U, error) == SessionPollResult::Ready);
        {
            std::scoped_lock lock(host.mutex_);
            host.transition_resume_until_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
        }
        host.end_authoritative_phase();
        {
            std::scoped_lock lock(host.mutex_);
            host.critical_outbound_.clear();
            protocol::Datagram packet{};
            packet.header.type = protocol::MessageType::TransitionBarrier;
            packet.payload = protocol::encode_transition_barrier({7U, 80U, 1U, 1U, protocol::TransitionBarrierStage::Acknowledge});
            host.handle_host_packet(source, 2U, packet);
            assert(!host.critical_outbound_.empty());
        }
    }
    static void staged_rekey_regression() {
        struct RekeyTransport : SessionTransport {
            QuickJoinRekeyStatus status = QuickJoinRekeyStatus::Idle;
            bool open(std::uint16_t, std::string&) override { return true; }
            void close() override {}
            bool is_open() const override { return true; }
            std::uint16_t local_port() const override { return 0; }
            bool quick_join() const override { return true; }
            bool rekey_quick_join(std::string, std::string&) override { status = QuickJoinRekeyStatus::Pending; return true; }
            QuickJoinRekeyStatus rekey_status() const override { return status; }
            std::string quick_join_code() const override { return status == QuickJoinRekeyStatus::Committed ? "FGHJK" : "ABCDE"; }
            DatagramSendStatus send_status(const PeerAddress&, std::span<const std::uint8_t>, TransportTrafficClass, std::string&) override { return DatagramSendStatus::Sent; }
            bool receive(PeerAddress&, std::vector<std::uint8_t>&, std::string&) override { return false; }
        };
        DirectSession s;
        auto transport = std::make_unique<RekeyTransport>();
        auto* raw = transport.get();
        {
            std::scoped_lock lock(s.mutex_);
            s.transport_ = std::move(transport);
            s.is_host_ = true; s.method_ = ConnectionMethod::QuickJoin;
            s.advertised_host_ = "quick"; s.invite_ = "ABCDE";
            s.invitation_capability_ = secure::generate_key();
            s.host_key_pair_ = secure::generate_key_pair();
        }
        const auto key = s.invitation_capability_;
        std::string error;
        assert(s.revoke_invitation(error));
        assert(s.invitation_capability_ == key && s.invite_ == "ABCDE");
        assert(!s.request_start(error));
        {
            std::scoped_lock lock(s.mutex_);
            raw->status = QuickJoinRekeyStatus::Failed;
            s.pump_locked();
            assert(!s.pending_invitation_ && s.invitation_capability_ == key && s.invite_ == "ABCDE");
        }
        assert(s.revoke_invitation(error));
        {
            std::scoped_lock lock(s.mutex_);
            raw->status = QuickJoinRekeyStatus::Committed;
            s.pump_locked();
            assert(!s.pending_invitation_ && s.invitation_capability_ != key && s.invite_ == "FGHJK");
        }
    }
    static void stale_admission_and_preflight_regression() {
        staged_rekey_regression();
        DirectSession session;
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.state_ = ConnectionState::Lobby;
        unsigned save_writes = 0;
        session.save_installer_ = [&](std::uint64_t, std::span<const std::uint8_t>, std::filesystem::path&, std::string&) {
            ++save_writes; return true;
        };
        protocol::Datagram packet{};
        packet.header.type = protocol::MessageType::JoinPending;
        session.handle_client_packet(packet);
        assert(session.state_ == ConnectionState::Lobby);
        packet.header.type = protocol::MessageType::HelloAck;
        packet.payload = protocol::encode_hello_ack({false, 0U, "stale rejection"});
        session.handle_client_packet(packet);
        assert(session.state_ == ConnectionState::Lobby && save_writes == 0);
        session.state_ = ConnectionState::Running;
        session.handle_client_packet(packet);
        assert(session.state_ == ConnectionState::Running);
        session.state_ = ConnectionState::Lobby;
        session.room_view_.players[0].occupied = true;
        session.room_view_.players[1].occupied = true;
        packet.header.type = protocol::MessageType::PreflightBegin;
        packet.payload = protocol::encode_preflight_begin({7U, 7000U});
        session.handle_client_packet(packet);
        assert(session.connection_test_active_ && session.connection_test_id_ == 7U);
        for (std::uint8_t slot = 0; slot < 2; ++slot) {
            packet.header.type = protocol::MessageType::PreflightResult;
            packet.payload = protocol::encode_preflight_result({7U, slot, 9U, 30U, 2U, 0U, 0U, true});
            session.handle_client_packet(packet);
        }
        assert(!session.connection_test_active_ && session.terminal_connection_test_id_ == 7U);
        packet.header.type = protocol::MessageType::PreflightRealtimeProbe;
        packet.payload = protocol::encode_preflight_probe({7U, 1U, 1000U, false, {1U}});
        session.handle_client_packet(packet);
        assert(!session.connection_test_active_);
        packet.header.type = protocol::MessageType::PreflightBegin;
        packet.payload = protocol::encode_preflight_begin({6U, 7000U});
        session.handle_client_packet(packet);
        assert(!session.connection_test_active_);
        packet.payload = protocol::encode_preflight_begin({8U, 7000U});
        session.handle_client_packet(packet);
        assert(session.connection_test_active_);
        const auto deadline = session.connection_test_drain_end_;
        session.handle_client_packet(packet); // Duplicate Begin cannot extend the deadline.
        assert(session.connection_test_drain_end_ == deadline);
        // Superseded in v1.0.11 (rollback-connection): the legacy preflight no
        // longer concludes from this service; the isolated Rollback check
        // replaced it. It must stay gone.
        session.service_connection_test_locked(deadline + std::chrono::seconds(1));
        assert(session.connection_test_active_ && session.terminal_connection_test_id_ == 7U);
        assert(session.status_.find("inconclusive") == std::string::npos);
    }
    static void expire_launch_countdown(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        if (session.launch_countdown_active_) {
            session.launch_countdown_deadline_ = std::chrono::steady_clock::now();
            return;
        }
        // The production worker is deliberately allowed to advance the
        // countdown concurrently. On a loaded CI host it can acquire the
        // mutex and complete the five-second gate before this test helper.
        assert(session.state_ == ConnectionState::Loading ||
               session.launch_requested_);
    }

    static void forget_remote_countdown_ack(DirectSession& session,
                                            std::uint8_t slot) {
        std::scoped_lock lock(session.mutex_);
        assert(slot < session.countdown_acks_.size());
        session.countdown_acks_[slot] = false;
    }

    static std::chrono::milliseconds launch_control_timeout(
        DirectSession& session, double rtt_ms, double jitter_ms,
        float loss_percent, std::chrono::milliseconds minimum,
        std::chrono::milliseconds maximum) {
        std::scoped_lock lock(session.mutex_);
        session.peers_[1].active = true;
        session.peers_[1].slot = 1U;
        session.peers_[1].rtt_ms = rtt_ms;
        session.peers_[1].jitter_ms = jitter_ms;
        session.peers_[1].loss_percent = loss_percent;
        return session.launch_control_timeout_locked(minimum, maximum);
    }

    static bool enqueue_checkpoint(DirectSession& session,
                                   std::uint8_t marker) {
        std::scoped_lock lock(session.mutex_);
        PeerAddress destination{};
        destination.size = 1U;
        destination.storage[0] = 1U;
        return session.enqueue_outbound(
            destination, protocol::MessageType::StateSnapshot,
            std::vector<std::uint8_t>{marker});
    }
    static std::size_t checkpoint_queue_size(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.authority_outbound_.size();
    }
    static std::uint8_t oldest_checkpoint_marker(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.authority_outbound_.front().bytes.front();
    }
    static bool enqueue_admission(DirectSession& session,
                                  protocol::MessageType type,
                                  std::uint8_t marker) {
        std::scoped_lock lock(session.mutex_);
        PeerAddress destination{};
        destination.size = 1U;
        destination.storage[0] = 1U;
        return session.enqueue_outbound(
            destination, type, std::vector<std::uint8_t>{marker});
    }
    static std::size_t admission_queue_size(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.critical_outbound_.size();
    }
    static std::size_t ordinary_queue_size(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.normal_priority_outbound_.size();
    }
    static std::uint64_t packets_sent(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.packets_sent_;
    }
    static std::uint64_t packets_received(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.packets_received_;
    }
    static std::uint64_t forced_prediction_frames(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.forced_prediction_frames_;
    }
    static std::uint32_t scene_epoch(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.scene_epoch_;
    }
    static std::uint32_t rollback_transport_epoch(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.rollback_transport_epoch_;
    }
    static std::uint32_t next_commit_frame(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.next_commit_frame_;
    }
    static void prepare_fast_forward_guest(
        DirectSession& session, std::uint32_t first_frame,
        std::uint32_t frame_count, bool break_last_link = false) {
        stop_worker(session);
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.state_ = ConnectionState::Running;
        session.local_slot_ = 1U;
        session.match_id_ = 0x44556677U;
        session.input_epoch_ = 9U;
        session.scene_epoch_ = 3U;
        session.authoritative_phase_active_ = true;
        session.authority_lifecycle_ =
            DirectSession::AuthorityLifecycle::Racing;
        LaunchDescriptor descriptor{};
        descriptor.match_id = session.match_id_;
        descriptor.occupied_mask = 0x03U;
        descriptor.player_count = 2U;
        descriptor.input_delay_frames = 2U;
        descriptor.rollback_window = 8U;
        descriptor.synchronization = SynchronizationMode::Rollback;
        session.launch_descriptor_ = descriptor;
        session.next_commit_frame_ = first_frame;
        session.last_consumed_commit_hash_ = 0x1020304050607080ULL;
        session.last_consumed_input_frame_.reset();
        session.frame_commits_.clear();
        std::uint64_t previous_hash = session.last_consumed_commit_hash_;
        for (std::uint32_t offset = 0U; offset < frame_count; ++offset) {
            protocol::FrameCommitPayload commit{};
            commit.epoch = session.authority_epoch();
            commit.frame = first_frame + offset;
            commit.occupied_mask = descriptor.occupied_mask;
            commit.inputs[0] = {0x8000U,
                static_cast<std::int8_t>(offset), 0};
            commit.inputs[1] = {0x4000U,
                static_cast<std::int8_t>(-static_cast<int>(offset)), 0};
            commit.previous_hash = previous_hash;
            if (break_last_link && offset + 1U == frame_count) {
                commit.previous_hash ^= 1U;
            }
            commit.commit_hash = protocol::frame_commit_hash(
                session.match_id_, commit);
            session.frame_commits_[commit.frame] = commit;
            previous_hash = commit.commit_hash;
        }
        session.authoritative_input_frame_ =
            first_frame + frame_count - 1U;
    }
    static std::uint32_t simulation_completed_frame(
        const DirectSession& session, std::uint8_t slot) {
        std::scoped_lock lock(session.mutex_);
        assert(slot < session.simulation_completed_frame_.size());
        return session.simulation_completed_frame_[slot];
    }
    static bool simulation_progress_present(
        const DirectSession& session, std::uint8_t slot) {
        std::scoped_lock lock(session.mutex_);
        assert(slot < session.simulation_progress_present_.size());
        return session.simulation_progress_present_[slot];
    }
    static std::uint32_t retired_authority_epoch(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.retired_authority_epoch_;
    }
    static std::uint64_t stale_epoch_packets(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.stale_epoch_packets_;
    }
    static std::uint32_t applied_correction_generation(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.applied_frame_correction_generation_;
    }
    static bool correction_transmission_pending(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.frame_correction_transmission_.has_value();
    }
    static std::uint32_t host_first_missing_input(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.host_input_first_missing_frame_;
    }
    static std::uint32_t peer_first_missing_input(
        const DirectSession& session, std::uint8_t slot) {
        std::scoped_lock lock(session.mutex_);
        assert(slot < session.peer_input_first_missing_frame_.size());
        return session.peer_input_first_missing_frame_[slot];
    }
    static void inject_client_input(DirectSession& host,
                                    std::uint8_t player_slot,
                                    const protocol::InputBatch& batch) {
        std::scoped_lock lock(host.mutex_);
        assert(player_slot < host.peers_.size());
        const DirectSession::PeerRecord& peer = host.peers_[player_slot];
        assert(peer.active && peer.slot == player_slot);
        const protocol::Datagram datagram{
            {protocol::MessageType::Input, host.match_id_, 1U,
             batch.first_frame},
            protocol::encode_input_batch(batch)};
        host.handle_host_packet(peer.address, peer.sender_id, datagram);
    }
    static void inject_transition_acknowledgement(
        DirectSession& host, std::uint8_t player_slot,
        std::uint32_t scene_epoch, std::uint32_t frame,
        std::uint8_t transition_kind) {
        std::scoped_lock lock(host.mutex_);
        assert(player_slot < host.peers_.size());
        const DirectSession::PeerRecord& peer = host.peers_[player_slot];
        assert(peer.active && peer.slot == player_slot);
        const protocol::Datagram datagram{
            {protocol::MessageType::TransitionBarrier, host.match_id_, 1U,
             frame},
            protocol::encode_transition_barrier({
                scene_epoch, frame, transition_kind, player_slot,
                protocol::TransitionBarrierStage::Acknowledge})};
        host.handle_host_packet(peer.address, peer.sender_id, datagram);
    }
    static bool has_resumed_transition_barrier(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.transition_barrier_.has_value() &&
               session.transition_barrier_->resumed;
    }
    static bool has_frame_commit(const DirectSession& session,
                                 std::uint32_t frame) {
        std::scoped_lock lock(session.mutex_);
        return session.frame_commits_.contains(frame);
    }
    static void discard_frame_commit(DirectSession& session,
                                     std::uint32_t frame) {
        std::scoped_lock lock(session.mutex_);
        session.frame_commits_.erase(frame);
    }
    static void discard_frame_commit_range(DirectSession& session,
                                           std::uint32_t first_frame,
                                           std::uint32_t count) {
        std::scoped_lock lock(session.mutex_);
        for (std::uint32_t offset = 0U; offset < count; ++offset) {
            session.frame_commits_.erase(first_frame + offset);
        }
    }
    static std::uint64_t commit_repair_requests_sent(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.commit_repair_requests_sent_;
    }
    static std::uint64_t commit_repair_requests_received(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.commit_repair_requests_received_;
    }
    static protocol::InputBatch retained_input_batch(
        DirectSession& session, std::uint32_t retained_first,
        std::uint32_t retained_count, std::uint32_t first_missing,
        bool reliable_repair) {
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.local_slot_ = 1U;
        session.input_epoch_ = 7U;
        session.host_input_first_missing_frame_ = first_missing;
        session.local_history_.clear();
        for (std::uint32_t offset = 0U; offset < retained_count; ++offset) {
            const std::uint32_t frame = retained_first + offset;
            session.local_history_.emplace_back(
                frame, PackedInput{
                    static_cast<std::uint16_t>(frame & 0xFFFFU),
                    static_cast<std::int8_t>(frame % 63U),
                    static_cast<std::int8_t>(-(static_cast<int>(frame % 63U)))});
        }
        return session.make_local_history_batch(
            retained_first + retained_count - 1U, reliable_repair);
    }
    static protocol::InputBatch repair_unproduced_input(
        DirectSession& session, std::uint32_t submitted_frame,
        std::uint32_t requested_frame, PackedInput held) {
        stop_worker(session);
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.state_ = ConnectionState::Running;
        session.local_slot_ = 1U;
        session.input_epoch_ = 7U;
        session.local_history_.clear();
        session.local_history_.emplace_back(submitted_frame, held);
        session.local_input_submitted_frame_ = submitted_frame;
        session.local_input_submitted_ = held;
        LaunchDescriptor descriptor{};
        descriptor.synchronization = SynchronizationMode::Lockstep;
        descriptor.occupied_mask = 0x03U;
        descriptor.player_count = 2U;
        session.launch_descriptor_ = descriptor;
        const protocol::InputRepairRequestPayload request{
            session.authority_epoch(), session.local_slot_, requested_frame};
        const protocol::Datagram datagram{
            {protocol::MessageType::InputRepairRequest, session.match_id_, 1U,
             requested_frame},
            protocol::encode_input_repair_request(request)};
        session.handle_client_packet(datagram);
        return session.make_local_history_batch(
            session.local_input_submitted_frame_, true, requested_frame);
    }
    static bool repair_created_input_while_gameplay_is_parked(
        DirectSession& session, std::uint32_t submitted_frame,
        std::uint32_t requested_frame, PackedInput held) {
        stop_worker(session);
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.state_ = ConnectionState::Running;
        session.local_slot_ = 1U;
        session.input_epoch_ = 7U;
        session.local_history_.clear();
        session.local_history_.emplace_back(submitted_frame, held);
        session.local_input_submitted_frame_ = submitted_frame;
        session.local_input_submitted_ = held;
        LaunchDescriptor descriptor{};
        descriptor.synchronization = SynchronizationMode::Lockstep;
        descriptor.occupied_mask = 0x03U;
        descriptor.player_count = 2U;
        session.launch_descriptor_ = descriptor;
        session.gameplay_handoff_ = DirectSession::GameplayHandoffState{
            7U, 8U, requested_frame, submitted_frame, 42U, true, true, false,
            std::chrono::steady_clock::now() + std::chrono::seconds(5)};
        const protocol::InputRepairRequestPayload request{
            session.authority_epoch(), session.local_slot_, requested_frame};
        const protocol::Datagram datagram{
            {protocol::MessageType::InputRepairRequest, session.match_id_, 1U,
             requested_frame},
            protocol::encode_input_repair_request(request)};
        session.handle_client_packet(datagram);
        return std::any_of(
            session.local_history_.begin(), session.local_history_.end(),
            [requested_frame](const auto& entry) {
                return entry.first == requested_frame;
            });
    }
    static bool enqueue_input_repair(DirectSession& session,
                                     std::uint32_t frame = 0U) {
        std::scoped_lock lock(session.mutex_);
        PeerAddress destination{};
        destination.size = 1U;
        destination.storage[0] = 1U;
        return session.enqueue_outbound(
            destination, protocol::MessageType::InputRepair,
            std::vector<std::uint8_t>{0x52U}, frame);
    }
    static bool enqueue_realtime_input(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        PeerAddress destination{};
        destination.size = 1U;
        destination.storage[0] = 1U;
        return session.enqueue_outbound(
            destination, protocol::MessageType::Input,
            std::vector<std::uint8_t>{0x49U});
    }
    static BlockingRealtimeTransport* install_blocking_transport(
        DirectSession& session) {
        // This case explicitly drives flush_outbound_locked itself. A live
        // worker could concurrently send through the fake and race its plain
        // counters (particularly under ARM emulation). Stop it for this one
        // deterministic unit case; pump-free admission below still tests the
        // real workers. Keep every exact-count assertion unchanged.
        stop_worker(session);
        auto transport = std::make_unique<BlockingRealtimeTransport>();
        BlockingRealtimeTransport* result = transport.get();
        session.transport_ = std::move(transport);
        return result;
    }
    static void flush_outbound(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        session.flush_outbound_locked();
    }
    static std::size_t repair_queue_size(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.repair_outbound_.size();
    }
    static std::size_t realtime_queue_size(const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.high_priority_outbound_.size();
    }
    static std::size_t simulation_queue_size(
        const DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        return session.repair_outbound_.size() +
               session.high_priority_outbound_.size();
    }
    static bool install_authoritative_frontend_handoff(
        DirectSession& session, std::uint32_t next_commit_frame,
        std::uint32_t boundary_frame, std::string& error) {
        std::scoped_lock lock(session.mutex_);
        session.is_host_ = false;
        session.state_ = ConnectionState::Running;
        session.local_slot_ = 1U;
        session.match_id_ = 0x44556677U;
        session.input_epoch_ = 7U;
        session.next_commit_frame_ = next_commit_frame;
        LaunchDescriptor descriptor{};
        descriptor.match_id = session.match_id_;
        descriptor.occupied_mask = 0x03U;
        descriptor.player_count = 2U;
        descriptor.synchronization = SynchronizationMode::Rollback;
        session.launch_descriptor_ = descriptor;
        const protocol::GameplayHandoffPayload handoff{
            session.input_epoch_, session.input_epoch_ + 1U,
            boundary_frame, 42U, 0U,
            protocol::GameplayHandoffStage::Suspend};
        return session.adopt_gameplay_handoff_locked(
            handoff, false, error);
    }
    static bool apply_delayed_local_frontend_load(
        DirectSession& session, std::uint32_t local_boundary_frame,
        std::string& error) {
        std::scoped_lock lock(session.mutex_);
        const protocol::GameplayHandoffPayload handoff{
            session.input_epoch_, 0U, local_boundary_frame, 42U,
            session.local_slot_, protocol::GameplayHandoffStage::Request};
        return session.adopt_gameplay_handoff_locked(
            handoff, true, error);
    }
    static bool reapply_authoritative_frontend_handoff(
        DirectSession& session, std::uint32_t boundary_frame,
        std::string& error) {
        std::scoped_lock lock(session.mutex_);
        const protocol::GameplayHandoffPayload handoff{
            session.input_epoch_, session.input_epoch_ + 1U,
            boundary_frame, 42U, 0U,
            protocol::GameplayHandoffStage::Suspend};
        return session.adopt_gameplay_handoff_locked(
            handoff, false, error);
    }
};

} // namespace dkr::runtime::netplay

namespace {

const char* test_host_address() {
    const char* configured = std::getenv("DKR_TEST_HOST_ADDRESS");
    return configured != nullptr && *configured != '\0'
        ? configured : "127.0.0.1";
}

dkr::runtime::netplay::CompatibilityManifest manifest() {
    using namespace dkr::runtime::netplay;
    CompatibilityManifest value{};
    value.release_version = "1.0.0-netplay-admission-restore-rc6";
    value.build_fingerprint =
        "DKR-R/1.0.0-gameplay-handoff-rc13/netplay-protocol-27/"
        "runtime-ae1ffbb9/gekkonet-5924b5c7";
    value.revision = Revision::UsV77;
    value.canonical_rom_hash = 1U;
    value.patch_policy_hash = 2U;
    value.gameplay_settings_hash = 3U;
    value.magic_codes_hash = 4U;
    value.session_save_hash = 5U;
    value.architecture = "test";
    value.floating_point_mode = "strict";
    return value;
}

std::vector<std::uint8_t> online_test_save() {
    std::vector<std::uint8_t> save(512U);
    for (std::size_t index = 0U; index < save.size(); ++index) {
        save[index] = static_cast<std::uint8_t>((index * 37U + 11U) & 0xFFU);
    }
    return save;
}

void configure_online_session(
    dkr::runtime::netplay::DirectSession& session,
    dkr::runtime::netplay::CompatibilityManifest value = manifest()) {
    session.configure_manifest(value);
    // v1.0.11 (rollback-only): every lobby is a Rollback lobby, which host and
    // clients admit only with the owned engines; the shipped game has them.
    assert(session.enable_owned_backend(true));
    session.configure_session_save(
        online_test_save(),
        [](std::uint64_t match_id, std::span<const std::uint8_t> bytes,
           std::filesystem::path& installed_path, std::string& error) {
            if (match_id == 0U || bytes.size() != 512U) {
                error = "The test online save contract was invalid.";
                return false;
            }
            installed_path = std::filesystem::path("online-test") /
                std::to_string(match_id) / "dkr.us.v77.bin";
            error.clear();
            return true;
        });
}

void mark_online_game_loaded(
    dkr::runtime::netplay::DirectSession& session,
    std::uint64_t bootstrap_hash) {
    const auto runtime = session.runtime_view();
    assert(runtime.online_save_generation != 0U);
    assert(runtime.online_save_hash != 0U);
    session.mark_game_loaded(bootstrap_hash,
                             runtime.online_save_generation,
                             runtime.online_save_hash);
}

// Fixture settling uses a one-millisecond schedule, not one scheduler sleep
// per poll. On Windows a 1 ms sleep can take an entire timer quantum; charging
// that overshoot again for every poll turned this test into a two-minute wait.
// Keep every requested pump and the intended settling duration. Late wakeups
// catch up to the same steady-clock deadline instead of accumulating drift.
template <typename Pump>
void pump_scheduled(int attempts, Pump&& pump) {
    auto next_poll = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt < attempts; ++attempt) {
        pump();
        next_poll += std::chrono::milliseconds(1);
        std::this_thread::sleep_until(next_poll);
    }
}

void pump_pair(dkr::runtime::netplay::DirectSession& host,
               dkr::runtime::netplay::DirectSession& client,
               int attempts = 200) {
    pump_scheduled(attempts, [&] {
        host.pump();
        client.pump();
    });
}

// Network completion is a predicate, not a guessed number of timer ticks.
// The deadline bounds failed exchanges while healthy peers return immediately.
template <typename Ready>
void pump_pair_until(dkr::runtime::netplay::DirectSession& host,
                     dkr::runtime::netplay::DirectSession& client,
                     const char* stage, Ready&& ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        host.pump();
        client.pump();
        if (ready()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    std::cerr << "Timed out waiting for " << stage << '\n';
    std::abort();
}

template <std::size_t Count>
void pump_sessions(
    const std::array<dkr::runtime::netplay::DirectSession*, Count>& sessions,
    int attempts = 200) {
    pump_scheduled(attempts, [&] {
        for (auto* session : sessions) session->pump();
    });
}

} // namespace

int main() {
    dkr::runtime::netplay::DirectSessionTestAccess::owned_lane_isolation();
    dkr::runtime::netplay::DirectSessionTestAccess::experimental_admission_isolation();
    dkr::runtime::netplay::DirectSessionTestAccess::input_freshness_regression();
    dkr::runtime::netplay::DirectSessionTestAccess::online_save_status_regression();
    dkr::runtime::netplay::DirectSessionTestAccess::remaining_gaps_regression();
    if (std::getenv("DKR_REMAINING_GAPS_REGRESSION")) return 0;
    dkr::runtime::netplay::DirectSessionTestAccess::delayed_probe_regression();
    dkr::runtime::netplay::DirectSessionTestAccess::recovery_budget_regression();
    dkr::runtime::netplay::DirectSessionTestAccess::stale_boundary_and_history_regression();
    for (unsigned cycle = 0; cycle < 1000U; ++cycle)
        dkr::runtime::netplay::DirectSessionTestAccess::terminal_release_regression();
    if (std::getenv("DKR_TERMINAL_RELEASE_REGRESSION")) return 0;
    dkr::runtime::netplay::DirectSessionTestAccess::stale_admission_and_preflight_regression();
    using namespace dkr::runtime::netplay;
    {
        DirectSession timeout_policy;
        const auto minimum = std::chrono::seconds(5);
        const auto maximum = std::chrono::seconds(15);
        assert(DirectSessionTestAccess::launch_control_timeout(
                   timeout_policy, 0.0, 0.0, 0.0F, minimum, maximum) ==
               minimum);
        assert(DirectSessionTestAccess::launch_control_timeout(
                   timeout_policy, 100.0, 20.0, 2.0F, minimum, maximum) ==
               std::chrono::milliseconds(5620));
        assert(DirectSessionTestAccess::launch_control_timeout(
                   timeout_policy, 2000.0, 1000.0, 100.0F, minimum, maximum) ==
               maximum);
    }
    {
        // Player 1 has consumed frame 100's one-shot Track Select confirm and
        // announces frame 101 as the frontend/gameplay boundary.  A guest
        // still waiting to consume frame 100 must not park at its local
        // cursor: doing so replaces the confirm with neutral input and leaves
        // that guest in Track Select while Player 1 waits on a black screen.
        DirectSession lagging_frontend_guest;
        std::string handoff_error;
        assert(DirectSessionTestAccess::install_authoritative_frontend_handoff(
            lagging_frontend_guest, 100U, 101U, handoff_error));
        assert(handoff_error.empty());
        assert(!lagging_frontend_guest.gameplay_handoff_suspended(100U));
        assert(lagging_frontend_guest.gameplay_handoff_suspended(101U));

        // Suspend is deliberately retransmitted until every racer has
        // acknowledged it.  Duplicate authenticated packets must therefore
        // be idempotent and must not move the boundary back to the guest's
        // current cursor.
        assert(DirectSessionTestAccess::reapply_authoritative_frontend_handoff(
            lagging_frontend_guest, 101U, handoff_error));
        assert(handoff_error.empty());
        assert(!lagging_frontend_guest.gameplay_handoff_suspended(100U));
        assert(lagging_frontend_guest.gameplay_handoff_suspended(101U));

        // The local level-load callback can run after the authoritative packet
        // on a reordered network path.  It must not pull the suspend boundary
        // back to frame 100 after the host has committed frame 101.
        assert(DirectSessionTestAccess::apply_delayed_local_frontend_load(
            lagging_frontend_guest, 100U, handoff_error));
        assert(handoff_error.empty());
        assert(!lagging_frontend_guest.gameplay_handoff_suspended(100U));
        assert(lagging_frontend_guest.gameplay_handoff_suspended(101U));
    }
    {
        DirectSession fast_forward_guest;
        DirectSessionTestAccess::prepare_fast_forward_guest(
            fast_forward_guest, 120U, 6U);
        std::string fast_forward_error;
        assert(fast_forward_guest.fast_forward_authoritative_commits(
            126U, fast_forward_error));
        assert(fast_forward_error.empty());
        assert(DirectSessionTestAccess::next_commit_frame(
                   fast_forward_guest) == 126U);

        DirectSession broken_fast_forward_guest;
        DirectSessionTestAccess::prepare_fast_forward_guest(
            broken_fast_forward_guest, 240U, 6U, true);
        assert(!broken_fast_forward_guest.fast_forward_authoritative_commits(
            246U, fast_forward_error));
        assert(!fast_forward_error.empty());
        assert(DirectSessionTestAccess::next_commit_frame(
                   broken_fast_forward_guest) == 240U);
    }
    if (const char* host_invite_file = std::getenv("DKR_TEST_HOST_INVITE_FILE");
        host_invite_file != nullptr && *host_invite_file != '\0') {
        DirectSession host_probe;
        configure_online_session(host_probe);
        Rules rules = rollback_policy::default_rules();
        std::string error;
        assert(host_probe.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        if (!host_probe.host(0U, test_host_address(), "Admission Host",
                             ConnectionMethod::VirtualLan, "Host", rules,
                             error)) {
            std::cerr << "host setup failed: " << error << '\n';
            return 4;
        }
        {
            std::ofstream invite_output(host_invite_file,
                                        std::ios::binary | std::ios::trunc);
            invite_output << host_probe.view().invite;
            if (!invite_output.good()) {
                std::cerr << "could not publish host test invitation\n";
                return 5;
            }
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            const SessionView view = host_probe.view();
            if (!view.pending_joins.empty()) {
                std::cout << "cross-process worker admission request received\n";
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::cerr << "cross-process host received no admission request; sent="
                  << DirectSessionTestAccess::packets_sent(host_probe)
                  << " received="
                  << DirectSessionTestAccess::packets_received(host_probe)
                  << '\n';
        return 6;
    }
    if (const char* remote_invite = std::getenv("DKR_TEST_REMOTE_INVITE");
        remote_invite != nullptr && *remote_invite != '\0') {
        DirectSession probe;
        configure_online_session(probe);
        std::string error;
        if (!probe.join(remote_invite, "Admission Probe", error)) {
            std::cerr << "join setup failed: " << error << '\n';
            return 2;
        }
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const SessionView view = probe.view();
            if (view.state == ConnectionState::AwaitingApproval ||
                view.state == ConnectionState::Lobby ||
                view.state == ConnectionState::Failed) {
                std::cout << "remote admission response: " << view.status << '\n';
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const SessionView view = probe.view();
        std::cerr << "remote admission received no response; sent="
                  << DirectSessionTestAccess::packets_sent(probe)
                  << " received="
                  << DirectSessionTestAccess::packets_received(probe)
                  << " status=" << view.status << '\n';
        return 3;
    }
    {
        // Lockstep live input deliberately uses a disposable, latency-first
        // stream. If one exact frame is lost for longer than the 16-frame
        // rolling redundancy window, the reliable repair must begin at the
        // host's contiguous hole rather than newest-63. This is the failure
        // that previously left a client with live audio and a frozen picture.
        DirectSession retained_history;
        const protocol::InputBatch live =
            DirectSessionTestAccess::retained_input_batch(
                retained_history, 100U, 180U, 121U, false);
        assert(live.first_frame == 264U);
        assert(live.inputs.size() == 16U);
        const protocol::InputBatch repair =
            DirectSessionTestAccess::retained_input_batch(
                retained_history, 100U, 180U, 121U, true);
        assert(repair.epoch != 0U);
        assert(repair.player_slot == 1U);
        assert(repair.first_frame == 121U);
        assert(repair.inputs.size() == 64U);
        assert(repair.inputs.front().buttons == 121U);
        assert(repair.inputs.back().buttons == 184U);

        // Reproduce the live lockstep freeze captured at frame 1456: Player 1
        // has committed through 1455, Player 2's game thread is parked waiting
        // for commit 1456, and therefore never authored the requested input.
        // The authenticated repair path must create exactly that one hold-last
        // sample so the host can commit instead of waiting forever.
        DirectSession unproduced_history;
        const PackedInput held{0x4321U, -27, 19};
        const protocol::InputBatch unproduced_repair =
            DirectSessionTestAccess::repair_unproduced_input(
                unproduced_history, 1455U, 1456U, held);
        assert(unproduced_repair.epoch != 0U);
        assert(unproduced_repair.player_slot == 1U);
        assert(unproduced_repair.first_frame == 1456U);
        assert(unproduced_repair.inputs.size() == 1U);
        assert(unproduced_repair.inputs.front() == held);
        // A loading client has not entered the new gameplay input epoch yet.
        // A repair request must never create race input while that authored
        // thread is still parked on the black loading frame.
        DirectSession parked_repair;
        assert(!DirectSessionTestAccess::
            repair_created_input_while_gameplay_is_parked(
                parked_repair, 1455U, 1456U, held));
        assert(DirectSessionTestAccess::enqueue_input_repair(
            retained_history));
        assert(DirectSessionTestAccess::simulation_queue_size(
                   retained_history) == 1U);
        assert(DirectSessionTestAccess::checkpoint_queue_size(
                   retained_history) == 0U);
    }
    {
        // Reliable repair retries are cumulative.  A blocked route must keep
        // only the newest repair for its peer rather than eventually ending a
        // healthy session through queue exhaustion.
        DirectSession coalesced_repairs;
        assert(DirectSessionTestAccess::enqueue_input_repair(
            coalesced_repairs, 100U));
        assert(DirectSessionTestAccess::enqueue_input_repair(
            coalesced_repairs, 100U));
        assert(DirectSessionTestAccess::enqueue_input_repair(
            coalesced_repairs, 116U));
        assert(DirectSessionTestAccess::repair_queue_size(
                   coalesced_repairs) == 1U);
    }
    {
        // A blocked disposable WebRTC input channel must not starve the
        // independent reliable channel carrying the repair for that input.
        DirectSession isolated_lanes;
        BlockingRealtimeTransport* transport =
            DirectSessionTestAccess::install_blocking_transport(isolated_lanes);
        assert(DirectSessionTestAccess::enqueue_realtime_input(isolated_lanes));
        assert(DirectSessionTestAccess::enqueue_input_repair(isolated_lanes));
        DirectSessionTestAccess::flush_outbound(isolated_lanes);
        assert(transport->realtime_attempts == 1U);
        assert(transport->authoritative_sends == 1U);
        assert(DirectSessionTestAccess::realtime_queue_size(isolated_lanes) ==
               1U);
        assert(DirectSessionTestAccess::repair_queue_size(isolated_lanes) ==
               0U);
    }
    {
        // The launcher must not be part of the transport scheduler. This is a
        // deliberately pump-free admission exchange: after host()/join(), only
        // the DirectSession workers are allowed to service UDP and retries.
        DirectSession worker_host;
        DirectSession worker_client;
        configure_online_session(worker_host);
        configure_online_session(worker_client);
        Rules worker_rules = rollback_policy::default_rules();
        std::string worker_error;
        assert(worker_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        if (!worker_host.host(0U, test_host_address(), "Worker Lobby",
                              ConnectionMethod::VirtualLan, "Host",
                              worker_rules, worker_error)) {
            std::cerr << "worker admission host setup failed: "
                      << worker_error << '\n';
            return 20;
        }
        if (!worker_client.join(worker_host.view().invite, "Client",
                                worker_error)) {
            std::cerr << "worker admission client setup failed: "
                      << worker_error << '\n';
            return 21;
        }
        const auto pending_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < pending_deadline &&
               (worker_host.view().pending_joins.empty() ||
                worker_client.view().state !=
                    ConnectionState::AwaitingApproval)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const SessionView pending_view = worker_host.view();
        if (pending_view.pending_joins.size() != 1U ||
            worker_client.view().state != ConnectionState::AwaitingApproval) {
            std::cerr << "worker admission did not reach host; sent="
                      << DirectSessionTestAccess::packets_sent(worker_client)
                      << " host-received="
                      << DirectSessionTestAccess::packets_received(worker_host)
                      << '\n';
            return 22;
        }
        if (!worker_host.approve_join(
                pending_view.pending_joins.front().request_id, worker_error)) {
            std::cerr << "worker admission approval failed: "
                      << worker_error << '\n';
            return 23;
        }
        const auto lobby_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < lobby_deadline &&
               worker_client.view().state != ConnectionState::Lobby) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (worker_client.view().state != ConnectionState::Lobby) {
            std::cerr << "worker admission approval did not reach client\n";
            return 24;
        }
    }
    {
        // A Friend lobby invitation carries one bounded admission credential.
        // The invited racer is admitted after all ordinary compatibility
        // checks, while a second/manual join still requires host approval.
        DirectSession friend_host;
        DirectSession friend_client;
        configure_online_session(friend_host);
        configure_online_session(friend_client);
        Rules rules = rollback_policy::default_rules();
        std::string error;
        assert(friend_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(friend_host.host(0U, test_host_address(), "Friend Invite Lobby",
            ConnectionMethod::VirtualLan, "Host", rules, error));
        secure::Key admission{};
        assert(friend_host.create_friend_admission(
            admission, std::chrono::minutes(5), error));
        assert(friend_client.join_friend_invite(
            friend_host.view().invite, "Invited", admission, error));
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline &&
               friend_client.view().state != ConnectionState::Lobby) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(friend_client.view().state == ConnectionState::Lobby);
        assert(friend_host.view().pending_joins.empty());
        assert(friend_host.view().room.players[1].occupied);

        DirectSession replay_client;
        configure_online_session(replay_client);
        assert(replay_client.join_friend_invite(
            friend_host.view().invite, "Replay", admission, error));
        const auto replay_deadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < replay_deadline &&
               replay_client.view().state !=
                   ConnectionState::AwaitingApproval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(replay_client.view().state == ConnectionState::AwaitingApproval);
        assert(friend_host.view().pending_joins.size() == 1U);
    }
    {
        // Preserve the proven pre-RetroArch admission path: lobby handshake
        // packets share the ordinary worker-owned UDP queue and are never sent
        // directly from the launcher/UI thread.
        DirectSession admission;
        assert(DirectSessionTestAccess::enqueue_admission(
            admission, protocol::MessageType::Hello, 1U));
        assert(DirectSessionTestAccess::enqueue_admission(
            admission, protocol::MessageType::JoinPending, 2U));
        assert(DirectSessionTestAccess::enqueue_admission(
            admission, protocol::MessageType::HelloAck, 3U));
        assert(DirectSessionTestAccess::admission_queue_size(admission) == 0U);
        assert(DirectSessionTestAccess::ordinary_queue_size(admission) == 3U);
    }
    {
        // Required checkpoint chunks use a non-evicting queue. Backpressure
        // must fail safely; it must never discard the oldest correction and
        // let the race continue with an incomplete state stream.
        DirectSession pressured;
        for (std::size_t index = 0U; index < 512U; ++index) {
            assert(DirectSessionTestAccess::enqueue_checkpoint(
                pressured, static_cast<std::uint8_t>(index)));
        }
        assert(DirectSessionTestAccess::checkpoint_queue_size(pressured) ==
               512U);
        assert(DirectSessionTestAccess::oldest_checkpoint_marker(pressured) ==
               0U);
        assert(!DirectSessionTestAccess::enqueue_checkpoint(pressured, 0xFFU));
        assert(DirectSessionTestAccess::checkpoint_queue_size(pressured) ==
               512U);
        assert(DirectSessionTestAccess::oldest_checkpoint_marker(pressured) ==
               0U);
    }
    {
        DirectSession diagnostic_host;
        DirectSession diagnostic_client;
        std::vector<std::uint8_t> host_save(512U, 0x42U);
        auto host_manifest = manifest();
        host_manifest.session_save_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(host_save.data()), host_save.size()));
        auto mismatched = manifest();
        mismatched.session_save_hash = host_manifest.session_save_hash ^ 1U;
        bool installed = false;
        std::vector<std::uint8_t> installed_save;
        diagnostic_host.configure_manifest(host_manifest);
        diagnostic_host.configure_session_save(host_save, {});
        diagnostic_client.configure_manifest(mismatched);
        diagnostic_client.configure_session_save({},
            [&](std::uint64_t match_id,
                std::span<const std::uint8_t> bytes,
                std::filesystem::path& installed_path,
                std::string& install_error) {
                assert(match_id != 0U);
                installed_save.assign(bytes.begin(), bytes.end());
                installed = true;
                installed_path = "test-online-save.bin";
                install_error.clear();
                return true;
            });
        Rules diagnostic_rules = rollback_policy::default_rules();
        std::string diagnostic_error;
        assert(diagnostic_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(diagnostic_host.host(0U, test_host_address(), "Save Diagnostic",
            ConnectionMethod::Lan, "Host", diagnostic_rules,
            diagnostic_error));
        assert(diagnostic_client.join(diagnostic_host.view().invite,
            "Client", diagnostic_error));
        pump_pair(diagnostic_host, diagnostic_client, 80);
        assert(diagnostic_client.view().state ==
               ConnectionState::AwaitingApproval);
        assert(diagnostic_host.view().pending_joins.size() == 1U);
        assert(diagnostic_host.view().pending_joins[0].compatible);
        assert(diagnostic_host.view().pending_joins[0].compatibility.find(
                   "synchronized") != std::string::npos);
        assert(diagnostic_host.approve_join(
            diagnostic_host.view().pending_joins[0].request_id,
            diagnostic_error));
        pump_pair(diagnostic_host, diagnostic_client, 80);
        assert(diagnostic_client.view().state == ConnectionState::Lobby);
        assert(installed && installed_save == host_save);
        assert(diagnostic_client.view().local_online_save_ready);
        assert(diagnostic_host.view().online_save_ready[1]);
        assert(diagnostic_client.view().online_save_ready[0]);
    }
    {
        DirectSession no_save_host;
        DirectSession mismatched_client;
        auto host_manifest = manifest();
        auto client_manifest = host_manifest;
        client_manifest.session_save_hash ^= 1U;
        no_save_host.configure_manifest(host_manifest);
        mismatched_client.configure_manifest(client_manifest);
        Rules rules = rollback_policy::default_rules();
        std::string error;
        assert(no_save_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(no_save_host.host(0U, test_host_address(), "No Save Lobby",
            ConnectionMethod::Lan, "Host", rules, error));
        assert(mismatched_client.join(no_save_host.view().invite,
            "Client", error));
        pump_pair(no_save_host, mismatched_client, 80);
        assert(mismatched_client.view().state == ConnectionState::Failed);
        assert(mismatched_client.view().status.find("validated save") !=
               std::string::npos);
    }
    {
        DirectSession guarded_host;
        DirectSession rejected_client;
        configure_online_session(guarded_host);
        configure_online_session(rejected_client);
        Rules guarded_rules = rollback_policy::default_rules();
        std::string guarded_error;
        assert(guarded_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(guarded_host.host(0U, test_host_address(), "Guarded Lobby",
            ConnectionMethod::Lan, "Host", guarded_rules, guarded_error));
        const std::string first_invite = guarded_host.view().invite;
        assert(guarded_host.set_lobby_locked(true, guarded_error));
        assert(rejected_client.join(first_invite, "Rejected", guarded_error));
        pump_pair(guarded_host, rejected_client, 80);
        assert(rejected_client.view().state == ConnectionState::Failed);
        assert(guarded_host.view().pending_joins.empty());
        assert(guarded_host.set_lobby_locked(false, guarded_error));
        assert(guarded_host.revoke_invitation(guarded_error));
        assert(guarded_host.view().invite != first_invite);

        DirectSession admitted_client;
        configure_online_session(admitted_client);
        assert(admitted_client.join(guarded_host.view().invite, "Admitted", guarded_error));
        pump_pair(guarded_host, admitted_client, 80);
        assert(guarded_host.view().pending_joins.size() == 1U);
        assert(guarded_host.approve_join(
            guarded_host.view().pending_joins[0].request_id, guarded_error));
        pump_pair(guarded_host, admitted_client, 80);
        assert(admitted_client.view().state == ConnectionState::Lobby);
        assert(guarded_host.kick_player(1U, guarded_error));
        pump_pair(guarded_host, admitted_client, 20);
        assert(admitted_client.view().state == ConnectionState::Failed);
        assert(!guarded_host.view().room.players[1].occupied);
    }
    {
        DirectSession checkpoint_host;
        DirectSession checkpoint_client;
        configure_online_session(checkpoint_host);
        configure_online_session(checkpoint_client);
        Rules checkpoint_rules = rollback_policy::default_rules();
        checkpoint_rules.automatic_input_delay = false;
        std::string checkpoint_error;
        assert(checkpoint_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(checkpoint_host.host(0U, test_host_address(), "Checkpoint Lobby",
            ConnectionMethod::Lan, "Host", checkpoint_rules,
            checkpoint_error));
        assert(checkpoint_client.join(checkpoint_host.view().invite, "Client",
                                      checkpoint_error));
        pump_pair(checkpoint_host, checkpoint_client);
        assert(checkpoint_host.approve_join(
            checkpoint_host.view().pending_joins[0].request_id,
            checkpoint_error));
        pump_pair(checkpoint_host, checkpoint_client);
        // v1.0.11 (rollback-connection): the connection check is the isolated
        // Rollback diagnostic, which never starts the legacy preflight. The
        // legacy scores and the legacy cold-boot checkpoint exchange this
        // scenario used to drive are superseded (Rollback lobbies exchange
        // checkpoints in the owned engines) and must stay gone.
        assert(checkpoint_host.request_connection_test(checkpoint_error));
        assert(!checkpoint_host.view().connection_test_active);
        pump_pair(checkpoint_host, checkpoint_client, 50);
        assert(checkpoint_host.view().connection_test_result_generation == 0U);
    }
    if constexpr (kSupportedOnlinePlayers == 4U) {
        const auto check_multi_racer = []<std::size_t PlayerCount>() {
        // Four active racers must cross the same lobby, track-baseline and
        // authored-input contracts. Earlier coverage only exercised Player 1
        // and one client, so a slot-3/slot-4 relay or acknowledgement defect
        // could survive every automated test and appear only when several
        // people began steering in a minigame.
        // Keep multi-peer transport fixtures off Windows' 1 MiB stack. Their
        // bounded queues are intentionally large even when they are empty.
        auto four_host_storage = std::make_unique<DirectSession>();
        auto four_client_2_storage = std::make_unique<DirectSession>();
        auto four_client_3_storage = std::make_unique<DirectSession>();
        auto four_client_4_storage = std::make_unique<DirectSession>();
        auto& four_host = *four_host_storage;
        auto& four_client_2 = *four_client_2_storage;
        auto& four_client_3 = *four_client_3_storage;
        auto& four_client_4 = *four_client_4_storage;
        const std::array<DirectSession*, 4U> available_sessions{
            &four_host, &four_client_2, &four_client_3, &four_client_4};
        std::array<DirectSession*, PlayerCount> sessions{};
        std::copy_n(available_sessions.begin(), PlayerCount, sessions.begin());
        for (DirectSession* session : sessions) {
            configure_online_session(*session);
        }
        Rules four_rules = rollback_policy::default_rules();
        four_rules.maximum_players = static_cast<std::uint8_t>(PlayerCount);
        four_rules.automatic_input_delay = false;
        four_rules.manual_input_delay = 2U;
        std::string four_error;
        assert(four_host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
        assert(four_host.host(0U, test_host_address(), "Four Racer Lobby",
                              ConnectionMethod::Lan, "Host", four_rules,
                              four_error));
        const std::array<std::pair<DirectSession*, const char*>, 3U> joins{{
            {&four_client_2, "Player 2"},
            {&four_client_3, "Player 3"},
            {&four_client_4, "Player 4"},
        }};
        for (const auto& [client_session, name] :
             std::span(joins).first(PlayerCount - 1U)) {
            assert(client_session->join(four_host.view().invite, name,
                                        four_error));
            pump_sessions(sessions, 80);
            const auto pending = four_host.view().pending_joins;
            assert(pending.size() == 1U);
            assert(four_host.approve_join(pending.front().request_id,
                                          four_error));
            pump_sessions(sessions, 80);
            assert(client_session->view().state == ConnectionState::Lobby);
        }
        const auto room = four_host.view().room;
        // Every instance must display the same host-confirmed save status
        // for every occupied card, not only its own acknowledgement.
        pump_sessions(sessions, 300);
        for (const auto* session : sessions)
            for (std::size_t slot = 0; slot < PlayerCount; ++slot)
                assert(session->view().online_save_ready[slot]);
        for (std::size_t slot = 0U; slot < sessions.size(); ++slot) {
            assert(room.players[slot].occupied);
            assert(sessions[slot]->view().local_slot == slot);
            assert(sessions[slot]->set_ready(true, four_error));
        }
        // v1.0.11 (rollback-only): from here the scenario drove the legacy
        // host-prediction launch and relayed input epochs through
        // DirectSession. Rollback lobbies run gameplay in the owned engines
        // instead, which a unit test does not have; that path is superseded
        // here, and the private replay and playtests (fold level 4) cover the
        // Rollback launch. The lobby contract above still holds for 3 and 4
        // racers.
        };
        check_multi_racer.template operator()<3U>();
        check_multi_racer.template operator()<4U>();
    }
    {
        // Superseded in v1.0.11 (rollback-only): strict lockstep is no longer
        // a lobby mode, so its launch is gone. It must stay gone: a Lockstep
        // lobby is refused with the unsupported-mode message.
        DirectSession lockstep_host;
        Rules lockstep_rules = rollback_policy::default_rules();
        lockstep_rules.synchronization = SynchronizationMode::Lockstep;
        lockstep_rules.rollback_window = 0U;
        std::string lockstep_error;
        assert(lockstep_host.enable_owned_backend(true));
        assert(!lockstep_host.host(0U, test_host_address(), "Lockstep Lobby",
            ConnectionMethod::Lan, "Host", lockstep_rules, lockstep_error));
        assert(lockstep_error == rollback_policy::unsupported_lobby);
    }

    DirectSession host;
    DirectSession client;
    configure_online_session(host);
    configure_online_session(client);
    Rules rules = rollback_policy::default_rules();
    rules.automatic_input_delay = false;
    rules.manual_input_delay = 2U;
    // Exercise the smallest practical runway used by real rollback lobbies.
    // The host must yield immediately once it is exhausted; it may never
    // block the authored game thread for an additional repair grace period.
    rules.rollback_window = 3U;
    std::string error;
    assert(host.enable_owned_backend(true)); // v1.0.11: Rollback lobbies need the owned engines
    assert(host.host(0U, test_host_address(), "Loopback Lobby", ConnectionMethod::Lan,
                     "Host", rules, error));
    assert(host.view().invite.starts_with("dkr-r://v7/"));
    // Experimental admission failure must not retire the already working
    // stable lobby, rotate its invite, change its rules, or touch its save.
    DirectSessionTestAccess::experimental_host_preserves_lobby(host, rules);
    assert(client.join(host.view().invite, "Client", error));
    pump_pair(host, client);
    assert(client.view().state == ConnectionState::AwaitingApproval);
    assert(host.view().pending_joins.size() == 1U);
    assert(host.view().pending_joins[0].display_name == "Client");
    assert(host.view().pending_joins[0].compatible);
    assert(host.approve_join(host.view().pending_joins[0].request_id, error));
    pump_pair(host, client);
    assert(client.view().state == ConnectionState::Lobby);
    assert(client.view().local_slot == 1U);
    assert(client.view().room.name == "Loopback Lobby");
    assert(client.view().room.rules.manual_input_delay == 2U);
    assert(client.view().input_delay_frames == 2U);
    assert(host.view().room.players[0].host);
    assert(host.view().room.players[1].occupied);

    assert(host.set_ready(true, error));
    assert(client.set_ready(true, error));
    pump_pair(host, client);
    assert(host.request_start(error));
    pump_pair_until(host, client, "client launch countdown", [&] {
        return client.view().launch_countdown_active;
    });
    const auto countdown_host_view = host.view();
    const auto countdown_client_view = client.view();
    assert(countdown_host_view.launch_countdown_active);
    assert(countdown_client_view.launch_countdown_active);
    assert(countdown_host_view.launch_countdown_generation ==
           countdown_client_view.launch_countdown_generation);
    assert(countdown_host_view.launch_countdown_remaining_ms <= 5000U);
    assert(countdown_client_view.launch_countdown_remaining_ms <= 5000U);
    // Countdown delivery is presentation telemetry, not the authority gate.
    // Simulate a lost final acknowledgement: the stronger, retransmitted
    // Prepare/Commit/Release transaction must still launch both racers.
    DirectSessionTestAccess::forget_remote_countdown_ack(host, 1U);
    DirectSessionTestAccess::expire_launch_countdown(host);
    pump_pair_until(host, client, "launch release", [&] {
        return DirectSessionTestAccess::launch_pending(host) &&
               DirectSessionTestAccess::launch_pending(client);
    });
    assert(host.consume_launch_request());
    assert(client.consume_launch_request());
    const auto host_launch = host.launch_descriptor();
    const auto client_launch = client.launch_descriptor();
    assert(host_launch && client_launch);
    assert(*host_launch == *client_launch);
    assert(host_launch->occupied_mask == 0x03U);
    assert(host_launch->player_count == 2U);
    assert(host_launch->occupied(0U));
    assert(host_launch->occupied(1U));

    mark_online_game_loaded(host, 0x1122334455667788ULL);
    mark_online_game_loaded(client, 0x1122334455667788ULL);
    pump_pair_until(host, client, "loaded race", [&] {
        return host.running() && client.running();
    });
    assert(host.running());
    assert(client.running());

    // A production rollback tick is explicitly non-blocking. Before Player 1
    // publishes frame zero, Player 2 must latch and transmit its one physical
    // sample, report Pending immediately, and remain in the live session.
    FrameInputs initial_pending_inputs{};
    assert(client.synchronize_inputs_result(
               0U, {0x4000U, -30, 8}, initial_pending_inputs,
               std::chrono::milliseconds(0)) ==
           InputSynchronizationResult::Pending);
    assert(client.running());

    // Presentation-only frontend state is not a gameplay authority contract.
    // An animation/music digest difference must be recorded without ejecting
    // either racer; the track baseline will resynchronize the portable game
    // state before simulation frame zero.
    host.submit_state_hash(0U, 0x1111111111111111ULL);
    client.submit_state_hash(0U, 0x2222222222222222ULL);
    pump_pair(host, client, 30);
    assert(host.running());
    assert(client.running());

    // Real players spend many synchronized frames in the frontend before a
    // track loads. That frontend history must not make the deliberately
    // frame-zero gameplay bootstrap look stale to the snapshot receiver.
    constexpr std::uint32_t frontend_frames = 96U;
    for (std::uint32_t frame = 0U; frame < frontend_frames; ++frame) {
        FrameInputs host_inputs{};
        FrameInputs client_inputs{};
        auto host_future = std::async(std::launch::async, [&] {
            return host.synchronize_inputs(frame, {0x8000U, 25, -5},
                                           host_inputs,
                                           std::chrono::seconds(2));
        });
        auto client_future = std::async(std::launch::async, [&] {
            return client.synchronize_inputs(frame, {0x4000U, -30, 8},
                                             client_inputs,
                                             std::chrono::seconds(2));
        });
        assert(host_future.get());
        assert(client_future.get());
        assert(host_inputs == client_inputs);
    }
    // Reproduce the real transition skew: Player 1 is waiting for the next
    // strict frontend input while Player 2 has already entered track loading.
    // The authenticated handoff must wake that wait as Suspended, preserve the
    // session, and keep the authored boundary frozen for the race baseline.
    FrameInputs parked_host_inputs{};
    auto parked_host = std::async(std::launch::async, [&] {
        return host.synchronize_inputs_result(
            frontend_frames, {0x8000U, 25, -5}, parked_host_inputs,
            std::chrono::seconds(2));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(client.begin_gameplay_handoff(frontend_frames, 42U, error));
    std::uint32_t host_transition_frame = frontend_frames;
    InputSynchronizationResult parked_result = parked_host.get();
    const auto handoff_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (parked_result == InputSynchronizationResult::Committed) {
        // This fixture has no 30 Hz game clock. Yield to BOTH transport
        // workers before another authored frame, rather than generating
        // thousands of unpaced frontend commits while the requesting peer's
        // queued handoff has not had a chance to leave its worker. Preserve
        // the real assertion: only an authenticated Suspended result passes.
        assert(std::chrono::steady_clock::now() < handoff_deadline);
        pump_pair(host, client, 1);
        ++host_transition_frame;
        parked_result = host.synchronize_inputs_result(
            host_transition_frame, {0x8000U, 25, -5}, parked_host_inputs,
            std::chrono::seconds(2));
    }
    assert(parked_result == InputSynchronizationResult::Suspended);
    assert(host.running());
    assert(client.running());
    assert(host.begin_gameplay_handoff(host_transition_frame, 42U, error));
    pump_pair(host, client, 40);
    assert(host.gameplay_handoff_suspended(host_transition_frame));
    assert(client.gameplay_handoff_suspended(frontend_frames));

    // A machine may observe an extra authored level lifecycle before the next
    // shared race (for example after a frontend or minigame transition). The
    // next race must use Player 1's epoch rather than deadlocking because the
    // two local counters no longer happen to match.
    host.begin_authoritative_phase();
    host.end_authoritative_phase();
    host.begin_authoritative_phase();
    client.begin_authoritative_phase();

    // Gameplay simulation may not start from two independently constructed
    // levels. A boss request can resolve to a separate introduction scene, so
    // the handoff must retain the requested destination while the barrier uses
    // the resolved scene. Both peers then install Player 1's portable
    // frame-zero state, acknowledge it, and only then receive Resume.
    constexpr std::uint32_t resolved_boss_intro_map = 142U;
    std::string host_barrier_error;
    std::string client_barrier_error;
    auto host_ready = std::async(std::launch::async, [&] {
        return host.wait_gameplay_ready(
            42U, resolved_boss_intro_map, 8U, std::chrono::seconds(2),
            host_barrier_error);
    });
    auto client_ready = std::async(std::launch::async, [&] {
        return client.wait_gameplay_ready(42U, resolved_boss_intro_map, 8U,
                                          std::chrono::seconds(2),
                                          client_barrier_error);
    });
    assert(host_ready.get());
    assert(client_ready.get());
    assert(DirectSessionTestAccess::scene_epoch(host) != 0U);
    assert(DirectSessionTestAccess::scene_epoch(host) ==
           DirectSessionTestAccess::scene_epoch(client));
    std::vector<std::uint8_t> gameplay_baseline(3713U);
    for (std::size_t index = 0U; index < gameplay_baseline.size(); ++index) {
        gameplay_baseline[index] = static_cast<std::uint8_t>(index * 13U + 7U);
    }
    assert(host.publish_authoritative_state(0U, gameplay_baseline, error));
    auto client_baseline = std::async(std::launch::async, [&] {
        std::vector<std::uint8_t> received;
        if (!client.wait_authoritative_state(
                0U, received, std::chrono::seconds(2)) ||
            received != gameplay_baseline) {
            return false;
        }
        client.confirm_authoritative_state(0U, false);
        return true;
    });
    assert(host.wait_authoritative_acknowledgements(
        0U, std::chrono::seconds(2)));
    assert(client_baseline.get());
    assert(host.poll_gameplay_resume(resolved_boss_intro_map, 8U,
                                     host_barrier_error) ==
           SessionPollResult::Pending);
    pump_pair_until(host, client, "client Arm announcement", [&] {
        return DirectSessionTestAccess::resume_arm_announced(client);
    });
    assert(client.poll_gameplay_resume(resolved_boss_intro_map, 8U,
                                       client_barrier_error) ==
           SessionPollResult::Pending);
    // Receiving Arm is not enough: both authored threads remain parked until
    // Player 1 has received every Armed acknowledgement and broadcasts Go.
    pump_pair_until(host, client, "host resume Ready", [&] {
        const auto result = host.poll_gameplay_resume(
            resolved_boss_intro_map, 8U, host_barrier_error);
        assert(result != SessionPollResult::Failed);
        return result == SessionPollResult::Ready;
    });
    pump_pair_until(host, client, "client resume Ready", [&] {
        const auto result = client.poll_gameplay_resume(
            resolved_boss_intro_map, 8U, client_barrier_error);
        assert(result != SessionPollResult::Failed);
        return result == SessionPollResult::Ready;
    });
    std::uint32_t host_resume_frame = 0U;
    std::uint32_t client_resume_frame = 0U;
    assert(client.complete_gameplay_handoff(client_resume_frame, error));
    assert(host.complete_gameplay_handoff(host_resume_frame, error));
    assert(host_resume_frame == client_resume_frame);

    // An authenticated datagram from the retired frontend input epoch can be
    // delayed by a VPN or Wi-Fi queue until after the race opens. It is stale
    // traffic, not a determinism failure and must never mutate the fresh race
    // timeline.
    const std::uint64_t stale_before =
        DirectSessionTestAccess::stale_epoch_packets(host);
    protocol::InputBatch stale_input{};
    stale_input.epoch =
        DirectSessionTestAccess::retired_authority_epoch(host);
    stale_input.player_slot = 1U;
    stale_input.first_frame = host_resume_frame;
    stale_input.inputs.push_back({0xFFFFU, 127, -127});
    DirectSessionTestAccess::inject_client_input(host, 1U, stale_input);
    assert(DirectSessionTestAccess::stale_epoch_packets(host) ==
           stale_before + 1U);
    assert(host.running());

    // Superseded in v1.0.11 (rollback-only): the Gekko rollback transport
    // served host-prediction lobbies. Rollback lobbies carry owned-engine
    // packets instead (send_owned_packet). It must stay gone: the legacy
    // transport refuses to send in a Rollback lobby.
    const std::array<std::uint8_t, 7> host_rollback{
        0x44U, 0x4BU, 0x52U, 0x01U, 0x80U, 25U, 0xFBU};
    assert(!host.send_rollback_packet(1U, host_rollback));

    // Superseded in v1.0.11 (rollback-only): the rest of this scenario soaked
    // host prediction (the prediction window, forced predictions, repair of
    // host-predicted commits, and the finish and track lifecycles built on
    // them). Host prediction belongs to the retired host-prediction mode
    // (host_may_predict_input); a Rollback lobby's native relay commits only
    // confirmed frames, and the owned engines do the rollback. It must stay
    // gone: Player 1 gets just the input-delay runway, then waits without
    // predicting.
    for (std::uint32_t frame = host_resume_frame;
         frame < host_resume_frame + rules.manual_input_delay; ++frame) {
        FrameInputs host_inputs{};
        assert(host.synchronize_inputs_result(frame, {0x8000U, 25, -5}, host_inputs,
                                              std::chrono::milliseconds(0)) ==
               InputSynchronizationResult::Committed);
        pump_pair(host, client, 4);
    }
    FrameInputs waiting_inputs{};
    assert(host.synchronize_inputs_result(host_resume_frame + rules.manual_input_delay, {},
                                          waiting_inputs, std::chrono::milliseconds(0)) ==
           InputSynchronizationResult::Pending);
    assert(host.view().prediction_limit_waits == 0U);

}
