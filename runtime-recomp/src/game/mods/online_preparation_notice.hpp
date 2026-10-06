#pragma once
#include <string>
#include <string_view>

namespace dkr::mods::online {
// UI-owned snapshot, separate from the short-lived worker transaction. A
// cancellation/idle snapshot must not erase a failure before the user reads it.
struct PreparationNotice {
    std::string stage,error,revision,backend;
    void progress(std::string_view value) { if(!value.empty())stage=value; }
    bool fail(std::string_view value,std::string_view game_pak,std::string_view mode) {
        if(value.empty() || error==value)return false;
        error=value;revision=game_pak;backend=mode;return true;
    }
    bool visible()const {return !error.empty();}
    void clear(){*this={};}
};
}
