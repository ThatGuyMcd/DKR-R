#pragma once
#include "service.hpp"
#include <ostream>
namespace dkr::feedback {
inline constexpr std::array<const char*, 15> SettingNames{
    "mode",       "strength", "balance",    "impacts",      "boost",    "environment",  "engine",  "menus",
    "resistance", "lights",   "brightness", "deck_pattern", "waveform", "touch_action", "reserved"};
inline std::array<int*, 14> fields(Settings& s) {
    return {&s.mode,  &s.strength,   &s.balance, &s.impacts,    &s.boost,        &s.environment, &s.engine,
            &s.menus, &s.resistance, &s.lights,  &s.brightness, &s.deck_pattern, &s.waveform,    &s.touch_action};
}
inline bool load_setting(const std::string& key, int value) {
    for (unsigned p = 0; p < 4; ++p)
        for (unsigned k = 0; k < 4; ++k) {
            const auto prefix = "feedback_p" + std::to_string(p + 1) + "_k" + std::to_string(k) + "_";
            if (!key.starts_with(prefix))
                continue;
            auto s = service().stored(p, k);
            auto f = fields(s);
            for (unsigned i = 0; i < f.size(); ++i)
                if (key == prefix + SettingNames[i]) {
                    *f[i] = i == 12 ? 0 : value;
                    service().stored(p, k, s);
                    return true;
                }
            return false;
        }
    return false;
}
inline void save_settings(std::ostream& out) {
    for (unsigned p = 0; p < 4; ++p)
        for (unsigned k = 0; k < 4; ++k) {
            auto s = service().stored(p, k);
            auto f = fields(s);
            const auto prefix = "feedback_p" + std::to_string(p + 1) + "_k" + std::to_string(k) + "_";
            for (unsigned i = 0; i < f.size(); ++i)
                out << prefix << SettingNames[i] << '=' << (i == 12 ? 0 : *f[i]) << '\n';
        }
}
} // namespace dkr::feedback
