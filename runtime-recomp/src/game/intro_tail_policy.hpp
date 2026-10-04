#pragma once

#include <algorithm>
#include <cstdint>

namespace dkr::runtime::intro {

enum class TailAction : std::uint8_t {
    Pass,
    Hold,
    Release,
};

// DKR switches away from its first title cinematic on the same authored tick
// that the cutscene reports complete. Preserve the finished scene for a short
// authored-time tail so its final queued voice/music data can drain naturally.
class TailGate {
public:
    static constexpr std::uint32_t kTailUpdateUnits = 30U;

    // Canonical checkpoint fields, not this object's padded host representation.
    // The live runtime's update/reset behaviour is unchanged. The isolated
    // rollback owner uses these fields to rewind the latched completion edge.
    struct Snapshot {
        std::uint32_t phase = 0U;
        std::uint32_t held_update_units = 0U;
    };

    [[nodiscard]] constexpr Snapshot capture() const {
        return {static_cast<std::uint32_t>(state_), held_update_units_};
    }

    [[nodiscard]] static constexpr bool valid(Snapshot value) {
        return (value.phase == 0U && value.held_update_units == 0U) ||
               (value.phase == 1U && value.held_update_units >= 1U &&
                                    value.held_update_units <= kTailUpdateUnits) ||
               (value.phase == 2U && value.held_update_units == kTailUpdateUnits);
    }

    [[nodiscard]] constexpr bool restore(Snapshot value) {
        if (!valid(value)) return false;
        state_ = static_cast<State>(value.phase);
        held_update_units_ = value.held_update_units;
        return true;
    }

    [[nodiscard]] constexpr TailAction update(bool cinematic_complete,
                                               bool first_title_demo,
                                               bool title_revealed,
                                               std::uint32_t update_rate) {
        if (!first_title_demo || title_revealed) {
            reset();
            return TailAction::Pass;
        }

        if (state_ == State::Released) {
            if (cinematic_complete) {
                // The original transition consumes one completion edge. Do
                // not emit a second one if the cinematic reports completion
                // for more than one authored update.
                return TailAction::Hold;
            }
            reset();
            return TailAction::Pass;
        }

        if (state_ == State::Idle) {
            if (!cinematic_complete) {
                return TailAction::Pass;
            }
            // Completion can be a one-update pulse. Latch it before
            // suppressing the original branch so it cannot be lost while the
            // final audio buffer drains.
            state_ = State::Holding;
            held_update_units_ = 0U;
        }

        if (held_update_units_ >= kTailUpdateUnits) {
            state_ = State::Released;
            return TailAction::Release;
        }

        const std::uint32_t step = std::max(update_rate, 1U);
        held_update_units_ = std::min(
            kTailUpdateUnits, held_update_units_ +
                std::min(step, kTailUpdateUnits - held_update_units_));
        return TailAction::Hold;
    }

    constexpr void reset() {
        state_ = State::Idle;
        held_update_units_ = 0U;
    }

private:
    enum class State : std::uint8_t {
        Idle,
        Holding,
        Released,
    };

    State state_ = State::Idle;
    std::uint32_t held_update_units_ = 0U;
};

} // namespace dkr::runtime::intro
