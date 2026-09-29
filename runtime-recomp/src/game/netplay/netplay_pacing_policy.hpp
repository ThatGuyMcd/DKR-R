#pragma once

#include "netplay_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

// Frame debt is meaningful only when both counters belong to the same input
// epoch and to a timeline which is actively advancing.  The gameplay rollback
// coordinator is deliberately destroyed in menus and after a finish seal, so
// its frame cursor must never be compared with the still-running frontend
// input ledger.
enum class FrameDebtPhase : std::uint8_t {
    Inactive,
    Frontend,
    LoadingBarrier,
    GameplayStartBarrier,
    Gameplay,
    RecoveryBarrier,
    FinishBarrier,
    PostRace,
};

// The input ledger counts simulation ticks, not elapsed renderer VIs. Retail
// fb_update can return different values on different machines; letting a menu
// or post-race timer consume those values makes identical commits lead to
// different level-load boundaries. Apply only to an admitted online tick.
inline constexpr std::int32_t authored_logic_step(
    bool online_tick_admitted, std::int32_t local_step) {
    return online_tick_admitted ? 2 : local_step;
}

struct FrameDebtSample {
    FrameDebtPhase phase = FrameDebtPhase::Inactive;
    bool valid = false;
    bool intentionally_parked = false;
    std::uint32_t authoritative_frame = 0U;
    std::uint32_t local_frame = 0U;
    std::uint32_t debt = 0U;
};

inline constexpr bool frame_debt_phase_is_barrier(FrameDebtPhase phase) {
    return phase == FrameDebtPhase::LoadingBarrier ||
           phase == FrameDebtPhase::GameplayStartBarrier ||
           phase == FrameDebtPhase::RecoveryBarrier ||
           phase == FrameDebtPhase::FinishBarrier;
}

inline constexpr bool frame_debt_phase_has_timeline(FrameDebtPhase phase) {
    return phase == FrameDebtPhase::Frontend ||
           phase == FrameDebtPhase::Gameplay ||
           phase == FrameDebtPhase::PostRace;
}

// Frontend and post-race presentation advance on the same immutable authored
// input ledger as gameplay. They do not have a portable rollback-state
// contract, but a lagging guest can still retire ordinary committed frames by
// briefly increasing only its outer authored-VI cadence. Lifecycle barriers
// remain exact parks and must immediately return to retail pacing.
inline constexpr bool frame_debt_phase_allows_catch_up(FrameDebtPhase phase) {
    return phase == FrameDebtPhase::Frontend ||
           phase == FrameDebtPhase::Gameplay ||
           phase == FrameDebtPhase::PostRace;
}

// Player 1 may bound its lead on every actively advancing authored timeline.
// The caller still chooses the phase-appropriate ceiling: gameplay rollback
// can use its replica-backed emergency window, while frontend/post-race must
// use the ordinary small window because no menu snapshot contract exists.
inline constexpr bool frame_debt_phase_allows_host_backpressure(
    FrameDebtPhase phase) {
    return frame_debt_phase_allows_catch_up(phase);
}

// The host-authoritative coordinator does not own a Gekko session, so testing
// only the client pointer leaves Player 1's final race cursor alive after the
// finish seal. Treat either implementation as a live coordinator.
inline constexpr bool rollback_coordinator_present(
    bool host_authoritative, bool client_session_attached) {
    return host_authoritative || client_session_attached;
}

// A rollback cursor belongs exclusively to the active gameplay timeline.
// Frontend and post-race presentation continue on the authored input cursor;
// comparing them with the frozen final race frame creates ever-growing
// diagnostic debt even though no simulation work is outstanding.
inline constexpr bool frame_debt_uses_rollback_cursor(
    FrameDebtPhase phase, bool gameplay_ready,
    bool coordinator_present) {
    return phase == FrameDebtPhase::Gameplay && gameplay_ready &&
           coordinator_present;
}

