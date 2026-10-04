#pragma once
#include "experimental_network.hpp"
#include <functional>
#include <deque>

namespace dkr::runtime::netplay::experimental {
// Borrow the already authenticated normal lobby. No signaling, second lobby,
// new port, or replacement countdown. Only the owned initial checkpoint is
// transferred here. DirectSession's existing Loaded/Start gate owns release.
class LobbyAdmission final : public SessionTransport {
public:
    using Clock = Network::Clock;
    using Prepare = std::function<bool(std::span<const std::uint8_t>,std::string&)>;
    static constexpr std::size_t kMaximumBaseline = 32U*1024U*1024U;
    LobbyAdmission(SessionTransport& transport,bool host,std::uint8_t players,
                   std::uint8_t local,secure::Key contract);
    bool begin(std::vector<std::uint8_t> baseline,Prepare prepare,std::string& error);
    bool service_admission(Clock::time_point now=Clock::now());
    bool prepared() const;
    bool all_prepared() const;
    void complete_start() { release_seen_=true; }
    std::uint32_t received_bytes() const;
    std::uint32_t total_bytes() const {return std::uint32_t(baseline_.size());}
    std::uint32_t decoded_bytes() const {return decoded_bytes_;}
    std::string status() const;
    std::string diagnostics() const;
    const secure::Key& incarnation() const {return incarnation_;}
    std::uint64_t baseline_identity() const;
    const std::string& error() const {return error_;}
    bool open(std::uint16_t,std::string&) override {return false;}
    void close() override {} // Borrowed lobby lifetime.
    bool is_open() const override {return error_.empty()&&transport_.is_open();}
    std::uint16_t local_port() const override {return transport_.local_port();}
    std::size_t maximum_plaintext_datagram_bytes() const override {return transport_.maximum_plaintext_datagram_bytes();}
    bool traffic_ready(const PeerAddress& p,TransportTrafficClass t) const override {return transport_.traffic_ready(p,t);}
    DatagramSendStatus send_status(const PeerAddress& p,std::span<const std::uint8_t> b,TransportTrafficClass t,std::string& e) override {const auto r=transport_.send_status(p,b,t,e);if(host_&&t==TransportTrafficClass::Realtime&&r==DatagramSendStatus::Sent)release_seen_=true;return r;}
    bool receive(PeerAddress&,std::vector<std::uint8_t>&,std::string&) override;
    void service() override {service_admission();}
    static PeerAddress address(std::uint8_t owner);
private:
    bool fail(std::string);
    DatagramSendStatus send(std::uint8_t,unsigned,std::span<const std::uint8_t>,TransportTrafficClass);
    bool packet(std::uint8_t,std::span<const std::uint8_t>,Clock::time_point);
    SessionTransport& transport_;
    bool host_,begun_=false,local_ready_=false,release_seen_=false;
    std::uint8_t players_,local_;
    secure::Key contract_{},incarnation_{},digest_{};
    // Keep the wire image for idempotent late chunks. The prepared game and
    // its identity always use the original, losslessly decoded checkpoint.
    std::vector<std::uint8_t> baseline_,chunks_;
    std::array<std::uint32_t,4> acknowledged_{},sent_end_{};
    std::array<std::uint32_t,4> coordinator_bytes_{};
    std::array<bool,4> ready_{};
    std::array<bool,4> hello_{};
    std::array<bool,4> offer_accepted_{};
    std::array<Clock::time_point,4> sent_{};
    std::array<Clock::time_point,4> peer_progress_{},repair_at_{};
    Clock::time_point progress_{},last_{},started_{},coordinator_at_{},coordinator_seen_{};
    std::uint8_t next_peer_=1,coordinator_ready_=0;
    std::size_t chunk_bytes_=0;
    std::uint32_t received_=0;
    std::uint32_t decoded_bytes_=0;
    bool compressed_=false;
    Prepare prepare_;
    struct Received {PeerAddress source;std::vector<std::uint8_t> bytes;};
    std::deque<Received> gameplay_;
    std::string error_;
};
}
