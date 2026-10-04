#pragma once
#include "experimental_session.hpp"
#include "secure_channel.hpp"
#include "sequence_window.hpp"
#include "session_transport.hpp"
#include <chrono>

namespace dkr::runtime::netplay::experimental {

struct PeerBinding {
    PeerAddress address{};
    std::uint64_t sender_id = 0;
    secure::Key admitted_key{};
};
struct NetworkConfiguration {
    // Provisioned only after experimental mode/schema/baseline admission.
    // A NEW random session incarnation and fresh admitted keys are required
    // for a new launch. The nonce is shared during authenticated admission,
    // not inferred from a frame/scene counter or reused on a reconnect.
    std::uint64_t match_id = 0, local_sender_id = 0;
    secure::Key incarnation{};
    std::array<PeerBinding,4> peers{};
};
struct NetworkStatistics {
    std::uint64_t received = 0, rejected = 0, duplicate = 0, sent = 0,
                  backpressure = 0, send_errors = 0, suppressed_resends = 0,
                  superseded_live = 0;
    std::size_t pending_packets = 0;
};

// Non-blocking experimental transport adapter. It is NOT wired into live
// DirectSession or an unqualified DKR world. Run on the Session's sole owner,
// never from an SDK callback. service() does not sample or simulate a frame.
// Each call processes at most 32 datagrams and 32 bounded sends. WouldBlock
// retains exact scene/repair packets; an unsent live packet may be replaced
// with fresher immutable input under a NEW nonce. One blocked route cannot
// consume another route's allowance. No stable ledger/checkpoint/replica traffic.
class Network final {
public:
    using Clock = std::chrono::steady_clock;
    Network(Session& session, SessionTransport& transport)
        : session_(session), transport_(transport) {}
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    bool start(NetworkConfiguration configuration, std::string& error);
    // Injected monotonic time makes resend/freshness policies testable without
    // sleeps. Receive/service remains available even when sends are paced.
    bool service(Clock::time_point now = Clock::now());
    const std::string& error() const { return error_; }
    const NetworkStatistics& statistics() const { return statistics_; }
private:
    struct Pending {
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> plain;
        std::vector<std::uint8_t> last_sent_plain;
        TransportTrafficClass traffic = TransportTrafficClass::Control;
        Clock::time_point sent_at{}, attempted_at{};
        bool sent = false, attempted = false;
    };
    static constexpr unsigned kLanes = 2 + 4 * 2;
    bool fail(std::string message);
    bool queue(unsigned peer, unsigned lane, unsigned kind,
               std::vector<std::uint8_t> payload, TransportTrafficClass traffic,
               Clock::time_point now);
    void receive(const PeerAddress& source, std::span<const std::uint8_t> bytes);
    Session& session_;
    SessionTransport& transport_;
    NetworkConfiguration configuration_{};
    std::array<secure::Key,4> keys_{};
    std::array<std::uint64_t,4> sequences_{};
    std::array<ReceiveSequenceWindow,4> replay_{};
    std::array<std::array<Pending,kLanes>,4> pending_{};
    NetworkStatistics statistics_{};
    std::uint64_t epoch_ = 0;
    unsigned next_route_ = 0, next_lane_ = 0;
    Clock::time_point last_service_{};
    bool clock_started_ = false;
    bool started_ = false;
    std::string error_;
};
}
