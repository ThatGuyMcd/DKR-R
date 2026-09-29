#include "session_transport.hpp"
#include "callback_lifetime.hpp"
#include "transport_send_policy.hpp"
#include "social_executor.hpp"
#include "netplay_protocol.hpp"

#if DKR_NETPLAY_WEBRTC
#include <nlohmann/json.hpp>
#include "social_json_policy.hpp"
#include <rtc/rtc.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace dkr::runtime::netplay {
namespace {

constexpr std::string_view kCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string normalize_code(std::string_view code) {
    std::string result;
    result.reserve(code.size());
    for (const char character : code) {
        if (std::isspace(static_cast<unsigned char>(character)) ||
            character == '-') continue;
        result.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    return result;
}

std::string random_token(std::size_t size, std::string_view alphabet) {
    std::random_device seed;
    std::mt19937 generator(seed());
    std::uniform_int_distribution<std::size_t> distribution(
        0U, alphabet.size() - 1U);
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0U; index < size; ++index) {
        result.push_back(alphabet[distribution(generator)]);
    }
    return result;
}

std::uint64_t steady_milliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

PeerAddress quick_address(std::uint64_t identifier) {
    PeerAddress result{};
    result.size = 12U;
    result.storage[0] = 'D';
    result.storage[1] = 'K';
    result.storage[2] = 'Q';
    result.storage[3] = 'J';
    for (std::size_t index = 0U; index < 8U; ++index) {
        result.storage[4U + index] = static_cast<std::uint8_t>(
            identifier >> ((7U - index) * 8U));
    }
    return result;
}

#if DKR_NETPLAY_WEBRTC

using Json = nlohmann::json;
#ifdef DKR_QUICK_JOIN_TESTING
std::string quick_test_signaling_endpoint;
#endif

// libdatachannel's Mbed TLS backend deliberately has no implicit platform
// trust store. Prefer the host's maintained CA bundle where one is exposed,
// then retain the current PeerJS chain root as a portable fallback (notably
// for a standalone Windows build). Signaling remains certificate-verified;
// DKR-R never disables TLS validation to make Quick Join appear to work.
constexpr std::string_view kPeerJsFallbackRoot = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

std::string signaling_ca_bundle() {
    if (const char* configured = std::getenv("SSL_CERT_FILE");
        configured != nullptr && configured[0] != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(configured, error)) {
            return configured;
        }
    }
#if !defined(_WIN32)
    constexpr std::string_view candidates[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
    };
    for (const std::string_view candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return std::string(candidate);
        }
    }
#endif
    return std::string(kPeerJsFallbackRoot);
}

class QuickJoinTransport final : public SessionTransport {
public:
    QuickJoinTransport(bool host, std::string code)
        : host_(host), code_(std::move(code)) {}
    ~QuickJoinTransport() override { close(); }

    bool open(std::uint16_t, std::string& error) override {
        close();
        callbacks_ = CallbackLifetime{};
        connection_closer_.start([] {}, [] {});
        closing_.store(false, std::memory_order_release);
        open_.store(true, std::memory_order_release);
        failed_.store(false, std::memory_order_release);
        signaling_established_once_.store(false, std::memory_order_release);
        signaling_connecting_.store(true, std::memory_order_release);
        signaling_retry_requested_.store(false, std::memory_order_release);
        route_retry_requested_.store(false, std::memory_order_release);
        next_route_retry_ms_.store(0U, std::memory_order_release);
        next_signaling_retry_ms_.store(0U, std::memory_order_release);
        local_peer_id_ = host_
            ? "dkrr-31-" + code_
            : "dkrr-31-client-" + random_token(16U, kCodeAlphabet);
        remote_host_id_ = "dkrr-31-" + code_;
        const std::uint64_t generation =
            next_signaling_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        signaling_generation_.store(generation);
        signaling_started_ms_.store(steady_milliseconds());
        websocket_ = make_signaling_socket(generation);
        try {
            websocket_->open(signaling_url(local_peer_id_));
        } catch (const std::exception& exception) {
            signaling_connecting_.store(false, std::memory_order_release);
            set_failure(exception.what());
            error = "Quick Join could not contact the rendezvous service.";
            return false;
        }
        last_heartbeat_ = std::chrono::steady_clock::now();
        error.clear();
        return true;
    }

    void close() override {
        closing_.store(true, std::memory_order_release);
        callbacks_.retire();
        open_.store(false, std::memory_order_release);
        signaling_ready_.store(false, std::memory_order_release);
        signaling_connecting_.store(false, std::memory_order_release);
        signaling_retry_requested_.store(false, std::memory_order_release);
        route_retry_requested_.store(false, std::memory_order_release);
        signaling_generation_.fetch_add(1U, std::memory_order_acq_rel);
        std::shared_ptr<rtc::WebSocket> websocket;
        std::shared_ptr<PendingRekey> pending;
        std::map<std::uint64_t, std::shared_ptr<Peer>> peers;
        std::vector<std::shared_ptr<Peer>> retired;
        {
            std::scoped_lock lock(mutex_);
            websocket = std::move(websocket_);
            pending = std::move(pending_rekey_);
            rekey_state_.store(QuickJoinRekeyStatus::Idle);
            peers.swap(peers_);
            remote_to_identifier_.clear();
            pinned_routes_.clear();
            retired.swap(retired_peers_);
            control_inbound_.clear();
            authority_inbound_.clear();
            checkpoint_inbound_.clear();
            realtime_inbound_.clear();
            replica_inbound_.clear();
            bootstrap_.clear();
            bootstrap_peer_ = {};
        }
        for (auto& [identifier, peer] : peers) {
            peer->current.store(false, std::memory_order_release);
            if (peer->control_channel) peer->control_channel->close();
            if (peer->authority_channel) peer->authority_channel->close();
            if (peer->realtime_channel) peer->realtime_channel->close();
            if (peer->replica_channel) peer->replica_channel->close();
            if (peer->checkpoint_channel) peer->checkpoint_channel->close();
            if (peer->connection) peer->connection->close();
        }
        for (const auto& peer : retired) {
            peer->current.store(false, std::memory_order_release);
            if (peer->connection) peer->connection->close();
        }
        if (websocket) websocket->close();
        if (pending && pending->socket) pending->socket->close();
        connection_closer_.stop();
    }

