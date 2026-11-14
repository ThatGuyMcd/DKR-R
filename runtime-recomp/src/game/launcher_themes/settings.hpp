#pragma once
#include "policy.hpp"
#include <ostream>

namespace dkr::launcher_themes {
inline Settings preferences;
inline Theme displayed = Theme::Original;
inline Date next_switch{};
inline bool legacy_static_background = false;
inline bool load(std::string_view key, int value) {
    // Consume old choices without allowing them to override the detected
    // calendar. Retain an existing motion opt-out for accessibility.
    if (key == "launcher_theme_animate")
        legacy_static_background = value == 0;
    else if (key != "launcher_theme_mode" && key != "launcher_theme_manual" && key != "launcher_theme_calendar" &&
             key != "launcher_theme_southern" && key != "launcher_theme_seasons" && key != "launcher_theme_events")
        return false;
    return true;
}
inline void save(std::ostream& output) {
    if (legacy_static_background)
        output << "launcher_theme_animate=0\n";
}
} // namespace dkr::launcher_themes
