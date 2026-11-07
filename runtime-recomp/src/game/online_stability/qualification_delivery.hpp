#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace dkr::online_stability::qualification_delivery {
// Armed only by the NEW disposable-profile CLI qualification entry point.
// Production launcher/overlay sessions cannot enable this from environment.
using Clock=std::chrono::steady_clock;
inline std::atomic<bool> enabled{false};
inline unsigned delay_ms=0,jitter_ms=0,spike_ms=0,loss_every=0;
inline std::atomic<std::uint64_t> disposable{0},dropped{0};
inline bool arm(unsigned delay,unsigned jitter,unsigned spike,unsigned loss) {
    if(delay>2000||jitter>100||spike>1000||(loss&&loss<2))return false;
    delay_ms=delay;jitter_ms=jitter;spike_ms=spike;loss_every=loss;
    disposable=0;dropped=0;enabled.store(true,std::memory_order_release);return true;
}
inline bool drop(unsigned traffic) {
    if(!enabled.load(std::memory_order_acquire)||!loss_every||(traffic!=2&&traffic!=3))return false;
    if((disposable.fetch_add(1,std::memory_order_relaxed)+1)%loss_every)return false;
    dropped.fetch_add(1,std::memory_order_relaxed);return true;
}
template<class Source>class Queue final {
    struct Packet {Source source;std::vector<std::uint8_t> bytes;Clock::time_point queued,ready;};
    std::vector<Packet> packets_;
    std::size_t bytes_=0,peak_=0;
    std::uint64_t sequence_=0,admitted_=0,delivered_=0,min_us_=UINT64_MAX,max_us_=0;
    Clock::time_point report_at_{};
public:
    void clear(){packets_.clear();bytes_=peak_=0;sequence_=admitted_=delivered_=max_us_=0;min_us_=UINT64_MAX;report_at_={};}
    template<class Fetch>bool receive(Source& source,std::vector<std::uint8_t>& bytes,std::string& error,Fetch fetch,Clock::time_point now=Clock::now()) {
        if(!enabled.load(std::memory_order_acquire)){if(!packets_.empty())clear();return fetch(source,bytes,error);}
        // Exhaustion fails the isolated qualification rather than hiding loss.
        for(unsigned n=0;n<128;++n) {
            Source incoming;std::vector<std::uint8_t> payload;
            if(!fetch(incoming,payload,error)){if(!error.empty())return false;break;}
            if(packets_.size()>=8192||payload.size()>8U*1024U*1024U-bytes_) {
                error="Isolated qualification delivery queue exhausted";return false;
            }
            const int jitter=jitter_ms?int((++sequence_*2654435761U)%(2*jitter_ms+1))-int(jitter_ms):0;
            const auto phase=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%6000;
            const unsigned spike=phase>=3000&&phase<3200?spike_ms:0;
            const auto due=now+std::chrono::milliseconds(std::max(0,int(delay_ms)+jitter)+spike);
            bytes_+=payload.size();peak_=std::max(peak_,bytes_);++admitted_;
            packets_.push_back({incoming,std::move(payload),now,due});
            std::push_heap(packets_.begin(),packets_.end(),[](const auto& a,const auto& b){return a.ready>b.ready;});
        }
        if(!report_at_.time_since_epoch().count())report_at_=now+std::chrono::seconds(5);
        if(now>=report_at_) {
            std::fprintf(stderr,"[online][qualification-delivery] delay-ms=%u jitter-ms=%u spike-ms=%u loss-every=%u admitted=%llu delivered=%llu dropped=%llu pending=%zu peak-bytes=%zu actual-min-us=%llu actual-max-us=%llu\n",
                delay_ms,jitter_ms,spike_ms,loss_every,static_cast<unsigned long long>(admitted_),static_cast<unsigned long long>(delivered_),
                static_cast<unsigned long long>(dropped.load()),packets_.size(),peak_,static_cast<unsigned long long>(min_us_==UINT64_MAX?0:min_us_),static_cast<unsigned long long>(max_us_));
            report_at_=now+std::chrono::seconds(5);
        }
        if(packets_.empty()||packets_.front().ready>now){bytes.clear();error.clear();return false;}
        std::pop_heap(packets_.begin(),packets_.end(),[](const auto& a,const auto& b){return a.ready>b.ready;});
        auto packet=std::move(packets_.back());packets_.pop_back();bytes_-=packet.bytes.size();
        const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now-packet.queued).count());
        min_us_=std::min(min_us_,us);max_us_=std::max(max_us_,us);++delivered_;
        source=packet.source;bytes=std::move(packet.bytes);error.clear();return true;
    }
};
}
