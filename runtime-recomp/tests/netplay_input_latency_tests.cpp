// Deterministic transport-clock harness for the production DirectSession
// input/commit path. No toy vehicle simulation, SDL, sockets, saves or profiles.
// Wall-clock GPU/OS scheduling and actual Internet routes remain playtests.
#include "direct_session.hpp"
#include "netplay_pacing_policy.hpp"
#include "transport_send_policy.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <vector>

namespace dkr::runtime::netplay {
struct TimedWire {
    struct Packet {
        unsigned due, from, to;
        std::vector<std::uint8_t> bytes;
        TransportTrafficClass traffic = TransportTrafficClass::Control;
        bool background = false;
    };
    unsigned now = 0, up = 0, down = 0, jitter = 0, loss_every = 0, sent = 0;
    bool outage = false;
    bool shaped = false;
    std::array<unsigned, 4> route_up{}, route_down{}, route_jitter{};
    unsigned bytes_per_ms = 0;
    std::array<unsigned, 4> tx_available{}; // host uplink is shared by all clients
    std::deque<Packet> packets;
};

class TimedTransport final : public SessionTransport {
public:
    TimedTransport(TimedWire& wire, unsigned slot, bool quick_join = false)
        : wire_(wire), slot_(slot), quick_join_(quick_join) {}
    bool quick_join() const override { return quick_join_; }
    bool open(std::uint16_t, std::string&) override { return true; }
    void close() override {}
    bool is_open() const override { return true; }
    std::uint16_t local_port() const override { return 0; }
    bool receive(PeerAddress&, std::vector<std::uint8_t>&, std::string&) override { return false; }
    DatagramSendStatus send_status(const PeerAddress& destination,
        std::span<const std::uint8_t> bytes, TransportTrafficClass traffic,
        std::string&) override {
        if (wire_.outage && slot_ != 0U) return DatagramSendStatus::WouldBlock;
        const unsigned peer = slot_ ? slot_ : destination.storage[0];
        if (wire_.shaped) {
            std::size_t lane = 0, aggregate = 0;
            for (const auto& pending : wire_.packets) {
                if (pending.from != slot_ || pending.to != destination.storage[0]) continue;
                aggregate += pending.bytes.size();
                if (pending.traffic == traffic) lane += pending.bytes.size();
            }
            if (!quick_join_admit(traffic, lane, aggregate, bytes.size() + 1U))
                return DatagramSendStatus::WouldBlock;
        }
        const unsigned serial = ++wire_.sent;
        if (traffic == TransportTrafficClass::Realtime && wire_.loss_every &&
            serial % wire_.loss_every == 0) return DatagramSendStatus::Sent;
        // Deterministic positive jitter reorders packets in every lane.
        const auto jitter = wire_.shaped ? wire_.route_jitter[peer] : wire_.jitter;
        const unsigned delay = (wire_.shaped ? (slot_ ? wire_.route_up[peer] : wire_.route_down[peer])
            : (slot_ ? wire_.up : wire_.down)) + (jitter ? serial * 17U % jitter : 0U);
        unsigned admitted = wire_.now;
        if (wire_.bytes_per_ms) {
            admitted = (std::max)(admitted, wire_.tx_available[slot_]) +
                static_cast<unsigned>((bytes.size() + wire_.bytes_per_ms - 1U) / wire_.bytes_per_ms);
            wire_.tx_available[slot_] = admitted;
        }
        wire_.packets.push_back({admitted + delay, slot_, destination.storage[0],
                                {bytes.begin(), bytes.end()}, traffic});
        return DatagramSendStatus::Sent;
    }
private:
    TimedWire& wire_;
    unsigned slot_;
    bool quick_join_;
};

struct DirectSessionTestAccess {
    static PeerAddress address(unsigned slot) {
        PeerAddress result{}; result.size = 1; result.storage[0] = static_cast<std::uint8_t>(slot);
        return result;
    }
    static void initialize(DirectSession& s, TimedWire& wire, unsigned slot,
                           unsigned players, unsigned delay, const secure::Key& key,
                           bool lockstep = false, bool quick_join = false) {
        // This harness owns a simulated transport clock. Stop the real worker
        // before installing it; all pumping is explicit and single-threaded.
        // Match production shutdown's condition-variable contract. Publishing
        // outside this lock can race the offline worker entering its wait.
        {
            std::scoped_lock lock(s.mutex_);
            s.worker_stop_.store(true, std::memory_order_release);
        }
        s.state_changed_.notify_all();
        s.network_worker_.join();
        s.transport_ = std::make_unique<TimedTransport>(wire, slot, quick_join);
        s.is_host_ = slot == 0;
        s.state_ = ConnectionState::Running;
        s.local_slot_ = static_cast<std::uint8_t>(slot);
        s.sender_id_ = slot + 1U; s.host_sender_id_ = 1U;
        s.host_address_ = address(0); s.key_ = key;
        s.match_id_ = 0x44524B55; s.input_epoch_ = 11; s.scene_epoch_ = 3;
        s.authoritative_phase_active_ = true;
        s.authority_lifecycle_ = DirectSession::AuthorityLifecycle::Racing;
        s.input_delay_ = static_cast<std::uint8_t>(delay);
        s.launch_descriptor_ = LaunchDescriptor{};
        auto& d = *s.launch_descriptor_;
        d.match_id = s.match_id_; d.player_count = static_cast<std::uint8_t>(players);
        d.occupied_mask = static_cast<std::uint8_t>((1U << players) - 1U);
        d.synchronization = lockstep ? SynchronizationMode::Lockstep : SynchronizationMode::Rollback;
        d.rollback_window = lockstep ? 0 : 2; d.input_delay_frames = s.input_delay_;
        Rules rules{}; rules.maximum_players = static_cast<std::uint8_t>(players);
        rules.automatic_input_delay = false; rules.manual_input_delay = s.input_delay_;
        rules.rollback_window = d.rollback_window; rules.synchronization = d.synchronization;
        std::string error;
        assert(s.lobby_.create("test", "test-code", "test", Visibility::Private,
            "host", "host", {}, rules, error));
        for (unsigned i = 1; i < players; ++i) {
            assert(s.lobby_.join(std::to_string(i), "client", {}, error));
            if (slot == 0) s.peers_[i] = {true, i + 1U, address(i),
                static_cast<std::uint8_t>(i), key};
        }
        s.room_view_ = s.lobby_.room();
        s.reset_input_delivery_tracking_locked(0);
    }
    static void deliver(TimedWire& wire, std::vector<std::unique_ptr<DirectSession>>& peers) {
        for (auto it = wire.packets.begin(); it != wire.packets.end();) {
            if (it->due > wire.now) { ++it; continue; }
            auto p = std::move(*it); it = wire.packets.erase(it);
            if (p.background) continue;
            auto& s = *peers[p.to];
            std::uint64_t sender = 0, sequence = 0;
            std::vector<std::uint8_t> plain;
            std::string error;
            assert(secure::open(p.bytes, s.key_, s.match_id_, sender, sequence, plain));
            protocol::Datagram packet{};
            assert(protocol::decode(plain, packet, error));
            assert(sequence == packet.header.sequence && sender == p.from + 1U);
            // Duplicates/reordering are deliberately delivered to the actual
            // handlers, additionally exercising idempotence beyond envelope replay filtering.
            if (p.to == 0) s.handle_host_packet(address(p.from), sender, packet);
            else s.handle_client_packet(packet);
        }
    }
    static void revisions_and_bounds() {
        TimedWire wire;
        const auto key = secure::generate_key();
        DirectSession host;
        initialize(host, wire, 0, 2, 1, key);
        protocol::InputBatch batch{};
        batch.epoch = host.authority_epoch(); batch.player_slot = 1;
        batch.first_frame = 1; batch.inputs = {{0x8000, 60, 0}}; batch.revisions = {2};
        auto inject = [&](protocol::MessageType type) {
            host.handle_host_packet(address(1), 2,
                {{type, host.match_id_, 1, batch.first_frame}, protocol::encode_input_batch(batch)});
        };
        inject(protocol::MessageType::Input);
        batch.inputs[0] = {}; batch.revisions[0] = 1;
        inject(protocol::MessageType::InputRepair);
        assert(host.timeline_.inputs_for(1)[1].buttons == 0x8000);
        batch.revisions[0] = 3;
        inject(protocol::MessageType::InputRepair);
        assert(host.timeline_.inputs_for(1)[1].buttons == 0);
        batch.inputs[0].buttons = 0x8000; batch.revisions[0] = 2;
        inject(protocol::MessageType::Input);
        assert(host.timeline_.inputs_for(1)[1].buttons == 0);
        FrameInputs result{};
        for (unsigned frame = 0; frame < 4; ++frame) {
            assert(host.synchronize_inputs_result(frame, {}, result,
                std::chrono::milliseconds(0)) == InputSynchronizationResult::Committed);
        }
        const auto immutable = host.frame_commits_.at(1);
        const auto began = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 100; ++i) {
            assert(host.synchronize_inputs_result(4, {}, result,
                std::chrono::milliseconds(0)) == InputSynchronizationResult::Pending);
        }
        assert(std::chrono::steady_clock::now() - began < std::chrono::seconds(1));
        assert(host.next_commit_frame_ == 4 && host.prediction_limit_waits_ > 0);
        batch.revisions[0] = 4;
        const auto late_before = host.late_inputs_discarded_;
        inject(protocol::MessageType::Input);
        assert(host.frame_commits_.at(1) == immutable);
        assert(host.late_inputs_discarded_ == late_before + 1U);
        // Retired scene revisions cannot suppress an initial sample in a new epoch.
        host.clear_input_history_locked();
        ++host.input_epoch_;
        inject(protocol::MessageType::Input);
        assert(host.received_input_revisions_[1].empty());
        // Bounded queue: no backlog grows with a blocked realtime transport.
        host.high_priority_outbound_.clear();
        for (unsigned i = 0; i < 10000; ++i)
            host.enqueue_outbound(address(1), protocol::MessageType::Input, {1}, i);
        assert(host.high_priority_outbound_.size() == 1);
        host.high_priority_outbound_.front().enqueued -= std::chrono::seconds(1);
        host.enqueue_outbound(address(1), protocol::MessageType::FrameCommit, {1});
        host.commit_outbound_.back().enqueued -= std::chrono::seconds(1);
        host.flush_outbound_locked();
        assert(host.high_priority_outbound_.empty());
        assert(host.commit_outbound_.empty() && !wire.packets.empty()); // ledger sent, not aged out
        const auto full = host.view();
        const auto pacing = host.pacing_view();
        assert(full.state == pacing.state && full.input_epoch == pacing.input_epoch);
        assert(full.network_rtt_ms == pacing.network_rtt_ms &&
            full.network_jitter_ms == pacing.network_jitter_ms &&
            full.authoritative_input_frame == pacing.authoritative_input_frame &&
            full.simulation_wake_generation == pacing.simulation_wake_generation);

