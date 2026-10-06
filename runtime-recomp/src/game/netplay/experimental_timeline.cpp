#include "experimental_timeline.hpp"
#include <algorithm>
#include <limits>

namespace dkr::runtime::netplay::experimental {
bool Timeline::start(TimelineConfiguration configuration, std::string& error) {
    if (configuration.local_owner >= configuration.simulation.players ||
        configuration.input_delay > kMaximumInputDelayFrames ||
        (started_ && configuration.simulation.epoch <= configuration_.simulation.epoch)) {
        error="Invalid experimental timeline owner, delay or epoch."; return false;
    }
    // Driver validates the state contract and stages its allocations before it
    // replaces an existing history. Do not clear a live timeline on rejection.
    if (!driver_.start(configuration.simulation,error)) return false;
    configuration_=configuration; pending_={}; pending_counts_={}; frontier_=0; error_.clear(); started_=true;
    if (!inputs_.begin(configuration.simulation.epoch,configuration.simulation.players,configuration.local_owner)) {
        error_=error="Experimental owner history could not begin."; return false;
    }
    // Each owner publishes only its own agreed neutral delay prefix. Remote
    // prefixes still arrive through authentication, never a host prediction.
    for (unsigned frame=0;frame<configuration.input_delay;++frame) {
        if (inputs_.publish(frame,{}) != OwnerInputResult::Accepted || !stage(configuration.local_owner,frame,{})) {
            error_=error="Experimental neutral delay prefix failed."; return false;
        }
    }
    return true;
}
bool Timeline::needs_local_input(bool live_lead) const {
    if (!started_ || !error().empty() || driver_.scene_boundary_pending()) return false;
    // Physical time must not rewind with the simulation. Permit a small,
    // bounded live lead while correcting or waiting, not an unbounded queue
    // of old controls after an outage. Historical samples remain immutable.
    const auto limit=std::uint64_t(frontier_)+configuration_.input_delay+(live_lead ? 2 : 1);
    return inputs_.received_next(configuration_.local_owner)<limit && limit<UINT32_MAX;
}
OwnerInputResult Timeline::sample_local(PackedInput input,bool live_lead) {
    if (!needs_local_input(live_lead)) return OwnerInputResult::Rejected;
    const auto frame=inputs_.received_next(configuration_.local_owner);
    const auto result=inputs_.publish(frame,input);
    if (result==OwnerInputResult::Accepted && !stage(configuration_.local_owner,frame,input))
        return OwnerInputResult::Rejected;
    return result;
}
bool Timeline::stage(std::uint8_t owner,std::uint32_t frame,PackedInput input) {
    if (frame < driver_.statistics().confirmed_frames) return true;
    auto& pending=pending_[owner][frame%128];
    if (pending.valid && (pending.frame!=frame || pending.input!=input)) {
        error_="Experimental pending input history conflicted or exceeded its bound."; return false;
    }
    if(!pending.valid)++pending_counts_[owner];
    pending={frame,input,true}; return true;
}
OwnerInputResult Timeline::receive_authenticated(std::uint8_t peer,std::span<const std::uint8_t> bytes) {
    if (!started_ || !error().empty()) return OwnerInputResult::Rejected;
    const auto packet=decode_owner_inputs(bytes);
    if (!packet) return OwnerInputResult::Rejected;
    std::vector<FinalOwnerInput> delivered;
    const auto result=inputs_.receive_authenticated(peer,*packet,delivered);
    if (result==OwnerInputResult::Conflict) error_="Conflicting immutable experimental owner input.";
    for (const auto& actual:delivered) if (!stage(actual.owner,actual.frame,actual.input)) return OwnerInputResult::Rejected;
    return result;
}
std::optional<OwnerInputPacket> Timeline::packet_for(std::uint8_t owner,std::uint8_t peer,OwnerInputSend lane) {
    if (!started_ || !error().empty()) return {};
    return inputs_.packet_for(owner,peer,lane);
}
bool Timeline::feed() {
    // A late packet can arrive during rewind with samples beyond the driver's
    // temporary cursor. Keep them bounded in owner history, and feed when its
    // real admission window reaches them instead of dropping/re-dating them.
    const auto& state=driver_.statistics();
    for (unsigned owner=0;owner<configuration_.simulation.players;++owner) if(pending_counts_[owner])
        for (auto& pending:pending_[owner]) if (pending.valid) {
            if (pending.frame<state.confirmed_frames) { pending.valid=false;--pending_counts_[owner]; continue; }
            if (std::uint64_t(pending.frame)>std::uint64_t(state.next_frame)+48) continue;
            const auto result=driver_.receive(configuration_.simulation.epoch,owner,pending.frame,pending.input);
            if (result!=InputResult::Accepted && result!=InputResult::Duplicate) {
                error_="Experimental driver rejected admitted owner input."; return false;
            }
            pending.valid=false;--pending_counts_[owner];
        }
    return true;
}
bool Timeline::service_inputs() {
    return started_ && error().empty() && feed();
}
TimelineStep Timeline::step(bool allow_advance) {
    if (!started_ || !error().empty() || !feed()) return TimelineStep::Failed;
    // Never predict this machine's own unsampled physical input. Even a host
    // must wait for its own owner-final value at the logical frame being run.
    // A scene intent can be waiting for remote confirmation after the last
    // local sample. Still service confirmation/correction; do not turn that
    // wait into an impossible request for another physical input frame.
    const auto result=driver_.step(allow_advance && inputs_.actual(configuration_.local_owner,driver_.statistics().next_frame).has_value());
    frontier_=(std::max)(frontier_,driver_.statistics().next_frame);
    if (!inputs_.set_simulation_cursor(frontier_)) {
        error_="Experimental owner-input frontier became invalid."; return TimelineStep::Failed;
    }
    switch(result) {
    case Step::Advanced:return TimelineStep::Advanced;
    case Step::Replayed:return TimelineStep::Replayed;
    case Step::WaitingForLocalInput:return TimelineStep::WaitingForLocalInput;
    case Step::PredictionLimit:return TimelineStep::PredictionLimit;
    case Step::AwaitingBoundaryConfirmation:return TimelineStep::AwaitingBoundaryConfirmation;
    case Step::ConfirmedBoundary:return TimelineStep::ConfirmedBoundary;
    case Step::WaitingForPresentation:return TimelineStep::WaitingForPresentation;
    case Step::WaitingForConfirmedInput:return TimelineStep::WaitingForConfirmedInput;
    default:return TimelineStep::Failed;
    }
}
}
