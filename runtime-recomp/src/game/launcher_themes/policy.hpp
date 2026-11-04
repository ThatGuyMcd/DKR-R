#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace dkr::launcher_themes {
enum class Theme : int {
    Original, Winter, Spring, Summer, Autumn, Halloween, Bonfire, Christmas,
    NewYear, Valentine, Patrick, Easter, Lunar, Diwali, EidFitr, EidAdha,
    Hanukkah, Thanksgiving, National, Count
};
enum class Mode : int { Automatic, Original, Manual };
enum class Calendar : int { UK, International, US, Canada };
inline constexpr std::array<const char*, int(Theme::Count)> Names{
    "Original", "Winter", "Spring", "Summer", "Autumn", "Halloween", "Bonfire Night",
    "Christmas", "New Year", "Valentine's Day", "St Patrick's Day", "Easter",
    "Lunar New Year", "Diwali", "Eid al-Fitr", "Eid al-Adha", "Hanukkah",
    "Thanksgiving", "National celebrations"
};
inline constexpr std::uint32_t EventBits = ((1U << int(Theme::Count)) - 1U) & ~31U;
inline constexpr std::uint32_t CoreEvents = ((1U << int(Theme::Lunar)) - 1U) & ~31U;
struct Settings {
    Mode mode = Mode::Automatic;
    Theme manual = Theme::Halloween;
    Calendar calendar = Calendar::UK;
    bool southern = false;
    bool seasons = true;
    bool animate = true;
    std::uint32_t events = CoreEvents;
    bool operator==(const Settings&) const = default;
};
using Date = std::chrono::year_month_day;
using Day = std::chrono::sys_days;
inline Date date(int year, unsigned month, unsigned day) {
    return {std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
}
inline Date plus(Date value, int days) { return Date{Day{value} + std::chrono::days{days}}; }
inline bool within(Date value, Date start, Date end) {
    return value.ok() && Day{value} >= Day{start} && Day{value} <= Day{end};
}
inline Date easter(int year) {
    // Gregorian computus (Meeus/Jones/Butcher), valid Gregorian civil years.
    const int a = year % 19, b = year / 100, c = year % 100;
    const int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451, n = h + l - 7 * m + 114;
    return date(year, unsigned(n / 31), unsigned(n % 31 + 1));
}
inline Date weekday_in(int year, unsigned month, unsigned weekday, unsigned ordinal) {
    const Day first{date(year, month, 1)};
    const unsigned offset = (weekday + 7 - std::chrono::weekday{first}.c_encoding()) % 7;
    return Date{first + std::chrono::days{offset + (ordinal - 1) * 7}};
}
struct Festival { Theme theme; Date start; Date end; };
// Sources, locale assumptions and bounded coverage: docs/LAUNCHER_THEMES.md.
// No approximate lunar arithmetic or invented dates after published coverage.
inline const std::array<Festival, 15> Festivals{{
    {Theme::Lunar, date(2026,2,14), date(2026,2,22)},
    {Theme::Lunar, date(2027,2,3), date(2027,2,11)},
    {Theme::Diwali, date(2026,11,6), date(2026,11,10)},
    {Theme::Diwali, date(2027,10,26), date(2027,10,30)},
    {Theme::EidFitr, date(2026,3,21), date(2026,3,23)},
    {Theme::EidFitr, date(2027,3,10), date(2027,3,12)},
    {Theme::EidAdha, date(2026,5,27), date(2026,5,30)},
    {Theme::EidAdha, date(2027,5,17), date(2027,5,20)},
    {Theme::Hanukkah, date(2026,12,4), date(2026,12,12)},
    {Theme::Hanukkah, date(2027,12,24), date(2028,1,1)},
    {Theme::Hanukkah, date(2028,12,12), date(2028,12,20)},
    {Theme::Hanukkah, date(2029,12,1), date(2029,12,9)},
    {Theme::Hanukkah, date(2030,12,20), date(2030,12,28)},
    {Theme::Hanukkah, date(2031,12,9), date(2031,12,17)},
    {Theme::Hanukkah, date(2032,11,27), date(2032,12,5)}
}};
inline Theme automatic(const Settings& s, Date today) {
    if (!today.ok()) return Theme::Original;
    const int year = int(today.year());
    const unsigned month = unsigned(today.month()), day = unsigned(today.day());
    const auto enabled = [&](Theme t) { return (s.events & (1U << int(t))) != 0; };
    // Optional festivals deliberately precede overlapping core events.
    for (const auto& festival : Festivals)
        if (enabled(festival.theme) && within(today, festival.start, festival.end)) return festival.theme;
    if (enabled(Theme::Thanksgiving)) {
        if (s.calendar == Calendar::US) {
            const auto holiday = weekday_in(year, 11, 4, 4);
            if (within(today, plus(holiday,-2), plus(holiday,3))) return Theme::Thanksgiving;
        } else if (s.calendar == Calendar::Canada) {
            const auto holiday = weekday_in(year, 10, 1, 2);
            if (within(today, plus(holiday,-2), plus(holiday,1))) return Theme::Thanksgiving;
        }
    }
    if (enabled(Theme::National) && month == 7 &&
        ((s.calendar == Calendar::US && day >= 2 && day <= 5) ||
         (s.calendar == Calendar::Canada && day <= 2))) return Theme::National;
    if (enabled(Theme::NewYear) && ((month == 12 && day >= 29) || (month == 1 && day <= 3))) return Theme::NewYear;
    if (enabled(Theme::Christmas) && month == 12 && day <= 28) return Theme::Christmas;
    if (enabled(Theme::Halloween) && month == 10 && day >= 20) return Theme::Halloween;
    if (enabled(Theme::Bonfire) && s.calendar == Calendar::UK && month == 11 && day <= 7) return Theme::Bonfire;
    if (enabled(Theme::Valentine) && month == 2 && day >= 10 && day <= 16) return Theme::Valentine;
    if (enabled(Theme::Patrick) && month == 3 && day >= 15 && day <= 18) return Theme::Patrick;
    if (enabled(Theme::Easter)) {
        const auto holiday = easter(year);
        if (within(today, plus(holiday,-7), plus(holiday,1))) return Theme::Easter;
    }
    if (!s.seasons) return Theme::Original;
    const unsigned shifted = (month - 1 + (s.southern ? 6 : 0)) % 12 + 1;
    if (shifted >= 3 && shifted <= 5) return Theme::Spring;
    if (shifted >= 6 && shifted <= 8) return Theme::Summer;
    if (shifted >= 9 && shifted <= 11) return Theme::Autumn;
    return Theme::Winter;
}
inline Theme resolve(const Settings& s, Date today) {
    if (s.mode == Mode::Original) return Theme::Original;
    if (s.mode == Mode::Manual) return int(s.manual) >= 0 && s.manual < Theme::Count ? s.manual : Theme::Original;
    return automatic(s, today);
}
inline Date next_change(const Settings& s, Date today) {
    if (s.mode != Mode::Automatic || !today.ok()) return {};
    const Theme current = resolve(s, today);
    // Bounded work, performed only on civil-date/settings change, never per frame.
    for (int offset = 1; offset <= 370; ++offset)
        if (resolve(s, plus(today, offset)) != current) return plus(today, offset);
    return {};
}
} // namespace dkr::launcher_themes
