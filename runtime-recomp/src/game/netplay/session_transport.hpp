#pragma once

#include "datagram_socket.hpp"

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {

// The authenticated session protocol remains transport-agnostic, but a
// WebRTC route must not place disposable race-state fragments ahead of the
// controller inputs which advance simulation. UDP ignores this classification;
// Quick Join maps it to independent SCTP streams with appropriate reliability.
enum class TransportTrafficClass : std::uint8_t {
    Control,
    Authoritative,
    Realtime,
    Replica,
    Checkpoint,
};

enum class QuickJoinRekeyStatus { Idle, Pending, Committed, Failed };

// Callbacks may outlive a retired transport. They own only this notification
// object, never a DirectSession pointer or its mutex. A generation prevents
// notifications during packet processing from being mistaken for old work.
struct TransportReceiveSignal {
    std::atomic<std::uint64_t> generation{0};
    std::condition_variable changed;
    void notify() {
        generation.fetch_add(1, std::memory_order_release);
        changed.notify_all();
    }
};

// Keep each traffic class on its own Quick Join data channel. In particular,
// immutable frame commits use a reliable/unordered channel: losing one SCTP
// message must not hold newer commits behind unrelated ordered lobby/control
// traffic. Session start explicitly proves every required channel before the
// launch descriptor is published.
constexpr TransportTrafficClass quick_join_delivery_class(
    TransportTrafficClass traffic) {
    return traffic;
}

// Packet-preserving transport boundary for DirectSession. LAN, virtual-LAN
// and direct Internet continue to use UDP; Quick Join can provide the same
// non-blocking contract through a WebRTC data channel without changing the
// authenticated DKR-R session protocol above it.
class SessionTransport {
public:
    virtual ~SessionTransport() = default;
    virtual bool open(std::uint16_t port, std::string& error) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual std::uint16_t local_port() const = 0;
    virtual DatagramSendStatus send_status(
        const PeerAddress& destination,
        std::span<const std::uint8_t> bytes,
        TransportTrafficClass traffic,
        std::string& error) = 0;
    virtual bool receive(PeerAddress& source,
                         std::vector<std::uint8_t>& bytes,
                         std::string& error) = 0;
    // Local scheduling hint ONLY. Immutable input remains in the sender's
    // repair ledger; transport can retire unsent live batches for this owner.
    // Never applied to control, save, checkpoint or repair traffic.
    virtual DatagramSendStatus send_live_status(const PeerAddress& destination,
        std::span<const std::uint8_t> bytes,std::uint8_t owner,std::string& error) {
        return send_status(destination,bytes,TransportTrafficClass::Realtime,error);
    }
    virtual void service() {}
    virtual void set_receive_signal(std::shared_ptr<TransportReceiveSignal>) {}
    // Only authenticated session admission may pin a logical route across ICE
    // replacement. An unapproved connection must retain its bounded lifetime.
    virtual void retain_peer_route(const PeerAddress&) {}
    virtual void release_peer_route(const PeerAddress&) {}
    // Terminal close of a currently admitted route, not an ICE Disconnected
    // notification or signaling-service outage. The session decides whether
    // its selected backend must end the match. UDP has no such close event.
    virtual bool take_peer_departure(PeerAddress&) { return false; }
    virtual std::size_t buffered_bytes(TransportTrafficClass) const {
        return 0U;
    }
    // UDP has one datagram route for all traffic classes. Quick Join overrides
    // this with per-peer data-channel readiness so a race cannot start with a
    // missing authority/realtime/replica lane.
    virtual bool traffic_ready(const PeerAddress&,
                               TransportTrafficClass) const {
        return is_open();
    }
    // The native UDP route deliberately keeps authenticated datagrams below
    // the conservative VPN/IPv6 fragmentation threshold. WebRTC data channels
    // already provide message framing and can carry one complete DKR-R state
    // checkpoint, avoiding repeated application-level encryption, queuing and
    // reassembly for the same authored frame.
    virtual std::size_t maximum_plaintext_datagram_bytes() const {
        return 1000U;
    }
    // Disposable receive lanes may contain old-scene data when a race ends.
    // Clearing only the requested lane prevents it from consuming the next
    // race's processing budget without touching reliable control/ledger data.
    virtual void discard_received(TransportTrafficClass) {}
    virtual bool quick_join() const { return false; }
    virtual std::string quick_join_code() const { return {}; }
    virtual void set_quick_join_bootstrap(std::string) {}
    virtual bool rekey_quick_join(std::string, std::string& error) {
        error = "This transport does not support Quick Join code rotation.";
        return false;
    }
    virtual QuickJoinRekeyStatus rekey_status() const { return QuickJoinRekeyStatus::Idle; }
    virtual bool take_quick_join_bootstrap(std::string&, PeerAddress&) {
        return false;
    }
};

std::unique_ptr<SessionTransport> make_udp_session_transport();
std::unique_ptr<SessionTransport> make_quick_join_session_transport(
    bool host, std::string_view code, std::string& error);
bool valid_quick_join_code(std::string_view code);

} // namespace dkr::runtime::netplay
