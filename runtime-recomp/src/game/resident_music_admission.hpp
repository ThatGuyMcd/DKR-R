#pragma once
// Resident music's only link to spectator admission: whether a verified
// observer admission is staging music on this thread, which lets the resident
// cache validate and decode the one selected recording.
//
// resident_music.cpp includes this header with angle brackets so the
// spectator-admission Before control (tests/historical/spectator_admission)
// can compile the production resident code without the observer arm and
// prove that DKRSpectatorAdmissionChecks rejects it. Shipped builds only ever
// see this file.
#include "observer_music.hpp"

namespace dkr::spectator_admission {
inline bool resident_music_admitted() noexcept {
    return preparing_music();
}
} // namespace dkr::spectator_admission
