#include "../src/game/graphics_snapshot_pool.hpp"
#include "../src/game/render_resource_policy.hpp"
#include "../src/game/presentation_wait.hpp"
#include "../src/game/performance_trace.hpp"
#include "../src/game/graphics_health.hpp"
#include "../src/game/android_descriptor_policy.hpp"
#include "../src/game/deferred_event_queue.hpp"
#include "../src/game/netplay/social_executor.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)
int main() {
    using namespace dkr::runtime;
    CHECK(performance_trace::enabled());
    const auto idle_baseline = performance_trace::snapshot();
    performance_trace::event(performance_trace::Region::IdleGpuSkipped);
    {
        performance_trace::Scope idle_measurement(performance_trace::Region::IdleGpuSubmit);
    }
    const auto idle_report = performance_trace::describe_delta(idle_baseline);
    CHECK(idle_report.find("idle-gpu-submit calls=1") != std::string::npos);
    CHECK(idle_report.find("idle-gpu-skipped calls=1 wall-ms=0") != std::string::npos);
    {
        performance_trace::Scope measurement(performance_trace::Region::SnapshotCopy, 128);
    }
    auto& counter = performance_trace::counters[0];
    CHECK(counter.calls.load() == 1 && counter.bytes.load() == 128);
    std::thread trace_worker([] {
        for (int i = 0; i < 100; ++i) {
            performance_trace::Scope measurement(performance_trace::Region::SnapshotCopy, 4);
        }
    });
    trace_worker.join();
    CHECK(counter.calls.load() == 101 && counter.bytes.load() == 528);
    CHECK(android_graphics::retry_full_pool(-1000069000, 12, 8192));
    CHECK(android_graphics::retry_full_pool(static_cast<std::int32_t>(0xC4642878U), 12, 8192));
    for (auto result : {0, -1, -2, -4}) CHECK(!android_graphics::retry_full_pool(result, 12, 8192));
    CHECK(!android_graphics::retry_full_pool(-1000069000, 0, 8192));
    CHECK(!android_graphics::retry_full_pool(-1000069000, 8192, 8192));
    CHECK(android_graphics::descriptor_features(true, true, true, true, true));
    CHECK(!android_graphics::descriptor_features(true, true, true, false, true));
    CHECK(!android_graphics::descriptor_features(true, true, true, true, false));
    graphics_health::reset();
    CHECK(!graphics_health::stop_failed_task());
    bool aborted = false;
    try {
        graphics_health::TaskBoundary boundary;
        graphics_health::record(graphics_health::Stage::DescriptorAllocate, -1);
        graphics_health::stop_failed_task();
    } catch (const graphics_health::TaskAborted&) { aborted = true; }
    CHECK(aborted && !graphics_health::can_abort_task);
    CHECK(graphics_health::stop_failed_task());
    graphics_health::reset();
    struct Message { int mq, value; };
    DeferredEventQueue<Message> queue;
    Message message{};
    queue.defer({1, 11}); queue.defer({2, 20}); queue.defer({1, 12});
    CHECK(!queue.try_dequeue(message));
    CHECK(!queue.wait_dequeue_timed(message, std::chrono::milliseconds(5)));
    queue.enqueue({3, 30});
    CHECK(queue.try_dequeue(message) && message.value == 30);
    queue.retry(1);
    CHECK(queue.try_dequeue(message) && message.value == 11);
    CHECK(queue.try_dequeue(message) && message.value == 12);
    CHECK(!queue.try_dequeue(message));
    auto wake = std::async(std::launch::async, [&] {
        Message received{}; queue.wait_dequeue(received); return received.value;
    });
    CHECK(wake.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout);
    queue.retry(2);
    CHECK(wake.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(wake.get() == 20);
    queue.defer({1, 99}); queue.enqueue({0, 0});
    CHECK(queue.try_dequeue_any(message) && message.value == 0);
    CHECK(queue.try_dequeue_any(message) && message.value == 99);
    CHECK(!queue.try_dequeue_any(message));
    graphics_health::reset();
    CHECK(graphics_health::record(graphics_health::Stage::Submit, -4));
    CHECK(!graphics_health::record(graphics_health::Stage::Fence, -4));
    CHECK((graphics_health::failure.load() >> 32) == static_cast<unsigned>(graphics_health::Stage::Submit));
    graphics_health::reset();
    coarse_presentation_wait(std::chrono::steady_clock::now() - std::chrono::seconds(1));
    CHECK(render_resources::image_count(3, 2, 0) == 3);
    CHECK(render_resources::image_count(3, 2, 2) == 2);
    CHECK(render_resources::image_count(1, 2, 0) == 2);
    for (auto threads : {0U, 1U, 2U, 8U, 64U}) {
        const auto mobile = render_resources::workers(threads, true);
        CHECK(mobile.raster >= 1 && mobile.raster <= 2 && mobile.uber <= 2 && mobile.texture == 1);
    }
    GraphicsSnapshotPool<1024, 2> pool;
    auto a = pool.acquire(); auto b = pool.acquire(); auto c = pool.acquire();
    CHECK(a.get() != b.get() && b.get() != c.get());
    a[0] = 17; b[0] = 42; c[0] = 91;
    auto previous = a.get(); a.reset();
    auto reused = pool.acquire();
    CHECK(reused.get() == previous && b[0] == 42 && c[0] == 91);
    GraphicsSnapshotPool<1024>::Snapshot survivor;
    { GraphicsSnapshotPool<1024> temporary; survivor = temporary.acquire(); }
    survivor[0] = 3; survivor.reset();
    std::atomic<unsigned> ticks = 0;
    netplay::SocialExecutor executor;
    executor.start([&] { ++ticks; throw std::runtime_error("fault injection"); },
                   [] { throw std::runtime_error("reporting also failed"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    CHECK(ticks > 0 && ticks <= 5);
    std::promise<void> posted; auto ready = posted.get_future();
    CHECK(executor.post([&] { posted.set_value(); }));
    CHECK(ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    executor.stop();
}
