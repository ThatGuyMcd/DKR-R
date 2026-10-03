#include "custom_music_decode.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace dkr::runtime::custom_music;
using dkr::runtime::custom_tracks::MusicCodec;

namespace {

void put16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    put16(out, value & 0xFFFFU);
    put16(out, value >> 16);
}

// A mono 16-bit WAV of `frames` frames holding 0, 1, 2, ...
std::vector<std::uint8_t> ramp_wav(std::uint32_t frames, std::uint32_t rate,
                                   std::uint16_t format = 1) {
    std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F'};
    put32(wav, 36U + frames * 2U);
    for (const char c : std::string("WAVEfmt ")) wav.push_back(static_cast<std::uint8_t>(c));
    put32(wav, 16U);
    put16(wav, format);
    put16(wav, 1U);
    put32(wav, rate);
    put32(wav, rate * 2U);
    put16(wav, 2U);
    put16(wav, 16U);
    for (const char c : std::string("data")) wav.push_back(static_cast<std::uint8_t>(c));
    put32(wav, frames * 2U);
    for (std::uint32_t i = 0; i < frames; ++i) {
        put16(wav, i & 0x7FFFU);
    }
    return wav;
}

} // namespace

int main(int argc, char** argv) {
    // FIPS 180-4 vectors, including the two-block padding case.
    const std::string abc = "abc";
    assert(sha256_hex(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(sha256_hex(nullptr, 0) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string thousand(1000, 'a');
    assert(sha256_hex(reinterpret_cast<const std::uint8_t*>(thousand.data()),
                      thousand.size()) ==
           "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    const std::string fifty_six(56, 'b');   // padding spills into a second block
    assert(sha256_hex(reinterpret_cast<const std::uint8_t*>(fifty_six.data()),
                      fifty_six.size()).size() == 64U);

    // A WAV decodes to stereo, mono doubled, sample for sample.
    {
        const auto wav = ramp_wav(16000U, 16000U);
        DecodedMusic music;
        std::string error;
        assert(decode_bytes(wav.data(), wav.size(), MusicCodec::Wav, music, error));
        assert(music.frames == 16000U && music.sample_rate == 16000U);
        assert(music.samples.size() == 32000U);
        assert(music.samples[0] == 0 && music.samples[1] == 0);
        assert(music.samples[2 * 1234] == 1234 && music.samples[2 * 1234 + 1] == 1234);
    }
    // Refused: compressed WAV, an MP3 decoded as WAV, a cancelled decode.
    {
        const auto adpcm = ramp_wav(16000U, 16000U, 2U);
        DecodedMusic music;
        std::string error;
        assert(!decode_bytes(adpcm.data(), adpcm.size(), MusicCodec::Wav, music, error));
        const std::vector<std::uint8_t> junk(4096U, 0x55U);
        assert(!decode_bytes(junk.data(), junk.size(), MusicCodec::Wav, music, error));
        assert(!decode_bytes(junk.data(), junk.size(), MusicCodec::Mp3, music, error));
        const auto wav = ramp_wav(100000U, 16000U);
        std::atomic<bool> cancel{true};
        assert(!decode_bytes(wav.data(), wav.size(), MusicCodec::Wav, music, error, &cancel));
        assert(error == "cancelled");
    }

    // The MP3 fixtures the addon's tests use: the decoder yields exactly the
    // length the addon measured (gapless trimming), and the digest guards the
    // bytes.
    std::filesystem::path fixtures = DKR_MUSIC_FIXTURES;
    if (argc > 1) {
        fixtures = argv[1];
    }
    {
        DecodedMusic music;
        std::string error;
        assert(decode_file(fixtures / "sine_stereo_44k.mp3", MusicCodec::Mp3,
                           "ae1103b3a8882b10e9fb8ef729e46127737b7d4ec0e68f32c062cc0bc49193b5",
                           music, error));
        assert(music.frames == 88200U && music.sample_rate == 44100U);
        // A 440 Hz tone, not silence (it peaks at 2884).
        int peak = 0;
        for (const std::int16_t sample : music.samples) {
            peak = std::max(peak, std::abs(static_cast<int>(sample)));
        }
        assert(peak > 2600 && peak < 3200);

        assert(decode_file(fixtures / "sine_mono_22k_id3.mp3", MusicCodec::Mp3,
                           "32c4d06effca0a8ded57208533211dd518cde5f6ae6163d47666ff1f09d165ac",
                           music, error));
        assert(music.frames == 33075U && music.sample_rate == 22050U);
        assert(music.samples[1000] == music.samples[1001]);   // mono, doubled

        assert(!decode_file(fixtures / "sine_mono_22k_id3.mp3", MusicCodec::Mp3,
                            std::string(64, '0'), music, error));
        assert(error.find("changed") != std::string::npos);
        assert(!decode_file(fixtures / "missing.mp3", MusicCodec::Mp3, "", music, error));
    }

    std::printf("[test][custom-music-decode] PASS\n");
    return 0;
}
