#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace dkr::online_stability {
// Source-local durations and receiver-local deadlines only. These counters do
// not infer one-way latency by subtracting clocks on different computers.
class InputTrace final {
    using Clock=std::chrono::steady_clock;
    struct Owner {std::uint64_t useful=0,duplicate=0,repairs=0,rejected=0;std::uint32_t gap=UINT32_MAX;Clock::time_point since{};};
    struct Event {std::uint32_t frame=0,ms=0;unsigned owner=0;std::uint64_t peer=0;unsigned kind=0;};
    std::array<Owner,4> owners_{};
    std::array<Event,64> events_{};
    std::array<std::uint64_t,12> ack_{};
    static constexpr std::array<unsigned,12> limits_{10,25,50,100,150,250,400,700,1200,2000,4000,UINT32_MAX};
    Clock::time_point start_{},report_{};
    std::uint64_t epoch_=0,serial_=0;
    unsigned local_=4,last_frame_=0;
public:
    void reset(std::uint64_t epoch,unsigned local,Clock::time_point now,unsigned frame=0){*this={};epoch_=epoch;local_=local;start_=report_=now;last_frame_=frame;}
    void event(unsigned owner,std::uint64_t peer,unsigned kind,std::uint32_t frame,Clock::time_point now) {
        if(owner>=4)return;
        const auto ms=now>=start_?std::chrono::duration_cast<std::chrono::milliseconds>(now-start_).count():0;
        events_[serial_++%events_.size()]={frame,std::uint32_t(std::min<std::int64_t>(UINT32_MAX,ms)),owner,peer,kind};
        if(kind==2)++owners_[owner].useful;else if(kind==3)++owners_[owner].duplicate;else if(kind==4)++owners_[owner].repairs;else if(kind==7)++owners_[owner].rejected;
    }
    void receipt(std::chrono::milliseconds age){unsigned n=0;while(age.count()>limits_[n])++n;++ack_[n];}
    void pulse(std::uint32_t frame,std::uint32_t confirmed,const std::array<std::uint32_t,4>& have,std::uint8_t missing,Clock::time_point now) {
        for(unsigned o=0;o<4;++o){auto& row=owners_[o];if(!(missing&(1U<<o))||o==local_){row.gap=UINT32_MAX;row.since={};}
            else if(row.gap!=have[o]){row.gap=have[o];row.since=now;}}
        if(now-report_<std::chrono::seconds(5))return;
        const auto seconds=std::chrono::duration<double>(now-report_).count();
        std::uint64_t total=0;for(auto n:ack_)total+=n;std::uint64_t sum=0;unsigned p95=0;
        for(unsigned n=0;n<ack_.size();++n){sum+=ack_[n];if(sum>=(total*95+99)/100){p95=total?limits_[n]:0;break;}}
        std::fprintf(stderr,"[party][input-rate] epoch=%llu local=%u frontier=%u confirmed=%u frontier-hz=%.2f authored-to-host-receipt-p95-upper-ms=%u receipt-samples=%llu\n",
            static_cast<unsigned long long>(epoch_),local_,frame,confirmed,frame>=last_frame_?(frame-last_frame_)/seconds:0.,p95,static_cast<unsigned long long>(total));
        report_=now;last_frame_=frame;
        for(unsigned o=0;o<4;++o){const auto& row=owners_[o];const auto age=row.since.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(now-row.since).count():0;
            std::fprintf(stderr,"[party][input-owner] local=%u owner=%u have=%u missing-frame=%u gap-age-ms=%lld useful=%llu duplicate=%llu repair-sends=%llu rejected=%llu\n",local_,o,have[o],row.gap,static_cast<long long>(age),
                static_cast<unsigned long long>(row.useful),static_cast<unsigned long long>(row.duplicate),static_cast<unsigned long long>(row.repairs),static_cast<unsigned long long>(row.rejected));}
    }
    void dump()const {
        for(auto n=serial_>events_.size()?serial_-events_.size():0;n<serial_;++n){const auto& e=events_[n%events_.size()];
            std::fprintf(stderr,"[party][input-flight] epoch=%llu local=%u owner=%u peer=%llu kind=%u frame=%u local-ms=%u\n",static_cast<unsigned long long>(epoch_),local_,e.owner,static_cast<unsigned long long>(e.peer),e.kind,e.frame,e.ms);}
    }
};
}
