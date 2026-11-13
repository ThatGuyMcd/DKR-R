#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace dkr::online_stability {
// Presentation pacing only. Auto uses the authenticated host's monotonic
// simulation frontier. Manual mode can infer it from its fixed input lead.
// Neither game time nor any published input is rewritten.
class PeerClock final {
public:
    using Clock=std::chrono::steady_clock;
    inline static constexpr auto Tick=std::chrono::nanoseconds(33333333);
    void reset(){*this={};}
    void observe(std::uint32_t sample,unsigned lead,unsigned rtt_ms,Clock::time_point now) {
        if(sample<lead)return;
        observe_frame(sample,sample-lead+1.,rtt_ms,now);
    }
    void observe_frontier(std::uint32_t frame,unsigned rtt_ms,Clock::time_point now) {
        observe_frame(frame,double(frame),rtt_ms,now,true);
    }
    std::uint32_t reference_frame()const{return sample_;}
    unsigned age_ms(Clock::time_point now)const {
        return valid_&&now>=at_?unsigned(std::min<std::int64_t>(65535,std::chrono::duration_cast<std::chrono::milliseconds>(now-at_).count())):65535;
    }
private:
    void observe_frame(std::uint32_t sample,double frontier,unsigned rtt_ms,Clock::time_point now,bool explicit_frontier=false) {
        if(rtt_ms>5000||(valid_&&(sample<sample_||(!explicit_frontier&&sample==sample_)||now<at_)))return;
        const auto second=std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        auto& bucket=rtts_[std::uint64_t(second)%rtts_.size()];
        if(bucket.second!=second)bucket={second,rtt_ms};
        else if(rtt_ms)bucket.minimum=bucket.minimum?std::min(bucket.minimum,rtt_ms):rtt_ms;
        unsigned floor=rtt_ms;
        for(const auto& row:rtts_)if(row.minimum&&second>=row.second&&second-row.second<10)
            floor=floor?std::min(floor,row.minimum):row.minimum;
        // Application-level pings can include transient reliable-lane queues.
        // A recent RTT floor avoids mistaking that queue for clock drift.
        reference_rtt_ms_=floor;
        const double estimate=frontier+double(floor)*.015;
        // A fresh explicit report can show that the host stopped advancing.
        // Extrapolating the old reference through that report would leave a
        // client permanently ahead after an outage. Only inferred input clocks
        // need the older smoothing; the period remains bounded in either case.
        frame_=valid_&&!explicit_frontier?std::clamp(estimate,projected(now)-2.,projected(now)+2.):estimate;
        sample_=sample;at_=now;valid_=true;
    }
public:
    std::optional<double> error(std::uint32_t local,Clock::time_point now)const {
        if(!valid_||now<at_||now-at_>std::chrono::seconds(2))return {};
        return projected(now)-double(local);
    }
    std::chrono::nanoseconds period(std::uint32_t local,Clock::time_point now)const {
        const auto phase=error(local,now);if(!phase)return Tick;
        // A 1.5-frame deadband absorbs packet scheduling variation. Correct
        // remaining phase at no more than five percent, with every tick intact.
        const double excess=*phase>1.5?*phase-1.5:*phase< -1.5?*phase+1.5:0.;
        const auto adjustment=std::chrono::nanoseconds(std::int64_t(std::clamp(excess*.0005,-.001666666,.001666666)*1.e9));
        return Tick-adjustment;
    }
    unsigned reference_rtt_ms()const{return reference_rtt_ms_;}
private:
    double projected(Clock::time_point now)const{return frame_+std::chrono::duration<double>(now-at_).count()*30.;}
    Clock::time_point at_{};
    std::uint32_t sample_=0;
    double frame_=0;
    bool valid_=false;
    unsigned reference_rtt_ms_=0;
    struct Rtt {std::int64_t second=-1;unsigned minimum=0;};
    std::array<Rtt,12> rtts_{};
};
}