inline constexpr std::uint32_t frame_debt_local_cursor(
    FrameDebtPhase phase, bool gameplay_ready, bool coordinator_present,
    std::uint32_t rollback_frame, std::uint32_t authored_frame) {
    return frame_debt_uses_rollback_cursor(
               phase, gameplay_ready, coordinator_present)
        ? rollback_frame
        : authored_frame;
}

// Frontend, character/track selection and post-race presentation are sealed
// by the next authenticated gameplay baseline. They must not make Player 1
// block its renderer/audio clock waiting for a remote menu sample. Player 1
// therefore commits hold-last input on these presentation timelines; every
// guest still consumes the same immutable host commit. Racing prediction
// remains governed exclusively by the selected synchronization mode.
inline constexpr bool host_may_predict_input(
    bool authoritative_gameplay_active, SynchronizationMode mode) {
    return !authoritative_gameplay_active ||
           mode == SynchronizationMode::Rollback;
}

inline constexpr FrameDebtSample measure_frame_debt(
    FrameDebtPhase phase, bool same_input_epoch,
    std::uint32_t authoritative_frame, std::uint32_t local_frame) {
    FrameDebtSample sample{};
    sample.phase = phase;
    sample.intentionally_parked = frame_debt_phase_is_barrier(phase);
    sample.authoritative_frame = authoritative_frame;
    sample.local_frame = local_frame;
    sample.valid = same_input_epoch && frame_debt_phase_has_timeline(phase);
    if (sample.valid && authoritative_frame > local_frame) {
        sample.debt = authoritative_frame - local_frame;
    }
    return sample;
}

// The authored race runs at 30 Hz, but the portable Player-1 snapshot is a
// corrective safety net rather than the input timeline.  Capturing and
// installing the complete racer plus moving-actor contract every authored
// tick made water/obstacle-heavy tracks spend most of their frame budget
// scanning, sorting, compressing and applying disposable state.  Five
// corrections per second keep RNG and moving actors bounded to Player 1 while
// leaving the immutable 30 Hz frame ledger and reliable one-second anchors
// untouched. This 200 ms safety-net cadence is intentionally independent of
// the 30 Hz input/commit timeline: the latter remains the source of every
// simulation tick and prevents correction work from pacing heavy tracks.
inline constexpr std::uint32_t kLiveReplicaCorrectionInterval = 6U;
static_assert(kLiveReplicaCorrectionInterval != 0U);

// The ordinary host lead is capped at 24 authored frames. Retain twice that
// span of complete live states so a guest which reaches the pacing boundary
// can always request a still-resident correction. This also covers a complete
// 30-frame reliable keyframe interval without reopening the old multi-second
// authority window.
inline constexpr std::uint32_t kLiveReplicaRecoveryHistoryFrames = 48U;

inline constexpr std::uint32_t live_replica_oldest_retained_frame(
    std::uint32_t newest_frame) {
    return newest_frame > kLiveReplicaRecoveryHistoryFrames
        ? newest_frame - kLiveReplicaRecoveryHistoryFrames : 0U;
}

inline constexpr bool live_replica_within_recovery_window(
    std::uint32_t next_client_frame, std::uint32_t replica_frame) {
    return replica_frame >= live_replica_oldest_retained_frame(
                                next_client_frame) &&
           (next_client_frame >
                    UINT32_MAX - kLiveReplicaRecoveryHistoryFrames ||
            replica_frame <= next_client_frame +
                                 kLiveReplicaRecoveryHistoryFrames);
}

// Installing a replica one frame ahead costs more than it recovers and can
// make harmless packet jitter visible. Two authored frames is the smallest
// useful hand-off: it removes real debt before Player 1 reaches the bounded
// backpressure window, while leaving ordinary one-frame delivery variance to
// the immutable input timeline.
inline constexpr std::uint32_t kMinimumReplicaFastForwardFrames = 2U;

