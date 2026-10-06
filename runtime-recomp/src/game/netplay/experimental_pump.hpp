#pragma once
#include "experimental_network.hpp"
#include <functional>

namespace dkr::runtime::netplay::experimental {

enum class PumpWait { None, FramePacing, OwnerInput, Confirmation, ScenePeers, ScenePreparation, Failed, PresentationDrain };
struct PumpView {
    PumpWait wait = PumpWait::None;
    SessionStep last_step = SessionStep::WaitingForInput;
    unsigned ticks_this_pulse = 0;
    std::uint64_t physical_samples = 0;
    std::uint64_t catchup_ticks = 0, replay_slices = 0;
    std::array<std::uint64_t,8> wait_nanoseconds{};
    bool waiting_for_clients() const {
        return wait==PumpWait::OwnerInput || wait==PumpWait::Confirmation ||
               wait==PumpWait::ScenePeers || wait==PumpWait::ScenePreparation || wait==PumpWait::PresentationDrain;
    }
};

// Production owner-thread orchestration; no SDK callback enters the world.
// Transport receive, replay, bounded scene preparation and confirmation stay
// serviceable between authored ticks. No sleeps or drain-until-caught-up loop.
// A hot UI loop cannot sample/advance gameplay at UI FPS. Both host and clients
// use the same 30 Hz cadence; short waits preserve phase and debt is bounded.
class Pump final {
public:
    Pump(Session& session,Network& network) : session_(session),network_(network) {}
    // False drains correction/confirmation/transport without assigning another
    // physical input. Used by a controlled stop/qualification boundary, NOT
    // an automatic mid-match switch back to stable mode.
    bool pulse(Network::Clock::time_point now,const std::function<PackedInput()>& sample,
               bool allow_new_tick = true);
    const PumpView& view() const { return view_; }
    const std::string& error() const { return error_; }
private:
    bool fail(std::string error);
    Session& session_;
    Network& network_;
    PumpView view_{};
    std::string error_;
    Network::Clock::time_point last_{},next_tick_{},next_sample_{};
    bool started_ = false;
};
}
