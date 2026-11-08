#pragma once
#include "control.hpp"
#include <algorithm>
#include <array>
#include <cstdio>

namespace dkr::online_stability {
enum class Pause : unsigned { Pacing, Network, LocalInput, Confirmation, Presentation,
    Replay, ScenePeers, ScenePreparation, SpectatorBaseline, Admission, Running, Count };
template<class Step>Pause pause_reason(Step step,bool due,
                          std::uint8_t missing,unsigned local) {
    if(step==Step::Replayed)return Pause::Replay;
    if(step==Step::WaitingForPresentation)return Pause::Presentation;
    if(step==Step::WaitingForConfirmedInput)return Pause::Confirmation;
    if(step==Step::PredictionLimit||step==Step::WaitingForLocalInput) {
        if(!due)return Pause::Pacing;
        return (missing&std::uint8_t(~(local<4?1U<<local:0U)))?Pause::Network:Pause::LocalInput;
    }
    return Pause::Running;
}
// One pulse always services transport. Only an unfinished replay can request
// another immediately; neither pacing nor GPU waits spin. The clock budget is
// checked between ticks because an individual guest tick cannot be preempted.
class ReplaySlice final {
    Clock::time_point start_;unsigned pulses_=0;
public:
    explicit ReplaySlice(Clock::time_point start):start_(start){}
    bool again(bool replay_ready,Clock::time_point now) {
        return ++pulses_<4&&replay_ready&&now>=start_&&now-start_<std::chrono::milliseconds(4);
    }
};
class PacingTrace final {
    std::array<std::uint64_t,unsigned(Pause::Count)> idle_us_{};
    Clock::time_point ended_{},report_{};
    Pause reason_=Pause::Admission;
    std::uint64_t ticks_=0,replays_=0,tick_us_=0,max_tick_us_=0,replay_us_=0,scheduling_us_=0;
public:
    void begin(Clock::time_point now) {
        if(ended_.time_since_epoch().count()&&now>=ended_) {
            const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now-ended_).count());
            // A descheduled worker is distinguishable from a measured network
            // wait; don't charge a long CPU scheduling gap to the remote peer.
            idle_us_[unsigned(reason_)]+=std::min<std::uint64_t>(us,50000);
            if(us>50000)scheduling_us_+=us-50000;
        }
    }
    void reason(Pause value){reason_=value;}
    Pause reason()const{return reason_;}
    template<class Step>void step(Step value,std::uint64_t us) {
        if(value!=Step::Advanced&&value!=Step::Replayed)return;
        ++ticks_;tick_us_+=us;max_tick_us_=std::max(max_tick_us_,us);
        if(value==Step::Replayed){++replays_;replay_us_+=us;}
    }
    void end(Clock::time_point now){ended_=now;}
    std::uint64_t idle(Pause value)const{return idle_us_[unsigned(value)];}
    std::uint64_t ticks()const{return ticks_;}
    std::uint64_t tick_us()const{return tick_us_;}
    void report(std::uint64_t epoch,unsigned frame,unsigned confirmed,Clock::time_point now) {
        if(report_.time_since_epoch().count()&&now-report_<std::chrono::seconds(5))return;
        constexpr const char* names[]{"pacing","network-input","local-input","confirmed-input","presentation","replay","scene-peers","scene-preparation","spectator-baseline","admission","running"};
        static_assert(std::size(names)==unsigned(Pause::Count));
        std::fprintf(stderr,"[party][pacing] epoch=%llu frame=%u confirmed=%u reason=%s executed=%llu replayed=%llu executed-mean-us=%llu executed-max-us=%llu replay-us=%llu descheduled-us=%llu",
            static_cast<unsigned long long>(epoch),frame,confirmed,names[unsigned(reason_)],
            static_cast<unsigned long long>(ticks_),static_cast<unsigned long long>(replays_),
            static_cast<unsigned long long>(ticks_?tick_us_/ticks_:0),static_cast<unsigned long long>(max_tick_us_),
            static_cast<unsigned long long>(replay_us_),static_cast<unsigned long long>(scheduling_us_));
        for(unsigned n=0;n<unsigned(Pause::Count);++n)std::fprintf(stderr," %s-idle-us=%llu",names[n],static_cast<unsigned long long>(idle_us_[n]));
        std::fputc('\n',stderr);report_=now;idle_us_={};ticks_=replays_=tick_us_=max_tick_us_=replay_us_=scheduling_us_=0;
    }
};
}
