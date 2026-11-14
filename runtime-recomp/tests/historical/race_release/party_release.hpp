#pragma once
// Test double for src/game/party_release.hpp: the release rule party runtime
// shipped with before the 1.0.9-stability-playtest.2 race-release repair.
// Only DKRRaceReleaseBeforeTests puts this directory ahead of src/game, so it
// compiles the production party runtime with this rule. That build must FAIL
// DKRRaceReleaseChecks with "FIRST POST-LOAD RELEASE WAS DISCARDED".
//
// The historical receiver used the local cursor as its duplicate receipt: a Go
// at or before the cursor with no newer controls generation was dropped. Scene
// preparation and spectator checkpoint admission advance the cursor to the new
// epoch at frame 0 before its Go arrives, so the first post-load release was
// mistaken for a retry. It also had a second guard, applied after roster
// verification (go && Running && at == cursor && generation == controls),
// which this first rule already subsumes. It had no activated-release receipt.
#include "party.hpp"
#include <cstdint>
#include <optional>
#include <utility>

namespace dkr::runtime::spectators {
enum class ReleaseDisposition { Apply, Stale, Duplicate };

inline ReleaseDisposition release_disposition(Cursor at, std::uint64_t generation, Cursor cursor,
                                              std::uint64_t controls, bool go,
                                              const std::optional<std::pair<Cursor, std::uint64_t>>&) {
    if (at.epoch < cursor.epoch ||
        (go && at.epoch == cursor.epoch && at.frame <= cursor.frame && generation <= controls))
        return ReleaseDisposition::Stale;
    return ReleaseDisposition::Apply;
}
} // namespace dkr::runtime::spectators