    bool is_open() const override {
        return open_.load(std::memory_order_acquire);
    }
    std::uint16_t local_port() const override { return 0U; }
    bool quick_join() const override { return true; }
    void retain_peer_route(const PeerAddress& address) override {
        std::scoped_lock lock(mutex_);
        for (const auto& [id, peer] : peers_) {
            if (peer->address == address &&
                (pinned_routes_.contains(peer->remote_id) || pinned_routes_.size() < 4U)) {
                pinned_routes_[peer->remote_id] = {id, address};
                return;
            }
        }
    }
    void release_peer_route(const PeerAddress& address) override {
        std::scoped_lock lock(mutex_);
        std::erase_if(pinned_routes_, [&](const auto& entry) {
            return entry.second.second == address;
        });
    }
    std::size_t maximum_plaintext_datagram_bytes() const override {
        return protocol::kMaximumQuickJoinDatagramBytes;
    }
    void discard_received(TransportTrafficClass traffic) override {
        std::scoped_lock lock(mutex_);
        switch (traffic) {
        case TransportTrafficClass::Checkpoint:
            for (const auto& packet : checkpoint_inbound_) release_packet_bytes_locked(packet);
            checkpoint_inbound_.clear();
            break;
        case TransportTrafficClass::Control:
            for (const auto& packet : control_inbound_) release_packet_bytes_locked(packet);
            control_inbound_.clear();
            break;
        case TransportTrafficClass::Authoritative:
            for (const auto& packet : authority_inbound_) release_packet_bytes_locked(packet);
            authority_inbound_.clear();
            break;
        case TransportTrafficClass::Realtime:
            for (const auto& packet : realtime_inbound_) release_packet_bytes_locked(packet);
            realtime_inbound_.clear();
            break;
        case TransportTrafficClass::Replica:
            for (const auto& packet : replica_inbound_) release_packet_bytes_locked(packet);
            replica_inbound_.clear();
            break;
        }
    }
    std::string quick_join_code() const override {
        std::scoped_lock lock(mutex_);
        return code_;
    }

    bool rekey_quick_join(std::string bootstrap, std::string& error) override {
        if (!host_ || !is_open() || rekey_status() == QuickJoinRekeyStatus::Pending) {
            error = "Only an active host without a pending code replacement can rekey.";
            return false;
        }
        auto pending = std::make_shared<PendingRekey>();
        {
            std::scoped_lock lock(mutex_);
            do { pending->code = random_token(5U, kCodeAlphabet); }
            while (pending->code == code_);
            pending->bootstrap = std::move(bootstrap);
            pending->generation = next_signaling_generation_.fetch_add(1U) + 1U;
            pending_rekey_ = pending;
        }
        rekey_state_.store(QuickJoinRekeyStatus::Pending);
        pending->socket = make_signaling_socket(pending->generation, pending);
        try { pending->socket->open(signaling_url("dkrr-31-" + pending->code)); }
        catch (...) { pending->failed.store(true); }
        error.clear();
        return true;
    }

    QuickJoinRekeyStatus rekey_status() const override {
        return rekey_state_.load(std::memory_order_acquire);
    }