inline bool live_replica_correction_due(std::uint32_t frame) {
    return frame % kLiveReplicaCorrectionInterval == 0U;
}

// Return the first published replica boundary strictly newer than `frame`.
// Saturating at UINT32_MAX keeps the helper well-defined at the end of the
// authored frame space without wrapping back to frame zero.
inline std::uint32_t next_live_replica_frame(std::uint32_t frame) {
    const std::uint32_t remainder = frame % kLiveReplicaCorrectionInterval;
    const std::uint32_t distance = remainder == 0U
        ? kLiveReplicaCorrectionInterval
        : kLiveReplicaCorrectionInterval - remainder;
    if (frame > UINT32_MAX - distance) return UINT32_MAX;
    return frame + distance;
}

// A delayed guest cannot safely execute hidden native game loops to recover
// time: renderer, audio and object-lifecycle work is owned by the outer loop.
// Once a complete Player-1 snapshot exists beyond the guest's target cushion,
// the guest may instead install that exact start-of-frame state and advance
// across the authenticated commit chain. This is a bounded state hand-off,
// not a simulation-speed change.
inline bool replica_fast_forward_window(
    std::uint32_t current_frame, std::uint32_t authoritative_frame,
    std::uint32_t target_debt, std::uint32_t& first_replica_frame,
    std::uint32_t& last_replica_frame) {
    if (authoritative_frame <= current_frame ||
        authoritative_frame <= target_debt) {
        return false;
    }
    const std::uint32_t maximum_frame = authoritative_frame - target_debt;
    const std::uint32_t first_frame = next_live_replica_frame(current_frame);
    if (first_frame <= current_frame || first_frame > maximum_frame ||
        first_frame - current_frame < kMinimumReplicaFastForwardFrames) {
        return false;
    }
    first_replica_frame = first_frame;
    last_replica_frame = maximum_frame;
    return true;
}

// Debt is measured against the newest ALREADY RECEIVED commitment, not the
// host's wall clock. Propagation delay is already behind that cursor: adding
// RTT/2 again retains avoidable old simulation frames. Only jitter needs a
// small receive cushion. Optional correction snapshots never require parking.
inline std::uint32_t rollback_replica_target_debt(
    std::uint16_t round_trip_ms, std::uint16_t jitter_ms,
    std::uint8_t input_delay_frames) {
    constexpr std::uint32_t frame_ms = 34U;
    (void)round_trip_ms;
    const std::uint32_t delivery_budget_ms =
        static_cast<std::uint32_t>(jitter_ms) * 2U + 8U;
    const std::uint32_t measured_frames =
        (delivery_budget_ms + frame_ms - 1U) / frame_ms + 1U;
    const std::uint32_t configured_ceiling = (std::clamp)(
        static_cast<std::uint32_t>(input_delay_frames) + 2U, 2U, 12U);
    return (std::clamp)(measured_frames, 1U, configured_ceiling);
}

// Keep a small delivery cushion behind Player 1 on frontend/post-race
// timelines. This is large enough to absorb one-way latency and jitter without
// pacing oscillation, but is capped well below the host lead ceiling so a real
// hitch always has room to recover. Input delay contributes to the available
// cushion but can never turn accumulated session debt into an accepted target.
inline std::uint32_t authored_timeline_target_debt(
    std::uint16_t round_trip_ms, std::uint16_t jitter_ms,
    std::uint8_t input_delay_frames) {
    return (std::min)(rollback_replica_target_debt(
        round_trip_ms, jitter_ms, input_delay_frames), 8U);
}

inline std::uint32_t authored_catch_up_budget(
    std::uint32_t frame_debt, std::uint32_t target_debt,
    std::uint32_t contiguous_commits, std::uint32_t maximum_ticks) {
    (void)frame_debt;
    (void)target_debt;
    if (maximum_ticks == 0U || contiguous_commits == 0U) return 0U;
    // main_game_loop owns native scheduler, renderer, audio and object
    // lifecycle state that is not rewindable from this call stack.  Executing
    // it more than once from a single outer authored dispatch can leave the
    // first hidden tick waiting for scheduler work that only the outer loop
    // can service.  Debt is handled by bounded host backpressure instead.
    return 1U;
}