        // Sample accounting: echo retransmits cannot inflate percentiles;
        // predicted/mismatching consumption must not disappear from outcomes.
        host.clear_input_history_locked();
        host.next_commit_frame_ = 0;
        host.store_local_input_locked(10, {0x8000, 30, 0});
        protocol::FrameCommitPayload commit{};
        commit.frame = 10; commit.inputs[0] = {0x8000, 30, 0};
        host.record_input_latency_locked(commit, false);
        host.record_input_latency_locked(commit, false);
        assert(host.view().input_echo_latency.samples == 1);
        host.record_input_latency_locked(commit, true);
        host.record_input_latency_locked(commit, true);
        assert(host.view().input_consume_latency.samples == 1);
        host.store_local_input_locked(11, {0x8000, 30, 0});
        commit.frame = 11; commit.predicted_mask = 1;
        host.record_input_latency_locked(commit, true);
        assert(host.view().predicted_local_samples == 1);
        host.store_local_input_locked(12, {0x8000, 30, 0});
        commit.frame = 12; commit.predicted_mask = 0; commit.inputs[0] = {};
        host.record_input_latency_locked(commit, true);
        assert(host.view().mismatched_local_samples == 1);

        if (std::getenv("DKR_LATENCY_BENCHMARK")) {
            constexpr unsigned iterations = 10000;
            unsigned sink = 0;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) sink += host.view().input_epoch;
            const auto middle = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) sink += host.pacing_view().input_epoch;
            const auto end = std::chrono::steady_clock::now();
            std::cout << "view microbench 10000 calls: full-us="
                << std::chrono::duration_cast<std::chrono::microseconds>(middle - start).count()
                << " pacing-us=" << std::chrono::duration_cast<std::chrono::microseconds>(end - middle).count()
                << " sink=" << sink << '\n';
        }
    }
    static void run(unsigned players, unsigned up, unsigned down, unsigned jitter,
                    unsigned loss, bool automatic, bool outage,
                    bool lockstep = false, bool quick_join = false) {
        TimedWire wire; wire.up = up; wire.down = down;
        wire.jitter = jitter; wire.loss_every = loss;
        const unsigned delay = automatic ? host_authoritative_input_delay_frames(
            up + down + jitter, jitter, loss ? 10.0F : 0.0F, false) : 1;
        const auto key = secure::generate_key();
        std::vector<std::unique_ptr<DirectSession>> peers;
        for (unsigned slot = 0; slot < players; ++slot) {
            peers.push_back(std::make_unique<DirectSession>());
            initialize(*peers.back(), wire, slot, players, delay, key, lockstep, quick_join);
        }
        std::vector<unsigned> next_tick(players, 0), observed(players, 0), last_buttons(players, 0);
        unsigned maximum_response = 0, pending = 0;
        // Five deliberate held press/release edges. Each lasts one second;
        // this specifically detects the reported 1-3 SECOND client response,
        // without pretending sub-tick button taps survive arbitrary packet loss.
        for (wire.now = 0; wire.now <= 6500; wire.now += 5) {
            wire.outage = outage && wire.now >= 2500 && wire.now < 3000;
            deliver(wire, peers);
            for (unsigned slot = 0; slot < players; ++slot) {
                auto& s = *peers[slot];
                // Explicit simulated 50-ms recovery timer. Production uses
                // steady_clock; the protocol/repair implementation is shared.
                if (wire.now % 50 == 0) {
                    if (slot == 0) s.advertise_missing_inputs_locked();
                    else if (!s.local_history_.empty()) {
                        s.send_local_history(s.local_input_submitted_frame_);
                        s.send_local_history(s.local_input_submitted_frame_, true);
                    }
                }
                if (wire.now < next_tick[slot]) continue;
                if (slot == players - 1U && wire.now >= 4000 && wire.now < 4200) continue;
                PackedInput local{};
                if (slot != 0 && wire.now >= 1000 && wire.now < 6000) {
                    local.buttons = (wire.now / 1000U) % 2U ? 0x8000U : 0U;
                    local.stick_x = local.buttons ? 60 : -60;
                }
                FrameInputs inputs{};
                const unsigned frame = s.next_commit_frame_;
                const auto status = s.synchronize_inputs_result(frame, local, inputs,
                    std::chrono::milliseconds(0));
                assert(status != InputSynchronizationResult::Failed);
                if (status == InputSynchronizationResult::Committed) {
                    assert(inputs == peers[0]->frame_commits_.at(frame).inputs);
                    s.report_simulation_progress(frame);
                    next_tick[slot] = wire.now + (slot == 0 ? 35U : 25U);
                    for (unsigned client = 1; client < players; ++client) {
                        if (slot != client) continue;
                        if (inputs[client].buttons != last_buttons[client]) {
                            const unsigned edge = (observed[client] + 1U) * 1000U;
                            assert(wire.now >= edge); // never respond before the physical change
                            maximum_response = std::max(maximum_response, wire.now - edge);
                            ++observed[client]; last_buttons[client] = inputs[client].buttons;
                        }
                    }
                } else { ++pending; next_tick[slot] = wire.now + 5; }
            }
            for (auto& s : peers) s->flush_outbound_locked();
            assert(wire.packets.size() < 2000);
        }
        for (unsigned slot = 1; slot < players; ++slot) {
            assert(observed[slot] == 6U); // no dropped or duplicated held edges
            assert(peers[0]->next_commit_frame_ >= peers[slot]->next_commit_frame_);
            assert(peers[0]->next_commit_frame_ - peers[slot]->next_commit_frame_ < 20);
            assert(peers[slot]->local_history_.size() < 300);
        }
        assert(peers[0]->next_commit_frame_ > (lockstep ? 20U : 80U));
        for (const auto& [frame, commit] : peers[0]->frame_commits_) {
            assert(commit.frame == frame);
            if (lockstep) assert(commit.predicted_mask == 0);
        }
        assert(maximum_response < 900U);
        std::cout << "peers=" << players << " route=" << up << '/' << down
            << "ms jitter=" << jitter << " loss_every=" << loss << " delay=" << delay
            << " mode=" << (lockstep ? "lockstep" : "rollback")
            << " transport-history=" << (quick_join ? "QuickJoin" : "UDP")
            << " outage=" << outage << " authored=" << peers[0]->next_commit_frame_
            << " max_edge_delay_ms=" << maximum_response << " pending_polls=" << pending << '\n';
    }

    // Equal 30-Hz clocks, actual production catch-up and host backpressure.
    // Different routes share host upload bandwidth. Unlike the older smoke
    // matrix above, a guest is not granted a permanent 40-Hz advantage.
    static void paced(unsigned players, unsigned rtt, bool asymmetric,
                      bool disturbance, bool background, bool strict) {
        TimedWire wire; wire.shaped = true;
        wire.bytes_per_ms = background ? 128U : 0U; // shared 1 Mbit/s upload
        unsigned maximum_rtt = rtt, maximum_jitter = 0;
        for (unsigned slot = 1; slot < players; ++slot) {
            const bool poor = asymmetric && slot == players - 1;
            wire.route_up[slot] = poor ? rtt / 4 + 35 : rtt / 2;
            wire.route_down[slot] = poor ? rtt * 3 / 4 + 35 : rtt / 2;
            wire.route_jitter[slot] = poor ? 25 : 0;
            maximum_rtt = (std::max)(maximum_rtt, wire.route_up[slot] + wire.route_down[slot]);
            maximum_jitter = (std::max)(maximum_jitter, wire.route_jitter[slot]);
        }
        if (disturbance) wire.loss_every = 11;
        const auto delay = host_authoritative_input_delay_frames(maximum_rtt + maximum_jitter,
            maximum_jitter, disturbance ? 10.0F : 0.0F, false);
        std::vector<std::unique_ptr<DirectSession>> peers;
        const auto key = secure::generate_key();
        for (unsigned slot = 0; slot < players; ++slot) {
            peers.push_back(std::make_unique<DirectSession>());
            initialize(*peers.back(), wire, slot, players, delay, key, strict, true);
        }
        std::array<ClientCatchUpController, 4> controllers;
        std::array<double, 4> next_tick{};
        std::array<unsigned, 4> observed{}, missed{}, last_edge{}, max_debt{};
        std::array<LatencyHistogram, 4> response;
        unsigned pending = 0, holds = 0;
        const auto lead = host_authority_lead_limit(maximum_rtt, maximum_jitter, delay);
        for (wire.now = 0; wire.now <= 15000; ++wire.now) {
            wire.outage = disturbance && wire.now >= 8000 && wire.now < 8300;
            deliver(wire, peers);
            // Serialized background state transfer, never interpreted as a
            // real snapshot by DirectSession. Models queue pressure only;
            // actual codec and native apply are tested in their own suites.
            if (background && wire.now % 200 == 0) {
                for (unsigned slot = 1; slot < players; ++slot) {
                    const std::vector<std::uint8_t> state(4096, 0);
                    std::string error;
                    const auto count = wire.packets.size();
                    if (peers[0]->transport_->send_status(address(slot), state,
                        TransportTrafficClass::Replica, error) == DatagramSendStatus::Sent &&
                        wire.packets.size() > count) wire.packets.back().background = true;
                }
            }
            for (unsigned slot = 0; slot < players; ++slot) {
                auto& s = *peers[slot];
                if (wire.now % 50 == 0) {
                    if (slot == 0) s.advertise_missing_inputs_locked();
                    else if (!s.local_history_.empty()) {
                        s.send_local_history(s.local_input_submitted_frame_);
                        s.send_local_history(s.local_input_submitted_frame_, true);
                        if (s.next_commit_frame_) {
                            // Reproduce timer expiry on the simulated clock.
                            s.last_simulation_progress_datagram_ = {};
                            s.simulation_progress_time_[slot] = {};
                            s.report_simulation_progress(s.next_commit_frame_ - 1);
                        }
                    }
                }
                if (wire.now < next_tick[slot]) continue;
                if (disturbance && slot == players - 1 && wire.now >= 5000 && wire.now < 5300) continue;
                const auto frame = s.next_commit_frame_;
                const auto debt = s.authoritative_input_frame_ > frame ? s.authoritative_input_frame_ - frame : 0;
                if (wire.now >= 12000) max_debt[slot] = (std::max)(max_debt[slot], debt);
                if (!slot && s.host_should_backpressure(frame, lead)) {
                    ++holds; next_tick[slot] = wire.now + 4; continue;
                }
                const auto target = strict ? 0U : rollback_replica_target_debt(
                    wire.route_up[slot] + wire.route_down[slot], wire.route_jitter[slot], delay);
                const auto pacing = controllers[slot].update({slot != 0, debt, target,
                    s.contiguous_authoritative_commits(frame, 4)});
                // Each second includes an 80-ms tap and release; stick_y is
                // the unique sample edge ID, so missing taps are counted.
                const unsigned second = wire.now / 1000;
                unsigned edge = 0;
                if (second >= 1 && second <= 12)
                    edge = (second - 1) * 2 + (wire.now % 1000 >= 80 ? 2 : 1);
                else if (second > 12) edge = 24;
                const PackedInput local{static_cast<std::uint16_t>(edge % 2 ? 0x8000 : 0),
                    static_cast<std::int8_t>(edge % 2 ? 60 : -60), static_cast<std::int8_t>(edge)};
                FrameInputs inputs{};
                const auto status = s.synchronize_inputs_result(frame, local, inputs, std::chrono::milliseconds(0));
                assert(status != InputSynchronizationResult::Failed);
                if (status == InputSynchronizationResult::Committed) {
                    assert(inputs == peers[0]->frame_commits_.at(frame).inputs);
                    if (strict) assert(peers[0]->frame_commits_.at(frame).predicted_mask == 0);
                    s.report_simulation_progress(frame);
                    next_tick[slot] = wire.now + 1000000.0 / (30.0 * (slot ? pacing.pacing_scale_milli : 1000U));
                    const auto current_edge = static_cast<unsigned>(static_cast<std::uint8_t>(inputs[slot].stick_y));
                    if (current_edge > last_edge[slot]) {
                        missed[slot] += current_edge - last_edge[slot] - 1;
                        last_edge[slot] = current_edge; ++observed[slot];
                        const auto sampled = ((current_edge + 1) / 2) * 1000 + (current_edge % 2 ? 0 : 80);
                        assert(wire.now >= sampled);
                        response[slot].observe(wire.now - sampled);
                    }
                } else { ++pending; next_tick[slot] = wire.now + 4; }
            }
            for (auto& s : peers) s->flush_outbound_locked();
            assert(wire.packets.size() < 2000);
        }
        assert(peers[0]->next_commit_frame_ > (background ? 180U : 350U));
        for (unsigned slot = 0; slot < players; ++slot) {
            const auto summary = response[slot].summary();
            assert(last_edge[slot] == 24 && observed[slot] + missed[slot] == 24);
            // Tight route-specific steady-state deadline; adverse scenarios
            // report missed edges explicitly and use a recovery bound.
            // The 1-Mbit/s shared host route with three clients is an overload
            // recovery case, not a 30-Hz or zero-missed-tap performance promise.
            const auto budget = background ? 1500U : (disturbance ? 1100U : 650U);
            if (summary.maximum_ms >= budget)
                std::cerr << "Latency gate: peers=" << players << " rtt=" << rtt
                    << " asym=" << asymmetric << " disturbance=" << disturbance << " bulk=" << background
                    << " slot=" << slot << " maximum=" << summary.maximum_ms << " budget=" << budget << '\n';
            assert(summary.maximum_ms < budget);
            if (!asymmetric && !disturbance && !background && rtt == 0) {
                assert(missed[slot] == 0);
                // The host consumes locally before the clients receive its
                // commitment. Give each role its own one-frame regression gate.
                const unsigned healthy_budget = slot == 0 ? 104U : 138U;
                assert(summary.p95_ms <= healthy_budget);
                // A one-authored-frame regression must fail this same gate.
                assert(summary.p95_ms + 33 > healthy_budget);
            }
            assert(max_debt[slot] <= lead);
            std::cout << "paced peers=" << players << " rtt=" << rtt << " asym=" << asymmetric
                << " disturbance=" << disturbance << " bulk=" << background << " strict=" << strict
                << " slot=" << slot << " delay=" << static_cast<unsigned>(delay)
                << " p50/95/99/max=" << summary.p50_ms << '/' << summary.p95_ms << '/' << summary.p99_ms
                << '/' << summary.maximum_ms << " missed=" << missed[slot] << " settled_debt=" << max_debt[slot]
                << " ticks=" << peers[slot]->next_commit_frame_ << " pending=" << pending << " holds=" << holds << '\n';
        }
    }
};
} // namespace dkr::runtime::netplay

