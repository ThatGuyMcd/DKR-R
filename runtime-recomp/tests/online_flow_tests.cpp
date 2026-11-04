#include "netplay/direct_session.hpp"
#include "netplay/experimental_lobby_admission.hpp"
#include "netplay/experimental_pump.hpp"
#include "online_stability/startup_transport.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>

#if defined(DKR_PIPELINE_TRANSFER)
// This ROM-free world has no SDK2 scripts. The package's optional extension
// channel is inactive; all immutable game input still uses the real Network.
namespace dkr::sdk2 {
void network_epoch(std::uint64_t,unsigned,unsigned){}
std::vector<std::uint8_t> network_packet(unsigned){return {};}
bool network_receive(unsigned,std::span<const std::uint8_t>){return true;}
}
#endif

namespace n=dkr::runtime::netplay;
namespace ex=n::experimental;
namespace {
struct LinkBus {
    struct Packet {unsigned from=0,to=0,due=0;std::vector<std::uint8_t> bytes;};
    unsigned time=0,players=3,serial=0,held_release=0,blocked_inputs=0,release_at=0,last_start=0;
    bool startup_fault=true;unsigned rtt=6,jitter=13,loss_every=7;
    std::array<n::PeerAddress,4> addresses{};std::array<n::secure::Key,4> keys{};
    std::deque<Packet> packets;
};
struct Link final:n::SessionTransport {
    LinkBus& bus;unsigned local;bool opened=true;
    Link(LinkBus& b,unsigned i):bus(b),local(i){}
    bool open(std::uint16_t,std::string&)override{return opened=true;}
    void close()override{opened=false;}
    bool is_open()const override{return opened;}
    std::uint16_t local_port()const override{return 0;}
    std::size_t maximum_plaintext_datagram_bytes()const override{return n::protocol::kMaximumQuickJoinDatagramBytes;}
    n::DatagramSendStatus send_status(const n::PeerAddress& destination,std::span<const std::uint8_t> bytes,n::TransportTrafficClass traffic,std::string& error)override{
        error.clear();unsigned to=bus.players;for(unsigned p=0;p<bus.players;++p)if(bus.addresses[p]==destination)to=p;
        assert(to<bus.players&&to!=local);
        std::uint64_t sender=0,sequence=0;std::vector<std::uint8_t> plain;
        const auto& key=bus.keys[local?local:to];
        n::protocol::Datagram datagram;
        assert(n::secure::open(bytes,key,90105,sender,sequence,plain));
        assert(n::protocol::decode(plain,datagram,error,n::protocol::kMaximumQuickJoinDatagramBytes));
        if(bus.startup_fault&&local==0&&to==bus.players-1&&
            (datagram.header.type==n::protocol::MessageType::Start||datagram.header.type==n::protocol::MessageType::LobbyState)){
            n::protocol::StartPayload start;n::protocol::LobbyStatePayload room;
            const bool released=datagram.header.type==n::protocol::MessageType::Start?
                n::protocol::decode_start(datagram.payload,start,error)&&start.stage==1:
                n::protocol::decode_lobby_state(datagram.payload,room,error)&&room.phase==n::RoomPhase::Running;
            if(released){if(!bus.release_at)bus.release_at=bus.time;
                if(bus.time<bus.release_at+10000){++bus.held_release;return n::DatagramSendStatus::Sent;}}
        }
        if(bus.startup_fault&&local==bus.players-1&&bus.last_start&&bus.time>=bus.last_start+4000&&bus.time<bus.last_start+8000&&
            (traffic==n::TransportTrafficClass::Realtime||traffic==n::TransportTrafficClass::Authoritative)){
            ++bus.blocked_inputs;return n::DatagramSendStatus::WouldBlock;
        }
        const auto serial=++bus.serial;
        if(traffic==n::TransportTrafficClass::Realtime&&serial%bus.loss_every==0)return n::DatagramSendStatus::Sent;
        LinkBus::Packet packet{local,to,bus.time+bus.rtt/2+serial%bus.jitter,{bytes.begin(),bytes.end()}};
        bus.packets.push_back(packet);if(serial%11==0)bus.packets.push_back(packet);
        return n::DatagramSendStatus::Sent;
    }
    bool receive(n::PeerAddress& source,std::vector<std::uint8_t>& bytes,std::string& error)override{
        error.clear();for(auto i=bus.packets.begin();i!=bus.packets.end();++i)if(i->to==local&&i->due<=bus.time){source=bus.addresses[i->from];bytes=std::move(i->bytes);bus.packets.erase(i);return true;}return false;
    }
};
struct World final:ex::SceneSimulation {
    std::array<std::uint64_t,2> state{};
    std::vector<std::uint64_t> committed;
    ex::SimulationContract contract()const override{return {17,sizeof(state),ex::kRequiredStateDomains,true,true,true};}
    bool capture(std::span<std::uint8_t> b,std::string&)override{if(b.size()!=sizeof(state))return false;std::memcpy(b.data(),state.data(),b.size());return true;}
    bool restore(std::span<const std::uint8_t> b,std::string&)override{if(b.size()!=sizeof(state))return false;std::memcpy(state.data(),b.data(),b.size());return true;}
    bool tick(std::uint32_t frame,const n::FrameInputs& inputs,ex::TickOutput& out,std::string&)override{
        assert(state[0]==frame);++state[0];for(const auto& input:inputs)state[1]=state[1]*31+input.buttons+unsigned(int(input.stick_x)+128);
        out.effects.resize(8);std::memcpy(out.effects.data(),&state[1],8);return true;
    }
    bool commit(std::uint64_t,std::uint32_t frame,std::span<const std::uint8_t> b,bool,std::string&)override{
        assert(frame==committed.size()&&b.size()==8);std::uint64_t value;std::memcpy(&value,b.data(),8);committed.push_back(value);return true;
    }
    bool confirmed_boundary(ex::BoundaryIntent&,std::string&)override{return false;}
    ex::PreparationStep prepare_scene(std::uint64_t,std::uint64_t&,std::string&)override{return ex::PreparationStep::Failed;}
};
}
namespace dkr::runtime::netplay {
struct DirectSessionTestAccess {
    static void stop(DirectSession& s){
        {std::scoped_lock lock(s.mutex_);s.worker_stop_.store(true,std::memory_order_release);}
        s.state_changed_.notify_all();if(s.network_worker_.joinable())s.network_worker_.join();
    }
    static void initialize(LinkBus& bus,const std::array<std::unique_ptr<DirectSession>,4>& sessions){
        const auto now=std::chrono::steady_clock::now();
        for(unsigned p=0;p<bus.players;++p){bus.addresses[p].size=1;bus.addresses[p].storage[0]=std::uint8_t(p+1);bus.keys[p]=secure::generate_key();}
        for(unsigned p=0;p<bus.players;++p){auto& s=*sessions[p];stop(s);s.transport_=std::make_unique<Link>(bus,p);
            s.is_host_=p==0;s.owned_backend_available_=true;s.state_=ConnectionState::Loading;s.match_id_=90105;
            s.sender_id_=p+1;s.host_sender_id_=1;s.local_slot_=std::uint8_t(p);s.local_name_="Flow "+std::to_string(p+1);
            s.input_delay_=2;s.manifest_.session_save_hash=5;s.session_save_generation_=1;s.local_online_save_ready_=true;
            s.last_admission_response_=s.last_client_request_=s.owned_last_host_packet_=now;
            if(p){s.host_address_=bus.addresses[0];s.key_=bus.keys[p];}
        }
        auto& host=*sessions[0];std::string error;Rules rules;rules.maximum_players=4;rules.synchronization=SynchronizationMode::ExperimentalRollback;rules.rollback_window=20;rules.automatic_input_delay=false;
        assert(host.lobby_.create("90105","FLOW12","Production flow",Visibility::Private,"1","Flow 1",host.manifest_,rules,error));
        for(unsigned p=1;p<bus.players;++p){const auto slot=host.lobby_.join(std::to_string(p+1),"Flow "+std::to_string(p+1),host.manifest_,error);assert(slot&&*slot==p);
            auto& peer=host.peers_[p];peer.active=true;peer.sender_id=p+1;peer.slot=std::uint8_t(p);peer.address=bus.addresses[p];peer.key=bus.keys[p];peer.online_save_ready=true;peer.owned_last_packet=now;}
        for(unsigned p=0;p<bus.players;++p)assert(host.lobby_.set_ready(std::to_string(p+1),true,error));
        assert(host.lobby_.begin_loading("1",error));host.room_view_=host.lobby_.room();
        const auto launch=host.make_launch_descriptor();assert(valid_launch_descriptor(launch));
        for(unsigned p=0;p<bus.players;++p){sessions[p]->room_view_=host.room_view_;sessions[p]->launch_descriptor_=launch;}
    }
    static void pump(DirectSession& s){std::scoped_lock lock(s.mutex_);s.pump_locked();}
};
}
namespace {
class LobbyRoute final:public n::SessionTransport {
    n::DirectSession& lobby;
public:
    explicit LobbyRoute(n::DirectSession& s):lobby(s){}
    bool open(std::uint16_t,std::string&)override{return false;}
    void close()override{}
    bool is_open()const override{return lobby.owned_game_active();}
    std::uint16_t local_port()const override{return 0;}
    std::size_t maximum_plaintext_datagram_bytes()const override{return 12352;}
    n::DatagramSendStatus send_status(const n::PeerAddress& p,std::span<const std::uint8_t> b,n::TransportTrafficClass t,std::string& e)override{return lobby.send_owned_packet(p.storage[3],b,t,e);}
    n::DatagramSendStatus send_live_status(const n::PeerAddress& p,std::span<const std::uint8_t> b,std::uint8_t owner,std::string& e)override{return lobby.send_owned_packet(p.storage[3],b,n::TransportTrafficClass::Realtime,e,owner);}
    bool receive(n::PeerAddress& p,std::vector<std::uint8_t>& b,std::string& e)override{
        n::DirectSession::OwnedPacket packet;e.clear();if(!lobby.take_owned_packet(packet))return false;p=ex::LobbyAdmission::address(packet.source);b=std::move(packet.bytes);return true;
    }
    bool configuration(const n::secure::Key& incarnation,ex::NetworkConfiguration& config,std::string& error){return lobby.owned_network_configuration(incarnation,config,error);}
};
using FlowRoute=dkr::online_stability::StartupTransport<n::SessionTransport,n::TransportTrafficClass,ex::LobbyAdmission,LobbyRoute>;
void trace(unsigned players,bool faults=true){
    LinkBus bus;bus.players=players;std::array<std::unique_ptr<n::DirectSession>,4> sessions;
    if(!faults){bus.startup_fault=false;bus.rtt=150;bus.jitter=30;bus.loss_every=100;}
    for(unsigned p=0;p<players;++p)sessions[p]=std::make_unique<n::DirectSession>();
    n::DirectSessionTestAccess::initialize(bus,sessions);
    std::array<World,4> worlds;std::array<std::unique_ptr<LobbyRoute>,4> routes;
    std::array<std::unique_ptr<ex::LobbyAdmission>,4> admissions;std::array<std::unique_ptr<FlowRoute>,4> flows;
    std::array<std::unique_ptr<ex::Session>,4> games;std::array<std::unique_ptr<ex::Network>,4> networks;
    std::array<std::unique_ptr<ex::Pump>,4> pumps;std::array<bool,4> loaded{};
    std::array<unsigned,4> samples{};std::string error;
    ex::AdmissionContract contract;contract.engine[0]=1;contract.rom[0]=2;contract.schema=17;contract.state_bytes=sizeof(worlds[0].state);contract.epoch=91;
    std::vector<std::uint8_t> baseline(sizeof(worlds[0].state));
    for(unsigned p=0;p<players;++p){routes[p]=std::make_unique<LobbyRoute>(*sessions[p]);
        admissions[p]=std::make_unique<ex::LobbyAdmission>(*routes[p],p==0,std::uint8_t(players),std::uint8_t(p),contract);
        assert(admissions[p]->begin(p==0?baseline:std::vector<std::uint8_t>{},[&,p](auto b,std::string& e){return worlds[p].restore(b,e);},error));
        flows[p]=std::make_unique<FlowRoute>(*admissions[p],*routes[p]);}
    const auto start=ex::Network::Clock::now();bool complete=false;
    for(bus.time=0;bus.time<150000;bus.time+=5){
        for(unsigned p=0;p<players;++p)n::DirectSessionTestAccess::pump(*sessions[p]);
        for(unsigned p=0;p<players;++p){
            if(networks[p]){if(!networks[p]->service(start+std::chrono::milliseconds(bus.time)))throw std::runtime_error("Network owner "+std::to_string(p)+": "+networks[p]->error());}
            else if(!admissions[p]->service_admission())throw std::runtime_error("Admission owner "+std::to_string(p)+": "+admissions[p]->error()+"; "+sessions[p]->runtime_view().status);
            if(!networks[p]&&(p==0||admissions[p]->prepared())){
                games[p]=std::make_unique<ex::Session>(worlds[p]);assert(games[p]->start({{91,std::uint8_t(players),20,1024*1024},std::uint8_t(p),2},error));
                ex::NetworkConfiguration config;assert(routes[p]->configuration(admissions[p]->incarnation(),config,error));
                networks[p]=std::make_unique<ex::Network>(*games[p],*flows[p]);assert(networks[p]->start(config,error));}
            if(!loaded[p]&&admissions[p]->prepared()){
                sessions[p]->mark_game_loaded(admissions[p]->baseline_identity(),1,5,n::launch_descriptor_hash(*sessions[p]->launch_descriptor()));loaded[p]=true;}
            if(sessions[p]->running()){
                admissions[p]->complete_start();
                if(!pumps[p]){pumps[p]=std::make_unique<ex::Pump>(*games[p],*networks[p]);if(p==players-1)bus.last_start=bus.time;}
                assert(pumps[p]->pulse(start+std::chrono::milliseconds(bus.time),[&,p]{return n::PackedInput{std::uint16_t(++samples[p]*7+p),std::int8_t(int(samples[p]%31)-15),0};},games[p]->frontier()<200));
            }else assert(worlds[p].state[0]==0); // Verified input service never steps before Start.
        }
        complete=bus.last_start&&bus.time>=bus.last_start+10000;for(unsigned p=0;p<players;++p)complete=complete&&worlds[p].committed.size()>=180;
        if(complete)break;
        if(bus.time%100==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!complete)for(unsigned p=0;p<players;++p)std::cerr<<"owner="<<p<<" state="<<unsigned(sessions[p]->runtime_view().state)<<" status="<<sessions[p]->runtime_view().status<<" confirmed="<<worlds[p].committed.size()<<" admission="<<admissions[p]->status()<<" network="<<(networks[p]?networks[p]->error():"none")<<'\n';
    assert(complete&&(!faults||(bus.held_release&&bus.blocked_inputs)));
    for(unsigned p=1;p<players;++p)assert(std::equal(worlds[0].committed.begin(),worlds[0].committed.begin()+180,worlds[p].committed.begin()));
    for(unsigned p=0;p<players;++p){assert(networks[p]->statistics().rejected==0);sessions[p]->disconnect();}
    std::cout<<"Production DirectSession/admission/Network: racers="<<players<<" confirmed=180 RTT="<<bus.rtt<<"ms jitter="<<bus.jitter<<"ms live-loss=1/"<<bus.loss_every<<" delayed-release="<<bus.held_release<<" input-backpressure="<<bus.blocked_inputs<<'\n';
}
}
int main()try{for(unsigned players=2;players<=4;++players){trace(players);trace(players,false);}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
