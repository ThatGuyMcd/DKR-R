#pragma once

// Turning a track's music file into samples: the digest check and the decode.
// Kept apart from custom_music.cpp, which reads guest memory, so it builds and
// is tested on its own. The decoders are dr_mp3 and dr_wav (third_party/dr_libs,
// public domain or MIT-0), the same family the addon's measurements mirror.

#include "custom_tracks.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkr::runtime::custom_music {

struct DecodedMusic {
    std::vector<std::int16_t> samples;   // interleaved stereo; mono is doubled
    std::uint64_t frames = 0;
    std::uint32_t sample_rate = 0;
};

// Lowercase hex SHA-256 of `size` bytes.
[[nodiscard]] std::string sha256_hex(const std::uint8_t* data, std::size_t size);

// Reads `file`, checks it against `sha256` (lowercase hex; empty skips the
// check) and decodes it as `codec`. False with a reason in `error` for a file
// that changed since the scan, does not decode, or is outside the bounds the
// addon enforces. `cancel`, when set during the decode, abandons it.
[[nodiscard]] bool decode_file(const std::filesystem::path& file,
                               custom_tracks::MusicCodec codec,
                               const std::string& sha256, DecodedMusic& out,
                               std::string& error,
                               const std::atomic<bool>* cancel = nullptr);

// The same, from bytes already in memory (no digest check).
[[nodiscard]] bool decode_bytes(const std::uint8_t* data, std::size_t size,
                                custom_tracks::MusicCodec codec, DecodedMusic& out,
                                std::string& error,
                                const std::atomic<bool>* cancel = nullptr,
                                std::size_t maximum_decoded_bytes = SIZE_MAX);

} // namespace dkr::runtime::custom_music
