#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace dkr::online_stability {
class QueueAgeMeter {
public:
    using Clock=std::chrono::steady_clock;
    void reset(){rows_={};reported_={};}
    void accepted(unsigned lane,std::uint64_t peer,Clock::time_point queued,Clock::time_point now) {
        if(lane>=5||now<queued)return;
        const auto age=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now-queued).count());
        auto& r=rows_[lane];++r.count;r.total+=age;if(age>=r.maximum){r.maximum=age;r.peer=peer;}
    }
    void pulse(Clock::time_point now) {
        if(!reported_.time_since_epoch().count()){reported_=now;return;}if(now-reported_<std::chrono::seconds(5))return;
        reported_=now;
        for(unsigned lane=0;lane<5;++lane){const auto& r=rows_[lane];if(r.count)std::fprintf(stderr,
            "[online][queue] lane=%u accepted=%llu application-queue-mean-us=%llu application-queue-max-us=%llu max-peer=%llu excludes-sdk-queue=1\n",lane,
            static_cast<unsigned long long>(r.count),static_cast<unsigned long long>(r.total/r.count),static_cast<unsigned long long>(r.maximum),static_cast<unsigned long long>(r.peer));}
        rows_={};
    }
private:
    struct Row {std::uint64_t count=0,total=0,maximum=0,peer=0;};
    std::array<Row,5> rows_{};Clock::time_point reported_{};
};
// Owner-local admission counters. These measure encrypted application bytes
// accepted by the underlay, excluding IP/DTLS/SCTP headers and SDK retransmits.
class TrafficMeter {
public:
    using Clock=std::chrono::steady_clock;
    struct Lane {std::uint64_t packets=0,bytes=0,blocked=0;};
    void sent(unsigned lane,std::size_t bytes,bool accepted){if(lane<5){auto& v=lanes_[lane];if(accepted){++v.packets;v.bytes+=bytes;}else ++v.blocked;}}
    const std::array<Lane,5>& lanes()const{return lanes_;}
    void reset(){lanes_={};previous_={};started_={};}
    void pulse(Clock::time_point now=Clock::now()) {
        if(!started_.time_since_epoch().count()){started_=now;return;}
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-started_).count();if(ms<5000)return;
        for(unsigned i=0;i<5;++i){const auto v=lanes_[i],old=previous_[i];if(v.packets==old.packets&&v.blocked==old.blocked)continue;
            std::fprintf(stderr,"[online][traffic] lane=%u interval-ms=%lld admitted-packets=%llu admitted-bytes=%llu blocked=%llu admitted-kbit-s=%.2f excludes-underlay-overhead=1\n",i,static_cast<long long>(ms),
                static_cast<unsigned long long>(v.packets-old.packets),static_cast<unsigned long long>(v.bytes-old.bytes),static_cast<unsigned long long>(v.blocked-old.blocked),double(v.bytes-old.bytes)*8./double(ms));}
        previous_=lanes_;started_=now;
    }
private:
    std::array<Lane,5> lanes_{},previous_{};Clock::time_point started_{};
};
}
