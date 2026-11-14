#include "netplay/direct_session.hpp"
#include "netplay/peer_relay/transport.hpp"
#include "netplay/session_chat/chat.hpp"
#include "../../patches/spectators/native/party_link.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
// Match runtime_netplay.cpp's application-global owner. The test subprocess
// must finish its destructor after the networking translation units retire.
dkr::runtime::netplay::DirectSession application_global_owner;
} // namespace

namespace dkr::runtime::netplay {
struct DirectSessionTestAccess {
    static void stop(DirectSession& s) {
        {
            std::scoped_lock lock(s.mutex_);
            s.worker_stop_.store(true, std::memory_order_release);
        }
        s.state_changed_.notify_all();
        if (s.network_worker_.joinable())
            s.network_worker_.join();
    }
    static void initialize(std::array<DirectSession*, 3> sessions) {
        std::array<PeerAddress, 3> addresses;
        std::array<secure::Key, 3> keys;
        for (unsigned i = 0; i < 3; ++i) {
            auto& s = *sessions[i];
            stop(s);
            std::scoped_lock lock(s.mutex_);
            std::string error;
            if (!s.transport_->open(0, error) ||
                !DatagramSocket::resolve("127.0.0.1", s.transport_->local_port(), addresses[i], error))
                throw std::runtime_error(error);
            keys[i] = secure::generate_key();
            s.is_host_ = i == 0;
            s.state_ = i == 0 ? ConnectionState::Hosting : ConnectionState::Lobby;
            s.match_id_ = 5476;
            s.sender_id_ = i + 1;
            s.host_sender_id_ = 1;
            s.local_slot_ = std::uint8_t(i);
            s.local_name_ = "Relay native " + std::to_string(i + 1);
            s.room_view_.name = "Relay native control fixture";
            s.last_admission_response_ = s.last_client_request_ = std::chrono::steady_clock::now();
        }
        auto& host = *sessions[0];
        std::string error;
        Rules rules;
        rules.maximum_players = 4;
        rules.synchronization = SynchronizationMode::ExperimentalRollback;
        rules.rollback_window = 2;
        if (!host.lobby_.create("5476", "ABCDEF", "Relay native control fixture", Visibility::Private, "1",
                                host.local_name_, host.manifest_, rules, error))
            throw std::runtime_error(error);
        for (unsigned i = 1; i < 3; ++i) {
            auto& s = *sessions[i];
            s.host_address_ = addresses[0];
            s.key_ = keys[i];
            const auto slot = host.lobby_.join(std::to_string(i + 1), s.local_name_, s.manifest_, error);
            if (!slot)
                throw std::runtime_error(error);
            auto& p = host.peers_[i - 1];
            p.active = true;
            p.sender_id = i + 1;
            p.address = addresses[i];
            p.slot = *slot;
            p.key = keys[i];
        }
        host.room_view_ = host.lobby_.room();
    }
    static void pump(DirectSession& s) {
        std::scoped_lock lock(s.mutex_);
        s.pump_locked();
    }
    static void enable_party(std::array<DirectSession*, 3> sessions) {
        namespace watch = dkr::runtime::spectators;
        watch::Party roster;
        std::string error;
        if (!roster.create(1, "Native host", error) || !roster.add(2, "Native racer", std::uint8_t(1), error) ||
            !roster.add(3, "Native spectator", std::nullopt, error))
            throw std::runtime_error(error);
        for (unsigned i = 0; i < sessions.size(); ++i) {
            auto& s = *sessions[i];
            std::scoped_lock lock(s.mutex_);
            auto p = watch::party_link(&s);
            std::scoped_lock party_lock(p->mutex);
            p->requested = p->enabled = p->admitted = true;
            p->host = i == 0;
            p->local = i + 1;
            p->authority = 1;
            p->room_id = s.match_id_;
            if (!p->party.replace_verified(roster.snapshot(), error))
                throw std::runtime_error(error);
            if (i == 0)
                for (const auto& peer : s.peers_)
                    if (peer.active) {
                        watch::LinkPeer member;
                        member.id = peer.sender_id;
                        member.address = peer.address;
                        member.key = peer.key;
                        member.heard = std::chrono::steady_clock::now();
                        p->peers.push_back(member);
                    }
        }
    }
};
} // namespace dkr::runtime::netplay
int main() try {
    using namespace dkr::runtime::netplay;
    using namespace std::chrono_literals;
    dkr::relay::host_enabled = true;
    dkr::relay::volunteer_enabled = true;
    DirectSession host, client, proxy;
    const std::array<DirectSession*, 3> all{&host, &client, &proxy};
    DirectSessionTestAccess::initialize(all);
    bool ready = false;
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (std::chrono::steady_clock::now() < end) {
        for (auto* s : all)
            DirectSessionTestAccess::pump(*s);
        const auto v = dkr::relay::view(&host);
        ready = v.rows.size() == 4 && std::all_of(v.rows.begin(), v.rows.end(), [](const auto& r) {
                    return r.ready && r.local.samples >= 3 && r.remote.samples >= 2;
                });
        for (auto* s : all)
            ready = ready && dkr::relay::view(s).lobby.size() == 3;
        for (auto* s : all) {
            const auto chat = dkr::chat::channel(s)->view();
            ready = ready && chat.known && chat.members.size() == 3 &&
                    std::all_of(chat.members.begin(), chat.members.end(),
                                [](const auto& m) { return dkr::chat::valid_text(m.name, 24); });
        }
        if (ready)
            break;
        std::this_thread::sleep_for(1ms);
    }
    if (!ready)
        throw std::runtime_error(
            "Native authenticated control did not establish and measure all direct and proxy routes.\n" +
            dkr::relay::diagnostics(dkr::relay::view(&host)));
    for (auto* s : all) {
        const auto v = dkr::relay::view(s);
        if (!v.enabled || v.rows.empty())
            throw std::runtime_error("Native owner did not publish route diagnostics");
    }
    for (auto* s : all)
        if (dkr::relay::lobby_route(dkr::relay::view(s), "2").text != "Direct")
            throw std::runtime_error("Native clients did not receive the host's current lobby card route");
    auto all_pings = [&] {
        for (auto* s : all) {
            const auto v = dkr::relay::view(s);
            for (unsigned id = 2; id <= 3; ++id)
                if (!dkr::relay::lobby_card_ping(v, std::to_string(id), false, v.local == id, id - 1,
                                                 v.lobby_generation)
                         .ms)
                    return false;
        }
        return true;
    };
    const auto classic_ping_end = std::chrono::steady_clock::now() + 4s;
    while (!all_pings() && std::chrono::steady_clock::now() < classic_ping_end) {
        for (auto* s : all)
            DirectSessionTestAccess::pump(*s);
        std::this_thread::sleep_for(1ms);
    }
    if (!all_pings())
        throw std::runtime_error("Native classic clients cannot see their own and other clients' authenticated RTTs");
    for (unsigned i = 1; i < all.size(); ++i) {
        const auto room = all[i]->view();
        const auto routes = dkr::relay::view(all[i]);
        if (std::count_if(room.room.players.begin(), room.room.players.end(),
                          [](const auto& p) { return p.occupied; }) != 3)
            throw std::runtime_error("Classic client fixture did not receive all three lobby slots");
        for (const auto& player : room.room.players)
            if (player.occupied) {
                if (!player.peer_id.empty())
                    throw std::runtime_error("Classic client fixture must exercise LobbyState without peer IDs");
                const auto label =
                    dkr::relay::lobby_card_route(routes, player.peer_id, player.host, player.slot == room.local_slot,
                                                 player.slot, room.room.generation);
                if (label.checking || label.text != (player.host ? "Host" : "Direct"))
                    throw std::runtime_error(
                        "Native classic client card could not resolve its authenticated slot route");
                if (dkr::relay::lobby_card_peer(routes, player.peer_id, player.host, player.slot == room.local_slot,
                                                player.slot, room.room.generation) != std::to_string(player.slot + 1))
                    throw std::runtime_error(
                        "Native client card has no stable identity for pictures and friend controls");
            }
    }
    std::string chat_error;
    for (auto* s : all)
        if (!dkr::chat::channel(s)->send("Native authenticated room chat", chat_error))
            throw std::runtime_error(chat_error);
    bool delivered = false;
    const auto chat_end = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < chat_end) {
        for (auto* s : all)
            DirectSessionTestAccess::pump(*s);
        delivered = true;
        for (auto* s : all) {
            const auto v = dkr::chat::channel(s)->view();
            delivered = delivered && v.messages.size() == 3 &&
                        std::all_of(v.messages.begin(), v.messages.end(),
                                    [](const auto& m) { return m.delivery == dkr::chat::Delivery::Sent; });
        }
        if (delivered)
            break;
        std::this_thread::sleep_for(1ms);
    }
    if (!delivered) {
        for (auto* s : all) {
            const auto v = dkr::chat::channel(s)->view();
            std::fprintf(stderr, "chat local=%llu active=%d known=%d enabled=%d messages=%zu members=%zu status=%s\n",
                         static_cast<unsigned long long>(v.local), v.active, v.known, v.enabled, v.messages.size(),
                         v.members.size(), v.status.c_str());
            for (const auto& m : v.messages)
                std::fprintf(stderr, "  message id=%llu author=%llu request=%llu delivery=%u name=%s\n",
                             static_cast<unsigned long long>(m.id), static_cast<unsigned long long>(m.author),
                             static_cast<unsigned long long>(m.request), unsigned(m.delivery), m.name.c_str());
        }
        throw std::runtime_error("Native host/client chat did not deliver exactly once to every admitted session");
    }
    DirectSessionTestAccess::enable_party(all);
    bool party_pings = false;
    const auto ping_end = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < ping_end) {
        for (auto* s : all)
            DirectSessionTestAccess::pump(*s);
        party_pings = all_pings() && dkr::relay::view(&client).lobby_generation == 3;
        if (party_pings)
            break;
        std::this_thread::sleep_for(1ms);
    }
    if (!party_pings)
        throw std::runtime_error("Native party racer or spectator cannot see all host-relative RTTs");
    host.disconnect("Close native relay test");
    if (dkr::relay::view(&host).enabled || !dkr::relay::view(&host).rows.empty() ||
        !dkr::relay::view(&host).lobby.empty())
        throw std::runtime_error("Closing native host retained relay routes");
    client.disconnect();
    proxy.disconnect();
    for (auto* s : all)
        if (dkr::chat::channel(s)->view().active)
            throw std::runtime_error("Closing native sessions retained active chat");
    dkr::relay::host_enabled = false;
    dkr::relay::volunteer_enabled = false;
    std::puts(
        "Actual DirectSession: three authenticated owners, native extension/grants, UDP proxy legs, bidirectional measurements and host-close cleanup passed.");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
