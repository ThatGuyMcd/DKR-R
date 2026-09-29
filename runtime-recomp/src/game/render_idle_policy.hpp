#pragma once

#include <mutex>
#include <utility>

namespace dkr::runtime::render_idle {
enum class Result { Submitted, RendererBusy, QueuedWork, WorkerBusy };

// Keep-awake is optional dummy work, not another renderer. Acquire locks in the
// real renderer's order and never wait for admission. The first lock covers CPU
// preparation, uploads and presentation dependencies, not just command recording.
// pending() must also be non-blocking and return true if it cannot inspect the
// queue. A newly arriving frame can still follow one already admitted submission;
// neither this helper nor the old path can preempt an in-flight GPU command.
template<class Pending, class Submit>
Result try_submit(std::mutex& renderer, std::mutex& worker, Pending&& pending, Submit&& submit) {
    std::unique_lock render_lock(renderer, std::try_to_lock);
    if (!render_lock.owns_lock()) return Result::RendererBusy;
    if (std::forward<Pending>(pending)()) return Result::QueuedWork;
    std::unique_lock worker_lock(worker, std::try_to_lock);
    if (!worker_lock.owns_lock()) return Result::WorkerBusy;
    std::forward<Submit>(submit)();
    return Result::Submitted;
}
}
