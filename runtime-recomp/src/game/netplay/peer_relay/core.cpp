#include "core.hpp"
#include "monocypher.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace dkr::relay {
namespace {
void put(Bytes& b,Id n,unsigned width=8){for(unsigned i=0;i<width;++i)b.push_back(std::uint8_t(n>>(i*8)));}
void text(Bytes& b,std::string_view s){put(b,s.size(),2);b.insert(b.end(),s.begin(),s.end());}
struct Read {
    std::span<const std::uint8_t> b;std::size_t at=0;bool ok=true;
    Id n(unsigned w=8){if(w>b.size()-at){ok=false;return 0;}Id v=0;for(unsigned i=0;i<w;++i)v|=Id(b[at++])<<(i*8);return v;}
    std::string text(unsigned limit){auto z=n(2);if(!ok||z>limit||z>b.size()-at){ok=false;return {};}
        std::string s(reinterpret_cast<const char*>(b.data()+at),std::size_t(z));at+=std::size_t(z);return s;}
    bool end()const{return ok&&at==b.size();}
};
Id random_id(){const auto key=secure::generate_key();Id n=0;std::memcpy(&n,key.data(),8);return n?n:1;}
bool clean(std::string_view s,unsigned limit){return s.size()<=limit&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
constexpr std::array<std::uint8_t,6> ControlMagic{'D','K','R','X',1,3};
constexpr std::array<std::uint8_t,5> WireMagic{'D','K','R','P',1};
enum Type {Policy=1,Hello,Permission,Ack,Start,Signal,Metrics,Select,Selected,Revoke,Receipt,LobbyStatus};
void summary(Bytes& b,const Summary& s){for(auto n:{s.samples,s.p50,s.p95,s.p99,s.lost})put(b,n,2);}
Summary summary(Read& r){Summary s;unsigned* p[]{&s.samples,&s.p50,&s.p95,&s.p99,&s.lost};for(auto v:p)*v=unsigned(r.n(2));return s;}
bool valid(const Summary& s){return s.samples<=64&&s.lost<=s.samples&&s.p50<=s.p95&&s.p95<=s.p99&&s.p99<=10000;}
unsigned score(const Summary& s){return s.p95+(s.samples?s.lost*1500/s.samples:1500);}
bool eligible(const Summary& s){return s.samples>=s.lost+8&&s.p95<=600&&s.lost*10<=s.samples;}
Bytes proof(const Grant& g,unsigned kind,Id origin,Id destination,unsigned lane,Id token){
    Bytes message;put(message,g.room);put(message,g.id);put(message,kind,1);put(message,origin);put(message,destination);put(message,lane,1);put(message,token);
    Bytes mac(16);crypto_blake2b_keyed(mac.data(),mac.size(),g.proof.data(),g.proof.size(),message.data(),message.size());return mac;
}
Bytes delivery_proof(const Grant& g,Id origin,Id destination,unsigned lane,Id token,std::span<const std::uint8_t> payload){
    auto context=proof(g,4,origin,destination,lane,token);std::array<std::uint8_t,32> hash{};
    crypto_blake2b(hash.data(),hash.size(),payload.data(),payload.size());context.insert(context.end(),hash.begin(),hash.end());
    Bytes mac(16);crypto_blake2b_keyed(mac.data(),mac.size(),g.proof.data(),g.proof.size(),context.data(),context.size());return mac;
}
}
void Samples::add(unsigned ms,bool lost,Clock::time_point now){values_.push_back({std::min(ms,10000U),lost,now});while(values_.size()>64)values_.pop_front();}
Summary Samples::summary(Clock::time_point now)const {
    std::vector<unsigned> ms;Summary out;for(const auto& v:values_)if(now-v.at<=std::chrono::seconds(32)){++out.samples;out.lost+=v.lost;if(!v.lost)ms.push_back(v.ms);}
    if(ms.empty())return out;std::sort(ms.begin(),ms.end());
    auto percentile=[&](unsigned p){return ms[std::min(ms.size()-1,(ms.size()*p+99)/100-1)];};
    out.p50=percentile(50);out.p95=percentile(95);out.p99=percentile(99);return out;
}
bool Samples::fresh(Clock::time_point now)const{
    unsigned misses=0;
    for(auto i=values_.rbegin();i!=values_.rend();++i){if(now-i->at>=std::chrono::seconds(3))return false;if(!i->lost)return misses<3;++misses;}
    return false;
}
bool Engine::Replay::accept(Id value) {
    if(!value)return false;
    if(value>top){const auto shift=value-top;if(shift>=512)bits={};else for(Id i=512;i-->0;){const bool v=i>=shift&&((bits[(i-shift)/64]>>((i-shift)%64))&1);auto& word=bits[i/64];const auto bit=Id(1)<<(i%64);word=v?word|bit:word&~bit;}top=value;}
    const auto offset=top-value;if(offset>=512)return false;auto& word=bits[offset/64];const auto bit=Id(1)<<(offset%64);if(word&bit)return false;word|=bit;return true;
}
Engine::~Engine(){reset();}
void Engine::recover(Clock::time_point now,Id peer){
    if(!context_.active||!peer||!admitted(peer)||peer==context_.local)return;
    const auto endpoint=context_.local==context_.host?peer:context_.local;
    if(context_.local!=context_.host&&peer!=context_.host)return;
    if(now<recovery_at_[endpoint])return;
    recovery_at_[endpoint]=now+std::chrono::seconds(5);
    for(auto& [id,r]:routes_)if(r.grant.proxy&&r.grant.endpoint==endpoint)r.blocked_until=now+std::chrono::seconds(20);
    const auto key=context_.local==context_.host?endpoint:context_.host;
    if(auto at=choices_.find(key);at!=choices_.end()) {
        auto& c=at->second;if(c.active||c.wanted){c.active=c.wanted=0;c.changed=now;c.sent={};++c.serial;c.announce=context_.local==context_.host;}
    }
}
void Engine::reset(){for(auto& [id,r]:routes_){if(io_.close)io_.close(id);crypto_wipe(r.grant.key.data(),32);crypto_wipe(r.grant.proof.data(),32);}routes_.clear();choices_.clear();capabilities_.clear();controls_.clear();pending_controls_.clear();control_replay_.clear();for(auto& q:queued_)q.clear();queued_bytes_=0;context_={};policy_=false;hello_=policy_at_=choose_at_={};recovery_at_.clear();serial_=signal_serial_=control_serial_=0;tokens_at_={};tokens_=256*1024;forwarded_=dropped_=0;lobby_.clear();lobby_payload_.clear();lobby_serial_=lobby_generation_=0;lobby_heard_=lobby_at_=lobby_heartbeat_={};}
const Member* Engine::member(Id id)const{for(const auto& m:context_.members)if(m.id==id)return &m;return nullptr;}
bool Engine::admitted(Id id)const{return id==context_.host||id==context_.local||member(id)!=nullptr;}
const Grant* Engine::grant(Id id)const{auto r=routes_.find(id);return r==routes_.end()?nullptr:&r->second.grant;}
void Engine::configure(Context next,Clock::time_point now) {
    if(next.members.size()>MaxLobbyMembers)next.members.resize(MaxLobbyMembers);
    if(!next.active||!next.room||!next.host||!next.local)next={};
    const bool changed=next.room!=context_.room||next.host!=context_.host||next.local!=context_.local;
    if(changed)reset();
    if(!changed&&context_.local==context_.host)next.allow=context_.allow; // Room policy is frozen until rehost.
    context_=std::move(next);if(!context_.active)return;
    std::erase_if(recovery_at_,[&](const auto& item){return !admitted(item.first);});
    if(context_.local==context_.host)policy_=context_.allow;
    std::vector<Id> retired;
    for(const auto& [id,r]:routes_)if(context_.local==context_.host){const auto* endpoint=member(r.grant.endpoint);const auto* proxy=member(r.grant.proxy);
        if(!endpoint||!endpoint->racer||(r.grant.proxy&&(!proxy||!proxy->racer)))retired.push_back(id);}
    for(auto id:retired)erase(id,context_.local==context_.host);
    (void)now;
}
bool Engine::emit(Id target,unsigned type,const std::function<void(Bytes&)>& body) {
    if(controls_.size()>=512||pending_controls_.size()>=512||control_serial_==UINT64_MAX)return false;Bytes b(ControlMagic.begin(),ControlMagic.end());put(b,type,1);put(b,context_.room);const auto seq=++control_serial_;put(b,seq);if(body)body(b);
    if(b.size()<=900){Control p{target,std::move(b)};if(type!=Hello&&type!=Metrics&&type!=Policy&&type!=LobbyStatus)pending_controls_[seq]={p,{},{}};controls_.push_back(std::move(p));return true;}return false;
}
void Engine::erase(Id id,bool notify) {
    auto r=routes_.find(id);if(r==routes_.end())return;
    if(notify)for(auto peer:{r->second.grant.endpoint,r->second.grant.proxy})if(peer)emit(peer,Revoke,[&](auto& b){put(b,id);});
    auto stale=[&](const Control& c){if(c.bytes.size()<31||c.bytes[6]<Permission||c.bytes[6]==Revoke||c.bytes[6]==Receipt||c.bytes[6]==LobbyStatus)return false;Read input{c.bytes,23};return input.n()==id;};
    std::erase_if(controls_,stale);std::erase_if(pending_controls_,[&](const auto& c){return stale(c.second.packet);});
    if(io_.close)io_.close(id);crypto_wipe(r->second.grant.key.data(),32);crypto_wipe(r->second.grant.proof.data(),32);routes_.erase(r);
    for(auto& queue:queued_)std::erase_if(queue,[&](const auto& q){if(q.route!=id)return false;queued_bytes_-=q.bytes.size();return true;});
    for(auto& [peer,c]:choices_)if(c.active==id||c.wanted==id)c={};
}
void Engine::make(Id endpoint,Id proxy,Clock::time_point now) {
    if(routes_.size()>=MaxRoutes)return;const auto* a=member(endpoint);const auto* p=member(proxy);if(!a||(proxy&&!p))return;
    Grant g{random_id(),context_.room,context_.host,endpoint,proxy,secure::generate_key(),secure::generate_key(),a->address,p?p->address:std::string{},a->name,p?p->name:std::string{}};
    Route route;route.grant=g;route.granted=now;route.acknowledgements.insert(context_.host);routes_.emplace(g.id,std::move(route));
    permission(g);
}
void Engine::permission(const Grant& g) {
    auto write=[&](Bytes& b,bool private_proof){put(b,g.id);put(b,g.host);put(b,g.endpoint);put(b,g.proxy);b.insert(b.end(),g.key.begin(),g.key.end());const secure::Key hidden{};const auto& measurement=private_proof?g.proof:hidden;b.insert(b.end(),measurement.begin(),measurement.end());text(b,g.endpoint_address);text(b,g.proxy_address);text(b,g.name);text(b,g.proxy_name);};
    emit(g.endpoint,Permission,[&](auto& b){write(b,true);});if(g.proxy)emit(g.proxy,Permission,[&](auto& b){write(b,false);});
}
void Engine::service(Clock::time_point now) {
    if(!context_.active)return;
    for(auto it=pending_controls_.begin();it!=pending_controls_.end();) {
        auto& p=it->second;if(!p.created.time_since_epoch().count()){p.created=now;p.sent=now+std::chrono::milliseconds(300);++it;continue;}
        if(now-p.created>std::chrono::seconds(20)){it=pending_controls_.erase(it);continue;}
        if(now>=p.sent&&controls_.size()<512){controls_.push_back(p.packet);p.sent=now+std::chrono::milliseconds(300);}++it;
    }
    if(context_.local==context_.host) {
        if(policy_&&now>=policy_at_){for(const auto& m:context_.members)if(m.id!=context_.host&&m.racer)emit(m.id,Policy,[](auto& b){put(b,1,1);});policy_at_=now+std::chrono::seconds(1);}
        if(policy_)for(const auto& a:context_.members)if(a.id!=context_.host&&a.racer&&capabilities_.contains(a.id)&&now-capabilities_[a.id].heard<std::chrono::seconds(4)) {
            for(Id p: [&]{std::vector<Id> peers{0};for(const auto& m:context_.members)if(m.id!=a.id&&m.id!=context_.host&&m.racer&&capabilities_.contains(m.id)&&capabilities_[m.id].volunteer&&now-capabilities_[m.id].heard<std::chrono::seconds(4))peers.push_back(m.id);return peers;}()) {
                const bool exists=std::any_of(routes_.begin(),routes_.end(),[&](const auto& pair){return pair.second.grant.endpoint==a.id&&pair.second.grant.proxy==p;});if(!exists)make(a.id,p,now);
            }
        }
    } else if(policy_&&(now>=hello_||last_volunteer_!=context_.volunteer)) {
        last_volunteer_=context_.volunteer;emit(context_.host,Hello,[&](auto& b){put(b,context_.volunteer,1);});hello_=now+std::chrono::seconds(1);
    }
    std::vector<Id> expired;
    for(auto& [id,r]:routes_) {
        if(context_.local==context_.host) {
            const auto peer=capabilities_.find(r.grant.endpoint),proxy=capabilities_.find(r.grant.proxy);
            if(peer==capabilities_.end()||now-peer->second.heard>std::chrono::seconds(8)||
                (r.grant.proxy&&(proxy==capabilities_.end()||!proxy->second.volunteer||now-proxy->second.heard>std::chrono::seconds(8)))) {expired.push_back(id);continue;}
            if(now-r.granted>=std::chrono::seconds(20)) {
                if(r.grant.proxy&&!r.local.fresh(now)){expired.push_back(id);continue;}
                permission(r.grant);if(r.started)for(auto p:{r.grant.endpoint,r.grant.proxy})if(p)emit(p,Start,[&](auto& b){put(b,id);});r.granted=now;
            }
        }else if(now-r.granted>std::chrono::seconds(60)||(context_.local==r.grant.proxy&&!context_.volunteer))expired.push_back(id);
        std::erase_if(r.signals,[&](const auto& a){return now-a.second.at>std::chrono::seconds(20);});
    }
    for(auto id:expired)erase(id,context_.local==context_.host);
    const auto elapsed=tokens_at_.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(now-tokens_at_).count():0;
    if(elapsed>0)tokens_=std::min<std::size_t>(256*1024,tokens_+std::size_t(elapsed)*1024);tokens_at_=now;
    // Independent bounded turns: a blocked repair lane cannot consume the
    // realtime deadline, and repair still receives a guaranteed share.
    for(const unsigned lane:{0U,2U,1U,3U,4U}){
        auto& queue=queued_[lane];const auto turns=std::min<std::size_t>(queue.size(),lane==2?32:lane<=1?16:4);
        for(std::size_t n=0;n<turns;++n){auto q=std::move(queue.front());queue.pop_front();queued_bytes_-=q.bytes.size();auto r=routes_.find(q.route);
            if(r==routes_.end()||now>=q.expires){++dropped_;continue;}
            if(!transmit(r->second,q.to,q.bytes,q.lane,now,false)){queued_bytes_+=q.bytes.size();queue.push_back(std::move(q));}}
    }
    for(auto& [id,r]:routes_) {
        const auto& g=r.grant;
        if(context_.local==context_.host&&!r.started&&r.acknowledgements.contains(g.endpoint)&&(!g.proxy||r.acknowledgements.contains(g.proxy))) {
            r.started=true;for(auto peer:{g.endpoint,g.proxy})if(peer)emit(peer,Start,[&](auto& b){put(b,id);});
        }
        if(!r.started)continue;
        if(context_.local==g.proxy&&now>=r.ping){packet(r,g.endpoint,3,g.proxy,g.endpoint,0,0,{},0,now);r.ping=now+std::chrono::milliseconds(500);}
        for(auto it=r.pending.begin();it!=r.pending.end();)if(now-it->second>=std::chrono::milliseconds(2500)){r.local.add(2500,true,now);it=r.pending.erase(it);}else ++it;
        if(context_.local==g.host||context_.local==g.endpoint) {
            if(now>=r.ping&&r.pending.size()<8&&(!io_.ready||io_.ready(g))) {
                const auto token=++serial_;r.pending[token]=now;packet(r,g.proxy?g.proxy:(context_.local==g.host?g.endpoint:g.host),1,context_.local,context_.local==g.host?g.endpoint:g.host,1,token,proof(g,1,context_.local,context_.local==g.host?g.endpoint:g.host,1,token),0,now);r.ping=now+std::chrono::milliseconds(500);
            }
            if(!delivering(r,now)){
                r.blocked_until=now+std::chrono::seconds(10);
                for(auto& d:r.delivery){d.pending=d.blocked=d.attempted={};d.proofs.clear();}
            }
            if(context_.local==g.endpoint&&now>=r.report){const auto s=r.local.summary(now);emit(g.host,Metrics,[&](auto& b){put(b,id);summary(b,s);put(b,now>=r.blocked_until&&delivering(r,now),1);});r.report=now+std::chrono::seconds(1);}
        }
    }
    for(auto& [peer,c]:choices_)if(c.active){auto p=routes_.find(c.active);if(p==routes_.end()||!p->second.local.fresh(now)||!delivering(p->second,now)||now<p->second.blocked_until||!io_.ready||!io_.ready(p->second.grant)){c.active=0;c.changed=now;if(context_.local==context_.host){c.wanted=0;++c.serial;c.announce=true;c.sent={};}}}
    if(context_.local==context_.host&&policy_&&now>=choose_at_){choose(now);choose_at_=now+std::chrono::milliseconds(250);}
    if(context_.local==context_.host)publish_lobby(now);
}
bool Engine::control(Id source,std::span<const std::uint8_t> bytes,Clock::time_point now) {
    if(bytes.size()<23||bytes.size()>900||!context_.active||!std::equal(ControlMagic.begin(),ControlMagic.end(),bytes.begin()))return false;
    Read r{bytes,6};const auto type=r.n(1),room=r.n(),serial=r.n();
    if(room!=context_.room||(context_.local==context_.host?(!policy_||source==context_.host||!admitted(source)):source!=context_.host))return false;
    if(type==Receipt||type==LobbyStatus)return handle(source,bytes,now);
    if(type<Policy||type>Revoke||!serial)return false;
    auto replay=control_replay_[source];const bool fresh=replay.accept(serial);
    if(fresh&&!handle(source,bytes,now))return false;
    if(fresh)control_replay_[source]=replay;
    if(type!=Hello&&type!=Metrics&&type!=Policy&&controls_.size()<512){Bytes b(ControlMagic.begin(),ControlMagic.end());put(b,Receipt,1);put(b,context_.room);put(b,0);put(b,serial);controls_.push_back({source,std::move(b)});}
    return true;
}
bool Engine::handle(Id source,std::span<const std::uint8_t> bytes,Clock::time_point now) {
    if(!context_.active||bytes.size()<23||bytes.size()>900||!std::equal(ControlMagic.begin(),ControlMagic.end(),bytes.begin()))return false;
    Read r{bytes,6};const auto type=unsigned(r.n(1));if(r.n()!=context_.room)return false;const auto serial=r.n();
    const bool host=context_.local==context_.host;if(host){if(!admitted(source)||source==context_.host||!policy_)return false;}else if(source!=context_.host)return false;
    if(type==LobbyStatus&&!host){
        if(!serial||serial<=lobby_serial_)return false;
        const auto count=r.n(1);if(!count||count>MaxLobbyMembers)return false;
        std::vector<LobbyRoute> rows;std::set<Id> ids;
        for(Id i=0;i<count;++i){LobbyRoute row;row.endpoint=r.n();row.proxy=r.n();row.name=r.text(24);row.via=r.text(24);
            if(!r.ok||!row.endpoint||!ids.insert(row.endpoint).second||row.name.empty()||!clean(row.name,24)||!clean(row.via,24)||
                row.proxy==row.endpoint||(row.proxy&&(row.proxy==context_.host||row.via.empty()))||(!row.proxy&&!row.via.empty()))return false;
            rows.push_back(std::move(row));}
        Id generation=0;
        // Optional display-only slot map. Classic LobbyState intentionally
        // omits peer IDs; never guess a player's identity from their name.
        if(r.at<r.b.size()) {
            if(r.n(1)!=0x53)return false;generation=r.n();std::set<unsigned> slots;
            for(auto& row:rows){row.slot=unsigned(r.n(1));if(row.slot!=255&&(row.slot>=4||!slots.insert(row.slot).second))return false;}
        }
        // Optional presentation-only RTTs, in the same authenticated row order.
        // Zero means unknown, never a measured zero-millisecond connection.
        if(r.at<r.b.size()) {
            if(r.n(1)!=0x50)return false;
            for(auto& row:rows){row.ping_ms=unsigned(r.n(2));if(row.ping_ms>=5000||(row.endpoint==context_.host&&row.ping_ms))return false;}
        }
        if(!r.end()||!ids.contains(context_.host)||!ids.contains(context_.local))return false;
        for(const auto& row:rows)if((row.endpoint==context_.host&&row.proxy)||(row.proxy&&!ids.contains(row.proxy)))return false;
        // Display metadata only. Never installs a route, key or gameplay state.
        lobby_=std::move(rows);lobby_serial_=serial;lobby_generation_=generation;lobby_heard_=now;return true;
    }
    if(type==Receipt){const auto ack=r.n();if(!r.end()||serial)return false;const auto p=pending_controls_.find(ack);if(p!=pending_controls_.end()&&p->second.packet.target==source)pending_controls_.erase(p);return true;}
    if(type==Policy&&!host){const auto value=r.n(1);if(!r.end()||value!=1)return false;policy_=true;return true;}
    if(!policy_)return false;
    if(type==Hello&&host){const auto volunteer=r.n(1);if(!r.end()||volunteer>1)return false;capabilities_[source]={volunteer!=0,now};
        if(!volunteer){std::vector<Id> retire;for(auto& [id,p]:routes_)if(p.grant.proxy==source)retire.push_back(id);for(auto id:retire)erase(id,true);}return true;}
    if(type==Permission&&!host) {
        Grant g;g.room=context_.room;g.id=r.n();g.host=r.n();g.endpoint=r.n();g.proxy=r.n();
        if(r.b.size()-r.at<64)return false;std::copy_n(r.b.begin()+std::ptrdiff_t(r.at),32,g.key.begin());r.at+=32;std::copy_n(r.b.begin()+std::ptrdiff_t(r.at),32,g.proof.begin());r.at+=32;
        g.endpoint_address=r.text(96);g.proxy_address=r.text(96);g.name=r.text(24);g.proxy_name=r.text(24);
        if(!r.end()||!g.id||g.host!=context_.host||!g.endpoint||g.endpoint==g.host||g.proxy==g.host||g.proxy==g.endpoint||
            (context_.local!=g.endpoint&&context_.local!=g.proxy)||(context_.local==g.proxy&&!context_.volunteer)||
            (context_.local==g.proxy&&g.proof!=secure::Key{})||(context_.local==g.endpoint&&g.proof==secure::Key{})||
            !clean(g.name,24)||!clean(g.proxy_name,24)||!clean(g.endpoint_address,96)||!clean(g.proxy_address,96))return false;
        if(const auto old=routes_.find(g.id);old!=routes_.end()){if(old->second.grant.key!=g.key||old->second.grant.proof!=g.proof||old->second.grant.endpoint!=g.endpoint||old->second.grant.proxy!=g.proxy)return false;old->second.granted=now;}
        else {if(routes_.size()>=MaxRoutes)return false;Route route;route.grant=g;route.granted=now;routes_.emplace(g.id,std::move(route));}
        emit(g.host,Ack,[&](auto& b){put(b,g.id);});return true;
    }
    const auto id=r.n();auto found=routes_.find(id);if(!r.ok||found==routes_.end())return false;auto& route=found->second;auto& g=route.grant;
    if(type==Ack&&host){if(!r.end()||(source!=g.endpoint&&source!=g.proxy))return false;route.acknowledgements.insert(source);return true;}
    if(type==Start&&!host){if(!r.end())return false;route.started=true;if(g.proxy&&!route.opening){route.opening=true;if(io_.open)io_.open(g,context_.local==g.endpoint);}return true;}
    if(type==Signal) {
        const auto seq=r.n();const auto index=unsigned(r.n(1)),count=unsigned(r.n(1));const auto part=r.text(700);
        if(!r.end()||!seq||!count||count>(MaxSignal+699)/700||index>=count)return false;
        if(host){if(!g.proxy||(source!=g.endpoint&&source!=g.proxy))return false;return emit(source==g.endpoint?g.proxy:g.endpoint,Signal,[&](auto& b){put(b,id);put(b,seq);put(b,index,1);put(b,count,1);text(b,part);});}
        if(!g.proxy||!route.started)return false;
        auto completed=route.completed_signals;if(!completed.accept(seq))return true;
        if(!route.signals.contains(seq)&&route.signals.size()>=16)return false;
        auto& assembly=route.signals[seq];if(assembly.count&&assembly.count!=count)return false;
        assembly.count=count;assembly.at=now;assembly.pieces[index]=part;
        if(assembly.pieces.size()==count){std::string value;for(unsigned i=0;i<count;++i)value+=assembly.pieces[i];route.signals.erase(seq);
            if(value.size()>MaxSignal)return false;route.completed_signals=completed;if(io_.signal)io_.signal(id,std::move(value));}return true;
    }
    if(type==Metrics&&host){const auto s=summary(r);const auto healthy=r.n(1);if(!r.end()||healthy>1||source!=g.endpoint||!valid(s))return false;route.remote=s;route.remote_delivering=healthy;route.remote_at=now;return true;}
    if(type==Select&&!host){const auto sequence=r.n();const auto selected=r.n(1);if(!r.end()||g.endpoint!=context_.local||selected>1||
        (selected&&(!eligible(route.local.summary(now))||!route.local.fresh(now)||!delivering(route,now)||now<route.blocked_until||!io_.ready||!io_.ready(g))))return false;
        auto& choice=choices_[g.host];if(sequence<choice.serial)return false;choice.serial=sequence;choice.active=selected?id:0;
        emit(g.host,Selected,[&](auto& b){put(b,id);put(b,sequence);put(b,selected,1);});return true;}
    if(type==Selected&&host){const auto seq=r.n();const auto selected=r.n(1);if(!r.end()||source!=g.endpoint||selected>1)return false;auto& c=choices_[source];
        if(seq!=c.serial||c.wanted!=(selected?id:0)||(selected&&(!route.remote_delivering||!delivering(route,now)||now<route.blocked_until)))return false;c.active=c.wanted;c.announce=false;c.changed=now;return true;}
    if(type==Revoke&&!host){if(!r.end())return false;erase(id,false);return true;}
    return false;
}
bool Engine::transmit(Route& r,Id to,std::span<const std::uint8_t> bytes,unsigned lane,Clock::time_point now,bool queue) {
    if(context_.local==r.grant.proxy&&!context_.volunteer)return false;
    if(bytes.size()>tokens_&&(lane==3||lane==4))return false;
    if(io_.send&&io_.send(r.grant,to,bytes,lane)){if(lane==3||lane==4)tokens_-=std::min(tokens_,bytes.size());return true;}
    if(!queue||lane>=queued_.size())return false;
    std::size_t count=0,route_count=0,route_bytes=0;
    for(const auto& q:queued_)for(const auto& entry:q){++count;if(entry.route==r.grant.id){++route_count;route_bytes+=entry.bytes.size();}}
    const std::size_t route_limit=lane==0?64:lane==2?56:48;
    const std::size_t limit=lane==0||lane==2?128*1024:lane==1?112*1024:96*1024;
    if(queue&&lane<queued_.size()&&count<(lane==0||lane==2?128U:lane==1?112U:96U)&&route_count<route_limit&&route_bytes+bytes.size()<=64*1024&&queued_bytes_+bytes.size()<=limit){queued_[lane].push_back({r.grant.id,to,lane,Bytes(bytes.begin(),bytes.end()),now+std::chrono::milliseconds(lane==2||lane==3?70:5000)});queued_bytes_+=bytes.size();return true;}return false;
}
bool Engine::packet(Route& r,Id to,unsigned kind,Id origin,Id destination,unsigned lane,Id token,std::span<const std::uint8_t> payload,unsigned hop,Clock::time_point now) {
    if(lane>4||payload.size()>MaxInner||serial_==UINT64_MAX)return false;
    Bytes plain;put(plain,context_.room);put(plain,kind,1);put(plain,origin);put(plain,destination);put(plain,lane,1);put(plain,token);put(plain,hop,1);put(plain,payload.size(),2);plain.insert(plain.end(),payload.begin(),payload.end());
    auto cipher=secure::seal(plain,r.grant.key,context_.local,++serial_,r.grant.id);Bytes wire(WireMagic.begin(),WireMagic.end());put(wire,r.grant.id);wire.insert(wire.end(),cipher.begin(),cipher.end());
    return transmit(r,to,wire,lane,now,context_.local==r.grant.proxy);
}
std::optional<Grant> Engine::grant_for_wire(std::span<const std::uint8_t> b)const {
    if(b.size()<57||b.size()>MaxWire||!std::equal(WireMagic.begin(),WireMagic.end(),b.begin()))return {};Read r{b,5};const auto* g=grant(r.n());return g?std::optional(*g):std::nullopt;
}
bool Engine::wire(Id id,Id physical,std::span<const std::uint8_t> bytes,Clock::time_point now) {
    const auto known=grant_for_wire(bytes);auto found=routes_.find(id);if(!known||known->id!=id||found==routes_.end()||!found->second.started)return false;
    auto& route=found->second;const auto& g=route.grant;Id sender=0,sequence=0;Bytes plain;
    if(!secure::open(bytes.subspan(13),g.key,id,sender,sequence,plain)||sender!=physical)return false;
    Read r{plain};const auto room=r.n(),kind=r.n(1),origin=r.n(),destination=r.n(),lane=r.n(1),token=r.n(),hop=r.n(1),size=r.n(2);
    if(!r.ok||room!=context_.room||kind>4||lane>4||hop>1||size!=plain.size()-r.at)return false;
    if(kind==3)return g.proxy&&sender==g.proxy&&origin==g.proxy&&destination==g.endpoint&&context_.local==g.endpoint&&!hop&&!size&&route.replay[{sender,0}].accept(sequence);
    if(!((origin==g.host&&destination==g.endpoint)||(origin==g.endpoint&&destination==g.host)))return false;
    const auto payload=std::span(plain).subspan(r.at);
    if(context_.local==g.proxy){if(hop||sender!=origin||!context_.volunteer)return false;
        if(!route.replay[{sender,unsigned(lane)}].accept(sequence))return false;
        const bool result=packet(route,destination,unsigned(kind),origin,destination,unsigned(lane),token,payload,1,now);if(result)forwarded_+=bytes.size();else ++dropped_;return result;
    }
    if(destination!=context_.local||sender!=(g.proxy?g.proxy:origin)||hop!=(g.proxy?1U:0U))return false;
    if(kind==1||kind==2){const auto mac=proof(g,unsigned(kind),origin,destination,unsigned(lane),token);if(size!=16||crypto_verify16(mac.data(),payload.data()))return false;}
    if(kind==4){const auto p=route.delivery[lane].proofs.find(token);if(size!=16||p==route.delivery[lane].proofs.end()||crypto_verify16(p->second.data(),payload.data()))return false;}
    if(!route.replay[{sender,unsigned(lane)}].accept(sequence))return false;
    if(kind==1){return packet(route,g.proxy?g.proxy:origin,2,destination,origin,unsigned(lane),token,proof(g,2,destination,origin,unsigned(lane),token),0,now);}
    if(kind==2){const auto p=route.pending.find(token);if(p==route.pending.end())return false;
        route.local.add(unsigned(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(now-p->second).count())),false,now);route.pending.erase(p);return true;}
    if(kind==4){auto& d=route.delivery[lane];if(!token||token>d.sent||token<=d.acknowledged)return false;
        d.acknowledged=token;d.received=now;d.pending={};d.proofs.erase(d.proofs.begin(),d.proofs.upper_bound(token));return true;}
    Id original=0,match=0;if(!secure::inspect_packet(payload,original,match)||original!=origin||match!=context_.room)return false;
    if(!io_.deliver||!io_.deliver(origin,Bytes(payload.begin(),payload.end()),unsigned(lane))){++dropped_;return false;}
    // An endpoint-only MAC proves application-lane delivery; the proxy cannot
    // manufacture it. This is health evidence, never an immutable-input ACK.
    auto& delivery=route.delivery[lane];
    if(token&&now>=delivery.receipt){packet(route,g.proxy?g.proxy:origin,4,destination,origin,unsigned(lane),token,delivery_proof(g,destination,origin,unsigned(lane),token,payload),0,now);delivery.receipt=now+std::chrono::milliseconds(100);}
    return true;
}
bool Engine::send(Id to,std::span<const std::uint8_t> bytes,unsigned lane,Clock::time_point now) {
    auto c=choices_.find(to);if(c==choices_.end()||!c->second.active)return false;
    auto r=routes_.find(c->second.active);if(r==routes_.end()||lane>4||!r->second.started||!r->second.local.fresh(now)||!delivering(r->second,now)||now<r->second.blocked_until||!io_.ready||!io_.ready(r->second.grant))return false;
    const auto& g=r->second.grant;auto& d=r->second.delivery[lane];
    // Receipts are limited to ten per second. Sample at that rate too, so
    // bursts cannot evict every outstanding proof before a long RTT returns.
    const auto token=now>=d.sample_after?serial_+1:0;
    if(now-d.attempted>std::chrono::milliseconds(500))d.blocked={};
    d.attempted=now;
    if(!packet(r->second,g.proxy,0,context_.local,to,lane,token,bytes,0,now)){
        if(!d.blocked.time_since_epoch().count())d.blocked=now;return false;
    }
    d.blocked={};if(!token)return true;
    d.sample_after=now+std::chrono::milliseconds(100);d.sent=token;d.proofs[token]=delivery_proof(g,to,context_.local,lane,token,bytes);
    while(d.proofs.size()>128)d.proofs.erase(d.proofs.begin());
    if(!d.pending.time_since_epoch().count())d.pending=now;return true;
}
bool Engine::delivering(const Route& route,Clock::time_point now)const{
    for(const auto& d:route.delivery)if(d.pending.time_since_epoch().count()&&now-std::max(d.pending,d.received)>std::chrono::milliseconds(1200))return false;
    for(const auto& d:route.delivery)if(d.blocked.time_since_epoch().count()&&now-d.attempted<=std::chrono::milliseconds(500)&&now-d.blocked>=std::chrono::milliseconds(1200))return false;
    return true;
}
void Engine::signal(Id route,std::string value) {
    if(!routes_.contains(route)||value.empty()||value.size()>MaxSignal)return;const auto count=unsigned((value.size()+699)/700);if(controls_.size()+count>512||pending_controls_.size()+count>512)return;const auto seq=++signal_serial_;
    for(unsigned i=0;i<count;++i)emit(context_.host,Signal,[&](auto& b){put(b,route);put(b,seq);put(b,i,1);put(b,count,1);text(b,std::string_view(value).substr(i*700,700));});
}
void Engine::choose(Clock::time_point now) {
    for(const auto& a:context_.members)if(a.id!=context_.host&&a.racer) {
        auto& choice=choices_[a.id];Route* direct=nullptr;Route* best=nullptr;unsigned best_score=UINT32_MAX;
        for(auto& [id,r]:routes_)if(r.grant.endpoint==a.id) {
            if(!r.grant.proxy)direct=&r;const auto local=r.local.summary(now);
            const bool fresh=r.started&&r.local.fresh(now)&&now-r.remote_at<std::chrono::seconds(3)&&eligible(local)&&eligible(r.remote)&&r.remote_delivering&&delivering(r,now)&&now>=r.blocked_until&&io_.ready&&io_.ready(r.grant);
            if(!fresh)continue;const auto value=std::max(score(local),score(r.remote));if(value<best_score){best=&r;best_score=value;}
        }
        Id target=0;
        if(best&&best->grant.proxy&&direct){const auto baseline=std::max(score(direct->local.summary(now)),score(direct->remote));
            if(!direct->local.fresh(now)||best_score+std::max(8U,baseline*15/100)<baseline)target=best->grant.id;}
        if(choice.active){auto active=routes_.find(choice.active);if(active==routes_.end()||!active->second.local.fresh(now)||!active->second.remote_delivering||!delivering(active->second,now)) {choice.active=choice.wanted=0;choice.changed=now;++choice.serial;choice.announce=true;choice.sent={};}}
        if(choice.candidate!=target){choice.candidate=target;choice.since=now;}
        if(target!=choice.wanted&&now-choice.since>=std::chrono::seconds(3)&&(!choice.changed.time_since_epoch().count()||now-choice.changed>=std::chrono::seconds(10))){choice.wanted=target;++choice.serial;choice.announce=true;choice.sent={};}
        if((choice.announce||choice.active!=choice.wanted)&&now>=choice.sent){const Id route=choice.wanted?choice.wanted:(direct?direct->grant.id:0);if(route)emit(a.id,Select,[&](auto& b){put(b,route);put(b,choice.serial);put(b,choice.wanted!=0,1);});choice.sent=now+std::chrono::seconds(1);}
    }
}
std::vector<Control> Engine::controls(){std::vector<Control> out;for(unsigned i=0;i<4&&!controls_.empty();++i){out.push_back(std::move(controls_.front()));controls_.pop_front();}return out;}
View Engine::view(Clock::time_point now)const {
    View out{policy_,context_.volunteer,{},forwarded_,dropped_,queued_bytes_,context_.local,context_.host};
    for(const auto& [id,r]:routes_)if(context_.local!=r.grant.proxy){const auto c=choices_.find(context_.local==context_.host?r.grant.endpoint:context_.host);
        std::uint64_t age=0;for(const auto& d:r.delivery)if(d.pending.time_since_epoch().count())age=std::max(age,std::uint64_t(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(now-std::max(d.pending,d.received)).count())));
        out.rows.push_back({r.grant.endpoint,id,r.grant.name,r.grant.proxy?r.grant.proxy_name:"Direct",io_.ice?io_.ice(id):std::string{},c!=choices_.end()&&c->second.active==id,r.started&&r.local.fresh(now)&&delivering(r,now)&&now>=r.blocked_until,r.local.summary(now),r.remote,age,!delivering(r,now)||now<r.blocked_until});
        auto& row=out.rows.back();row.reason=row.selected?"selected":now<r.blocked_until?"recovering this route":
            !r.started||!io_.ready||!io_.ready(r.grant)?"connecting":!delivering(r,now)?"delivery stalled":
            row.local.samples<row.local.lost+8||row.remote.samples<row.remote.lost+8?"measuring":
            !eligible(row.local)||!eligible(row.remote)?"outside gameplay quality budget":
            !r.local.fresh(now)||now-r.remote_at>=std::chrono::seconds(3)?"stale measurements":"available; comparing direct and hold-down";
    }
    if(context_.active){
        if(context_.local==context_.host){out.lobby=lobby_routes(now);out.lobby_generation=context_.generation;}
        else if(lobby_heard_.time_since_epoch().count()&&now-lobby_heard_<std::chrono::seconds(4)){out.lobby=lobby_;out.lobby_generation=lobby_generation_;}
    }
    return out;
}
std::vector<LobbyRoute> Engine::lobby_routes(Clock::time_point now)const {
    std::vector<LobbyRoute> rows;
    for(const auto& m:context_.members){LobbyRoute row{m.id,0,m.name,{},m.slot,m.id==context_.host?0:std::min(m.ping_ms,4999U)};
        if(m.id!=context_.host&&usable(m.id,now)){const auto c=choices_.find(m.id);const auto r=routes_.find(c->second.active);
            row.proxy=r->second.grant.proxy;const auto* proxy=member(row.proxy);row.via=proxy?proxy->name:r->second.grant.proxy_name;
            row.ping_ms=r->second.local.summary(now).p95;}
        rows.push_back(std::move(row));}
    return rows;
}
void Engine::publish_lobby(Clock::time_point now) {
    if(now<lobby_at_)return;lobby_at_=now+std::chrono::milliseconds(250);
    const auto rows=lobby_routes(now);Bytes payload;put(payload,rows.size(),1);
    for(const auto& row:rows){put(payload,row.endpoint);put(payload,row.proxy);text(payload,row.name);text(payload,row.via);}
    put(payload,0x53,1);put(payload,context_.generation);for(const auto& row:rows)put(payload,row.slot,1);
    put(payload,0x50,1);for(const auto& row:rows)put(payload,row.ping_ms,2);
    if(payload==lobby_payload_&&now<lobby_heartbeat_)return;
    lobby_payload_=payload;lobby_heartbeat_=now+std::chrono::seconds(1);
    for(const auto& m:context_.members)if(m.id!=context_.host)emit(m.id,LobbyStatus,[&](auto& b){b.insert(b.end(),payload.begin(),payload.end());});
}
RouteLabel lobby_route(const View& v,std::string_view peer,bool host) {
    if(host)return {"Host",false,false};
    Id id=0;for(const unsigned char c:peer){if(c<'0'||c>'9'||id>(UINT64_MAX-(c-'0'))/10)return {"Checking route...",false,true};id=id*10+c-'0';}
    if(!id)return {"Checking route...",false,true};
    // The local endpoint can confirm its own selection before the next host
    // snapshot arrives. Other players always use the authenticated host view.
    if(id==v.local){for(const auto& row:v.rows)if(row.endpoint==id&&row.selected&&row.ready)return {"Via "+row.via,true,false};return {"Direct",false,false};}
    for(const auto& row:v.lobby)if(row.endpoint==id)return {row.proxy?"Via "+row.via:"Direct",row.proxy!=0,false};
    return {"Checking route...",false,true};
}
std::string lobby_card_peer(const View& v,std::string_view peer,bool host,bool local,unsigned slot,Id generation) {
    if(!peer.empty())return std::string(peer);
    if(host&&v.host)return std::to_string(v.host);
    if(local&&v.local)return std::to_string(v.local);
    if(slot<4&&v.lobby_generation==generation)
        for(const auto& row:v.lobby)if(row.slot==slot)return std::to_string(row.endpoint);
    return {};
}
RouteLabel lobby_card_route(const View& v,std::string_view peer,bool host,bool local,unsigned slot,Id generation) {
    if(local&&v.local&&!host)return lobby_route(v,std::to_string(v.local));
    return lobby_route(v,lobby_card_peer(v,peer,host,local,slot,generation),host);
}
PingLabel lobby_card_ping(const View& v,std::string_view peer,bool host,bool local,unsigned slot,Id generation) {
    if(host)return {0,local?"This PC":"Host","The host is the reference for every player's ping. It has no network round trip to itself."};
    const auto id=lobby_card_peer(v,peer,host,local,slot,generation);
    if(v.lobby_generation==generation)for(const auto& row:v.lobby)if(std::to_string(row.endpoint)==id&&row.ping_ms)
        return {row.ping_ms,std::to_string(row.ping_ms)+" ms to host",
            row.proxy?"Recent round-trip time over the selected proxy route to the lobby host. Reported by the host; updates automatically.":
                      "Recent authenticated lobby round-trip time to the host. Reported by the host; updates automatically."};
    return {0,"Measuring...","Waiting for a fresh ping measurement from the lobby host. This is not a zero-millisecond connection."};
}
unsigned Engine::rtt(Id endpoint,Clock::time_point now)const{auto c=choices_.find(endpoint);if(c==choices_.end()||!c->second.active)return 0;auto r=routes_.find(c->second.active);return r==routes_.end()||!r->second.local.fresh(now)?0:r->second.local.summary(now).p95;}
bool Engine::usable(Id endpoint,Clock::time_point now)const{auto c=choices_.find(endpoint);if(c==choices_.end()||!c->second.active)return false;auto r=routes_.find(c->second.active);return r!=routes_.end()&&r->second.started&&r->second.local.fresh(now)&&delivering(r->second,now)&&now>=r->second.blocked_until&&io_.ready&&io_.ready(r->second.grant);}
std::string diagnostics(const View& v){std::ostringstream out;out<<"Proxy routes: "<<(v.enabled?"enabled":"off")<<"; volunteer="<<v.volunteer<<" forwarded="<<v.forwarded<<" queued="<<v.queued<<" dropped="<<v.dropped<<'\n';for(const auto& r:v.rows)out<<r.name<<" via "<<r.via<<" ready="<<r.ready<<" selected="<<r.selected<<" samples="<<r.local.samples<<" p50/p95/p99="<<r.local.p50<<'/'<<r.local.p95<<'/'<<r.local.p99<<" ms lost="<<r.local.lost<<" remote-p95="<<r.remote.p95<<" delivery-age-ms="<<r.delivery_age_ms<<" delivery-stalled="<<r.delivery_stalled<<" ICE="<<r.ice<<" reason="<<r.reason<<'\n';return out.str();}
} // namespace dkr::relay
