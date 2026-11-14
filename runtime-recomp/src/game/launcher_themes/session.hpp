#pragma once
#include "policy.hpp"
#include <algorithm>
#include <ctime>

namespace dkr::launcher_themes {
inline Date local_date() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
#ifdef _WIN32
    if (localtime_s(&value, &now) != 0)
        return {};
#else
    if (!localtime_r(&now, &value))
        return {};
#endif
    return date(value.tm_year + 1900, unsigned(value.tm_mon + 1), unsigned(value.tm_mday));
}
class Session {
    Settings saved_{};
    bool initialized_ = false, active_ = false;
    double checked_ = 0, previous_ = 0, seconds_ = 0;
    Date today_{};
    Theme theme_ = Theme::Original;
    Date next_{};

  public:
    template <class Provider>
    bool tick(double now, bool visible, bool focused, const Settings& s, Provider&& provider) {
        const bool active = visible && focused;
        if (initialized_ && active && active_ && s.animate)
            seconds_ += std::clamp(now - previous_, 0.0, 0.1);
        previous_ = now;
        const bool resumed = active && !active_;
        active_ = active;
        if (!visible)
            return false;
        const bool settings_changed = !initialized_ || s != saved_;
        if (!settings_changed && !resumed && now >= checked_ && now - checked_ < 60)
            return false;
        checked_ = now;
        const Date civil = provider();
        if (!settings_changed && civil == today_)
            return false;
        const auto old = theme_;
        saved_ = s;
        today_ = civil;
        initialized_ = true;
        theme_ = resolve(s, civil);
        next_ = next_change(s, civil);
        return old != theme_;
    }
    Theme theme() const {
        return theme_;
    }
    Date next() const {
        return next_;
    }
    double seconds() const {
        return seconds_;
    }
};
} // namespace dkr::launcher_themes