// Client catch-up is performed by shortening only the outer authored-VI
// interval. It never executes more than the single canonical game tick
// permitted by authored_catch_up_budget(), and it never changes Player 1's
// fixed 30 Hz authority clock. The controller is deliberately sample-based so
// both runtime code and unit tests share the same deterministic hysteresis.
enum class ClientCatchUpState : std::uint8_t {
    Normal,
    CatchUp,
    Cooldown,
};

struct ClientCatchUpSample {
    bool eligible = false;
    std::uint32_t frame_debt = 0U;
    std::uint32_t target_debt = 0U;
    std::uint32_t contiguous_commits = 0U;
};

struct ClientCatchUpDecision {
    ClientCatchUpState state = ClientCatchUpState::Normal;
    std::uint32_t pacing_scale_milli = 1000U;
    std::uint32_t target_simulation_hz = 30U;
    bool state_changed = false;
};

inline constexpr std::uint32_t client_catch_up_rate_hz(
    std::uint32_t frame_debt, std::uint32_t target_debt) {
    const std::uint32_t excess = frame_debt > target_debt
        ? frame_debt - target_debt : 0U;
    if (excess <= 1U) return 30U;
    if (excess <= 2U) return 32U;
    if (excess <= 5U) return 34U;
    if (excess <= 8U) return 36U;
    if (excess <= 16U) return 38U;
    return 40U;
}

inline constexpr std::uint32_t client_catch_up_scale_milli(
    std::uint32_t simulation_hz) {
    return (std::clamp)(
        (simulation_hz * 1000U + 15U) / 30U, 1000U, 1333U);
}

// Required checkpoint repair needs the request/response round trip, not just
// one-way latency. Keep a LAN floor and bounded retry on distant routes; this
// does not change the independent tiny release/commit retry cadence.
inline constexpr std::uint32_t checkpoint_retry_milliseconds(
    std::uint16_t round_trip_ms, std::uint16_t jitter_ms) {
    return (std::clamp)(100U + 2U * round_trip_ms + 2U * jitter_ms, 100U, 1000U);
}