    DatagramSendStatus send_status(
        const PeerAddress& destination,
        std::span<const std::uint8_t> bytes,
        TransportTrafficClass traffic,
        std::string& error) override {
        std::shared_ptr<rtc::DataChannel> channel;
        std::shared_ptr<Peer> target;
        std::array<std::shared_ptr<rtc::DataChannel>, 5> peer_channels{};
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [identifier, peer] : peers_) {
                if (peer->address == destination) {
                    target = peer;
                    peer_channels = {peer->control_channel, peer->authority_channel,
                        peer->realtime_channel, peer->checkpoint_channel, peer->replica_channel};
                    switch (quick_join_delivery_class(traffic)) {
                    case TransportTrafficClass::Checkpoint:
                        channel = peer->checkpoint_channel;
                        break;
                    case TransportTrafficClass::Control:
                        channel = peer->control_channel;
                        break;
                    case TransportTrafficClass::Authoritative:
                        channel = peer->authority_channel;
                        break;
                    case TransportTrafficClass::Realtime:
                        channel = peer->realtime_channel;
                        break;
                    case TransportTrafficClass::Replica:
                        channel = peer->replica_channel;
                        break;
                    }
                    break;
                }
            }
        }
        if (!channel || !channel->isOpen()) {
            request_route_retry(target);
            error = "The Quick Join racer channel is not open.";
            return DatagramSendStatus::WouldBlock;
        }
        // Checkpoints have a separate reliable/unordered lane from control.
        // Realtime traffic is tiny and must never wait behind more than a few
        // frames. Replica traffic is disposable latest-wins data, so stop
        // feeding SCTP as soon as one useful state is already in flight.
        const auto buffered = channel->bufferedAmount();
        std::size_t aggregate = 0;
        for (const auto& lane : peer_channels) if (lane) aggregate += lane->bufferedAmount();
        if (!quick_join_admit(traffic, buffered, aggregate, bytes.size() + 1U)) {
            error.clear();
            return DatagramSendStatus::WouldBlock;
        }
        rtc::binary framed;
        framed.reserve(bytes.size() + 1U);
        framed.push_back(static_cast<std::byte>(1U));
        for (const std::uint8_t byte : bytes) {
            framed.push_back(static_cast<std::byte>(byte));
        }
        try {
            const bool immediate = channel->send(std::move(framed));
            if (!quick_join_send_accepted(immediate, channel->isOpen(),
                    target && target->connection->state() == rtc::PeerConnection::State::Connected,
                    target && target->current.load(std::memory_order_acquire))) {
                request_route_retry(target);
                error.clear();
                return DatagramSendStatus::WouldBlock;
            }
        } catch (const std::exception& exception) {
            // A channel can close between isOpen() and send(). Preserve the
            // authenticated packet on DirectSession's queue and rebuild the
            // logical route instead of silently dropping a reliable commit.
            request_route_retry(target);
            error = std::string("Quick Join route is rebuilding: ") +
                    exception.what();
            return DatagramSendStatus::WouldBlock;
        }
        error.clear();
        return DatagramSendStatus::Sent;
    }

    bool receive(PeerAddress& source, std::vector<std::uint8_t>& bytes,
                 std::string& error) override {
        std::scoped_lock lock(mutex_);
        if (failed_.load(std::memory_order_acquire)) {
            error = failure_;
            return false;
        }
        std::deque<InboundPacket>* queue = nullptr;
        // A sustained control/ledger backlog must not starve a required
        // checkpoint. Preserve priority with weighted service, but guarantee
        // each ready lane an opportunity across bounded receive batches.
        constexpr std::array<unsigned, 13> schedule{
            0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4};
        const std::array<std::deque<InboundPacket>*, 5> queues{
            &control_inbound_, &authority_inbound_, &realtime_inbound_,
            &checkpoint_inbound_, &replica_inbound_};
        for (unsigned attempt = 0; attempt < schedule.size(); ++attempt) {
            auto* candidate = queues[schedule[receive_service_cursor_]];
            receive_service_cursor_ = (receive_service_cursor_ + 1U) % schedule.size();
            if (!candidate->empty()) { queue = candidate; break; }
        }
        if (queue == nullptr) {
            source = {};
            bytes.clear();
            error.clear();
            return false;
        }
        source = queue->front().source;
        release_packet_bytes_locked(queue->front());
        bytes = std::move(queue->front().bytes);
        queue->pop_front();
        error.clear();
        return true;
    }

    void set_receive_signal(std::shared_ptr<TransportReceiveSignal> signal) override {
        std::scoped_lock lock(mutex_);
        receive_signal_ = std::move(signal);
    }

    std::size_t buffered_bytes(TransportTrafficClass traffic) const override {
        std::scoped_lock lock(mutex_);
        std::size_t total = 0U;
        for (const auto& [identifier, peer] : peers_) {
            (void)identifier;
            std::shared_ptr<rtc::DataChannel> channel;
            switch (quick_join_delivery_class(traffic)) {
            case TransportTrafficClass::Checkpoint:
                channel = peer->checkpoint_channel;
                break;
            case TransportTrafficClass::Control:
                channel = peer->control_channel;
                break;
            case TransportTrafficClass::Authoritative:
                channel = peer->authority_channel;
                break;
            case TransportTrafficClass::Realtime:
                channel = peer->realtime_channel;
                break;
            case TransportTrafficClass::Replica:
                channel = peer->replica_channel;
                break;
            }
            if (channel && channel->isOpen()) {
                total += channel->bufferedAmount();
            }
        }
        return total;
    }

    bool traffic_ready(const PeerAddress& destination,
                       TransportTrafficClass traffic) const override {
        std::scoped_lock lock(mutex_);
        for (const auto& [identifier, peer] : peers_) {
            (void)identifier;
            if (!peer || peer->address != destination ||
                !peer->current.load(std::memory_order_acquire)) {
                continue;
            }
            std::shared_ptr<rtc::DataChannel> channel;
            switch (quick_join_delivery_class(traffic)) {
            case TransportTrafficClass::Checkpoint:
                channel = peer->checkpoint_channel;
                break;
            case TransportTrafficClass::Control:
                channel = peer->control_channel;
                break;
            case TransportTrafficClass::Authoritative:
                channel = peer->authority_channel;
                break;
            case TransportTrafficClass::Realtime:
                channel = peer->realtime_channel;
                break;
            case TransportTrafficClass::Replica:
                channel = peer->replica_channel;
                break;
            }
            return peer->route_ready.load(std::memory_order_acquire) &&
                   channel && channel->isOpen();
        }
        return false;
    }

    void service() override {
        const auto now = std::chrono::steady_clock::now();
        finish_rekey();
        if (signaling_connecting_.load() &&
            steady_milliseconds() - signaling_started_ms_.load() > 15000U) {
            signaling_connecting_.store(false);
            request_signaling_retry();
        }
        std::vector<std::shared_ptr<Peer>> retired;
        {
            std::scoped_lock lock(mutex_);
            for (auto it = peers_.begin(); it != peers_.end();) {
                const auto& peer = it->second;
                const bool pinned = pinned_routes_.contains(peer->remote_id);
                if (!peer->current.load() || (!pinned && !peer->route_ready.load() && now - peer->created > std::chrono::seconds(30))) {
                    request_route_retry(peer);
                    peer->current.store(false);
                    purge_peer_packets_locked(peer);
                    remote_to_identifier_.erase(peer->remote_id + "\n" + peer->connection_id);
                    retired.push_back(peer);
                    it = peers_.erase(it);
                } else ++it;
            }
            retired.insert(retired.end(), retired_peers_.begin(), retired_peers_.end());
            retired_peers_.clear();
        }
        for (const auto& peer : retired) {
            if (pending_retirements_.fetch_add(1U) >= 16U) {
                pending_retirements_.fetch_sub(1U);
                std::scoped_lock lock(mutex_);
                retired_peers_.push_back(peer);
                continue;
            }
            if (!connection_closer_.post([this, peer] {
                    try { if (peer->connection) peer->connection->close(); } catch (...) {}
                    pending_retirements_.fetch_sub(1U);
                })) {
                pending_retirements_.fetch_sub(1U);
                std::scoped_lock lock(mutex_);
                retired_peers_.push_back(peer);
            }
        }
        const std::uint64_t now_ms = steady_milliseconds();
        if (signaling_retry_requested_.load(std::memory_order_acquire) &&
            !signaling_ready_.load(std::memory_order_acquire) &&
            !signaling_connecting_.load(std::memory_order_acquire) &&
            now_ms >= next_signaling_retry_ms_.load(
                          std::memory_order_acquire)) {
            reconnect_signaling();
        }
        if (signaling_ready_.load(std::memory_order_acquire) &&
            route_retry_requested_.load(std::memory_order_acquire) &&
            !host_ && now_ms >= next_route_retry_ms_.load(
                              std::memory_order_acquire)) {
            route_retry_requested_.store(false, std::memory_order_release);
            // Give this negotiation its own time budget. Send retries cannot
            // restart either the first retry timer or an in-flight offer.
            next_route_retry_ms_.store(now_ms + 15000U, std::memory_order_release);
            std::string remote;
            {
                std::scoped_lock lock(mutex_);
                remote = remote_host_id_;
            }
            create_offer(remote);
        }
        if (signaling_ready_.load(std::memory_order_acquire) &&
            now - last_heartbeat_ >= std::chrono::seconds(5)) {
            send_signal(Json{{"type", "HEARTBEAT"}});
            last_heartbeat_ = now;
        }
    }

    void set_quick_join_bootstrap(std::string bootstrap) override {
        std::vector<std::shared_ptr<rtc::DataChannel>> channels;
        {
            std::scoped_lock lock(mutex_);
            host_bootstrap_ = std::move(bootstrap);
            for (const auto& [identifier, peer] : peers_) {
                if (peer->control_channel && peer->control_channel->isOpen()) {
                    channels.push_back(peer->control_channel);
                }
            }
        }
        for (const auto& channel : channels) send_bootstrap(channel);
    }

    bool take_quick_join_bootstrap(std::string& bootstrap,
                                   PeerAddress& peer) override {
        std::scoped_lock lock(mutex_);
        if (bootstrap_.empty() || !bootstrap_peer_) return false;
        bootstrap = std::move(bootstrap_);
        peer = bootstrap_peer_;
        bootstrap_peer_ = {};
        return true;
    }

