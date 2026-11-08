#pragma once
#include <atomic>
#include <optional>

namespace dkr::session_content {
inline std::atomic<bool> next_enabled{true};
inline std::atomic<bool> running_enabled{true},running_online{false},running_active{false};
inline std::atomic<bool> host_enabled{true},running_modded{false};
inline thread_local std::optional<bool> preparation;
struct Preparation {
    std::optional<bool> previous;
    explicit Preparation(bool enabled):previous(preparation){preparation=enabled;}
    ~Preparation(){preparation=previous;}
};
inline bool preparing(){return preparation.value_or(next_enabled.load());}
inline void begin(bool enabled,bool online) {
    running_enabled=enabled;running_online=online;running_modded=false;running_active=true;
}
inline void end(){running_modded=false;running_active=false;running_enabled=true;running_online=false;}
inline bool local_assets_allowed(){return !running_active||(!running_online&&running_enabled);}
inline bool textures_allowed(){return !running_active||running_enabled;}
// The loop owns this scope before preparation and retires it on every exit,
// including a failed launch, a return to the launcher, and process shutdown.
struct Session {
    Session(bool enabled,bool online){begin(enabled,online);}
    ~Session(){end();}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
};
}
