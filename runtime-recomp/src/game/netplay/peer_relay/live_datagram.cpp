#include "live_datagram.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#if defined(DKR_LIVE_DATAGRAM_ICE)
#include <juice/juice.h>
#endif

namespace dkr::relay {
namespace {
constexpr std::array<std::uint8_t,6> Magic{'D','K','R','X',1,6};
constexpr std::size_t MaximumPacket=1200,MaximumDescription=4095;
void put(Bytes& b,Id v){for(unsigned i=0;i<8;++i)b.push_back(std::uint8_t(v>>(i*8)));}
Id get(std::span<const std::uint8_t> b,unsigned at){Id v=0;for(unsigned i=0;i<8;++i)v|=Id(b[at+i])<<(i*8);return v;}
struct Description {std::string credentials;std::set<std::string> candidates;bool done=false;};
std::optional<Description> description(std::string_view text) {
    if(text.empty()||text.size()>MaximumDescription||text.find('\0')!=text.npos)return {};
    Description result;bool ufrag=false,password=false;
    while(!text.empty()) {
        const auto end=text.find('\n');auto line=text.substr(0,end);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(line.starts_with("a=ice-ufrag:")||line.starts_with("a=ice-pwd:")) {
            if(line.size()>160)return {};
            ufrag|=line.starts_with("a=ice-ufrag:")&&line.size()>12;
            password|=line.starts_with("a=ice-pwd:")&&line.size()>10;
            result.credentials+=std::string(line)+'\n';
        } else if(line.starts_with("a=candidate:")) {
            if(line.size()>256||result.candidates.size()>=32)return {};
            result.candidates.emplace(line);
        } else if(line=="a=end-of-candidates")result.done=true;
        if(end==text.npos)break;text.remove_prefix(end+1);
    }
    if(!ufrag||!password)return {};return result;
}
bool envelope(std::span<const std::uint8_t> b,Id sender,Id room) {
    Id claimed=0,match=0;
    return b.size()>=44&&b.size()<=MaximumPacket&&secure::inspect_packet(b,claimed,match)&&claimed==sender&&match==room;
}
}
struct LiveDatagram::Impl {
    Context context;std::weak_ptr<dkr::runtime::netplay::TransportReceiveSignal> wake;
#if defined(DKR_LIVE_DATAGRAM_ICE)
    struct Packet {Bytes bytes;unsigned lane;};
    struct Inbox {
        std::mutex mutex;std::deque<Packet> queue;std::size_t bytes=0;
        Id source=0,room=0;Clock::time_point received{};
        std::weak_ptr<dkr::runtime::netplay::TransportReceiveSignal> wake;
    };
    struct Peer {
        Inbox inbox;juice_agent_t* agent=nullptr;
        Clock::time_point created=Clock::now(),advertise{},budget_at=created;
        std::size_t tokens=4096;std::string remote;Description parsed;
        bool announced=false;
        ~Peer(){if(agent)juice_destroy(agent);}
    };
    std::map<Id,std::unique_ptr<Peer>> peers;
    static void received(juice_agent_t*,const char* data,std::size_t size,void* user) {
        auto& box=*static_cast<Inbox*>(user);
        if(size<45||size>MaximumPacket+1)return;
        const auto lane=static_cast<unsigned char>(data[0]);if(lane!=2&&lane!=3)return;
        const auto sealed=std::span(reinterpret_cast<const std::uint8_t*>(data+1),size-1);
        if(!envelope(sealed,box.source,box.room))return;
        std::shared_ptr<dkr::runtime::netplay::TransportReceiveSignal> wake;
        {std::lock_guard lock(box.mutex);
            if(box.queue.size()>=128||size>128*1024-box.bytes)return;
            box.received=Clock::now();box.bytes+=sealed.size();
            box.queue.push_back({Bytes(sealed.begin(),sealed.end()),lane});wake=box.wake.lock();}
        if(wake)wake->notify();
    }
    bool connected(const Peer& p)const {if(!p.agent)return false;const auto state=juice_get_state(p.agent);return state==JUICE_STATE_CONNECTED||state==JUICE_STATE_COMPLETED;}
    void open(Id id,const std::vector<std::string>& servers) {
        if(peers.contains(id)||peers.size()>=MaxLobbyMembers)return;
        auto peer=std::make_unique<Peer>();peer->inbox.source=id;peer->inbox.room=context.room;peer->inbox.wake=wake;
        juice_config_t config{};config.cb_recv=received;config.user_ptr=&peer->inbox;
        // The same optional STUN service as the WebRTC fallback. Failure to
        // establish this extra route never prevents an ordinary connection.
        std::string host;unsigned port=3478;
        for(auto server:servers)if(server.starts_with("stun:")) {
            server.erase(0,5);if(server.starts_with("//"))server.erase(0,2);
            const auto colon=server.rfind(':');
            if(colon!=server.npos&&server.find(':')==colon) {
                const auto number=server.substr(colon+1);
                if(!number.empty()&&number.size()<=5&&std::all_of(number.begin(),number.end(),[](char c){return c>='0'&&c<='9';})) {
                    port=unsigned(std::stoul(number));server.resize(colon);
                }
            }
            host=std::move(server);break;
        }
        if(!host.empty()&&port&&port<=65535){config.stun_server_host=host.c_str();config.stun_server_port=std::uint16_t(port);}
        peer->agent=juice_create(&config);
        if(peer->agent&&juice_gather_candidates(peer->agent)!=JUICE_ERR_SUCCESS){juice_destroy(peer->agent);peer->agent=nullptr;}
        peers.emplace(id,std::move(peer));
    }
#endif
};
LiveDatagram::LiveDatagram():impl_(std::make_unique<Impl>()){}
LiveDatagram::~LiveDatagram()=default;
void LiveDatagram::reset(){
#if defined(DKR_LIVE_DATAGRAM_ICE)
    impl_->peers.clear();
#endif
    impl_->context={};
}
void LiveDatagram::signal(std::shared_ptr<dkr::runtime::netplay::TransportReceiveSignal> signal) {
    impl_->wake=signal;
#if defined(DKR_LIVE_DATAGRAM_ICE)
    for(auto& [id,p]:impl_->peers){std::lock_guard lock(p->inbox.mutex);p->inbox.wake=signal;}
#endif
}
void LiveDatagram::configure(const Context& context,const std::vector<std::string>& servers) {
    auto& s=*impl_;
    if(!context.active||!context.quick||!context.room||!context.local||!context.host){reset();return;}
    if(s.context.room!=context.room||s.context.local!=context.local||s.context.host!=context.host)reset();
    s.context=context;
#if defined(DKR_LIVE_DATAGRAM_ICE)
    std::set<Id> wanted;
    for(const auto& member:context.members)if(member.id&&member.id!=context.local&&
        (context.local==context.host||member.id==context.host))wanted.insert(member.id);
    std::erase_if(s.peers,[&](const auto& p){return !wanted.contains(p.first);});
    for(const auto id:wanted)s.open(id,servers);
#else
    (void)servers;
#endif
}
bool LiveDatagram::control(Id source,std::span<const std::uint8_t> bytes) {
    if(bytes.size()<30||bytes.size()>30+MaximumDescription||!std::equal(Magic.begin(),Magic.end(),bytes.begin()))return false;
    auto& s=*impl_;
    if(!s.context.active||get(bytes,6)!=s.context.room||get(bytes,14)!=source||get(bytes,22)!=s.context.local)return false;
#if defined(DKR_LIVE_DATAGRAM_ICE)
    const auto at=s.peers.find(source);if(at==s.peers.end()||!at->second->agent)return false;
    auto& peer=*at->second;
    const std::string text(reinterpret_cast<const char*>(bytes.data()+30),bytes.size()-30);
    const auto parsed=description(text);if(!parsed)return false;
    if(peer.remote.empty()) {
        if(juice_set_remote_description(peer.agent,text.c_str())!=JUICE_ERR_SUCCESS)return false;
        peer.remote=text;peer.parsed=*parsed;
    } else {
        if(parsed->credentials!=peer.parsed.credentials)return false;
        for(const auto& candidate:parsed->candidates)if(!peer.parsed.candidates.contains(candidate)) {
            if(juice_add_remote_candidate(peer.agent,candidate.c_str())!=JUICE_ERR_SUCCESS)return false;
            peer.parsed.candidates.insert(candidate);
        }
        if(parsed->done&&!peer.parsed.done){juice_set_remote_gathering_done(peer.agent);peer.parsed.done=true;}
    }
    return true;
#else
    return false;
#endif
}
std::vector<Control> LiveDatagram::controls() {
    std::vector<Control> out;
#if defined(DKR_LIVE_DATAGRAM_ICE)
    auto& s=*impl_;const auto now=Clock::now();
    for(auto& [id,p]:s.peers) {
        if(!p->agent||now<p->advertise||now-p->created>std::chrono::seconds(60))continue;
        const bool ready=s.connected(*p);
        if(ready&&p->announced)continue;
        p->advertise=now+std::chrono::milliseconds(500);
        char text[JUICE_MAX_SDP_STRING_LEN]{};
        if(juice_get_local_description(p->agent,text,sizeof(text))!=JUICE_ERR_SUCCESS||!description(text))continue;
        Bytes bytes(Magic.begin(),Magic.end());put(bytes,s.context.room);put(bytes,s.context.local);put(bytes,id);
        bytes.insert(bytes.end(),text,text+std::strlen(text));out.push_back({id,std::move(bytes)});
        if(ready){p->announced=true;std::fprintf(stderr,"[online][datagram] peer=%llu event=connected encrypted=1 sctp-bypass=live-and-repair\n",static_cast<unsigned long long>(id));}
    }
#endif
    return out;
}
bool LiveDatagram::send(Id destination,std::span<const std::uint8_t> sealed,unsigned lane) {
#if defined(DKR_LIVE_DATAGRAM_ICE)
    auto& s=*impl_;const auto at=s.peers.find(destination);
    if((lane!=2&&lane!=3)||at==s.peers.end()||!envelope(sealed,s.context.local,s.context.room)||!s.connected(*at->second))return false;
    auto& p=*at->second;const auto now=Clock::now();
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(now-p.budget_at).count();
    if(elapsed>0){p.tokens=std::min<std::size_t>(4096,p.tokens+std::size_t(std::min<std::int64_t>(elapsed,1000000))*32768/1000000);p.budget_at=now;}
    if(sealed.size()+1>p.tokens)return false;
    Bytes bytes{std::uint8_t(lane)};bytes.insert(bytes.end(),sealed.begin(),sealed.end());
    if(juice_send(p.agent,reinterpret_cast<const char*>(bytes.data()),bytes.size())!=JUICE_ERR_SUCCESS)return false;
    p.tokens-=bytes.size();
    // Duplicate via the existing path until this route carries traffic, and
    // again if it falls silent. Inputs/receipts remain independently retried.
    std::lock_guard lock(p.inbox.mutex);
    return p.inbox.received.time_since_epoch().count()&&now-p.inbox.received<std::chrono::milliseconds(750);
#else
    (void)destination;(void)sealed;(void)lane;return false;
#endif
}
bool LiveDatagram::receive(Id& source,Bytes& sealed,unsigned& lane) {
#if defined(DKR_LIVE_DATAGRAM_ICE)
    for(auto& [id,p]:impl_->peers){std::lock_guard lock(p->inbox.mutex);if(p->inbox.queue.empty())continue;
        auto packet=std::move(p->inbox.queue.front());p->inbox.queue.pop_front();p->inbox.bytes-=packet.bytes.size();
        source=id;sealed=std::move(packet.bytes);lane=packet.lane;return true;}
#else
    (void)source;(void)sealed;(void)lane;
#endif
    return false;
}
void LiveDatagram::discard(unsigned lane) {
#if defined(DKR_LIVE_DATAGRAM_ICE)
    for(auto& [id,p]:impl_->peers){std::lock_guard lock(p->inbox.mutex);
        std::erase_if(p->inbox.queue,[&](const auto& packet){if(packet.lane!=lane)return false;p->inbox.bytes-=packet.bytes.size();return true;});}
#else
    (void)lane;
#endif
}
}
