#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "surface_policy.hpp"

namespace dkr::library_ui {
struct SidebarPlacement { float navigation, actions; };
inline SidebarPlacement sidebar_placement(float height,float padding,float after_brand,
        float button,float spacing,float section_gap,float action_gap,std::size_t pages) {
    const float navigation=pages*button+(pages?pages-1:0)*spacing;
    const float actions=2.F*button+action_gap+2.F*spacing;
    const float separation=NavigationToggleHeight+section_gap+2.F*spacing;
    // Keep the logo clear on short windows. At normal sizes the page group is
    // centred in the rail, while Restart/Exit keep the bottom padding.
    const float action_y=std::max(height-padding-actions,after_brand+navigation+separation);
    return {std::clamp((height-navigation)*.5F,after_brand,action_y-navigation-separation),action_y};
}
// UI clocks only. Neither carousel time nor page motion enters game state.
struct PageMotion {
    std::string route;
    int frame=-100;
    double started=-10;
    void observe(std::string next,int current,double now) {
        if(next!=route||current>frame+1||current<frame)started=now;
        route=std::move(next);frame=current;
    }
    float progress(double now,float relative_y)const {
        const double delay=.12*std::clamp(double(relative_y)/550.0,0.0,1.0);
        return float(std::clamp((now-started-delay)/.32,0.0,1.0));
    }
    bool active(double now)const{return now-started<.44;}
};
struct FeaturedRotation {
    std::vector<std::string> ids;
    std::size_t index=0,previous=0;
    bool paused=false;
    double remaining=30,last=-1,changed=-10;
    int frame=-100;
    void sync(std::vector<std::string> next) {
        if(next==ids)return;
        const auto selected=ids.empty()?std::string{}:ids[index];
        ids=std::move(next);
        const auto found=std::find(ids.begin(),ids.end(),selected);
        index=found==ids.end()?0:std::size_t(found-ids.begin());
        previous=index;remaining=30;changed=-10;
    }
    void step(int direction,double now) {
        if(ids.size()<2)return;
        previous=index;
        index=direction<0?(index+ids.size()-1)%ids.size():(index+1)%ids.size();
        remaining=30;changed=now;
    }
    void tick(double now,int current,bool suspended) {
        const double elapsed=last>=0&&current==frame+1?std::clamp(now-last,0.0,.25):0;
        last=now;frame=current;
        if(paused||suspended||ids.size()<2)return;
        remaining-=elapsed;
        if(remaining<=0)step(1,now);
    }
    float blend(double now)const{return float(std::clamp((now-changed)/.45,0.0,1.0));}
};
} // namespace dkr::library_ui
