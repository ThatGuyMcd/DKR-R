#pragma once
#include "policy.hpp"
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <sstream>

namespace dkr::lobby_stability {
struct Diagnostics {
    std::string attempt,role,manifest,stage,package,error;
    int phase=-1;
    bool running=false;
    std::uint64_t received=0,total=0,wire=0,cached=0,retries=0;
    std::uint64_t sampled_wire=0;
    double bytes_per_second=0;
    Clock::time_point began=Clock::now(),phase_began=began,progress=began;
    Clock::time_point sampled_at=began;
};
inline std::mutex diagnostics_mutex;
inline std::map<const void*,Diagnostics> diagnostics;
inline std::atomic<std::uint64_t> sequence{0};
inline std::atomic<std::uint64_t> bootstrap_match{0};
inline std::atomic<unsigned> bootstrap_events{0};
inline void begin_bootstrap(std::uint64_t match){bootstrap_events.store(0);bootstrap_match.store(match);}
inline void end_bootstrap(){bootstrap_match.store(0);}
inline void bootstrap_event(const char* event,std::uint32_t scheduler,std::uint32_t task,std::uint64_t generation=0) {
    const auto match=bootstrap_match.load();if(!match)return;
    auto count=bootstrap_events.load();
    while(count<64&&!bootstrap_events.compare_exchange_weak(count,count+1)){}
    if(count>=64)return;
    std::fprintf(stderr,"[lobby-start][scheduler] match=%llu event=%s scheduler=%08X task=%08X generation=%llu\n",
        static_cast<unsigned long long>(match),event,scheduler,task,static_cast<unsigned long long>(generation));
}
inline std::string printable(std::string_view text) {
    std::string out;for(char c:text.substr(0,1024))out+=static_cast<unsigned char>(c)>=32?c:' ';return out;
}
inline void begin(const void* owner,std::string_view role) {
    std::lock_guard lock(diagnostics_mutex);
    if(diagnostics.size()>=16&&!diagnostics.contains(owner))diagnostics.erase(diagnostics.begin());
    auto& d=diagnostics[owner];d={};d.role=role;d.running=true;
    d.attempt=std::to_string(Clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence);
    std::fprintf(stderr,"[lobby-join] attempt=%s role=%s event=begin\n",d.attempt.c_str(),d.role.c_str());
}
inline void erase(const void* owner){std::lock_guard lock(diagnostics_mutex);diagnostics.erase(owner);}
inline void retire(const void* owner){std::lock_guard lock(diagnostics_mutex);diagnostics[owner].running=false;}
inline Diagnostics snapshot(const void* owner){std::lock_guard lock(diagnostics_mutex);const auto at=diagnostics.find(owner);return at==diagnostics.end()?Diagnostics{}:at->second;}
inline void stage(const void* owner,int phase,std::string_view label,std::string_view error={}) {
    std::lock_guard lock(diagnostics_mutex);auto& d=diagnostics[owner];
    if(d.attempt.empty() || (phase==0&&label.empty()&&error.empty()) || (d.phase==phase&&d.stage==label&&error.empty()))return;
    const auto now=Clock::now();if(d.phase!=phase){d.phase=phase;d.phase_began=now;}
    d.stage=printable(label);if(!error.empty())d.error=printable(error);
    std::fprintf(stderr,"[lobby-join] attempt=%s role=%s phase=%d elapsed-ms=%lld stage=%s package=%s received=%llu total=%llu wire=%llu cache=%llu repairs=%llu error=%s\n",
        d.attempt.c_str(),d.role.c_str(),phase,static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now-d.began).count()),
        d.stage.c_str(),d.package.c_str(),static_cast<unsigned long long>(d.received),static_cast<unsigned long long>(d.total),
        static_cast<unsigned long long>(d.wire),static_cast<unsigned long long>(d.cached),static_cast<unsigned long long>(d.retries),d.error.c_str());
}
inline void manifest(const void* owner,std::string_view value) {
    std::lock_guard lock(diagnostics_mutex);auto& d=diagnostics[owner];if(d.manifest==value)return;d.manifest=value;
    std::fprintf(stderr,"[lobby-join] attempt=%s manifest=%s\n",d.attempt.c_str(),d.manifest.c_str());
}
inline void package(const void* owner,std::string_view value,std::string_view name) {
    std::lock_guard lock(diagnostics_mutex);auto& d=diagnostics[owner];if(d.package==value)return;d.package=value;
    std::fprintf(stderr,"[lobby-join] attempt=%s payload=%s name=%s\n",d.attempt.c_str(),d.package.c_str(),printable(name).c_str());
}
inline void counters(const void* owner,std::uint64_t received,std::uint64_t total,std::uint64_t wire,std::uint64_t cached,std::uint64_t retries) {
    std::lock_guard lock(diagnostics_mutex);auto& d=diagnostics[owner];
    if(received>d.received)d.progress=Clock::now();d.received=received;d.total=total;d.wire=wire;d.cached=cached;d.retries=retries;
    const auto now=Clock::now();const auto seconds=std::chrono::duration<double>(now-d.sampled_at).count();
    if(seconds>=1){d.bytes_per_second=double(wire-d.sampled_wire)/seconds;d.sampled_wire=wire;d.sampled_at=now;}
}
inline std::string report(const void* owner) {
    const auto d=snapshot(owner);std::ostringstream out;
    out<<"Attempt: "<<d.attempt<<"\nRole: "<<d.role<<"\nManifest: "<<d.manifest<<"\nStage: "<<d.stage<<"\nPayload: "<<d.package
       <<"\nReceived: "<<d.received<<" / "<<d.total<<"\nWire bytes: "<<d.wire<<"\nCache bytes: "<<d.cached<<"\nGap repairs: "<<d.retries<<"\nError: "<<d.error;
    return out.str();
}
}
