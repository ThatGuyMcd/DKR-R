#include "transport.hpp"
#include "live_datagram.hpp"
#include "online_stability/traffic_meter.hpp"
#include "online_stability/live_send_capacity.hpp"
#include "online_stability/qualification_delivery.hpp"
#include "netplay/transport_send_policy.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>
#if defined(DKR_NETPLAY_WEBRTC)
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#endif

namespace dkr::relay {
namespace {
class Transport;
struct Entry {Transport* owner=nullptr;View snapshot;};
struct Registry {std::mutex mutex;std::map<const void*,Entry> entries;};
Registry& registry(){
    // The runtime owns a DirectSession before main(). Construct this registry
    // on first use and retain it through global session teardown as well.
    static auto* const instance=new Registry;
    return *instance;
}
struct Event {Id route;unsigned type=0;Bytes bytes;std::string text;};
struct Events {
    std::mutex mutex;std::deque<Event> pending;std::size_t bytes=0;bool active=true;
    std::weak_ptr<net::TransportReceiveSignal> signal;
    void push(Event e){std::lock_guard lock(mutex);const auto size=e.bytes.size()+e.text.size();if(!active||pending.size()>=512||size>1024*1024-bytes)return;bytes+=size;pending.push_back(std::move(e));if(auto s=signal.lock())s->notify();}
    std::deque<Event> take(){std::lock_guard lock(mutex);bytes=0;std::deque<Event> out;out.swap(pending);return out;}
};
bool endpoint(std::string value,net::PeerAddress& address){const auto at=value.rfind(':');if(at==std::string::npos)return false;try{const auto port=std::stoul(value.substr(at+1));if(!port||port>65535)return false;std::string error;return net::DatagramSocket::resolve(value.substr(0,at),std::uint16_t(port),address,error);}catch(...){return false;}}
class Transport final:public net::SessionTransport {
public:
    Transport(const void* key,std::unique_ptr<net::SessionTransport> base,NetworkOptions options):key_(key),base_(std::move(base)),options_(std::move(options)),events_(std::make_shared<Events>()),engine_(Io{
        [this](const Grant& g,Id peer,std::span<const std::uint8_t> bytes,unsigned lane){return wire_send(g,peer,bytes,lane);},
        [this](const Grant& g){return ready(g);},[this](const Grant& g,bool initiate){open_extra(g,initiate);},
        [this](Id id){close_extra(id);},[this](Id id,std::string text){signal_extra(id,std::move(text));},
        [this](Id source,Bytes b,unsigned lane){if(deliveries_.size()>=128)return false;deliveries_.push_back({source,std::move(b),lane});return true;},
        [this](Id id){return ice(id);}}) {
        auto& state=registry();std::lock_guard lock(state.mutex);state.entries[key_]={this,{}};
#if defined(DKR_NETPLAY_WEBRTC)
        closer_=std::jthread([this](std::stop_token stop){for(;;){std::shared_ptr<rtc::PeerConnection> pc;{
            std::unique_lock lock(close_mutex_);close_cv_.wait(lock,[&]{return stop.stop_requested()||!retired_.empty();});
            if(retired_.empty()&&stop.stop_requested())break;if(!retired_.empty()){pc=std::move(retired_.front());retired_.pop_front();}}
            if(pc){try{pc->close();}catch(...){}pc.reset();}}});
#endif
    }
    ~Transport()override{close();
#if defined(DKR_NETPLAY_WEBRTC)
        closer_.request_stop();close_cv_.notify_all();if(closer_.joinable())closer_.join();
#endif
        auto& state=registry();std::lock_guard lock(state.mutex);const auto i=state.entries.find(key_);if(i!=state.entries.end()&&i->second.owner==this)state.entries.erase(i);
    }
    bool open(std::uint16_t port,std::string& error)override {return base_->open(port,error);}
    void close()override {live_.reset();engine_.reset();meter_.reset();deliveries_.clear();qualification_.clear();addresses_.clear();base_->close();service_at_=snapshot_at_={};snapshot();}
    bool is_open()const override{return base_->is_open();}
    std::uint16_t local_port()const override{return base_->local_port();}
    net::DatagramSendStatus send_status(const net::PeerAddress& to,std::span<const std::uint8_t> bytes,net::TransportTrafficClass traffic,std::string& error)override {
        const auto id=identify(to);if(id&&engine_.send(id,bytes,unsigned(traffic)))return net::DatagramSendStatus::Sent;
        if(id&&live_.send(id,bytes,unsigned(traffic)))return net::DatagramSendStatus::Sent;
        const auto result=base_->send_status(to,bytes,traffic,error);meter_.sent(unsigned(traffic),bytes.size(),result==net::DatagramSendStatus::Sent);return result;
    }
    net::DatagramSendStatus send_live_status(const net::PeerAddress& to,std::span<const std::uint8_t> b,std::uint8_t owner,std::string& e)override {
        const auto id=identify(to);if(id&&engine_.send(id,b,unsigned(net::TransportTrafficClass::Realtime)))return net::DatagramSendStatus::Sent;
        if(id&&live_.send(id,b,unsigned(net::TransportTrafficClass::Realtime)))return net::DatagramSendStatus::Sent;
        const auto result=base_->send_live_status(to,b,owner,e);meter_.sent(unsigned(net::TransportTrafficClass::Realtime),b.size(),result==net::DatagramSendStatus::Sent);return result;
    }
    bool receive(net::PeerAddress& source,Bytes& bytes,std::string& error)override {
        return qualification_.receive(source,bytes,error,[this](auto& s,auto& b,auto& e){return receive_now(s,b,e);});
    }
    bool receive_now(net::PeerAddress& source,Bytes& bytes,std::string& error) {
        gather();if(delivery(source,bytes))return true;
        for(unsigned n=0;n<32;++n){if(!base_->receive(source,bytes,error))return false;
            if(bytes.size()<5||!std::equal(bytes.begin(),bytes.begin()+5,std::array<std::uint8_t,5>{'D','K','R','P',1}.begin()))return true;
            if(const auto g=engine_.grant_for_wire(bytes)) {
                Id physical=identify(source);
                if(!base_->quick_join()&&(engine_.context().local==g->endpoint||engine_.context().local==g->proxy)) {
                    net::PeerAddress expected;const bool local_proxy=engine_.context().local==g->proxy;
                    const auto resolved=resolved_.find(g->id);if(resolved!=resolved_.end())expected=local_proxy?resolved->second.endpoint:resolved->second.proxy;
                    if(expected&&expected==source)physical=local_proxy?g->endpoint:g->proxy;
                }
                if(physical)engine_.wire(g->id,physical,bytes);
            }
            if(delivery(source,bytes))return true;
        }bytes.clear();return false;
    }
    void service()override{base_->service();gather();tick();snapshot();meter_.pulse();}
    void set_receive_signal(std::shared_ptr<net::TransportReceiveSignal> signal)override {live_.signal(signal);base_->set_receive_signal(signal);std::lock_guard lock(events_->mutex);events_->signal=signal;}
    void retain_peer_route(const net::PeerAddress& a)override{base_->retain_peer_route(a);}
    void release_peer_route(const net::PeerAddress& a)override{base_->release_peer_route(a);}
    bool take_peer_departure(net::PeerAddress& a)override{while(base_->take_peer_departure(a)){if(!engine_.usable(identify(a)))return true;}return false;}
    std::size_t buffered_bytes(net::TransportTrafficClass c)const override{auto bytes=base_->buffered_bytes(c);
#if defined(DKR_NETPLAY_WEBRTC)
        for(const auto& [id,p]:extras_)if(p.channels[unsigned(c)])bytes+=p.channels[unsigned(c)]->bufferedAmount();
#endif
        return bytes;}
    bool traffic_ready(const net::PeerAddress& a,net::TransportTrafficClass c)const override {
        return base_->traffic_ready(a,c)||engine_.usable(identify(a));
    }
    std::size_t maximum_plaintext_datagram_bytes()const override{return base_->maximum_plaintext_datagram_bytes();}
    void discard_received(net::TransportTrafficClass c)override{live_.discard(unsigned(c));base_->discard_received(c);std::erase_if(deliveries_,[&](const auto& p){return p.lane==unsigned(c);});}
    bool quick_join()const override{return base_->quick_join();}
    std::string quick_join_code()const override{return base_->quick_join_code();}
    void set_quick_join_bootstrap(std::string s)override{base_->set_quick_join_bootstrap(std::move(s));}
    bool rekey_quick_join(std::string s,std::string& e)override{return base_->rekey_quick_join(std::move(s),e);}
    net::QuickJoinRekeyStatus rekey_status()const override{return base_->rekey_status();}
    bool take_quick_join_bootstrap(std::string& s,net::PeerAddress& a)override{return base_->take_quick_join_bootstrap(s,a);}
    void configure(Context c,std::vector<Address> addresses){addresses_=std::move(addresses);live_.configure(c,options_.ice_servers);engine_.configure(std::move(c));gather();tick();snapshot();}
    bool control(Id source,std::span<const std::uint8_t> bytes){return bytes.size()>=6&&bytes[5]==6?live_.control(source,bytes):engine_.control(source,bytes);}
    std::vector<Control> controls(){auto out=engine_.controls();for(auto& packet:live_.controls())out.push_back(std::move(packet));return out;}
    void reset(){live_.reset();engine_.reset();meter_.reset();deliveries_.clear();service_at_=snapshot_at_={};snapshot();}
    void recover(Id peer){engine_.recover(Clock::now(),peer);snapshot();}
private:
    Id identify(const net::PeerAddress& a)const{for(const auto& p:addresses_)if(p.address==a)return p.id;return 0;}
    net::PeerAddress address(Id id)const{for(const auto& p:addresses_)if(p.id==id)return p.address;return {};}
    bool delivery(net::PeerAddress& source,Bytes& bytes){while(!deliveries_.empty()){auto p=std::move(deliveries_.front());deliveries_.pop_front();source=address(p.source);if(source){bytes=std::move(p.bytes);return true;}}return false;}
    bool underlay_ready(Id peer)const{const auto a=address(peer);if(!a)return false;for(unsigned i=0;i<5;++i)if(!base_->traffic_ready(a,net::TransportTrafficClass(i)))return false;return true;}
    bool ready(const Grant& g)const {
        const auto local=engine_.context().local;if(!g.proxy)return underlay_ready(local==g.host?g.endpoint:g.host);
        if(local==g.host)return underlay_ready(g.proxy);
#if defined(DKR_NETPLAY_WEBRTC)
        if(base_->quick_join()){auto p=extras_.find(g.id);if(p==extras_.end())return false;for(const auto& c:p->second.channels)if(!c||!c->isOpen())return false;return true;}
#endif
        return !base_->quick_join()&&resolved_.contains(g.id);
    }
    bool wire_send(const Grant& g,Id to,std::span<const std::uint8_t> bytes,unsigned lane) {
        if(lane>4||bytes.size()>MaxWire)return false;const auto local=engine_.context().local;
        // Host/proxy leg uses its pinned original transport, never another
        // selected relay. UDP client/proxy traffic uses that same socket.
        if(to==g.host||local==g.host||!g.proxy){std::string error;const auto a=address(to);const bool sent=a&&base_->send_status(a,bytes,net::TransportTrafficClass(lane),error)==net::DatagramSendStatus::Sent;meter_.sent(lane,bytes.size(),sent);return sent;}
        if(!base_->quick_join()) {const auto p=resolved_.find(g.id);if(p==resolved_.end())return false;const auto& a=to==g.endpoint?p->second.endpoint:p->second.proxy;std::string error;const bool sent=base_->send_status(a,bytes,net::TransportTrafficClass(lane),error)==net::DatagramSendStatus::Sent;meter_.sent(lane,bytes.size(),sent);return sent;}
#if defined(DKR_NETPLAY_WEBRTC)
        auto p=extras_.find(g.id);if(p==extras_.end())return false;auto channel=p->second.channels[lane];if(!channel||!channel->isOpen())return false;
        std::size_t aggregate=0;for(auto& c:p->second.channels)if(c)aggregate+=c->bufferedAmount();
        const auto queued=channel->bufferedAmount();
        if(!net::quick_join_admit(net::TransportTrafficClass(lane),queued,aggregate,bytes.size())||
           !dkr::online_stability::fresh_input_capacity(net::TransportTrafficClass(lane),queued,bytes.size())){meter_.sent(lane,bytes.size(),false);return false;}
        try{const auto sent=channel->send(reinterpret_cast<const std::byte*>(bytes.data()),bytes.size());const bool accepted=net::quick_join_send_accepted(sent,channel->isOpen(),p->second.pc->state()==rtc::PeerConnection::State::Connected,true);meter_.sent(lane,bytes.size(),accepted);return accepted;}catch(...){meter_.sent(lane,bytes.size(),false);return false;}
#else
        return false;
#endif
    }
    void tick(){const auto now=Clock::now();if(now>=service_at_){engine_.service(now);service_at_=now+std::chrono::milliseconds(10);}}
    void snapshot(){const auto now=Clock::now();if(now<snapshot_at_)return;snapshot_at_=now+std::chrono::milliseconds(250);auto v=engine_.view(now);
        std::map<Id,Id> selected;for(const auto& r:v.rows)if(r.selected)selected[r.endpoint]=r.route;
        for(const auto& [peer,route]:selected)if(!selected_.contains(peer)||selected_[peer]!=route){
            const auto row=std::find_if(v.rows.begin(),v.rows.end(),[&](const auto& r){return r.route==route;});
            std::fprintf(stderr,"[online][proxy] endpoint=%llu event=selected p95-ms=%u lost=%u samples=%u one-hop=1\n",(unsigned long long)peer,row->local.p95,row->local.lost,row->local.samples);}
        for(const auto& [peer,route]:selected_)if(!selected.contains(peer))std::fprintf(stderr,"[online][proxy] endpoint=%llu event=direct-fallback\n",(unsigned long long)peer);
        selected_=std::move(selected);auto& state=registry();std::lock_guard lock(state.mutex);auto i=state.entries.find(key_);if(i!=state.entries.end()&&i->second.owner==this)i->second.snapshot=std::move(v);
    }
    void gather(){
        Id source=0;Bytes sealed;unsigned lane=0;
        while(deliveries_.size()<128&&live_.receive(source,sealed,lane))deliveries_.push_back({source,std::move(sealed),lane});
        for(auto& e:events_->take()) {
        if(!engine_.grant(e.route))continue;
        if(e.type==0)engine_.signal(e.route,std::move(e.text));
        else if(e.type==1){const auto* g=engine_.grant(e.route);const auto peer=engine_.context().local==g->endpoint?g->proxy:g->endpoint;engine_.wire(e.route,peer,e.bytes);}
#if defined(DKR_NETPLAY_WEBRTC)
        else if(e.type==2){const auto p=extras_.find(e.route);if(p!=extras_.end()&&e.bytes.size()==1&&e.bytes[0]<5){
            auto state=channel_events_.find(e.route);if(state!=channel_events_.end()){std::lock_guard lock(state->second->mutex);p->second.channels=state->second->channels;}}}
#endif
    }}
#if defined(DKR_NETPLAY_WEBRTC)
    struct Extra {std::shared_ptr<rtc::PeerConnection> pc;std::array<std::shared_ptr<rtc::DataChannel>,5> channels;std::deque<std::pair<std::string,std::string>> candidates;bool described=false;};
    struct ChannelEvents {std::mutex mutex;std::array<std::shared_ptr<rtc::DataChannel>,5> channels;};
    std::map<Id,Extra> extras_;std::map<Id,std::shared_ptr<ChannelEvents>> channel_events_;
    std::jthread closer_;std::mutex close_mutex_;std::condition_variable close_cv_;std::deque<std::shared_ptr<rtc::PeerConnection>> retired_;
    static std::string label(unsigned i){return "dkrr-proxy-"+std::to_string(i);}
    void bind(Id id,std::shared_ptr<rtc::DataChannel> channel){if(!channel)return;const auto name=channel->label();unsigned lane=5;for(unsigned i=0;i<5;++i)if(name==label(i))lane=i;if(lane==5){channel->close();return;}
        auto state=channel_events_.find(id);if(state==channel_events_.end())return;{std::lock_guard lock(state->second->mutex);if(state->second->channels[lane]){channel->close();return;}state->second->channels[lane]=channel;}
        const std::weak_ptr<Events> events(events_);channel->onOpen([events,id,lane]{if(auto p=events.lock())p->push({id,2,{std::uint8_t(lane)}, {}});});
        channel->onMessage([events,id](rtc::message_variant value){if(!std::holds_alternative<rtc::binary>(value))return;const auto& b=std::get<rtc::binary>(value);if(b.size()>MaxWire)return;if(auto p=events.lock())p->push({id,1,Bytes(reinterpret_cast<const std::uint8_t*>(b.data()),reinterpret_cast<const std::uint8_t*>(b.data())+b.size()),{}});});
        if(channel->isOpen())events_->push({id,2,{std::uint8_t(lane)}, {}});
        // Only weak callback state is captured. Route removal cannot re-enter
        // DirectSession or dereference a destroyed adapter.
    }
#endif
    void open_extra(const Grant& g,bool initiate) {
        if(!base_->quick_join()){
            Resolved r;if(endpoint(g.endpoint_address,r.endpoint)&&endpoint(g.proxy_address,r.proxy))resolved_[g.id]=r;return;
        }
#if defined(DKR_NETPLAY_WEBRTC)
        if(extras_.contains(g.id)||extras_.size()>=6)return;
        try{rtc::Configuration config;for(const auto& server:options_.ice_servers)config.iceServers.emplace_back(server);
            auto pc=std::make_shared<rtc::PeerConnection>(config);extras_.emplace(g.id,Extra{pc});channel_events_[g.id]=std::make_shared<ChannelEvents>();
            const std::weak_ptr<Events> events(events_);const auto id=g.id;
            pc->onLocalDescription([events,id](rtc::Description d){if(auto p=events.lock())p->push({id,0,{},nlohmann::json{{"type",d.typeString()},{"sdp",std::string(d)}}.dump()});});
            pc->onLocalCandidate([events,id](rtc::Candidate c){if(auto p=events.lock())p->push({id,0,{},nlohmann::json{{"candidate",std::string(c)},{"mid",c.mid()}}.dump()});});
            // Incoming channel callbacks publish handles to a mailbox. They
            // never touch the owner-thread maps or the adapter itself.
            const std::weak_ptr<ChannelEvents> state(channel_events_[id]);
            pc->onDataChannel([events,state,id](std::shared_ptr<rtc::DataChannel> channel){
                unsigned lane=5;for(unsigned i=0;i<5;++i)if(channel->label()==label(i))lane=i;if(lane==5){channel->close();return;}
                if(auto s=state.lock()){std::lock_guard lock(s->mutex);if(s->channels[lane]){channel->close();return;}s->channels[lane]=channel;}else return;
                channel->onOpen([events,id,lane]{if(auto p=events.lock())p->push({id,2,{std::uint8_t(lane)}, {}});});
                channel->onMessage([events,id](rtc::message_variant value){if(!std::holds_alternative<rtc::binary>(value))return;const auto& b=std::get<rtc::binary>(value);if(b.size()>MaxWire)return;if(auto p=events.lock())p->push({id,1,Bytes(reinterpret_cast<const std::uint8_t*>(b.data()),reinterpret_cast<const std::uint8_t*>(b.data())+b.size()),{}});});
                if(channel->isOpen())if(auto p=events.lock())p->push({id,2,{std::uint8_t(lane)}, {}});
            });
            if(initiate)for(unsigned lane=0;lane<5;++lane){rtc::DataChannelInit init;init.reliability.unordered=lane!=0;if(lane==2)dkr::online_stability::configure_live_reliability(init.reliability);else if(lane==3)init.reliability.maxRetransmits=0;bind(id,pc->createDataChannel(label(lane),init));}
        }catch(...){close_extra(g.id);}
#else
        (void)g;(void)initiate;
#endif
    }
    void signal_extra(Id id,std::string value) {
#if defined(DKR_NETPLAY_WEBRTC)
        auto p=extras_.find(id);if(p==extras_.end())return;const auto message=nlohmann::json::parse(value,nullptr,false);if(!message.is_object())return;
        try{if(message.contains("sdp")&&message["sdp"].is_string()&&message.contains("type")&&message["type"].is_string()) {
                p->second.pc->setRemoteDescription(rtc::Description(message["sdp"].get<std::string>(),message["type"].get<std::string>()));p->second.described=true;
                for(const auto& c:p->second.candidates)p->second.pc->addRemoteCandidate(rtc::Candidate(c.first,c.second));p->second.candidates.clear();
            }else if(message.contains("candidate")&&message["candidate"].is_string()&&message.contains("mid")&&message["mid"].is_string()) {
                auto candidate=message["candidate"].get<std::string>(),mid=message["mid"].get<std::string>();if(candidate.size()>4096||mid.size()>64)return;
                if(p->second.described)p->second.pc->addRemoteCandidate(rtc::Candidate(candidate,mid));else if(p->second.candidates.size()<32)p->second.candidates.emplace_back(std::move(candidate),std::move(mid));
            }}catch(...){close_extra(id);}
#else
        (void)id;(void)value;
#endif
    }
    void close_extra(Id id) {
        resolved_.erase(id);
#if defined(DKR_NETPLAY_WEBRTC)
        auto p=extras_.find(id);if(p==extras_.end())return;auto pc=std::move(p->second.pc);extras_.erase(p);channel_events_.erase(id);
        {std::lock_guard lock(close_mutex_);retired_.push_back(std::move(pc));}close_cv_.notify_one();
#else
        (void)id;
#endif
    }
    std::string ice(Id id)const {
#if defined(DKR_NETPLAY_WEBRTC)
        auto p=extras_.find(id);if(p!=extras_.end()) {rtc::Candidate local,remote;if(p->second.pc->getSelectedCandidatePair(&local,&remote)){std::ostringstream out;out<<local.type()<<'/'<<remote.type();return out.str();}}
#endif
        return base_->quick_join()?"host WebRTC leg":"UDP";
    }
    struct Resolved {net::PeerAddress endpoint,proxy;};std::map<Id,Resolved> resolved_;
    const void* key_;std::unique_ptr<net::SessionTransport> base_;NetworkOptions options_;std::shared_ptr<Events> events_;Engine engine_;
    LiveDatagram live_;
    dkr::online_stability::TrafficMeter meter_;
    dkr::online_stability::qualification_delivery::Queue<net::PeerAddress> qualification_;
    Clock::time_point service_at_{},snapshot_at_{};std::map<Id,Id> selected_;
    struct Delivery {Id source;Bytes bytes;unsigned lane;};
    std::vector<Address> addresses_;std::deque<Delivery> deliveries_;
};
Transport* lookup(const void* key){auto& state=registry();std::lock_guard lock(state.mutex);const auto p=state.entries.find(key);return p==state.entries.end()?nullptr:p->second.owner;}
} // namespace
std::unique_ptr<net::SessionTransport> wrap(const void* key,std::unique_ptr<net::SessionTransport> base,NetworkOptions options){if(!base)return {};return std::make_unique<Transport>(key,std::move(base),std::move(options));}
void configure(const void* key,Context c,std::vector<Address> a){if(auto* p=lookup(key))p->configure(std::move(c),std::move(a));}
bool control(const void* key,Id source,std::span<const std::uint8_t> b){if(auto* p=lookup(key))return p->control(source,b);return false;}
std::vector<Control> controls(const void* key){if(auto* p=lookup(key))return p->controls();return {};}
void reset(const void* key){if(auto* p=lookup(key))p->reset();}
void recover(const void* key,Id peer){if(auto* p=lookup(key))p->recover(peer);}
View view(const void* key){auto& state=registry();std::lock_guard lock(state.mutex);const auto p=state.entries.find(key);return p==state.entries.end()?View{}:p->second.snapshot;}
unsigned rtt(const void* key,Id peer){auto& state=registry();std::lock_guard lock(state.mutex);const auto p=state.entries.find(key);if(p==state.entries.end())return 0;const auto& v=p->second.snapshot;for(const auto& r:v.rows)if(r.selected&&r.ready&&(r.endpoint==peer||(v.local!=v.host&&peer==v.host)))return r.local.p95;return 0;}
} // namespace dkr::relay
