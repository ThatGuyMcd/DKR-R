#include "../src/game/render_idle_policy.hpp"
#include <atomic>
#include <cstdio>
#include <latch>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)
using dkr::runtime::render_idle::Result;
using dkr::runtime::render_idle::try_submit;

// A different thread owns the mutex: try_lock on one's own std::mutex is UB.
struct HeldLock {
    std::latch ready{1}, release{1};
    std::thread thread;
    explicit HeldLock(std::mutex& mutex) : thread([&] {
        std::unique_lock lock(mutex);
        ready.count_down(); release.wait();
    }) { ready.wait(); }
    ~HeldLock() { release.count_down(); thread.join(); }
};

int main() {
    std::mutex renderer, worker;
    unsigned submitted = 0, inspected = 0;
    auto pending = [&] { ++inspected; return false; };
    auto submit = [&] { ++submitted; };
    CHECK(try_submit(renderer, worker, pending, submit) == Result::Submitted);
    CHECK(submitted == 1 && inspected == 1);
    {
        HeldLock render(renderer);
        unsigned old_admitted = 0;
        for (unsigned i = 0; i < 10000; ++i) {
            // Original admission only checked the worker, which is unused
            // during real-frame CPU preparation. Controlled reproduction.
            { std::unique_lock old(worker, std::try_to_lock); old_admitted += old.owns_lock(); }
            CHECK(try_submit(renderer, worker, pending, submit) == Result::RendererBusy);
        }
        CHECK(old_admitted == 10000 && submitted == 1 && inspected == 1);
        std::puts("Busy real-frame preparation: original admission 10000, candidate admission 0 (not an FPS benchmark)");
    }
    CHECK(try_submit(renderer, worker, [] { return true; }, submit) == Result::QueuedWork);
    CHECK(submitted == 1);
    {
        HeldLock busy_worker(worker);
        CHECK(try_submit(renderer, worker, pending, submit) == Result::WorkerBusy);
    }
    // Shutdown/cursor contention are conservatively reported as pending work.
    std::mutex cursor;
    {
        HeldLock busy_cursor(cursor);
        CHECK(try_submit(renderer, worker, [&] {
            std::unique_lock lock(cursor, std::try_to_lock);
            return !lock.owns_lock();
        }, submit) == Result::QueuedWork);
    }
    bool caught = false;
    try {
        try_submit(renderer, worker, pending, [] { throw std::runtime_error("GPU failure"); });
    } catch (const std::runtime_error&) { caught = true; }
    CHECK(caught);
    CHECK(try_submit(renderer, worker, pending, submit) == Result::Submitted);
    CHECK(submitted == 2); // No leaked lock on any rejection or exception path.

    // Both locks stay owned through the GPU wait; new real work cannot reuse
    // the command list until the admitted dummy has completed, just as before.
    std::latch admitted{1}, finish_gpu{1};
    std::atomic<bool> submitted_gpu{false};
    std::thread idle_gpu([&] {
        submitted_gpu = try_submit(renderer, worker, [] { return false; }, [&] {
            admitted.count_down(); finish_gpu.wait();
        }) == Result::Submitted;
    });
    admitted.wait();
    {
        std::unique_lock a(renderer, std::try_to_lock), b(worker, std::try_to_lock);
        CHECK(!a.owns_lock() && !b.owns_lock());
    }
    finish_gpu.count_down(); idle_gpu.join(); CHECK(submitted_gpu);

    // Concurrent real-work loop and optional idle attempts. Complete every
    // real workload, never overlap command-list ownership, and stop cleanly.
    std::atomic<bool> stop{false}, real_active{false}, overlap{false};
    std::atomic<unsigned> real_frames{0}, idle_frames{0};
    std::thread idle([&] {
        while (!stop.load()) {
            try_submit(renderer, worker, [&] { return stop.load(); }, [&] {
                if (real_active.load()) overlap = true;
                ++idle_frames;
            });
            std::this_thread::yield();
        }
    });
    for (unsigned i = 0; i < 2000; ++i) {
        std::unique_lock render_lock(renderer);
        real_active = true;
        std::this_thread::yield(); // Preparation, worker mutex still available.
        { std::unique_lock worker_lock(worker); ++real_frames; }
        real_active = false;
    }
    stop = true; idle.join();
    CHECK(real_frames == 2000 && !overlap);
    CHECK(try_submit(renderer, worker, pending, submit) == Result::Submitted);
    std::puts("PASS: idle, preparation, queued work, worker/cursor contention, failure unwinding, in-flight ownership, stress and shutdown");
}
