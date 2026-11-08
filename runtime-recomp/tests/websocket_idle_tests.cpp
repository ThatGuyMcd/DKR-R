#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

template<class Predicate>
void await(Predicate ready, const char* failure) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(failure);
        std::this_thread::sleep_for(2ms);
    }
}

int main() {
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::WebSocketServer::Configuration server_config;
    server_config.bindAddress = "127.0.0.1";
    server_config.port = 0;
    server_config.enableTls = true;
    rtc::WebSocketServer server(server_config);
    std::mutex accepted_mutex;
    std::vector<std::shared_ptr<rtc::WebSocket>> accepted;
    server.onClient([&](std::shared_ptr<rtc::WebSocket> peer) {
        peer->onMessage([weak = std::weak_ptr(peer)](rtc::message_variant message) {
            if (auto socket = weak.lock()) socket->send(std::move(message));
        });
        std::lock_guard lock(accepted_mutex);
        accepted.push_back(std::move(peer));
    });
    // This isolated loopback fixture uses the server's ephemeral certificate.
    // Production certificate verification is unchanged by this test.
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        rtc::WebSocket::Configuration config;
        config.disableTlsVerification = true;
        config.pingInterval = 20ms;
        config.maxOutstandingPings = 10;
        rtc::WebSocket client(config);
        std::atomic<unsigned> echoes = 0;
        const std::string probe = "idle-tls-probe-" + std::to_string(cycle);
        client.onMessage([&](rtc::message_variant message) {
            if (auto text = std::get_if<std::string>(&message); text && *text == probe) ++echoes;
        });
        client.open("wss://127.0.0.1:" + std::to_string(server.port()) + "/idle");
        await([&] { return client.isOpen(); }, "TLS connection did not open");
        client.send(probe);
        await([&] { return echoes.load() == 1; }, "initial echo missing");
        // Idle notifications travel TCP -> TLS -> WebSocket -> TLS ping.
        // A callback under the TLS send lock deadlocks this exact path.
        std::this_thread::sleep_for(1200ms);
        client.send(probe);
        await([&] { return echoes.load() == 2; }, "echo missing after idle pings");
        client.close();
        await([&] { return client.isClosed(); }, "close blocked after idle pings");
    }
    server.stop();
    std::puts("TLS idle ping, echo and close: four cycles passed");
}
