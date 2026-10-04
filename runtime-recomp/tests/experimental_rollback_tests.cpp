#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include "netplay/experimental_runtime_admission.hpp"
#include "netplay/netplay_protocol.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include <tuple>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;

namespace {

// Synthetic deterministic world, not a DKR runtime qualification. Covers
// coupled state/lifecycle/edge inputs so a position-only rewind cannot pass.
struct World {
    std::array<std::int64_t, 4> positions{};
    std::array<std::uint16_t, 4> buttons{};
    std::array<std::uint32_t, 64> actors{};
    std::uint32_t random = 7U;
    std::uint32_t ticks = 0U;
    std::uint32_t spawns = 0U;
    std::uint32_t collisions = 0U;
    std::array<std::uint8_t, 8192> padding{};
    bool operator==(const World&) const = default;
};

struct Model final : Simulation {
    World world{};
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> effects;
    SimulationContract declared{0x12345678U, sizeof(World), kRequiredStateDomains, true, true, true};
    unsigned captures = 0, restores = 0, ticks = 0, commits = 0;
    unsigned retirements = 0, ticks_at_retirement = 0;
    std::uint64_t retired_epoch = 0;
    std::uint32_t retired_frame = UINT32_MAX;
    bool reject_retirement = false;
    bool reject_capture = false, reject_restore = false, reject_tick = false, reject_commit = false;
    bool transition = false, excessive_effects = false;
    bool hidden_native_state = false;
    unsigned restore_waits=0;
    bool waiting_retired=false;
    unsigned hidden_counter = 0U;
    unsigned confirmed_from = UINT32_MAX, tick_waits = 0, tick_preparations = 0;
    bool reject_tick_preparation = false;
    SimulationContract contract() const override { return declared; }
    bool requires_confirmed_tick() const override { return world.ticks >= confirmed_from; }
    RestoreStep prepare_tick(std::uint64_t, std::uint32_t frame, bool confirmed, std::string& error) override {
        assert(frame == world.ticks);
        ++tick_preparations;
        if (requires_confirmed_tick()) assert(confirmed);
        if (reject_tick_preparation) { error="injected tick admission failure";return RestoreStep::Failed; }
        if (tick_waits) { --tick_waits;return RestoreStep::Pending; }
        return RestoreStep::Ready;
    }
    bool before_restore(std::uint64_t epoch,std::uint32_t frame,std::string& error) override {
        ++retirements; retired_epoch=epoch; retired_frame=frame;
        ticks_at_retirement=world.ticks;
        if(reject_retirement) {error="injected retirement failure";return false;}
        return true;
    }
    RestoreStep prepare_restore(std::uint64_t epoch,std::uint32_t frame,std::string& error) override {
        if(restore_waits||waiting_retired) {
            if(!waiting_retired||retired_epoch!=epoch||retired_frame!=frame) {
                if(!before_restore(epoch,frame,error))return RestoreStep::Failed;
                waiting_retired=true;
            }
            if(restore_waits){--restore_waits;return RestoreStep::Pending;}
            waiting_retired=false;return RestoreStep::Ready;
        }
        return Simulation::prepare_restore(epoch,frame,error);
    }
    bool capture(std::span<std::uint8_t> state, std::string& error) override {
        ++captures;
        if (reject_capture) { error = "injected capture failure"; return false; }
        assert(state.size() == sizeof(World));
        std::memcpy(state.data(), &world, sizeof(world));
        return true;
    }
    bool restore(std::span<const std::uint8_t> state, std::string& error) override {
        ++restores;
        if (reject_restore) { error = "injected restore failure"; return false; }
        assert(state.size() == sizeof(World));
        std::memcpy(&world, state.data(), sizeof(world));
        return true;
    }
    bool tick(std::uint32_t frame, const FrameInputs& inputs, TickOutput& output, std::string& error) override {
        assert(frame == world.ticks);
        ++ticks;
        if (hidden_native_state) world.random += ++hidden_counter;
        for (std::size_t slot = 0; slot < inputs.size(); ++slot) {
            const auto pressed = inputs[slot].buttons & ~world.buttons[slot];
            world.buttons[slot] = inputs[slot].buttons;
            world.positions[slot] += inputs[slot].stick_x * 7 + inputs[slot].stick_y;
            if (pressed & 1U) {
                world.random = world.random * 1664525U + 1013904223U;
                world.actors[world.spawns++ % world.actors.size()] = world.random;
                output.effects.push_back(static_cast<std::uint8_t>(slot + 1));
            }
            if (inputs[slot].buttons & 2U) world.actors[(frame + slot) % world.actors.size()] = 0;
        }
        // Interaction: one player's input changes another player's result.
        if ((inputs[1].buttons & 4U) && (inputs[0].buttons & 4U)) {
            std::swap(world.positions[0], world.positions[1]);
            ++world.collisions;
        }
        output.scene_boundary = transition && (inputs[1].buttons & 0x8000U);
        if (excessive_effects) output.effects.resize(65537);
        world.padding[frame % world.padding.size()] = static_cast<std::uint8_t>(world.random);
        ++world.ticks;
        if (reject_tick) { error = "injected tick failure"; return false; }
        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame,
                std::span<const std::uint8_t> bytes, bool, std::string& error) override {
        assert(epoch != 0);
        ++commits;
        if (reject_commit) { error = "injected commit failure"; return false; }
        assert(effects.empty() || effects.back().first + 1 == frame);
        effects.emplace_back(frame, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        return true;
    }
};

PackedInput input_for(std::uint32_t frame, unsigned player) {
    return {static_cast<std::uint16_t>((frame * (player + 5) / 3) % 8),
            static_cast<std::int8_t>(static_cast<int>((frame * 17 + player * 7) % 111) - 55),
            static_cast<std::int8_t>(static_cast<int>((frame * 11 + player * 3) % 101) - 50)};
}

void delayed_corrections(unsigned players, unsigned window, unsigned seed) {
    Model model, reference;
    Driver driver(model);
    std::string error;
    assert(driver.start({9, static_cast<std::uint8_t>(players), static_cast<std::uint8_t>(window)}, error));
    constexpr unsigned frames = 350;
    struct Packet { unsigned delivery, frame, player; };
    std::vector<Packet> packets;
    std::mt19937 rng(seed);
    std::vector<std::vector<std::uint8_t>> expected_effects;
    for (unsigned frame = 0; frame < frames; ++frame) {
        FrameInputs inputs{};
        for (unsigned slot = 0; slot < players; ++slot) {
            inputs[slot] = input_for(frame, slot);
            packets.push_back({frame + static_cast<unsigned>(rng() % (window + 4)), frame, slot});
        }
        TickOutput output;
        assert(reference.tick(frame, inputs, output, error));
        expected_effects.push_back(output.effects);
    }
    std::stable_sort(packets.begin(), packets.end(), [](auto a, auto b) { return a.delivery < b.delivery; });
    // Wall time advances independently of the speculative simulation. Window
    // exhaustion must park, not drop missing inputs or run into unbounded debt.
    std::size_t packet = 0;
    for (unsigned time = 0; time < frames + 1000; ++time) {
        while (packet < packets.size() && packets[packet].delivery <= time) {
            const auto p = packets[packet];
            const auto result = driver.receive(9, static_cast<std::uint8_t>(p.player), p.frame, input_for(p.frame, p.player));
            assert(result == InputResult::Accepted);
            assert(driver.receive(9, static_cast<std::uint8_t>(p.player), p.frame, input_for(p.frame, p.player)) == InputResult::Duplicate);
            ++packet;
        }
        // Bounded service budget, independent of a potentially long replay.
        for (unsigned budget = 0; budget < 5; ++budget) {
            if (driver.statistics().next_frame == frames && !driver.correcting()) break;
            const auto step = driver.step();
            if (step == Step::Failed) std::cerr << driver.error() << '\n';
            assert(step != Step::Failed);
            if (step != Step::Replayed) break;
        }
        if (packet == packets.size() && driver.statistics().next_frame == frames && !driver.correcting()) break;
    }
    assert(packet == packets.size());
    assert(driver.statistics().next_frame == frames);
    assert(driver.statistics().confirmed_frames == frames);
    assert(driver.statistics().rollbacks > 0);
    assert(driver.statistics().replayed_frames > 0);
    assert(driver.statistics().largest_rollback <= window);
    assert(model.world == reference.world);
    assert(model.effects.size() == frames);
    for (unsigned frame = 0; frame < frames; ++frame) assert(model.effects[frame].second == expected_effects[frame]);
}

void predictions_never_confirm() {
    Model model;
    Driver driver(model);
    std::string error;
    assert(driver.start({1, 2, 3}, error));
    for (unsigned frame = 0; frame < 3; ++frame) {
        assert(driver.receive(1, 0, frame, input_for(frame, 0)) == InputResult::Accepted);
        assert(driver.step() == Step::Advanced);
    }
    for (unsigned i = 0; i < 50; ++i) assert(driver.step() == Step::PredictionLimit);
    assert(driver.statistics().next_frame == 3);
    assert(driver.statistics().confirmed_frames == 0);
    assert(model.effects.empty());
    assert(!driver.start({2, 2, 3}, error)); // Cannot discard an unconfirmed timeline.
    for (unsigned frame = 0; frame < 3; ++frame)
        assert(driver.receive(1, 1, frame, input_for(frame, 1)) == InputResult::Accepted);
    for (unsigned i = 0; i < 3; ++i) assert(driver.step() == Step::Replayed);
    assert(driver.statistics().confirmed_frames == 3);
    assert(model.effects.size() == 3);
    assert(driver.receive(0, 1, 0, {}) == InputResult::StaleEpoch);
    assert(driver.receive(1, 2, 3, {}) == InputResult::OutOfRange);
    assert(driver.receive(1, 1, 100, {}) == InputResult::OutOfRange);
    assert(driver.receive(1, 1, 0, {}) == InputResult::Conflict);
    assert(driver.step() == Step::Failed);
    assert(model.effects.size() == 3);
}

void corrected_boundary() {
    Model model;
    model.transition = true;
    Driver driver(model);
    std::string error;
    assert(driver.start({1, 2, 6}, error));
    for (unsigned frame = 0; frame < 5; ++frame) assert(driver.step() == Step::Advanced);
    assert(driver.receive(1, 1, 1, {0x8000, 0, 0}) == InputResult::Accepted);
    assert(driver.step() == Step::Replayed);
    assert(driver.statistics().next_frame == 2);
    assert(driver.step() == Step::AwaitingBoundaryConfirmation);
    assert(model.effects.empty());
    assert(driver.receive(1, 0, 0, {}) == InputResult::Accepted);
    assert(driver.receive(1, 1, 0, {}) == InputResult::Accepted);
    assert(driver.receive(1, 0, 1, {}) == InputResult::Accepted);
    assert(driver.step() == Step::ConfirmedBoundary);
    assert(model.world.ticks == 2);
    assert(model.effects.size() == 2);
    assert(driver.step() == Step::ConfirmedBoundary);
    assert(model.effects.size() == 2);
    model.world = {};
    model.effects.clear();
    assert(driver.start({2, 2, 6}, error));
    assert(driver.receive(1, 1, 1, {}) == InputResult::StaleEpoch);
    assert(driver.step() == Step::Advanced);
}

void prediction_removed_boundary() {
    Model model;
    model.transition = true;
    Driver driver(model);
    std::string error;
    assert(driver.start({1, 2, 6}, error));
    // An unconfirmed transition in frame zero must not commit or unload.
    assert(driver.receive(1, 1, 0, {0x8000, 0, 0}) == InputResult::Accepted);
    assert(driver.step() == Step::Advanced);
    assert(driver.step() == Step::AwaitingBoundaryConfirmation);
    assert(model.effects.empty());
    assert(driver.receive(1, 0, 0, {1, 0, 0}) == InputResult::Accepted);
    assert(driver.step() == Step::ConfirmedBoundary);
    assert(model.effects.size() == 1);
    assert(model.effects[0].second == std::vector<std::uint8_t>{1});
}

void faults() {
    std::string error;
    Model missing;
    missing.declared.state_domains &= ~static_cast<std::uint32_t>(StateDomain::Allocation);
    Driver denied(missing);
    assert(!denied.start({1, 2, 6}, error));
    assert(missing.captures == 0 && missing.ticks == 0);
    for (unsigned fault = 0; fault < 5; ++fault) {
        Model model;
        Driver driver(model);
        assert(driver.start({1, 2, 6}, error));
        if (fault == 0) model.reject_capture = true;
        if (fault == 1) model.reject_tick = true;
        if (fault == 2) model.excessive_effects = true;
        if (fault == 3) model.reject_commit = true;
        if (fault == 4) {
            assert(driver.step() == Step::Advanced);
            model.reject_restore = true;
        }
        assert(driver.receive(1, 0, 0, {1, 12, 7}) == InputResult::Accepted);
        assert(driver.receive(1, 1, 0, {}) == InputResult::Accepted);
        assert(driver.step() == Step::Failed);
        const auto ticks = model.ticks, commits = model.commits;
        assert(driver.step() == Step::Failed);
        assert(model.ticks == ticks && model.commits == commits);
        assert(model.effects.empty());
        if (fault == 1 || fault == 2) assert(model.world == World{});
    }
}

void qualification() {
    std::array<FrameInputs, 12> inputs{};
    for (unsigned frame = 0; frame < inputs.size(); ++frame)
        for (unsigned slot = 0; slot < 4; ++slot) inputs[frame][slot] = input_for(frame, slot);
    Model good;
    auto result = qualify_replay(good, inputs);
    assert(result.passed());
    assert(result.replayed_ticks == 78);
    assert(good.world == World{});
    assert(good.commits == 0 && good.effects.empty());
    Model missing_native;
    missing_native.hidden_native_state = true;
    result = qualify_replay(missing_native, inputs);
    assert(result.failure == QualificationFailure::StateMismatch);
    assert(missing_native.commits == 0);
    Model transition;
    transition.transition = true;
    inputs[4][1].buttons |= 0x8000;
    result = qualify_replay(transition, inputs);
    assert(result.passed() && result.replayed_ticks == 15);
    assert(transition.commits == 0);
}

void retirement_before_ram_restore() {
    for(bool reject:{false,true}) {
        Model model;Driver driver(model);std::string error;
        assert(driver.start({11,2,6},error));
        for(unsigned f=0;f<3;++f) {
            assert(driver.receive(11,0,f,{0,12,0})==InputResult::Accepted);
            assert(driver.step()==Step::Advanced);
        }
        const auto before=model.world;model.reject_retirement=reject;
        assert(driver.receive(11,1,1,{1,-24,0})==InputResult::Accepted);
        const auto step=driver.step(false);
        assert(model.retirements==1 && model.retired_epoch==11 && model.retired_frame==1 &&
               model.ticks_at_retirement==3);
        if(reject) {
            assert(step==Step::Failed && model.restores==0 && model.world==before && model.commits==0);
            assert(driver.step()==Step::Failed && model.retirements==1);
        } else assert(step==Step::Replayed && model.restores==1 && model.world.ticks==2);
    }
}

void distinct_modes() {
    for (int original : {0, 1}) {
        int stable = original;
        bool enabled = false;
        assert(select_mode(2, stable, enabled));
        assert(stable == original && enabled);
        assert(selected_mode(stable, enabled) == SynchronizationMode::ExperimentalRollback);
        enabled = false; // explicit switch back, or older build ignoring the new key
        assert(static_cast<int>(selected_mode(stable, enabled)) == original);
        assert(!select_mode(3, stable, enabled));
        assert(stable == original && !enabled);
    }
    static_assert(static_cast<unsigned>(SynchronizationMode::Rollback) == 0);
    static_assert(static_cast<unsigned>(SynchronizationMode::Lockstep) == 1);
    static_assert(static_cast<unsigned>(SynchronizationMode::ExperimentalRollback) == 2);
    static_assert(!runtime_admission_error(SynchronizationMode::Rollback));
    static_assert(!runtime_admission_error(SynchronizationMode::Lockstep));
    assert(runtime_admission_error(SynchronizationMode::ExperimentalRollback));
    LaunchDescriptor descriptor{1, 2, 3, 3, 2, 1, 6, SynchronizationMode::ExperimentalRollback};
    assert(valid_launch_descriptor(descriptor));
    const auto experiment_hash = launch_descriptor_hash(descriptor);
    protocol::StartPayload payload{0, descriptor, experiment_hash};
    const auto bytes = protocol::encode_start(payload);
    protocol::StartPayload decoded;
    std::string error;
    assert(protocol::decode_start(bytes, decoded, error));
    assert(decoded.descriptor == descriptor);
    descriptor.synchronization = SynchronizationMode::Rollback;
    assert(experiment_hash != launch_descriptor_hash(descriptor));
    descriptor.synchronization = static_cast<SynchronizationMode>(3);
    assert(!valid_launch_descriptor(descriptor));
    Rules rules;
    rules.synchronization = SynchronizationMode::ExperimentalRollback;
    assert(valid_rules(rules));
    rules.rollback_window = 0;
    assert(!valid_rules(rules));
}
void asynchronous_retirement_drain() {
    Model model,reference;Driver driver(model);std::string error;
    assert(driver.start({19,2,6},error));
    for(unsigned f=0;f<3;++f) {
        assert(driver.receive(19,0,f,input_for(f,0))==InputResult::Accepted);
        assert(driver.step()==Step::Advanced);
    }
    const auto future=model.world;
    model.restore_waits=1003;
    assert(driver.receive(19,1,1,input_for(1,1))==InputResult::Accepted);
    assert(driver.step(false)==Step::WaitingForPresentation);
    assert(driver.statistics().restore_checkpoint_loads==1);
    for(unsigned pulse=0;pulse<1000;++pulse)assert(driver.step(false)==Step::WaitingForPresentation);
    assert(driver.statistics().restore_checkpoint_loads==1);
    // An EARLIER actual packet during the nonblocking drain changes the rewind
    // point. Do not resume the first request or overwrite immutable inputs.
    assert(driver.receive(19,1,0,input_for(0,1))==InputResult::Accepted);
    for(unsigned pulse=0;pulse<2;++pulse)assert(driver.step(false)==Step::WaitingForPresentation);
    assert(driver.statistics().restore_checkpoint_loads==2);
    assert(model.world==future&&model.restores==0&&model.commits==0&&
           driver.statistics().next_frame==3&&driver.statistics().rollbacks==0&&
           model.retirements==2&&model.retired_frame==0);
    assert(driver.step(false)==Step::Replayed);
    assert(model.restores==1&&driver.statistics().rollbacks==1&&driver.statistics().next_frame==1);
    assert(driver.receive(19,1,2,input_for(2,1))==InputResult::Accepted);
    while(driver.statistics().confirmed_frames<3)assert(driver.step(false)!=Step::Failed);
    for(unsigned f=0;f<3;++f) {
        FrameInputs input{};input[0]=input_for(f,0);input[1]=input_for(f,1);TickOutput output;
        assert(reference.tick(f,input,output,error)&&reference.commit(19,f,output.effects,false,error));
    }
    assert(model.world==reference.world&&model.effects==reference.effects);
}
void confirmed_loader_tick_admission() {
    Model model;Driver driver(model);std::string error;
    model.confirmed_from=2;
    assert(driver.start({23,2,6},error));
    for (unsigned f=0;f<3;++f)
        assert(driver.receive(23,0,f,input_for(f,0))==InputResult::Accepted);
    // Ordinary gameplay is still predicted. Entering a loader-capable scene
    // cannot free resources while either earlier tick remains speculative.
    assert(driver.step()==Step::Advanced);
    assert(driver.step()==Step::Advanced);
    const auto future=model.world;
    const auto captures=model.captures,preparations=model.tick_preparations;
    assert(driver.receive(23,1,2,input_for(2,1))==InputResult::Accepted);
    for (unsigned pulse=0;pulse<100;++pulse)
        assert(driver.step()==Step::WaitingForConfirmedInput);
    assert(model.world==future && model.captures==captures && model.tick_preparations==preparations);
    // Corrections of the earlier gameplay must still run before admission.
    for (unsigned f=0;f<2;++f)
        assert(driver.receive(23,1,f,input_for(f,1))==InputResult::Accepted);
    assert(driver.step(false)==Step::Replayed);
    assert(driver.step(false)==Step::Replayed);
    assert(driver.statistics().confirmed_frames==2);
    model.tick_waits=100;
    const auto checkpointed=model.world;
    const auto before_capture=model.captures;
    for (unsigned pulse=0;pulse<100;++pulse)
        assert(driver.step()==Step::WaitingForPresentation);
    assert(model.world==checkpointed && model.captures==before_capture);
    assert(driver.step()==Step::Advanced);
    assert(driver.statistics().confirmed_frames==3 && model.commits==3);
    assert(driver.statistics().predicted_frames==2);
    // Repeated polling never consumes physical input or advances the world.
    assert(driver.receive(23,0,3,input_for(3,0))==InputResult::Accepted);
    assert(driver.step()==Step::WaitingForConfirmedInput);
    assert(driver.receive(23,1,3,input_for(3,1))==InputResult::Accepted);
    model.reject_tick_preparation=true;
    const auto before_fail=model.world;
    assert(driver.step()==Step::Failed && model.world==before_fail);
    assert(driver.statistics().confirmed_frames==3);
}

void confirmed_checkpoint_retirement() {
    constexpr unsigned bytes=RollbackStateStore::kPageBytes*4;
    RollbackStateStore store(bytes,32,4,0);
    std::vector<std::uint8_t> state(bytes),restored(bytes);
    for(unsigned f=0;f<20;++f) {
        std::fill(state.begin(),state.end(),std::uint8_t(f));
        assert(store.save(f,state,f));
    }
    const auto before=store.allocated_bytes();
    store.retire_before(17);
    assert(store.allocated_bytes()<before && !store.contains(15) && store.contains(16));
    for(unsigned f=16;f<20;++f) {
        std::uint64_t checksum=0;
        assert(store.load(f,restored,&checksum) && checksum==f);
        assert(std::all_of(restored.begin(),restored.end(),[f](auto value){return value==f;}));
    }
    // Rewind leaves the retained predecessor checkpoint/deltas reconstructible.
    store.discard_after(17);
    std::fill(state.begin(),state.end(),17); state[17]=99;
    assert(store.save(18,state,18) && store.load(18,restored) && restored==state);
    store.retire_before(0); // Must not remove its necessary predecessor.
    assert(store.load(18,restored) && restored==state);
}

} // namespace

int main() {
    predictions_never_confirm();
    corrected_boundary();
    prediction_removed_boundary();
    faults();
    qualification();
    retirement_before_ram_restore();
    asynchronous_retirement_drain();
    confirmed_loader_tick_admission();
    distinct_modes();
    confirmed_checkpoint_retirement();
    for (unsigned players : {2, 3, 4})
        for (unsigned window : {2, 6, 20})
            for (unsigned seed = 0; seed < 12; ++seed) delayed_corrections(players, window, seed);
    std::cout << "Experimental rollback: 108 delayed/reordered 350-frame worlds match confirmed-input references.\n";
    std::cout << "This is driver validation, NOT qualification of the DKR game-state adapter.\n";
}
