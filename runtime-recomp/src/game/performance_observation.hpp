#pragma once
#include "performance_switches.hpp"
#include "performance_capture.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dkr::runtime::surgical {
enum class Region : unsigned { Decode, Registry, LiquidWarmup, Count };
struct Measurement {
    std::uint64_t calls=0,total_us=0,maximum_us=0;
    std::array<std::uint64_t,32> buckets{};
};
inline constexpr std::array<std::uint64_t,32> bounds_us{{
    1,2,4,8,16,32,64,100,150,250,400,600,800,1000,1500,2000,
    3000,4000,5560,8334,12000,16667,25000,33334,50000,75000,
    100000,150000,250000,500000,1000000,UINT64_MAX}};
struct Observations {
    std::array<Measurement,unsigned(Region::Count)> values{};
    std::uint32_t scene=~0U;
    unsigned tasks=0;
};
inline thread_local Observations observations;
inline bool observing() noexcept {return profile() || performance_capture::active();}
inline void report() noexcept {
    constexpr const char* names[]{"decode","water-registry","liquid-warmup"};
    for(unsigned i=0;i<unsigned(Region::Count);++i) {
        const auto& m=observations.values[i];if(!m.calls)continue;
        const auto percentile=[&](unsigned p) {
            const auto rank=(m.calls/100)*p+((m.calls%100)*p+99)/100;
            std::uint64_t sum=0;
            for(unsigned bucket=0;bucket<bounds_us.size();++bucket)
                if((sum+=m.buckets[bucket])>=rank)return bounds_us[bucket];
            return std::uint64_t(0);
        };
        std::fprintf(stderr,"[perf][surgical] scene=%u region=%s calls=%llu wall-us=%llu p95-upper-us=%llu p99-upper-us=%llu max-us=%llu\n",
            observations.scene,names[i],static_cast<unsigned long long>(m.calls),
            static_cast<unsigned long long>(m.total_us),static_cast<unsigned long long>(percentile(95)),
            static_cast<unsigned long long>(percentile(99)),static_cast<unsigned long long>(m.maximum_us));
    }
    observations.values={};observations.tasks=0;
}
inline void context(std::uint32_t scene,std::uint32_t map,std::uint32_t menu,unsigned target,unsigned quality) noexcept {
    if(!observing())return;
    if(observations.scene!=scene) {
        report();observations.scene=scene;
        std::fprintf(stderr,"[perf][surgical-context] scene=%u map=%u menu=%u target=%u water-quality=%u; CPU wall times, not GPU or scanout\n",scene,map,menu,target,quality);
    }
    if(++observations.tasks>=240)report();
}
class Scope {
    Region region_;bool enabled_;std::chrono::steady_clock::time_point begin_{};
public:
    explicit Scope(Region value):region_(value),enabled_(observing()) {if(enabled_)begin_=std::chrono::steady_clock::now();}
    ~Scope() {
        if(!enabled_)return;
        const auto us=std::uint64_t(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin_).count()));
        auto& m=observations.values[unsigned(region_)];++m.calls;m.total_us+=us;m.maximum_us=std::max(m.maximum_us,us);
        unsigned bucket=0;while(us>bounds_us[bucket])++bucket;++m.buckets[bucket];
    }
};
}
