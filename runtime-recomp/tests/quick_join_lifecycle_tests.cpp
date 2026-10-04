#define DKR_QUICK_JOIN_TESTING 1
#include "netplay/quick_join_transport.cpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <set>

namespace dkr::runtime::netplay { namespace {
struct QuickJoinTestAccess {
    static void remaining_gaps_regression() {
        QuickJoinTransport lanes(true, "ABCDE");
        const std::array<std::deque<QuickJoinTransport::InboundPacket>*, 5> queues{
            &lanes.control_inbound_, &lanes.authority_inbound_, &lanes.realtime_inbound_,
            &lanes.checkpoint_inbound_, &lanes.replica_inbound_};
        for (unsigned lane = 0; lane < 5; ++lane)
            for (unsigned count = 0; count < 100; ++count)
                queues[lane]->push_back({{}, {static_cast<std::uint8_t>(lane)}});
        std::array<unsigned, 5> serviced{};
        for (unsigned count = 0; count < 13; ++count) {
            PeerAddress source{};
            std::vector<std::uint8_t> bytes;
            std::string error;
            assert(lanes.receive(source, bytes, error));
            ++serviced[bytes[0]];
        }
        assert((serviced == std::array<unsigned, 5>{4, 4, 2, 2, 1}));

        QuickJoinTransport factory(false, "ABCDE");
        auto original = factory.create_peer("same-racer", "original");
        assert(original);
        original->route_ready.store(true);
        for (int stage = 0; stage <= 6; ++stage) {
            factory.construction_failure_stage_ = stage;
            assert(!factory.create_peer("same-racer", "replacement", true));
            assert(original->current.load() && original->route_ready.load());
            assert(factory.peers_.at(original->identifier) == original);
            assert(factory.find_peer("same-racer", "original") == original);
        }
        factory.construction_failure_stage_ = -1;
        auto replacement = factory.create_peer("same-racer", "replacement", true);
        assert(replacement && replacement->address == original->address);
        assert(!original->current.load());
        assert(factory.peers_.at(original->identifier) == replacement);
    }
    static void retry_deadline_regression() {
        QuickJoinTransport client(false, "ABCDE");
        auto peer = std::make_shared<QuickJoinTransport::Peer>();
        client.request_route_retry(peer);
        const auto deadline = steady_milliseconds() + 100U;
        client.next_route_retry_ms_.store(deadline);
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            client.request_route_retry(peer);
            assert(client.next_route_retry_ms_.load() == deadline);
        }
        client.open_.store(true);
        client.next_signaling_retry_ms_.store(deadline);
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            client.request_signaling_retry();
            assert(client.next_signaling_retry_ms_.load() == deadline);
        }
    }
    static void run() {
        // Test-only SDK configuration: a small real SCTP send buffer makes
        // backpressure reproducible on fast loopback without an OS firewall
        // or altering the production transport/dependency configuration.
        rtc::SctpSettings constrained_sctp{};
        constrained_sctp.sendBufferSize = 16U << 10U;
        constrained_sctp.recvBufferSize = 16U << 10U;
        constrained_sctp.initialCongestionWindow = 1U;
        rtc::SetSctpSettings(constrained_sctp);
        struct Server {
            std::mutex mutex;
            std::map<std::string, std::shared_ptr<rtc::WebSocket>> routes;
            std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
            std::atomic<bool> reject{false};
        };
        auto state = std::make_shared<Server>();
        rtc::WebSocketServer::Configuration config;
        config.bindAddress = "127.0.0.1"; config.port = 0;
        rtc::WebSocketServer server(config);
        server.onClient([state](auto socket) {
            { std::lock_guard lock(state->mutex); state->sockets.push_back(socket); }
            std::weak_ptr<rtc::WebSocket> weak = socket;
            socket->onOpen([state, weak] {
                const auto socket = weak.lock(); if (!socket) return;
                const auto id = socket->path().value_or("/").substr(1);
                if (state->reject.load()) { socket->send(Json{{"type", "ID-TAKEN"}}.dump()); return; }
                { std::lock_guard lock(state->mutex); state->routes[id] = socket; }
                socket->onMessage([state, id](rtc::message_variant message) {
                    if (!std::holds_alternative<std::string>(message)) return;
                    auto packet = Json::parse(std::get<std::string>(message));
                    if (!packet.contains("dst")) return;
                    std::shared_ptr<rtc::WebSocket> target;
                    { std::lock_guard lock(state->mutex);
                      const auto found = state->routes.find(packet["dst"].get<std::string>());
                      if (found != state->routes.end()) target = found->second; }
                    packet["src"] = id;
                    if (target && target->isOpen()) target->send(packet.dump());
                });
                socket->send(Json{{"type", "OPEN"}}.dump());
            });
        });
        quick_test_signaling_endpoint = "ws://127.0.0.1:" + std::to_string(server.port());
        QuickJoinTransport host(true, "ABCDE"), client(false, "ABCDE");
        std::string error;
        assert(host.open(0, error));
        const auto wait = [&](auto predicate) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!predicate() && std::chrono::steady_clock::now() < end) {
                host.service(); client.service();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(predicate());
        };
        wait([&] { return host.signaling_ready_.load(); });
        state->reject.store(true);
        assert(host.rekey_quick_join("rejected", error));
        assert(host.quick_join_code() == "ABCDE");
        wait([&] { return host.rekey_status() == QuickJoinRekeyStatus::Failed; });
        assert(host.quick_join_code() == "ABCDE" && host.signaling_ready_.load());
        state->reject.store(false);
        assert(client.open(0, error));
        host.set_quick_join_bootstrap("original");
        std::string bootstrap; PeerAddress address;
        wait([&] { return !bootstrap.empty() || client.take_quick_join_bootstrap(bootstrap, address); });
        assert(bootstrap == "original");
        wait([&] { return client.traffic_ready(address, TransportTrafficClass::Authoritative); });
        // Actual SCTP delivery: this first run is not a saturation claim.
        std::set<unsigned> received;
        unsigned sent = 0, blocked = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (received.size() < 128 && std::chrono::steady_clock::now() < end) {
            host.service(); client.service();
            if (sent < 128) {
                std::vector<std::uint8_t> packet(8192, 0);
                packet[0] = static_cast<std::uint8_t>(sent);
                const auto status = client.send_status(address, packet, TransportTrafficClass::Authoritative, error);
                if (status == DatagramSendStatus::Sent) ++sent;
                else { assert(status == DatagramSendStatus::WouldBlock); ++blocked; }
            }
            PeerAddress source; std::vector<std::uint8_t> packet;
            while (host.receive(source, packet, error)) {
                assert(packet.size() == 8192);
                assert(received.insert(packet[0]).second);
            }
            assert(error.empty());
        }
        if (sent != 128 || received.size() != 128)
            std::cerr << "Delivery incomplete: sent=" << sent << " received=" << received.size() << " blocked=" << blocked << '\n';
        assert(sent == 128 && received.size() == 128);
        std::cout << "Actual SCTP delivery: " << sent << " unique packets, " << blocked << " bounded backpressure attempts\n";
        wait([&] { return client.traffic_ready(address, TransportTrafficClass::Checkpoint); });
        unsigned checkpoint_sent = 0U;
        bool checkpoint_blocked = false;
        std::promise<void> receiver_parked;
        auto parked = receiver_parked.get_future();
        std::jthread receiver_hitch([&] {
            std::scoped_lock lock(host.mutex_);
            receiver_parked.set_value();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        });
        parked.wait();
        // Enqueue a burst without interleaving application receive/service.
        // Unlike the paced transfer above, this must hit real SCTP admission
        // pressure. The bounded burst remains below the receive queue cap.
        for (; checkpoint_sent < 256U; ++checkpoint_sent) {
            std::vector<std::uint8_t> packet(8192U, 0U);
            packet[0] = static_cast<std::uint8_t>(checkpoint_sent);
            const auto status = client.send_status(address, packet, TransportTrafficClass::Checkpoint, error);
            if (status == DatagramSendStatus::WouldBlock) { checkpoint_blocked = true; break; }
            assert(status == DatagramSendStatus::Sent);
        }
        receiver_hitch.join();
        std::cerr << "Checkpoint pressure: sent=" << checkpoint_sent << " blocked=" << checkpoint_blocked << '\n';
        assert(checkpoint_blocked && checkpoint_sent > 0U);
        const std::vector<std::uint8_t> control_payload(16U, 0xAAU);
        assert(client.send_status(address, control_payload, TransportTrafficClass::Control, error) == DatagramSendStatus::Sent);
        bool control_received = false;
        std::set<unsigned> checkpoint_received;
        wait([&] {
            PeerAddress source; std::vector<std::uint8_t> packet;
            while (host.receive(source, packet, error)) {
                if (packet == control_payload) control_received = true;
                else {
                    assert(packet.size() == 8192U);
                    assert(checkpoint_received.insert(packet[0]).second);
                }
            }
            return control_received && checkpoint_received.size() == checkpoint_sent;
        });
        std::cout << "SCTP checkpoint saturation reached after " << checkpoint_sent
                  << " packets; control and all accepted checkpoints delivered\n";
        // Real SDK mixed-stream latency, not just eventual delivery. Bulk and
        // input share one SCTP association; send timestamps stay on this host.
        auto wake_signal = std::make_shared<TransportReceiveSignal>();
        host.set_receive_signal(wake_signal);
        std::array<std::chrono::steady_clock::time_point, 60> input_times{};
        std::set<unsigned> inputs_received;
        std::vector<unsigned> input_age_ms;
        unsigned inputs_sent = 0, bulk_accepted = 0;
        const auto mixed_start = std::chrono::steady_clock::now();
        auto next_input = mixed_start;
        const auto mixed_end = mixed_start + std::chrono::seconds(6);
        while (inputs_received.size() < input_times.size() &&
               std::chrono::steady_clock::now() < mixed_end) {
            host.service(); client.service();
            const auto now = std::chrono::steady_clock::now();
            if (inputs_sent < input_times.size() && now >= next_input) {
                std::vector<std::uint8_t> input(64, 0xE1);
                input[0] = static_cast<std::uint8_t>(inputs_sent);
                if (input_times[inputs_sent] == std::chrono::steady_clock::time_point{}) input_times[inputs_sent] = now;
                const auto status = client.send_status(address, input, TransportTrafficClass::Realtime, error);
                assert(status != DatagramSendStatus::Error);
                if (status == DatagramSendStatus::Sent) { ++inputs_sent; next_input = now + std::chrono::milliseconds(33); }
            }
            if (inputs_sent < input_times.size()) {
                std::vector<std::uint8_t> bulk(14000, 0xA7);
                const auto status = client.send_status(address, bulk,
                    bulk_accepted % 2 ? TransportTrafficClass::Checkpoint : TransportTrafficClass::Replica, error);
                assert(status != DatagramSendStatus::Error);
                if (status == DatagramSendStatus::Sent) ++bulk_accepted;
            }
            PeerAddress source; std::vector<std::uint8_t> packet;
            while (host.receive(source, packet, error)) {
                if (packet.size() != 64) { assert(packet.size() == 14000); continue; }
                const auto id = packet[0];
                assert(id < input_times.size());
                assert(inputs_received.insert(id).second);
                input_age_ms.push_back(static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - input_times[id]).count()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(inputs_received.size() == input_times.size() && bulk_accepted > 10);
        assert(wake_signal->generation.load() >= input_times.size());
        std::sort(input_age_ms.begin(), input_age_ms.end());
        std::cout << "Mixed SCTP: inputs=" << inputs_received.size() << " bulk=" << bulk_accepted
            << " input p95/max(ms)=" << input_age_ms[56] << '/' << input_age_ms.back() << '\n';
        assert(input_age_ms[56] < 150 && input_age_ms.back() < 500);
        // Detaching releases the session-independent notification object; old
        // callbacks never dereference a destroyed DirectSession.
        host.set_receive_signal({});
        // Drain all accepted bulk before the route replacement checks below.
        wait([&] {
            PeerAddress source; std::vector<std::uint8_t> packet;
            while (host.receive(source, packet, error)) assert(packet.size() == 14000);
            return client.buffered_bytes(TransportTrafficClass::Checkpoint) == 0 &&
                client.buffered_bytes(TransportTrafficClass::Replica) == 0;
        });
        // An admitted route must outlive its disposable ICE connection. Age
        // it without sleeping, then force a real local WebRTC replacement.
        std::shared_ptr<QuickJoinTransport::Peer> old_client, old_host;
        { std::scoped_lock lock(client.mutex_); old_client = client.peers_.begin()->second; }
        { std::scoped_lock lock(host.mutex_); old_host = host.peers_.begin()->second; }
        client.retain_peer_route(address);
        host.retain_peer_route(old_host->address);
        const auto admitted_host_source = old_host->address;
        old_client->created -= std::chrono::seconds(31);
        old_host->created -= std::chrono::seconds(31);
        client.request_route_retry(old_client);
        client.service();
        { std::scoped_lock lock(client.mutex_); assert(client.peers_.contains(old_client->identifier)); }
        old_client->connection->close();
        wait([&] {
            std::scoped_lock lock(client.mutex_);
            const auto found = client.peers_.find(old_client->identifier);
            return found != client.peers_.end() && found->second != old_client &&
                found->second->authority_channel && found->second->authority_channel->isOpen();
        });
        wait([&] { return client.traffic_ready(address, TransportTrafficClass::Authoritative); });
        const auto retry_before_stale = client.next_route_retry_ms_.load();
        client.request_route_retry(old_client);
        assert(client.next_route_retry_ms_.load() == retry_before_stale);
        const std::vector<std::uint8_t> resumed_payload(32U, 0x6DU);
        assert(client.send_status(address, resumed_payload, TransportTrafficClass::Authoritative, error) == DatagramSendStatus::Sent);
        bool recovered_delivery = false;
        wait([&] {
            PeerAddress source; std::vector<std::uint8_t> packet;
            if (host.receive(source, packet, error)) {
                assert(source == admitted_host_source && packet == resumed_payload);
                recovered_delivery = true;
            }
            return recovered_delivery;
        });
        std::cout << "Mature admitted route recovered with unchanged addresses and stale callbacks fenced\n";
        assert(host.rekey_quick_join("replacement", error));
        wait([&] { return host.rekey_status() == QuickJoinRekeyStatus::Committed; });
        assert(host.quick_join_code() != "ABCDE");
        assert(client.traffic_ready(address, TransportTrafficClass::Control));
        // Late malformed signaling cannot globally poison the established pair.
        host.handle_signal("[]");
        host.handle_signal(R"({"type":7})");
        host.handle_signal(std::string(65537, 'x'));
        assert(!host.failed_.load());
        std::atomic<bool> sending{true};
        std::thread sender([&] {
            while (sending.load()) {
                std::string send_error;
                const std::vector<std::uint8_t> packet(1024, 0);
                const auto status = client.send_status(address, packet, TransportTrafficClass::Control, send_error);
                assert(status == DatagramSendStatus::Sent || status == DatagramSendStatus::WouldBlock);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        client.close();
        sending.store(false);
        sender.join();
        std::vector<std::uint8_t> payload(16);
        assert(client.send_status(address, payload, TransportTrafficClass::Control, error) == DatagramSendStatus::WouldBlock);
        host.close();
        server.stop();
        std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
        { std::lock_guard lock(state->mutex); sockets.swap(state->sockets); state->routes.clear(); }
        for (auto& socket : sockets) { socket->resetCallbacks(); socket->close(); }
        quick_test_signaling_endpoint.clear();
    }
};
}}
int main() {
    if (std::getenv("DKR_QUICK_JOIN_GAPS_REGRESSION")) {
        dkr::runtime::netplay::QuickJoinTestAccess::remaining_gaps_regression();
        return 0;
    }
    if (std::getenv("DKR_QUICK_JOIN_RETRY_REGRESSION")) {
        dkr::runtime::netplay::QuickJoinTestAccess::retry_deadline_regression();
        return 0;
    }
    dkr::runtime::netplay::QuickJoinTestAccess::retry_deadline_regression();
    dkr::runtime::netplay::QuickJoinTestAccess::run();
    dkr::runtime::netplay::QuickJoinTestAccess::remaining_gaps_regression();
    std::cout << "Quick Join registration, failed/successful rekey, congestion, malformed input and retirement passed.\n";
}
