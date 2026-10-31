#pragma once
// Host-only protocol integration fixture. Compile the exact production party
// runtime, exposing its owner state in THIS test TU only. Its transitive headers
// are included first so the private macro affects only PartyRuntime's handle.
#include "party_link.hpp"
#include "owned_observer.hpp"
#include "owned_game.hpp"
#include <functional>
#include <memory>
#define private public
#include "party_runtime.hpp"
#undef private
#include "../src/game/party_runtime.cpp"
#include <cstring>
#include <iostream>

// No live SDK/development command service is used by this synthetic world.
namespace dkr::sdk2 {
Command take(bool) {
    return {};
}
} // namespace dkr::sdk2

namespace dkr::runtime::spectators {
namespace {
using Clock = PartyRuntime::Clock;
unsigned assertions = 0;
void check(bool result, const char* why) {
    ++assertions;
    if (!result)
        throw std::runtime_error(why);
}
Digest digest(std::span<const std::uint8_t> bytes) {
    Digest result{};
    result.back() = 1;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        result[i % result.size()] ^= bytes[i];
    return result;
}
class FixtureWorld final : public ex::OwnedGame {
  public:
    std::array<std::uint8_t, 16> state{};
    std::uint32_t committed = 0;
    unsigned captures = 0, preparations = 0;
    ex::SimulationContract contract() const override {
        return {101, state.size(), ex::kRequiredStateDomains, true, true, true};
    }
    bool capture(std::span<std::uint8_t> out, std::string&) override {
        ++captures;
        std::copy(state.begin(), state.end(), out.begin());
        return true;
    }
    bool restore(std::span<const std::uint8_t> in, std::string&) override {
        std::copy(in.begin(), in.end(), state.begin());
        return true;
    }
    bool tick(std::uint32_t, const net::FrameInputs&, ex::TickOutput&, std::string&) override {
        return true;
    }
    bool commit(std::uint64_t, std::uint32_t frame, std::span<const std::uint8_t>, bool, std::string&) override {
        committed = frame + 1;
        return true;
    }
    bool confirmed_boundary(ex::BoundaryIntent& b, std::string&) override {
        b = {12, 34};
        return true;
    }
    ex::PreparationStep prepare_scene(std::uint64_t, std::uint64_t& proof, std::string&) override {
        ++preparations;
        ++state[0];
        committed = 0;
        proof = state[0];
        return ex::PreparationStep::Ready;
    }
    ex::OwnedSceneView scene_view() const override {
        return {};
    }
    bool publish_current(std::uint64_t, std::uint32_t, std::string&) override {
        return true;
    }
    bool admit_initial(std::span<const std::uint8_t> in, std::string& e) override {
        return restore(in, e);
    }
    std::uint32_t confirmed_count() const override {
        return committed;
    }
    std::span<const std::uint8_t> confirmed_save() const override {
        return {};
    }
    std::span<const std::uint8_t> confirmed_paks() const override {
        return {};
    }
    bool install_initial_paks(std::uint64_t, std::span<const std::uint8_t>, std::string&) override {
        return true;
    }
    void set_confirmed_rumble_sink(ex::ConfirmedRumbleSink) override {}
};
struct Fixture {
    FixtureWorld world;
    ex::PresentationMailbox box;
    net::LaunchDescriptor launch{};
    std::shared_ptr<PartyLink> hub;
    std::unique_ptr<PartyRuntime::Impl> runtime;
    std::string error;
    explicit Fixture(bool spectator = false, bool host = false, bool watcher = true) {
        launch.player_count = 2;
        launch.occupied_mask = 3;
        launch.rollback_window = 14;
        launch.input_delay_frames = 6;
        launch.match_id = 1234;
        hub = party_link(this);
        hub->enabled = hub->admitted = true;
        hub->host = host;
        hub->local = host ? 1 : spectator ? 3 : 2;
        hub->authority = 1;
        hub->launch = launch;
        check(hub->party.create(1, "Host", error), "fixture host");
        check(hub->party.add(2, "Client", std::uint8_t(1), error), "fixture racer");
        if (watcher)
            check(hub->party.add(3, "Watcher", {}, error), "fixture spectator");
        runtime = std::make_unique<PartyRuntime::Impl>(this, world, box, launch, digest);
        check(runtime->begin(error), "runtime begin");
    }
    ~Fixture() {
        runtime.reset();
        forget_party_link(this);
    }
    Baseline baseline(Cursor at) {
        auto state = std::make_shared<Bytes>(world.state.begin(), world.state.end());
        return {at, runtime->match, world.contract().schema, runtime->controls, digest(*state), state, 0};
    }
    void go(Cursor at, std::uint64_t generation = 1, MemberId sender = 1, std::optional<PartySnapshot> roster = {}) {
        Write body;
        body.at(at).n(generation).data(wire::roster(roster.value_or(hub->party.snapshot())));
        Write packet;
        packet.n('D', 1).n('P', 1).n('R', 1).n(2, 1).n(unsigned(Msg::Go), 1).n(runtime->match).n(99).data(body.b);
        runtime->message(sender, packet.b, Clock::now());
    }
    void initial() {
        runtime->ready(baseline({91, 0}));
        go({91, 0});
        check(runtime->go && runtime->phase == PartyRuntime::Impl::Phase::Running, "initial release must work");
    }
};
void racer_transitions() {
    Fixture f;
    f.initial();
    for (std::uint64_t epoch = 92; epoch < 98; ++epoch) {
        f.runtime->phase = PartyRuntime::Impl::Phase::ScenePrepare;
        f.runtime->boundary = {12, 34};
        f.runtime->scene_step(Clock::now());
        check(f.runtime->cursor == Cursor{epoch, 0}, "actual scene preparation advances cursor");
        check(f.runtime->phase == PartyRuntime::Impl::Phase::SceneReady && !f.runtime->driver,
              "prepared racer awaits release with no driver");
        f.go({epoch, 0});
        check(f.runtime->phase == PartyRuntime::Impl::Phase::Running && bool(f.runtime->driver),
              "FIRST POST-LOAD RELEASE WAS DISCARDED");
        check(f.runtime->next() == Cursor{epoch, 0}, "release starts exact loaded epoch");
        const auto captures = f.world.captures;
        f.runtime->frozen[999] = {};
        for (unsigned retry = 0; retry < 10; ++retry)
            f.go({epoch, 0});
        check(f.world.captures == captures && f.runtime->frozen.contains(999),
              "duplicate retries must not reset driver or phase votes");
        f.go({epoch - 1, 0});
        check(f.runtime->cursor == Cursor{epoch, 0}, "stale release must not rewind");
        f.runtime->frozen.clear();
    }
    check(f.world.preparations == 6, "multiple actual scene preparations exercised");
    // A newer control generation at the SAME cursor remains a valid seat swap.
    f.go({97, 0}, 2);
    check(f.runtime->controls == 2, "same-cursor control generation may advance");
    const auto captures = f.world.captures;
    f.go({97, 0}, 1);
    f.go({97, 0}, 2);
    check(f.world.captures == captures && f.runtime->controls == 2, "old control/duplicate cannot reactivate");
    f.go({98, 0}, 1);
    check(f.runtime->cursor == Cursor{97, 0}, "stale controls cannot release a future scene");
}
void watcher_orders() {
    {
        Fixture f(true);
        f.initial();
        ++f.world.state[0];
        f.runtime->ready(f.baseline({92, 0}));
        check(f.runtime->cursor == Cursor{92, 0}, "checkpoint admission advances spectator cursor");
        f.hub->running = false;
        f.go({92, 0});
        check(f.hub->running, "checkpoint-before-Go must activate new spectator release");
        auto state = f.runtime->held->state;
        f.hub->running = false;
        f.go({92, 0});
        check(!f.hub->running && f.runtime->held && f.runtime->held->state == state,
              "spectator duplicate must not re-offer checkpoint");
    }
    {
        Fixture f(true);
        f.initial();
        f.go({92, 0});
        check(f.runtime->cursor == Cursor{92, 0} && !f.runtime->held, "Go-before-checkpoint requests resync");
        ++f.world.state[0];
        f.runtime->ready(f.baseline({92, 0}));
        f.hub->running = false;
        f.go({92, 0});
        check(!f.hub->running && bool(f.runtime->held), "retry after late checkpoint is harmless");
    }
    {
        Fixture f(true);
        f.initial();
        f.go({92, 0});
        f.world.committed = 60;
        f.runtime->ready(f.baseline({92, 60}));
        f.hub->running = false;
        f.go({92, 30});
        check(!f.hub->running && f.runtime->cursor == Cursor{92, 60} && bool(f.runtime->held),
              "older same-scene release must not rewind a newly admitted checkpoint");
    }
}
void driver_advances_after_release() {
    Fixture f;
    f.initial();
    f.runtime->phase = PartyRuntime::Impl::Phase::ScenePrepare;
    f.runtime->scene_step(Clock::now());
    f.go({92, 0});
    check(bool(f.runtime->driver), "post-load driver exists");
    for (unsigned owner = 0; owner < 2; ++owner)
        check(f.runtime->driver->receive(92, std::uint8_t(owner), 0, {}) == ex::InputResult::Accepted,
              "actual driver accepts both racers' first input");
    check(f.runtime->driver->step(true) == ex::Step::Advanced, "first race tick advances after release");
    check(f.runtime->confirmed() == 1 && f.world.confirmed_count() == 1, "first race tick is confirmed");
}
void racer_without_watcher() {
    Fixture f(false, false, false);
    f.initial();
    f.runtime->phase = PartyRuntime::Impl::Phase::ScenePrepare;
    f.runtime->scene_step(Clock::now());
    f.go({92, 0});
    check(f.runtime->phase == PartyRuntime::Impl::Phase::Running && bool(f.runtime->driver),
          "spectator-capable runtime also starts with only racers connected");
}
void authority_and_roster() {
    Fixture f;
    f.initial();
    f.runtime->phase = PartyRuntime::Impl::Phase::ScenePrepare;
    f.runtime->scene_step(Clock::now());
    f.go({92, 0}, 1, 3);
    check(!f.runtime->driver, "spectator cannot forge host release");
    auto bad = f.hub->party.snapshot();
    bad.host = 99;
    bool rejected = false;
    try {
        f.go({92, 0}, 1, 1, bad);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && !f.runtime->driver, "invalid roster must not activate or consume release");
    f.go({92, 0});
    check(bool(f.runtime->driver), "valid retry works after rejected roster");
}
void host_does_not_wait_for_watcher() {
    Fixture f(false, true);
    f.runtime->activate({91, 0});
    f.runtime->phase = PartyRuntime::Impl::Phase::ScenePrepare;
    f.runtime->boundary = {12, 34};
    f.runtime->scene_step(Clock::now());
    check(f.runtime->phase == PartyRuntime::Impl::Phase::SceneReady, "host waits for racer proof");
    f.runtime->frozen[2] = f.runtime->phase_digest;
    check(!f.runtime->frozen.contains(3), "watcher deliberately has not acknowledged");
    f.runtime->scene_step(Clock::now());
    check(f.runtime->phase == PartyRuntime::Impl::Phase::Running && bool(f.runtime->driver),
          "host must release once racers agree without watcher vote");
}
} // namespace
} // namespace dkr::runtime::spectators
