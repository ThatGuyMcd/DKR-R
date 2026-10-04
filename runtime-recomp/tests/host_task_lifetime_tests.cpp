#include "../src/game/host_task_lifetime.hpp"
#include <atomic>
#include <vector>

#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while(false)
using namespace dkr::runtime::host_tasks;
using namespace std::chrono_literals;
int main() {
    Registry r;
    const auto now = Clock::now();
    auto gfx = r.begin(0x80100010, 1, now);
    CHECK(r.protect(0x00100010, Engine::Sp, 10));
    CHECK(r.protect(0xA0100010, Engine::Dp, 1000));
    CHECK(!r.protect(0x80100110, Engine::Sp, 11));
    bool rejected = false;
    try { r.begin(0xA0100010, 1); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    Task stalled;
    CHECK(!r.take_stall(stalled, now + 14s));
    CHECK(r.take_stall(stalled, now + 15s));
    CHECK(stalled.token.generation == gfx.generation && stalled.pending == 3);
    CHECK(!r.take_stall(stalled, now + 60s));
    // Reporting a hang never invents a completion or permits early reuse.
    CHECK(r.protect(gfx.address, Engine::Dp, 999));
    r.consume(gfx, Engine::Sp, now + 16s);
    CHECK(!r.lookup(gfx.address, Engine::Sp));
    CHECK(r.lookup(gfx.address, Engine::Dp));
    r.mark(gfx, Stage::Parse, now + 16s);
    r.consume(gfx, Engine::Dp, now + 17s);
    CHECK(!r.lookup(gfx.address, Engine::Dp));
    auto next = r.begin(gfx.address, 1, now + 18s);
    CHECK(next.generation != gfx.generation);
    r.consume(gfx, Engine::Sp); // Late old token cannot retire new work.
    r.consume(gfx, Engine::Dp);
    CHECK(r.lookup(next.address, Engine::Sp));
    CHECK(r.lookup(next.address, Engine::Dp));
    r.reset();
    auto audio = r.begin(next.address, 2, now);
    CHECK(audio.generation != next.generation);
    CHECK(r.lookup(audio.address, Engine::Sp));
    CHECK(!r.lookup(audio.address, Engine::Dp));
    r.consume(next, Engine::Sp);
    CHECK(r.lookup(audio.address, Engine::Sp));
    r.consume(audio, Engine::Sp);
    CHECK(!r.lookup(audio.address, Engine::Sp));
    for (auto bad : {0U, 0x80800000U, 0x807FFFD0U, 0xC0100000U}) {
        rejected = false;
        try { r.begin(bad, 1); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    }
    // Independent queued tasks keep ownership while workers and the scheduler
    // concurrently record/consume edges. Exercise reuse thousands of times.
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        auto address = 0x80200010U + worker * 0x100;
        for (int i = 0; i < 2000; ++i) {
            auto task = r.begin(address, 1);
            CHECK(r.lookup(address, Engine::Sp).generation == task.generation);
            r.mark(task, Stage::SpNotify);
            r.consume(task, Engine::Sp);
            CHECK(r.lookup(address, Engine::Dp).generation == task.generation);
            r.mark(task, Stage::Parse);
            r.mark(task, Stage::DpNotify);
            r.consume(task, Engine::Dp);
        }
    });
    for (auto& worker : workers) worker.join();
    CHECK(!r.take_stall(stalled, Clock::now() + 30s));
    return 0;
}