private:
    friend struct QuickJoinTestAccess;
    CallbackLifetime callbacks_;
    struct PendingRekey {
        std::string code;
        std::string bootstrap;
        std::uint64_t generation = 0U;
        std::shared_ptr<rtc::WebSocket> socket;
        std::atomic<bool> ready{false};
        std::atomic<bool> failed{false};
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    };

    void finish_rekey() {
        std::shared_ptr<PendingRekey> pending;
        std::shared_ptr<rtc::WebSocket> previous;
        std::vector<std::shared_ptr<rtc::DataChannel>> channels;
        bool committed = false;
        {
            std::scoped_lock lock(mutex_);
            pending = pending_rekey_;
            if (!pending) return;
            if (!pending->ready.load() && !pending->failed.load() &&
                std::chrono::steady_clock::now() - pending->started < std::chrono::seconds(15)) return;
            committed = pending->ready.load() && !pending->failed.load();
            if (committed) {
                previous = std::move(websocket_);
                websocket_ = pending->socket;
                code_ = pending->code;
                local_peer_id_ = "dkrr-31-" + code_;
                host_bootstrap_ = pending->bootstrap;
                signaling_generation_.store(pending->generation);
                signaling_ready_.store(true);
                signaling_connecting_.store(false);
                signaling_established_once_.store(true);
                signaling_retry_requested_.store(false);
                for (const auto& [id, peer] : peers_)
                    if (peer->current.load() && peer->control_channel)
                        channels.push_back(peer->control_channel);
            }
            pending_rekey_.reset();
            rekey_state_.store(committed ? QuickJoinRekeyStatus::Committed : QuickJoinRekeyStatus::Failed);
        }
        if (!committed) { if (pending->socket) pending->socket->close(); return; }
        for (const auto& channel : channels) {
            rtc::binary framed{static_cast<std::byte>(3U)};
            for (unsigned char c : pending->code) framed.push_back(static_cast<std::byte>(c));
            try { if (channel->isOpen()) channel->send(std::move(framed)); } catch (...) {}
        }
        if (previous) previous->close();
    }
    struct Peer {
        std::uint64_t identifier = 0U;
        std::string remote_id;
        std::string connection_id;
        PeerAddress address{};
        std::atomic<bool> current{true};
        std::atomic<bool> route_ready{false};
        std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
        std::size_t queued_bytes = 0U;
        std::shared_ptr<rtc::PeerConnection> connection;
        std::shared_ptr<rtc::DataChannel> control_channel;
        std::shared_ptr<rtc::DataChannel> authority_channel;
        std::shared_ptr<rtc::DataChannel> realtime_channel;
        std::shared_ptr<rtc::DataChannel> replica_channel;
        std::shared_ptr<rtc::DataChannel> checkpoint_channel;
    };

    struct InboundPacket {
        PeerAddress source{};
        std::vector<std::uint8_t> bytes;
    };

    void release_packet_bytes_locked(const InboundPacket& packet) {
        for (const auto& [id, peer] : peers_)
            if (peer->address == packet.source)
                peer->queued_bytes -= (std::min)(peer->queued_bytes, packet.bytes.size());
    }

    void purge_peer_packets_locked(const std::shared_ptr<Peer>& peer) {
        for (auto* queue : {&control_inbound_, &authority_inbound_, &checkpoint_inbound_, &realtime_inbound_, &replica_inbound_}) {
            std::erase_if(*queue, [&](const InboundPacket& packet) { return packet.source == peer->address; });
        }
        peer->queued_bytes = 0U;
    }

    std::string signaling_url(std::string_view peer_id) const {
#ifdef DKR_QUICK_JOIN_TESTING
        return quick_test_signaling_endpoint + "/" + std::string(peer_id);
#endif
        return "wss://0.peerjs.com/peerjs?key=peerjs&id=" +
               std::string(peer_id) + "&token=" +
               random_token(24U, kCodeAlphabet) + "&version=1.5.5";
    }

    bool current_signaling_generation(std::uint64_t generation) const {
        return signaling_generation_.load(std::memory_order_acquire) ==
               generation;
    }

    std::shared_ptr<rtc::WebSocket> make_signaling_socket(
        std::uint64_t generation, std::weak_ptr<PendingRekey> pending = {}) {
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = signaling_ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto websocket = std::make_shared<rtc::WebSocket>(
            std::move(configuration));
        // WebSocket open is not PeerJS registration; wait for its OPEN packet.
        websocket->onClosed(callbacks_.wrap([this, generation, pending] {
            if (const auto staged = pending.lock()) staged->failed.store(true);
            if (!current_signaling_generation(generation)) return;
            signaling_ready_.store(false, std::memory_order_release);
            signaling_connecting_.store(false, std::memory_order_release);
            request_signaling_retry();
        }));
        websocket->onError(callbacks_.wrap([this, generation, pending](std::string) {
            if (const auto staged = pending.lock()) staged->failed.store(true);
            if (!current_signaling_generation(generation)) return;
            signaling_ready_.store(false, std::memory_order_release);
            signaling_connecting_.store(false, std::memory_order_release);
            request_signaling_retry();
        }));
        websocket->onMessage(
            callbacks_.wrap([this, generation, pending](rtc::message_variant message) {
                if (std::holds_alternative<std::string>(message)) {
                    if (const auto staged = pending.lock(); staged && !current_signaling_generation(generation)) {
                        const auto& text = std::get<std::string>(message);
                        if (text.size() > 65536U) return;
                        const auto value = Json::parse(text, nullptr, false);
                        if (!valid_signaling_message(value)) return;
                        const auto type = value.value("type", "");
                        if (type == "OPEN") staged->ready.store(true);
                        if (type == "ERROR" || type == "ID-TAKEN") staged->failed.store(true);
                        return;
                    }
                    if (!current_signaling_generation(generation)) return;
                    handle_signal(std::get<std::string>(message));
                }
            }));
        return websocket;
    }

    bool has_established_route() const {
        std::scoped_lock lock(mutex_);
        return std::any_of(peers_.begin(), peers_.end(),
            [](const auto& entry) {
                const auto& peer = entry.second;
                return peer && peer->current.load(std::memory_order_acquire) &&
                       peer->route_ready.load(std::memory_order_acquire) &&
                       peer->control_channel && peer->control_channel->isOpen();
            });
    }

    void request_signaling_retry() {
        if (closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire) ||
            !open_.load(std::memory_order_acquire)) return;
        std::uint64_t unscheduled = 0U;
        next_signaling_retry_ms_.compare_exchange_strong(
            unscheduled, steady_milliseconds() + 750U, std::memory_order_acq_rel);
        signaling_retry_requested_.store(true, std::memory_order_release);
    }

    void reconnect_signaling() {
        if (closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire) ||
            !open_.load(std::memory_order_acquire)) return;
        bool expected = false;
        if (!signaling_connecting_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) return;
        const std::uint64_t generation =
            next_signaling_generation_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
        signaling_generation_.store(generation);
        signaling_started_ms_.store(steady_milliseconds());
        next_signaling_retry_ms_.store(steady_milliseconds() + 2000U);
        auto replacement = make_signaling_socket(generation);
        std::shared_ptr<rtc::WebSocket> previous;
        {
            std::scoped_lock lock(mutex_);
            previous = std::move(websocket_);
            websocket_ = replacement;
        }
        if (previous) previous->close();
        try {
            replacement->open(signaling_url(local_peer_id_));
        } catch (...) {
            signaling_connecting_.store(false, std::memory_order_release);
            next_signaling_retry_ms_.store(
                steady_milliseconds() + 2000U, std::memory_order_release);
            signaling_retry_requested_.store(true, std::memory_order_release);
        }
    }

    void request_route_retry(const std::shared_ptr<Peer>& peer) {
        if (!peer || !peer->current.load(std::memory_order_acquire) ||
            closing_.load(std::memory_order_acquire) ||
            failed_.load(std::memory_order_acquire)) return;
        peer->route_ready.store(false, std::memory_order_release);
        if (!host_) {
            std::uint64_t unscheduled = 0U;
            next_route_retry_ms_.compare_exchange_strong(
                unscheduled, steady_milliseconds() + 750U,
                std::memory_order_acq_rel);
            route_retry_requested_.store(true, std::memory_order_release);
            if (!signaling_ready_.load(std::memory_order_acquire)) {
                request_signaling_retry();
            }
        }
    }

    void set_failure(std::string message) {
        std::scoped_lock lock(mutex_);
        failure_ = std::move(message);
        failed_.store(true, std::memory_order_release);
    }

    void send_signal(const Json& message) {
        std::shared_ptr<rtc::WebSocket> websocket;
        {
            std::scoped_lock lock(mutex_);
            websocket = websocket_;
        }
        if (!websocket || !websocket->isOpen()) return;
        try { websocket->send(message.dump()); } catch (...) {}
    }

    std::shared_ptr<Peer> create_peer(std::string remote,
                                      std::string connection_id,
                                      bool offer = false,
                                      const Json* remote_sdp = nullptr) {
        auto peer = std::make_shared<Peer>();
        peer->remote_id = std::move(remote);
        peer->connection_id = std::move(connection_id);
        std::shared_ptr<Peer> replaced;
        {
            std::scoped_lock lock(mutex_);
            if (peer->remote_id.size() > 128U || peer->connection_id.size() > 128U ||
                retired_peers_.size() + pending_retirements_.load() >= 16U ||
                (peers_.size() >= 8U && std::none_of(peers_.begin(), peers_.end(), [&](const auto& entry) { return entry.second->remote_id == peer->remote_id; }))) return {};
            auto existing = std::find_if(
                peers_.begin(), peers_.end(), [&](const auto& entry) {
                    return entry.second &&
                           entry.second->remote_id == peer->remote_id;
                });
            const auto pinned = pinned_routes_.find(peer->remote_id);
            if (existing == peers_.end() && pinned != pinned_routes_.end()) {
                existing = peers_.find(pinned->second.first);
            }
            if (existing != peers_.end()) {
                replaced = existing->second;
                peer->identifier = replaced->identifier;
                peer->address = replaced->address;
            } else if (pinned != pinned_routes_.end()) {
                peer->identifier = pinned->second.first;
                peer->address = pinned->second.second;
            } else {
                peer->identifier = next_peer_identifier_.fetch_add(
                    1U, std::memory_order_relaxed);
                peer->address = quick_address(peer->identifier);
            }
        }
        try {
        route_construction_check(0);
        rtc::Configuration configuration;
#ifndef DKR_QUICK_JOIN_TESTING
        configuration.iceServers.emplace_back(
            "stun:stun.l.google.com:19302");
#endif
        peer->connection = std::make_shared<rtc::PeerConnection>(configuration);
        const std::weak_ptr<Peer> weak_peer(peer);
        peer->connection->onLocalDescription(
            callbacks_.wrap([this, weak_peer](rtc::Description description) {
                const auto locked = weak_peer.lock();
                if (!locked || !locked->current.load()) return;
                const std::string type = description.typeString();
                Json payload{{"connectionId", locked->connection_id},
                             {"type", "data"},
                             {"sdp", {{"sdp", std::string(description)},
                                      {"type", type}}}};
                if (type == "offer") {
                    payload["label"] = locked->connection_id;
                    payload["reliable"] = true;
                    payload["serialization"] = "none";
                }
                send_signal(Json{{"type", type == "offer" ? "OFFER" : "ANSWER"},
                                 {"dst", locked->remote_id},
                                 {"payload", std::move(payload)}});
            }));
        peer->connection->onLocalCandidate(
            callbacks_.wrap([this, weak_peer](rtc::Candidate candidate) {
                const auto locked = weak_peer.lock();
                if (!locked || !locked->current.load()) return;
                send_signal(Json{
                    {"type", "CANDIDATE"}, {"dst", locked->remote_id},
                    {"payload", {
                        {"connectionId", locked->connection_id},
                        {"type", "data"},
                        {"candidate", {
                            {"candidate", candidate.candidate()},
                            {"sdpMid", candidate.mid()}}}}}});
            }));
        peer->connection->onDataChannel(
            callbacks_.wrap([this, weak_peer](std::shared_ptr<rtc::DataChannel> channel) {
                if (const auto locked = weak_peer.lock()) {
                    bind_channel(locked, std::move(channel));
                }
            }));
        peer->connection->onStateChange(
            callbacks_.wrap([this, weak_peer](rtc::PeerConnection::State state) {
                const auto locked = weak_peer.lock();
                if (!locked || !locked->current.load(
                        std::memory_order_acquire)) return;
                if (state == rtc::PeerConnection::State::Connected) {
                    locked->route_ready.store(true, std::memory_order_release);
                    if (!host_) {
                        next_route_retry_ms_.store(0U, std::memory_order_release);
                        route_retry_requested_.store(false, std::memory_order_release);
                    }
                } else if (state == rtc::PeerConnection::State::Disconnected ||
                           state == rtc::PeerConnection::State::Failed ||
                           state == rtc::PeerConnection::State::Closed) {
                    request_route_retry(locked);
                }
            }));
        if (offer) create_offer_channels(peer);
        route_construction_check(6);
        if (remote_sdp) {
            peer->connection->setRemoteDescription(rtc::Description(
                remote_sdp->value("sdp", ""), remote_sdp->value("type", "")));
        }
        {
            std::scoped_lock lock(mutex_);
            // Do not retire the usable route until replacement construction
            // and initial negotiation have succeeded.
            const auto current = peers_.find(peer->identifier);
            if (current != peers_.end() && current->second != replaced)
                throw std::runtime_error("A newer route already replaced this construction");
            if (replaced) {
                replaced->current.store(false, std::memory_order_release);
                purge_peer_packets_locked(replaced);
                remote_to_identifier_.erase(
                    replaced->remote_id + "\n" + replaced->connection_id);
            }
            remote_to_identifier_[peer->remote_id + "\n" +
                                  peer->connection_id] = peer->identifier;
            peers_[peer->identifier] = peer;
        }
        if (replaced) {
            std::scoped_lock lock(mutex_);
            retired_peers_.push_back(std::move(replaced));
        }
        return peer;
        } catch (const std::exception&) {
            request_route_retry(peer);
            peer->current.store(false, std::memory_order_release);
            std::scoped_lock lock(mutex_);
            retired_peers_.push_back(std::move(peer));
            return {};
        }
    }

    void create_offer(const std::string& remote) {
        (void)create_peer(remote, "dkrr-" + random_token(20U, kCodeAlphabet), true);
    }

    void route_construction_check(int stage) {
#ifdef DKR_QUICK_JOIN_TESTING
        if (construction_failure_stage_ == stage)
            throw std::runtime_error("Injected route construction failure");
#else
        (void)stage;
#endif
    }

    void create_offer_channels(const std::shared_ptr<Peer>& peer) {
        route_construction_check(1);
        bind_channel(peer,
            peer->connection->createDataChannel("dkr-r-control"));

        // Frame commits and input-repair batches are reliable, but unordered.
        // Each batch carries an authenticated frame chain and overlapping
        // history, so newer authority can be processed without waiting for an
        // older retransmission or an unrelated ordered control message.
        rtc::DataChannelInit authority{};
        authority.reliability.unordered = true;
        route_construction_check(2);
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-authority", authority));
        // Required snapshots are reliable, but cannot block the ordered
        // release/control stream or the immutable frame-commit stream.
        route_construction_check(3);
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-checkpoint", authority));

        rtc::DataChannelInit realtime{};
        realtime.reliability.unordered = true;
        realtime.reliability.maxRetransmits = 0U;
        route_construction_check(4);
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-realtime", realtime));

        rtc::DataChannelInit replica{};
        replica.reliability.unordered = true;
        replica.reliability.maxRetransmits = 0U;
        route_construction_check(5);
        bind_channel(peer, peer->connection->createDataChannel(
            "dkr-r-replica", replica));
    }

    std::shared_ptr<Peer> find_peer(const std::string& remote,
                                    const std::string& connection_id) {
        std::scoped_lock lock(mutex_);
        const auto found = remote_to_identifier_.find(
            remote + "\n" + connection_id);
        if (found == remote_to_identifier_.end()) return {};
        const auto peer = peers_.find(found->second);
        return peer == peers_.end() ? std::shared_ptr<Peer>{} : peer->second;
    }

    void bind_channel(const std::shared_ptr<Peer>& peer,
                      std::shared_ptr<rtc::DataChannel> channel) {
        if (!channel) return;
        const std::string label = channel->label();
        if (label != "dkr-r-control" && label != "dkr-r" && label != "dkr-r-authority" &&
            label != "dkr-r-realtime" && label != "dkr-r-replica" && label != "dkr-r-checkpoint") {
            channel->close();
            return;
        }
        const auto bound = channel;
        {
        std::scoped_lock lock(mutex_);
        if (!peer->current.load(std::memory_order_acquire)) return;
        if (label == "dkr-r-checkpoint") {
            peer->checkpoint_channel = std::move(channel);
        } else if (label == "dkr-r-authority") {
            peer->authority_channel = std::move(channel);
        } else if (label == "dkr-r-realtime") {
            peer->realtime_channel = std::move(channel);
        } else if (label == "dkr-r-replica") {
            peer->replica_channel = std::move(channel);
        } else {
            // Accept the legacy label as control during a rolling upgrade. The
            // compatibility manifest prevents mixed gameplay builds, but this
            // also keeps admission diagnostics intelligible.
            peer->control_channel = std::move(channel);
        }
        }
        const std::weak_ptr<Peer> weak_peer(peer);
        bound->onOpen(callbacks_.wrap([this, weak_peer, label] {
            const auto locked = weak_peer.lock();
            if (!locked || !locked->current.load(
                    std::memory_order_acquire)) return;
            if (label != "dkr-r-authority" &&
                label != "dkr-r-checkpoint" &&
                label != "dkr-r-realtime" &&
                label != "dkr-r-replica") {
                locked->route_ready.store(true, std::memory_order_release);
            }
            if (host_ && label != "dkr-r-authority" &&
                label != "dkr-r-checkpoint" &&
                label != "dkr-r-realtime" &&
                label != "dkr-r-replica") {
                send_bootstrap(locked->control_channel);
            }
        }));
        bound->onClosed(callbacks_.wrap([this, weak_peer] {
            if (const auto locked = weak_peer.lock()) {
                request_route_retry(locked);
            }
        }));
        bound->onError(callbacks_.wrap([this, weak_peer](std::string) {
            if (const auto locked = weak_peer.lock()) {
                request_route_retry(locked);
            }
        }));
        bound->onMessage(
            callbacks_.wrap([this, weak_peer, label](rtc::message_variant message) {
                const auto locked = weak_peer.lock();
                if (!locked || !locked->current.load(std::memory_order_acquire) ||
                    closing_.load(std::memory_order_acquire) || !std::holds_alternative<rtc::binary>(message)) {
                    return;
                }
                const rtc::binary& framed = std::get<rtc::binary>(message);
                // Allow the authenticated envelope above the plaintext limit,
                // but reject oversized payloads before a second allocation.
                if (framed.empty() || framed.size() > protocol::kMaximumQuickJoinDatagramBytes + 512U) return;
                const std::uint8_t kind = std::to_integer<std::uint8_t>(framed[0]);
                std::vector<std::uint8_t> bytes;
                bytes.reserve(framed.size() - 1U);
                for (std::size_t index = 1U; index < framed.size(); ++index) {
                    bytes.push_back(std::to_integer<std::uint8_t>(framed[index]));
                }
                std::scoped_lock lock(mutex_);
                if (!locked->current.load(std::memory_order_acquire) || closing_.load()) return;
                if (kind == 2U && !host_) {
                    bootstrap_.assign(bytes.begin(), bytes.end());
                    bootstrap_peer_ = locked->address;
                } else if (kind == 3U && !host_) {
                    const std::string replacement(bytes.begin(), bytes.end());
                    if (valid_quick_join_code(replacement)) {
                        const std::string new_remote = "dkrr-31-" + normalize_code(replacement);
                        const auto pinned = pinned_routes_.find(remote_host_id_);
                        if (pinned != pinned_routes_.end()) {
                            const auto route = pinned->second;
                            pinned_routes_.erase(pinned);
                            pinned_routes_[new_remote] = route;
                        }
                        remote_host_id_ = new_remote;
                    }
                } else if (kind == 1U && !bytes.empty()) {
                    std::deque<InboundPacket>* queue = &control_inbound_;
                    std::size_t maximum = 4096U;
                    bool latest_wins = false;
                    if (label == "dkr-r-checkpoint") {
                        queue = &checkpoint_inbound_;
                        maximum = 512U;
                    } else if (label == "dkr-r-authority") {
                        // Frame commits are the simulation ledger. Never let a
                        // replica/input burst evict them from the receive path.
                        queue = &authority_inbound_;
                        maximum = 16384U;
                    } else if (label == "dkr-r-realtime") {
                        queue = &realtime_inbound_;
                        maximum = 2048U;
                        latest_wins = true;
                    } else if (label == "dkr-r-replica") {
                        queue = &replica_inbound_;
                        maximum = 512U;
                        latest_wins = true;
                    }
                    while (queue->size() >= maximum || locked->queued_bytes + bytes.size() > (16U << 20U)) {
                        if (latest_wins) {
                            const auto discard = std::find_if(queue->begin(), queue->end(),
                                [&](const InboundPacket& packet) { return packet.source == locked->address; });
                            if (discard == queue->end()) return;
                            release_packet_bytes_locked(*discard);
                            queue->erase(discard);
                        } else {
                            // Only retire the overflowing route. The service
                            // pass closes it outside this receive-state lock.
                            request_route_retry(locked);
                            locked->current.store(false, std::memory_order_release);
                            return;
                        }
                    }
                    locked->queued_bytes += bytes.size();
                    queue->push_back({locked->address, std::move(bytes)});
                }
                if (receive_signal_) receive_signal_->notify();
            }));
    }

    void send_bootstrap(const std::shared_ptr<rtc::DataChannel>& channel) {
        std::string bootstrap;
        {
            std::scoped_lock lock(mutex_);
            bootstrap = host_bootstrap_;
        }
        if (bootstrap.empty() || !channel || !channel->isOpen()) return;
        rtc::binary framed;
        framed.reserve(bootstrap.size() + 1U);
        framed.push_back(static_cast<std::byte>(2U));
        for (const unsigned char character : bootstrap) {
            framed.push_back(static_cast<std::byte>(character));
        }
        try { channel->send(std::move(framed)); } catch (...) {}
    }

    void handle_signal(const std::string& text) {
        Json message;
        if (text.size() > 65536U) return;
        try { message = Json::parse(text); } catch (...) { return; }
        if (!valid_signaling_message(message)) return;
        const std::string type = message.value("type", "");
        if (type == "OPEN") {
            signaling_established_once_.store(true);
            signaling_connecting_.store(false);
            signaling_retry_requested_.store(false);
            signaling_ready_.store(true);
            next_signaling_retry_ms_.store(0U);
            if (!host_ && !has_established_route()) {
                // Service owns offer creation, keeping SDK work out of the
                // signaling callback and allowing registration to settle.
                next_route_retry_ms_.store(steady_milliseconds());
                route_retry_requested_.store(true);
            }
            return;
        }
        if (type == "ID-TAKEN" || type == "ERROR") {
            if (type == "ID-TAKEN" &&
                !signaling_established_once_.load(
                    std::memory_order_acquire)) {
                set_failure(
                    "That Quick Join code is already in use. Create a new lobby code.");
            } else {
                signaling_ready_.store(false, std::memory_order_release);
                signaling_connecting_.store(false, std::memory_order_release);
                request_signaling_retry();
            }
            return;
        }
        if (type != "OFFER" && type != "ANSWER" &&
            type != "CANDIDATE" && type != "LEAVE") return;
        const std::string remote = message.value("src", "");
        if (remote.empty()) return;
        if (type == "LEAVE") return;
        if (!message.contains("payload") ||
            !message["payload"].is_object()) return;
        const Json& payload = message["payload"];
        const std::string connection_id = payload.value("connectionId", "");
        if (connection_id.empty()) return;
        std::shared_ptr<Peer> peer = find_peer(remote, connection_id);
        if (!peer && type == "OFFER" && host_) {
            if (!payload.contains("sdp") || !payload["sdp"].is_object()) return;
            (void)create_peer(remote, connection_id, false, &payload["sdp"]);
            return;
        }
        if (!peer) return;
        try {
            if ((type == "OFFER" || type == "ANSWER") &&
                payload.contains("sdp")) {
                const Json& sdp = payload["sdp"];
                peer->connection->setRemoteDescription(rtc::Description(
                    sdp.value("sdp", ""), sdp.value("type", "")));
            } else if (type == "CANDIDATE" &&
                       payload.contains("candidate")) {
                const Json& candidate = payload["candidate"];
                peer->connection->addRemoteCandidate(rtc::Candidate(
                    candidate.value("candidate", ""),
                    candidate.value("sdpMid", "")));
            }
        } catch (const std::exception& exception) {
            // A malformed new offer is not a reason to fail a healthy lobby.
            // Retiring the offending route lets an admitted client rebuild via
            // the existing authenticated route-repair protocol.
            (void)exception;
            request_route_retry(peer);
            peer->current.store(false, std::memory_order_release);
        }
    }

    bool host_ = false;
