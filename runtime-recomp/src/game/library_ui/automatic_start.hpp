#pragma once
#include "startup_progress.hpp"
#include <string_view>
#include <utility>

namespace dkr::library_ui {
// Local frontend preference; never a lobby setting or guest checkpoint.
inline bool skip_launcher = false;
inline bool startup_screen_seen = false;
inline bool parse_skip_launcher(std::string_view value) {
    if (!value.empty() && value.back() == '\r')
        value.remove_suffix(1);
    return value == "1";
}
class AutomaticStart {
    enum class State { Off, Waiting, Preparing };
    State state_ = State::Off;
    bool loading_mods_ = false;

  public:
    enum class Action { None, Start, ShowLauncher };
    struct Conditions {
        bool rom_ready = false, online = false, operation_busy = false;
        bool modal = false, preparing = false, failed = false;
    };
    AutomaticStart(bool enabled, bool& seen, bool returning, bool recovery) {
        const bool first = !std::exchange(seen, true);
        if (enabled && first && !returning && !recovery)
            state_ = State::Waiting;
    }
    bool active() const {
        return state_ != State::Off;
    }
    void manual(bool mods) {
        state_ = State::Preparing;
        loading_mods_ = mods;
    }
    void loading_mods(bool enabled) {
        loading_mods_ = enabled;
    }
    bool loading_mods() const {
        return loading_mods_;
    }
    void cancel() {
        state_ = State::Off;
    }
    Action poll(Conditions c) {
        if (!active())
            return Action::None;
        if (!c.rom_ready || c.online || c.failed) {
            cancel();
            return Action::ShowLauncher;
        }
        if (state_ == State::Waiting) {
            // Existing preparation/approval modals always take precedence.
            if (c.modal) {
                cancel();
                return Action::ShowLauncher;
            }
            if (c.operation_busy)
                return Action::None;
            state_ = State::Preparing;
            return Action::Start;
        }
        if (!c.modal && !c.preparing) {
            cancel();
            return Action::ShowLauncher;
        }
        return Action::None;
    }
};
} // namespace dkr::library_ui
