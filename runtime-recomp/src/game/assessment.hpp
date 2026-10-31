#pragma once
#include "rollback_check.hpp"
#include <algorithm>
#include <cmath>

namespace dkr::runtime::rollback_check {
inline constexpr unsigned ScoringVersion=1,MinimumRacerReplies=90,MinimumSpectatorReplies=60;
struct Assessment {unsigned score=0;const char* label="Not rated";const char* reason="Run a fresh connection check.";bool rated()const{return score!=0;}};
inline constexpr unsigned weighted_grade(unsigned timing,unsigned reliability,unsigned consistency,unsigned latency){
    return std::clamp((35*timing+35*reliability+20*consistency+10*latency+50)/100,1U,10U);
}
static_assert(weighted_grade(10,10,10,10)==10 && weighted_grade(1,1,1,1)==1);
static_assert(weighted_grade(8,5,8,8)<weighted_grade(8,9,8,8));
inline double arrival_budget(const View& v){return (v.delay+(v.window>2?v.window-2:0))*1000./30.;}
inline const char* grade_label(unsigned score){return score>=9?"Excellent":score>=7?"Good":score>=5?"Mixed":score>=3?"Poor":"Very poor";}
inline unsigned latency_grade(double ms){return ms<=40?10:ms<=80?9:ms<=120?8:ms<=180?7:ms<=250?5:ms<=400?3:1;}
inline unsigned consistency_grade(double ms){return ms<=4?10:ms<=8?9:ms<=16?8:ms<=25?6:ms<=40?4:ms<=60?2:1;}
inline unsigned reliability_grade(double missing){return missing<=.5?10:missing<=1?9:missing<=2?8:missing<=5?6:missing<=10?4:2;}
inline unsigned timing_grade(double frames){return frames>=3?10:frames>=2?9:frames>=1?8:frames>=0?7:frames>=-1?5:frames>=-2?3:1;}
inline unsigned playback_grade(double ms){return ms<=80?10:ms<=120?9:ms<=180?8:ms<=250?7:ms<=350?5:ms<=500?3:1;}
inline Assessment remote_assessment(const View& v,const Row& r){
    if(!v.complete)return {0,"Not rated","This report is incomplete or no longer current. Run a fresh check."};
    if(!r.supported)return {0,"Not rated","No compatible diagnostic acknowledgement. Update this participant's build and retry."};
    const unsigned minimum=r.spectator?MinimumSpectatorReplies:MinimumRacerReplies;
    if(r.replies<minimum||r.sent<minimum||r.replies>r.sent||
       (!r.spectator&&!v.party&&r.repair_replies<60))
        return {0,"Not rated","Not enough completed probes to give a reliable score. Please re-run the test."};
    unsigned total=0;for(const auto& row:v.rows)if(!row.host)total+=row.sent+row.repair_sent;
    if(r.blocked>std::max(4U,r.sent/20)||r.errors||v.scheduler_gaps>std::max(3U,total/20)||
       v.final_queued>64*1024||v.peak_queued>64*1024)
        return {0,"Retest when quiet","Local scheduling or queue pressure affected this check. This is not a bad-connection verdict."};
    if(!std::isfinite(r.p95)||!std::isfinite(r.median)||!std::isfinite(r.jitter)||r.median<0||r.p95<r.median||r.jitter<0)
        return {0,"Not rated","The timing measurements are incomplete. Please re-run the test."};
    double missing=100.*double(r.sent-r.replies)/r.sent;
    if(r.repair_sent)missing=std::max(missing,100.*double(r.repair_sent-r.repair_replies)/r.repair_sent);
    const double p95=std::max(r.p95,r.repair_p95);
    const double headroom=arrival_budget(v)-(p95+8.);
    const unsigned timing=r.spectator?playback_grade(p95):timing_grade(headroom*30./1000.);
    unsigned score=weighted_grade(timing,reliability_grade(missing),consistency_grade(r.jitter),latency_grade(r.median));
    if(missing>10)score=std::min(score,3U);else if(missing>5)score=std::min(score,5U);else if(missing>2)score=std::min(score,7U);
    if(!r.spectator){if(headroom<0)score=std::min(score,4U);if(p95>400)score=std::min(score,5U);else if(p95>250)score=std::min(score,7U);}
    const char* reason=r.spectator?"Steady playback timing expected; checkpoint catch-up speed is not benchmarked.":"Stable timing. Smooth racing is expected with the tested settings.";
    if(missing>2)reason=r.spectator?"Missing playback probe replies. Viewing may pause; racers are assessed separately.":"Missing probe replies may cause corrections or waiting. Check the connection and retry.";
    else if(!r.spectator&&headroom<0)reason="Timing spikes exceed the conservative Rollback allowance. More delay may help, at a responsiveness cost.";
    else if(r.jitter>16)reason=r.spectator?"Variable playback timing. You may pause or need to catch up.":"Some timing spikes. Rollback corrections or brief waiting are possible.";
    else if(p95>180)reason=r.spectator?"Slower playback timing. Some pauses or catch-up may occur.":"A longer round trip reduces timing headroom. Expect a less immediate online experience.";
    return {score,grade_label(score),reason};
}
inline Assessment race_assessment(const View& v){
    Assessment result{10,"Excellent","Stable racer routes. Smooth racing is expected with the tested settings."};bool found=false;
    for(const auto& r:v.rows)if(!r.host&&!r.spectator){
        const auto a=remote_assessment(v,r);if(!a.rated())return {0,"Incomplete","At least one racer is not rated. Re-run the check before relying on a lobby score."};
        found=true;if(a.score<result.score)result=a;
    }
    if(!found)return {0,"No remote racers","There is no remote racer route to assess. Spectator playback is listed separately."};
    return result;
}
inline Assessment assessment(const View& v,const Row& r){return r.host?race_assessment(v):remote_assessment(v,r);}
}
