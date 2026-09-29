#pragma once

#include "experimental_rollback.hpp"

namespace dkr::runtime::netplay::experimental {

enum class QualificationFailure {
    None, InvalidContract, Capture, Tick, Restore, StateMismatch, EffectMismatch, BoundaryMismatch
};

struct QualificationResult {
    QualificationFailure failure = QualificationFailure::None;
    std::uint32_t frame = 0U;
    std::size_t first_differing_byte = 0U;
    std::uint32_t replayed_ticks = 0U;
    std::string detail;
    bool passed() const { return failure == QualificationFailure::None; }
};

// Runs ONLY against an isolated simulation instance, never a live user's
// match. A dishonest/incomplete adapter can leak state outside its checkpoint,
// so an unsuccessful probe cannot promise to repair that instance. No effects
// are committed and nothing is written to a profile or a network peer.
// Exhaustively checks every rewind point in a short authored input sequence,
// not just a save/load byte round trip. Tests include the effect/scene journal.
QualificationResult qualify_replay(Simulation& isolated,
                                   std::span<const FrameInputs> inputs);

} // namespace dkr::runtime::netplay::experimental
