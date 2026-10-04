#pragma once
#include <cstdint>
#include <span>
namespace dkr::runtime::netplay::experimental {
// Guest RAM stores each 32-bit word byte-reversed on our little-endian hosts.
// A stereo frame is [R low, R high, L low, L high] in that storage. XOR 3
// produces BIG-endian samples; SDL AUDIO_S16LSB needs XOR 2 instead.
inline bool pcm_s16le_from_guest(std::span<const std::uint8_t> ram,
    std::size_t offset, std::span<std::uint8_t> output) {
    if ((offset & 3U) || (output.size() & 3U) || offset > ram.size() ||
        output.size() > ram.size() - offset) return false;
    for (std::size_t b = 0; b < output.size(); ++b) output[b] = ram[(offset + b) ^ 2U];
    return true;
}
}
