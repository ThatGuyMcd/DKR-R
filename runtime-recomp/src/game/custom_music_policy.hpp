#pragma once

// The arithmetic of a track's recorded music, free of the runtime so it can be
// tested on its own: how loud the game wants its music, how fast, and how a
// looping file is resampled into the output. custom_music.cpp owns the state,
// the threads and the guest reads; nothing here touches any of them.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::custom_music {

// The loop in source sample frames. The file plays from 0, and each time the
// read position reaches `end` it continues from `start` - so whatever comes
// before `start` is an intro heard once.
struct LoopRange {
    double start = 0.0;
    double end = 0.0;
};

[[nodiscard]] inline LoopRange loop_range(std::uint64_t frames,
                                          std::uint64_t loop_start,
                                          std::uint64_t loop_end) {
    LoopRange range;
    range.end = static_cast<double>(loop_end != 0U && loop_end <= frames ? loop_end : frames);
    range.start = static_cast<double>(std::min<std::uint64_t>(loop_start, frames));
    if (range.start >= range.end) {
        range.start = 0.0;  // A loop the scan would have refused: loop it all.
    }
    return range;
}

// DKR's music volume as a fraction of full. Every alCSPSetVol on the music
// player is base * slider * fade (audio.c: music_volume_set, music_volume_config_set,
// sound_volume_change), with base the song's own level from gSeqSoundTable and
// slider out of 256. Dividing the base back out leaves what the player and the
// game asked for - the options slider, a fade, the pause menu's halving -
// without the carrier song's mix level, which means nothing to another file.
[[nodiscard]] inline float game_gain(std::int32_t requested, std::uint32_t base_volume) {
    if (requested <= 0 || base_volume == 0U) {
        return 0.0F;
    }
    const double full = static_cast<double>(base_volume) * 256.0;
    return static_cast<float>(std::clamp(static_cast<double>(requested) / full, 0.0, 1.0));
}

// How much faster than its starting tempo the carrier now runs: the final lap
// multiplies DKR's BPM by 1.12 (racer.c), and the file follows when the author
// asked for it. Bounded so a stray tempo read cannot produce a chipmunk or a
// dirge.
[[nodiscard]] inline double tempo_ratio(std::int32_t bpm, std::int32_t base_bpm, bool follow) {
    if (!follow || bpm <= 0 || base_bpm <= 0) {
        return 1.0;
    }
    return std::clamp(static_cast<double>(bpm) / static_cast<double>(base_bpm), 0.5, 2.0);
}

// Moves a read position on by `step` source frames, wrapping at the loop end.
[[nodiscard]] inline double advance(double position, double step, const LoopRange& loop) {
    position += step;
    if (position >= loop.end) {
        const double length = loop.end - loop.start;
        position = length > 0.0 ? loop.start + std::fmod(position - loop.end, length)
                                : 0.0;
    }
    return position;
}

// Adds `frames` stereo frames of the file into `out` (interleaved L,R floats in
// int16 units), reading interleaved stereo int16 `pcm` from `position` at
// `step` source frames per output frame. Linear interpolation, with the frame
// after the last one in the loop being the loop's first, so the seam is as
// smooth as the author cut it. The gain ramps from `gain_from` to `gain_to`
// across the block, so fades and the carrier stopping never click.
inline void render(const std::int16_t* pcm, std::uint64_t source_frames,
                   const LoopRange& loop, double& position, double step,
                   float gain_from, float gain_to, float* out, std::size_t frames) {
    if (pcm == nullptr || source_frames == 0U || frames == 0U || loop.end <= 0.0) {
        return;
    }
    const auto last_in_loop = static_cast<std::uint64_t>(loop.end) - 1U;
    const auto loop_first = static_cast<std::uint64_t>(loop.start);
    const float gain_step = (gain_to - gain_from) / static_cast<float>(frames);
    float gain = gain_from;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        gain += gain_step;
        if (position < 0.0) {
            position = 0.0;
        }
        auto index = static_cast<std::uint64_t>(position);
        if (index > last_in_loop) {
            index = last_in_loop;
        }
        const std::uint64_t next = index >= last_in_loop ? loop_first : index + 1U;
        const float fraction = static_cast<float>(position - static_cast<double>(index));
        const float left = pcm[index * 2U] +
                           (pcm[next * 2U] - pcm[index * 2U]) * fraction;
        const float right = pcm[index * 2U + 1U] +
                            (pcm[next * 2U + 1U] - pcm[index * 2U + 1U]) * fraction;
        out[frame * 2U] += left * gain;
        out[frame * 2U + 1U] += right * gain;
        position = advance(position, step, loop);
    }
}

} // namespace dkr::runtime::custom_music
