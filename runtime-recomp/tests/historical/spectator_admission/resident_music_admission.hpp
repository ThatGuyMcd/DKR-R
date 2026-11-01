#pragma once
// Test double for src/game/resident_music_admission.hpp: resident music as it
// shipped before the playtest.4 spectator admission repair, which had no
// observer arm. Activation only decoded at a confirmed course load, and an
// observer checkpoint was never pre-validated against admitted metadata.
//
// Only DKRSpectatorAdmissionBeforeTests puts this directory ahead of src/game,
// so it compiles the production resident_music.cpp with this answer. That
// build must FAIL DKRSpectatorAdmissionChecks with "Custom music was not
// prepared at a confirmed loading boundary." on the first admitted cold
// observer checkpoint.
namespace dkr::spectator_admission {
inline bool resident_music_admitted() noexcept {
    return false;
}
} // namespace dkr::spectator_admission
