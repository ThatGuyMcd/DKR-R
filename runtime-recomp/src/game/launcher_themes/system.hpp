#pragma once
#include "region.hpp"
#include "settings.hpp"
#include "regional_sources.hpp"
#include <cstdlib>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dkr::launcher_themes {
inline std::string detected_country_source;
inline std::string system_country() {
#ifdef _WIN32
    wchar_t country[16]{};
    if (GetUserDefaultGeoName(country, int(std::size(country))) > 0 &&
        country[0] >= L'A' && country[0] <= L'Z' &&
        country[1] >= L'A' && country[1] <= L'Z' && !country[2])
        {detected_country_source="Windows country setting";return country_from_locale(std::string{char(country[0]), char(country[1])});}
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string value;
        for (const wchar_t c : locale) { if (!c) break; if (c > 127) return {}; value += char(c); }
        detected_country_source="Windows locale territory";return country_from_locale(value);
    }
    return {};
#else
    const auto env = [](const char* key) -> std::string_view {
        const char* value = std::getenv(key);
        return value ? std::string_view(value) : std::string_view{};
    };
    RegionalValues environment;
    for(const auto key:{"LC_ALL","LC_ADDRESS","LC_TIME","LANG"})environment[key]=env(key);
    std::filesystem::path config;
    if(!env("XDG_CONFIG_HOME").empty()&&std::filesystem::path(env("XDG_CONFIG_HOME")).is_absolute())config=env("XDG_CONFIG_HOME");
    else if(!env("HOME").empty())config=std::filesystem::path(env("HOME"))/".config";
    auto user=config.empty()?RegionalValues{}:read_regional(config/"plasma-localerc",true);
    // Steam may supply an application-specific XDG directory. The desktop's
    // persisted regional formats still belong to this user's ordinary config.
    if(regional_country(user).empty()&&!env("HOME").empty())
        user=read_regional(std::filesystem::path(env("HOME"))/".config"/"plasma-localerc",true);
    auto system=read_regional("/etc/locale.conf",false);
    if(system.empty())system=read_regional("/etc/default/locale",false);
    const auto result=resolve_country(user,environment,system);
    detected_country_source=result.source;return result.country;
#endif
}
inline bool system_animation() {
    if (legacy_static_background) return false;
#ifdef _WIN32
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return enabled != FALSE;
#endif
    return true;
}
class SystemSettings {
    bool initialized_ = false, active_ = false;
    double checked_ = 0;
    std::string country_;
public:
    template<class Provider>
    bool refresh(double now, bool visible, bool focused, Provider&& provider) {
        const bool active = visible && focused;
        const bool resumed = active && !active_;
        active_ = active;
        if (!visible || (initialized_ && !resumed && now >= checked_ && now - checked_ < 60)) return false;
        initialized_ = true; checked_ = now;
        auto [country, animate] = provider();
        auto next = settings_for_country(country, animate);
        const bool changed = next != preferences || country != country_;
        country_ = std::move(country); preferences = next;
        return changed;
    }
    bool refresh(double now, bool visible, bool focused) {
        const bool first=!initialized_;
        const bool changed=refresh(now, visible, focused, [] { return std::pair{system_country(),system_animation()}; });
        if(visible&&(first||changed))std::fprintf(stderr,"[launcher][region] country=%s source=%s animation=%u; local OS settings, cached 60s\n",
            country_.empty()?"unspecified":country_.c_str(),detected_country_source.c_str(),unsigned(preferences.animate));
        return changed;
    }
    const std::string& country() const { return country_; }
};
} // namespace dkr::launcher_themes
