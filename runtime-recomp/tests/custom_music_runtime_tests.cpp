// custom_music end to end against simulated guest memory: a scanned track with
// a real MP3, a level load, the carrier's volume calls, its play state and
// tempo, and the mixed output.

#include "custom_music.hpp"
#include "custom_music_decode.hpp"
#include "custom_tracks.hpp"
#include "game_payload.hpp"
#include "revision_addresses.hpp"

#include "recomp.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// custom_tracks_hooks.cpp asks for the retail payload on paths this test never
// takes; there is no game here.
namespace dkr::runtime {
const GamePayload* active_payload() { return nullptr; }
} // namespace dkr::runtime

namespace ct = dkr::runtime::custom_tracks;
namespace cm = dkr::runtime::custom_music;
namespace addresses = dkr::runtime::revision_addresses;

namespace {

constexpr std::uint32_t kPlayer = 0x80300000U;   // where the fake ALCSPlayer lives
constexpr std::uint32_t kRate = 44100U;

std::vector<std::uint8_t> g_memory(8U * 1024U * 1024U);

gpr guest(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

void set_song(std::uint8_t current, std::uint8_t next, std::int32_t state,
              std::int16_t bpm, std::uint8_t base = 127U) {
    std::uint8_t* rdram = g_memory.data();
    MEM_W(0, guest(addresses::MusicPlayer)) = static_cast<std::int32_t>(kPlayer);
    MEM_W(0x2C, guest(kPlayer)) = state;
    MEM_BU(0, guest(addresses::CurrentSequence)) = current;
    MEM_BU(0, guest(addresses::MusicNextSequence)) = next;
    MEM_BU(0, guest(addresses::MusicBaseVolume)) = base;
    MEM_H(0, guest(addresses::MusicTempo)) = bpm;
}

float block_peak(std::size_t frames = 512U) {
    std::vector<float> out(frames * 2U, 0.0F);
    cm::render(out.data(), frames, kRate);
    float peak = 0.0F;
    for (const float sample : out) {
        peak = std::max(peak, std::fabs(sample));
    }
    return peak;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

int main() {
    addresses::select(dkr::runtime::rom::Revision::UsV77);
    std::uint8_t* rdram = g_memory.data();

    // A schema 2 track whose header starts sequence 12 and whose music is the
    // addon's 2 s stereo test tone.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "dkrr-custom-music-runtime";
    std::filesystem::remove_all(root);
    const std::filesystem::path track = root / "tone.dkrmap";
    std::filesystem::create_directories(track / "music");
    const std::filesystem::path mp3 =
        std::filesystem::path(DKR_MUSIC_FIXTURES) / "sine_stereo_44k.mp3";
    std::filesystem::copy_file(mp3, track / "music" / "main.mp3");
    std::string header(0xC8, '\0');
    header[0x37] = 1;
    header[0xBB] = 1;
    header[ct::kHeaderMusic] = 12;
    write_text(track / "h.bin", header);
    write_text(track / "manifest.json",
               "{\"schemaVersion\":2,\"id\":\"tone\",\"name\":\"Tone\","
               "\"music\":{\"format\":\"audio-stream-v1\",\"codec\":\"mp3\","
               "\"file\":\"music/main.mp3\",\"sha256\":"
               "\"ae1103b3a8882b10e9fb8ef729e46127737b7d4ec0e68f32c062cc0bc49193b5\","
               "\"bytes\":" + std::to_string(std::filesystem::file_size(mp3)) +
               ",\"sampleRate\":44100,\"channels\":2,\"frames\":88200,"
               "\"carrierSequence\":12,\"volume\":100,\"loopStartFrame\":0,"
               "\"loopEndFrame\":0,\"finalLap\":\"speedup\"},"
               "\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
    ct::scan(root);
    assert(ct::tracks().size() == 1U);
    const std::int32_t retail[] = {0x0, 0x100, 0x250, 0x400, -1};
    (void) ct::build_extended_table(ct::Section::LevelHeaders, retail);
    const std::int32_t level = ct::resolved_level_id("tone");
    assert(level >= 0);

    // A retail level binds nothing: the game's volume passes untouched.
    set_song(12, 0, 1, 120);
    cm::on_level_load(0);
    assert(!cm::active());
    assert(!cm::intercept_music_volume(rdram, 127 * 256));

    // Loading the custom level binds the file and starts its decode.
    set_song(0, 0, 0, -1);
    cm::on_level_load(level);
    assert(cm::active());
    for (int i = 0; i < 200 && block_peak() == 0.0F; ++i) {
        // Nothing plays before the carrier does, decoded or not.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(block_peak() == 0.0F);

    // music_sequence_init: the volume is set while 12 is still the pending
    // song. It is taken for the file and the carrier is silenced.
    set_song(0, 12, 0, -1);
    assert(cm::intercept_music_volume(rdram, 127 * 256));
    // Another song's volume is not ours.
    set_song(5, 0, 1, 100);
    assert(!cm::intercept_music_volume(rdram, 127 * 256));

    // The carrier plays: once the decode is ready, the tone comes through at
    // its own level (the fixture decodes with a peak of 2884).
    set_song(12, 0, 1, 120);
    assert(cm::intercept_music_volume(rdram, 127 * 256));
    cm::tick(rdram);
    float peak = 0.0F;
    for (int i = 0; i < 300 && peak < 1000.0F; ++i) {
        peak = block_peak();
        if (peak < 1000.0F) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    // Past the block that ramped up from silence, and past the encoder's own
    // soft start at the top of the file.
    peak = 0.0F;
    for (int i = 0; i < 8; ++i) {
        peak = std::max(peak, block_peak());
    }
    assert(peak > 2600.0F && peak < 3000.0F);

    // The pause menu halves twice over: base * slider >> 2.
    assert(cm::intercept_music_volume(rdram, (127 * 256) >> 2));
    (void) block_peak();   // the ramp down
    peak = block_peak();
    assert(peak > 600.0F && peak < 760.0F);
    assert(cm::intercept_music_volume(rdram, 127 * 256));
    (void) block_peak();

    // The final lap: the tempo rises and the file follows (no crash, still
    // sounding). The loop wraps the 2 s file many times over without silence.
    set_song(12, 0, 1, 134);
    cm::tick(rdram);
    for (int i = 0; i < 400; ++i) {
        assert(block_peak() > 1000.0F);
    }

    // The carrier stops: one ramp out, then silence.
    set_song(12, 0, 0, 134);
    cm::tick(rdram);
    (void) block_peak();
    assert(block_peak() == 0.0F);

    // Leaving for a retail level clears everything.
    cm::on_level_load(0);
    assert(!cm::active());
    set_song(12, 0, 1, 120);
    cm::tick(rdram);
    assert(block_peak() == 0.0F);
    assert(!cm::intercept_music_volume(rdram, 127 * 256));

    // A native sequence: copied into the music buffer when the music player
    // starts the carrier, with the carrier's tempo/volume/reverb row swapped
    // for the track's until the row has been read.
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path tune = root / "tune.dkrmap";
        std::filesystem::create_directories(tune / "music");
        const std::string hex =
            "00000044000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000180"
            "00ff5107a12000c00100ff2e00ff00903c648140830090436481408300ff2dffff00"
            "00001700ff2f";
        std::vector<std::uint8_t> song;
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            song.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        }
        // One unreachable byte after the end marker makes the length odd, so
        // the buffer must get the pad byte too.
        song.push_back(0x00);
        {
            std::ofstream out(tune / "music" / "main.cseq", std::ios::binary);
            out.write(reinterpret_cast<const char*>(song.data()),
                      static_cast<std::streamsize>(song.size()));
        }
        std::string header(0xC8, '\0');
        header[0x37] = 1;
        header[0xBB] = 1;
        header[ct::kHeaderMusic] = 12;
        header[ct::kHeaderInstruments] = '\xFF';
        header[ct::kHeaderInstruments + 1] = '\xFF';
        write_text(tune / "h.bin", header);
        write_text(tune / "manifest.json",
                   "{\"schemaVersion\":2,\"id\":\"tune\",\"name\":\"Tune\","
                   "\"music\":{\"format\":\"dkr-alcseq-v1\",\"bank\":\"dkr-stock-v1\","
                   "\"file\":\"music/main.cseq\",\"sha256\":\"" +
                       cm::sha256_hex(song.data(), song.size()) + "\",\"bytes\":" +
                       std::to_string(song.size()) +
                       ",\"carrierSequence\":12,\"tempoBpm\":120,\"volume\":100,"
                       "\"reverb\":0,\"channelMask\":65535},"
                   "\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
        ct::scan(root);
        assert(ct::tracks().size() == 1U);
        (void) ct::build_extended_table(ct::Section::LevelHeaders, retail);
        const std::int32_t tune_level = ct::resolved_level_id("tune");
        assert(tune_level >= 0);

        // The game's side: a player with a bank of 128 programs, a sequence
        // table whose longest song is 13,032 bytes, the sound rows, the
        // buffer and the pending id.
        constexpr std::uint32_t kJingle = 0x80300100U;
        constexpr std::uint32_t kBank = 0x80301000U;
        constexpr std::uint32_t kSeqFile = 0x80302000U;
        constexpr std::uint32_t kLengths = 0x80303000U;
        constexpr std::uint32_t kRows = 0x80304000U;
        constexpr std::uint32_t kBuffer = 0x80310000U;
        constexpr std::uint32_t kPending = 0x80305000U;
        MEM_W(0, guest(addresses::MusicPlayer)) = static_cast<std::int32_t>(kPlayer);
        MEM_W(0x20, guest(kPlayer)) = static_cast<std::int32_t>(kBank);
        MEM_H(0, guest(kBank)) = 128;
        for (std::int32_t i = 0; i < 128; ++i) {
            MEM_W(0x0C + i * 4, guest(kBank)) = static_cast<std::int32_t>(0x80400000U + i * 16);
        }
        MEM_W(0, guest(addresses::SequenceTable)) = static_cast<std::int32_t>(kSeqFile);
        MEM_W(0, guest(addresses::SequenceLengths)) = static_cast<std::int32_t>(kLengths);
        MEM_W(0, guest(addresses::SequenceSoundTable)) = static_cast<std::int32_t>(kRows);
        MEM_H(2, guest(kSeqFile)) = 66;
        for (std::int32_t i = 0; i < 66; ++i) {
            MEM_W(i * 4, guest(kLengths)) = i == 30 ? 13032 : 4000;
            MEM_BU(i * 3, guest(kRows)) = 110;
            MEM_BU(i * 3 + 1, guest(kRows)) = 154;
            MEM_BU(i * 3 + 2, guest(kRows)) = 1;
        }
        const auto fill_buffer = [&](std::uint8_t value) {
            for (std::int32_t i = 0; i < 256; ++i) {
                MEM_BU(i, guest(kBuffer)) = value;
            }
        };
        const auto buffer_holds_song = [&] {
            for (std::size_t i = 0; i < song.size(); ++i) {
                if (MEM_BU(static_cast<std::int32_t>(i), guest(kBuffer)) != song[i]) {
                    return false;
                }
            }
            return MEM_BU(static_cast<std::int32_t>(song.size()), guest(kBuffer)) == 0;
        };
        const auto row = [&](std::uint32_t id, std::int32_t field) {
            return MEM_BU(static_cast<std::int32_t>(id * 3) + field, guest(kRows));
        };

        // A retail level: nothing is bound and the carrier loads as retail.
        cm::on_level_load(0);
        assert(!cm::sequence_active());
        fill_buffer(0xAA);
        MEM_BU(0, guest(kPending)) = 12;
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(MEM_BU(0, guest(kBuffer)) == 0xAA && row(12, 1) == 154);

        cm::on_level_load(tune_level);
        assert(cm::sequence_active() && !cm::active());
        // The carrier on the music player: the song replaces the bytes and
        // the row says the track's tempo, volume and reverb until it is read.
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(buffer_holds_song());
        assert(row(12, 0) == 100 && row(12, 1) == 120 && row(12, 2) == 0);
        assert(row(11, 1) == 154 && row(13, 1) == 154);
        cm::sequence_started(rdram);
        assert(row(12, 0) == 110 && row(12, 1) == 154 && row(12, 2) == 1);
        // The player wrote loop counters into the buffer; a restart copies the
        // original again.
        fill_buffer(0x11);
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(buffer_holds_song());
        cm::sequence_started(rdram);
        assert(row(12, 1) == 154);
        // The jingle player, and another song on the music player, are left alone.
        fill_buffer(0xAA);
        cm::sequence_loaded(rdram, kJingle, kBuffer, kPending);
        assert(MEM_BU(0, guest(kBuffer)) == 0xAA && row(12, 1) == 154);
        MEM_BU(0, guest(kPending)) = 13;
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(MEM_BU(0, guest(kBuffer)) == 0xAA && row(13, 1) == 154);
        // A swapped row never outlives one start, even without the second hook.
        MEM_BU(0, guest(kPending)) = 12;
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        MEM_BU(0, guest(kPending)) = 13;
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(row(12, 1) == 154);
        // A recording's volume hook never claims a sequence's carrier.
        set_song(12, 0, 1, 120);
        assert(!cm::intercept_music_volume(rdram, 127 * 256));

        // A bank without the song's program: the carrier plays as retail.
        cm::on_level_load(0);
        cm::on_level_load(tune_level);
        MEM_W(0x0C + 1 * 4, guest(kBank)) = 0;
        fill_buffer(0xAA);
        MEM_BU(0, guest(kPending)) = 12;
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(MEM_BU(0, guest(kBuffer)) == 0xAA && row(12, 1) == 154);
        MEM_W(0x0C + 1 * 4, guest(kBank)) = static_cast<std::int32_t>(0x80400010U);

        // A buffer too small for it (a ROM whose longest song is shorter).
        cm::on_level_load(0);
        cm::on_level_load(tune_level);
        for (std::int32_t i = 0; i < 66; ++i) {
            MEM_W(i * 4, guest(kLengths)) = 64;
        }
        cm::sequence_loaded(rdram, kPlayer, kBuffer, kPending);
        assert(MEM_BU(0, guest(kBuffer)) == 0xAA);

        cm::on_level_load(0);
        assert(!cm::sequence_active());
    }

    std::filesystem::remove_all(root);
    std::printf("[test][custom-music-runtime] PASS\n");
    return 0;
}
