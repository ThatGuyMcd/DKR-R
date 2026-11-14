#pragma once
// Whether a racer or spectator applies the host's release (Go) for a cursor.
//
// party_runtime.cpp includes this header with angle brackets so the
// race-release Before control (tests/historical/race_release) can compile the
// production party runtime against the pre-repair rule and prove that
// DKRRaceReleaseChecks rejects it. Shipped builds only ever see this file.
#include "party.hpp"
#include <cstdint>
#include <optional>
#include <utility>

namespace dkr::runtime::spectators {
enum class ReleaseDisposition { Apply, Stale, Duplicate };

// `activated` is the (cursor, controls generation) of the last release this
// peer actually activated. A prepared scene or an admitted observer checkpoint
// advances `cursor` before its Go arrives, so the cursor alone can't tell a
// first post-load release from a retry.
inline ReleaseDisposition release_disposition(Cursor at, std::uint64_t generation, Cursor cursor,
                                              std::uint64_t controls, bool go,
                                              const std::optional<std::pair<Cursor, std::uint64_t>>& activated) {
    if (at.epoch < cursor.epoch ||
        (go && at.epoch == cursor.epoch && at.frame < cursor.frame && generation <= controls))
        return ReleaseDisposition::Stale;
    if (activated && at == activated->first && generation <= activated->second)
        return ReleaseDisposition::Duplicate;
    return ReleaseDisposition::Apply;
}
} // namespace dkr::runtime::spectators
