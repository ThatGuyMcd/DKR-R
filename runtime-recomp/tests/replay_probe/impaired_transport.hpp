#pragma once
#include "netplay/experimental_network.hpp"
#include <deque>
#include <stdexcept>
namespace dkr::runtime::netplay::experimental {
// Qualification only: wrap Launch AFTER admission; never affect ROM/baseline
// transfer or weaken the actual encrypted Quick Join route underneath.
class ImpairedTransport final : public SessionTransport {
public:
    explicit ImpairedTransport(SessionTransport& base) : base_(base) {}
    bool open(std::uint16_t,std::string& error) override {error="Already borrowed qualification route.";return false;}
    void close() override {pending_.clear();}
    bool is_open() const override {return base_.is_open();}
    std::uint16_t local_port() const override {return base_.local_port();}
    bool quick_join() const override {return true;}
    bool traffic_ready(const PeerAddress& peer,TransportTrafficClass traffic) const override {return base_.traffic_ready(peer,traffic);}
    DatagramSendStatus send_status(const PeerAddress& peer,std::span<const std::uint8_t> bytes,
        TransportTrafficClass traffic,std::string& error) override {
        if(bytes.size()>118){error="Unexpected qualification envelope.";return DatagramSendStatus::Error;}
        if(pending_.size()>=128)return DatagramSendStatus::WouldBlock;
        const auto serial=serial_++;
        if(serial%7==3){++dropped_;return DatagramSendStatus::Sent;}
        pending_.push_back({peer,{bytes.begin(),bytes.end()},traffic,
            Network::Clock::now()+std::chrono::milliseconds(70+(serial%4)*20)});
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress& peer,std::vector<std::uint8_t>& bytes,std::string& error) override {return base_.receive(peer,bytes,error);}
    void service() override {
        base_.service();const auto now=Network::Clock::now();unsigned sent=0;
        for(auto it=pending_.begin();it!=pending_.end() && sent<32;) {
            if(it->due>now){++it;continue;}
            std::string error;const auto result=base_.send_status(it->peer,it->bytes,it->traffic,error);
            if(result==DatagramSendStatus::Error)throw std::runtime_error(error);
            if(result==DatagramSendStatus::WouldBlock){++it;continue;}
            it=pending_.erase(it);++sent;
        }
    }
    std::uint64_t dropped() const {return dropped_;}
private:
    struct Packet {PeerAddress peer;std::vector<std::uint8_t> bytes;TransportTrafficClass traffic;Network::Clock::time_point due;};
    SessionTransport& base_;std::deque<Packet> pending_;std::uint64_t serial_=0,dropped_=0;
};
}
