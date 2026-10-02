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

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::custom_music {

// Game thread, every level load (custom_tracks' level-load observer). Binds
// the music of the custom track that owns `level`, starting its decode on a
// worker thread, or clears the binding for any other level.
void on_level_load(std::int32_t level);

// Game thread, at alCSPSetVol on the music player. `requested` is the volume
// about to be set (after the launcher's music slider). Returns true when the
// bound track's carrier is the song concerned: the caller then sets zero, and
// the file takes the requested level instead.
[[nodiscard]] bool intercept_music_volume(std::uint8_t* rdram, std::int32_t requested);

// Game thread, once per authored frame (main_game_loop's frame-begin hook):
// follows the carrier's play state and tempo. The sound_update_queue hook
// is not used: it sits on the branch that only runs when DKR re-reads the
// song's BPM, which skips it on every other update.
void tick(std::uint8_t* rdram);

// Audio output, with the platform audio lock held: adds `frames` stereo frames
// (interleaved L,R, int16 units) of the music into `out` at `output_rate` Hz.
void render(float* out, std::size_t frames, std::uint32_t output_rate);

// Whether a music file is bound to the loaded level (for the log and the UI).
[[nodiscard]] bool active();

} // namespace dkr::runtime::custom_music
