#include "live_delay.hpp"
#include "response_trace.hpp"
#include <algorithm>
#include <cstdio>

namespace dkr::online_stability {
namespace {
thread_local LiveDelay* bound_delay=nullptr;
thread_local bool batched_arrival=false;
void put(Bytes& b,std::uint64_t value,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(std::uint8_t(value>>(i*8)));}
std::uint64_t get(std::span<const std::uint8_t> b,std::size_t& at,unsigned width){std::uint64_t value=0;for(unsigned i=0;i<width;++i)value|=std::uint64_t(b[at++])<<(i*8);return value;}
}
bool InputQueue::capture(Input value,Clock::time_point now) {
    if(value==observed_)return true;
    if(!changes_.empty()&&changes_.back().value.buttons==value.buttons) {
        changes_.back().value=value;observed_=value;observed_at_=now;return true;
    }
    if(changes_.size()>=64)return false;
    changes_.push_back({value,now});observed_=value;observed_at_=now;return true;
}
Input InputQueue::take(Clock::time_point now) {
    age_ms_=0;
    if(!changes_.empty()) {
        const auto change=changes_.front();const bool edge=held_.buttons!=change.value.buttons;
        held_=observed_;held_.buttons=change.value.buttons;held_at_=edge?change.at:observed_at_;++serial_;
        if(now>=held_at_)age_ms_=unsigned(std::min<std::int64_t>(65535,std::chrono::duration_cast<std::chrono::milliseconds>(now-held_at_).count()));
        changes_.pop_front();
    }
    return held_;
}
void InputQueue::reset(){changes_.clear();observed_=held_={};age_ms_=0;held_at_=observed_at_={};serial_=0;}
Bytes encode(const DelayMessage& m) {
    if(m.kind<DelayKind::Hello||m.kind>DelayKind::Target||!m.room||!m.epoch||!m.controls||
        m.delay>MaximumDelay||m.rtt_ms>5000||m.late_frames>48||m.network_waits>65535||m.samples>255||m.blocked_ms>65535||m.arrival_age_ms>65535||
        (m.owner>3&&m.owner!=255)||m.work_us>10000000||m.replay_us>m.work_us||m.interval_us>10000000)return {};
    for(unsigned i=0;i<4;++i)if(m.owner_late[i]>48||m.owner_samples[i]>255||m.owner_waits[i]>65535)return {};
    Bytes b{'D','K','R','X',1,DelayPayloadKind};put(b,unsigned(m.kind),1);put(b,m.room,8);put(b,m.epoch,8);
    put(b,m.controls,8);put(b,m.sequence,8);put(b,m.frame,4);put(b,m.effective,4);
    put(b,m.delay,1);put(b,m.rtt_ms,2);put(b,m.late_frames,1);put(b,m.network_waits,2);put(b,m.samples,1);put(b,m.enabled,1);put(b,m.blocked_ms,2);put(b,m.arrival_age_ms,2);
    put(b,m.target,8);put(b,m.owner,1);
    for(unsigned i=0;i<4;++i){put(b,m.owner_late[i],1);put(b,m.owner_samples[i],1);put(b,m.owner_waits[i],2);}
    put(b,m.work_us,4);put(b,m.replay_us,4);put(b,m.interval_us,4);return b;
}
bool decode(std::span<const std::uint8_t> b,DelayMessage& m) {
    if(b.size()!=96||!std::equal(b.begin(),b.begin()+6,std::array<std::uint8_t,6>{'D','K','R','X',1,DelayPayloadKind}.begin()))return false;
    DelayMessage n;std::size_t at=6;n.kind=DelayKind(get(b,at,1));n.room=get(b,at,8);n.epoch=get(b,at,8);n.controls=get(b,at,8);n.sequence=get(b,at,8);
    n.frame=std::uint32_t(get(b,at,4));n.effective=std::uint32_t(get(b,at,4));n.delay=unsigned(get(b,at,1));n.rtt_ms=unsigned(get(b,at,2));n.late_frames=unsigned(get(b,at,1));
    n.network_waits=unsigned(get(b,at,2));n.samples=unsigned(get(b,at,1));const auto enabled=get(b,at,1);if(enabled>1)return false;n.enabled=enabled!=0;
    n.blocked_ms=unsigned(get(b,at,2));n.arrival_age_ms=unsigned(get(b,at,2));n.target=get(b,at,8);n.owner=unsigned(get(b,at,1));
    for(unsigned i=0;i<4;++i){n.owner_late[i]=unsigned(get(b,at,1));n.owner_samples[i]=unsigned(get(b,at,1));n.owner_waits[i]=unsigned(get(b,at,2));}
    n.work_us=unsigned(get(b,at,4));n.replay_us=unsigned(get(b,at,4));n.interval_us=unsigned(get(b,at,4));
    if(encode(n)!=Bytes(b.begin(),b.end()))return false;m=n;return true;
}
void DelayPolicy::reset(unsigned,Clock::time_point now){changed_=stable_=now;recovery_until_={};}
std::optional<unsigned> DelayPolicy::evaluate(unsigned current,unsigned window,unsigned,unsigned late,unsigned waits,bool fresh,bool measured,Clock::time_point now,bool pressure,bool busy,unsigned blocked_ms) {
    if(!fresh||!measured){stable_=now;return {};}
    // Reserve one frame for scheduling variation and one for recovery. Lateness
    // which fits in available history is useful prediction, not an input fault.
    const auto covered=window>2?window-2:0;
    // A failed low-delay probe needs a recovery interval. Otherwise a narrow
    // window repeatedly drops its protective lead immediately after an outage.
    // Healthy, inexpensive prediction still takes the two-second reduction path.
    // Poll counts are not pause durations. A transient missing packet serviced
    // before half a 30-Hz tick must not buy a full extra tick of input delay.
    const bool waiting=waits&&(blocked_ms==UINT32_MAX||blocked_ms>=17);
    if(waiting||late>covered)recovery_until_=now+std::chrono::seconds(10);
    auto target=current;
    if(waiting||late>covered)target=std::min(MaximumDelay,current+std::min(3U,std::max(1U,late>covered?late-covered:1U)));
    else if(pressure&&late>1)target=std::min(MaximumDelay,current+1);
    if(target>current) {
        stable_=now;
        if(now-changed_>=std::chrono::seconds(1)){changed_=now;return target;}
        return {};
    }
    // Periodically explore a lower local lead despite cheap corrections. A
    // reduction drains immutable future samples; it never rewrites an input.
    const bool margin=late==0||(covered>1&&late<covered);
    if(((pressure||busy)&&late)||!margin){stable_=now;return {};}
    if(current&&now>=recovery_until_&&now-stable_>=std::chrono::seconds(2)&&now-changed_>=std::chrono::seconds(2)) {
        changed_=stable_=now;return current-1;
    }
    return {};
}
LiveDelay::LiveDelay(std::shared_ptr<Control> control,unsigned fixed,unsigned window)
    :control_(std::move(control)),fixed_(std::min(fixed,MaximumDelay)),current_(fixed_),window_(window){}
void LiveDelay::send(Member target,DelayMessage m) {
    m.room=room_;m.epoch=epoch_;m.controls=controls_;m.frame=m.kind==DelayKind::Hello?clock_frontier_:frame_;(void)control_->send(target,encode(m));
}
void LiveDelay::apply(unsigned value,Clock::time_point now) {
    if(value==current_)return;
    if(value>current_)fill_+=value-current_;
    std::fprintf(stderr,"[online][dynamic-delay] epoch=%llu controls=%llu frame=%u owner=%u from=%u to=%u policy=responsive-per-owner\n",
        static_cast<unsigned long long>(epoch_),static_cast<unsigned long long>(controls_),frame_,local_owner_,current_,value);
    current_=value;applied_at_=now;
}
void LiveDelay::receive(const Identity& id,Member source,const DelayMessage& m,Clock::time_point now) {
    if(m.room!=room_||m.epoch!=epoch_||m.controls!=controls_||!id.dynamic)return;
    if(m.kind==DelayKind::Hello||m.kind==DelayKind::Report) {
        if(!m.enabled&&m.kind==DelayKind::Report)return;
        capable_[source]=now;
        if(!id.authority&&source==id.host)host_lead_=m.delay;
        if(!id.authority&&source==id.host&&m.kind==DelayKind::Hello) {
            host_ready_=m.enabled;
            if(m.enabled&&(!host_clock_||m.frame>host_clock_->frame)) {
                unsigned rtt=0;for(const auto& route:id.routes)if(route.id==id.host)rtt=route.rtt_ms;
                host_clock_=HostClock{m.frame,rtt,now};
            }
        }
        if(id.authority&&m.kind==DelayKind::Report) {
            const auto route=std::find_if(id.routes.begin(),id.routes.end(),[&](const Route& r){return r.id==source&&r.racer&&r.owner==m.owner;});
            if(route==id.routes.end())return;
            const auto previous=reports_.find(source);
            if(m.sequence&&(previous==reports_.end()||m.sequence>previous->second.message.sequence))reports_[source]={m,now};
        }
        return;
    }
    if(id.authority) {
        const auto pending=targets_.find(source);
        if(m.kind==DelayKind::Ack&&pending!=targets_.end()&&m.target==source&&m.owner==pending->second.message.owner&&
            m.sequence==pending->second.message.sequence&&m.delay==pending->second.message.delay)targets_.erase(pending);
        return;
    }
    if(source!=id.host||!negotiated_||m.kind!=DelayKind::Target||m.target!=id.local||m.owner!=local_owner_||!m.sequence)return;
    if(m.sequence<retired_)return;
    if(m.sequence>retired_){apply(m.delay,now);retired_=m.sequence;}
    auto ack=m;ack.kind=DelayKind::Ack;send(id.host,ack);
}
void LiveDelay::pulse(std::uint64_t epoch,std::uint64_t controls,std::uint32_t frame,bool paused,bool network_wait,Clock::time_point now,std::uint8_t missing_owners,bool confirmed_gate) {
    const auto id=control_->identity();const bool was_dynamic=dynamic_;paused_=paused;dynamic_=id.dynamic;
    if(id.prediction_window&&*id.prediction_window>=2&&*id.prediction_window<=20&&window_!=*id.prediction_window) {
        window_=*id.prediction_window;policies_.clear();
    }
    if(id.manual_delay&&*id.manual_delay<=MaximumDelay)fixed_=*id.manual_delay;
    if(room_!=id.room||epoch_!=epoch||controls_!=controls) {
        const bool retain=room_==id.room&&controls_==controls&&was_dynamic&&id.dynamic&&pulse_at_.time_since_epoch().count()&&now>=pulse_at_&&now-pulse_at_<=std::chrono::seconds(10);
        const auto learned=retain?current_:fixed_;
        room_=id.room;epoch_=epoch;controls_=controls;frame_=frame;current_=learned;fill_=learned>fixed_?learned-fixed_:0;late_=waits_=samples_=0;
        clock_frontier_=frame;host_clock_.reset();
        capable_.clear();reports_.clear();targets_.clear();policies_.clear();serial_=retired_=report_serial_=0;report_at_=hello_at_=decision_at_=sample_at_={};
        waiting_frame_=UINT32_MAX;negotiated_=host_ready_=false;host_lead_.reset();inputs_.reset();traced_serial_=0;moved_at_=now;
        pulse_at_=arrival_at_=wait_started_=applied_at_={};owner_arrivals_={};blocked_time_=wait_time_={};was_waiting_=false;
        owner_late_={};owner_samples_={};owner_waits_={};arrival_histogram_={};work_us_=replay_us_=0;measured_at_=now;response::reset(epoch);
    }
    if(was_dynamic!=id.dynamic) {
        capable_.clear();reports_.clear();targets_.clear();policies_.clear();host_lead_.reset();
        negotiated_=host_ready_=false;hello_at_=report_at_=decision_at_={};
    }
    if(!id.dynamic){apply(fixed_,now);host_lead_.reset();}
    const bool waiting=network_wait&&!paused&&!confirmed_gate;
    if(waiting&&was_waiting_&&pulse_at_.time_since_epoch().count()&&now>=pulse_at_){
        const auto elapsed=std::min(now-pulse_at_,std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(50)));
        blocked_time_+=elapsed;wait_time_+=elapsed;
    }
    if(waiting&&!was_waiting_)wait_started_=now;
    if(!waiting){wait_started_={};wait_time_={};}
    pulse_at_=now;was_waiting_=waiting;
    if(frame_!=frame)moved_at_=now;frame_=frame;
    // A private correction rewinds execution, never the host's pacing clock.
    clock_frontier_=std::max(clock_frontier_,frame);
    if(waiting&&waiting_frame_!=frame) {
        waits_=std::min(65535U,waits_+1);waiting_frame_=frame;
        for(unsigned o=0;o<4;++o)if(o!=local_owner_&&(missing_owners&(1U<<o)))owner_waits_[o]=std::min(65535U,owner_waits_[o]+1);
    }
    for(auto& packet:control_->incoming()){DelayMessage m;if(decode(packet.bytes,m))receive(id,packet.source,m,now);}
    // Menus/resource transactions deliberately require agreed inputs. Their
    // waits cannot measure the lead needed for speculative racing. Keep input
    // capture active here so menu buttons continue to work normally.
    if(confirmed_gate){late_=waits_=samples_=0;owner_late_={};owner_samples_={};owner_waits_={};arrival_histogram_={};blocked_time_={};work_us_=replay_us_=0;measured_at_=now;}
    bool capable=id.dynamic&&id.active;
    if(id.authority) {
        for(const auto& route:id.routes)if(route.racer&&route.id!=id.local){const auto peer=capable_.find(route.id);if(peer==capable_.end()||now-peer->second>std::chrono::seconds(5))capable=false;}
    } else {const auto peer=capable_.find(id.host);capable=capable&&host_ready_&&peer!=capable_.end()&&now-peer->second<=std::chrono::seconds(5);}
    negotiated_=capable;
    if(id.active&&id.dynamic&&room_&&epoch_&&controls_) {
        if(!hello_at_.time_since_epoch().count()||now>=hello_at_) {
            DelayMessage hello;hello.kind=DelayKind::Hello;hello.delay=current_;hello.owner=local_owner_<4?local_owner_:255;hello.enabled=id.authority?negotiated_:true;
            send(id.authority?0:id.host,hello);hello_at_=now+std::chrono::milliseconds(500);
        }
        if(!paused&&(!report_at_.time_since_epoch().count()||now>=report_at_)) {
            DelayMessage report;report.kind=DelayKind::Report;report.enabled=true;report.delay=current_;report.owner=local_owner_<4?local_owner_:255;report.sequence=++report_serial_;
            for(const auto& route:id.routes)if(route.racer)report.rtt_ms=std::max(report.rtt_ms,std::min(5000U,route.rtt_ms));
            report.late_frames=late_;report.network_waits=waits_;report.samples=samples_;
            report.blocked_ms=unsigned(std::min<std::int64_t>(65535,std::chrono::duration_cast<std::chrono::milliseconds>(blocked_time_).count()));
            report.arrival_age_ms=arrival_at_.time_since_epoch().count()?unsigned(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now-arrival_at_).count(),0,65535)):0;
            report.owner_late=owner_late_;report.owner_samples=owner_samples_;report.owner_waits=owner_waits_;
            // Deadline p90 rejects an isolated old/reordered packet. Real
            // exhausted-window pauses still take the measured wait path.
            for(unsigned o=0;o<4;++o)if(owner_samples_[o]) {
                const auto rank=(owner_samples_[o]*90+99)/100;unsigned count=0;
                for(unsigned n=0;n<49;++n){count+=arrival_histogram_[o][n];if(count>=rank){report.owner_late[o]=n;break;}}
            }
            for(unsigned o=0;o<4;++o)if(waiting&&o!=local_owner_&&(missing_owners&(1U<<o)))report.owner_waits[o]=std::max(report.owner_waits[o],(report.blocked_ms+32)/33);
            report.work_us=unsigned(std::min<std::uint64_t>(10000000,work_us_));report.replay_us=unsigned(std::min<std::uint64_t>(report.work_us,replay_us_));
            report.interval_us=unsigned(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now-measured_at_).count(),0,10000000));
            if(id.authority){report.room=room_;report.epoch=epoch_;report.controls=controls_;report.frame=frame_;reports_[id.local]={report,now};}else send(id.host,report);
            late_=waits_=samples_=0;owner_late_={};owner_samples_={};owner_waits_={};arrival_histogram_={};blocked_time_={};work_us_=replay_us_=0;measured_at_=now;report_at_=now+std::chrono::milliseconds(500);
        }
    }
    if(id.authority&&negotiated_&&!paused&&!confirmed_gate&&now>=decision_at_) {
        decision_at_=now+std::chrono::milliseconds(500);
        std::vector<std::pair<Member,unsigned>> racers;
        if(id.local_racer&&local_owner_<4)racers.push_back({id.local,local_owner_});
        for(const auto& route:id.routes)if(route.racer&&route.owner<4)racers.push_back({route.id,route.owner});
        for(const auto& [member,owner]:racers) {
            if(targets_.contains(member))continue;
            const auto origin=reports_.find(member);if(origin==reports_.end()||now-origin->second.at>std::chrono::seconds(2))continue;
            unsigned late=0,waits=0,blocked=0;bool fresh=true,measured=true,pressure=false,busy=false;
            for(const auto& [receiver,receiver_owner]:racers)if(receiver!=member) {
                (void)receiver_owner;const auto found=reports_.find(receiver);
                if(found==reports_.end()||now-found->second.at>std::chrono::seconds(2)){fresh=false;break;}
                const auto& m=found->second.message;
                if(m.blocked_ms&&m.arrival_age_ms>=1200){fresh=false;break;}
                late=std::max(late,m.owner_late[owner]);waits=std::max(waits,m.owner_waits[owner]);measured&=m.owner_samples[owner]!=0||m.owner_waits[owner]!=0;
                if(m.owner_waits[owner])blocked=std::max(blocked,m.blocked_ms);
                if(m.interval_us>=100000&&std::uint64_t(m.work_us)*10>std::uint64_t(m.interval_us)*7&&std::uint64_t(m.replay_us)*5>m.interval_us)pressure=true;
                if(m.interval_us>=100000&&std::uint64_t(m.work_us)*2>m.interval_us&&std::uint64_t(m.replay_us)*10>m.interval_us)busy=true;
            }
            auto [policy,inserted]=policies_.try_emplace(member);if(inserted)policy->second.reset(origin->second.message.delay,now);
            const auto target=policy->second.evaluate(origin->second.message.delay,window_,0,late,waits,fresh,measured,now,pressure,busy,blocked);
            if(!target)continue;
            std::fprintf(stderr,"[online][delay-target] epoch=%llu member=%llu owner=%u from=%u to=%u late=%u waits=%u replay-pressure=%u window=%u\n",
                static_cast<unsigned long long>(epoch_),static_cast<unsigned long long>(member),owner,origin->second.message.delay,*target,late,waits,unsigned(pressure),window_);
            if(member==id.local){apply(*target,now);reports_[member].message.delay=*target;}
            else {DelayMessage message;message.kind=DelayKind::Target;message.target=member;message.owner=owner;message.sequence=++serial_;message.delay=*target;message.enabled=true;targets_[member]={message,{}};}
        }
    }
    if(id.authority&&negotiated_&&!paused)for(auto& [member,target]:targets_)if(now>=target.retry){send(member,target.message);target.retry=now+std::chrono::milliseconds(250);}
    const bool stalled=wait_time_>=std::chrono::milliseconds(1200);std::uint8_t stalled_owners=0;unsigned oldest=0;
    for(unsigned owner=0;owner<4;++owner)if(owner!=local_owner_&&(missing_owners&(1U<<owner))) {
        const auto at=owner_arrivals_[owner].time_since_epoch().count()?owner_arrivals_[owner]:wait_started_;
        const auto age=at.time_since_epoch().count()&&now>=at?unsigned(std::min<std::int64_t>(65535,std::chrono::duration_cast<std::chrono::milliseconds>(now-at).count())):0;
        oldest=std::max(oldest,age);if(stalled&&age>=1200)stalled_owners|=std::uint8_t(1U<<owner);
    }
    control_->report({id.dynamic,negotiated_,current_,!id.dynamic?"Manual input delay":!negotiated_?"Waiting for Dynamic Input Delay support":paused?"Holding delay through a scene or player change":stalled?"Connection stalled; recovering input":current_==MaximumDelay?"At delay limit; monitoring racing connections":"Monitoring local response and racing connections",stalled,stalled_owners,oldest,unsigned(inputs_.queued())});
    if(now>=diagnostic_at_) {
        diagnostic_at_=now+std::chrono::seconds(5);
        std::fprintf(stderr,"[online][delay-status] epoch=%llu frame=%u dynamic=%u negotiated=%u lead=%u local-owner=%u missing-mask=0x%X stalled-mask=0x%X arrival-age-ms=%u transitions=%zu sampled-age-upper-ms=%u targets=%zu policy=responsive-per-owner\n",
            static_cast<unsigned long long>(epoch_),frame_,unsigned(id.dynamic),unsigned(negotiated_),current_,local_owner_,unsigned(missing_owners),unsigned(stalled_owners),oldest,inputs_.queued(),inputs_.age_ms(),targets_.size());
        response::report(now);
    }
}
bool LiveDelay::capture(Input input,Clock::time_point now) {
    if(paused_||now-moved_at_>std::chrono::milliseconds(500))return true;
    return inputs_.capture(input,now);
}
void LiveDelay::arrival(unsigned owner,std::uint32_t frame,bool repair) {
    if(owner>=4||owner==local_owner_)return;owner_arrivals_[owner]=arrival_at_=pulse_at_;if(repair)return;
    samples_=std::min(255U,samples_+1);
    if(owner_samples_[owner]<255){++owner_samples_[owner];++arrival_histogram_[owner][frame<frame_?std::min(48U,frame_-frame):0];}
    if(frame<frame_){const auto late=std::min(48U,frame_-frame);late_=std::max(late_,late);owner_late_[owner]=std::max(owner_late_[owner],late);}
}
void LiveDelay::work(std::uint64_t us,bool replay,std::uint32_t frame,bool executed) {
    work_us_=std::min<std::uint64_t>(10000000,work_us_+us);if(replay)replay_us_=std::min<std::uint64_t>(10000000,replay_us_+us);
    if(executed)response::reached(epoch_,frame,response::Stage::Execute,std::max(pulse_at_,Clock::now()));
}
void LiveDelay::authored(std::uint32_t frame) {
    if(inputs_.serial()==traced_serial_)return;traced_serial_=inputs_.serial();
    response::assigned(epoch_,frame,inputs_.captured_at(),pulse_at_);
}
unsigned sample_lead(unsigned fallback){return bound_delay?bound_delay->lead():fallback;}
std::uint64_t sample_limit(std::uint32_t frontier,unsigned fallback,bool live_lead) {
    return std::uint64_t(frontier)+(bound_delay?bound_delay->lead()+(bound_delay->dynamic()?1:live_lead?2:1):fallback+(live_lead?2:1));
}
DelayScope::DelayScope(LiveDelay& delay):previous_(bound_delay),prediction_(delay.prediction_window()){bound_delay=&delay;}
DelayScope::~DelayScope(){bound_delay=previous_;}
void input_arrival(unsigned owner,std::uint32_t frame){if(bound_delay&&!batched_arrival)bound_delay->arrival(owner,frame);}
void input_authored(std::uint32_t frame){if(bound_delay)bound_delay->authored(frame);}
void input_work(std::uint64_t us,bool replay,std::uint32_t frame,bool executed){if(bound_delay)bound_delay->work(us,replay,frame,executed);}
WorkScope::WorkScope(const std::uint32_t& next,const std::uint32_t& replayed)
    :delay_(bound_delay),next_(next),replayed_(replayed),before_(next),replays_(replayed),start_(delay_?Clock::now():Clock::time_point{}){}
WorkScope::~WorkScope(){if(delay_)delay_->work(std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start_).count()),replayed_>replays_,next_?next_-1:0,next_>before_||replayed_>replays_);}
ArrivalScope::ArrivalScope():previous_(batched_arrival){batched_arrival=true;}
ArrivalScope::~ArrivalScope(){batched_arrival=previous_;}
void batch_arrival(unsigned owner,std::uint32_t newest,bool repair){if(bound_delay)bound_delay->arrival(owner,newest,repair);}
}
