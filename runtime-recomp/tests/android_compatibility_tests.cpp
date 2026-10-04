#include "../src/game/android_memory_policy.hpp"
#include "../src/game/android_memory_budget.hpp"
#include "../src/game/performance_capture.hpp"
#include "../src/game/performance_trace.hpp"
#include "../src/game/surface_registry.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Surface { std::atomic<int> refs{1}; };
void acquire(Surface* surface) { CHECK(surface->refs.fetch_add(1) > 0); }
void release(Surface* surface) { CHECK(surface->refs.fetch_sub(1) > 0); }
int main() {
    using namespace dkr::runtime;
    Surface a, b;
    {
        using Registry = SurfaceRegistry<Surface, acquire, release>;
        Registry registry;
        CHECK(!registry.matches(0));
        registry.publish(&a);
        {
            Registry::Lease old(registry);
            CHECK(a.refs == 3 && old.window == &a && registry.matches(old.generation));
            registry.publish(&a);
            CHECK(registry.matches(old.generation));
            registry.publish(nullptr);
            CHECK(a.refs == 2 && !registry.matches(old.generation));
            registry.publish(&a); // Same pointer, different lifetime/generation.
            CHECK(!registry.matches(old.generation));
        }
        CHECK(a.refs == 2);
        std::thread reader([&] { for (int i = 0; i < 20000; ++i) {
            Registry::Lease lease(registry);
            if (lease.window) CHECK(lease.window->refs >= 2);
        }});
        for (int i = 0; i < 20000; ++i) registry.publish(i % 3 == 0 ? nullptr : i % 3 == 1 ? &a : &b);
        reader.join();
    }
    CHECK(a.refs == 1 && b.refs == 1);
    for (std::uint64_t size : std::array<std::uint64_t, 5>{0, 1, 4096, 16384, UINT64_MAX}) {
        const auto whole = android_graphics::mapped_range(size, false, 17, 99);
        CHECK(whole.valid && whole.offset == 0 && whole.size == size);
        const auto empty = android_graphics::mapped_range(size, true, size, size);
        CHECK(empty.valid && empty.size == 0);
    }
    const auto unaligned = android_graphics::mapped_range(16384, true, 3, 4093);
    CHECK(unaligned.valid && unaligned.offset == 3 && unaligned.size == 4090);
    CHECK(!android_graphics::mapped_range(4096, true, 4000, 3999).valid);
    CHECK(!android_graphics::mapped_range(4096, true, 0, 4097).valid);
    CHECK(!android_graphics::mapped_range(4096, true, UINT64_MAX, 0).valid);
    CHECK(android_memory::limit_for_trim(20) == 512ULL * 1024 * 1024);
    CHECK(android_memory::limit_for_trim(40) == 512ULL * 1024 * 1024);
    android_memory::trim(10); CHECK(android_memory::unused_cache_limit == 128ULL * 1024 * 1024);
    android_memory::trim(20); CHECK(android_memory::unused_cache_limit == 128ULL * 1024 * 1024);
    android_memory::trim(15); CHECK(android_memory::unused_cache_limit == 64ULL * 1024 * 1024);
    CHECK(!performance_capture::in_window(0, 0));
    CHECK(performance_capture::in_window(59, 60));
    CHECK(!performance_capture::in_window(60, 60));
    CHECK(!performance_capture::active());
    const auto base = performance_trace::snapshot();
    performance_capture::start();
    CHECK(performance_capture::active() && performance_trace::enabled());
    std::thread renderer([] {
        for (unsigned i = 0; i != 40000; ++i) {
            performance_trace::Scope sample(performance_trace::Region::UiBuild);
            performance_capture::present();
        }
    });
    renderer.join();
    const auto report = performance_capture::finish();
    CHECK(!performance_capture::active());
    CHECK(performance_capture::count == 32768 && performance_capture::overflow > 0);
    CHECK(report.find("p50/p95/p99") != std::string::npos);
    CHECK(performance_trace::describe_delta(base).find("UI-build calls=40000") != std::string::npos);
    performance_capture::start();
    CHECK(performance_capture::finish().find("unavailable") != std::string::npos);
    std::cout << "PASS: surface generations/reference lifetimes, map ranges, pressure budgets, bounded opt-in capture and counters\n";
}
