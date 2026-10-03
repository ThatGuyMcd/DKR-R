#include "custom_music_decode.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"

namespace {

// The addon refuses anything longer (music_audio.MAX_SECONDS); the runtime
// enforces it again so a hand-edited manifest cannot ask for unbounded memory.
constexpr std::uint64_t kMaxSeconds = 15U * 60U;
constexpr std::uint32_t kMinRate = 8000U;
constexpr std::uint32_t kMaxRate = 192000U;
// Decoded in pieces so a cancel is noticed promptly and memory grows by steps.
constexpr std::uint64_t kChunkFrames = 32768U;

// -- SHA-256 (FIPS 180-4) ------------------------------------------------

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

void sha256_block(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + choice + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

bool cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

bool check_shape(std::uint32_t channels, std::uint32_t rate, std::string& error) {
    if (channels < 1U || channels > 2U) {
        error = "the file has " + std::to_string(channels) + " channels; mono or stereo only";
        return false;
    }
    if (rate < kMinRate || rate > kMaxRate) {
        error = "a sample rate of " + std::to_string(rate) + " Hz is not supported";
        return false;
    }
    return true;
}

// Appends `count` frames of `channels`-channel PCM as stereo.
void append_stereo(std::vector<std::int16_t>& out, const std::int16_t* pcm,
                   std::uint64_t count, std::uint32_t channels) {
    if (channels == 2U) {
        out.insert(out.end(), pcm, pcm + count * 2U);
        return;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        out.push_back(pcm[i]);
        out.push_back(pcm[i]);
    }
}

bool decode_mp3(const std::uint8_t* data, std::size_t size,
                dkr::runtime::custom_music::DecodedMusic& out, std::string& error,
                const std::atomic<bool>* cancel) {
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data, size, nullptr)) {
        error = "the MP3 could not be opened";
        return false;
    }
    const std::uint32_t channels = mp3.channels;
    const std::uint32_t rate = mp3.sampleRate;
    if (!check_shape(channels, rate, error)) {
        drmp3_uninit(&mp3);
        return false;
    }
    const std::uint64_t limit = kMaxSeconds * rate;
    std::vector<std::int16_t> chunk(static_cast<std::size_t>(kChunkFrames * channels));
    out.samples.clear();
    for (;;) {
        if (cancelled(cancel)) {
            drmp3_uninit(&mp3);
            error = "cancelled";
            return false;
        }
        const drmp3_uint64 read = drmp3_read_pcm_frames_s16(&mp3, kChunkFrames, chunk.data());
        if (read == 0U) {
            break;
        }
        append_stereo(out.samples, chunk.data(), read, channels);
        if (out.samples.size() / 2U > limit) {
            drmp3_uninit(&mp3);
            error = "the music is longer than 15 minutes";
            return false;
        }
    }
    drmp3_uninit(&mp3);
    out.frames = out.samples.size() / 2U;
    out.sample_rate = rate;
    return true;
}

bool decode_wav(const std::uint8_t* data, std::size_t size,
                dkr::runtime::custom_music::DecodedMusic& out, std::string& error,
                const std::atomic<bool>* cancel) {
    drwav wav;
    if (!drwav_init_memory(&wav, data, size, nullptr)) {
        error = "the WAV could not be opened";
        return false;
    }
    const std::uint32_t channels = wav.channels;
    const std::uint32_t rate = wav.sampleRate;
    if (!check_shape(channels, rate, error)) {
        drwav_uninit(&wav);
        return false;
    }
    if (wav.translatedFormatTag != DR_WAVE_FORMAT_PCM &&
        wav.translatedFormatTag != DR_WAVE_FORMAT_IEEE_FLOAT) {
        drwav_uninit(&wav);
        error = "only uncompressed PCM or float WAV is supported";
        return false;
    }
    if (wav.totalPCMFrameCount > kMaxSeconds * rate) {
        drwav_uninit(&wav);
        error = "the music is longer than 15 minutes";
        return false;
    }
    std::vector<std::int16_t> chunk(static_cast<std::size_t>(kChunkFrames * channels));
    out.samples.clear();
    out.samples.reserve(static_cast<std::size_t>(wav.totalPCMFrameCount * 2U));
    for (;;) {
        if (cancelled(cancel)) {
            drwav_uninit(&wav);
            error = "cancelled";
            return false;
        }
        const drwav_uint64 read = drwav_read_pcm_frames_s16(&wav, kChunkFrames, chunk.data());
        if (read == 0U) {
            break;
        }
        append_stereo(out.samples, chunk.data(), read, channels);
    }
    drwav_uninit(&wav);
    out.frames = out.samples.size() / 2U;
    out.sample_rate = rate;
    return true;
}

} // namespace

namespace dkr::runtime::custom_music {

std::string sha256_hex(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint32_t, 8> state = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                          0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                          0x1f83d9abU, 0x5be0cd19U};
    std::size_t offset = 0;
    for (; offset + 64U <= size; offset += 64U) {
        sha256_block(state, data + offset);
    }
    std::array<std::uint8_t, 128> tail{};
    const std::size_t remaining = size - offset;
    std::copy(data + offset, data + size, tail.begin());
    tail[remaining] = 0x80U;
    const std::size_t blocks = remaining + 9U > 64U ? 2U : 1U;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8U;
    for (int i = 0; i < 8; ++i) {
        tail[blocks * 64U - 1U - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bits >> (8 * i));
    }
    for (std::size_t block = 0; block < blocks; ++block) {
        sha256_block(state, tail.data() + block * 64U);
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(kHex[(word >> shift) & 0xFU]);
        }
    }
    return hex;
}

bool decode_bytes(const std::uint8_t* data, std::size_t size,
                  custom_tracks::MusicCodec codec, DecodedMusic& out,
                  std::string& error, const std::atomic<bool>* cancel) {
    const bool ok = codec == custom_tracks::MusicCodec::Wav
        ? decode_wav(data, size, out, error, cancel)
        : decode_mp3(data, size, out, error, cancel);
    if (ok && out.frames == 0U) {
        error = "the file decodes to no audio";
        return false;
    }
    return ok;
}

bool decode_file(const std::filesystem::path& file, custom_tracks::MusicCodec codec,
                 const std::string& sha256, DecodedMusic& out, std::string& error,
                 const std::atomic<bool>* cancel) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "could not open " + file.filename().string();
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() > custom_tracks::kMaxMusicBytes) {
        error = file.filename().string() + " is empty or over 64 MB";
        return false;
    }
    if (!sha256.empty() && sha256_hex(bytes.data(), bytes.size()) != sha256) {
        error = file.filename().string() + " changed since the track was exported; "
                "export it again";
        return false;
    }
    return decode_bytes(bytes.data(), bytes.size(), codec, out, error, cancel);
}

} // namespace dkr::runtime::custom_music
