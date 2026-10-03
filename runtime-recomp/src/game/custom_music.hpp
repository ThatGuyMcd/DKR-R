#pragma once

// A custom track's recorded music (MP3 or WAV), played on the host.
//
// DKR's music player cannot play a recording: it runs sequences against the
// ROM's instrument bank from a 13 KB buffer. So the retail song the track's
// header names - the carrier - is left to play exactly as retail does, with
// its volume forced to zero at the player boundary, and the file is mixed into
// the audio output in its place. Everything the game does to its music it
// therefore still does: the carrier starting and stopping, fades, the pause
// menu's halving, the options slider and the final-lap speed-up are read back
// from the carrier and applied to the file. Jingles keep their own player and
// are never touched. See docs/CUSTOM_MUSIC_PLAN.md.
//
// A track can instead carry a native sequence (usually a converted MIDI
// file). That one the game plays itself: when the carrier is about to start,
// music_sequence_init has just loaded its bytes into the music buffer, and
// they are replaced with the track's song before the player is given them.
// Its tempo, volume and reverb come from the track too, through the carrier's
// gSeqSoundTable row, which is swapped in for the few instructions that read
// it and put back before anything else can.

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::custom_music {

// Game thread, every level load (custom_tracks' level-load observer). Binds
// the music of the custom track that owns `level` - starting a recording's
// decode on a worker thread - or clears the binding for any other level.
void on_level_load(std::int32_t level);

// Game thread, inside music_sequence_init once a song's bytes are in `buffer`
// and before alCSeqNew reads them (dkr_custom_music_sequence_loaded). When the
// music player is starting the bound track's carrier, the track's sequence
// replaces those bytes - on every start, because the player rewrites loop
// counters in the buffer as it plays - and the carrier's tempo, volume and
// reverb row is swapped for the track's. Anything else is left alone, and so
// is a song this ROM's bank or buffer could not play (it is logged once).
void sequence_loaded(std::uint8_t* rdram, std::uint32_t player, std::uint32_t buffer,
                     std::uint32_t sequence_id_address);

// Game thread, in the same call once the row has been read
// (dkr_custom_music_sequence_started): puts the carrier's own row back.
void sequence_started(std::uint8_t* rdram);

// Game thread, at alCSPSetVol on the music player. `requested` is the volume
// about to be set (after the launcher's music slider). Returns true when the
// bound track's carrier is the song concerned: the caller then sets zero, and
// the file takes the requested level instead.
[[nodiscard]] bool intercept_music_volume(std::uint8_t* rdram, std::int32_t requested);

// Game thread, once per authored frame (main_game_loop's frame-begin hook):
// follows the carrier's play state and tempo. It does not depend on the
// sound_update_queue hook, which until 2026-10-02 sat on the branch that only
// runs when DKR re-reads the song's BPM.
void tick(std::uint8_t* rdram);

// Audio output, with the platform audio lock held: adds `frames` stereo frames
// (interleaved L,R, int16 units) of the music into `out` at `output_rate` Hz.
void render(float* out, std::size_t frames, std::uint32_t output_rate);

// Whether a music file is bound to the loaded level (for the log and the UI).
[[nodiscard]] bool active();

// Whether a native sequence is bound to the loaded level.
[[nodiscard]] bool sequence_active();

} // namespace dkr::runtime::custom_music
