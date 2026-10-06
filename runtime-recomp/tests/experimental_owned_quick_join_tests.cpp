#define DKR_QUICK_JOIN_TESTING 1
#include "netplay/quick_join_transport.cpp"
#include "netplay/experimental_launch.hpp"
#include "netplay/experimental_pump.hpp"
#include "owned_game.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
#ifndef DKR_OWNED_FULL_SCENES
#define DKR_OWNED_FULL_SCENES 0
#endif

namespace dkr::runtime::netplay { namespace {
std::vector<std::uint8_t> private_file(const char* variable,std::size_t maximum) {
    const char* path=std::getenv(variable);if(!path)throw std::runtime_error(std::string("Missing private fixture variable ")+variable);
    std::ifstream stream(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!stream || stream.tellg()<=0 || stream.tellg()>std::streamoff(maximum))throw std::runtime_error("Invalid private fixture file size.");
    std::vector<std::uint8_t> bytes(std::size_t(stream.tellg()));stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Private fixture could not be read.");return bytes;
}
PackedInput owned_sample(unsigned frame,unsigned owner) {
    return {std::uint16_t(frame%4?0x8000:0),std::int8_t(int((frame+owner)%5)*25-50),std::int8_t(int(frame%3)*30-30)};
}
// Impair only authenticated gameplay AFTER real Quick Join admission. Zero-
// latency loopback can legitimately deliver every input before the next tick;
// requiring a rollback in that case was a scheduling-dependent assertion.
// This private bounded queue forces late inputs without faking a transport,
// changing production channels or delaying the 16-MiB baseline transaction.
class ImpairedGameplay final : public SessionTransport {
public:
    explicit ImpairedGameplay(SessionTransport& base) : base_(base) {}
    bool open(std::uint16_t,std::string& error) override {error="Already borrowed test transport.";return false;}
    void close() override {pending_.clear();}
    bool is_open() const override {return base_.is_open();}
    std::uint16_t local_port() const override {return base_.local_port();}
    bool quick_join() const override {return true;}
    bool traffic_ready(const PeerAddress& peer,TransportTrafficClass traffic) const override {return base_.traffic_ready(peer,traffic);}
    DatagramSendStatus send_status(const PeerAddress& peer,std::span<const std::uint8_t> bytes,
        TransportTrafficClass traffic,std::string& error) override {
        if(bytes.size()>118){error="Unexpected test gameplay envelope.";return DatagramSendStatus::Error;}
        if(pending_.size()>=128)return DatagramSendStatus::WouldBlock;
        const auto serial=serial_++;
        // One in seven sealed packets is lost; repair must recover its owner
        // input. Others take 70-130 ms, creating deterministic reordering.
        if(serial%7==3){++dropped_;return DatagramSendStatus::Sent;}
        pending_.push_back({peer,{bytes.begin(),bytes.end()},traffic,
            experimental::Network::Clock::now()+std::chrono::milliseconds(70+(serial%4)*20)});
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress& peer,std::vector<std::uint8_t>& bytes,std::string& error) override {return base_.receive(peer,bytes,error);}
    void service() override {
        base_.service();const auto now=experimental::Network::Clock::now();unsigned sent=0;
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
    struct Packet {PeerAddress peer;std::vector<std::uint8_t> bytes;TransportTrafficClass traffic;experimental::Network::Clock::time_point due;};
    SessionTransport& base_;std::deque<Packet> pending_;std::uint64_t serial_=0,dropped_=0;
};
struct QuickJoinTestAccess {
    static void run(bool continuous=false,bool adventure=false) {
        using namespace experimental;
        assert(!continuous || DKR_OWNED_FULL_SCENES);
        assert(!adventure || continuous);
        std::cout.setf(std::ios::unitbuf);
        // REAL guest CPU/audio/inputs/Paks/Magic and real Quick Join, but still
        // private loopback signaling and no real renderer/controller frontend.
        // An actually initialized four-player fixture with two owners is NOT
        // separately initialized 2P scene or WAN/multi-machine qualification.
        const auto fixture=private_file("DKR_PROBE_FIXTURE",Launch::kMaximumBaseline);
        const auto rom=private_file("DKR_PROBE_ROM",32U*1024U*1024U);
        struct ServerState {
            std::mutex mutex;std::map<std::string,std::shared_ptr<rtc::WebSocket>> routes;
            std::vector<std::shared_ptr<rtc::WebSocket>> sockets;
        };
        const auto state=std::make_shared<ServerState>();rtc::WebSocketServer::Configuration server_config;
        server_config.bindAddress="127.0.0.1";server_config.port=0;rtc::WebSocketServer server(server_config);
        server.onClient([state](auto socket) {
            {std::scoped_lock lock(state->mutex);state->sockets.push_back(socket);}std::weak_ptr<rtc::WebSocket> weak=socket;
            socket->onOpen([state,weak] {
                const auto socket=weak.lock();if(!socket)return;const auto id=socket->path().value_or("/").substr(1);
                {std::scoped_lock lock(state->mutex);state->routes[id]=socket;}
                socket->onMessage([state,id](rtc::message_variant message) {
                    if(!std::holds_alternative<std::string>(message))return;
                    auto packet=Json::parse(std::get<std::string>(message));if(!packet.contains("dst"))return;
                    std::shared_ptr<rtc::WebSocket> target;
                    {std::scoped_lock lock(state->mutex);const auto found=state->routes.find(packet["dst"].get<std::string>());if(found!=state->routes.end())target=found->second;}
                    packet["src"]=id;if(target && target->isOpen())target->send(packet.dump());
                });socket->send(Json{{"type","OPEN"}}.dump());
            });
        });
        quick_test_signaling_endpoint="ws://127.0.0.1:"+std::to_string(server.port());
        QuickJoinTransport ht(true,"ABCDE"),ct(false,"ABCDE");std::string error;assert(ht.open(0,error));
        const auto wait=[&](auto predicate,unsigned seconds=60) {
            const auto deadline=Network::Clock::now()+std::chrono::seconds(seconds);
            while(!predicate() && Network::Clock::now()<deadline) {ht.service();ct.service();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            assert(predicate());
        };
        wait([&]{return ht.signaling_ready_.load();});
        std::array<PresentationMailbox,3> boxes;
        struct AudioProof {
            std::uint64_t epoch;std::uint32_t frame,rate;std::vector<std::uint8_t> bytes;
            bool operator==(const AudioProof&) const=default;
        };
        std::array<std::vector<AudioProof>,3> pcm;
        std::array<std::unique_ptr<OwnedGame>,3> worlds;
        for(unsigned owner=0;owner<3;++owner) {
            assert(boxes[owner].begin_epoch(continuous ? 90:91));
            worlds[owner]=make_owned_game(fixture,rom,91,boxes[owner],[&,owner](ConfirmedAudio audio,std::string&) {
                if(!pcm[owner].empty()) {
                    const auto& last=pcm[owner].back();
                    assert(audio.epoch>last.epoch || (audio.epoch==last.epoch && audio.frame==last.frame+1));
                }
                pcm[owner].push_back({audio.epoch,audio.frame,audio.rate,{audio.pcm.begin(),audio.pcm.end()}});return true;
            },error,continuous,adventure ? (1U<<24):0);
            if(!worlds[owner])throw std::runtime_error(error);
        }
        LaunchContract contract;contract.revision=checkpoint_hash(rom)==rom::kUsV80Xxh3?80:77;
        contract.schema=worlds[0]->contract().schema;contract.state_bytes=worlds[0]->contract().state_bytes;contract.epoch=91;
        contract.prediction_window=12;contract.input_delay=1;
        // Test identities only. The actual launcher must derive these digests
        // from its verified build/rules/ABI, not these test-local constants.
        contract.build.fill(1);contract.rom.fill(2);contract.rules.fill(3);contract.abi.fill(4);
        std::vector<std::uint8_t> initial(contract.state_bytes);assert(worlds[0]->capture(initial,error));
        assert(worlds[2]->admit_initial(initial,error));
        Launch host(ht),client(ct);assert(host.host(contract,initial,error));assert(ct.open(0,error));
        assert(client.join(contract,[&](auto bytes,std::string& message) {
            return worlds[1]->admit_initial(bytes,message)?PreparationStep::Ready:PreparationStep::Failed;
        },error));
        const auto service_launch=[&] {
            if(!host.service_launch())throw std::runtime_error(host.error());
            if(!client.service_launch())throw std::runtime_error(client.error());
        };
        wait([&]{service_launch();return host.view().phase==LaunchPhase::Ready;});assert(host.release(error));
        wait([&]{service_launch();return host.is_open() && client.is_open();});
        Session hs(*worlds[0]),cs(*worlds[1]);assert(hs.start({{91,2,12},0,1},error) && cs.start({{91,2,12},1,1},error));
        ImpairedGameplay host_link(host),client_link(client);
        Network hn(hs,host_link),cn(cs,client_link);assert(hn.start(*host.network_configuration(),error) && cn.start(*client.network_configuration(),error));
        Pump hp(hs,hn),cp(cs,cn);constexpr unsigned frames=60;
        const auto deadline=Network::Clock::now()+std::chrono::seconds(adventure ? 720:continuous ? 240:60);
        std::array<OwnedSceneView,2> observed{worlds[0]->scene_view(),worlds[1]->scene_view()};
        std::array<std::uint32_t,2> menu_since{};
        std::array<std::uint64_t,2> observed_epoch{91,91};
        const auto destination_ready=[&] {
            return hs.epoch()==cs.epoch() && hs.epoch()>91 &&
                !worlds[0]->scene_view().menu && !worlds[1]->scene_view().menu &&
                (!adventure || (worlds[0]->scene_view().level==0 && worlds[1]->scene_view().level==0 &&
                    worlds[0]->scene_view().race_type==5 && worlds[1]->scene_view().race_type==5));
        };
        while((continuous && !destination_ready()) || hs.statistics().confirmed_frames<frames || cs.statistics().confirmed_frames<frames) {
            if(Network::Clock::now()>deadline)throw std::runtime_error("Owned Quick Join gameplay timed out.");
            for(unsigned owner=0;owner<2;++owner) {
                auto& session=owner?cs:hs;auto& pump=owner?cp:hp;
                const auto scene=worlds[owner]->scene_view();
                if(scene.menu!=observed[owner].menu || scene.menu_id!=observed[owner].menu_id || session.epoch()!=observed_epoch[owner]) {
                    observed[owner]=scene;observed_epoch[owner]=session.epoch();menu_since[owner]=session.frontier();
                    std::cout<<"Quick Join retail owner="<<owner<<" epoch="<<session.epoch()<<" menu="<<scene.menu<<" id="<<scene.menu_id<<'\n';
                }
                // One neutral delay tick is agreed by Timeline. Thereafter
                // freeze/sample the SAME logical owner input exactly once.
                const bool playing=!continuous || (!scene.menu && session.epoch()>91);
                if(!pump.pulse(Network::Clock::now(),[&]{
                    if(continuous && scene.menu) {
                        const auto age=session.frontier()-menu_since[owner];
                        if(scene.menu_id==0 && age>=150 && age%30==0)return PackedInput{0x1000,0,0};
                        if((scene.menu_id==3 || scene.menu_id==15) && age>=50 && age%30==20)return PackedInput{0x8000,0,0};
                        if(adventure && owner==0 && (scene.menu_id==19 || scene.menu_id==6) && age>=30 && age%30==20)
                            return PackedInput{0x8000,0,0};
                        return PackedInput{};
                    }
                    return owned_sample(session.frontier()+1,owner);
                },!playing || session.frontier()<frames))
                    throw std::runtime_error(pump.error());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(continuous) {
            std::vector<std::uint8_t> host_state(contract.state_bytes),client_state(contract.state_bytes);
            assert(hs.epoch()==cs.epoch() && worlds[0]->capture(host_state,error) &&
                   worlds[1]->capture(client_state,error) && host_state==client_state && pcm[0]==pcm[1]);
            assert(hs.statistics().next_frame==frames && cs.statistics().next_frame==frames &&
                   hs.statistics().rollbacks+cs.statistics().rollbacks>0);
            if(adventure)for(unsigned owner=0;owner<2;++owner) {
                const auto scene=worlds[owner]->scene_view();
                assert(scene.owners==2 && scene.viewports==1 && scene.two_player_adventure && scene.level==0 && scene.race_type==5);
            }
            std::cout<<"REAL owned retail menus + REAL impaired Quick Join: epochs="<<hs.epoch()-91
                <<" mode="<<(adventure ? "fresh-adventure":"tracks")<<" gameplay_frames="<<frames<<" confirmed_pcm_blocks="<<pcm[0].size()
                <<" state_pcm_exact=1 corrections="<<hs.statistics().rollbacks+cs.statistics().rollbacks
                <<" simulated_one_way_ms=70-130 dropped="<<host_link.dropped()+client_link.dropped()<<'\n';
            host.close();client.close();ht.close();ct.close();return;
        }
        for(unsigned frame=0;frame<frames;++frame) {
            FrameInputs input{};if(frame)for(unsigned owner=0;owner<2;++owner)input[owner]=owned_sample(frame,owner);
            TickOutput output;assert(worlds[2]->tick(frame,input,output,error) && !output.scene_boundary);
            assert(worlds[2]->commit(91,frame,output.effects,false,error));
        }
        std::vector<std::uint8_t> expected(contract.state_bytes),actual(contract.state_bytes);assert(worlds[2]->capture(expected,error));
        for(unsigned owner=0;owner<2;++owner) {
            assert(worlds[owner]->capture(actual,error) && actual==expected && pcm[owner]==pcm[2] && worlds[owner]->confirmed_count()==frames);
            assert(worlds[owner]->publish_current(91,frames-1,error));const auto snapshot=boxes[owner].take();assert(snapshot && snapshot->descriptor().frame==frames-1);
        }
        assert(hs.statistics().rollbacks+cs.statistics().rollbacks>0);
        std::cout<<"REAL owned DKR CPU/audio via REAL private-loopback Quick Join: authenticated_baseline_bytes="<<contract.state_bytes
                 <<" frames="<<frames<<" corrections="<<hs.statistics().rollbacks+cs.statistics().rollbacks
                 <<" simulated_one_way_ms=70-130 dropped="<<host_link.dropped()+client_link.dropped()
                 <<" canonical_state_pcm_exact=1 render_snapshot_valid=1; NOT WAN/rendered gameplay qualification.\n";
        host.close();client.close();ht.close();ct.close();
    }
};
}}
int main() try {
    const bool adventure=std::getenv("DKR_OWNED_CHECK_ADVENTURE")!=nullptr;
    dkr::runtime::netplay::QuickJoinTestAccess::run(adventure || std::getenv("DKR_OWNED_CHECK_FULL_MENUS")!=nullptr,adventure);return 0;
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
