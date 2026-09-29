#include "water_performance.hpp"
#include "revision_addresses.hpp"
#include "recomp.h"
#include <cstring>

namespace {
using Clock = dkr::runtime::track_performance::Clock;
struct Region {
    Clock::time_point start{};
    std::uint64_t calls=0, ns=0;
    unsigned depth=0;
};
thread_local std::array<Region,10> regions{};
thread_local unsigned renders=0;
thread_local std::uint32_t profiled_map=~0U;
std::uint32_t Read(std::uint8_t* ram, std::uint32_t address) {
    std::uint32_t value; std::memcpy(&value,ram+(address&0x007FFFFCU),4); return value;
}
}
extern "C" void dkr_water_profile_begin(std::uint32_t i) {
    if (!dkr::runtime::water::enabled() || i>=regions.size()) return;
    auto& r=regions[i]; if(r.depth++==0) r.start=Clock::now();
}
extern "C" void dkr_water_profile_end(std::uint8_t* ram, std::uint32_t i) {
    if (!dkr::runtime::water::enabled() || i>=regions.size()) return;
    const auto map=Read(ram,dkr::runtime::revision_addresses::CurrentMapId);
    if(map!=profiled_map) {
        // Drop counts from the preceding level; preserve active nesting and
        // start times so a parent water-region return remains balanced.
        for(auto& region:regions) region.calls=region.ns=0;
        renders=0;profiled_map=map;
    }
    auto& r=regions[i]; if(!r.depth || --r.depth) return;
    r.ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-r.start).count(); ++r.calls;
    if(i!=5 || ++renders%120U) return;
    const char* names[]={"phase-uv","visibility","block-lookup","mesh-xlu","mesh-opa","render-total",
                         "height-normal","water-effects","object-height","generators"};
    for(unsigned n=0;n<regions.size();++n) {
        auto& region=regions[n];
        if(region.calls) std::fprintf(stderr,"[perf][water-guest] map=%u region=%s calls=%llu wall-ms=%.3f\n",
            map,names[n],(unsigned long long)region.calls,region.ns/1000000.0);
        region.calls=region.ns=0;
    }
    // Nested/inclusive wall durations, NOT summed CPU usage. Diagnostic clock
    // overhead is measured separately with this environment option disabled.
}
extern "C" void dkr_water_private_scene(std::uint8_t*, recomp_context* ctx) {
#if defined(DKR_WATER_QUALIFICATION)
    // Private offline preview qualification only. No writes to any save. The
    // normal loader still creates the scene; this is NOT a full racing route.
    static const int map=[] {
        const char* v=std::getenv("DKR_WATER_TEST_MAP");
        if(!v) return -1;
        char* end=nullptr; const long value=std::strtol(v,&end,10);
        if(*end) return -1;
        // Central hub (0) is not a valid title/track-preview substitution:
        // its scripted reload differs. Do not expose it in this test probe.
        for(int allowed:{3,4,5,7,8,10,14,26,30,40,53}) if(value==allowed) return allowed;
        return -1;
    }();
    if(map>=0 && static_cast<std::int32_t>(ctx->r4)>=0) {
        ctx->r4=map;ctx->r5=static_cast<gpr>(-1);ctx->r6=1;
        std::fprintf(stderr,"[perf][private-water-preview] map=%d\n",map);
    }
#else
    (void)ctx;
#endif
}
