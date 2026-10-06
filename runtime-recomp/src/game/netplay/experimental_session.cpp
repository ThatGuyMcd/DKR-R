#include "experimental_session.hpp"
#include <exception>
#include <limits>

namespace dkr::runtime::netplay::experimental {
bool Session::fail(std::string error) {
    if (error.empty()) error="Experimental session failed without adapter diagnostics.";
    if (error_.empty()) error_ = std::move(error);
    phase_ = Phase::Failed; return false;
}
bool Session::start(TimelineConfiguration configuration, std::string& error) {
    // A failed or running experiment must be discarded, not converted into the
    // stable world or reset over a speculative cursor by another start call.
    if (phase_ != Phase::Inactive) { error="Experimental session already started."; return false; }
    EpochGate gate;
    if (!gate.begin(configuration.simulation.epoch, configuration.simulation.players,
                    configuration.local_owner, world_.contract().schema)) {
        error="Invalid experimental scene protocol configuration."; return false;
    }
    if (!timeline_.start(configuration,error)) return false;
    configuration_=configuration; schema_=world_.contract().schema;
    gate_=std::move(gate); phase_=Phase::Running;
    error_.clear(); error.clear(); return true;
}
bool Session::needs_local_input(bool live_lead) const { return phase_==Phase::Running && timeline_.needs_local_input(live_lead); }
bool Session::service_inputs() {
    if(phase_==Phase::Inactive || phase_==Phase::Failed)return false;
    if(phase_==Phase::Running && !timeline_.service_inputs())return fail(timeline_.error());
    return true;
}
OwnerInputResult Session::sample_local(PackedInput input,bool live_lead) {
    return needs_local_input(live_lead) ? timeline_.sample_local(input,live_lead) : OwnerInputResult::Rejected;
}
SessionInputResult Session::receive_input(std::uint8_t peer, std::span<const std::uint8_t> bytes) {
    if (phase_==Phase::Inactive || phase_==Phase::Failed || peer>=configuration_.simulation.players ||
        peer==configuration_.local_owner || (configuration_.local_owner && peer)) return SessionInputResult::Rejected;
    const auto packet=decode_owner_inputs(bytes);
    if (!packet || packet->players!=configuration_.simulation.players) return SessionInputResult::Rejected;
    if (packet->epoch<epoch()) return SessionInputResult::Stale;
    if (packet->epoch!=epoch()) {
        // One client may have received Release before the host receives its ACK.
        // Never apply/ACK its next-epoch actuals to the OLD world. Immutable
        // owner history retains them and the repair lane retries after release.
        return phase_!=Phase::Running && packet->epoch==gate_.next_epoch() ?
            SessionInputResult::Pending : SessionInputResult::Rejected;
    }
    switch (timeline_.receive_authenticated(peer,bytes)) {
    case OwnerInputResult::Accepted:return SessionInputResult::Accepted;
    case OwnerInputResult::Duplicate:return SessionInputResult::Duplicate;
    case OwnerInputResult::StaleEpoch:return SessionInputResult::Stale;
    case OwnerInputResult::Backpressure:return SessionInputResult::Pending;
    case OwnerInputResult::Conflict:
        fail(timeline_.error()); return SessionInputResult::Conflict;
    default:
        if (!timeline_.error().empty()) fail(timeline_.error());
        return SessionInputResult::Rejected;
    }
}
EpochResult Session::receive_control(std::uint8_t peer, std::span<const std::uint8_t> bytes) {
    if (phase_==Phase::Inactive || phase_==Phase::Failed) return EpochResult::Rejected;
    const auto message=decode_epoch_message(bytes);
    if (!message) return EpochResult::Rejected;
    // Scene identity comes from the confirmed local intent, not host authority
    // over an arbitrary scene. Mismatches cannot load either scene.
    if (message->kind==EpochMessageKind::Prepare && message->epoch==epoch() &&
        peer==0 && configuration_.local_owner && phase_!=Phase::Running &&
        message->players==configuration_.simulation.players && message->schema==schema_ &&
        message->scene_hash!=intent_.scene_hash) {
        fail("Confirmed experimental scene intents disagree."); return EpochResult::Conflict;
    }
    const auto result=gate_.receive_authenticated(peer,*message);
    if (result==EpochResult::Conflict || gate_.failed()) fail("Experimental scene agreement failed.");
    return result;
}
std::optional<OwnerInputPacket> Session::packet_for(std::uint8_t owner,std::uint8_t peer,OwnerInputSend lane) {
    if (phase_==Phase::Inactive || phase_==Phase::Failed) return {};
    return timeline_.packet_for(owner,peer,lane);
}
std::optional<EpochMessage> Session::control_for(std::uint8_t peer) const {
    return phase_==Phase::Failed ? std::nullopt : gate_.message_for(peer);
}
std::optional<EpochMessage> Session::previous_control_for(std::uint8_t peer) const {
    return phase_==Phase::Failed ? std::nullopt : gate_.previous_message_for(peer);
}
SessionStep Session::step(bool allow_advance) try {
    if (phase_==Phase::Inactive || phase_==Phase::Failed) return SessionStep::Failed;
    if (phase_==Phase::Running) {
        switch (timeline_.step(allow_advance)) {
        case TimelineStep::Advanced:return SessionStep::Advanced;
        case TimelineStep::Replayed:return SessionStep::Replayed;
        case TimelineStep::WaitingForPresentation:return SessionStep::WaitingForPresentation;
        case TimelineStep::WaitingForLocalInput:return SessionStep::WaitingForInput;
        case TimelineStep::PredictionLimit:return SessionStep::PredictionLimit;
        case TimelineStep::AwaitingBoundaryConfirmation:return SessionStep::WaitingForConfirmation;
        case TimelineStep::WaitingForConfirmedInput:return SessionStep::WaitingForConfirmation;
        case TimelineStep::ConfirmedBoundary: {
            std::string detail;
            if (epoch()==UINT64_MAX || !world_.confirmed_boundary(intent_,detail) ||
                !intent_.scene_hash || !gate_.confirm_boundary(statistics().confirmed_frames,intent_.boundary_hash)) {
                fail("Experimental confirmed scene intent invalid: "+detail); return SessionStep::Failed;
            }
            phase_=Phase::Boundary; return SessionStep::WaitingForPeers;
        }
        default:fail(timeline_.error()); return SessionStep::Failed;
        }
    }
    if (phase_==Phase::Boundary) {
        if (!configuration_.local_owner && gate_.all_boundaries_confirmed() &&
            !gate_.propose(epoch()+1,intent_.scene_hash)) {
            fail("Experimental scene proposal failed."); return SessionStep::Failed;
        }
        if (gate_.can_prepare_scene()) phase_=Phase::Preparing;
        return SessionStep::WaitingForPeers;
    }
    if (phase_==Phase::Preparing) {
        std::uint64_t baseline=0; std::string detail;
        switch (world_.prepare_scene(intent_.scene_hash,baseline,detail)) {
        case PreparationStep::Pending:return SessionStep::PreparingScene;
        case PreparationStep::Failed:
            fail("Experimental scene preparation failed: "+detail); return SessionStep::Failed;
        case PreparationStep::Ready:
            if (world_.contract().schema!=schema_) {
                fail("Experimental checkpoint schema changed during preparation."); return SessionStep::Failed;
            }
            if (!gate_.mark_prepared(baseline)) {
                fail("Experimental scene baseline rejected."); return SessionStep::Failed;
            }
            phase_=Phase::Release; return SessionStep::WaitingForPeers;
        default:
            fail("Experimental adapter returned an invalid preparation state."); return SessionStep::Failed;
        }
    }
    if (phase_==Phase::Release && gate_.released()) {
        auto next=configuration_; next.simulation.epoch=gate_.next_epoch(); std::string detail;
        // The schema cannot change during a match without a new negotiation.
        if (world_.contract().schema!=schema_) {
            fail("Experimental checkpoint schema changed during a scene transaction."); return SessionStep::Failed;
        }
        if (!timeline_.start(next,detail) || !gate_.begin(next.simulation.epoch,next.simulation.players,
                                                         next.local_owner,world_.contract().schema)) {
            fail("Experimental next scene could not start: "+detail); return SessionStep::Failed;
        }
        configuration_=next; intent_={}; phase_=Phase::Running; return SessionStep::SceneStarted;
    }
    return SessionStep::WaitingForPeers;
} catch (const std::exception& exception) {
    fail(std::string("Experimental scene adapter stopped: ")+exception.what()); return SessionStep::Failed;
} catch (...) {
    fail("Experimental scene adapter stopped with an unknown exception."); return SessionStep::Failed;
}
}