#ifdef DKR_QUICK_JOIN_TESTING
    int construction_failure_stage_ = -1;
#endif
    std::string code_;
    std::string local_peer_id_;
    std::string remote_host_id_;
    mutable std::mutex mutex_;
    std::shared_ptr<TransportReceiveSignal> receive_signal_;
    std::shared_ptr<rtc::WebSocket> websocket_;
    std::shared_ptr<PendingRekey> pending_rekey_;
    std::atomic<QuickJoinRekeyStatus> rekey_state_{QuickJoinRekeyStatus::Idle};
    std::map<std::uint64_t, std::shared_ptr<Peer>> peers_;
    std::map<std::string, std::uint64_t> remote_to_identifier_;
    std::map<std::string, std::pair<std::uint64_t, PeerAddress>> pinned_routes_;
    std::vector<std::shared_ptr<Peer>> retired_peers_;
    SocialExecutor connection_closer_;
    std::atomic<unsigned> pending_retirements_{0U};
    // Independent receive queues mirror the SCTP traffic classes. Reliable
    // control and frame-ledger data cannot be evicted by disposable live
    // replicas when a latency spike releases a burst of queued messages.
    std::deque<InboundPacket> control_inbound_;
    std::deque<InboundPacket> authority_inbound_;
    std::deque<InboundPacket> checkpoint_inbound_;
    std::deque<InboundPacket> realtime_inbound_;
    std::deque<InboundPacket> replica_inbound_;
    std::size_t receive_service_cursor_ = 0U;
    std::string host_bootstrap_;
    std::string bootstrap_;
    PeerAddress bootstrap_peer_{};
    std::string failure_;
    std::atomic<std::uint64_t> next_peer_identifier_{1U};
    std::atomic<std::uint64_t> signaling_generation_{0U};
    std::atomic<std::uint64_t> next_signaling_generation_{0U};
    std::atomic<std::uint64_t> signaling_started_ms_{0U};
    std::atomic<bool> open_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> signaling_ready_{false};
    std::atomic<bool> signaling_established_once_{false};
    std::atomic<bool> signaling_connecting_{false};
    std::atomic<bool> signaling_retry_requested_{false};
    std::atomic<bool> route_retry_requested_{false};
    std::chrono::steady_clock::time_point last_heartbeat_{};
    std::atomic<std::uint64_t> next_signaling_retry_ms_{0U};
    std::atomic<std::uint64_t> next_route_retry_ms_{0U};
};

#endif

} // namespace

bool valid_quick_join_code(std::string_view code) {
    const std::string normalized = normalize_code(code);
    return normalized.size() == 5U &&
           std::all_of(normalized.begin(), normalized.end(), [](char value) {
               return kCodeAlphabet.find(value) != std::string_view::npos;
           });
}

std::unique_ptr<SessionTransport> make_quick_join_session_transport(
    bool host, std::string_view requested_code, std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::string code = normalize_code(requested_code);
    if (host && code.empty()) code = random_token(5U, kCodeAlphabet);
    if (!valid_quick_join_code(code)) {
        error = "Quick Join codes contain five letters or numbers.";
        return {};
    }
    error.clear();
    return std::make_unique<QuickJoinTransport>(host, std::move(code));
#else
    (void)host;
    (void)requested_code;
    error = "This build does not include the optional Quick Join transport.";
    return {};
#endif
}

} // namespace dkr::runtime::netplay
