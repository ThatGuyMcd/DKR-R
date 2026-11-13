#pragma once

#include "netplay/experimental_draw_events.h"
#include <algorithm>
#include <span>
#include <utility>

namespace dkr::online_stability {
enum class ViewReason : unsigned { Ready, Session, Malformed, Layout, Menu, SharedScene,
    Players, RaceType, MixedRace, Duplicate, Incomplete };
struct ViewEligibility {
    bool eligible=false; unsigned race_type=0, layout=0;
    ViewReason reason=ViewReason::Ready;
};
inline ViewEligibility view_eligibility(unsigned owner,unsigned players,std::span<const dkr_owned_draw_event> events) {
    ViewEligibility result;
    const auto reject=[](ViewReason reason){ViewEligibility value;value.reason=reason;return value;};
    if(players<2||players>4||owner>=players)return reject(ViewReason::Session);
    unsigned mask=0;bool shared=false,extra=false,first=true;
    for(const auto& event:events) {
        if(event.kind==DKR_OWNED_ONLINE_SHARED)shared=true;
        if(event.kind!=DKR_OWNED_ONLINE_VIEW)continue;
        const auto race=event.parameters[2];
        if(!dkr_owned_draw_event_valid(&event))return reject(ViewReason::Malformed);
        if(event.parameters[0]!=players-1)return reject(ViewReason::Layout);
        if(event.parameters[1]!=0)return reject(ViewReason::Menu);
        if(event.parameters[3]!=0)return reject(ViewReason::SharedScene);
        if(event.parameters[4]!=players)return reject(ViewReason::Players);
        if(race!=0&&race!=3&&race!=64&&race!=65&&race!=66)return reject(ViewReason::RaceType);
        if(!first&&result.race_type!=race)return reject(ViewReason::MixedRace);
        first=false;result.race_type=race;result.layout=event.parameters[0];
        if(event.token<players) {
            if(mask&(1U<<event.token))return reject(ViewReason::Duplicate);
            mask|=1U<<event.token;
        } else if(players!=3||event.token!=3||std::exchange(extra,true))return reject(ViewReason::Duplicate);
    }
    result.eligible=shared&&!first&&mask==((1U<<players)-1);
    if(!result.eligible)result.reason=ViewReason::Incomplete;
    return result;
}
template<class Plan,class Labels>
Plan focus_plan(Plan plan,const ViewEligibility& decision,unsigned owner,bool focus,Labels labels,unsigned size) {
    if(!decision.eligible)return {};
    plan.eligible=true;plan.focus=focus;plan.owner=owner;plan.layout=decision.layout;
    plan.labels=labels;plan.label_size=std::clamp(size,75U,150U);return plan;
}
} // namespace dkr::online_stability
