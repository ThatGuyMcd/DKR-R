#pragma once
#include "experimental_network.hpp"
#include <deque>
#include <functional>

namespace dkr::runtime::netplay::experimental {

// Canonical agreement for the FIRST experimental two-owner integration.
// Digests come from locally verified immutable build/ROM/rules/ABI inputs;
// accepting a peer's claimed ROM digest is not proof of remote ROM ownership.
// No retail baseline is stored in a distributable build or a user's SP save.
struct LaunchContract {
    std::uint32_t revision = 0, state_bytes = 0;
    std::uint64_t schema = 0, epoch = 0;
    std::uint8_t players = 2, prediction_window = 6, input_delay = 1;
    secure::Key build{}, rom{}, rules{}, abi{};
    bool operator==(const LaunchContract&) const = default;
};
enum class LaunchPhase { Idle, AwaitingPeer, Authenticating, ReceivingBaseline,
                         PreparingWorld, Ready, Releasing, Running, Failed };
struct LaunchView {
    LaunchPhase phase = LaunchPhase::Idle;
    std::uint32_t baseline_received = 0, baseline_bytes = 0;
    std::uint64_t rejected = 0;
};

// Sole-owner, bounded Quick Join admission and immutable baseline handoff.
// The supplied transport is exclusively borrowed, never closed here. SDK
// callbacks may NOT invoke this class or its owned-world preparation callback.
// It also demultiplexes late launch retransmissions from the gameplay lane,
// so losing a release ACK cannot strand one peer or discard its first inputs.
// This class is a dependency of live integration, NOT runtime admission itself.
class Launch final : public SessionTransport {
public:
    using Clock = Network::Clock;
    using Prepare = std::function<PreparationStep(std::span<const std::uint8_t>, std::string&)>;
    static constexpr std::size_t kMaximumBaseline = 32U * 1024U * 1024U;
    // Below the production Quick Join 14,400-byte plaintext limit, including
    // both application framing and authenticated-envelope overhead.
    static constexpr std::size_t kChunkBytes = 12U * 1024U;
    explicit Launch(SessionTransport& transport) : transport_(transport) {}
    ~Launch();
    Launch(const Launch&) = delete;
    Launch& operator=(const Launch&) = delete;
    bool host(LaunchContract contract, std::vector<std::uint8_t> baseline, std::string& error);
    bool join(LaunchContract contract, Prepare prepare, std::string& error);
    bool service_launch(Clock::time_point now = Clock::now());
    // Host-only: no ticking before matching client world is prepared. This
    // starts a new launch; it is never a mid-game stable -> experimental switch.
    bool release(std::string& error);
    const LaunchView& view() const { return view_; }
    const std::string& error() const { return error_; }
    const LaunchContract& contract() const { return contract_; }
    std::optional<NetworkConfiguration> network_configuration() const;

    bool open(std::uint16_t, std::string& error) override;
    void close() override;
    bool is_open() const override;
    std::uint16_t local_port() const override { return transport_.local_port(); }
    bool quick_join() const override { return true; }
    std::string quick_join_code() const override { return transport_.quick_join_code(); }
    std::size_t maximum_plaintext_datagram_bytes() const override { return transport_.maximum_plaintext_datagram_bytes(); }
    bool traffic_ready(const PeerAddress&, TransportTrafficClass) const override;
    std::size_t buffered_bytes(TransportTrafficClass traffic) const override { return transport_.buffered_bytes(traffic); }
    DatagramSendStatus send_status(const PeerAddress&, std::span<const std::uint8_t>,
                                   TransportTrafficClass, std::string&) override;
    bool receive(PeerAddress&, std::vector<std::uint8_t>&, std::string&) override;
    void service() override; // Called by Network; does not call a simulation tick.
private:
    struct Pending {
        std::vector<std::uint8_t> bytes;
        TransportTrafficClass traffic = TransportTrafficClass::Control;
        Clock::time_point attempted{};
        bool attempted_once = false;
        std::uint32_t offset = 0, end = 0;
    };
    bool fail(std::string message);
    bool receive_packet(const PeerAddress&, std::span<const std::uint8_t>, Clock::time_point);
    bool set_control(unsigned kind, std::span<const std::uint8_t> payload);
    bool send_pending(Pending&, Clock::time_point);
    bool lanes_ready() const;
    void drop_secrets();
    SessionTransport& transport_;
    LaunchContract contract_{};
    LaunchView view_{};
    bool host_ = false, admitted_ = false, route_retained_ = false, clock_started_ = false, release_received_ = false;
    Clock::time_point last_service_{}, progress_{}, release_ack_until_{};
    secure::KeyPair local_{};
    secure::Key capability_{}, remote_public_{}, admitted_key_{}, incarnation_{}, baseline_digest_{};
    std::uint64_t match_ = 0, sender_ = 0, remote_sender_ = 0, sequence_ = 0;
    ReceiveSequenceWindow replay_{};
    PeerAddress remote_{};
    Pending control_{};
    std::vector<std::uint8_t> join_packet_, baseline_;
    std::vector<std::uint8_t> received_chunks_;
    std::array<Pending,4> chunks_{};
    std::uint32_t acknowledged_ = 0;
    Prepare prepare_;
    std::deque<std::vector<std::uint8_t>> gameplay_;
    std::string error_;
};
}
