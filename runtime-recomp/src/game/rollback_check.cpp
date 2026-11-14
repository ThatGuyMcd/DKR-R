#include "rollback_check.hpp"
#include "netplay/experimental_latency.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <cstdio>
#include "assessment.hpp"

namespace dkr::runtime::rollback_check {
namespace {
using Type=net::protocol::MessageType;
constexpr unsigned MaxPeers=11,MaxSamples=512;
constexpr std::size_t QueueLimit=64*1024;
constexpr auto Cadence=std::chrono::nanoseconds(1000000000/30);
constexpr auto ReplicaCadence=std::chrono::milliseconds(50);
constexpr auto RepairCadence=std::chrono::milliseconds(50);
enum Kind:std::uint8_t {Hello=1,HelloAck,Probe,Echo,Result,Receipt,Cancel,ReportRow};
struct Pending {unsigned sequence=0,lane=0;std::uint64_t at=0;bool replied=false;};
struct Measure {
    Row row;
    std::vector<Pending> pending;
    std::vector<double> main,repair;
    unsigned next=1;
    bool receipted=false; unsigned report_next=0;
    Clock::time_point main_due{},repair_due{},hello_due{},result_due{};
};
struct State {
    std::mutex mutex;
    Context context;View view;
    std::map<Id,Measure> measures;
    std::uint32_t test=0;
    std::uint64_t nonce=0;
    Clock::time_point started{},measure_at{},drain_at{},finish_at{},retry_until{},last_start{};
    unsigned client_packets=0;std::array<std::array<bool,MaxSamples+1>,2> client_seen{};
    Clock::time_point client_hello{},receipt_at{},report_expected_at{};
    std::vector<Row> report_rows;
    std::array<std::vector<std::uint8_t>,12> report_parts{};
    std::array<Row,12> received_rows{};
    unsigned report_cursor=0;
};
struct CheckRegistry {std::mutex mutex;std::map<const void*,std::shared_ptr<State>> states;};
CheckRegistry& check_registry(){static auto* value=new CheckRegistry;return *value;}
std::shared_ptr<State> state(const void* key){
    auto& registry=check_registry();std::scoped_lock lock(registry.mutex);
    if(auto it=registry.states.find(key);it!=registry.states.end())return it->second;
    if(registry.states.size()>=8)registry.states.erase(registry.states.begin());
    auto next=std::make_shared<State>();registry.states[key]=next;return next;
}
std::uint64_t micros(Clock::time_point now){return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());}
void put(std::vector<std::uint8_t>& b,unsigned at,std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)b[at+i]=std::uint8_t(value>>(8*i));}
std::uint64_t get(std::span<const std::uint8_t> b,unsigned at,unsigned size){std::uint64_t n=0;for(unsigned i=0;i<size;++i)n|=std::uint64_t(b[at+i])<<(8*i);return n;}
bool same(const Context& a,const Context& b){
    if(a.room!=b.room||a.local!=b.local||a.host_id!=b.host_id||a.host!=b.host||a.party!=b.party||a.roster!=b.roster||
       a.automatic!=b.automatic||a.local_watching!=b.local_watching||(!a.automatic&&a.delay!=b.delay)||a.window!=b.window||a.peers.size()!=b.peers.size()||a.members.size()!=b.members.size()||a.local_member!=b.local_member||a.host_member!=b.host_member)return false;
    for(unsigned i=0;i<a.peers.size();++i)if(a.peers[i].id!=b.peers[i].id||a.peers[i].spectator!=b.peers[i].spectator)return false;
    for(unsigned i=0;i<a.members.size();++i)if(a.members[i].id!=b.members[i].id||a.members[i].spectator!=b.members[i].spectator||a.members[i].name!=b.members[i].name)return false;
    return true;
}
Type traffic(const Context& c,const Peer& p,unsigned lane){
    if(p.spectator)return Type::PreflightReplicaProbe;
    if(c.party)return Type::PreflightControlProbe;
    return lane?Type::PreflightAuthorityProbe:Type::PreflightRealtimeProbe;
}
const Peer* find(const Context& c,Id id){for(const auto& p:c.peers)if(p.id==id)return &p;return nullptr;}
std::vector<std::uint8_t> wire(const State& s,Kind kind,unsigned sequence,std::uint64_t at,unsigned lane=0,const Measure* m=nullptr){
    // Reuse the existing authenticated preflight envelope, not game-input or
    // Party Runtime messages. Old echo-only clients cannot manufacture HelloAck.
    const unsigned size=kind==Probe||kind==Echo?128:96;
    std::vector<std::uint8_t> b(size,0);
    b[0]='D';b[1]='K';b[2]='R';b[3]='C';b[4]=2;b[5]=kind;b[6]=s.context.party?1:0;b[7]=std::uint8_t(lane);
    put(b,8,s.nonce,8);b[16]=std::uint8_t(s.view.window);b[17]=std::uint8_t(s.view.delay);b[18]=std::uint8_t(s.view.automatic);
    if(kind==Hello){const auto finish=micros(s.finish_at);put(b,20,finish>at?std::min<std::uint64_t>(10000,(finish-at)/1000):0,4);}
    if(kind==Result&&m){
        const auto& r=m->row;b[19]=std::uint8_t(r.supported);
        put(b,20,r.sent,2);put(b,22,r.replies,2);put(b,24,r.repair_sent,2);put(b,26,r.repair_replies,2);
        put(b,28,std::uint32_t(std::ceil(r.median*1000)),4);put(b,32,std::uint32_t(std::ceil(r.p95*1000)),4);
        put(b,36,std::uint32_t(std::ceil(r.jitter*1000)),4);put(b,40,std::uint32_t(std::ceil(r.repair_p95*1000)),4);
        put(b,44,r.blocked,4);put(b,48,r.errors,4);put(b,52,s.view.scheduler_gaps,4);
        put(b,56,s.view.peak_queued,4);put(b,60,s.view.final_queued,4);
    }
    return net::protocol::encode_preflight_probe({s.test,sequence,at,kind==HelloAck||kind==Echo||kind==Receipt,std::move(b)});
}
void publish(State& s){
    s.view.rows.clear();for(const auto& p:s.context.members){
        if(p.id==s.context.host_member){Row r;r.id=p.id;r.name=p.name;r.spectator=p.spectator;r.host=true;s.view.rows.push_back(r);continue;}
        for(const auto& [id,m]:s.measures)if(m.row.id==p.id){s.view.rows.push_back(m.row);break;}
    }
}
double percentile(std::vector<double> v,double p){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[std::size_t(std::ceil(v.size()*p))-1];}
void calculate(Measure& m){
    m.row.median=percentile(m.main,.5);m.row.p95=percentile(m.main,.95);m.row.repair_p95=percentile(m.repair,.95);
    double total=0;for(unsigned i=1;i<m.main.size();++i)total+=std::abs(m.main[i]-m.main[i-1]);
    m.row.jitter=m.main.size()>1?total/double(m.main.size()-1):0;
    // Host and clients grade identical wire-precision measurements.
    for(auto* value:{&m.row.median,&m.row.p95,&m.row.jitter,&m.row.repair_p95})*value=std::ceil(*value*1000.)/1000.;
}
void cancel(State& s,std::string reason){s.view.active=false;s.view.complete=false;s.view.phase=Phase::Cancelled;s.view.status=std::move(reason);s.view.remaining_ms=0;s.retry_until={};}
void reset(State& s,const Context& c){s.context=c;s.view={};s.view.room=c.room;s.view.local=c.local_member;s.view.host_id=c.host_member;s.view.host=c.host;s.view.party=c.party;s.measures.clear();s.test=0;s.nonce=0;s.retry_until={};s.report_rows.clear();s.report_parts={};s.received_rows={};s.report_cursor=0;s.receipt_at=s.report_expected_at={};}
#include "scoreboard_wire.inl"
}
bool diagnostic_type(Type type){switch(type){
    case Type::PreflightBegin:case Type::PreflightResult:case Type::PreflightControlProbe:case Type::PreflightAuthorityProbe:
    case Type::PreflightRealtimeProbe:case Type::PreflightReplicaProbe:case Type::PreflightCheckpointProbe:return true;
    default:return false;}}