class ClientCatchUpController {
public:
    ClientCatchUpDecision update(const ClientCatchUpSample& sample) {
        constexpr std::uint8_t entry_samples = 3U;
        constexpr std::uint8_t settled_samples = 6U;
        constexpr std::uint8_t minimum_cooldown_samples = 12U;
        constexpr std::uint32_t rise_step_milli = 67U; // about +2 Hz/sample
        constexpr std::uint32_t fall_step_milli = 25U; // about -0.75 Hz/sample

        const ClientCatchUpState previous_state = state_;
        if (!sample.eligible) {
            reset();
            return decision(previous_state != state_);
        }
        if (sample.contiguous_commits == 0U) {
            // Never speed through a missing commit. A short delivery gap does
            // not erase established catch-up hysteresis, but six consecutive
            // unavailable samples reset it. Phase changes reset immediately.
            current_scale_milli_ = 1000U;
            if (++missing_count_ >= 6U) reset();
            return decision(previous_state != state_);
        }
        missing_count_ = 0U;

        const std::uint32_t settled_limit = sample.target_debt == UINT32_MAX
            ? UINT32_MAX : sample.target_debt + 1U;
        const std::uint32_t entry_debt = sample.target_debt > UINT32_MAX - 2U
            ? UINT32_MAX : sample.target_debt + 2U;
        if (state_ == ClientCatchUpState::Normal) {
            if (sample.frame_debt >= entry_debt &&
                sample.frame_debt > settled_limit) {
                entry_count_ = static_cast<std::uint8_t>(
                    (std::min)(static_cast<unsigned>(entry_count_) + 1U,
                               static_cast<unsigned>(entry_samples)));
                if (entry_count_ >= entry_samples) {
                    state_ = ClientCatchUpState::CatchUp;
                    settled_count_ = 0U;
                }
            } else {
                entry_count_ = 0U;
            }
        } else if (state_ == ClientCatchUpState::CatchUp) {
            if (sample.frame_debt <= settled_limit) {
                settled_count_ = static_cast<std::uint8_t>(
                    (std::min)(static_cast<unsigned>(settled_count_) + 1U,
                               static_cast<unsigned>(settled_samples)));
                if (settled_count_ >= settled_samples) {
                    state_ = ClientCatchUpState::Cooldown;
                    cooldown_count_ = 0U;
                }
            } else {
                settled_count_ = 0U;
            }
        }

        std::uint32_t desired_scale = 1000U;
        if (state_ == ClientCatchUpState::CatchUp) {
            desired_scale = client_catch_up_scale_milli(
                client_catch_up_rate_hz(sample.frame_debt,
                                        sample.target_debt));
        }

        if (current_scale_milli_ < desired_scale) {
            current_scale_milli_ = (std::min)(
                desired_scale, current_scale_milli_ + rise_step_milli);
        } else if (current_scale_milli_ > desired_scale) {
            current_scale_milli_ = current_scale_milli_ >
                    desired_scale + fall_step_milli
                ? current_scale_milli_ - fall_step_milli : desired_scale;
        }

        if (state_ == ClientCatchUpState::Cooldown) {
            cooldown_count_ = static_cast<std::uint8_t>((std::min)(
                static_cast<unsigned>(cooldown_count_) + 1U, 255U));
            if (cooldown_count_ >= minimum_cooldown_samples &&
                current_scale_milli_ == 1000U) {
                state_ = ClientCatchUpState::Normal;
                entry_count_ = 0U;
            }
        }

        return decision(previous_state != state_);
    }

    void reset() {
        state_ = ClientCatchUpState::Normal;
        entry_count_ = 0U;
        settled_count_ = 0U;
        cooldown_count_ = 0U;
        missing_count_ = 0U;
        current_scale_milli_ = 1000U;
    }

    ClientCatchUpState state() const { return state_; }
    std::uint32_t pacing_scale_milli() const {
        return current_scale_milli_;
    }

private:
    ClientCatchUpDecision decision(bool changed) const {
        return ClientCatchUpDecision{
            state_, current_scale_milli_,
            (current_scale_milli_ * 30U + 500U) / 1000U, changed};
    }

    ClientCatchUpState state_ = ClientCatchUpState::Normal;
    std::uint8_t entry_count_ = 0U;
    std::uint8_t settled_count_ = 0U;
    std::uint8_t cooldown_count_ = 0U;
    std::uint8_t missing_count_ = 0U;
    std::uint32_t current_scale_milli_ = 1000U;
};

// Player 1 may stay a small bounded distance ahead so commits continue to
// absorb normal delivery jitter.  The completed-frame watermark then applies
// backpressure before a client hitch can grow into seconds of simulation
// debt.  This never changes DKR's fixed 30 Hz simulation cadence.
inline std::uint32_t host_authority_lead_limit(
    std::uint16_t round_trip_ms, std::uint16_t jitter_ms,
    std::uint8_t input_delay_frames) {
    constexpr std::uint32_t frame_ms = 34U;
    constexpr std::uint32_t minimum_window = 8U;
    constexpr std::uint32_t maximum_window = 24U;

    // SimulationProgress is an acknowledgement, not a one-way replica: a
    // frame must travel Player 1 -> guest, complete there, then return to
    // Player 1. Sizing this window from half the RTT made a healthy LAN fill
    // its three-frame allowance and alternate run/park ticks indefinitely.
    const std::uint32_t acknowledgement_budget_ms =
        static_cast<std::uint32_t>(round_trip_ms) +
        static_cast<std::uint32_t>(jitter_ms) * 2U;
    const std::uint32_t acknowledgement_frames =
        (acknowledgement_budget_ms + frame_ms - 1U) / frame_ms;
    const std::uint32_t processing_margin =
        static_cast<std::uint32_t>(input_delay_frames) + 2U;
    return (std::clamp)(acknowledgement_frames + processing_margin,
                        minimum_window, maximum_window);
}

