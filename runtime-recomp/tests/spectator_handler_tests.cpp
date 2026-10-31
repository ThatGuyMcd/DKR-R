#include "party_runtime_fixture.hpp"

namespace dkr::runtime::spectators {
namespace {
struct HookFixture {
    Fixture f{true};
    bool allow = false;
    HookFixture() {
        register_owned_observer(&f.world, {this, [](void*, Cursor, std::string&) { return Work::Ready; },
                                           [](void* p, const Baseline& b, std::string& error) {
                                               auto& self = *static_cast<HookFixture*>(p);
                                               if (!self.allow) {
                                                   error = "synthetic permanent admission rejection";
                                                   return false;
                                               }
                                               self.f.world.committed = b.next.frame;
                                               return self.f.world.restore(*b.state, error);
                                           },
                                           [](void*) -> std::uint32_t { return 0; }, [](void*, bool) {}});
    }
    ~HookFixture() {
        unregister_owned_observer(&f.world);
    }
    void offer(std::uint64_t epoch) {
        auto b = f.baseline({epoch, 0});
        f.runtime->held = b;
        check(f.runtime->playback.offer(b, f.error), "synthetic baseline offer");
    }
};
void bounded_failure() {
    HookFixture h;
    const auto now = Clock::now();
    for (unsigned attempt = 1; attempt <= 5; ++attempt) {
        h.offer(92);
        const bool running = h.f.runtime->pulse(now + std::chrono::milliseconds(attempt * 10), {}, h.f.error);
        check(running == (attempt < 5), "Only five failed spectator admissions are allowed");
        check(h.f.runtime->observer_retry.failures() == attempt, "Handler must count actual admission failures");
        check(!h.f.runtime->driver && h.f.world.committed == 0, "Failed watcher must not simulate racing input");
        check(h.f.hub->enabled && !h.f.hub->stopping, "Watcher recovery must not close the racing lobby");
    }
    check(h.f.error.find("synthetic permanent admission rejection") != std::string::npos,
          "Final failure must retain concrete restore reason");
}
void recovery_and_progress() {
    HookFixture h;
    const auto now = Clock::now();
    h.offer(92);
    check(h.f.runtime->pulse(now, {}, h.f.error), "First failure is recoverable");
    check(h.f.runtime->observer_retry.failures() == 1, "Failed admission counted");
    h.allow = true;
    h.offer(92);
    check(h.f.runtime->pulse(now + std::chrono::milliseconds(10), {}, h.f.error),
          "Verified baseline admission should recover");
    check(h.f.runtime->observer_retry.failures() == 1, "Admission alone must not reset the failure streak");
    const std::array frames{ConfirmedFrame{{92, 0}, {}, {}, false}};
    check(h.f.runtime->playback.append(frames, h.f.error) == FeedResult::Accepted, "Confirmed history appended");
    check(h.f.runtime->pulse(now + std::chrono::milliseconds(20), {}, h.f.error),
          "Recovered watcher must consume confirmed frame");
    check(h.f.world.committed == 1 && h.f.runtime->observer_retry.failures() == 0,
          "Only committed playback clears failure streak");
    check(h.f.runtime->observer_render_epoch == 92, "First live frame is diagnosed once per epoch");
    check(!h.f.runtime->driver && !h.f.runtime->simulating(), "Recovered watcher does not become a racer");
}
} // namespace
} // namespace dkr::runtime::spectators
int main() {
    using namespace dkr::runtime::spectators;
    try {
        bounded_failure();
        recovery_and_progress();
        std::cout << assertions
                  << " spectator handler checks passed; actual party handler with synthetic owner hooks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SPECTATOR HANDLER FAILURE: " << e.what() << '\n';
        return 1;
    }
}
