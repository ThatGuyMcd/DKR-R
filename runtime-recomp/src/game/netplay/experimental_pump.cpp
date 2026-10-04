#include "experimental_pump.hpp"
#include <algorithm>
#include <exception>

namespace dkr::runtime::netplay::experimental {
bool Pump::fail(std::string error) {
    if(error.empty()) error="Experimental owner pump stopped without diagnostics.";
    error_=std::move(error); view_.wait=PumpWait::Failed; return false;
}
bool Pump::pulse(Network::Clock::time_point now,const std::function<PackedInput()>& sample,bool allow_new_tick) try {
    view_.ticks_this_pulse=0;
    if(!error_.empty()) return false;
    if(!sample || (started_ && now<last_)) return fail("Invalid experimental pump input source or monotonic clock.");
    if(!started_) { started_=true; next_tick_=now; }
    last_=now;
    if(!network_.service(now)) return fail(network_.error());
    // Network delivery first stages immutable input. Admit that bounded ledger
    // before needs_local_input(), so a newly discovered correction cannot be
    // mistaken for a fresh physical sampling opportunity. No world call here.
    if(!session_.service_inputs())return fail(session_.error());
    constexpr auto period=std::chrono::nanoseconds(1'000'000'000/30);
    const auto deadline=Network::Clock::now()+std::chrono::milliseconds(2);
    const bool due=allow_new_tick && now>=next_tick_;
    if(due && session_.needs_local_input()) {
        if(session_.sample_local(sample())!=OwnerInputResult::Accepted) return fail("Experimental owner input could not be published.");
        ++view_.physical_samples;
    }
    // A call includes at most four ticks in total and one NEW tick, never a
    // physical input sample during rewind. Each guest tick must itself meet
    // the adapter's measured budget; it cannot be preempted safely halfway.
    for(unsigned budget=0;budget<4;++budget) {
        const auto epoch=session_.epoch();
        const auto frontier=session_.frontier();
        const auto result=session_.step(due);
        view_.last_step=result;
        if(result==SessionStep::Failed) return fail(session_.error());
        if(result==SessionStep::Replayed) {
            ++view_.ticks_this_pulse; view_.wait=PumpWait::None;
            if(Network::Clock::now()>=deadline) break;
            continue;
        }
        if(result==SessionStep::Advanced) {
            ++view_.ticks_this_pulse; view_.wait=PumpWait::None;
            // Preserve cadence but cap a CPU hitch to two owed authored ticks.
            // This clock never changes any immutable logical input assignment.
            next_tick_=(std::max)(next_tick_+period,now-period);
        } else if(result==SessionStep::SceneStarted || epoch!=session_.epoch()) {
            next_tick_=now; view_.wait=PumpWait::None;
        } else if(result==SessionStep::WaitingForInput) {
            view_.wait=due ? PumpWait::OwnerInput : PumpWait::FramePacing;
        } else if(result==SessionStep::PredictionLimit) view_.wait=PumpWait::OwnerInput;
        else if(result==SessionStep::WaitingForConfirmation) view_.wait=PumpWait::Confirmation;
        else if(result==SessionStep::PreparingScene) view_.wait=PumpWait::ScenePreparation;
        else if(result==SessionStep::WaitingForPresentation) view_.wait=PumpWait::PresentationDrain;
        else view_.wait=PumpWait::ScenePeers;
        if(view_.waiting_for_clients() && session_.frontier()==frontier) next_tick_=now;
        break;
    }
    // Deliver freshly sampled/repaired input after this bounded slice. The
    // next caller turn remains free to draw UI, poll input or stop the match.
    return network_.service(now) || fail(network_.error());
} catch(const std::exception& e) { return fail(std::string("Experimental pump exception: ")+e.what()); }
catch(...) { return fail("Unexpected experimental pump failure."); }
}
