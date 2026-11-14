#pragma once
#include "policy.hpp"

namespace dkr::launcher_themes {
inline constexpr std::string_view greeting(Theme theme, Calendar calendar) {
    switch (theme) {
    case Theme::Halloween:
        return "Happy Halloween, racers!";
    case Theme::Bonfire:
        return "Have a sparkling Bonfire Night!";
    case Theme::Christmas:
        return "Merry Christmas, racers!";
    case Theme::NewYear:
        return "Happy New Year, racers!";
    case Theme::Valentine:
        return "Happy Valentine's Day!";
    case Theme::Patrick:
        return "Happy St Patrick's Day!";
    case Theme::Easter:
        return "Happy Easter, racers!";
    case Theme::Lunar:
        return "Happy Lunar New Year!";
    case Theme::Diwali:
        return "Wishing you a bright and happy Diwali!";
    case Theme::EidFitr:
    case Theme::EidAdha:
        return "Eid Mubarak, racers!";
    case Theme::Hanukkah:
        return "Happy Hanukkah!";
    case Theme::Thanksgiving:
        return "Happy Thanksgiving!";
    case Theme::National:
        return calendar == Calendar::US       ? "Happy Independence Day!"
               : calendar == Calendar::Canada ? "Happy Canada Day!"
                                              : "";
    default:
        return {};
    }
}
} // namespace dkr::launcher_themes
