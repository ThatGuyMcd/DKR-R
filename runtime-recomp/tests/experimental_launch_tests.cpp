#include "netplay/experimental_launch.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
PeerAddress address(unsigned owner) {PeerAddress a{};a.size=1;a.storage[0]=std::uint8_t(owner+1);return a;}
struct Bus {
    struct Packet {unsigned from,to,at;std::vector<std::uint8_t> bytes;TransportTrafficClass traffic;};
    std::deque<Packet> packets;
    std::string bootstrap;
    unsigned time=0,sends=0;
    bool adverse=true,block=false,drop_ack=false;
};
struct Transport final : SessionTransport {
    Bus& bus;unsigned owner;bool open_=true,ready_game=true;unsigned retained=0;
    Transport(Bus& b,unsigned p):bus(b),owner(p) {}
    bool open(std::uint16_t,std::string&) override {return open_=true;}
    void close() override {open_=false;}
    bool is_open() const override {return open_;}
    std::uint16_t local_port() const override {return 0;}
    bool quick_join() const override {return true;}
    bool traffic_ready(const PeerAddress&,TransportTrafficClass traffic) const override {
        return open_ && (ready_game || (traffic!=TransportTrafficClass::Realtime && traffic!=TransportTrafficClass::Authoritative));
    }
    std::size_t maximum_plaintext_datagram_bytes() const override {return 65536;}
    void set_quick_join_bootstrap(std::string value) override {assert(owner==0);bus.bootstrap=std::move(value);}
    bool take_quick_join_bootstrap(std::string& value,PeerAddress& source) override {
        if(owner!=1 || bus.bootstrap.empty())return false;value=bus.bootstrap;source=address(0);return true;
    }
    void retain_peer_route(const PeerAddress&) override {++retained;}
    void release_peer_route(const PeerAddress&) override {assert(retained);--retained;}
    DatagramSendStatus send_status(const PeerAddress& to,std::span<const std::uint8_t> bytes,TransportTrafficClass traffic,std::string&) override {
        assert(to==address(1-owner) && bytes.size()<=Launch::kChunkBytes+160);
        if(bus.block)return DatagramSendStatus::WouldBlock;
        const auto id=++bus.sends;
        if(bus.adverse && id%5==0)return DatagramSendStatus::Sent;
        if(bus.drop_ack && owner==1 && traffic==TransportTrafficClass::Control)return DatagramSendStatus::Sent;
        bus.packets.push_back({owner,1-owner,bus.time+(bus.adverse?id%7:0),{bytes.begin(),bytes.end()},traffic});
        if(bus.adverse && id%3==0)bus.packets.push_front(bus.packets.back());
        if(bus.adverse && id%11==0) {auto bad=bus.packets.back();bad.bytes.back()^=1;bus.packets.push_front(std::move(bad));}
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress& source,std::vector<std::uint8_t>& bytes,std::string&) override {
        for(auto i=bus.packets.begin();i!=bus.packets.end();++i)if(i->to==owner && i->at<=bus.time) {
            source=address(i->from);bytes=std::move(i->bytes);bus.packets.erase(i);return true;
        }return false;
    }
};
LaunchContract contract(unsigned bytes) {
    LaunchContract c;c.revision=77;c.schema=17;c.state_bytes=bytes;c.epoch=91;
    c.build.fill(1);c.rom.fill(2);c.rules.fill(3);c.abi.fill(4);return c;
}
auto at(unsigned ms) {return Launch::Clock::time_point{}+std::chrono::milliseconds(ms);}
std::vector<std::uint8_t> baseline(unsigned bytes) {
    std::vector<std::uint8_t> out(bytes);for(unsigned i=0;i<bytes;++i)out[i]=std::uint8_t(i*7+(i>>12));return out;
}
void transfer(unsigned size,bool adverse) {
    Bus bus;bus.adverse=adverse;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
    const auto state=baseline(size);const auto c=contract(size);unsigned prepares=0;
    assert(host.host(c,state,error));
    assert(client.join(c,[&](auto bytes,std::string&) {
        assert(std::equal(bytes.begin(),bytes.end(),state.begin(),state.end()));
        return ++prepares<9?PreparationStep::Pending:PreparationStep::Ready;
    },error));
    bool released=false;
    for(bus.time=0;bus.time<40000;++bus.time) {
        assert(host.service_launch(at(bus.time)) && client.service_launch(at(bus.time)));
        if(host.view().phase==LaunchPhase::Ready && !released) {
            assert(client.view().phase==LaunchPhase::Ready && prepares==9);
            assert(!host.is_open() && !client.is_open());assert(host.release(error));released=true;
        }
        if(host.is_open() && client.is_open())break;
    }
    assert(bus.time<40000 && released && prepares==9);
    assert(host.view().baseline_received==size && client.view().baseline_received==size);
    const auto h=host.network_configuration(),p=client.network_configuration();assert(h && p);
    assert(h->match_id==p->match_id && h->incarnation==p->incarnation && h->peers[1].admitted_key==p->peers[0].admitted_key &&
           h->local_sender_id==p->peers[0].sender_id && p->local_sender_id==h->peers[1].sender_id);
    assert(host.contract()==c && client.contract()==c);assert(!host.release(error));
    // Independent game key survives this admission transaction; no stable
    // session or file writer was consulted. Routes release exactly once.
    host.close();client.close();assert(ht.retained==0 && ct.retained==0 && ht.open_ && ct.open_);
}
void contract_mismatches() {
    for(unsigned field=0;field<10;++field) {
        Bus bus;bus.adverse=false;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
        auto c=contract(256),bad=c;
        switch(field) {
            case 0:bad.build[0]^=1;break;case 1:bad.rom[0]^=1;break;case 2:bad.rules[0]^=1;break;
            case 3:bad.abi[0]^=1;break;case 4:++bad.schema;break;case 5:++bad.epoch;break;
            case 6:bad.revision=80;break;case 7:++bad.state_bytes;break;case 8:++bad.input_delay;break;case 9:++bad.prediction_window;break;
        }
        assert(host.host(c,baseline(256),error));unsigned prepared=0;
        assert(client.join(bad,[&](auto,std::string&){++prepared;return PreparationStep::Ready;},error));
        assert(!client.service_launch(at(0)) && client.view().phase==LaunchPhase::Failed && prepared==0);
        assert(host.service_launch(at(0)) && host.view().phase==LaunchPhase::AwaitingPeer && !host.network_configuration());
    }
}
void preparation_failure_and_timeout() {
    for(unsigned mode=0;mode<3;++mode) {
        Bus bus;bus.adverse=false;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
        assert(host.host(contract(256),baseline(256),error));
        assert(client.join(contract(256),[&](auto,std::string& e) {
            if(mode==0){e="owned-world fence";return PreparationStep::Failed;}
            if(mode==1)throw std::runtime_error("owned-world throw");return PreparationStep::Pending;
        },error));
        for(bus.time=0;bus.time<46000;++bus.time) {
            assert(host.service_launch(at(bus.time)));if(!client.service_launch(at(bus.time)))break;
        }
        assert(bus.time<46000 && client.view().phase==LaunchPhase::Failed && !host.is_open() && !client.is_open());
    }
    Bus bus;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
    assert(host.host(contract(256),baseline(256),error));
    assert(client.join(contract(256),[](auto,std::string&){return PreparationStep::Ready;},error));
    bus.block=true;assert(client.service_launch(at(0)));assert(!client.service_launch(at(45001)));
    assert(client.view().phase==LaunchPhase::Failed && host.view().phase==LaunchPhase::AwaitingPeer);
}
void dropped_release_ack_preserves_inputs() {
    Bus bus;bus.adverse=false;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
    assert(host.host(contract(256),baseline(256),error));
    assert(client.join(contract(256),[](auto,std::string&){return PreparationStep::Ready;},error));
    for(bus.time=0;host.view().phase!=LaunchPhase::Ready && bus.time<1000;++bus.time)
        assert(host.service_launch(at(bus.time)) && client.service_launch(at(bus.time)));
    assert(host.view().phase==LaunchPhase::Ready);bus.drop_ack=true;assert(host.release(error));
    assert(host.service_launch(at(++bus.time)) && client.service_launch(at(bus.time)) && client.is_open());
    assert(!host.is_open());const auto conf=client.network_configuration();assert(conf);
    // Send a domain-separated Network-sized early packet while host is still
    // waiting for release ACK. Admission preserves it, not arbitrary large work.
    secure::Key game_key{};game_key.fill(19);std::array<std::uint8_t,8> payload{};
    const auto bytes=secure::seal(payload,game_key,conf->local_sender_id,1,conf->match_id);
    assert(client.send_status(conf->peers[0].address,bytes,TransportTrafficClass::Realtime,error)==DatagramSendStatus::Sent);
    assert(host.service_launch(at(++bus.time)));PeerAddress source;std::vector<std::uint8_t> received;
    assert(!host.receive(source,received,error));bus.drop_ack=false;
    for(unsigned n=0;n<300 && !host.is_open();++n) {++bus.time;assert(client.service_launch(at(bus.time)) && host.service_launch(at(bus.time)));}
    assert(host.is_open() && host.receive(source,received,error) && received==bytes && source==address(1));
}
void delayed_channel_open_and_reuse() {
    Bus bus;bus.adverse=false;Transport ht(bus,0),ct(bus,1);Launch host(ht),client(ct);std::string error;
    for(unsigned launch=0;launch<2;++launch) {
        assert(host.host(contract(256),baseline(256),error));
        assert(client.join(contract(256),[](auto,std::string&){return PreparationStep::Ready;},error));
        for(;host.view().phase!=LaunchPhase::Ready && bus.time<10000;++bus.time)
            assert(host.service_launch(at(bus.time)) && client.service_launch(at(bus.time)));
        assert(host.view().phase==LaunchPhase::Ready);ht.ready_game=false;assert(!host.release(error));
        ht.ready_game=true;ct.ready_game=false;assert(host.release(error));
        assert(host.service_launch(at(++bus.time)) && client.service_launch(at(bus.time)));
        assert(!host.is_open() && !client.is_open() && client.view().phase==LaunchPhase::Ready);
        for(unsigned n=0;n<150;++n){++bus.time;assert(host.service_launch(at(bus.time)) && client.service_launch(at(bus.time)));}
        // Original Release was already admitted into the replay filter. Open
        // notification alone must resume the retained intent, without needing
        // another NEW encrypted Release or a simulation-time polling loop.
        ct.ready_game=true;
        for(unsigned n=0;n<300 && !host.is_open();++n){++bus.time;assert(client.service_launch(at(bus.time)) && host.service_launch(at(bus.time)));}
        assert(host.is_open() && client.is_open());host.close();client.close();
        assert(ht.retained==0 && ct.retained==0);
    }
}
}
int main() {
    transfer(1,false);transfer(Launch::kChunkBytes*7+29,true);transfer(16U*1024U*1024U+1324,true);
    contract_mismatches();preparation_failure_and_timeout();dropped_release_ack_preserves_inputs();delayed_channel_open_and_reuse();
    std::cout<<"Experimental Quick Join admission MODEL transport: full baseline, matching contract, bounded preparation, release repair passed.\n";
}
