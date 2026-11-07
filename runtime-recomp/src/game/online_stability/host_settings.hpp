#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace dkr::online_stability {
struct HostSettings {
    unsigned menu_control=0,maximum_players=4,prediction_window=14,input_delay=1;
    bool automatic_delay=true,record_replay=true,spectators=true,development=false;
    bool operator==(const HostSettings&)const=default;
};
struct HostSettingsContext {
    bool playing=false,spectator_capable=false,transition=false;
    unsigned occupied_ports=1;
};
struct HostSettingsPlan {
    HostSettings immediate;
    std::optional<HostSettings> queued;
    std::string error;
    explicit operator bool()const{return error.empty();}
};
inline HostSettingsPlan plan_host_settings(HostSettings current,HostSettings desired,HostSettingsContext context) {
    HostSettingsPlan result{current,{},{}};
    if(desired.menu_control>2||desired.maximum_players<2||desired.maximum_players>4||
       desired.prediction_window<2||desired.prediction_window>20||desired.input_delay>9)
        result.error="Choose a supported player count, prediction window and input delay.";
    else if(context.transition)result.error="Wait for the game to finish loading or returning to the lobby, then apply the settings.";
    else if(desired.maximum_players<context.occupied_ports)result.error="Maximum players cannot be lower than the occupied racing places.";
    else if(desired.development!=current.development)result.error="Create a new lobby to change development mode. Existing save ownership and player consent are preserved.";
    else if(!context.spectator_capable&&desired.spectators!=current.spectators)
        result.error="Create a new spectator-capable lobby to enable spectators.";
    if(!result)return result;
    result.immediate=desired;
    if(context.playing) {
        result.immediate.maximum_players=current.maximum_players;
        result.immediate.menu_control=current.menu_control;
        result.immediate.record_replay=current.record_replay;
        if(result.immediate!=desired)result.queued=desired;
    }
    return result;
}
struct HostSettingsRequest {std::uint64_t room=0,serial=0;HostSettings desired;};
struct HostSettingsView {
    std::uint64_t room=0,reply=0;
    bool available=false,accepted=false;
    HostSettings current,submitted;
    HostSettingsContext context;
    std::optional<HostSettings> queued;
    std::string status;
};
}
