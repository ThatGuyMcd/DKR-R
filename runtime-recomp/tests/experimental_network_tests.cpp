#include "netplay/experimental_network.hpp"
#include "netplay/experimental_pump.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/experimental_launch.hpp"
#if defined(DKR_EXPERIMENTAL_QUICKJOIN_TEST)
#define DKR_QUICK_JOIN_TESTING 1
#include "netplay/quick_join_transport.cpp"
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
// Transport/simulation MODEL, not a native DKR world or a WAN qualification.
struct World final : SceneSimulation {
    std::array<std::uint64_t,8> state{};
    std::vector<std::array<std::uint64_t,3>> committed;
    unsigned ticks=0,prepares=0,boundary_frame=24;
    bool presentation_pending=false;
    unsigned retirement_pulses=0;
    SimulationContract contract() const override { return {17,sizeof(state),kRequiredStateDomains,true,true,true}; }
    bool capture(std::span<std::uint8_t> out,std::string&) override {
        if(out.size()!=sizeof(state)) return false;
        std::memcpy(out.data(),state.data(),out.size()); return true;
    }
    bool restore(std::span<const std::uint8_t> in,std::string&) override {
        if(in.size()!=sizeof(state)) return false;
        std::memcpy(state.data(),in.data(),in.size()); return true;
    }
    RestoreStep prepare_restore(std::uint64_t,std::uint32_t,std::string&) override {
        ++retirement_pulses;
        return presentation_pending?RestoreStep::Pending:RestoreStep::Ready;
    }
    bool tick(std::uint32_t frame,const FrameInputs& input,TickOutput& out,std::string&) override {
        assert(state[0]==frame); ++ticks;
        for(unsigned owner=0;owner<4;++owner) state[2+owner]+=std::uint64_t(input[owner].buttons)*17+std::uint64_t(int(input[owner].stick_x)+80);
        ++state[0]; out={}; out.effects={std::uint8_t(state[2]),std::uint8_t(state[3])};
        out.scene_boundary=state[0]==boundary_frame; return true;
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes,bool boundary,std::string&) override {
        assert(bytes.size()==2 && boundary==(frame==boundary_frame-1));
        committed.push_back({epoch,frame,std::uint64_t(bytes[0])|(std::uint64_t(bytes[1])<<8)}); return true;
    }
    bool confirmed_boundary(BoundaryIntent& out,std::string&) override {
        if(state[0]!=24) return false;
        out={checkpoint_hash({reinterpret_cast<const std::uint8_t*>(state.data()),sizeof(state)}),100+state[1]+1}; return true;
    }
    PreparationStep prepare_scene(std::uint64_t scene,std::uint64_t& hash,std::string&) override {
        assert(scene==100+state[1]+1 && state[0]==24); state[0]=0; ++state[1]; ++prepares;
        hash=checkpoint_hash({reinterpret_cast<const std::uint8_t*>(state.data()),sizeof(state)}); return PreparationStep::Ready;
    }
};
PeerAddress address(unsigned player) { PeerAddress result{}; result.size=1; result.storage[0]=std::uint8_t(player+1); return result; }
struct Bus {
    struct Packet { unsigned from,to,at; std::vector<std::uint8_t> bytes; };
    std::deque<Packet> packets;
    unsigned time=0,sends=0,delay=0;
    bool adverse=true;
};
struct Transport final : SessionTransport {
    Bus& bus; unsigned local;
    bool open_=true,throw_service=false,block_live=false;
    unsigned attempts=0,receives=0,blocks=0,errors=0,retries=0;
    std::array<unsigned,5> lanes{};
    std::deque<std::vector<std::uint8_t>> retained;
    Transport(Bus& b,unsigned p):bus(b),local(p) {}
    bool open(std::uint16_t,std::string&) override { open_=true; return true; }
    void close() override { open_=false; }
    bool is_open() const override { return open_; }
    std::uint16_t local_port() const override { return 0; }
    void service() override { if(throw_service) throw std::runtime_error("test SDK fault"); }
    DatagramSendStatus send_status(const PeerAddress& destination,std::span<const std::uint8_t> bytes,
                                   TransportTrafficClass traffic,std::string&) override {
        assert(destination.size==1 && bytes.size()<=118); ++attempts; ++lanes[unsigned(traffic)];
        if(block_live && traffic==TransportTrafficClass::Realtime) {
            retained.emplace_back(bytes.begin(),bytes.end()); ++blocks; return DatagramSendStatus::WouldBlock;
        }
        if(bus.adverse && (attempts%17==0 || attempts%37==0)) {
            retained.emplace_back(bytes.begin(),bytes.end());
            if(attempts%17==0) { ++blocks; return DatagramSendStatus::WouldBlock; }
            ++errors; return DatagramSendStatus::Error;
        }
        for(auto it=retained.begin();it!=retained.end();++it) if(std::equal(it->begin(),it->end(),bytes.begin(),bytes.end())) {
            ++retries; retained.erase(it); break;
        }
        const unsigned send=++bus.sends,to=destination.storage[0]-1;
        if(bus.adverse && send%5==0) return DatagramSendStatus::Sent; // network loss, not queue loss
        bus.packets.push_back({local,to,bus.time+bus.delay+send%4,{bytes.begin(),bytes.end()}});
        if(bus.adverse && send%7==0) bus.packets.push_front({local,to,bus.time+bus.delay+1,{bytes.begin(),bytes.end()}});
        if(bus.adverse && send%11==0) { auto bad=std::vector<std::uint8_t>(bytes.begin(),bytes.end()); bad.back()^=1;
            bus.packets.push_back({local,to,bus.time+1,std::move(bad)}); }
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress& source,std::vector<std::uint8_t>& bytes,std::string&) override {
        for(auto it=bus.packets.begin();it!=bus.packets.end();++it) if(it->to==local && it->at<=bus.time) {
            ++receives; source=address(it->from); bytes=std::move(it->bytes); bus.packets.erase(it); return true;
        }
        return false;
    }
};
PackedInput sample(unsigned frame,unsigned owner) { return {std::uint16_t((frame*7+owner)%65536),std::int8_t(int(frame%161)-80),0}; }
void owner_pump() {
    // A paced owner loop, not a native DKR tick/renderer integration. All
    // timings are injected: 1000 UI calls/s must not imply 1000 game ticks/s.
    Bus bus; bus.adverse=false;
    World host_world,client_world; host_world.boundary_frame=client_world.boundary_frame=UINT32_MAX;
    Session host(host_world),client(client_world);
    Transport host_transport(bus,0),client_transport(bus,1);
    Network host_network(host,host_transport),client_network(client,client_transport);
    std::string error;
    assert(host.start({{77,2,6},0,0},error) && client.start({{77,2,6},1,0},error));
    const auto key=secure::generate_key(),incarnation=secure::generate_key();
    NetworkConfiguration h{42,1000},c{42,1001}; h.incarnation=c.incarnation=incarnation;
    h.peers[1]={address(1),1001,key}; c.peers[0]={address(0),1000,key};
    assert(host_network.start(h,error) && client_network.start(c,error));
    Pump host_pump(host,host_network),client_pump(client,client_network);
    const auto at=[](unsigned ms){return Network::Clock::time_point{}+std::chrono::milliseconds(ms);};
    std::array<std::vector<unsigned>,2> sampled;
    const auto host_sample=[&]{sampled[0].push_back(host.frontier());return sample(host.frontier(),0);};
    const auto client_sample=[&]{sampled[1].push_back(client.frontier());return sample(client.frontier(),1);};
    for(bus.time=0;bus.time<=1000;++bus.time) {
        for(unsigned owner=0;owner<2;++owner) {
            auto& pump=owner ? client_pump:host_pump; auto& world=owner ? client_world:host_world;
            const auto ticks=world.ticks;
            assert(pump.pulse(at(bus.time),owner ? std::function<PackedInput()>(client_sample):std::function<PackedInput()>(host_sample)));
            assert(world.ticks-ticks==pump.view().ticks_this_pulse && pump.view().ticks_this_pulse<=4);
        }
    }
    bus.time=1004;
    for(unsigned n=0;n<10;++n) {
        assert(host_pump.pulse(at(1004),host_sample) && client_pump.pulse(at(1004),client_sample));
    }
    for(unsigned owner=0;owner<2;++owner) {
        assert(sampled[owner].size()==31);
        for(unsigned frame=0;frame<31;++frame) assert(sampled[owner][frame]==frame);
    }
    assert(host.frontier()==31 && client.frontier()==31 && host.statistics().confirmed_frames==31 && client.statistics().confirmed_frames==31);
    World reference; reference.boundary_frame=UINT32_MAX;
    for(unsigned frame=0;frame<31;++frame) {
        FrameInputs inputs{}; inputs[0]=sample(frame,0); inputs[1]=sample(frame,1);
        TickOutput out; assert(reference.tick(frame,inputs,out,error)); assert(reference.commit(77,frame,out.effects,false,error));
    }
    assert(host_world.state==reference.state && client_world.state==reference.state &&
           host_world.committed==reference.committed && client_world.committed==reference.committed);
    assert(host_pump.view().wait==PumpWait::FramePacing && !host_pump.view().waiting_for_clients());
    // A missing client stalls at the speculation window, never at an
    // unbounded frame debt. UI calls still service transport and return.
    for(bus.time=1005;bus.time<2000;++bus.time) assert(host_pump.pulse(at(bus.time),host_sample));
    assert(host.frontier()==37 && host.statistics().confirmed_frames==31);
    assert(host_pump.view().waiting_for_clients() && host_pump.view().wait==PumpWait::OwnerInput);
    const auto waiting_samples=sampled[0].size();
    const auto waiting_ticks=host_world.ticks;
    for(unsigned n=0;n<1000;++n) assert(host_pump.pulse(at(2000),host_sample));
    assert(sampled[0].size()==waiting_samples && host_world.ticks==waiting_ticks);
    // Reopening input delivery after a scheduler hitch cannot execute a
    // second worth of NEW ticks in one pulse or resample corrected inputs.
    bus.time=5000;
    for(unsigned n=0;n<100;++n) {
        const auto old_frontier=client.frontier();
        const auto old_samples=sampled[1].size();
        assert(client_pump.pulse(at(5000),client_sample));
        assert(client.frontier()-old_frontier<=1 && sampled[1].size()-old_samples<=1 && client_pump.view().ticks_this_pulse<=4);
        assert(host_pump.pulse(at(5000),host_sample));
        bus.time=5004; // deliver pending model datagrams; monotonic pulse clock stays 5000
    }
    assert(client.statistics().rollbacks && host.statistics().rollbacks);
    // SDK failure halts this coordinator. No further simulation, samples or
    // conversion back to a stable world is permitted by a later pulse.
    const auto old_ticks=host_world.ticks;
    const auto old_samples=sampled[0].size();
    host_transport.close();
    assert(!host_pump.pulse(at(5001),host_sample) && host_pump.view().wait==PumpWait::Failed);
    assert(!host_pump.pulse(at(6000),host_sample) && host_world.ticks==old_ticks && sampled[0].size()==old_samples);
    const auto client_ticks=client_world.ticks;
    assert(!client_pump.pulse(at(4999),client_sample) && client_world.ticks==client_ticks);
    std::cout<<"Experimental paced owner MODEL: 1000 Hz UI, 30 Hz authored ticks, bounded replay/wait/failure exact=1\n";
}
void presentation_drain_keeps_network_live() {
    Bus bus;bus.adverse=false;bus.delay=1000;
    World host_world,client_world;host_world.boundary_frame=client_world.boundary_frame=UINT32_MAX;
    Session host(host_world),client(client_world);
    Transport host_transport(bus,0),client_transport(bus,1);
    Network host_network(host,host_transport),client_network(client,client_transport);
    std::string error;
    assert(host.start({{77,2,6},0,0},error)&&client.start({{77,2,6},1,0},error));
    const auto key=secure::generate_key(),incarnation=secure::generate_key();
    NetworkConfiguration h{42,1000},c{42,1001};h.incarnation=c.incarnation=incarnation;
    h.peers[1]={address(1),1001,key};c.peers[0]={address(0),1000,key};
    assert(host_network.start(h,error)&&client_network.start(c,error));
    Pump host_pump(host,host_network),client_pump(client,client_network);
    const auto at=[](unsigned ms){return Network::Clock::time_point{}+std::chrono::milliseconds(ms);};
    std::vector<unsigned> samples;
    const auto local=[&]{samples.push_back(host.frontier());return sample(host.frontier(),0);};
    const auto remote=[&]{return sample(client.frontier(),1);};
    for(unsigned ms:{0,34,68}) {
        bus.time=ms;assert(host_pump.pulse(at(ms),local)&&client_pump.pulse(at(ms),remote));
    }
    assert(host.frontier()==3&&samples.size()==3&&host.statistics().confirmed_frames==0);
    const auto parked=host_world.state;const auto ticks=host_world.ticks;
    host_world.presentation_pending=true;bus.time=2000;bus.delay=0;
    assert(host_pump.pulse(at(2000),local));
    assert(host_pump.view().wait==PumpWait::PresentationDrain&&host_pump.view().ticks_this_pulse==0);
    const auto received=host_network.statistics().received;
    assert(client_pump.pulse(at(2000),remote)); // Send more actual input/ACKs while host is parked.
    for(unsigned pulse=1;pulse<=1000;++pulse) {
        bus.time=2000+pulse;assert(host_pump.pulse(at(bus.time),local));
        assert(host_pump.view().wait==PumpWait::PresentationDrain&&host_pump.view().ticks_this_pulse==0);
        assert(host.frontier()==3&&samples.size()==3&&host_world.state==parked&&host_world.ticks==ticks&&host_world.committed.empty());
    }
    assert(host_network.statistics().received>received&&host_world.retirement_pulses>=1001);
    assert(host.statistics().restore_checkpoint_loads==1);
    host_world.presentation_pending=false;
    for(unsigned pulse=0;pulse<10&&host.statistics().confirmed_frames<3;++pulse)
        assert(host_pump.pulse(at(3000),local));
    assert(host.frontier()==3&&samples.size()==3&&host.statistics().confirmed_frames==3);
    World reference;reference.boundary_frame=UINT32_MAX;
    for(unsigned frame=0;frame<3;++frame) {
        FrameInputs inputs{};inputs[0]=sample(frame,0);inputs[1]=sample(frame,1);TickOutput out;
        assert(reference.tick(frame,inputs,out,error)&&reference.commit(77,frame,out.effects,false,error));
    }
    assert(host_world.state==reference.state&&host_world.committed==reference.committed);
    std::cout<<"Presentation drain MODEL: 1000 bounded pump returns, encrypted input/ACK service stays live, no RAM/effect/sample mutation, exact corrected state.\n";
}
void distributed(unsigned players,unsigned delay,unsigned input_delay) {
    assert(players>=2 && players<=4);
    Bus bus; bus.delay=delay;
    std::array<std::unique_ptr<World>,4> worlds;
    std::array<std::unique_ptr<Session>,4> sessions;
    std::array<std::unique_ptr<Transport>,4> transports;
    std::array<std::unique_ptr<Network>,4> networks;
    std::array<secure::Key,4> keys{};
    const auto incarnation=secure::generate_key();
    for(unsigned p=1;p<players;++p) keys[p]=secure::generate_key();
    std::string error;
    for(unsigned p=0;p<players;++p) {
        worlds[p]=std::make_unique<World>(); sessions[p]=std::make_unique<Session>(*worlds[p]);
        assert(sessions[p]->start({{77,std::uint8_t(players),6},std::uint8_t(p),std::uint8_t(input_delay)},error));
        transports[p]=std::make_unique<Transport>(bus,p);
        networks[p]=std::make_unique<Network>(*sessions[p],*transports[p]);
        NetworkConfiguration config{42,1000+p};
        config.incarnation=incarnation;
        for(unsigned q=0;q<players;++q) if(q!=p && (!p || !q)) config.peers[q]={address(q),1000+q,keys[p ? p:q]};
        assert(networks[p]->start(config,error));
        assert(!networks[p]->start(config,error)); // nonce epoch cannot reset on this object
    }
    for(;bus.time<5000;++bus.time) {
        bool finished=true;
        for(unsigned p=0;p<players;++p) {
            auto& world=*worlds[p]; auto& session=*sessions[p]; auto& transport=*transports[p];
            const auto before=world.ticks,attempts=transport.attempts,receives=transport.receives;
            assert(networks[p]->service(Network::Clock::time_point{}+std::chrono::milliseconds(bus.time*20)));
            assert(world.ticks==before && transport.attempts-attempts<=32 && transport.receives-receives<=32);
            assert(networks[p]->statistics().pending_packets<=40);
            if(session.epoch()<80) {
                finished=false;
                if(session.needs_local_input()) assert(session.sample_local(sample(session.frontier()+input_delay,p))==OwnerInputResult::Accepted);
                for(unsigned budget=0;budget<8;++budget) {
                    const auto previous=world.ticks+world.prepares; const auto result=session.step();
                    if(result==SessionStep::Failed) std::cerr<<session.error()<<'\n';
                    assert(result!=SessionStep::Failed && world.ticks+world.prepares-previous<=1);
                    if(result!=SessionStep::Replayed) break;
                }
            }
        }
        // Client may prepare a new scene before host receives Release ACK.
        if(finished && std::all_of(sessions.begin(),sessions.begin()+players,[](const auto& s){return s->epoch()==80;})) break;
    }
    assert(bus.time<5000);
    World reference;
    for(unsigned scene=0;scene<3;++scene) {
        for(unsigned frame=0;frame<24;++frame) {
            FrameInputs inputs{};
            // Unoccupied owners stay neutral, including stick x zero (+80).
            if(frame>=input_delay) for(unsigned p=0;p<players && p<4;++p) inputs[p]=sample(frame,p);
            TickOutput out; assert(reference.tick(frame,inputs,out,error));
            assert(reference.commit(77+scene,frame,out.effects,out.scene_boundary,error));
        }
        std::uint64_t hash=0; assert(reference.prepare_scene(101+scene,hash,error)==PreparationStep::Ready);
    }
    for(unsigned p=0;p<players;++p) {
        assert(worlds[p]->state==reference.state && worlds[p]->committed==reference.committed);
        const auto stats=networks[p]->statistics(); const auto& t=*transports[p];
        assert(stats.rejected && stats.duplicate && stats.backpressure && stats.send_errors && t.retries);
        assert(t.lanes[unsigned(TransportTrafficClass::Control)] && t.lanes[unsigned(TransportTrafficClass::Realtime)] &&
               t.lanes[unsigned(TransportTrafficClass::Authoritative)]);
        assert(!t.lanes[unsigned(TransportTrafficClass::Replica)] && !t.lanes[unsigned(TransportTrafficClass::Checkpoint)]);
    }
    std::cout<<"Experimental ENCRYPTED TRANSPORT MODEL: peers="<<players<<" delay="<<delay<<" input_delay="<<input_delay<<" exact=1\n";
}
void boundaries() {
    Bus bus; bus.adverse=false; World world; Session session(world); Transport transport(bus,0); Network network(session,transport);
    std::string error; assert(session.start({{77,2,6},0,0},error));
    NetworkConfiguration config{42,1000}; config.peers[1]={address(1),1001,secure::generate_key()};
    config.incarnation=secure::generate_key();
    auto bad=config; bad.peers[1].sender_id=1000; assert(!network.start(bad,error));
    bad=config; bad.peers[1].admitted_key={}; assert(!network.start(bad,error));
    bad=config; bad.incarnation={}; assert(!network.start(bad,error));
    assert(network.start(config,error));
    OwnerInputPacket input{}; input.epoch=77; input.players=2; input.owner=1; input.count=1; input.inputs[0]=sample(0,1);
    // A perfectly valid packet under the STABLE admitted key cannot enter the
    // experiment, even with a matching sender/match id and owner payload.
    auto stable=secure::seal(encode_owner_inputs(input),config.peers[1].admitted_key,1001,1,42);
    bus.packets.push_back({1,0,0,stable});
    for(unsigned n=0;n<100;++n) bus.packets.push_back({1,0,0,std::vector<std::uint8_t>(5000,1)});
    assert(network.service() && transport.receives==32 && world.ticks==0);
    assert(network.statistics().rejected==32);
    transport.throw_service=true;
    assert(!network.service() && network.error().find("test SDK fault")!=std::string::npos);
    assert(!network.service() && world.ticks==0);
}
void cadence_and_freshness() {
    Bus bus; bus.adverse=false;
    World host_world,client_world; Session host(host_world),client(client_world);
    Transport host_transport(bus,0),client_transport(bus,1);
    Network host_network(host,host_transport),client_network(client,client_transport);
    std::string error;
    assert(host.start({{77,2,6},0,0},error) && client.start({{77,2,6},1,0},error));
    const auto key=secure::generate_key(),incarnation=secure::generate_key();
    NetworkConfiguration h{42,1000},c{42,1001}; h.incarnation=c.incarnation=incarnation;
    h.peers[1]={address(1),1001,key}; c.peers[0]={address(0),1000,key};
    assert(host_network.start(h,error) && client_network.start(c,error));
    const auto at=[](unsigned ms){return Network::Clock::time_point{}+std::chrono::milliseconds(ms);};
    assert(host_network.service(at(0)) && host_transport.attempts==1);
    // A hot 1000 Hz UI loop cannot create 1000 identical ACK packets. Receive
    // remains serviced on every call and no world tick happens in service.
    for(unsigned n=0;n<1000;++n) assert(host_network.service(at(0)));
    assert(host_transport.attempts==1 && host_world.ticks==0 && host_network.statistics().suppressed_resends>=1000);
    assert(host_network.service(at(32)) && host_transport.attempts==1);
    assert(host_network.service(at(33)) && host_transport.attempts==2);
    host_transport.block_live=true;
    for(unsigned frame=0;frame<6;++frame) {
        assert(host.sample_local(sample(frame,0))==OwnerInputResult::Accepted);
        assert(host.step()==SessionStep::Advanced);
        assert(host_network.service(at(34+frame)));
    }
    assert(host_network.statistics().superseded_live==5);
    assert(host_transport.blocks==2); // 5 ms retry floor, despite six changed live batches
    assert(host_network.statistics().pending_packets<=40);
    host_transport.block_live=false;
    assert(host_network.service(at(44)));
    bus.time=100; assert(client_network.service(at(44)));
    const auto ack=client.packet_for(1,0);
    // Newest redundant live data includes all six immutable samples. Earlier
    // blocked packets did not strand the client or rewrite any owner sample.
    assert(ack && ack->received_next[0]==6 && client_world.ticks==0);
    assert(!client_network.service(at(43)) && client_network.error().find("clock moved backwards")!=std::string::npos);
    std::cout<<"Experimental unchanged-packet cadence and congested-live freshness checks passed.\n";
}
void real_udp(unsigned players) {
    const auto incarnation=secure::generate_key();
    std::array<secure::Key,4> keys{};
    std::array<PeerAddress,4> addresses{};
    std::array<std::unique_ptr<World>,4> worlds;
    std::array<std::unique_ptr<Session>,4> sessions;
    std::array<std::unique_ptr<SessionTransport>,4> transports;
    std::array<std::unique_ptr<Network>,4> networks;
    std::string error;
    for(unsigned p=0;p<players;++p) {
        if(p) keys[p]=secure::generate_key();
        transports[p]=make_udp_session_transport(); assert(transports[p]->open(0,error));
        assert(DatagramSocket::resolve("127.0.0.1",transports[p]->local_port(),addresses[p],error));
        worlds[p]=std::make_unique<World>(); sessions[p]=std::make_unique<Session>(*worlds[p]);
        assert(sessions[p]->start({{77,std::uint8_t(players),6},std::uint8_t(p),1},error));
    }
    for(unsigned p=0;p<players;++p) {
        NetworkConfiguration config{42,1000+p}; config.incarnation=incarnation;
        for(unsigned q=0;q<players;++q) if(q!=p && (!p || !q)) config.peers[q]={addresses[q],1000+q,keys[p ? p:q]};
        networks[p]=std::make_unique<Network>(*sessions[p],*transports[p]);
        assert(networks[p]->start(config,error));
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    for(;;) {
        assert(std::chrono::steady_clock::now()<deadline);
        for(unsigned p=0;p<players;++p) {
            assert(networks[p]->service());
            auto& s=*sessions[p];
            if(s.epoch()==80) continue;
            if(s.needs_local_input()) assert(s.sample_local(sample(s.frontier()+1,p))==OwnerInputResult::Accepted);
            for(unsigned budget=0;budget<8;++budget) {
                const auto result=s.step(); assert(result!=SessionStep::Failed);
                if(result!=SessionStep::Replayed) break;
            }
        }
        if(std::all_of(sessions.begin(),sessions.begin()+players,[](const auto& s){return s->epoch()==80;})) break;
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    World reference;
    for(unsigned scene=0;scene<3;++scene) {
        for(unsigned frame=0;frame<24;++frame) {
            FrameInputs inputs{};
            if(frame) for(unsigned p=0;p<players && p<4;++p) inputs[p]=sample(frame,p);
            TickOutput out; assert(reference.tick(frame,inputs,out,error));
            assert(reference.commit(77+scene,frame,out.effects,out.scene_boundary,error));
        }
        std::uint64_t hash=0; assert(reference.prepare_scene(101+scene,hash,error)==PreparationStep::Ready);
    }
    for(unsigned p=0;p<players;++p) {
        assert(worlds[p]->state==reference.state && worlds[p]->committed==reference.committed);
        assert(networks[p]->statistics().sent && networks[p]->statistics().received);
    }
    std::cout<<"Experimental real LOOPBACK UDP / MODEL world: peers="<<players<<" scene_epochs=3 exact=1\n";
}
}
#if defined(DKR_EXPERIMENTAL_QUICKJOIN_TEST)
namespace dkr::runtime::netplay { namespace {
struct QuickJoinTestAccess {
    static void run() {
        // Actual local WebSocket rendezvous, ICE/DTLS/SCTP channels and the
        // same experimental encrypted adapter; no production endpoint or
        // user's profile/friend/lobby state is contacted or changed.
        struct ServerState {
            std::mutex mutex;
            std::map<std::string,std::shared_ptr<rtc::WebSocket>> routes;
            std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
        };
        const auto server_state=std::make_shared<ServerState>();
        rtc::WebSocketServer::Configuration server_config;
        server_config.bindAddress="127.0.0.1"; server_config.port=0;
        rtc::WebSocketServer server(server_config);
        server.onClient([server_state](auto socket) {
            { std::scoped_lock lock(server_state->mutex); server_state->sockets.push_back(socket); }
            std::weak_ptr<rtc::WebSocket> weak=socket;
            const auto registered=std::make_shared<std::atomic<bool>>(false);
            const auto register_socket=[server_state,weak,registered] {
                const auto socket=weak.lock(); if(!socket) return;
                if(registered->exchange(true)) return;
                const auto id=socket->path().value_or("/").substr(1);
                { std::scoped_lock lock(server_state->mutex); server_state->routes[id]=socket; }
                socket->onMessage([server_state,id](rtc::message_variant message) {
                    if(!std::holds_alternative<std::string>(message)) return;
                    auto packet=Json::parse(std::get<std::string>(message));
                    if(!packet.contains("dst")) return;
                    std::shared_ptr<rtc::WebSocket> target;
                    { std::scoped_lock lock(server_state->mutex); const auto found=server_state->routes.find(packet["dst"].get<std::string>());
                      if(found!=server_state->routes.end()) target=found->second; }
                    packet["src"]=id; if(target && target->isOpen()) target->send(packet.dump());
                });
                socket->send(Json{{"type","OPEN"}}.dump());
            };
            socket->onOpen(register_socket);
            // The accepted socket can finish its handshake before onClient
            // installs onOpen under load. Cover that ordering without sending
            // duplicate OPEN messages; this is the private loopback fixture.
            if(socket->isOpen()) register_socket();
        });
        quick_test_signaling_endpoint="ws://127.0.0.1:"+std::to_string(server.port());
        QuickJoinTransport host(true,"ABCDE"),client(false,"ABCDE");
        std::string error; assert(host.open(0,error));
        const auto wait=[&](const char* stage,auto predicate) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(!predicate() && std::chrono::steady_clock::now()<deadline) {
                host.service(); client.service(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(!predicate()) throw std::runtime_error(std::string("Quick Join fixture timed out at: ")+stage);
        };
        wait("host registration",[&]{return host.signaling_ready_.load();});
        host.set_quick_join_bootstrap("experimental-private-test"); assert(client.open(0,error));
        std::string bootstrap; PeerAddress host_address{},client_address{};
        wait("client bootstrap",[&]{return !bootstrap.empty() || client.take_quick_join_bootstrap(bootstrap,host_address);});
        assert(bootstrap=="experimental-private-test");
        wait("host peer route",[&] {
            std::scoped_lock lock(host.mutex_);
            if(host.peers_.empty()) return false;
            client_address=host.peers_.begin()->second->address;
            return true;
        });
        for(const auto lane:{TransportTrafficClass::Control,TransportTrafficClass::Realtime,TransportTrafficClass::Authoritative})
            wait("data channel lane",[&]{return host.traffic_ready(client_address,lane) && client.traffic_ready(host_address,lane);});
        World host_world,client_world; Session host_session(host_world),client_session(client_world);
        Launch host_launch(host),client_launch(client);
        LaunchContract contract;contract.revision=77;contract.schema=17;contract.epoch=77;
        contract.state_bytes=sizeof(host_world.state);contract.input_delay=1;
        contract.build.fill(1);contract.rom.fill(2);contract.rules.fill(3);contract.abi.fill(4);
        std::vector<std::uint8_t> initial(contract.state_bytes);
        std::memcpy(initial.data(),host_world.state.data(),initial.size());
        assert(host_launch.host(contract,initial,error));
        unsigned preparation_pulses=0;
        assert(client_launch.join(contract,[&](auto bytes,std::string&) {
            assert(std::equal(bytes.begin(),bytes.end(),initial.begin(),initial.end()));
            if(++preparation_pulses<8)return PreparationStep::Pending;
            std::memcpy(client_world.state.data(),bytes.data(),bytes.size());return PreparationStep::Ready;
        },error));
        wait("owned launch preparation",[&]{
            assert(host_launch.service_launch() && client_launch.service_launch());
            return host_launch.view().phase==LaunchPhase::Ready;
        });
        assert(preparation_pulses==8 && !host_launch.is_open() && !client_launch.is_open());
        assert(host_launch.release(error));
        wait("owned launch release",[&]{
            assert(host_launch.service_launch() && client_launch.service_launch());
            return host_launch.is_open() && client_launch.is_open();
        });
        assert(host_session.start({{77,2,6},0,1},error) && client_session.start({{77,2,6},1,1},error));
        const auto host_config=host_launch.network_configuration(),client_config=client_launch.network_configuration();
        assert(host_config && client_config);
        Network host_network(host_session,host_launch),client_network(client_session,client_launch);
        assert(host_network.start(*host_config,error) && client_network.start(*client_config,error));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(host_session.epoch()!=80 || client_session.epoch()!=80) {
            assert(std::chrono::steady_clock::now()<deadline);
            for(unsigned owner=0;owner<2;++owner) {
                auto& s=owner ? client_session:host_session; auto& n=owner ? client_network:host_network;
                assert(n.service());
                if(s.epoch()==80) continue;
                if(s.needs_local_input()) assert(s.sample_local(sample(s.frontier()+1,owner))==OwnerInputResult::Accepted);
                for(unsigned budget=0;budget<8;++budget) {
                    const auto result=s.step(); assert(result!=SessionStep::Failed);
                    if(result!=SessionStep::Replayed) break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        World reference;
        for(unsigned scene=0;scene<3;++scene) {
            for(unsigned frame=0;frame<24;++frame) {
                FrameInputs input{}; if(frame) for(unsigned owner=0;owner<2;++owner) input[owner]=sample(frame,owner);
                TickOutput out; assert(reference.tick(frame,input,out,error));
                assert(reference.commit(77+scene,frame,out.effects,out.scene_boundary,error));
            }
            std::uint64_t hash=0; assert(reference.prepare_scene(101+scene,hash,error)==PreparationStep::Ready);
        }
        assert(host_world.state==reference.state && client_world.state==reference.state &&
               host_world.committed==reference.committed && client_world.committed==reference.committed);
        host_launch.close();client_launch.close();host.close(); client.close();
        std::cout<<"Experimental real LOOPBACK QUICK JOIN / MODEL world: authenticated_launch=1 owned_baseline=1 peers=2 scene_epochs=3 exact=1\n";
    }
};
}}
#endif
int main() {
#if defined(DKR_EXPERIMENTAL_QUICKJOIN_TEST)
    dkr::runtime::netplay::QuickJoinTestAccess::run();
#else
    boundaries();
    owner_pump();
    presentation_drain_keeps_network_live();
    cadence_and_freshness();
    for(unsigned players:{2,3,4}) for(unsigned delay:{0,12,30}) for(unsigned input_delay:{0,3}) distributed(players,delay,input_delay);
    for(unsigned players:{2,3,4}) real_udp(players);
    std::cout<<"Experimental network bounds, nonce isolation and model replay checks passed.\n";
#endif
}
