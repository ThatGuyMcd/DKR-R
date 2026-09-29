#include "replay_qualification.hpp"

#include <algorithm>
#include <exception>

namespace dkr::runtime::netplay::experimental {

QualificationResult qualify_replay(Simulation& isolated, std::span<const FrameInputs> inputs) {
    QualificationResult result;
    const auto fail = [&](QualificationFailure reason, std::uint32_t frame, std::string detail) {
        result.failure = reason;
        result.frame = frame;
        result.detail = std::move(detail);
    };
    const auto contract = isolated.contract();
    // Qualification is deliberately offline and tightly bounded. At 32 MiB
    // per checkpoint even this small horizon needs an explicit memory bound.
    if (!Driver::valid_contract(contract) || inputs.empty() || inputs.size() > 20U ||
        contract.state_bytes > (256U * 1024U * 1024U) / (inputs.size() + 2U)) {
        fail(QualificationFailure::InvalidContract, 0, "Invalid isolated replay contract or qualification budget.");
        return result;
    }
    try {
        std::string error;
        std::vector<std::vector<std::uint8_t>> checkpoints(inputs.size() + 1,
            std::vector<std::uint8_t>(contract.state_bytes));
        std::vector<TickOutput> expected(inputs.size());
        std::vector<std::uint8_t> actual(contract.state_bytes);
        if (!isolated.capture(checkpoints[0], error)) {
            fail(QualificationFailure::Capture, 0, error);
            return result;
        }
        std::size_t count = inputs.size();
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            if (!isolated.tick(frame, inputs[frame], expected[frame], error)) {
                fail(QualificationFailure::Tick, frame, error);
                return result;
            }
            if (expected[frame].effects.size() > 64U * 1024U) {
                fail(QualificationFailure::EffectMismatch, frame, "Effect journal exceeded the qualification budget.");
                return result;
            }
            if (!isolated.capture(checkpoints[frame + 1], error)) {
                fail(QualificationFailure::Capture, frame, error);
                return result;
            }
            if (expected[frame].scene_boundary) count = frame + 1U;
        }
        // Rewind across allocating/freeing actors, water updates and RNG at
        // every boundary. A test-only world with hidden native state must fail.
        for (std::uint32_t from = 0; from < count; ++from) {
            if (!isolated.restore(checkpoints[from], error)) {
                fail(QualificationFailure::Restore, from, error);
                return result;
            }
            for (std::uint32_t frame = from; frame < count; ++frame) {
                TickOutput output;
                if (!isolated.tick(frame, inputs[frame], output, error)) {
                    fail(QualificationFailure::Tick, frame, error);
                    return result;
                }
                ++result.replayed_ticks;
                if (!isolated.capture(actual, error)) {
                    fail(QualificationFailure::Capture, frame, error);
                    return result;
                }
                const auto mismatch = std::mismatch(actual.begin(), actual.end(), checkpoints[frame + 1].begin());
                if (mismatch.first != actual.end()) {
                    result.first_differing_byte = static_cast<std::size_t>(mismatch.first - actual.begin());
                    fail(QualificationFailure::StateMismatch, frame,
                         "Save/advance/restore/replay produced different simulation state.");
                    return result;
                }
                if (output.effects != expected[frame].effects) {
                    fail(QualificationFailure::EffectMismatch, frame, "Replay produced a different external-effect journal.");
                    return result;
                }
                if (output.scene_boundary != expected[frame].scene_boundary) {
                    fail(QualificationFailure::BoundaryMismatch, frame, "Replay changed the scene-transition boundary.");
                    return result;
                }
            }
        }
        if (!isolated.restore(checkpoints[0], error)) {
            fail(QualificationFailure::Restore, 0, error);
            return result;
        }
        if (!isolated.capture(actual, error) || actual != checkpoints[0]) {
            fail(QualificationFailure::Restore, 0, "The initial checkpoint did not restore exactly.");
        }
    } catch (const std::exception& exception) {
        fail(QualificationFailure::Tick, result.frame, exception.what());
    }
    return result;
}

} // namespace dkr::runtime::netplay::experimental
