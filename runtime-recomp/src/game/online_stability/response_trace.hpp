#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>

// Local steady-clock measurements only. Renderer submission is not display
// scanout. No input, identity, timestamp or counter enters canonical state.
namespace dkr::online_stability::response {
using Clock = std::chrono::steady_clock;
enum class Stage : unsigned { Assign, Execute, Publish, Renderer, Count };
inline constexpr std::array<unsigned,16> Limits{1,5,10,17,34,50,67,100,150,250,400,700,1000,1500,2000,UINT32_MAX};
struct Counter {
    std::array<std::uint64_t,Limits.size()> buckets{};
    std::uint64_t count=0,maximum_us=0;
    void add(Clock::time_point from,Clock::time_point to) {
        if(to<from)return;
        const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(to-from).count());
        unsigned bucket=0;while((us+999)/1000>Limits[bucket])++bucket;
        ++buckets[bucket];++count;maximum_us=std::max(maximum_us,us);
    }
    unsigned p95_ms()const {
        std::uint64_t sum=0;for(unsigned i=0;i<buckets.size();++i){sum+=buckets[i];if(count&&sum>=(count*95+99)/100)return Limits[i];}return 0;
    }
};
struct Entry {std::uint32_t frame=0;Clock::time_point captured{};unsigned stages=0;bool valid=false;};
struct Trace {
    std::mutex mutex;
    std::uint64_t epoch=0,overwritten=0;
    std::array<Entry,128> entries{};
    std::array<Counter,unsigned(Stage::Count)> counters{};
    Clock::time_point report_at{};
};
inline Trace& trace(){static Trace value;return value;}
inline void reset(std::uint64_t epoch) {
    auto& t=trace();std::lock_guard<std::mutex> lock(t.mutex);
    t.epoch=epoch;t.entries={};t.counters={};t.overwritten=0;t.report_at={};
}
inline void assigned(std::uint64_t epoch,std::uint32_t frame,Clock::time_point captured,Clock::time_point now) {
    auto& t=trace();std::lock_guard<std::mutex> lock(t.mutex);if(epoch!=t.epoch)return;
    auto& row=t.entries[frame%t.entries.size()];if(row.valid&&!(row.stages&(1U<<unsigned(Stage::Renderer))))++t.overwritten;
    row={frame,captured,1,true};t.counters[0].add(captured,now);
}
inline void reached(std::uint64_t epoch,std::uint32_t frame,Stage stage,Clock::time_point now=Clock::now()) {
    auto& t=trace();std::lock_guard<std::mutex> lock(t.mutex);if(epoch!=t.epoch)return;
    const auto mask=1U<<unsigned(stage);
    for(auto& row:t.entries)if(row.valid&&row.frame<=frame&&!(row.stages&mask)) {
        // A renderer may skip superseded pictures, but may never count an
        // input before a successful tick has actually consumed its frame.
        if(stage!=Stage::Execute&&!(row.stages&(1U<<unsigned(Stage::Execute))))continue;
        row.stages|=mask;t.counters[unsigned(stage)].add(row.captured,now);
    }
}
inline std::array<Counter,unsigned(Stage::Count)> snapshot(){auto& t=trace();std::lock_guard<std::mutex> lock(t.mutex);return t.counters;}
inline void report(Clock::time_point now) {
    auto& t=trace();std::array<Counter,unsigned(Stage::Count)> counters;
    std::uint64_t epoch=0,overwritten=0;
    {
        std::lock_guard<std::mutex> lock(t.mutex);
        if(t.report_at.time_since_epoch().count()&&now<t.report_at)return;t.report_at=now+std::chrono::seconds(5);
        counters=t.counters;epoch=t.epoch;overwritten=t.overwritten;
    } // A slow log sink must never hold the renderer's measurement mutex.
    constexpr const char* names[]{"assign","execute","publish","renderer-submit"};
    for(unsigned i=0;i<counters.size();++i){const auto& c=counters[i];
        std::fprintf(stderr,"[online][response] epoch=%llu stage=%s samples=%llu p95-upper-ms=%u max-us=%llu overwritten=%llu scanout=unmeasured\n",
            static_cast<unsigned long long>(epoch),names[i],static_cast<unsigned long long>(c.count),c.p95_ms(),
            static_cast<unsigned long long>(c.maximum_us),static_cast<unsigned long long>(overwritten));}
}
}
