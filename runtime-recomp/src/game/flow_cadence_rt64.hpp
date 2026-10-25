#pragma once
#include "hle/rt64_application.h"
#include "hle/rt64_shared_queue_resources.h"
#include "runtime_platform.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dkr::runtime::water {
// No GPU queries, readbacks or per-triangle clocks. One bounded line per five
// seconds while flowing water is submitted, plus a new baseline per scene.
inline void report_flow_cadence(const RT64::Application& application,
    std::uint32_t scene,bool allowed,unsigned loads,unsigned faces,unsigned fractional,
    unsigned manual,unsigned held,unsigned image_changes) {
    using Clock=std::chrono::steady_clock;
    struct Window {
        Clock::time_point start{};
        std::uint64_t presented=0,interpolated=0,tasks=0,approved=0,loads=0,faces=0,fractional=0;
        std::uint64_t manual=0,held=0,image_changes=0;
        std::uint32_t scene=~0U;
    };
    static thread_local Window window;
    if(!application.sharedQueueResources)return;
    const auto presented=application.sharedQueueResources->totalPresentations.load(std::memory_order_relaxed);
    const auto interpolated=application.sharedQueueResources->totalInterpolatedPresentations.load(std::memory_order_relaxed);
    const auto now=Clock::now();
    if(window.scene!=scene || window.start==Clock::time_point{} || presented<window.presented || interpolated<window.interpolated)
        window={now,presented,interpolated,0,0,0,0,0,0,0,0,scene};
    ++window.tasks;window.approved+=allowed;window.loads+=loads;window.faces+=faces;window.fractional+=fractional;
    window.manual+=manual;window.held+=held;window.image_changes+=image_changes;
    const double elapsed=std::chrono::duration<double>(now-window.start).count();
    if(elapsed<5)return;
    if(window.faces) {
        int width=0,height=0;
        if(auto* w=static_cast<SDL_Window*>(platform::sdl_window()))SDL_GetWindowSize(w,&width,&height);
        std::fprintf(stderr,"[water][cadence] scene=%u viewport=%dx%d tasks=%.1f presents=%.1f interpolated=%.1f target=%u sidecars=%llu/%llu flow-loads=%llu flow-faces=%llu fractional-faces=%llu manual-faces=%llu held-faces=%llu image-changes=%llu\n",
            scene,width,height,window.tasks/elapsed,(presented-window.presented)/elapsed,
            (interpolated-window.interpolated)/elapsed,unsigned(application.userConfig.refreshRateTarget),
            static_cast<unsigned long long>(window.approved),static_cast<unsigned long long>(window.tasks),
            static_cast<unsigned long long>(window.loads),static_cast<unsigned long long>(window.faces),
            static_cast<unsigned long long>(window.fractional),static_cast<unsigned long long>(window.manual),
            static_cast<unsigned long long>(window.held),static_cast<unsigned long long>(window.image_changes));
    }
    window={now,presented,interpolated,0,0,0,0,0,0,0,0,scene};
}
}
