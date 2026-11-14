#include "launcher_themes/pixels.hpp"
#include "launcher_themes/session.hpp"
#include "launcher_themes/settings.hpp"
#include "launcher_themes/system.hpp"
#include "launcher_themes/greeting.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

using namespace dkr::launcher_themes;
int main() {
    Settings s;
    assert(greeting(resolve(s, date(2026, 10, 19)), s.calendar).empty());
    assert(!greeting(resolve(s, date(2026, 10, 20)), s.calendar).empty());
    assert(!greeting(resolve(s, date(2026, 11, 1)), s.calendar).empty());
    assert(greeting(resolve(s, date(2026, 11, 8)), s.calendar).empty());
    for (int theme = int(Theme::Halloween); theme < int(Theme::National); ++theme)
        assert(!greeting(Theme(theme), Calendar::UK).empty());
    assert(greeting(Theme::National, Calendar::International).empty());
    assert(greeting(Theme::National, Calendar::US) != greeting(Theme::National, Calendar::Canada));
    assert(greeting(Theme::Count, Calendar::UK).empty());
    assert(resolve(s, date(2026, 10, 19)) == Theme::Autumn);
    assert(resolve(s, date(2026, 10, 20)) == Theme::Halloween);
    assert(resolve(s, date(2026, 10, 31)) == Theme::Halloween);
    assert(resolve(s, date(2026, 11, 1)) == Theme::Bonfire);
    assert(resolve(s, date(2026, 11, 7)) == Theme::Bonfire);
    assert(resolve(s, date(2026, 11, 8)) == Theme::Autumn);
    assert(resolve(s, date(2026, 12, 1)) == Theme::Christmas);
    assert(resolve(s, date(2026, 12, 28)) == Theme::Christmas);
    assert(resolve(s, date(2026, 12, 29)) == Theme::NewYear);
    assert(resolve(s, date(2027, 1, 3)) == Theme::NewYear);
    assert(resolve(s, date(2027, 1, 4)) == Theme::Winter);
    assert(next_change(s, date(2026, 10, 31)) == date(2026, 11, 1));
    assert(easter(2026) == date(2026, 4, 5));
    assert(easter(2027) == date(2027, 3, 28));
    assert(easter(2028) == date(2028, 4, 16));
    assert(resolve(s, date(2026, 3, 29)) == Theme::Easter);
    assert(resolve(s, date(2026, 4, 6)) == Theme::Easter);
    assert(resolve(s, date(2026, 4, 7)) == Theme::Spring);
    assert(plus(date(2028, 2, 28), 1) == date(2028, 2, 29));
    s.events = 0;
    for (unsigned month = 1; month <= 12; ++month) {
        const auto northern = resolve(s, date(2028, month, 1));
        s.southern = true;
        const auto southern = resolve(s, date(2028, month, 1));
        s.southern = false;
        assert(northern != southern);
    }
    s.seasons = false;
    assert(resolve(s, date(2026, 10, 31)) == Theme::Original);
    s = Settings{};
    s.events = EventBits;
    for (const auto& festival : Festivals) {
        assert(resolve(s, festival.start) == festival.theme);
        assert(resolve(s, festival.end) == festival.theme);
    }
    s.events = 1U << int(Theme::Lunar);
    assert(resolve(s, date(2028, 2, 1)) == Theme::Winter); // published coverage expired
    s = Settings{};
    s.calendar = Calendar::International;
    assert(resolve(s, date(2026, 11, 5)) == Theme::Autumn);
    s.events = EventBits;
    s.calendar = Calendar::US;
    assert(resolve(s, date(2026, 11, 26)) == Theme::Thanksgiving);
    assert(resolve(s, date(2026, 7, 4)) == Theme::National);
    s.calendar = Calendar::Canada;
    assert(resolve(s, date(2026, 10, 12)) == Theme::Thanksgiving);
    assert(resolve(s, date(2026, 7, 1)) == Theme::National);
    assert(resolve(s, date(2026, 7, 4)) == Theme::Summer);
    s.mode = Mode::Manual;
    for (int i = 0; i < int(Theme::Count); ++i) {
        s.manual = Theme(i);
        assert(resolve(s, date(2026, 10, 31)) == Theme(i));
    }
    assert(!next_change(s, date(2026, 10, 31)).ok());
    s.mode = Mode::Original;
    assert(resolve(s, date(2026, 10, 31)) == Theme::Original);
    assert(resolve(Settings{}, Date{}) == Theme::Original);

    preferences = Settings{};
    assert(load("launcher_theme_mode", 2));
    assert(load("launcher_theme_manual", 18));
    assert(load("launcher_theme_calendar", 3));
    assert(load("launcher_theme_events", 0));
    assert(load("launcher_theme_animate", 0));
    assert(load("launcher_theme_southern", 1));
    assert(load("launcher_theme_seasons", 0));
    // Old manual themes, hemisphere, event masks and calendars are consumed,
    // but must never override the OS region after this migration.
    assert(preferences == Settings{});
    assert(legacy_static_background);
    std::ostringstream saved;
    save(saved);
    assert(saved.str() == "launcher_theme_animate=0\n");
    const auto expected = preferences;
    load("launcher_theme_mode", 1000);
    load("launcher_theme_manual", -1);
    load("launcher_theme_calendar", 99);
    assert(preferences == expected);
    assert(!load("unrelated", 0));
    load("launcher_theme_animate", 1);
    saved.str("");
    save(saved);
    assert(saved.str().empty());

    assert(country_from_locale("en_GB.UTF-8") == "GB");
    assert(country_from_locale("en-gb") == "GB");
    assert(country_from_locale("zh_Hans_CN.UTF-8") == "CN");
    assert(country_from_locale("sr_RS@latin") == "RS");
    assert(country_from_locale("US") == "US");
    for (const auto invalid : {"en", "C", "C.UTF-8", "POSIX", "001", "XX", "ZZ", "invalid"})
        assert(country_from_locale(invalid).empty());
    assert(locale_country("en_AU.UTF-8", "en_US", "en_GB", "fr_CA") == "AU");
    assert(locale_country("C", "en_US", "en_GB", "fr_CA") == "US");
    assert(locale_country("", "en_GB", "en_US", "en_CA") == "GB");
    assert(locale_country("", "", "en_CA", "en_US") == "CA");
    assert(locale_country("", "", "", "en_NZ.UTF-8") == "NZ");
    assert(settings_for_country("GB").calendar == Calendar::UK);
    assert(settings_for_country("US").calendar == Calendar::US);
    assert(settings_for_country("CA").calendar == Calendar::Canada);
    assert(resolve(settings_for_country("GB"), date(2026, 11, 5)) == Theme::Bonfire);
    assert(resolve(settings_for_country("US"), date(2026, 11, 5)) == Theme::Autumn);
    assert(resolve(settings_for_country("US"), date(2026, 11, 26)) == Theme::Thanksgiving);
    assert(resolve(settings_for_country("CA"), date(2026, 10, 12)) == Theme::Thanksgiving);
    assert(resolve(settings_for_country("CA"), date(2026, 7, 1)) == Theme::National);
    assert(resolve(settings_for_country("US"), date(2026, 7, 4)) == Theme::National);
    assert(resolve(settings_for_country("AU"), date(2027, 1, 20)) == Theme::Summer);
    assert(resolve(settings_for_country("NZ"), date(2027, 7, 20)) == Theme::Winter);
    assert(resolve(settings_for_country("SG"), date(2026, 2, 17)) == Theme::Lunar);
    assert(resolve(settings_for_country("IN"), date(2026, 11, 8)) == Theme::Diwali);
    assert(resolve(settings_for_country("SG"), date(2026, 3, 21)) == Theme::EidFitr);
    assert(resolve(settings_for_country("SG"), date(2026, 5, 27)) == Theme::EidAdha);
    assert(resolve(settings_for_country("IL"), date(2026, 12, 4)) == Theme::Hanukkah);
    assert(resolve(settings_for_country("??"), date(2026, 12, 4)) == Theme::Christmas);
    assert(resolve(settings_for_country(""), date(2026, 10, 31)) == Theme::Halloween);
    assert(!settings_for_country("GB", false).animate);
    SystemSettings region;
    int region_reads = 0;
    std::string detected = "GB";
    const auto region_provider = [&] {
        ++region_reads;
        return std::pair{detected, true};
    };
    region.refresh(0, true, true, region_provider);
    assert(region_reads == 1);
    for (int i = 1; i < 600; ++i)
        region.refresh(i * .1, true, true, region_provider);
    assert(region_reads == 1);
    region.refresh(60, false, false, region_provider);
    assert(region_reads == 1);
    detected = "CA";
    assert(region.refresh(100, true, true, region_provider));
    assert(region.country() == "CA" && preferences.calendar == Calendar::Canada && region_reads == 2);
    assert(!region.refresh(160, true, true, region_provider) && region_reads == 3);

    Session session;
    int reads = 0;
    auto today = date(2026, 10, 31);
    const auto provider = [&] {
        ++reads;
        return today;
    };
    s = Settings{};
    assert(session.tick(0, true, true, s, provider));
    assert(reads == 1);
    for (int i = 1; i < 600; ++i)
        session.tick(i * .1, true, true, s, provider);
    assert(reads == 1);
    const double animated = session.seconds();
    session.tick(60, false, false, s, provider);
    session.tick(999, false, false, s, provider);
    assert(reads == 1);
    assert(session.seconds() == animated);
    today = date(2026, 11, 1);
    assert(session.tick(1000, true, true, s, provider));
    assert(reads == 2);
    assert(session.theme() == Theme::Bonfire);
    assert(session.seconds() == animated);
    session.tick(1010, true, true, s, provider);
    assert(session.seconds() <= animated + .101);
    s.animate = false;
    const auto still = session.seconds();
    session.tick(1011, true, true, s, provider);
    session.tick(1012, true, true, s, provider);
    assert(session.seconds() == still);
    s.mode = Mode::Manual;
    s.manual = Theme::Easter;
    assert(session.tick(1012.01, true, true, s, provider));
    assert(session.theme() == Theme::Easter);
    s.manual = Theme::Summer;
    assert(!session.tick(1013, false, false, s, provider));
    assert(session.theme() == Theme::Easter);
    assert(session.tick(1014, true, true, s, provider));
    assert(session.theme() == Theme::Summer);
    today = date(2026, 10, 30);
    s = Settings{};
    assert(session.tick(0, true, true, s, provider)); // clock discontinuity/settings change

    const std::vector<unsigned char> original{0, 0, 0, 18, 120, 130, 140, 255, 255, 255, 255, 89};
    auto pixels = original;
    assert(tint(pixels, 3, 1, Theme::Original));
    assert(pixels == original);
    std::set<std::vector<unsigned char>> distinct;
    for (int i = 1; i < int(Theme::Count); ++i) {
        pixels = original;
        assert(tint(pixels, 3, 1, Theme(i)));
        for (std::size_t a = 3; a < pixels.size(); a += 4)
            assert(pixels[a] == original[a]);
        distinct.insert(pixels);
    }
    assert(distinct.size() == 18);
    assert(!tint(pixels, 0, 1, Theme::Summer));
    assert(!tint(pixels, 8193, 1, Theme::Summer));
    assert(!tint(pixels, 2, 1, Theme::Summer));
    assert(!tint(pixels, 3, 1, Theme::Count));
    std::cout
        << "19 themes; local date boundaries, regional coverage, preferences, suspension and distinct cached palettes passed\n";
}
