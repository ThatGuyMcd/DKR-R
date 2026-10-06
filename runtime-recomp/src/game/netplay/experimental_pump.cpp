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
    if(!started_) { started_=true; next_tick_=next_sample_=last_=now; }
    view_.wait_nanoseconds[unsigned(view_.wait)]+=std::uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now-last_).count());
    last_=now;
    if(!network_.service(now)) return fail(network_.error());
    // Network delivery first stages immutable input. Its admission does not
    // rewind the independent physical-input clock. No world call here.
    if(!session_.service_inputs())return fail(session_.error());
    constexpr auto period=std::chrono::nanoseconds(1'000'000'000/30);
    const auto deadline=Network::Clock::now()+std::chrono::milliseconds(2);
    if(allow_new_tick && now>=next_sample_ && session_.needs_local_input(true)) {
        if(session_.sample_local(sample(),true)!=OwnerInputResult::Accepted) return fail("Experimental owner input could not be published.");
        ++view_.physical_samples;
        next_sample_=(std::max)(next_sample_+period,now-period);
    }
    // A call includes at most four ticks, at most two NEW ticks, and a bounded
    // replay slice. Never sample a historical replay frame. Each guest tick meets
    // the adapter's measured budget; it cannot be preempted safely halfway.
    unsigned new_ticks=0;
    for(unsigned budget=0;budget<4;++budget) {
        const auto epoch=session_.epoch();
        const bool due=allow_new_tick && now>=next_tick_ && new_ticks<2;
        const auto result=session_.step(due);
        view_.last_step=result;
        if(result==SessionStep::Failed) return fail(session_.error());
        if(result==SessionStep::Replayed) {
            ++view_.ticks_this_pulse; view_.wait=PumpWait::None;
            ++view_.replay_slices;
            if(Network::Clock::now()>=deadline) break;
            continue;
        }
        if(result==SessionStep::Advanced) {
            ++view_.ticks_this_pulse; view_.wait=PumpWait::None;
            if(++new_ticks>1) ++view_.catchup_ticks;
            // Preserve cadence but cap a CPU hitch to two owed authored ticks.
            // This clock never changes any immutable logical input assignment.
            next_tick_=(std::max)(next_tick_+period,now-period);
            if(new_ticks<2 && now>=next_tick_ && Network::Clock::now()<deadline) continue;
        } else if(result==SessionStep::SceneStarted || epoch!=session_.epoch()) {
            next_tick_=next_sample_=now; view_.wait=PumpWait::None;
        } else if(result==SessionStep::WaitingForInput) {
            view_.wait=due ? PumpWait::OwnerInput : PumpWait::FramePacing;
        } else if(result==SessionStep::PredictionLimit) view_.wait=PumpWait::OwnerInput;
        else if(result==SessionStep::WaitingForConfirmation) view_.wait=PumpWait::Confirmation;
        else if(result==SessionStep::PreparingScene) view_.wait=PumpWait::ScenePreparation;
        else if(result==SessionStep::WaitingForPresentation) view_.wait=PumpWait::PresentationDrain;
        else view_.wait=PumpWait::ScenePeers;
        // Preserve the phase through a short network wait; long stalls accrue
        // at most two ticks of debt. Resetting to every poll time causes drift.
        if(now-next_tick_>period*2) next_tick_=now-period;
        break;
    }
    // Deliver freshly sampled/repaired input after this bounded slice. The
    // next caller turn remains free to draw UI, poll input or stop the match.
    return network_.service(now) || fail(network_.error());
} catch(const std::exception& e) { return fail(std::string("Experimental pump exception: ")+e.what()); }
catch(...) { return fail("Unexpected experimental pump failure."); }
}
