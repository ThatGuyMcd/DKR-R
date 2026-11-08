// CPU-only copy/lease measurement. No game, ROM, RSP, GPU or assets.
#include "producer_snapshot.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>
int main(){
    using namespace dkr::runtime::graphics_handoff;
    using Clock=std::chrono::steady_clock;
    ProductionMailbox mailbox;std::vector<std::uint8_t> memory(0x800000,0x35);
    std::vector<double> samples;unsigned copies=0;std::size_t bytes=0;std::uint64_t check=0;
    for(unsigned i=0;i<1032;++i){
        memory.back()=std::uint8_t(i);const auto start=Clock::now();
        if(!mailbox.capture(memory.data(),0x80001000,0x80001100,[&](auto* dst,const auto* src,auto n){++copies;bytes+=n;std::memcpy(dst,src,n);}))return 1;
        auto image=mailbox.take(memory.data(),0x80001000,32);
        if(!image || image[0x7FFFFF]!=std::uint8_t(i))return 2;
        check+=image[0]+image[0x7FFFFF];
        const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if(i>=32)samples.push_back(ms);
    }
    const auto stats=mailbox.metrics();
    if(copies!=1032 || bytes!=std::size_t(1032)*0x800000 || stats.fallback || stats.mismatch || stats.overflow)return 3;
    std::sort(samples.begin(),samples.end());
    std::cout<<"1000 steady-state producer copy/transfers after 32 warmups; mean-ms="
        <<std::accumulate(samples.begin(),samples.end(),0.0)/samples.size()<<" median-ms="<<samples[499]
        <<" p95-ms="<<samples[949]<<" max-ms="<<samples.back()<<" copies-per-transfer=1 checksum="<<check
        <<"; CPU synthetic measurement, not gameplay FPS\n";
}
