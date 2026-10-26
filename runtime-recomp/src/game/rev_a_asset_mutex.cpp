#include "rev_a_asset_mutex.hpp"

#include "recomp.h"
#include "ultramodern/ultra64.h"

#include <atomic>
#include <cstdint>

namespace {

struct Counters {
    std::atomic<std::uint64_t> fast_acquires{0};
    std::atomic<std::uint64_t> scheduler_acquires{0};
    std::atomic<std::uint64_t> fast_releases{0};
    std::atomic<std::uint64_t> scheduler_releases{0};
};

Counters g_counters;

gpr ResultRegister(s32 result) {
    return static_cast<gpr>(static_cast<std::int64_t>(result));
}

bool CanReceiveWithoutScheduling(const OSMesgQueue& queue) {
    return queue.validCount > 0 && queue.msgCount > 0 && queue.blocked_on_send == NULLPTR;
}

bool CanSendWithoutScheduling(const OSMesgQueue& queue) {
    return queue.validCount >= 0 && queue.validCount < queue.msgCount && queue.msgCount > 0 &&
           queue.blocked_on_recv == NULLPTR;
}

} // namespace

void dkr::runtime::rev_a_asset_mutex::reset_statistics() {
    g_counters.fast_acquires.store(0, std::memory_order_relaxed);
    g_counters.scheduler_acquires.store(0, std::memory_order_relaxed);
    g_counters.fast_releases.store(0, std::memory_order_relaxed);
    g_counters.scheduler_releases.store(0, std::memory_order_relaxed);
}

dkr::runtime::rev_a_asset_mutex::Statistics dkr::runtime::rev_a_asset_mutex::statistics() {
    return {
        .fast_acquires = g_counters.fast_acquires.load(std::memory_order_relaxed),
        .scheduler_acquires = g_counters.scheduler_acquires.load(std::memory_order_relaxed),
        .fast_releases = g_counters.fast_releases.load(std::memory_order_relaxed),
        .scheduler_releases = g_counters.scheduler_releases.load(std::memory_order_relaxed),
    };
}

// DKR Rev A serialises asset DMA through a one-token OSMesgQueue. On original
// hardware that queue is also the mutex. N64ModernRuntime's general message
// path additionally drains host events and evaluates the emulated scheduler on
// every uncontended receive/send pair, which v1.0 never performs. These hooks
// retain the queue and its token exactly, but handle the common uncontended
// case directly. If another emulated thread is genuinely waiting, control is
// delegated to the original runtime functions so blocking, wake-up and thread
// priority semantics remain unchanged.
extern "C" void dkr_v11_asset_mutex_acquire(std::uint8_t* rdram, recomp_context* context) {
    const auto queue_address = static_cast<PTR(OSMesgQueue)>(context->r4);
    const auto message_address = static_cast<PTR(OSMesg)>(context->r5);
    OSMesgQueue* queue = TO_PTR(OSMesgQueue, queue_address);

    if (CanReceiveWithoutScheduling(*queue)) {
        if (message_address != NULLPTR) {
            *TO_PTR(OSMesg, message_address) = TO_PTR(OSMesg, queue->msg)[queue->first];
        }
        queue->first = (queue->first + 1) % queue->msgCount;
        --queue->validCount;
        context->r2 = ResultRegister(0);
        g_counters.fast_acquires.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    context->r2 = ResultRegister(osRecvMesg(rdram, queue_address, message_address, OS_MESG_BLOCK));
    g_counters.scheduler_acquires.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void dkr_v11_asset_mutex_release(std::uint8_t* rdram, recomp_context* context) {
    const auto queue_address = static_cast<PTR(OSMesgQueue)>(context->r4);
    const auto message = static_cast<OSMesg>(context->r5);
    OSMesgQueue* queue = TO_PTR(OSMesgQueue, queue_address);

    if (CanSendWithoutScheduling(*queue)) {
        const s32 last = (queue->first + queue->validCount) % queue->msgCount;
        TO_PTR(OSMesg, queue->msg)[last] = message;
        ++queue->validCount;
        context->r2 = ResultRegister(0);
        g_counters.fast_releases.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    context->r2 = ResultRegister(osSendMesg(rdram, queue_address, message, OS_MESG_NOBLOCK));
    g_counters.scheduler_releases.fetch_add(1, std::memory_order_relaxed);
}
