// Compile the project-owned implementation into this isolated test binary to
// expose fake peer delivery, without adding test entry points to release code.
#define DKR_FRIEND_TESTING 1
#include "netplay/friend_service.cpp"

#include <cassert>
#include <future>
#include <iostream>

namespace dkr::runtime::netplay {
// A timestamp is only a candidate name: parallel CTest processes may observe
// the same clock tick. Claim the directory atomically before writing profiles.
static std::filesystem::path reserve_test_directory(const std::filesystem::path& base) {
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        const auto path = std::filesystem::path(base.string() + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(path))
            return path;
    }
    throw std::runtime_error("Could not reserve an isolated Friend test directory.");
}

static std::filesystem::path test_directory(std::string_view prefix) {
    return reserve_test_directory(
        std::filesystem::temp_directory_path() /
        (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
}

struct FriendServiceTestAccess {
    using Impl = FriendService::Impl;
    using Peer = Impl::Peer;
    struct Link {
        std::shared_ptr<Peer> a, b;
        std::deque<std::pair<bool, std::string>> messages;
    };
    static void profile(FriendService& service, const std::filesystem::path& root, const char* name) {
        auto& p = *service.impl_;
        p.directory = root / name;
        p.store_path = p.directory / "friends-v1.ini";
        p.generate_identity();
        p.display_name = name;
        p.configured = true;
        p.save_locked();
        p.flush_save();
    }
    static void link(FriendService& a, FriendService& b, const Capability& capability, Link& link,
                     bool legacy = false) {
        link.a = std::make_shared<Peer>();
        link.b = std::make_shared<Peer>();
        link.a->remote_id = peer_id_for(b.impl_->local_identity);
        for (const auto& [id, request] : a.impl_->requests) {
            if (!request.incoming && request.capability == capability)
                link.a->remote_id = peer_id_for_route(request.expected_identity);
        }
        link.a->invitation_route = link.a->remote_id.starts_with("dkrr-invite-3-");
        link.b->remote_id = peer_id_for(a.impl_->local_identity);
        link.a->local_nonce = a.impl_->make_nonce();
        link.b->local_nonce = b.impl_->make_nonce();
        link.a->local_invitation = capability;
        a.impl_->peers[link.a->remote_id] = link.a;
        b.impl_->peers[link.b->remote_id] = link.b;
        link.a->test_send = [&link, legacy](std::string s) {
            if (legacy) {
                auto j = Json::parse(s);
                j.erase("request_protocol");
                s = j.dump();
            }
            link.messages.emplace_back(false, std::move(s));
        };
        link.b->test_send = [&link, legacy](std::string s) {
            if (legacy) {
                auto j = Json::parse(s);
                j.erase("request_protocol");
                s = j.dump();
            }
            link.messages.emplace_back(true, std::move(s));
        };
        a.impl_->send_hello(link.a);
        b.impl_->send_hello(link.b);
    }
    static void drain(FriendService& a, FriendService& b, Link& link, bool synchronize = true) {
        unsigned guard = 0;
        const auto drain_messages = [&] {
            while (!link.messages.empty()) {
                assert(++guard < 100);
                auto [to_a, data] = std::move(link.messages.front());
                link.messages.pop_front();
                if (to_a)
                    a.impl_->handle_message(link.a, data);
                else
                    b.impl_->handle_message(link.b, data);
            }
        };
        drain_messages();
        if (synchronize)
            for (int pass = 0; pass < 4; ++pass) {
                a.impl_->resolve_requests_locked(link.a);
                b.impl_->resolve_requests_locked(link.b);
                a.impl_->flush_save();
                b.impl_->flush_save();
                a.impl_->send_requests_locked(link.a);
                b.impl_->send_requests_locked(link.b);
                drain_messages();
            }
    }
    static void directory_isolation() {
        const auto root = test_directory("dkrr-social-directory-");
        // Force the same candidate name, as if both processes saw one tick.
        const auto first = reserve_test_directory(root / "same-tick");
        {
            std::ofstream sentinel(first / "profile");
            sentinel << "original identity";
        }
        const auto second = reserve_test_directory(root / "same-tick");
        assert(first != second && std::filesystem::is_empty(second));
        std::ifstream sentinel(first / "profile");
        assert(std::string(std::istreambuf_iterator<char>(sentinel), {}) == "original identity");
        sentinel.close();
        std::filesystem::remove_all(root);
    }
    static void run() {
        const auto root = test_directory("dkrr-social-delivery-");
        FriendService a, b, c;
        profile(a, root, "A");
        profile(b, root, "B");
        profile(c, root, "C");
        FriendInviteView invite;
        std::string error;
        assert(b.create_invite(FriendInviteLifetime::Permanent, {}, invite, error));
        ParsedFriendCode parsed;
        assert(parse_friend_code(invite.code, parsed));
        for (int i = 0; i < 100; ++i)
            assert(a.submit_friend_code(invite.code, error));
        assert(a.pending_requests().size() == 1);
        assert(c.submit_friend_code(invite.code, error));
        Link ab, cb;
        link(a, b, parsed.capability, ab);
        drain(a, b, ab);
        link(c, b, parsed.capability, cb);
        drain(c, b, cb);
        assert(b.pending_requests().size() == 2);
        assert(ab.a->authenticated && ab.b->authenticated);
        // Sender-role proof cannot be reflected back as a receiver proof.
        const auto outgoing =
            a.impl_->proof_for(b.impl_->identity.public_key, ab.a->local_nonce, ab.a->remote_nonce, true);
        const auto expected =
            a.impl_->proof_for(b.impl_->identity.public_key, ab.a->local_nonce, ab.a->remote_nonce, false);
        assert(outgoing != expected);
        // Acceptance while the sender is unavailable persists independently of
        // any live data channel, then reconciles after a profile restart.
        const auto request = std::find_if(b.impl_->requests.begin(), b.impl_->requests.end(), [&](const auto& entry) {
            return entry.second.public_key == a.impl_->identity.public_key;
        });
        assert(request != b.impl_->requests.end());
        assert(b.accept_request(request->first, error));
        b.impl_->flush_save();
        const auto identity_before = b.identity_label();
        assert(b.impl_->load_file(b.impl_->store_path));
        assert(b.identity_label() == identity_before);
        assert(b.impl_->decisions.size() == 1);
        b.impl_->send_decisions_locked(ab.b);
        drain(a, b, ab);
        assert(a.friends().size() == 1 && a.pending_requests().empty());
        a.impl_->flush_save();
        b.impl_->send_decisions_locked(ab.b);
        drain(a, b, ab);
        assert(b.impl_->decisions.begin()->second.acknowledged);
        // Replayed terminal decisions are harmless; they cannot duplicate friends.
        b.impl_->decisions.begin()->second.acknowledged = false;
        b.impl_->send_decisions_locked(ab.b);
        drain(a, b, ab);
        assert(a.friends().size() == 1);
        // Decline persists and a reconnect cannot resurrect the request.
        assert(b.pending_requests().size() == 1);
        assert(b.reject_request(b.pending_requests().front().request_id, false, error));
        b.impl_->flush_save();
        b.impl_->send_decisions_locked(cb.b);
        drain(c, b, cb);
        assert(c.pending_requests().empty());
        c.impl_->flush_save();
        b.impl_->send_decisions_locked(cb.b);
        drain(c, b, cb);
        Link cb2;
        link(c, b, parsed.capability, cb2);
        drain(c, b, cb2);
        assert(b.pending_requests().empty());
        // Retired callbacks/packets cannot mutate the replacement route.
        b.impl_->handle_message(cb.b, Json{{"kind", "removed"}}.dump());
        assert(b.friends().size() == 1);
        // Two different accepted friends may use the same wire invitation ID.
        // They must produce distinct local inbox entries and terminal states
        // must survive duplicated delivery/late cancellation.
        Impl::Friend carol{};
        carol.public_key = c.impl_->identity.public_key;
        carol.name = "Carol";
        b.impl_->friends[identity_of(carol.public_key)] = carol;
        const auto make_invitation = [&] {
            return Json{{"kind", "lobby-invite"},
                        {"id", 91ULL},
                        {"code", "ABCDE"},
                        {"players", 1U},
                        {"maximum", 2U},
                        {"expires", unix_seconds() + 120U},
                        {"admission", hex_encode(secure::generate_key())}};
        };
        const auto invitation_a = make_invitation(), invitation_c = make_invitation();
        b.impl_->handle_message(ab.b, invitation_a.dump());
        b.impl_->handle_message(cb2.b, invitation_c.dump());
        assert(b.incoming_lobby_invites().size() == 2);
        const auto a_record = b.impl_->find_incoming_invite(a.impl_->local_identity, 91U);
        const auto c_record = b.impl_->find_incoming_invite(c.impl_->local_identity, 91U);
        assert(a_record != b.impl_->incoming_lobby_invites.end() && c_record != b.impl_->incoming_lobby_invites.end());
        assert(a_record->first != c_record->first);
        FriendLobbyInviteView response;
        assert(b.respond_lobby_invite(a_record->first, true, response, error));
        b.impl_->handle_message(ab.b, invitation_a.dump());
        b.impl_->handle_message(ab.b, Json{{"kind", "lobby-invite-cancelled"}, {"id", 91ULL}}.dump());
        assert(a_record->second.status == FriendLobbyInviteStatus::Accepted);
        assert(c_record->second.status == FriendLobbyInviteStatus::Delivered);
        b.impl_->handle_message(cb2.b, Json{{"kind", "lobby-invite-decision-ack"}, {"id", 91ULL}}.dump());
        assert(!a_record->second.decision_acknowledged);
        b.impl_->handle_message(ab.b, Json{{"kind", "lobby-invite-decision-ack"}, {"id", 91ULL}}.dump());
        assert(a_record->second.decision_acknowledged);
        // A published snapshot stays immutable across mutations; unchanged
        // worker passes keep its identity so UI sorting can remain cached.
        b.impl_->publish_snapshot();
        const auto snapshot = b.snapshot();
        b.impl_->publish_snapshot();
        assert(b.snapshot() == snapshot);
        assert(b.set_display_name("Bob changed", error));
        b.impl_->publish_snapshot();
        assert(snapshot->incoming_lobby_invites.size() == 2);
        // Invalid field types and nested objects cannot mutate verified state.
        const auto count = b.pending_requests().size();
        for (const auto& bad : {"[]", R"({"kind":7})", R"({"kind":"lobby-invite","id":{}})",
                                R"({"kind":"state","lobby":{"lobby":{"lobby":{}}}})"})
            b.impl_->handle_message(ab.b, bad);
        assert(b.pending_requests().size() == count);
        // Corrupt primary must recover the same identity from its last valid
        // backup, and a failed write must never acknowledge queued decisions.
        b.impl_->flush_save();
        const auto original_path = b.impl_->store_path;
        std::filesystem::copy_file(original_path, original_path.string() + ".bak",
                                   std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream corrupt(original_path);
            corrupt << "schema=5\nidentity_secret=damaged\n";
        }
        assert(!b.impl_->load_file(original_path));
        assert(b.impl_->load_file(original_path.string() + ".bak"));
        assert(b.identity_label() == identity_before);
        b.impl_->store_path = root; // a directory is not a writable profile file
        b.impl_->save_locked();
        b.impl_->flush_save();
        assert(b.impl_->save_failed && b.impl_->saved_revision != b.impl_->save_revision);
        b.impl_->store_path = original_path;
        b.impl_->flush_save();
        assert(!b.impl_->save_failed);
        // Cancel and accept can cross while offline. The retained capability
        // must authenticate the reconnect even after both pending records are
        // gone; cancellation must not leave a one-sided accepted friendship.
        FriendService d, e;
        profile(d, root, "D");
        profile(e, root, "E");
        FriendInviteView crossed_invite;
        assert(e.create_invite(FriendInviteLifetime::Permanent, {}, crossed_invite, error));
        ParsedFriendCode crossed_code;
        assert(parse_friend_code(crossed_invite.code, crossed_code));
        assert(d.submit_friend_code(crossed_invite.code, error));
        Link de;
        link(d, e, crossed_code.capability, de);
        drain(d, e, de);
        assert(e.accept_request(e.pending_requests().front().request_id, error));
        d.impl_->publish_snapshot();
        assert(d.snapshot()->requests.size() == 1);
        assert(d.reject_request(d.pending_requests().front().request_id, false, error));
        assert(d.snapshot()->requests.empty() && d.pending_requests().empty());
        d.impl_->flush_save();
        e.impl_->flush_save();
        Link reconnected;
        link(d, e, crossed_code.capability, reconnected);
        drain(d, e, reconnected);
        assert(reconnected.a->authenticated && reconnected.b->authenticated);
        d.impl_->flush_save();
        d.impl_->send_decisions_locked(reconnected.a);
        drain(d, e, reconnected);
        assert(d.friends().empty() && e.friends().empty());
        e.impl_->flush_save();
        e.impl_->send_decisions_locked(reconnected.b);
        drain(d, e, reconnected);
        d.impl_->flush_save();
        d.impl_->send_decisions_locked(reconnected.a);
        drain(d, e, reconnected);
        assert(d.pending_requests().empty() && e.pending_requests().empty());
        Link duplicate;
        link(d, e, crossed_code.capability, duplicate);
        drain(d, e, duplicate);
        assert(d.pending_requests().empty() && e.pending_requests().empty());
        // A never-authenticated/offline cancellation disappears synchronously
        // from both public request views, but survives restart for remote
        // cleanup. Publishing again must not bring the cancelled card back.
        FriendService f, g;
        profile(f, root, "F");
        profile(g, root, "G");
        FriendInviteView offline_invite;
        assert(g.create_invite(FriendInviteLifetime::Permanent, {}, offline_invite, error));
        ParsedFriendCode offline_code;
        assert(parse_friend_code(offline_invite.code, offline_code));
        assert(f.submit_friend_code(offline_invite.code, error));
        f.impl_->publish_snapshot();
        const auto before_cancel = f.snapshot();
        const auto offline_id = f.pending_requests().front().request_id;
        assert(f.reject_request(offline_id, false, error));
        assert(f.pending_requests().empty() && f.snapshot()->requests.empty());
        assert(before_cancel->requests.size() == 1); // old snapshots remain immutable
        assert(f.impl_->requests.size() == 1 && f.impl_->requests.at(offline_id).cancelled);
        f.impl_->publish_snapshot();
        assert(f.snapshot()->requests.empty());
        // Re-adding the code is an explicit new user action, not an invisible
        // duplicate of a cancelled request. It may be cancelled immediately again.
        assert(f.submit_friend_code(offline_invite.code, error));
        assert(f.pending_requests().size() == 1 && f.snapshot()->requests.size() == 1);
        assert(f.reject_request(offline_id, false, error));
        f.impl_->flush_save();
        assert(f.impl_->load_file(f.impl_->store_path));
        f.impl_->publish_snapshot();
        assert(f.pending_requests().empty() && f.snapshot()->requests.empty());
        assert(f.status().find("Cancellation queued") == std::string::npos);
        Link fg;
        link(f, g, offline_code.capability, fg);
        drain(f, g, fg);
        assert(fg.a->authenticated && fg.b->authenticated);
        assert(f.impl_->requests.empty() && g.pending_requests().empty());
        f.impl_->flush_save();
        f.impl_->send_decisions_locked(fg.a);
        drain(f, g, fg);
        assert(g.pending_requests().empty());
        assert(f.friends().empty() && g.friends().empty());
        g.impl_->flush_save();
        f.impl_->send_decisions_locked(fg.a);
        drain(f, g, fg);
        assert(f.impl_->decisions.begin()->second.acknowledged);
        f.shutdown();
        g.shutdown();
        d.shutdown();
        e.shutdown();
        a.shutdown();
        b.shutdown();
        c.shutdown();
        std::filesystem::remove_all(root);
    }
    static void integration(unsigned soak_seconds = 0) {
        struct ServerState {
            std::mutex mutex;
            std::map<std::string, std::shared_ptr<rtc::WebSocket>> routes;
            std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
            std::map<std::string, std::chrono::steady_clock::time_point> last_heartbeat;
            std::map<std::string, unsigned> heartbeats;
            unsigned expired = 0;
            unsigned dropped_opens = 0;
            bool withhold_next_invite_open = false;
            std::set<std::string> withheld_routes;
        };
        auto server_state = std::make_shared<ServerState>();
        rtc::WebSocketServer::Configuration config;
        config.port = 0;
        config.bindAddress = "127.0.0.1";
        rtc::WebSocketServer server(config);
        server.onClient([server_state](std::shared_ptr<rtc::WebSocket> socket) {
            {
                std::lock_guard lock(server_state->mutex);
                server_state->sockets.push_back(socket);
            }
            const std::weak_ptr<rtc::WebSocket> weak(socket);
            socket->onOpen([server_state, weak] {
                const auto socket = weak.lock();
                if (!socket)
                    return;
                const auto id = socket->path().value_or("/").substr(1);
                {
                    std::lock_guard lock(server_state->mutex);
                    server_state->routes[id] = socket;
                    server_state->last_heartbeat[id] = std::chrono::steady_clock::now();
                }
                socket->onMessage([server_state, id](rtc::message_variant value) {
                    if (!std::holds_alternative<std::string>(value))
                        return;
                    auto packet = Json::parse(std::get<std::string>(value));
                    if (packet.value("type", "") == "HEARTBEAT") {
                        std::lock_guard lock(server_state->mutex);
                        server_state->last_heartbeat[id] = std::chrono::steady_clock::now();
                        ++server_state->heartbeats[id];
                        return;
                    }
                    if (!packet.contains("dst"))
                        return;
                    std::shared_ptr<rtc::WebSocket> target;
                    {
                        std::lock_guard lock(server_state->mutex);
                        const auto found = server_state->routes.find(packet["dst"].get<std::string>());
                        if (found != server_state->routes.end())
                            target = found->second;
                    }
                    packet["src"] = id;
                    if (target && target->isOpen())
                        target->send(packet.dump());
                });
                bool withhold = false;
                {
                    std::lock_guard lock(server_state->mutex);
                    if (id.starts_with("dkrr-invite-3-") && server_state->withhold_next_invite_open) {
                        server_state->withhold_next_invite_open = false;
                        ++server_state->dropped_opens;
                        withhold = true;
                    }
                    if (withhold)
                        server_state->withheld_routes.insert(id);
                    else
                        server_state->withheld_routes.erase(id);
                }
                if (!withhold)
                    socket->send(Json{{"type", "OPEN"}}.dump());
            });
        });
        // Like PeerServer, WebSocket ping/pong does not renew the application
        // registration. This catches the former missing invite heartbeat.
        std::jthread expiry_worker([server_state](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::vector<std::shared_ptr<rtc::WebSocket>> expired;
                {
                    std::lock_guard lock(server_state->mutex);
                    for (const auto& [id, socket] : server_state->routes) {
                        if (!server_state->withheld_routes.contains(id) && socket->isOpen() &&
                            std::chrono::steady_clock::now() - server_state->last_heartbeat[id] >
                                std::chrono::seconds(9)) {
                            expired.push_back(socket);
                            ++server_state->expired;
                        }
                    }
                }
                for (const auto& socket : expired)
                    socket->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        test_signaling_endpoint = "ws://127.0.0.1:" + std::to_string(server.port());
        const auto root = test_directory("dkrr-social-local-");
        FriendService a, b, c;
        a.configure(root / "A", "Alice");
        b.configure(root / "B", "Bob");
        c.configure(root / "C", "Carol");
        const auto wait = [&](auto predicate, const char* label) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(35);
            while (!predicate() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (!predicate()) {
                std::cerr << "Timed out: " << label << '\n';
                for (auto* service : {&a, &b, &c}) {
                    std::scoped_lock lock(service->impl_->mutex);
                    std::cerr << service->impl_->status << "; requests=" << service->impl_->requests.size()
                              << "; peers=" << service->impl_->peers.size() << '\n';
                    for (const auto& [route, peer] : service->impl_->peers)
                        std::cerr << "peer state=" << peer->connection->state() << " hello=" << peer->have_remote_hello
                                  << " auth=" << peer->authenticated << '\n';
                }
            }
            assert(predicate());
        };
        wait([&] { return a.presence_available() && b.presence_available() && c.presence_available(); },
             "local registration");
        FriendInviteView code;
        std::string error;
        assert(b.create_invite(FriendInviteLifetime::Permanent, {}, code, error));
        ParsedFriendCode route_code;
        assert(parse_friend_code(code.code, route_code));
        wait(
            [&] {
                std::lock_guard lock(server_state->mutex);
                return server_state->routes.contains(peer_id_for_invite(*route_code.invite_token));
            },
            "invite registration");
        // Route registration may be asynchronous; retries must retain requests.
        assert(a.submit_friend_code(code.code, error));
        assert(c.submit_friend_code(code.code, error));
        wait([&] { return b.pending_requests().size() == 2; }, "two actual WebRTC friend requests");
        wait(
            [&] {
                std::lock_guard lock(server_state->mutex);
                return server_state->heartbeats[peer_id_for_invite(*route_code.invite_token)] >= 3U;
            },
            "invite registration survives repeated heartbeat deadlines");
        {
            std::lock_guard lock(server_state->mutex);
            assert(server_state->expired == 0U);
        }
        // A lost registration acknowledgement must not leave an apparently
        // ready incoming route permanently registered only in local memory.
        {
            std::lock_guard lock(server_state->mutex);
            server_state->withhold_next_invite_open = true;
        }
        FriendInviteView delayed_code;
        assert(c.create_invite(FriendInviteLifetime::Permanent, {}, delayed_code, error));
        wait([&] { return c.invitations().front().connection_status == "Incoming friend requests ready."; },
             "missing OPEN recovery");
        {
            std::lock_guard lock(server_state->mutex);
            assert(server_state->dropped_opens == 1U);
        }
        a.shutdown();
        for (const auto& request : b.pending_requests())
            assert(b.accept_request(request.request_id, error));
        wait([&] { return c.friends().size() == 1; }, "online decision delivery");
        b.shutdown();
        b.configure(root / "B", "Bob");
        a.configure(root / "A", "Alice");
        wait([&] { return a.friends().size() == 1 && a.pending_requests().empty(); },
             "offline accept after both sides restart");
        assert(b.friends().size() == 2);
        // Simulate dropped SDK events / network loss and ensure established
        // friendships reconnect without recreating or losing request state.
        a.impl_->callbacks_lost.store(true);
        wait([&] { return a.diagnostics().find("queue overflow") != std::string::npos; },
             "bounded queue overflow recovery");
        wait(
            [&] {
                const auto friends = a.friends();
                return !friends.empty() && friends.front().online;
            },
            "presence after network reconstruction");
        if (soak_seconds) {
            for (unsigned i = 0; i < 32; ++i) {
                ParsedFriendCode offline{};
                offline.invite_token = generate_invite_token();
                offline.capability = derive_capability(*offline.invite_token);
                assert(a.submit_friend_code(encode_friend_code(offline), error));
            }
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(soak_seconds);
            std::uint64_t frames = 0, maximum_us = 0;
            std::size_t maximum_peers = 0;
            while (std::chrono::steady_clock::now() < end) {
                const auto start = std::chrono::steady_clock::now();
                a.pump({});
                assert(a.pending_requests().size() == 32U);
                assert(a.friends().size() == 1U);
                a.status();
                const auto cost = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                        .count());
                maximum_us = (std::max)(maximum_us, cost);
                {
                    std::lock_guard lock(a.impl_->mutex);
                    maximum_peers = (std::max)(maximum_peers, a.impl_->peers.size());
                    assert(a.impl_->peers.size() <= 6U);
                }
                assert(a.impl_->executor.rejected() == 0U);
                ++frames;
                std::this_thread::sleep_until(start + std::chrono::microseconds(16667));
            }
            std::cout << "Social soak seconds=" << soak_seconds << " UI passes=" << frames
                      << " maximum service us=" << maximum_us << " peak peers=" << maximum_peers << '\n';
        }
        a.shutdown();
        b.shutdown();
        c.shutdown();
        expiry_worker.request_stop();
        expiry_worker.join();
        server.stop();
        std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
        {
            std::lock_guard lock(server_state->mutex);
            sockets.swap(server_state->sockets);
            server_state->routes.clear();
        }
        for (auto& socket : sockets) {
            socket->resetCallbacks();
            socket->close();
        }
        test_signaling_endpoint.clear();
        std::filesystem::remove_all(root);
    }
    static void regressions() {
        const auto root = test_directory("dkrr-social-regression-");
        FriendService a, b;
        profile(a, root, "Sender");
        profile(b, root, "Receiver");
        FriendInviteView code;
        std::string error;
        assert(b.create_invite(FriendInviteLifetime::Permanent, {}, code, error));
        ParsedFriendCode parsed;
        assert(parse_friend_code(code.code, parsed));
        assert(a.submit_friend_code(code.code, error));
        Link ab;
        link(a, b, parsed.capability, ab);
        drain(a, b, ab, false);
        // Resolving a name is not evidence that the receiver saved the request.
        a.impl_->flush_save();
        assert(a.pending_requests().front().delivery_status.find("Delivered") == std::string::npos);
        a.impl_->send_requests_locked(ab.a);
        drain(a, b, ab, false);
        assert(b.pending_requests().size() == 1);
        const auto receiver_path = b.impl_->store_path;
        b.impl_->store_path = root;
        b.impl_->flush_save();
        assert(b.impl_->save_failed);
        b.impl_->send_requests_locked(ab.b);
        drain(a, b, ab, false);
        assert(!a.impl_->requests.begin()->second.received);
        b.impl_->store_path = receiver_path;
        b.impl_->flush_save();
        // Lost receipt: replaying the same transaction returns its receipt,
        // without creating a second inbox row.
        b.impl_->send_requests_locked(ab.b);
        ab.messages.clear();
        a.impl_->send_requests_locked(ab.a);
        drain(a, b, ab);
        assert(a.impl_->requests.begin()->second.received && b.pending_requests().size() == 1);
        const auto first_id = a.pending_requests().front().request_id;
        assert(a.reject_request(first_id, false, error));
        a.impl_->flush_save();
        a.impl_->send_decisions_locked(ab.a);
        drain(a, b, ab);
        assert(b.pending_requests().empty());
        assert(a.submit_friend_code(code.code, error));
        const auto second_id = a.pending_requests().front().request_id;
        assert(second_id != first_id);
        // No new connection or authentication event occurs here.
        drain(a, b, ab);
        assert(b.pending_requests().size() == 1 && a.impl_->requests.at(second_id).received);
        a.impl_->handle_message(ab.a, Json{{"kind", "decision"},
                                           {"id", 900ULL},
                                           {"action", "rejected"},
                                           {"request_id", first_id},
                                           {"capability", hex_encode(parsed.capability)}}
                                          .dump());
        assert(a.pending_requests().size() == 1);
        b.impl_->handle_message(
            ab.b,
            Json{{"kind", "friend-request"}, {"request_id", first_id}, {"capability", hex_encode(parsed.capability)}}
                .dump());
        assert(b.pending_requests().size() == 1); // cancelled replay stays cancelled
        assert(b.reject_request(b.pending_requests().front().request_id, false, error));
        b.impl_->flush_save();
        b.impl_->send_decisions_locked(ab.b);
        drain(a, b, ab);
        assert(a.pending_requests().empty());
        assert(a.submit_friend_code(code.code, error));
        Link replacement;
        link(a, b, parsed.capability, replacement);
        drain(a, b, replacement);
        assert(b.pending_requests().size() == 1); // prior decline does not suppress a new attempt
        assert(a.retry_request(a.pending_requests().front().request_id, error));
        assert(!a.retry_request(a.pending_requests().front().request_id, error));
        assert(b.accept_request(b.pending_requests().front().request_id, error));
        b.impl_->flush_save();
        b.impl_->send_decisions_locked(replacement.b);
        drain(a, b, replacement);
        assert(a.friends().size() == 1 && b.friends().size() == 1);
        assert(a.diagnostics().find(code.code) == std::string::npos);
        assert(a.diagnostics().find(a.impl_->local_identity) == std::string::npos);
        // Multiple distinct codes belonging to one person resolve every
        // transaction; a previously resolved request cannot steal the match.
        FriendService c, d;
        profile(c, root, "MultipleSender");
        profile(d, root, "MultipleReceiver");
        FriendInviteView code1, code2;
        assert(d.create_invite(FriendInviteLifetime::Permanent, {}, code1, error));
        assert(d.create_invite(FriendInviteLifetime::Permanent, {}, code2, error));
        ParsedFriendCode p1, p2;
        assert(parse_friend_code(code1.code, p1));
        assert(parse_friend_code(code2.code, p2));
        assert(c.submit_friend_code(code1.code, error));
        Link cd1;
        link(c, d, p1.capability, cd1);
        drain(c, d, cd1);
        assert(c.submit_friend_code(code2.code, error));
        Link cd2;
        link(c, d, p2.capability, cd2);
        drain(c, d, cd2);
        for (const auto& request : c.pending_requests()) {
            assert(request.display_name == "MultipleReceiver");
            assert(request.delivery_status.find("Delivered") != std::string::npos);
        }
        assert(d.pending_requests().size() == 2);
        // Legacy v4 peers still connect, without claiming a durable receipt.
        FriendService old_a, old_b;
        profile(old_a, root, "LegacySender");
        profile(old_b, root, "LegacyReceiver");
        FriendInviteView old_code;
        assert(old_b.create_invite(FriendInviteLifetime::Permanent, {}, old_code, error));
        ParsedFriendCode old_parsed;
        assert(parse_friend_code(old_code.code, old_parsed));
        assert(old_a.submit_friend_code(old_code.code, error));
        Link old;
        link(old_a, old_b, old_parsed.capability, old, true);
        drain(old_a, old_b, old);
        assert(old_b.pending_requests().size() == 1);
        assert(old_a.pending_requests().front().delivery_status.find("unconfirmed") != std::string::npos);
        assert(old_b.accept_request(old_b.pending_requests().front().request_id, error));
        old_b.impl_->flush_save();
        old_b.impl_->send_decisions_locked(old.b);
        drain(old_a, old_b, old);
        assert(old_a.friends().size() == 1);
        // Historical decisions are sent in bounded, rotating batches rather
        // than overflowing the shared callback executor on reconnect.
        old.messages.clear();
        old_b.impl_->decisions.clear();
        for (std::uint64_t i = 1; i <= 100; ++i)
            old_b.impl_->decisions[i] =
                Impl::Decision{i, old_a.impl_->identity.public_key, old_parsed.capability, "rejected", false, i};
        old.b->last_decision_sent = 0;
        std::set<std::uint64_t> seen;
        for (unsigned pass = 0; pass < 4; ++pass) {
            old_b.impl_->send_decisions_locked(old.b);
            assert(old.messages.size() == 32);
            for (const auto& [to_a, packet] : old.messages)
                seen.insert(Json::parse(packet).at("id").get<std::uint64_t>());
            old.messages.clear();
        }
        assert(seen.size() == 100);
        old_b.impl_->decisions.clear();
        // Real schema-5 bytes migrate without rotating the identity or
        // dropping offline requests. The exact old file is retained once.
        FriendService migrated;
        profile(migrated, root, "Migrated");
        assert(migrated.submit_friend_code(old_code.code, error));
        migrated.impl_->flush_save();
        const auto identity = migrated.identity_label();
        std::ifstream current(migrated.impl_->store_path, std::ios::binary);
        std::string line, legacy_bytes;
        while (std::getline(current, line)) {
            if (line.starts_with("checksum="))
                continue;
            if (line.starts_with("schema="))
                line = "schema=5";
            if (line.starts_with("request=")) {
                std::size_t position = 0;
                for (int comma = 0; comma < 7; ++comma)
                    position = line.find(',', position) + 1;
                assert(position > 0);
                line.resize(position - 1);
            }
            legacy_bytes += line + '\n';
        }
        current.close();
        std::array<std::uint8_t, 32> checksum{};
        crypto_blake2b(checksum.data(), checksum.size(), reinterpret_cast<const std::uint8_t*>(legacy_bytes.data()),
                       legacy_bytes.size());
        legacy_bytes += "checksum=" + hex_encode(checksum) + "\n";
        {
            std::ofstream fixture(migrated.impl_->store_path, std::ios::binary | std::ios::trunc);
            fixture << legacy_bytes;
        }
        assert(migrated.impl_->load_file(migrated.impl_->store_path));
        assert(migrated.identity_label() == identity && migrated.pending_requests().size() == 1);
        assert(migrated.impl_->requests.begin()->second.legacy_retry);
        migrated.impl_->save_locked();
        migrated.impl_->flush_save();
        assert(!migrated.impl_->save_failed);
        std::ifstream backup(migrated.impl_->store_path.string() + ".pre-receipts", std::ios::binary);
        assert(std::string(std::istreambuf_iterator<char>(backup), {}) == legacy_bytes);
        assert(migrated.impl_->load_file(migrated.impl_->store_path));
        assert(migrated.identity_label() == identity && migrated.pending_requests().size() == 1);
        backup.close();
        migrated.shutdown();
        FriendService recovered;
        profile(recovered, root, "RecoveredLegacy");
        {
            std::ofstream fixture(recovered.impl_->store_path, std::ios::binary | std::ios::trunc);
            fixture << "corrupt primary";
        }
        {
            std::ofstream fixture(recovered.impl_->store_path.string() + ".bak", std::ios::binary | std::ios::trunc);
            fixture << legacy_bytes;
        }
        assert(!recovered.impl_->load_file(recovered.impl_->store_path));
        assert(recovered.impl_->load_file(recovered.impl_->store_path.string() + ".bak"));
        recovered.impl_->save_locked();
        recovered.impl_->flush_save();
        assert(!recovered.impl_->save_failed && recovered.identity_label() == identity);
        std::ifstream recovery_backup(recovered.impl_->store_path.string() + ".pre-receipts", std::ios::binary);
        assert(std::string(std::istreambuf_iterator<char>(recovery_backup), {}) == legacy_bytes);
        recovery_backup.close();
        recovered.shutdown();
        old_a.shutdown();
        old_b.shutdown();
        c.shutdown();
        d.shutdown();
        a.shutdown();
        b.shutdown();
        std::filesystem::remove_all(root);
    }
};
} // namespace dkr::runtime::netplay

int main(int argc, char** argv) {
    using namespace dkr::runtime::netplay;
    // Exercise callback re-entry through the actual executor: enqueue while
    // holding a non-recursive state mutex must not invoke the callback inline.
    SocialExecutor executor;
    std::mutex state;
    std::promise<void> closed;
    auto completion = closed.get_future();
    executor.start([] {}, [] { assert(false); });
    {
        std::lock_guard lock(state);
        assert(executor.post([&] {
            std::lock_guard callback_lock(state);
            closed.set_value();
        }));
    }
    assert(completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    executor.stop();
    assert(!executor.post([] {}));
    FriendServiceTestAccess::directory_isolation();
    FriendServiceTestAccess::run();
    FriendServiceTestAccess::regressions();
    if (argc > 1 && std::string_view(argv[1]) == "--integration")
        FriendServiceTestAccess::integration();
    if (argc > 1 && std::string_view(argv[1]) == "--soak") {
        const unsigned duration = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 1800U;
        assert(duration >= 30U && duration <= 3600U);
        FriendServiceTestAccess::integration(duration);
    }
    std::cout << "Social delivery, persistence, proof roles, deduplication and callback tests passed.\n";
}
