#include "producer_snapshot.hpp"
#include "performance_trace.hpp"
#include <atomic>
#include <cstdio>
#include <exception>

namespace dkr::runtime::graphics_handoff {
namespace {ProductionMailbox mailbox;std::atomic<unsigned> failures{0};}
bool capture(const std::uint8_t* ram,std::uint32_t begin,std::uint32_t end) noexcept {
    try {
        const bool ok=mailbox.capture(ram,begin,end,[](std::uint8_t* dst,const std::uint8_t* src,std::size_t bytes){
            performance_trace::Scope copy_cost(performance_trace::Region::SnapshotCopy,bytes);
            std::memcpy(dst,src,bytes);
        });
        static std::atomic_flag notice=ATOMIC_FLAG_INIT;
        if(ok && !notice.test_and_set(std::memory_order_relaxed))
            std::fputs("[render][producer-snapshot] captured before scheduler enqueue; transferred without a second submission copy\n",stderr);
        return ok;
    } catch(const std::exception&) {
        mailbox.invalidate();
        if(failures.fetch_add(1,std::memory_order_relaxed)<3U)
            std::fputs("[render][producer-snapshot] capture unavailable; submission fallback retained\n",stderr);
        return false;
    }
}
Snapshot take(const std::uint8_t* ram,std::uint32_t root,std::uint32_t count){return mailbox.take(ram,root,count);}
Snapshot acquire(){return mailbox.acquire();}
void discard(const std::uint8_t* ram,std::uint32_t root,std::uint32_t count){auto unused=mailbox.take(ram,root,count);}
void reset() noexcept {
    const auto m=mailbox.reset();
    if(m.captured || m.fallback)
        std::fprintf(stderr,"[render][handoff-summary] captured=%llu transferred=%llu fallback=%llu mismatch=%llu overflow=%llu; original SP/DP completion retained\n",
            static_cast<unsigned long long>(m.captured),static_cast<unsigned long long>(m.transferred),
            static_cast<unsigned long long>(m.fallback),static_cast<unsigned long long>(m.mismatch),
            static_cast<unsigned long long>(m.overflow));
}
}
