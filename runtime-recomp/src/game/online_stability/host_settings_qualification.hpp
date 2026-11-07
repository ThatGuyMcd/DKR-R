#pragma once
#include "control.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace dkr::online_stability {
// Enabled only inside the existing disposable-profile owned-game check.
class HostSettingsQualification final {
    struct Change {unsigned seconds,window;bool automatic;unsigned delay;};
    static constexpr std::array<Change,5> Changes{{{5,20,true,1},{25,2,true,1},{45,20,false,3},{65,6,false,0},{85,20,true,1}}};
    bool enabled_=false,client_checked_=false,finished_=false;
    unsigned stage_=0;std::uint64_t waiting_=0;
    Clock::time_point start_{};
public:
    explicit HostSettingsQualification(bool disposable) {
        const char* value=disposable?std::getenv("DKR_OWNED_CHECK_HOST_SETTINGS"):nullptr;
        enabled_=value&&std::string_view(value)=="1";
    }
    void pulse(const void* key,bool host,std::uint64_t epoch,Clock::time_point now) {
        if(!enabled_||finished_||epoch<92)return;
        auto bridge=control(key);const auto identity=bridge->identity();const auto view=bridge->host_settings();
        if(!host) {
            if(client_checked_)return;
            std::uint64_t serial=0;std::string error;
            if(bridge->request_host_settings(identity.room,{},serial,error))throw std::runtime_error("A client changed host settings.");
            client_checked_=true;std::fprintf(stderr,"[online][host-settings-check] client-denied=1\n");return;
        }
        if(!start_.time_since_epoch().count())start_=now;
        if(waiting_) {
            if(view.reply!=waiting_)return;
            if(!view.accepted)throw std::runtime_error("Host settings check rejected: "+view.status);
            waiting_=0;++stage_;
            if(stage_==Changes.size()){finished_=true;std::fprintf(stderr,"[online][host-settings-check] completed=5 queued=%u\n",unsigned(bool(view.queued)));return;}
        }
        const auto change=Changes[stage_];
        if(now-start_<std::chrono::seconds(change.seconds)||!view.available||view.context.transition||!view.context.playing)return;
        auto desired=view.queued.value_or(view.current);
        desired.prediction_window=change.window;desired.automatic_delay=change.automatic;desired.input_delay=change.delay;
        if(view.context.spectator_capable&&(stage_==2||stage_==3))desired.spectators=stage_==3;
        if(stage_==0){desired.menu_control=(view.current.menu_control+1)%3;desired.record_replay=!view.current.record_replay;}
        if(stage_==4){desired.menu_control=view.current.menu_control;desired.record_replay=view.current.record_replay;}
        std::string error;
        if(!bridge->request_host_settings(view.room,desired,waiting_,error))throw std::runtime_error("Host settings check could not submit: "+error);
        std::fprintf(stderr,"[online][host-settings-check] stage=%u request=%llu window=%u auto=%u delay=%u epoch=%llu\n",stage_,
            static_cast<unsigned long long>(waiting_),change.window,unsigned(change.automatic),change.delay,static_cast<unsigned long long>(epoch));
    }
};
}
