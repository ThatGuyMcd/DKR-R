#pragma once
#include "policy.hpp"
#include <string>

namespace dkr::launcher_themes {
// Parse a territory, not a display language. C/POSIX and numeric world
// regions deliberately remain unspecified; English alone does not mean US.
inline std::string country_from_locale(std::string_view value) {
    const auto end = value.find_first_of(".@");
    value = value.substr(0, end);
    const auto letters = [](std::string_view part) {
        if (part.size() != 2)
            return false;
        for (char c : part)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    };
    // Direct ISO codes are used by Windows' configured country API.
    if (value.size() == 2 && value[0] >= 'A' && value[0] <= 'Z' && value[1] >= 'A' && value[1] <= 'Z')
        return value == "XX" || value == "ZZ" ? std::string{} : std::string(value);
    std::string result;
    bool territory = false;
    while (!value.empty()) {
        const auto split = value.find_first_of("_-");
        const auto part = value.substr(0, split);
        if (territory && letters(part)) {
            result.assign(part);
            for (char& c : result)
                if (c >= 'a' && c <= 'z')
                    c -= 'a' - 'A';
        }
        if (split == value.npos)
            break;
        territory = true;
        value.remove_prefix(split + 1);
    }
    return result == "XX" || result == "ZZ" ? std::string{} : result;
}
inline std::string locale_country(std::string_view all, std::string_view address, std::string_view time,
                                  std::string_view language) {
    if (auto country = country_from_locale(all); !country.empty())
        return country;
    // A separate regional format wins over the language of UI messages.
    for (const auto value : {address, time, language})
        if (auto country = country_from_locale(value); !country.empty())
            return country;
    return {};
}
inline bool listed_country(std::string_view country, std::string_view list) {
    for (std::size_t at = 0; at + 2 <= list.size(); at += 3)
        if (list.substr(at, 2) == country)
            return true;
    return false;
}
inline Settings settings_for_country(std::string_view country, bool animate = true) {
    if (country.size() != 2 || country[0] < 'A' || country[0] > 'Z' || country[1] < 'A' || country[1] > 'Z' ||
        country == "XX" || country == "ZZ")
        country = {};
    Settings s;
    s.mode = Mode::Automatic;
    s.calendar = country == "GB"   ? Calendar::UK
                 : country == "US" ? Calendar::US
                 : country == "CA" ? Calendar::Canada
                                   : Calendar::International;
    s.animate = animate;
    s.seasons = country.size() == 2;
    s.southern = listed_country(
        country, "AU NZ ZA AR CL UY BR PE BO PY AO BW FJ FK LS MG MU MW MZ NA NC NF NU PG SC SZ TL TO TZ VU WS ZM ZW");
    s.events = CoreEvents;
    if (!listed_country(country, "GB IE US CA AU NZ"))
        s.events &= ~(1U << int(Theme::Patrick));
    if (listed_country(country, "CN HK MO TW KR VN SG MY"))
        s.events |= 1U << int(Theme::Lunar);
    if (listed_country(country, "IN SG MY MU FJ NP"))
        s.events |= 1U << int(Theme::Diwali);
    if (listed_country(country, "AE BH BD BN DZ EG ID IN IQ IR JO KW LB LY MA MY OM PK PS QA SA SD SG TN TR YE"))
        s.events |= (1U << int(Theme::EidFitr)) | (1U << int(Theme::EidAdha));
    if (country == "IL")
        s.events |= 1U << int(Theme::Hanukkah);
    if (s.calendar == Calendar::US || s.calendar == Calendar::Canada)
        s.events |= (1U << int(Theme::Thanksgiving)) | (1U << int(Theme::National));
    // International holidays remain available without inventing a country or
    // hemisphere. Regional celebrations still require an identified country.
    return s;
}
} // namespace dkr::launcher_themes
