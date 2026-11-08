#pragma once
#include <chrono>
#include <cstdio>
namespace dkr::runtime::detail {
struct MicroScope {
    double& total;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~MicroScope(){total+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();}
};
struct FrameMetrics {
    using Clock=std::chrono::steady_clock;
    Clock::time_point reported=Clock::now();
    unsigned scene=~0U,frames=0,observations=0;
    double prepare_us=0,submit_us=0;
    void begin(unsigned next,unsigned count,unsigned& poses,unsigned& uploads,unsigned& batches,unsigned& hits,unsigned& captures) {
        const auto now=Clock::now();
        if(scene==next&&frames&&now-reported>=std::chrono::seconds(2)) {
            std::fprintf(stderr,"[graphics][detail-cost] scene=%u frames=%u prepare-avg-ms=%.3f submit-avg-ms=%.3f observations-avg=%.1f poses=%u vertex-uploads=%u batches=%u material-cache-hits=%u material-captures=%u; batched-detail-v2\n",
                scene,frames,prepare_us/(frames*1000.0),submit_us/(frames*1000.0),double(observations)/frames,poses,uploads,batches,hits,captures);
            *this={};scene=next;reported=now;poses=uploads=batches=hits=captures=0;
        }
        if(scene!=next){*this={};scene=next;reported=now;poses=uploads=batches=hits=captures=0;}
        ++frames;observations+=count;
    }
};
}
