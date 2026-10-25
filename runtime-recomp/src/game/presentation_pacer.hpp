#pragma once
#include "performance_observation.hpp"
#include "presentation_wait.hpp"
#include <cmath>
#include <thread>

namespace dkr::runtime::surgical {
// Same calibrated short-sleep/spin algorithm as the accepted timer. Only the
// presentation deadline uses steady_clock; no guest, audio or network clock.
inline void precise_present_wait(std::chrono::steady_clock::time_point end) {
    using Clock=std::chrono::steady_clock;
    coarse_presentation_wait(end);
    auto start=Clock::now();
    auto remaining=std::chrono::duration<double>(end-start).count();
    if(remaining<=0)return;
    constexpr double sleep_seconds=0.0001,upper=0.002;
    static thread_local double estimate=sleep_seconds,mean=sleep_seconds,m2=0;
    static thread_local std::uint64_t samples=1;
    while(remaining>estimate) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(100000));
        const auto next=Clock::now();
        const double elapsed=std::chrono::duration<double>(next-start).count();
        remaining-=elapsed;
        if(elapsed<upper) {
            ++samples;const double delta=elapsed-mean;mean+=delta/samples;
            m2+=delta*(elapsed-mean);estimate=mean+2*std::sqrt(m2/(samples-1));
        }
        start=next;
    }
    while(Clock::now()<end) {}
}
class PresentationPacer {
    using Clock=std::chrono::steady_clock;
    using SourceStamp=std::chrono::high_resolution_clock::time_point;
    const void* owner_=nullptr;
    SourceStamp stamp_{};
    Clock::time_point submitted_{};
    int target_=0;
public:
    void wait(const void* owner,SourceStamp stamp,int target) {
        // A paused/repeated presentation can run on another owner thread.
        // Its changed source stamp invalidates this thread's local anchor.
        if(owner!=owner_ || stamp!=stamp_ || target!=target_ || target<=0)return;
        precise_present_wait(submitted_+std::chrono::nanoseconds(1000000000LL/target));
    }
    void submitted(const void* owner,SourceStamp stamp,int target) noexcept {
        owner_=owner;stamp_=stamp;target_=target;submitted_=Clock::now();
    }
};
inline PresentationPacer& presentation_pacer(){static thread_local PresentationPacer value;return value;}

inline std::atomic<std::uint64_t> producer_context{0};
inline void publish_context(std::uint32_t scene,std::uint32_t map,std::uint32_t menu) noexcept {
    if(observing())producer_context.store((std::uint64_t(scene)<<32)|((map&65535U)<<16)|(menu&65535U),std::memory_order_relaxed);
}
inline void observe_present(unsigned width,unsigned height,unsigned target) noexcept {
    using Clock=std::chrono::steady_clock;
    struct Window {
        std::uint64_t context=0;unsigned width=0,height=0,target=0,count=0;
        Clock::time_point previous{},stable{};
        std::array<double,300> intervals{};
    };
    static thread_local Window window;
    if(!observing()){window.previous={};window.count=0;return;}
    const auto now=Clock::now();const auto context=producer_context.load(std::memory_order_relaxed);
    if(context!=window.context || width!=window.width || height!=window.height || target!=window.target || window.stable==Clock::time_point{}) {
        window={};window.context=context;window.width=width;window.height=height;window.target=target;window.stable=now;return;
    }
    // Exclude half a second around a scene/viewport/target transition. Context
    // is the latest producer observation, not an exact GPU-frame scene tag.
    if(now-window.stable<std::chrono::milliseconds(500)){window.previous={};return;}
    if(window.previous!=Clock::time_point{})window.intervals[window.count++]=std::chrono::duration<double,std::milli>(now-window.previous).count();
    window.previous=now;
    if(window.count!=window.intervals.size())return;
    auto values=window.intervals;std::sort(values.begin(),values.end());
    const double budget=target?1000.0/target:0;
    const auto misses=[&](double limit){return std::count_if(values.begin(),values.end(),[&](double ms){return ms>limit;});};
    std::fprintf(stderr,"[perf][surgical-present] producer-scene=%u viewport=%ux%u target=%u n=300 ms(p50/p95/p99/max)=%.3f/%.3f/%.3f/%.3f over-1.5-budget=%zu over-2-budget=%zu; swapchain returns, not scanout\n",
        unsigned(context>>32),width,height,target,values[149],values[284],values[296],values[299],std::size_t(misses(budget*1.5)),std::size_t(misses(budget*2)));
    window.count=0;
}
}