int main() {
    std::cout << std::unitbuf;
    using dkr::runtime::netplay::DirectSessionTestAccess;
    DirectSessionTestAccess::revisions_and_bounds();
    for (unsigned players : {2U, 3U, 4U}) {
        for (unsigned rtt : {0U, 20U, 60U, 100U, 150U, 200U})
            DirectSessionTestAccess::paced(players, rtt, false, false, false, false);
        DirectSessionTestAccess::paced(players, 100, true, false, true, false);
        DirectSessionTestAccess::paced(players, 100, true, true, true, false);
        DirectSessionTestAccess::paced(players, 20, false, false, false, true);
    }
    for (unsigned players : {2U, 3U, 4U}) {
        for (bool automatic : {false, true}) {
            for (bool quick_join : {false, true}) {
                for (unsigned rtt : {0U, 20U, 60U, 100U, 150U, 200U}) {
                    DirectSessionTestAccess::run(players, rtt / 2, rtt / 2,
                        0, 0, automatic, false, false, quick_join);
                }
                DirectSessionTestAccess::run(players, 30, 70, 35, 7, automatic, false, false, quick_join);
                DirectSessionTestAccess::run(players, 100, 100, 50, 5, automatic, true, false, quick_join);
                DirectSessionTestAccess::run(players, 10, 10, 0, 0, automatic, false, true, quick_join);
                DirectSessionTestAccess::run(players, 30, 70, 35, 7, automatic, false, true, quick_join);
            }
        }
    }
}
