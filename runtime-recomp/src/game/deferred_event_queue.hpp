#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace dkr::runtime {
// Full guest queues must not keep the idle thread runnable. The guest is
// serialized: defer() and retry() are called by its current execution owner,
// so a receive cannot race between the failed send and its deferral. Native
// producers only enqueue; they never inspect or mutate guest memory.
template<class Message>
class DeferredEventQueue {
public:
    bool enqueue(Message message) {
        { std::lock_guard lock(mutex_); ready_.push_back(std::move(message)); }
        changed_.notify_one();
        return true;
    }
    void defer(Message message) {
        std::lock_guard lock(mutex_);
        blocked_.push_back(std::move(message));
    }
    template<class Key>
    void retry(Key queue) {
        bool moved = false;
        {
            std::lock_guard lock(mutex_);
            for (auto it = blocked_.begin(); it != blocked_.end();) {
                if (it->mq == queue) {
                    ready_.push_back(std::move(*it));
                    it = blocked_.erase(it);
                    moved = true;
                } else ++it;
            }
        }
        if (moved) changed_.notify_one();
    }
    bool try_dequeue(Message& message) {
        std::lock_guard lock(mutex_);
        return pop(ready_, message);
    }
    // Reset runs after guest threads stop. Drain both lists so delivery
    // acknowledgements/ownership survive quit and repeated game launches.
    bool try_dequeue_any(Message& message) {
        std::lock_guard lock(mutex_);
        return pop(ready_, message) || pop(blocked_, message);
    }
    void wait_dequeue(Message& message) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return !ready_.empty(); });
        pop(ready_, message);
    }
    template<class Rep, class Period>
    bool wait_dequeue_timed(Message& message,
                           std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);
        if (!changed_.wait_for(lock, timeout, [&] { return !ready_.empty(); })) return false;
        return pop(ready_, message);
    }
private:
    static bool pop(std::deque<Message>& queue, Message& message) {
        if (queue.empty()) return false;
        message = std::move(queue.front()); queue.pop_front(); return true;
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Message> ready_, blocked_;
};
}
