#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dkr::runtime::launcher {

class Profile {
  public:
    using Clock = std::chrono::steady_clock;
    enum Phase {
        Services,
        Events,
        Build,
        Draw,
        Present,
        Frame,
        Background,
        Underlay,
        Root,
        Sidebar,
        Content,
        OtherLists,
        Count
    };
    Profile() {
        const char* value = std::getenv("DKR_LAUNCHER_PROFILE");
        enabled_ = value != nullptr && std::strcmp(value, "1") == 0;
    }
    bool enabled() const {
        return enabled_;
    }
    void add(Phase phase, Clock::time_point begin, Clock::time_point end) {
        if (!enabled_)
            return;
        auto& samples = samples_[phase];
        if (counts_[phase] < samples.size()) {
            samples[counts_[phase]++] = std::chrono::duration<double, std::milli>(end - begin).count();
        }
    }
    void report(int page, int width, int height, unsigned flags, int hz, bool force = false) {
        if (!enabled_)
            return;
        const auto now = Clock::now();
        if (!force && now - last_report_ < std::chrono::seconds(5))
            return;
        last_report_ = now;
        std::fprintf(stderr, "[perf][launcher-detail] page=%d output=%dx%d flags=0x%08X target=%dHz", page, width,
                     height, flags, hz);
        constexpr const char* names[] = {"services",   "events",   "build", "draw",    "present", "frame",
                                         "background", "underlay", "root",  "sidebar", "content", "other-lists"};
        for (int phase = 0; phase < Count; ++phase) {
            const auto count = counts_[phase];
            if (count == 0)
                continue;
            auto& samples = samples_[phase];
            std::sort(samples.begin(), samples.begin() + count);
            double sum = 0;
            for (std::size_t i = 0; i < count; ++i)
                sum += samples[i];
            std::fprintf(stderr, " %s[n=%zu avg=%.3f p95=%.3f p99=%.3f max=%.3f]ms", names[phase], count, sum / count,
                         samples[(count - 1) * 95 / 100], samples[(count - 1) * 99 / 100], samples[count - 1]);
            counts_[phase] = 0;
        }
        std::fputc('\n', stderr);
    }

  private:
    bool enabled_ = false;
    Clock::time_point last_report_ = Clock::now();
    std::array<std::array<double, 2048>, Count> samples_{};
    std::array<std::size_t, Count> counts_{};
};

} // namespace dkr::runtime::launcher
