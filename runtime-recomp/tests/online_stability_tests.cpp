#include "online_stability/control.hpp"
#include "online_stability/live_delay.hpp"
#include "online_stability/response_trace.hpp"
#include "online_stability/live_inputs.hpp"
#include "online_stability/traffic_meter.hpp"
#include "online_stability/catalogue_cache.hpp"
#include "online_stability/battle_view.hpp"
#include "online_stability/party_pacing.hpp"
#include "online_stability/peer_clock.hpp"
#include "online_stability/live_send_capacity.hpp"
#include "online_stability/ping_window.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/session_transport.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace dkr::online_stability;
namespace {
void background_budget_checks() {
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::time_point(std::chrono::seconds(100));
    auto anchor=start;std::size_t tokens=0,total=0;
    for(unsigned i=1;i<=1000;++i) {
        refill_background_budget(start+std::chrono::microseconds(i*100),anchor,tokens,16*1024,256);
        total+=tokens;tokens=0;
    }
    assert(total==25600); // A busy 10 kHz pump must retain the full 100 ms credit.
    refill_background_budget(start+std::chrono::hours(1),anchor,tokens,16*1024,256);
    assert(tokens==16*1024); // Long idle time cannot exceed the bounded burst.
    tokens=0;
    refill_background_budget(anchor,anchor,tokens,16*1024,256);assert(tokens==0);
    refill_background_budget(start,anchor,tokens,16*1024,256);assert(tokens==0&&anchor==start);
    Clock::time_point empty{};tokens=100000;
    refill_background_budget(start,empty,tokens,16*1024,256);assert(tokens==16*1024&&empty==start);
}
void host_settings_checks() {
    HostSettings current;HostSettingsContext context{true,true,false,4};
    auto desired=current;desired.prediction_window=20;desired.input_delay=5;desired.automatic_delay=false;
    desired.menu_control=2;desired.record_replay=false;
    auto plan=plan_host_settings(current,desired,context);
    assert(plan&&plan.immediate.prediction_window==20&&plan.immediate.input_delay==5&&!plan.immediate.automatic_delay);
    assert(plan.immediate.menu_control==current.menu_control&&plan.immediate.record_replay==current.record_replay&&plan.queued==desired);
    auto bad=desired;bad.maximum_players=2;assert(!plan_host_settings(current,bad,context));
    bad=desired;bad.development=true;assert(!plan_host_settings(current,bad,context));
    bad=desired;bad.input_delay=10;assert(!plan_host_settings(current,bad,context));
    bad=desired;bad.prediction_window=21;assert(!plan_host_settings(current,bad,context));
    context.transition=true;assert(!plan_host_settings(current,desired,context));context.transition=false;
    context.playing=false;plan=plan_host_settings(current,desired,context);assert(plan&&plan.immediate==desired&&!plan.queued);
    context.spectator_capable=false;current.spectators=false;assert(!plan_host_settings(current,desired,context));
    current.spectators=true;context.spectator_capable=true;
    auto bridge=std::make_shared<Control>();Identity identity{88,1,1,true,true,false,{{2,50,true,1}}};
    bridge->configure(identity);bridge->publish_host_settings(current,context);
    std::uint64_t serial=0;std::string error;
    assert(!bridge->request_host_settings(87,desired,serial,error));
    assert(bridge->request_host_settings(88,desired,serial,error)&&serial==1);
    auto request=bridge->take_host_settings();assert(request&&request->desired==desired&&!bridge->take_host_settings());
    bridge->finish_host_settings(*request,desired,"Queued",true);
    assert(bridge->host_settings().queued==desired&&bridge->host_settings().accepted);
    identity.room=89;bridge->configure(identity);assert(!bridge->host_settings().available&&!bridge->host_settings().queued);
    identity.authority=false;bridge->configure(identity);bridge->publish_host_settings(current,context);
    assert(!bridge->request_host_settings(89,desired,serial,error));
    identity={88,1,1,true,true,false,{{2,50,true,1}}};identity.manual_delay=3;identity.prediction_window=4;
    bridge->configure(identity);LiveDelay delay(bridge,3,4);delay.local_owner(0);
    const auto start=Clock::time_point(std::chrono::seconds(100));
    delay.pulse(91,1,100,false,false,start);assert(delay.lead()==3);
    {DelayScope scope(delay);assert(requested_prediction_window()==4);assert(sample_limit(100,3,true)==105);
        identity.manual_delay=7;identity.prediction_window=20;bridge->configure(identity);
        delay.pulse(91,1,101,false,false,start+std::chrono::milliseconds(34));
        assert(delay.lead()==7&&delay.take_fill()==4&&requested_prediction_window()==20);
        assert(sample_limit(101,3,true)==110);}
    assert(!requested_prediction_window());
    assert(delay.capture({0x8000,10,0},start+std::chrono::milliseconds(35)));
    assert(delay.capture({0,20,0},start+std::chrono::milliseconds(36)));
    identity.manual_delay=1;bridge->configure(identity);delay.pulse(91,1,102,false,false,start+std::chrono::milliseconds(67));
    assert(delay.lead()==1&&delay.take_fill()==0&&delay.take().buttons==0x8000&&delay.take().buttons==0);
    identity.dynamic=true;bridge->configure(identity);delay.pulse(91,1,103,false,false,start+std::chrono::milliseconds(100));
    identity.dynamic=false;identity.manual_delay=2;bridge->configure(identity);
    delay.pulse(91,1,104,false,false,start+std::chrono::milliseconds(134));assert(!delay.dynamic()&&delay.lead()==2);
}
void delayed_ping_checks() {
    PingWindow window;const auto start=Clock::time_point(std::chrono::seconds(100));
    window.sent(11,start);window.sent(12,start+std::chrono::seconds(1));
    assert(window.receive(11,start+std::chrono::milliseconds(1600))==1600);
    assert(!window.receive(11,start+std::chrono::milliseconds(1700)));
    assert(!window.receive(999,start+std::chrono::milliseconds(1700)));
    assert(window.receive(12,start+std::chrono::milliseconds(2500))==1500);
    window.sent(13,start+std::chrono::seconds(3));window.sent(14,start+std::chrono::seconds(4));
    assert(window.receive(14,start+std::chrono::milliseconds(4100))==100);
    assert(!window.receive(13,start+std::chrono::milliseconds(4200)));
    window.sent(15,start+std::chrono::seconds(5));
    assert(!window.receive(15,start+std::chrono::seconds(10)));
    window.sent(16,start+std::chrono::seconds(11));
    assert(!window.receive(16,start+std::chrono::seconds(10)));
    for(unsigned n=0;n<100;++n)window.sent(100+n,start+std::chrono::seconds(12));
    assert(!window.receive(100,start+std::chrono::seconds(13)));
    assert(window.receive(199,start+std::chrono::seconds(13))==1000);
    window={};assert(!window.receive(199,start+std::chrono::seconds(13)));
}
struct CatalogueFixture {
    enum class Kind { Track,Character };
    inline static unsigned generation=0,scans=0;
    static std::vector<unsigned> installed_items(const std::filesystem::path&,std::string_view,Kind){++scans;return {generation};}
};
void peer_clock_checks() {
    PeerClock clock;const auto start=Clock::time_point(std::chrono::seconds(100));
    assert(clock.period(1,start)==PeerClock::Tick);
    clock.observe(101,1,40,start); // Host frontier 100, one tick plus transit later.
    assert(clock.period(101,start)==PeerClock::Tick);
    assert(clock.period(90,start)<PeerClock::Tick&&clock.period(90,start)>=PeerClock::Tick*95/100);
    assert(clock.period(110,start)>PeerClock::Tick&&clock.period(110,start)<=PeerClock::Tick*105/100);
    const auto prior=clock.error(100,start);
    clock.observe(100,1,40,start+std::chrono::milliseconds(5)); // Reordered/duplicate input.
    assert(clock.error(100,start)==prior);
    clock.observe(1000,0,6000,start);assert(clock.error(100,start)==prior); // Invalid route estimate.
    assert(clock.period(90,start+std::chrono::seconds(3))==PeerClock::Tick);
    clock.reset();assert(!clock.error(1,start));
    // Repeated short delivery jitter cannot buy unbounded simulation speed.
    for(unsigned frame=0;frame<600;++frame) {
        const auto now=start+PeerClock::Tick*frame;
        clock.observe(frame+4,4,40+(frame%3)*10,now);
        const auto period=clock.period(frame,now);
        assert(period>=PeerClock::Tick*95/100&&period<=PeerClock::Tick*105/100);
    }
    // Explicit host progress is independent of changing future input lead.
    // Old repair/replay frontiers cannot move that reference backwards.
    clock.reset();clock.observe_frontier(300,750,start);
    assert(clock.error(311,start).value()>0&&clock.error(311,start).value()<1);
    clock.observe_frontier(290,750,start+std::chrono::milliseconds(50));
    assert(clock.reference_frame()==300&&clock.age_ms(start+std::chrono::milliseconds(50))==50);
    clock.observe_frontier(300,750,start+std::chrono::milliseconds(100));
    assert(clock.age_ms(start+std::chrono::milliseconds(100))==0);
    assert(clock.error(311,start+std::chrono::milliseconds(100)).value()<1);
    clock.observe_frontier(315,750,start+std::chrono::milliseconds(500));
    assert(clock.error(326,start+std::chrono::milliseconds(500)).value()>0);
    // After an outage, normal reports must let the client return to nominal
    // speed rather than buying more input lead to compensate for clock drift.
    double local=326.;
    for(unsigned ms=500;ms<30500;++ms) {
        const auto now=start+std::chrono::milliseconds(ms);
        if(ms%500==0)clock.observe_frontier(300+ms*30/1000,ms<5000?750:1,now);
        local+=1000000./double(clock.period(std::uint32_t(local),now).count());
    }
    const auto final=start+std::chrono::milliseconds(30499);
    assert(std::abs(clock.error(std::uint32_t(local),final).value())<2.1);
}
void host_clock_regressions() {
    const auto start=Clock::time_point(std::chrono::seconds(100));
    auto host=std::make_shared<Control>(),client=std::make_shared<Control>();
    host->configure({55,1,1,true,true,true,{{2,40,true,1}}});
    client->configure({55,2,1,true,false,true,{{1,40,true,0}}});
    LiveDelay authority(host,1,14),racer(client,1,14);
    racer.pulse(91,1,100,false,false,start);
    for(auto& p:client->outgoing())assert(host->receive(2,true,p.bytes));
    authority.pulse(91,1,100,false,false,start);
    for(auto& p:host->outgoing())assert(client->receive(1,true,p.bytes));
    racer.pulse(91,1,100,false,false,start);
    auto clock=racer.take_host_clock();assert(clock&&clock->frame==100&&clock->rtt_ms==40);
    assert(!racer.take_host_clock());
    authority.pulse(91,1,94,false,false,start+std::chrono::milliseconds(500)); // Host is replaying.
    for(auto& p:host->outgoing())assert(client->receive(1,true,p.bytes));
    racer.pulse(91,1,108,false,false,start+std::chrono::milliseconds(500));
    clock=racer.take_host_clock();assert(clock&&clock->frame==100);
    authority.pulse(91,1,130,false,false,start+std::chrono::milliseconds(1000));
    for(auto& p:host->outgoing())assert(client->receive(1,true,p.bytes));
    racer.pulse(91,1,120,false,false,start+std::chrono::milliseconds(1000));
    clock=racer.take_host_clock();assert(clock&&clock->frame==130);
    DelayMessage stale;stale.kind=DelayKind::Hello;stale.room=55;stale.controls=1;stale.epoch=91;stale.frame=999;stale.enabled=true;
    assert(client->receive(1,true,encode(stale)));
    racer.pulse(92,1,0,false,false,start+std::chrono::milliseconds(1500));
    assert(!racer.take_host_clock()); // An old scene is never the new clock.
}
void live_queue_freshness_checks() {
    struct Reliability {bool unordered=false;std::optional<unsigned> maxRetransmits=0;std::optional<std::chrono::milliseconds> maxPacketLifeTime;};
    Reliability policy;configure_live_reliability(policy);
    assert(policy.unordered&&!policy.maxRetransmits&&policy.maxPacketLifeTime==std::chrono::milliseconds(1000));
    using Traffic=dkr::runtime::netplay::TransportTrafficClass;
    static_assert(fresh_input_capacity(Traffic::Realtime,0,1500));
    static_assert(!fresh_input_capacity(Traffic::Realtime,400,200));
    static_assert(fresh_input_capacity(Traffic::Authoritative,16000,200));
    static_assert(!fresh_input_capacity(Traffic::Realtime,SIZE_MAX,1));
    auto congested=[](bool bounded) {
        std::deque<unsigned> sdk;std::optional<unsigned> latest;unsigned maximum_age=0;
        for(unsigned ms=0;ms<6000;++ms) {
            if(ms%33==0)latest=ms;
            if(latest&&(!bounded||fresh_input_capacity(Traffic::Realtime,sdk.size()*200,200))) {
                sdk.push_back(*latest);latest.reset();
            }
            if(ms%100==0&&!sdk.empty()) {maximum_age=std::max(maximum_age,ms-sdk.front());sdk.pop_front();}
        }
        return maximum_age;
    };
    assert(congested(false)>3000&&congested(true)<=300);
}
void catalogue_freshness() {
    using Scope=CatalogueScope<CatalogueFixture,unsigned>;
    const auto kind=CatalogueFixture::Kind::Track;
    {Scope job;assert(Scope::installed("library","us.v77",kind)==std::vector<unsigned>{0});
        ++CatalogueFixture::generation;assert(Scope::installed("library","us.v77",kind)==std::vector<unsigned>{0});
        {Scope nested;assert(Scope::installed("library","us.v77",kind)==std::vector<unsigned>{1});}
        assert(Scope::installed("library","us.v77",kind)==std::vector<unsigned>{0});}
    {Scope next;assert(Scope::installed("library","us.v77",kind)==std::vector<unsigned>{1});}
    assert(CatalogueFixture::scans==3);
}
void fullscreen_checks() {
    for(unsigned players=2;players<=4;++players)for(unsigned owner=0;owner<players;++owner)
        for(unsigned race:{0U,3U,64U,65U,66U,67U,5U,8U}) {
            std::vector<dkr_owned_draw_event> events;
            dkr_owned_draw_event shared{};shared.kind=DKR_OWNED_ONLINE_SHARED;shared.address=0x80001000;events.push_back(shared);
            for(unsigned port=0;port<players;++port){dkr_owned_draw_event view{};view.kind=DKR_OWNED_ONLINE_VIEW;view.address=0x80002000+port*8;
                view.token=port;view.parameters[0]=players-1;view.parameters[2]=race;view.parameters[4]=players;events.push_back(view);}
            const bool supported=race==0||race==3||race==64||race==65||race==66;
            assert(view_eligibility(owner,players,events).eligible==supported);
            events.back().parameters[3]=1;assert(!view_eligibility(owner,players,events).eligible);events.back().parameters[3]=0;
            events.back().parameters[1]=1;assert(!view_eligibility(owner,players,events).eligible);events.back().parameters[1]=0;
            if(supported){auto duplicate=events;duplicate.push_back(events.back());assert(!view_eligibility(owner,players,duplicate).eligible);
                events.pop_back();assert(!view_eligibility(owner,players,events).eligible);}
        }
}
void progress_checks() {
    Progress p{42,1,{{5,6,40,100,"Racer",Stage::Downloading}}};
    auto b=encode(p);Progress decoded;
    assert(decode(b,decoded)&&decoded.rows==p.rows);
    for(std::size_t n=0;n<b.size();++n)assert(!decode(std::span(b).first(n),decoded));
    b.push_back(0);assert(!decode(b,decoded));
    p.rows[0].received=101;assert(encode(p).empty());p.rows[0].received=40;
    p.rows.push_back(p.rows[0]);assert(encode(p).empty());p.rows.pop_back();
    auto host=std::make_shared<Control>(),client=std::make_shared<Control>();
    host->configure({42,1,1,true,true,true,{{2,20,true}}});
    client->configure({42,2,1,true,false,true,{{1,20,true}}});
    const auto t=Clock::now();host->pending(p.rows,t);const auto publication=host->publication(t);assert(publication);
    assert(!host->receive(2,true,*publication));
    assert(!client->receive(1,false,*publication));assert(!client->receive(9,true,*publication));
    assert(client->receive(1,true,*publication));assert(client->joins().size()==1);
    assert(!client->receive(1,true,*publication)); // Out of order/repeated snapshot.
    assert(!host->publication(t+std::chrono::milliseconds(499)));
    host->pending({},t);const auto empty=host->publication(t+std::chrono::milliseconds(500));assert(empty);
    assert(client->receive(1,true,*empty));assert(client->joins().empty());
    p.room=43;p.revision=99;assert(!client->receive(1,true,encode(p)));
    client->configure({43,2,1,true,false,true,{{1,20,true}}});assert(client->joins().empty());
    assert(!client->receive(1,true,*publication));
    host->configure({});assert(host->outgoing().empty()&&host->joins().empty());
}
void input_checks() {
    InputQueue q;
    const Input down{0x8000,40,0},up{0,40,0};
    assert(q.capture(down)&&q.capture(down)&&q.capture(up)&&q.capture(down));
    assert(q.queued()==3);assert(q.take()==down&&q.take()==up&&q.take()==down&&q.take()==down);
    q.reset();assert(q.held()==Input{});
    // Simulate a nine-frame lead reduction while continuing physical capture.
    std::vector<Input> observed;
    for(unsigned i=0;i<18;++i){Input v{std::uint16_t(i%2?0:0x8000),0,0};assert(q.capture(v));observed.push_back(v);}
    for(const auto& v:observed)assert(q.take()==v);
}
void jitter_and_analogue_regressions() {
    const auto start=Clock::time_point(std::chrono::seconds(100));
    InputQueue q;
    assert(q.capture({0x8000,10,0,25,0,1},start));
    assert(q.capture({0,20,0,50,0,1},start+std::chrono::milliseconds(10)));
    assert(q.capture({0x8000,70,0,220,40,1},start+std::chrono::milliseconds(20)));
    for(unsigned buttons:{0x8000U,0U,0x8000U}) {
        const auto value=q.take(start+std::chrono::milliseconds(30));
        assert(value.buttons==buttons&&value.x==70&&value.accelerator==220&&value.brake==40);
    }
    // A single old packet in an otherwise healthy delivery interval must not
    // ratchet an owner's lead up on every Wi-Fi jitter spike.
    auto bridge=std::make_shared<Control>();bridge->configure({44,2,1,true,false,true,{{1,50,true,0}}});
    LiveDelay delay(bridge,1,6);delay.local_owner(1);delay.pulse(91,1,100,false,false,start);bridge->outgoing();
    for(unsigned n=0;n<19;++n)delay.arrival(0,100);
    delay.arrival(0,91);
    delay.pulse(91,1,100,false,false,start+std::chrono::milliseconds(500));
    bool report=false;
    for(const auto& packet:bridge->outgoing()) {DelayMessage m;assert(decode(packet.bytes,m));if(m.kind==DelayKind::Report){report=true;assert(m.owner_late[0]==0);}}
    assert(report);
    // A confirmed resource transaction must not teach Auto that a race needs
    // additional lead. Its physical buttons must remain usable meanwhile.
    delay.pulse(91,1,101,false,true,start+std::chrono::milliseconds(1000),1,true);
    assert(delay.capture({0x8000,60,0},start+std::chrono::milliseconds(1001)));
    assert(delay.take().buttons==0x8000);
    for(const auto& packet:bridge->outgoing()) {DelayMessage m;assert(decode(packet.bytes,m));if(m.kind==DelayKind::Report)assert(m.network_waits==0&&m.samples==0&&m.blocked_ms==0);}
    DelayPolicy policy;policy.reset(1,start);
    assert(policy.evaluate(1,6,0,0,1,true,true,start+std::chrono::seconds(2),false,false,2)==0);
    policy.reset(1,start);
    assert(policy.evaluate(1,6,0,0,1,true,true,start+std::chrono::seconds(2),false,false,34)==2);
    using Step=dkr::runtime::netplay::experimental::Step;
    assert(pause_reason(Step::PredictionLimit,true,2,0)==Pause::Network);
    assert(pause_reason(Step::WaitingForLocalInput,false,2,0)==Pause::Pacing);
    assert(pause_reason(Step::WaitingForLocalInput,true,1,0)==Pause::LocalInput);
    assert(pause_reason(Step::WaitingForPresentation,true,2,0)==Pause::Presentation);
    assert(pause_reason(Step::WaitingForConfirmedInput,true,2,0)==Pause::Confirmation);
    ReplaySlice slice(start);for(unsigned n=0;n<3;++n)assert(slice.again(true,start+std::chrono::milliseconds(n)));
    assert(!slice.again(true,start+std::chrono::milliseconds(3)));
    ReplaySlice slow(start);assert(!slow.again(true,start+std::chrono::milliseconds(4)));
    ReplaySlice waiting(start);assert(!waiting.again(false,start));
    PacingTrace trace;trace.reason(Pause::Network);trace.end(start);
    trace.begin(start+std::chrono::milliseconds(4));trace.step(Step::WaitingForLocalInput,5000);
    trace.step(Step::Replayed,7000);assert(trace.ticks()==1&&trace.tick_us()==7000&&trace.idle(Pause::Network)==4000);
}
void codec_checks() {
    DelayMessage m; m.room=1;m.epoch=91;m.controls=1;m.enabled=true;
    for(unsigned kind=1;kind<=7;++kind) {
        m.kind=DelayKind(kind);m.sequence=100;m.effective=500;m.delay=9;m.rtt_ms=250;m.late_frames=20;m.network_waits=4;
        m.blocked_ms=500;m.arrival_age_ms=1200;
        m.target=9;m.owner=2;m.owner_late={1,2,3,4};m.owner_samples={5,6,7,8};m.owner_waits={9,10,11,12};m.work_us=25000;m.replay_us=6000;m.interval_us=500000;
        auto b=encode(m);DelayMessage next;assert(b.size()==96&&b[5]==DelayPayloadKind&&b[5]!=3&&b[5]!=4&&decode(b,next)&&next==m);
        for(std::size_t n=0;n<b.size();++n)assert(!decode(std::span(b).first(n),next));
        b.push_back(0);assert(!decode(b,next));
    }
    m.delay=10;assert(encode(m).empty());m.delay=9;m.room=0;assert(encode(m).empty());
}
void stationary_wait_reports() {
    auto bridge=std::make_shared<Control>();bridge->configure({44,2,1,true,false,true,{{1,80,true}}});
    LiveDelay delay(bridge,4,20);const auto start=Clock::time_point(std::chrono::seconds(100));
    delay.pulse(91,1,12,false,true,start);bridge->outgoing();
    DelayMessage hello;hello.kind=DelayKind::Hello;hello.room=44;hello.epoch=91;hello.controls=1;hello.enabled=true;
    assert(bridge->receive(1,true,encode(hello)));
    unsigned sustained=0;
    for(unsigned ms=5;ms<=2000;ms+=5){
        delay.pulse(91,1,12,false,true,start+std::chrono::milliseconds(ms));
        for(const auto& packet:bridge->outgoing()){DelayMessage report;assert(decode(packet.bytes,report));
            if(report.kind==DelayKind::Report&&ms>=500){assert(report.blocked_ms>=495&&report.frame==12);++sustained;}}
    }
    assert(sustained==4&&bridge->delay().reason=="Connection stalled; recovering input");
    delay.pulse(91,1,13,true,true,start+std::chrono::milliseconds(2005));
    assert(bridge->delay().reason=="Waiting for Dynamic Input Delay support"||bridge->delay().reason=="Holding delay through a scene or player change");
}
void deadline_regressions() {
    const auto start=Clock::time_point(std::chrono::seconds(100));
    auto receiver=std::make_shared<Control>();receiver->configure({55,2,1,true,false,true,{{1,50,true,0}}});
    LiveDelay delivery(receiver,4,3);delivery.local_owner(1);delivery.pulse(91,1,100,false,false,start);receiver->outgoing();
    delivery.arrival(0,0,true); // An exact old repair is useful, but is not fresh delivery jitter.
    delivery.pulse(91,1,100,false,false,start+std::chrono::milliseconds(500));
    for(const auto& packet:receiver->outgoing()){DelayMessage m;assert(decode(packet.bytes,m));if(m.kind==DelayKind::Report)assert(m.samples==0&&m.late_frames==0);}
    {DelayScope bound(delivery);ArrivalScope batch;input_arrival(0,0);input_arrival(0,99);batch_arrival(0,100,false);}
    delivery.pulse(91,1,100,false,false,start+std::chrono::milliseconds(1000));
    for(const auto& packet:receiver->outgoing()){DelayMessage m;assert(decode(packet.bytes,m));if(m.kind==DelayKind::Report)assert(m.samples==1&&m.late_frames==0);}
    TrafficMeter meter;meter.sent(2,100,true);meter.sent(2,999,false);meter.sent(9,55,true);
    assert(meter.lanes()[2].packets==1&&meter.lanes()[2].bytes==100&&meter.lanes()[2].blocked==1);
    meter.reset();assert(meter.lanes()[2].bytes==0);
    auto manual=std::make_shared<Control>();manual->configure({88,1,1,true,true,false,{{2,50,true,1},{3,50,true,2}}});
    LiveDelay observed(manual,3,3);observed.local_owner(0);
    observed.pulse(91,1,12,false,true,start,6);
    for(unsigned ms=5;ms<=1300;ms+=5){observed.arrival(1,12);observed.pulse(91,1,12,false,true,start+std::chrono::milliseconds(ms),6);}
    assert(manual->delay().stalled_owners==4); // A healthy owner cannot conceal the silent owner, including manual mode.
    observed.pulse(92,1,0,false,true,start+std::chrono::milliseconds(1400),6);
    observed.pulse(92,1,0,false,true,start+std::chrono::milliseconds(3400),6);
    assert(!manual->delay().stalled); // A single two-second CPU scheduling gap is not two seconds of measured network wait.
    LiveInputs live;live.controls=1;live.epoch=91;live.lease=7;live.owner=3;live.first=100;live.count=8;
    for(unsigned i=0;i<8;++i)live.inputs[i]={std::uint16_t(i*197),std::int8_t(-127+int(i)),127};
    const auto encoded=encode_live_inputs(live);LiveInputs decoded;
    assert(encoded.size()==86&&decode_live_inputs(encoded,decoded)&&decoded.inputs==live.inputs);
    for(unsigned n=0;n<encoded.size();++n)assert(!decode_live_inputs(std::span(encoded).first(n),decoded));
    auto bad=encoded;bad.push_back(0);assert(!decode_live_inputs(bad,decoded));live.first=UINT32_MAX;assert(encode_live_inputs(live).empty());
    for(unsigned window=2;window<=20;++window) {
        DelayPolicy policy;policy.reset(5,start);
        const auto target=policy.evaluate(5,window,20,0,0,true,true,start+std::chrono::seconds(20));
        assert(target&&*target==4); // Includes the former impossible window=3 reduction.
    }
    InputQueue analog;
    for(int x=1;x<64;++x)assert(analog.capture({0,std::int8_t(x),0}));
    assert(analog.queued()==1&&analog.take().x==63);
    assert(analog.capture({0x8000,40,0})&&analog.capture({0x8000,50,0})&&analog.capture({0,60,0}));
    assert(analog.take()==Input({0x8000,60,0})&&analog.take()==Input({0,60,0}));
    std::array<std::shared_ptr<Control>,2> bridges;
    std::array<std::unique_ptr<LiveDelay>,2> delays;
    for(unsigned i=0;i<2;++i) {
        bridges[i]=std::make_shared<Control>();
        bridges[i]->configure({77,i+1,1,true,i==0,true,{{i?1U:2U,100,true,i?0U:1U}}});
        delays[i]=std::make_unique<LiveDelay>(bridges[i],4,3);delays[i]->local_owner(i);
    }
    unsigned first_increase=0,pre_wait_delay=4;
    for(unsigned ms=0;ms<=8000;ms+=5)for(unsigned i=0;i<2;++i) {
        const auto frame=std::min(150U,ms*30/1000);
        delays[i]->arrival(i?0:1,frame+1);
        delays[i]->pulse(91,1,frame,false,ms>=5000,start+std::chrono::milliseconds(ms),std::uint8_t(i?1:2));
        if(i==0&&ms<5000)pre_wait_delay=delays[0]->lead();
        if(i==0&&ms>=5000&&!first_increase&&delays[0]->lead()>pre_wait_delay)first_increase=ms;
        for(auto& p:bridges[i]->outgoing())assert(bridges[1-i]->receive(i+1,true,p.bytes));
    }
    assert(first_increase&&first_increase<=6000&&delays[1]->lead()>pre_wait_delay);
    const auto learned=delays[0]->lead();
    delays[0]->pulse(92,1,0,false,false,start+std::chrono::milliseconds(8005));
    assert(delays[0]->lead()==learned); // Same room/owners retain the learned sample lead.
    delays[0]->pulse(93,2,0,false,false,start+std::chrono::milliseconds(8010));
    assert(delays[0]->lead()==4); // A new controller lease resets the lead.
}
struct Envelope {unsigned destination;Member source;Bytes bytes;unsigned due;};
void responsiveness_checks() {
    const auto start=Clock::time_point(std::chrono::seconds(100));
    DelayPolicy policy;policy.reset(4,start);
    assert(!policy.evaluate(4,12,900,6,0,true,true,start+std::chrono::seconds(1)));
    assert(policy.evaluate(4,12,900,6,0,true,true,start+std::chrono::seconds(2))==3); // Cheap prediction can reduce delay despite high RTT.
    policy.reset(2,start);
    assert(policy.evaluate(2,12,900,6,0,true,true,start+std::chrono::seconds(2),true)==3); // Actual replay pressure protects a weak CPU.
    policy.reset(2,start);
    assert(!policy.evaluate(2,12,900,6,0,true,true,start+std::chrono::seconds(3),false,true)); // The middle CPU band holds, rather than oscillating.
    assert(policy.evaluate(2,12,900,0,0,true,true,start+std::chrono::seconds(6),false,true)==1); // On-time input is not penalised for another owner's replay work.
    policy.reset(2,start);
    assert(!policy.evaluate(2,12,900,0,0,true,false,start+std::chrono::seconds(20))); // Ping alone is not input delay.
    assert(policy.evaluate(2,3,20,3,0,true,true,start+std::chrono::seconds(21))==4); // Exhausted history still grows the lead.
    policy.reset(0,start);
    assert(policy.evaluate(0,3,20,2,1,true,true,start+std::chrono::seconds(2))==1);
    assert(!policy.evaluate(1,3,20,0,0,true,true,start+std::chrono::seconds(4))); // Do not repeatedly probe a failed lower lead after an outage.
    assert(policy.evaluate(1,3,20,0,0,true,true,start+std::chrono::seconds(13))==0);
    namespace trace=response;
    trace::reset(123);trace::assigned(123,10,start,start+std::chrono::milliseconds(5));
    trace::reached(123,9,trace::Stage::Renderer,start+std::chrono::milliseconds(10));
    trace::reached(123,10,trace::Stage::Renderer,start+std::chrono::milliseconds(20));
    assert(trace::snapshot()[unsigned(trace::Stage::Renderer)].count==0);
    trace::reached(123,10,trace::Stage::Execute,start+std::chrono::milliseconds(30));
    trace::reached(123,10,trace::Stage::Publish,start+std::chrono::milliseconds(40));
    trace::reached(123,11,trace::Stage::Renderer,start+std::chrono::milliseconds(80));
    trace::reached(123,12,trace::Stage::Renderer,start+std::chrono::milliseconds(100));
    auto measured=trace::snapshot();assert(measured[0].maximum_us==5000&&measured[1].maximum_us==30000&&measured[2].maximum_us==40000&&measured[3].maximum_us==80000&&measured[3].count==1);
    trace::reset(124);trace::reached(123,20,trace::Stage::Execute,start);assert(trace::snapshot()[1].count==0);
}
void asymmetric_targets(bool cpu_pressure=false) {
    std::array<std::shared_ptr<Control>,3> bridges;
    std::array<std::unique_ptr<LiveDelay>,3> delays;
    for(unsigned i=0;i<3;++i){bridges[i]=std::make_shared<Control>();
        bridges[i]->configure({909,i+1,1,true,i==0,true,i?std::vector<Route>{{1,300,true,0}}:std::vector<Route>{{2,20,true,1},{3,600,true,2}}});
        delays[i]=std::make_unique<LiveDelay>(bridges[i],4,12);delays[i]->local_owner(i);}
    const auto start=Clock::time_point(std::chrono::seconds(100));std::vector<Envelope> wire;bool dropped_target=false,dropped_ack=false;
    for(unsigned ms=0;ms<22000;ms+=5){const auto frame=ms*30/1000;
        for(auto it=wire.begin();it!=wire.end();)if(it->due<=ms){assert(bridges[it->destination]->receive(it->source,true,it->bytes));it=wire.erase(it);}else ++it;
        for(unsigned i=0;i<3;++i){
            for(unsigned owner=0;owner<3;++owner)if(owner!=i){const auto late=owner==2?(cpu_pressure?5U:12U):0U;delays[i]->arrival(owner,frame>late?frame-late:0);}
            if(cpu_pressure&&i==1)delays[i]->work(5000,true,frame,false);
            delays[i]->pulse(91,1,frame,false,false,start+std::chrono::milliseconds(ms));
            for(auto& packet:bridges[i]->outgoing()){DelayMessage message;assert(decode(packet.bytes,message));
                if(message.kind==DelayKind::Target&&!dropped_target){dropped_target=true;continue;}
                if(message.kind==DelayKind::Ack&&!dropped_ack){dropped_ack=true;continue;}
                for(unsigned d=0;d<3;++d)if(d!=i&&(!packet.target||packet.target==d+1))wire.push_back({d,i+1,packet.bytes,ms+25});}
        }
    }
    assert(dropped_target&&dropped_ack&&delays[0]->lead()==0&&delays[1]->lead()==0&&delays[2]->lead()>2);
    DelayMessage report;report.kind=DelayKind::Report;report.enabled=true;report.room=909;report.epoch=91;report.controls=1;
    report.frame=660;report.sequence=100;report.owner=1;report.owner_samples={10,0,10,0};report.delay=0;
    assert(bridges[0]->receive(2,true,encode(report)));
    report.sequence=99;report.owner_late[0]=48;assert(bridges[0]->receive(2,true,encode(report)));
    delays[0]->pulse(91,1,660,false,false,start+std::chrono::seconds(22));assert(delays[0]->lead()==0); // Reordered feedback at a frozen frontier cannot replace a newer report.
    // A client cannot change its own target, another owner's target, or a new scene.
    DelayMessage bad;bad.kind=DelayKind::Target;bad.enabled=true;bad.room=909;bad.epoch=91;bad.controls=1;bad.sequence=999;bad.target=2;bad.owner=1;bad.delay=9;
    assert(!bridges[1]->receive(3,true,encode(bad)));
    bad.target=3;assert(bridges[1]->receive(1,true,encode(bad)));delays[1]->pulse(91,1,660,false,false,start+std::chrono::seconds(22));assert(delays[1]->lead()==0);
    bad.target=2;bad.epoch=90;assert(bridges[1]->receive(1,true,encode(bad)));delays[1]->pulse(91,1,661,false,false,start+std::chrono::seconds(22)+std::chrono::milliseconds(5));assert(delays[1]->lead()==0);
    std::cout<<"Per-owner targets: healthy owners 0/0, impaired owner "<<delays[2]->lead()<<"; cpu-pressure="<<cpu_pressure<<"; lost commands/acks retried\n";
}
void negotiated_trace(bool dynamic,unsigned window,bool impaired,bool loss=false,bool unsupported=false,bool transitions=false) {
    std::array<std::shared_ptr<Control>,3> bridges;
    std::array<std::unique_ptr<LiveDelay>,3> delays;
    for(unsigned i=0;i<3;++i) {
        bridges[i]=std::make_shared<Control>();
        std::vector<Route> routes=i?std::vector<Route>{{1,20,true,0}}:std::vector<Route>{{2,20,true,1},{3,20,true,2},{9,4900,false}};
        bridges[i]->configure({44,i+1,1,true,i==0,dynamic&&!(unsupported&&i==2),std::move(routes)});
        delays[i]=std::make_unique<LiveDelay>(bridges[i],4,window);
        delays[i]->local_owner(i);
    }
    std::vector<Envelope> wire;
    const auto start=Clock::time_point(std::chrono::seconds(100));
    unsigned high=0,low=9,changes=0,previous=4;
    std::set<std::pair<unsigned,unsigned>> dropped;
    for(unsigned ms=0;ms<60000;ms+=5) {
        const auto now=start+std::chrono::milliseconds(ms);
        const unsigned rtt=impaired&&ms>=20000&&ms<35000?250:20;
        for(unsigned i=0;i<3;++i){auto id=bridges[i]->identity();for(auto& route:id.routes)if(route.racer)route.rtt_ms=rtt;bridges[i]->configure(std::move(id));}
        for(auto it=wire.begin();it!=wire.end();) {
            if(it->due>ms){++it;continue;}
            assert(bridges[it->destination]->receive(it->source,true,it->bytes));it=wire.erase(it);
        }
        for(unsigned i=0;i<3;++i) {
            const bool pause=transitions&&ms>=18000&&ms<19000;
            const auto epoch=transitions&&ms>=30000?92ULL:91ULL;
            const auto frame=(transitions?ms%30000:ms)*30/1000;
            const unsigned late=impaired&&ms>=20000&&ms<35000?8:0;
            for(unsigned o=0;o<3;++o)if(o!=i)delays[i]->arrival(o,frame>late?frame-late:0);
            delays[i]->pulse(epoch,transitions&&ms>=45000?2:1,frame,pause,false,now);
            assert(delays[i]->lead()<=9);
            for(auto& p:bridges[i]->outgoing()) {
                DelayMessage message;assert(decode(p.bytes,message));
                for(unsigned d=0;d<3;++d)if(d!=i&&(!p.target||p.target==d+1)) {
                    const auto kind=unsigned(message.kind);
                    if(loss&&(message.kind==DelayKind::Target||message.kind==DelayKind::Ack)&&dropped.insert({kind,d}).second)continue;
                    wire.push_back({d,i+1,p.bytes,ms+rtt/2+(loss?(unsigned(p.bytes[6])+d)%3*15:0)});
                }
            }
        }
        if(delays[0]->lead()!=previous){++changes;previous=delays[0]->lead();}
        if(ms>12000&&ms<20000)low=std::min(low,delays[0]->lead());
        if(ms>25000&&ms<35000)high=std::max(high,delays[0]->lead());
    }
    if(dynamic&&!unsupported){assert(delays[0]->negotiated());assert(changes>0&&low<4);if(impaired&&window==2&&!transitions)assert(high>=8);}
    else if(unsupported){assert(!delays[0]->negotiated());for(const auto& delay:delays)assert(delay->lead()==4);}
    else for(const auto& delay:delays)assert(delay->lead()==4&&!delay->negotiated());
    if(dynamic&&!unsupported)for(const auto& delay:delays)assert(delay->lead()<=3); // Independent owners need not change on the same wall-clock turn.
    std::cout<<"delay trace dynamic="<<dynamic<<" window="<<window<<" impairment="<<impaired<<" low="<<low<<" high="<<high<<" changes="<<changes<<'\n';
}
}
int main() {
    background_budget_checks();
    host_settings_checks();
    delayed_ping_checks();
    peer_clock_checks();
    host_clock_regressions();
    live_queue_freshness_checks();
    catalogue_freshness();fullscreen_checks();progress_checks();input_checks();jitter_and_analogue_regressions();codec_checks();stationary_wait_reports();deadline_regressions();responsiveness_checks();asymmetric_targets();asymmetric_targets(true);
    negotiated_trace(false,2,false);negotiated_trace(true,2,false);
    negotiated_trace(true,2,true);negotiated_trace(true,10,true);
    negotiated_trace(true,2,true,true);negotiated_trace(true,2,false,false,true);
    negotiated_trace(true,2,false,true,false,true);
    std::cout<<"Online status codecs, origin/scope cleanup, edge retention and live delay traces passed\n";
}
