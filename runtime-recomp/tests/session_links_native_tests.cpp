#include "netplay/direct_session.hpp"
#include "netplay/session_links/registry.hpp"
#include "netplay/session_links/transport.hpp"
#include "online_stability/qualification_delivery.hpp"
#include "party_link.hpp"
#include "start_gate.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace net = dkr::runtime::netplay;
namespace watch = dkr::runtime::spectators;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
namespace dkr::runtime::netplay {
struct DirectSessionTestAccess {
    static void initialize(std::array<DirectSession, 12>& sessions) {
        std::array<PeerAddress, 12> addresses;
        std::array<secure::Key, 12> keys;
        for (unsigned i = 0; i < 12; ++i) {
            auto& s = sessions[i];
            {
                std::scoped_lock lock(s.mutex_);
                s.worker_stop_.store(true);
            }
            s.state_changed_.notify_all();
            if (s.network_worker_.joinable())
                s.network_worker_.join();
            std::scoped_lock lock(s.mutex_);
            std::string error;
            check(s.transport_->open(0, error), error);
            check(DatagramSocket::resolve("127.0.0.1", s.transport_->local_port(), addresses[i], error), error);
            keys[i] = secure::generate_key();
            s.is_host_ = i == 0;
            s.state_ = i == 0 ? ConnectionState::Hosting : ConnectionState::Lobby;
            s.match_id_ = 71145;
            s.sender_id_ = i + 1;
            s.host_sender_id_ = 1;
            s.local_slot_ = i < 4 ? std::uint8_t(i) : 255;
            s.local_name_ = "Native member " + std::to_string(i + 1);
            s.room_view_.name = "Full native session links";
            s.last_admission_response_ = s.last_client_request_ = std::chrono::steady_clock::now();
            s.manifest_.architecture = i % 2 ? "aarch64" : "x86_64";
            s.session_save_.assign(512, 42);
            s.session_save_generation_ = 1;
            s.local_online_save_ready_ = s.local_online_save_acknowledged_ = true;
            s.manifest_.session_save_hash =
                stable_hash(std::string_view(reinterpret_cast<const char*>(s.session_save_.data()), 512));
        }
        auto& host = sessions[0];
        std::string error;
        Rules rules;
        rules.maximum_players = 4;
        host.host_key_pair_ = secure::generate_key_pair();
        host.invitation_capability_ = secure::generate_key();
        host.invite_ = host.make_invite("127.0.0.1");
        check(host.invite_.size() > 128, "The native fixture must cover a full direct-connect invitation.");
        rules.synchronization = SynchronizationMode::ExperimentalRollback;
        check(host.lobby_.create("71145", "ABCDEF", "Full native session links", Visibility::Private, "1",
                                 host.local_name_, host.manifest_, rules, error),
              error);
        for (unsigned i = 1; i < 4; ++i) {
            const auto slot =
                host.lobby_.join(std::to_string(i + 1), sessions[i].local_name_, sessions[i].manifest_, error);
            check(bool(slot), error);
            auto& p = host.peers_[i - 1];
            p.active = true;
            p.sender_id = i + 1;
            p.address = addresses[i];
            p.slot = *slot;
            p.key = keys[i];
            p.online_save_ready = true;
        }
        watch::Party roster;
        check(roster.create(1, host.local_name_, error), error);
        for (unsigned i = 1; i < 12; ++i)
            check(roster.add(i + 1, sessions[i].local_name_, i < 4 ? std::optional<std::uint8_t>(i) : std::nullopt,
                             error),
                  error);
        host.room_view_ = host.lobby_.room();
        for (unsigned i = 0; i < 12; ++i) {
            auto& s = sessions[i];
            s.room_view_ = host.room_view_;
            s.key_ = keys[i];
            s.host_address_ = addresses[0];
            auto hub = watch::party_link(&s);
            std::scoped_lock lock(hub->mutex);
            hub->requested = hub->enabled = hub->admitted = true;
            hub->host = i == 0;
            hub->local = i + 1;
            hub->authority = 1;
            hub->room_id = s.match_id_;
            hub->links.scope.room = s.match_id_;
            hub->links.save_owner = 1;
            if (!i)
                for (unsigned peer = 0; peer < 12; ++peer)
                    hub->links.architectures[peer + 1] = sessions[peer].manifest_.architecture;
            hub->seed = s.session_save_;
            hub->save_hash = s.manifest_.session_save_hash;
            hub->save_generation = 1;
            check(hub->party.replace_verified(roster.snapshot(), error), error);
            if (!i)
                for (unsigned peer = 1; peer < 12; ++peer) {
                    watch::LinkPeer member;
                    member.id = peer + 1;
                    member.address = addresses[peer];
                    member.key = keys[peer];
                    member.heard = std::chrono::steady_clock::now();
                    hub->peers.push_back(member);
                }
        }
    }
    static void platform_identity(DirectSession& session, unsigned index) {
        std::scoped_lock lock(session.mutex_);
        check(session.manifest_.architecture == (index % 2 ? "aarch64" : "x86_64"),
              "Host handover replaced the local architecture.");
        for (unsigned peer = 0; peer < 12; ++peer)
            if (peer != index)
                check(session.other_architecture_senders_.contains(peer + 1) == ((peer % 2) != (index % 2)),
                      "Host handover lost the cross-architecture roster.");
    }
    static void pump(DirectSession& session) {
        std::scoped_lock lock(session.mutex_);
        session.pump_locked();
    }
    static void stale_start_receipt(DirectSession& client, DirectSession& host) {
        std::scoped_lock lock(client.mutex_, host.mutex_);
        const auto source = dkr::runtime::party_start::gate(&host);
        check(bool(source->launch), "The successor did not stage a normal starting grid.");
        auto target = dkr::runtime::party_start::gate(&client);
        *target = *source;
        --target->roster;
        target->prepared.clear();
        target->committed.clear();
    }
    static void diagnostic(DirectSession& s) {
        std::scoped_lock lock(s.mutex_);
        auto h = watch::party_link(&s);
        std::scoped_lock party_lock(h->mutex);
        std::fprintf(stderr, "native id=%llu generation=%llu scope=%llu authority=%llu queued=%zu\n",
                     (unsigned long long)s.sender_id_, (unsigned long long)h->party.snapshot().generation,
                     (unsigned long long)h->links.scope.generation, (unsigned long long)s.host_sender_id_,
                     h->outgoing_bytes);
        for (const auto& p : h->peers) {
            const auto found = s.received_sequences_.find(p.id);
            std::fprintf(stderr, "  peer=%llu outbound=%llu received-control=%llu heartbeats=%u\n",
                         (unsigned long long)p.id, (unsigned long long)p.sequence,
                         (unsigned long long)(found == s.received_sequences_.end() ? 0 : found->second[0].highest),
                         p.rtt_count);
        }
    }
    static void send(DirectSession& session, unsigned target, watch::LinkKind kind,
                     std::span<const std::uint8_t> body) {
        std::scoped_lock lock(session.mutex_);
        auto hub = watch::party_link(&session);
        std::scoped_lock party_lock(hub->mutex);
        auto* peer = hub->peer(target);
        check(peer, "Missing direct test peer");
        const auto payload = watch::link_encode(kind, body);
        const auto traffic =
            kind == watch::LinkKind::Runtime ? TransportTrafficClass::Realtime : TransportTrafficClass::Control;
        protocol::Datagram packet{
            {protocol::MessageType(watch::PartyMessageType), session.match_id_, peer->sequence++, unsigned(traffic)},
            payload};
        const auto bytes = secure::seal(protocol::encode(packet, 14000), peer->key, session.sender_id_,
                                        packet.header.sequence, session.match_id_);
        std::string error;
        check(session.transport_->send_status(peer->address, bytes, traffic, error) == DatagramSendStatus::Sent, error);
    }
};
} // namespace dkr::runtime::netplay
int main(int argc, char**) try {
    {
        int owner = 0;
        auto hub = watch::party_link(&owner);
        hub->enabled = true;
        auto packet = [](unsigned kind, std::uint64_t game, std::uint64_t transfer) {
            std::vector<std::uint8_t> bytes{'D', 'P', 'R', 2, std::uint8_t(kind)};
            for (auto v : {game, std::uint64_t(1), transfer})
                for (unsigned i = 0; i < 8; ++i)
                    bytes.push_back(std::uint8_t(v >> (8 * i)));
            return bytes;
        };
        using Lane = net::TransportTrafficClass;
        using Kind = watch::LinkKind;
        for (const auto& row : std::array<std::tuple<unsigned, unsigned, std::uint64_t, std::uint64_t, Lane>, 7>{
                 {{5, 3, 77, 11, Lane::Checkpoint},
                  {5, 7, 77, 11, Lane::Replica},
                  {5, 3, 77, 12, Lane::Checkpoint},
                  {6, 3, 77, 11, Lane::Checkpoint},
                  {5, 3, 78, 11, Lane::Checkpoint},
                  {5, 19, 77, 11, Lane::Replica},
                  {5, 19, 77, 11, Lane::Realtime}}}) {
            const auto [target, kind, game, transfer, lane] = row;
            check(hub->enqueue({target, Kind::Runtime, lane, packet(kind, game, transfer)}),
                  "Could not stage checkpoint supersession fixture.");
        }
        const auto before = hub->outgoing_bytes;
        check(watch::party_retire_checkpoint(&owner, 5, 77, 12) == 58,
              "Supersession did not discard exactly the old observer chunk and history.");
        check(hub->outgoing_bytes == before - 58 && hub->outgoing[4].size() == 3 && hub->outgoing[3].size() == 1 &&
                  hub->outgoing[2].size() == 1,
              "Checkpoint supersession touched another game, viewer, racing repair or live input.");
        watch::forget_party_link(&owner);
    }
    {
        std::string error;
        auto transport = net::make_quick_join_session_transport(false, "ABCDE", error);
        check(bool(transport), error);
        net::PeerAddress reserved, renamed, other;
        check(net::links::introduce_peer(*transport, "dkrr-test-before", false, reserved, error), error);
        check(net::links::alias_peer(*transport, reserved, "dkrr-test-after", error),
              "A reserved route cannot follow successor registration: " + error);
        check(net::links::introduce_peer(*transport, "dkrr-test-after", false, renamed, error) && renamed == reserved,
              "Quiet re-registration changed the authenticated logical address.");
        check(net::links::introduce_peer(*transport, "dkrr-test-other", false, other, error), error);
        check(!net::links::alias_peer(*transport, reserved, "dkrr-test-other", error),
              "A reservation alias stole another member's route.");
        transport->release_peer_route(reserved);
        transport->release_peer_route(other);
        for (unsigned i = 0; i < 24; ++i) {
            net::PeerAddress temporary;
            check(net::links::introduce_peer(*transport, "dkrr-retired-" + std::to_string(i), false, temporary, error),
                  "Retired reservations leaked admission capacity: " + error);
            transport->release_peer_route(temporary);
        }
        transport->close();
    }
    const bool turbulence = argc > 1;
    if (turbulence)
        check(dkr::online_stability::qualification_delivery::arm(375, 25, 200, 97),
              "Native qualification settings are invalid.");
    std::array<net::DirectSession, 12> sessions;
    net::DirectSessionTestAccess::initialize(sessions);
    auto pump = [&] {
        for (auto& s : sessions)
            net::DirectSessionTestAccess::pump(s);
        std::this_thread::sleep_for(1ms);
    };
    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        pump();
        ready = true;
        for (unsigned i = 1; i < 4; ++i) {
            auto hub = watch::party_link(&sessions[i]);
            std::scoped_lock lock(hub->mutex);
            ready = ready && hub->links.introduced.size() == 2;
            for (unsigned peer = 2; peer <= 4; ++peer)
                if (peer != i + 1) {
                    const auto at = hub->links.samples.find({i + 1, peer});
                    ready = ready && at != hub->links.samples.end() && at->second.observations >= 3;
                }
        }
        if (ready)
            break;
    }
    if (!ready)
        for (unsigned i = 0; i < 4; ++i) {
            const auto status = sessions[i].view().status;
            auto h = watch::party_link(&sessions[i]);
            std::scoped_lock lock(h->mutex);
            std::fprintf(stderr, "mesh member=%u capable=%zu introduced=%zu samples=%zu members=%zu status=%s\n", i + 1,
                         h->links.capable.size(), h->links.introduced.size(), h->links.samples.size(),
                         h->party.snapshot().members.size(), status.c_str());
        }
    check(ready, "Four native racers did not establish and measure their authenticated direct input links.");
    // Seed after normal room initialization, which deliberately clears stale
    // session-link state before any UI can offer a handover.
    if (turbulence) {
        auto hub = watch::party_link(&sessions[0]);
        std::scoped_lock lock(hub->mutex);
        hub->links.save_paks.assign(131072, 0x52);
    }
    for (unsigned i = 4; i < 12; ++i) {
        auto hub = watch::party_link(&sessions[i]);
        std::scoped_lock lock(hub->mutex);
        check(hub->links.introduced.empty() && hub->peers.empty(), "Spectators unexpectedly joined the racer mesh.");
    }
    std::vector<std::uint8_t> input{'D', 'P', 'R', 2, 19};
    input.resize(29, 0x42);
    net::DirectSessionTestAccess::send(sessions[1], 3, watch::LinkKind::Runtime, input);
    bool delivered = false;
    const auto delivery_deadline = std::chrono::steady_clock::now() + (turbulence ? 3s : 1s);
    auto input_retry = std::chrono::steady_clock::now() + 100ms;
    while (std::chrono::steady_clock::now() < delivery_deadline && !delivered) {
        if (turbulence && std::chrono::steady_clock::now() >= input_retry) {
            net::DirectSessionTestAccess::send(sessions[1], 3, watch::LinkKind::Runtime, input);
            input_retry = std::chrono::steady_clock::now() + 100ms;
        }
        pump();
        watch::LinkIncoming incoming;
        while (watch::party_receive(&sessions[2], incoming))
            if (incoming.source == 2 && incoming.bytes == input)
                delivered = true;
    }
    check(delivered, "A direct client input did not pass real native AEAD and replay validation.");
    // A previous racer may have left before this twelve-member roster formed.
    // Its retired pair key must not poison the next successor's inventory.
    {
        auto hub = watch::party_link(&sessions[0]);
        std::scoped_lock lock(hub->mutex);
        hub->links.keys[{2, 99}] = net::secure::generate_key();
    }
    pump();
    {
        auto hub = watch::party_link(&sessions[0]);
        std::scoped_lock lock(hub->mutex);
        check(!hub->links.keys.contains({2, 99}), "A departed member remained in the host transfer key inventory.");
    }
    const std::vector<std::uint8_t> stop{'b', 'a', 'd'};
    net::DirectSessionTestAccess::send(sessions[1], 3, watch::LinkKind::Stop, stop);
    for (unsigned i = 0; i < 20; ++i)
        pump();
    check(!watch::party_link_view(&sessions[2]).stopping, "A directly linked racer acquired host Stop privileges.");
    const auto before = watch::party_link_view(&sessions[0]).party;
    unsigned current_host = 1;
    for (const auto target : {3U, 12U}) {
        {
            auto hub = watch::party_link(&sessions[current_host - 1]);
            std::scoped_lock lock(hub->mutex);
            hub->links.request = target;
        }
        std::optional<net::links::Proposal> offered;
        const auto offer_deadline = std::chrono::steady_clock::now() + 10s;
        while (!offered && std::chrono::steady_clock::now() < offer_deadline) {
            pump();
            auto hub = watch::party_link(&sessions[target - 1]);
            std::scoped_lock lock(hub->mutex);
            if (hub->links.handover.phase() == net::links::Phase::Offered)
                offered = hub->links.handover.proposal();
        }
        check(bool(offered), "The candidate never received a host transfer offer.");
        net::links::Write forged;
        forged.number(offered->serial).number(1, 1);
        net::DirectSessionTestAccess::send(
            sessions[current_host - 1], target, watch::LinkKind::Extension,
            net::links::encode({net::links::Kind::Accept, 71145, offered->before.generation, std::move(forged.bytes)}));
        const auto consent_deadline = std::chrono::steady_clock::now() + (turbulence ? 1500ms : 50ms);
        while (std::chrono::steady_clock::now() < consent_deadline)
            pump();
        {
            auto hub = watch::party_link(&sessions[target - 1]);
            std::scoped_lock lock(hub->mutex);
            check(hub->links.handover.phase() == net::links::Phase::Offered && !hub->links.registration_started,
                  "The old host bypassed the candidate's local consent.");
        }
        bool transferred = false;
        const auto handover_deadline = std::chrono::steady_clock::now() + (turbulence ? 90s : 20s);
        while (std::chrono::steady_clock::now() < handover_deadline) {
            pump();
            transferred = true;
            for (unsigned i = 0; i < sessions.size(); ++i) {
                auto hub = watch::party_link(&sessions[i]);
                std::scoped_lock lock(hub->mutex);
                if (i + 1 == target && hub->links.handover.phase() == net::links::Phase::Offered)
                    hub->links.consent = true;
                transferred = transferred && hub->links.finished && hub->party.snapshot().host == target;
            }
            if (transferred)
                break;
        }
        if (!transferred)
            for (unsigned i = 0; i < sessions.size(); ++i) {
                net::DirectSessionTestAccess::diagnostic(sessions[i]);
                auto hub = watch::party_link(&sessions[i]);
                std::scoped_lock lock(hub->mutex);
                std::fprintf(
                    stderr,
                    "member=%u host=%llu phase=%u ready=%u state=%u bytes=%zu expected=%zu released=%u final=%u status=%s policy=%s\n",
                    i + 1, static_cast<unsigned long long>(hub->party.snapshot().host),
                    unsigned(hub->links.handover.phase()), hub->links.handover.ready(i + 1), hub->links.prepared_state,
                    hub->links.assembly.bytes.size(), hub->links.expected_transfer, hub->links.released,
                    hub->links.finished, hub->links.status.c_str(), hub->links.handover.status().c_str());
            }
        check(transferred, "The native twelve-member handover did not finish.");
        bool healthy = true;
        {
            auto hub = watch::party_link(&sessions[target - 1]);
            std::scoped_lock lock(hub->mutex);
            for (const auto& m : hub->party.snapshot().members)
                if (m.port && m.id != target) {
                    const auto* peer = hub->peer(m.id);
                    healthy = healthy && peer && peer->rtt_count && std::chrono::steady_clock::now() - peer->heard < 5s;
                }
        }
        if (!healthy)
            net::DirectSessionTestAccess::diagnostic(sessions[target - 1]);
        check(healthy, "The successor could not pass the real lobby start connection gate.");
        for (unsigned i = 0; i < sessions.size(); ++i) {
            net::DirectSessionTestAccess::platform_identity(sessions[i], i);
            const auto v = watch::party_link_view(&sessions[i]);
            check(v.host == (i + 1 == target) && v.party.controls == before.controls,
                  "Handover changed native controller ownership or left two hosts.");
            auto hub = watch::party_link(&sessions[i]);
            std::scoped_lock lock(hub->mutex);
            check(hub->links.save_owner == 1 && !net::links::busy(hub->links),
                  "Handover changed persistent save ownership or left start locked.");
            if (turbulence)
                check(hub->links.save_paks == std::vector<std::uint8_t>(131072, 0x52),
                      "The chunked shared Pak fixture changed during handover.");
        }
        current_host = target;
    }
    // Reproduce a descriptor accepted before the new roster/Room arrives.
    // This uses the production socket, countdown, receipt validation and
    // release path after transfers to both a racer and a spectator host.
    for (unsigned i = 0; i < 4; ++i)
        watch::party_ready(&sessions[i], true);
    const auto ready_deadline = std::chrono::steady_clock::now() + 5s;
    bool all_ready = false;
    while (std::chrono::steady_clock::now() < ready_deadline && !all_ready) {
        pump();
        auto hub = watch::party_link(&sessions[current_host - 1]);
        std::scoped_lock lock(hub->mutex);
        all_ready = true;
        for (const auto& m : hub->party.snapshot().members)
            if (m.port)
                all_ready = all_ready && hub->ready[m.id];
    }
    check(all_ready, "Transferred racers did not ready for the actual synchronized-start test.");
    watch::party_start(&sessions[current_host - 1]);
    net::DirectSessionTestAccess::pump(sessions[current_host - 1]);
    net::DirectSessionTestAccess::stale_start_receipt(sessions[0], sessions[current_host - 1]);
    bool launched = false;
    const auto launch_deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < launch_deadline && !launched) {
        pump();
        launched = true;
        for (unsigned i = 0; i < sessions.size(); ++i) {
            auto hub = watch::party_link(&sessions[i]);
            std::scoped_lock lock(hub->mutex);
            launched = launched && bool(hub->launch);
        }
    }
    check(launched, "A stale pre-roster starting-grid receipt prevented synchronized launch after handover.");
    for (auto& s : sessions) {
        const auto v = watch::party_link_view(&s);
        check(v.party.members.size() == 12, "Native membership changed during mesh preparation.");
        s.disconnect();
    }
    std::puts(
        "Actual DirectSession: four racers, eight spectators, authenticated direct input, host-only controls and sequential racer/spectator host handovers passed.");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