inline bool host_backpressure_required(
    std::uint32_t next_frame, std::uint32_t peer_completed_frame,
    std::uint32_t maximum_lead) {
    if (next_frame <= peer_completed_frame) return false;
    return next_frame - peer_completed_frame > maximum_lead;
}

// Once Player 1 reaches its hard lead limit, keep the soft hold active until
// the guest has retired a small batch of commits. Releasing on the very first
// progress acknowledgement creates an alternating run/park cadence; a
// three-frame hysteresis window lets the existing 32-40 Hz guest catch-up
// controller settle without changing DKR's fixed authored tick.
inline constexpr std::uint32_t host_backpressure_release_limit(
    std::uint32_t maximum_lead) {
    constexpr std::uint32_t hysteresis_frames = 3U;
    if (maximum_lead > hysteresis_frames) {
        return maximum_lead - hysteresis_frames;
    }
    return maximum_lead > 1U ? maximum_lead - 1U : 0U;
}

// Both synchronization modes use the same immutable Player-1 timeline and the
// production path does not rewind a committed frame. The measured ordinary
// limit is therefore also the hard fairness limit in predictive mode. The old
// 96-frame minimum explicitly allowed Player 1 to become 3.2 seconds ahead of
// a guest and exceeded the live-replica recovery history.
inline std::uint32_t effective_host_authority_lead_limit(
    SynchronizationMode mode, std::uint32_t ordinary_limit) {
    (void)mode;
    return ordinary_limit;
}

// A guest samples against Player 1's newest received frame and then sends its
// future input back to Player 1. Because committed predictions are immutable,
// both predictive and lockstep sessions need runway for that complete feedback
// RTT rather than the half-RTT budget used by genuine rollback. The result is
// shared by every racer for the launch and cannot exceed protocol validation.
inline std::uint8_t host_authoritative_input_delay_frames(
    double p99_round_trip_ms, double burst_jitter_ms, float loss_percent,
    bool local_network) {
    // Keep the qualified high-jitter allowance: the simpler single-percentile
    // candidate lost more short taps on a constrained four-peer route. Only
    // the healthy-route floor is reduced after fresh probe qualification.
    (void)local_network;
    const double bounded_rtt = (std::clamp)(p99_round_trip_ms, 0.0, 10000.0);
    const double bounded_jitter = (std::clamp)(burst_jitter_ms, 0.0, 10000.0);
    const double loss_headroom_ms =
        (std::clamp)(static_cast<double>(loss_percent), 0.0, 15.0) * 2.0;
    const double route_budget_ms = bounded_rtt + bounded_jitter * 2.5 + loss_headroom_ms;
    return static_cast<std::uint8_t>((std::clamp)(
        static_cast<int>(std::ceil(
            route_budget_ms / (1000.0 / 30.0))) + 1,
        2, static_cast<int>(kMaximumInputDelayFrames)));
}

// An unordered reliable authority stream may deliver overlapping batches in
// either order. Including the preceding five commits lets a newly-arrived
// batch heal short loss/reorder bursts immediately; hash validation and frame
// identity make duplicates idempotent. UDP keeps its existing three-frame
// forward-error window.
inline constexpr std::size_t steady_commit_history_count(bool quick_join) {
    return quick_join ? 6U : 3U;
}

} // namespace dkr::runtime::netplay
