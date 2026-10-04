#pragma once

// Validating a native DKR music sequence (libultra's compact ALCSeq) before it
// is copied into the game's music buffer. The player trusts its data: an
// unknown meta event leaves the event type uninitialised, a zero-length back
// reference wraps a byte counter, a loop that lands mid-event desynchronises
// its track and a loop over no ticks never lets a frame end. This reads a song
// the way cseq.c and csplayer.c will and refuses what they would mishandle.
//
// It is the twin of the addon's tools/blender/track_lab/music_sequence.py:
// the same rules and the same error codes, held together by the shared
// fixtures in tools/blender/tests/fixtures/music_sequences.txt. Whether a note
// reaches a sound of its program is checked by the addon only - a dropped note
// is an authoring mistake, not a hazard - but a program change to a null slot
// crashes the player, so the caller may pass which programs exist.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace dkr::runtime::custom_music {

// The music buffer the retail game allocates at boot (largest even-rounded
// song in US 1.0 and 1.1); a custom song is copied into that same buffer.
inline constexpr std::uint32_t kRetailSequenceCapacity = 13032U;

struct SequenceReport {
    std::uint32_t size = 0;
    std::uint32_t division = 0;
    std::uint32_t notes = 0;
    std::uint16_t track_mask = 0;
    std::uint16_t channel_mask = 0;
    std::uint32_t initial_tempo = 0;   // microseconds per beat at tick 0; 0 if none
    bool loops_forever = false;
};

struct SequenceCheck {
    std::string code;      // "ok" or the shared error code
    std::string message;   // for the log; empty when ok
    SequenceReport report;

    [[nodiscard]] bool ok() const { return code == "ok"; }
};

// `programs`, when given, marks which of the bank's 128 program slots are
// non-null.
[[nodiscard]] SequenceCheck validate_sequence(
    std::span<const std::uint8_t> data,
    std::uint32_t capacity = kRetailSequenceCapacity,
    const std::array<bool, 128>* programs = nullptr);

} // namespace dkr::runtime::custom_music