void forget(const void* key){auto& registry=check_registry();std::scoped_lock lock(registry.mutex);registry.states.erase(key);}
View snapshot(const void* key){auto s=state(key);std::scoped_lock lock(s->mutex);return s->view;}
bool active(const void* key){
    std::shared_ptr<State> s;{auto& registry=check_registry();std::scoped_lock lock(registry.mutex);auto it=registry.states.find(key);if(it==registry.states.end())return false;s=it->second;}
    std::scoped_lock lock(s->mutex);return s->view.active;
}
bool engaged(const void* key){
    std::shared_ptr<State> s;{auto& registry=check_registry();std::scoped_lock lock(registry.mutex);auto it=registry.states.find(key);if(it==registry.states.end())return false;s=it->second;}
    std::scoped_lock lock(s->mutex);return s->view.active||s->view.complete;
}
bool request(const void* key,const Context& c,std::string& error){
    auto p=state(key);auto& s=*p;std::scoped_lock lock(s.mutex);const auto now=Clock::now();
    if(!c.host||!c.idle){error="Only the host can check Rollback connections while waiting in the lobby.";return false;}
    if(s.view.active){error="A Rollback connection check is already running.";return false;}
    if(c.members.size()>12||!c.local_member||!c.host_member||!report_member(c,c.local_member)||!report_member(c,c.host_member)||c.members.size()!=c.peers.size()+1){error="Waiting for the complete lobby roster. Try again in a moment.";return false;}
    if(c.peers.empty()||c.peers.size()>MaxPeers){error="At least one remote participant must join before checking Rollback.";return false;}
    if(s.last_start.time_since_epoch().count()&&now-s.last_start<std::chrono::seconds(12)){error="Please wait a moment before checking again.";return false;}
    for(const auto& peer:c.peers)if(!peer.ready){error="A required Rollback channel is still warming up. Try again in a moment.";return false;}
    if(c.queued>QueueLimit){error="The connection is still transferring lobby resources. Wait for its queues to settle.";return false;}
    const auto revision=s.view.revision;reset(s,c);s.view.revision=revision;
    s.last_start=s.started=now;s.measure_at=now+std::chrono::seconds(2);s.drain_at=s.measure_at+std::chrono::seconds(6);s.finish_at=s.drain_at+std::chrono::seconds(2);
    s.nonce=micros(now);s.test=std::uint32_t(s.nonce^(s.nonce>>32));if(!s.test)s.test=1;
    s.view.active=true;s.view.phase=Phase::Negotiating;s.view.remaining_ms=10000;s.view.delay=c.delay;s.view.window=c.window;s.view.automatic=c.automatic;
    s.view.status="Negotiating the Rollback diagnostic with every participant...";
    for(const auto& peer:c.peers){auto& m=s.measures[peer.id];m.row.id=peer.member;m.row.name=peer.name;m.row.spectator=peer.spectator;m.pending.reserve(MaxSamples);m.main.reserve(200);m.repair.reserve(125);}
    publish(s);error.clear();return true;
}
void service(const void* key,const Context& c,const Send& send,Clock::time_point now){
    auto p=state(key);auto& s=*p;std::scoped_lock lock(s.mutex);
    if(s.context.room!=c.room||s.context.local!=c.local){reset(s,c);return;}
    if((s.view.active||s.view.complete)&&(!same(s.context,c)||!c.idle)){
        if(s.view.active&&c.host)for(const auto& peer:c.peers)send(peer.id,Type::PreflightControlProbe,wire(s,Cancel,1,micros(now)));
        cancel(s,"The lobby, roles, settings or launch state changed. Run a fresh Rollback check.");return;
    }
    if(!s.view.active){
        if(c.host&&s.view.complete&&now<s.retry_until)deliver_report(s,send,now);
        return;
    }
    s.view.remaining_ms=unsigned(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(s.finish_at-now).count()));
    if(!c.host){
        s.view.remaining_ms=unsigned(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(s.report_expected_at-now).count()));
        if(now>=s.report_expected_at){s.view.phase=Phase::Draining;s.view.status="Receiving the complete connection scoreboard...";}
        if(now>=s.finish_at)cancel(s,"The host's Rollback check expired without a final report. Retry from the lobby.");return;}
    s.view.peak_queued=std::max(s.view.peak_queued,c.queued);
    if(now<s.measure_at){
        for(auto& [id,m]:s.measures)if(!m.row.supported&&now>=m.hello_due){send(id,Type::PreflightControlProbe,wire(s,Hello,1,micros(now)));m.hello_due=now+std::chrono::milliseconds(400);}
    } else if(now<s.drain_at){
        s.view.phase=Phase::Sampling;s.view.status="Measuring Rollback input timing (30 Hz); spectator playback is measured separately.";
        for(auto& [id,m]:s.measures){
            if(!m.row.supported)continue;
            const auto* peer=find(c,id);if(!peer)continue;
            for(unsigned lane=0;lane<2;++lane){
                if(lane&&(c.party||peer->spectator))continue;
                auto& due=lane?m.repair_due:m.main_due;
                const auto interval=lane?std::chrono::duration_cast<Clock::duration>(RepairCadence):peer->spectator?std::chrono::duration_cast<Clock::duration>(ReplicaCadence):std::chrono::duration_cast<Clock::duration>(Cadence);
                if(!due.time_since_epoch().count())due=s.measure_at;
                if(now<due)continue;
                // No catch-up burst after a paused/overloaded worker. Count the
                // local scheduling gap so it cannot be blamed on the network.
                if(now-due>=Cadence*2)++s.view.scheduler_gaps;
                due=now-due>=interval?now+interval:due+interval;
                if(c.queued>QueueLimit){++m.row.blocked;continue;}
                if(m.pending.size()>=MaxSamples)continue;
                const unsigned sequence=m.next++;const auto at=micros(now);
                if(send(id,traffic(c,*peer,lane),wire(s,Probe,sequence,at,lane))){m.pending.push_back({sequence,lane,at,false});if(lane)++m.row.repair_sent;else ++m.row.sent;}
                else ++m.row.blocked;
            }
        }
    } else if(now<s.finish_at){s.view.phase=Phase::Draining;s.view.status="Waiting for final Rollback probe replies and queue drain...";}
    else{
        for(auto& [id,m]:s.measures)calculate(m);
        s.view.active=false;s.view.complete=true;s.view.phase=Phase::Complete;s.view.final_queued=c.queued;s.view.remaining_ms=0;
        s.view.revision=s.test; // Unique per check, including after reconnect.
        s.view.status="Connection scoreboard ready. Open it for player scores and advice.";
        publish(s);freeze_report(s);s.retry_until=now+std::chrono::seconds(3);
    }
    publish(s);
}
void receive(const void* key,const Context& c,Id sender,Type type,std::span<const std::uint8_t> bytes,const Send& send,Clock::time_point now){
    if(!diagnostic_type(type)||!c.idle)return;
    net::protocol::PreflightProbePayload packet;std::string ignored;
    if(!net::protocol::decode_preflight_probe(bytes,packet,ignored))return;
    const auto& b=packet.padding;
    if((b.size()!=96&&b.size()!=128)||b[0]!='D'||b[1]!='K'||b[2]!='R'||b[3]!='C'||b[4]!=2||b[5]<Hello||b[5]>ReportRow||b[6]!=unsigned(c.party)||b[7]>1||!get(b,8,8))return;
    const auto kind=Kind(b[5]);
    if((kind==Probe||kind==Echo)!=(b.size()==128))return;
    if(packet.echo!=(kind==HelloAck||kind==Echo||kind==Receipt))return;
    if(kind!=Probe&&kind!=Echo&&type!=Type::PreflightControlProbe)return;
    auto p=state(key);auto& s=*p;std::scoped_lock lock(s.mutex);
    if(s.context.room!=c.room||s.context.local!=c.local)reset(s,c);
    if(c.host){
        if(!same(s.context,c)||s.test!=packet.test_id||s.nonce!=get(b,8,8))return;
        auto it=s.measures.find(sender);if(it==s.measures.end())return;auto& m=it->second;
        if(kind==HelloAck&&s.view.active&&now<s.measure_at)m.row.supported=true;
        else if(kind==Receipt&&s.view.complete)m.receipted=true;
        else if(kind==Echo&&s.view.active&&m.row.supported){
            const auto* peer=find(c,sender);if(!peer||type!=traffic(c,*peer,b[7]))return;
            auto found=std::find_if(m.pending.begin(),m.pending.end(),[&](const auto& v){return v.sequence==packet.sequence&&v.at==packet.sent_time_us&&v.lane==b[7];});
            if(found==m.pending.end()||found->replied||micros(now)<found->at)return;
            found->replied=true;const double ms=double(micros(now)-found->at)/1000.;
            if(b[7]){m.repair.push_back(ms);++m.row.repair_replies;}else{m.main.push_back(ms);++m.row.replies;}
        }
        publish(s);return;
    }
    if(sender!=c.host_id)return; // Only the admitted authenticated host can ask.
    if(kind==Hello){
        if(get(b,20,4)>10000||c.members.empty()||c.members.size()>12||!report_member(c,c.local_member)||!report_member(c,c.host_member))return;
        if(b[16]!=c.window||b[18]!=unsigned(c.automatic)||(!c.automatic&&b[17]!=c.delay))return;
        if(s.test!=packet.test_id||s.nonce!=get(b,8,8)){
            if(s.client_hello.time_since_epoch().count()&&now-s.client_hello<std::chrono::seconds(12))return;
            reset(s,c);s.client_hello=now;s.test=packet.test_id;s.nonce=get(b,8,8);s.client_packets=0;s.client_seen={};
            s.report_expected_at=now+std::chrono::milliseconds(get(b,20,4));s.finish_at=s.report_expected_at+std::chrono::seconds(4);s.view.active=true;s.view.phase=Phase::Sampling;s.view.delay=b[17];s.view.window=b[16];s.view.automatic=b[18]!=0;
            s.view.status="The host is checking Rollback input timing. No game has been started.";
        }
        // Retries are bounded, even from a valid host. Never reset the deadline.
        if(++s.client_packets<=8)send(sender,Type::PreflightControlProbe,wire(s,HelloAck,packet.sequence,packet.sent_time_us));
        return;
    }
    if(s.test!=packet.test_id||s.nonce!=get(b,8,8)||!same(s.context,c)||now>=s.finish_at)return;
    if(kind==Probe&&s.view.active){
        const bool watching=c.local_watching;
        const Type expected=watching?Type::PreflightReplicaProbe:c.party?Type::PreflightControlProbe:b[7]?Type::PreflightAuthorityProbe:Type::PreflightRealtimeProbe;
        if(type!=expected||(b[7]&&(watching||c.party))||packet.sequence>MaxSamples||s.client_seen[b[7]][packet.sequence]||++s.client_packets>MaxSamples+8)return;
        s.client_seen[b[7]][packet.sequence]=true;send(sender,type,wire(s,Echo,packet.sequence,packet.sent_time_us,b[7]));
    }else if(kind==ReportRow&&(s.view.active||s.view.complete)){
        receive_report(s,c,packet,send,now);
    }
    else if(kind==Cancel&&s.view.active)cancel(s,"The host cancelled the Rollback check because the lobby changed.");
}
}
