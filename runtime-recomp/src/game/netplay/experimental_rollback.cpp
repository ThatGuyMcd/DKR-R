#include "experimental_rollback.hpp"
#include "experimental_checkpoint_hash.hpp"
#include "experimental_performance.hpp"

#include <algorithm>
#include <exception>
#include <limits>

namespace dkr::runtime::netplay::experimental {

Driver::Driver(Simulation& simulation) : simulation_(simulation) {}

bool Driver::valid_contract(const SimulationContract& contract) {
    return contract.schema != 0U && contract.state_bytes != 0U &&
        contract.state_bytes <= 32U * 1024U * 1024U &&
        contract.state_domains == kRequiredStateDomains &&
        contract.quiescent_tick_boundary && contract.transactional_restore &&
        contract.deferred_external_effects;
}

bool Driver::fail(std::string message) {
    // Never fall through into the stable simulation at a speculative cursor.
    // Returning to a different mode is an explicit new lobby/scene operation.
    if (error_.empty()) error_ = std::move(message);
    prepared_restore_.reset();
    return false;
}

bool Driver::start(Configuration configuration, std::string& error) {
    // Reject before disturbing an existing scene or invoking any adapter.
    const auto contract = simulation_.contract();
    if (!valid_contract(contract) || configuration.epoch == 0U ||
        configuration.players < 2U || configuration.players > kMaximumPlayers ||
        configuration.prediction_window < 2U || configuration.prediction_window > 20U ||
        configuration.checkpoint_budget_bytes < contract.state_bytes * 3U ||
        configuration.checkpoint_budget_bytes > 512U * 1024U * 1024U ||
        (active_ && (configuration.epoch <= configuration_.epoch ||
                     statistics_.next_frame != statistics_.confirmed_frames))) {
        error = "Experimental rollback requires a complete replay-safe simulation contract and a new scene epoch.";
        return false;
    }
    try {
        // Stage allocations and the initial checkpoint before replacing the
        // active history. The adapter is unchanged if admission fails.
        auto checkpoints = std::make_unique<RollbackStateStore>(
            contract.state_bytes, configuration.prediction_window + 8U, 4U, 2U, true);
        std::vector<std::uint8_t> scratch(contract.state_bytes);
        if (!simulation_.capture(scratch, error)) return false;
        if (!checkpoints->save(0U, scratch, checkpoint_hash(scratch)) ||
            checkpoints->allocated_bytes() + scratch.capacity() > configuration.checkpoint_budget_bytes) {
            error = "Experimental rollback checkpoint budget is insufficient.";
            return false;
        }
        configuration_ = configuration;
        contract_ = contract;
        checkpoints_ = std::move(checkpoints);
        scratch_ = std::move(scratch);
        prepared_restore_.reset();
        for (auto& frame : frames_) frame = {};
        confirmed_inputs_ = {};
        statistics_ = {};
        statistics_.checkpoint_bytes = checkpoints_->allocated_bytes() + scratch_.capacity();
        dirty_frame_ = boundary_frame_ = UINT32_MAX;
        replay_goal_ = 0U;
        committed_boundary_ = false;
        active_ = true;
        error_.clear();
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Experimental rollback admission failed: ") + exception.what();
        return false;
    }
}

Driver::Frame* Driver::find(std::uint32_t frame) {
    auto& value = frames_[frame % kHistory];
    return value.valid && value.number == frame ? &value : nullptr;
}

Driver::Frame& Driver::entry(std::uint32_t frame) {
    auto& value = frames_[frame % kHistory];
    if (!value.valid || value.number != frame) {
        // Reuse only storage, never the old ring entry's input/effect contents.
        // start() still releases all capacities at a new scene epoch.
        auto effects=std::move(value.output.effects);
        effects.clear();
        value = {};
        value.output.effects=std::move(effects);
        value.number = frame;
        value.valid = true;
    }
    return value;
}

InputResult Driver::receive(std::uint64_t epoch, std::uint8_t owner,
                            std::uint32_t frame, PackedInput input) {
    if (!active()) return InputResult::Failed;
    if (epoch != configuration_.epoch) return InputResult::StaleEpoch;
    if (owner >= configuration_.players ||
        static_cast<std::uint64_t>(frame) >
            static_cast<std::uint64_t>(statistics_.next_frame) + kFutureInputs ||
        (frame < statistics_.confirmed_frames &&
         statistics_.confirmed_frames - frame >= kHistory - kFutureInputs)) {
        return InputResult::OutOfRange;
    }
    auto* existing = find(frame);
    const auto mask = static_cast<std::uint8_t>(1U << owner);
    if (existing && (existing->actual_mask & mask)) {
        if (existing->actual[owner] == input) return InputResult::Duplicate;
        fail("An authenticated owner supplied conflicting final input for the same experimental frame.");
        return InputResult::Conflict;
    }
    if (frame < statistics_.confirmed_frames) return InputResult::OutOfRange;
    auto& value = entry(frame);
    value.actual[owner] = input;
    value.actual_mask |= mask;
    if (value.simulated && value.used[owner] != input) {
        dirty_frame_ = (std::min)(dirty_frame_, frame);
    }
    return InputResult::Accepted;
}

FrameInputs Driver::predict(const Frame& frame) {
    // Never predict from a newer input which happened to arrive first. Replay
    // rebuilds every dependent prediction in chronological order.
    auto inputs = confirmed_inputs_;
    if (frame.number > statistics_.confirmed_frames) {
        if (const auto* previous = find(frame.number - 1U); previous && previous->simulated) {
            inputs = previous->used;
        }
    }
    for (std::uint8_t slot = 0U; slot < configuration_.players; ++slot) {
        if (frame.actual_mask & (1U << slot)) inputs[slot] = frame.actual[slot];
    }
    for (std::size_t slot = configuration_.players; slot < inputs.size(); ++slot) inputs[slot] = {};
    return inputs;
}

bool Driver::save(std::uint32_t frame) {
    prepared_restore_.reset(); // capture overwrites the sole staged checkpoint.
    std::string detail;
    {performance::Scope timing(performance::Stage::Capture);
     if (!simulation_.capture(scratch_, detail)) return fail("Checkpoint capture failed: " + detail);}
    std::uint64_t hash;
    {performance::Scope timing(performance::Stage::Hash);hash=checkpoint_hash(scratch_);}
    performance::Scope store_timing(performance::Stage::Store);
    if (!checkpoints_->save(frame, scratch_, hash))
        return fail("The experimental checkpoint store rejected a frame.");
    checkpoints_->trim_spares(configuration_.checkpoint_budget_bytes-scratch_.capacity());
    statistics_.checkpoint_bytes = checkpoints_->allocated_bytes() + scratch_.capacity();
    if (statistics_.checkpoint_bytes > configuration_.checkpoint_budget_bytes)
        return fail("Experimental rollback exceeded its bounded checkpoint budget.");
    return true;
}

RestoreStep Driver::restore(std::uint32_t frame) {
    if(prepared_restore_!=frame) {
        performance::Scope timing(performance::Stage::Load);
        prepared_restore_.reset();
        std::uint64_t hash = 0U;
        if (!checkpoints_->load(frame, scratch_, &hash) || checkpoint_hash(scratch_) != hash) {
            fail("Experimental correction has no intact checkpoint; gameplay was not rewound.");return RestoreStep::Failed;
        }
        prepared_restore_=frame;++statistics_.restore_checkpoint_loads;
    }
    // Pending performs no simulation/capture/store mutation. Keep this intact
    // private staging buffer, not a RAM pointer, while GPU/WSI drains. Earlier
    // late input changes `frame` and therefore reconstructs/revalidates once.
    std::string detail;
    const auto readiness=simulation_.prepare_restore(configuration_.epoch,frame,detail);
    if(readiness==RestoreStep::Pending)return readiness;
    if(readiness!=RestoreStep::Ready) {
        fail("Speculative output retirement failed; gameplay was not rewound: " + detail);return RestoreStep::Failed;
    }
    {performance::Scope timing(performance::Stage::Restore);
     if (!simulation_.restore(scratch_, detail)) {fail("Transactional restore failed: " + detail);return RestoreStep::Failed;}}
    prepared_restore_.reset();
    checkpoints_->discard_after(frame);
    return RestoreStep::Ready;
}

bool Driver::confirm() {
    const auto all = static_cast<std::uint8_t>((1U << configuration_.players) - 1U);
    while (statistics_.confirmed_frames < statistics_.next_frame) {
        auto* frame = find(statistics_.confirmed_frames);
        if (!frame || !frame->simulated || frame->actual_mask != all) break;
        // Real inputs may arrive between step() calls. They cannot become
        // irreversible before a correction has actually been simulated.
        if (frame->used != frame->actual) break;
        std::string detail;
        if (!simulation_.commit(configuration_.epoch, frame->number,
                                frame->output.effects, frame->output.scene_boundary, detail)) {
            return fail("Confirmed effect delivery failed (will not retry): " + detail);
        }
        confirmed_inputs_ = frame->actual;
        ++statistics_.confirmed_frames;
        frame->output.effects.clear();
        if (frame->output.scene_boundary) {
            committed_boundary_ = true;
            break;
        }
    }
    // Experimental history needs no recovery frame older than confirmation.
    // Reclaim those groups and their capacities rather than allowing spare
    // full-RAM checkpoints from repeated correction to accumulate. Stable
    // callers keep their original retention/reuse policy and do not use this.
    checkpoints_->retire_before(statistics_.confirmed_frames);
    checkpoints_->trim_spares(configuration_.checkpoint_budget_bytes-scratch_.capacity());
    return true;
}

bool Driver::correcting() const {
    return active() && (dirty_frame_ != UINT32_MAX || statistics_.next_frame < replay_goal_);
}

Step Driver::step(bool allow_advance) {
    if (!active()) return Step::Failed;
    if (committed_boundary_) return Step::ConfirmedBoundary;
    try {
        if (dirty_frame_ != UINT32_MAX) {
            const auto from = dirty_frame_;
            if (from < statistics_.confirmed_frames || from >= statistics_.next_frame) {
                fail("Experimental correction crossed the confirmed frame boundary.");
                return Step::Failed;
            }
            const auto end = (std::max)(statistics_.next_frame, replay_goal_);
            const auto restored=restore(from);
            if(restored==RestoreStep::Pending)return Step::WaitingForPresentation;
            if(restored!=RestoreStep::Ready)return Step::Failed;
            for (auto frame = from; frame < end; ++frame) {
                if (auto* value = find(frame)) {
                    value->simulated = false;
                    value->output.effects.clear();
                    value->output.scene_boundary = false;
                }
            }
            ++statistics_.rollbacks;
            statistics_.largest_rollback = (std::max)(statistics_.largest_rollback, end - from);
            statistics_.next_frame = from;
            replay_goal_ = end;
            dirty_frame_ = UINT32_MAX;
            boundary_frame_ = UINT32_MAX;
        }
        if (!confirm()) return Step::Failed;
        if (committed_boundary_) return Step::ConfirmedBoundary;
        if (boundary_frame_ != UINT32_MAX) return Step::AwaitingBoundaryConfirmation;
        if (statistics_.next_frame - statistics_.confirmed_frames >= configuration_.prediction_window)
            return Step::PredictionLimit;
        // A scene epoch is finite; wrapping a frame number could alias an old
        // authenticated packet and must not be treated as an ordinary tick.
        if (statistics_.next_frame == UINT32_MAX) {
            fail("Experimental scene frame counter exhausted.");
            return Step::Failed;
        }
        const bool replay = statistics_.next_frame < replay_goal_;
        if (!allow_advance && !replay) return Step::WaitingForLocalInput;
        auto& frame = entry(statistics_.next_frame);
        const auto all = static_cast<std::uint8_t>((1U << configuration_.players) - 1U);
        const bool agreed = frame.actual_mask == all &&
                            statistics_.next_frame == statistics_.confirmed_frames;
        if (simulation_.requires_confirmed_tick() && !agreed)
            return Step::WaitingForConfirmedInput;
        std::string detail;
        const auto readiness = simulation_.prepare_tick(configuration_.epoch, frame.number, agreed, detail);
        if (readiness == RestoreStep::Pending) return Step::WaitingForPresentation;
        if (readiness != RestoreStep::Ready) {
            fail("Tick resource admission failed: " + detail);
            return Step::Failed;
        }
        frame.used = predict(frame);
        frame.output.effects.clear();
        frame.output.scene_boundary = false;
        bool tick_ok;
        {performance::Scope timing(performance::Stage::Tick);
         tick_ok=simulation_.tick(frame.number, frame.used, frame.output, detail);}
        if (!tick_ok) {
            // The adapter may already have modified its reversible world.
            // Recover this tick's starting state, then terminate this mode.
            const auto tick_error = "Replay-safe tick failed: " + detail;
            (void)restore(frame.number);
            fail(tick_error);
            return Step::Failed;
        }
        if (frame.output.effects.size() > kMaximumEffectBytes) {
            (void)restore(frame.number);
            fail("Experimental effect journal exceeded its per-frame budget.");
            return Step::Failed;
        }
        frame.simulated = true;
        ++statistics_.next_frame;
        if (!save(statistics_.next_frame)) return Step::Failed;
        if (replay) ++statistics_.replayed_frames;
        else if (frame.actual_mask != ((1U << configuration_.players) - 1U)) ++statistics_.predicted_frames;
        if (frame.output.scene_boundary) {
            boundary_frame_ = frame.number;
            // A corrected prediction can introduce a transition earlier than
            // the old timeline. Discard that future, never replay past it.
            replay_goal_ = statistics_.next_frame;
        }
        if (!confirm()) return Step::Failed;
        if (committed_boundary_) return Step::ConfirmedBoundary;
        return replay ? Step::Replayed : Step::Advanced;
    } catch (const std::exception& exception) {
        fail(std::string("Experimental rollback stopped: ") + exception.what());
        return Step::Failed;
    }
}

} // namespace dkr::runtime::netplay::experimental
