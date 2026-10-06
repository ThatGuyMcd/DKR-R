#include "custom_music.hpp"

#include "custom_music_decode.hpp"
#include "custom_music_policy.hpp"
#include "custom_music_sequence.hpp"
#include "custom_tracks.hpp"
#include "revision_addresses.hpp"
#include "runtime_legacy_mods.hpp"

#include "recomp.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace custom_tracks = dkr::runtime::custom_tracks;
namespace addresses = dkr::runtime::revision_addresses;
using dkr::runtime::custom_music::DecodedMusic;
using dkr::runtime::custom_music::LoopRange;

// ALCSPlayer (include/PR/libaudio.h): a 20-byte ALPlayer node, then drvr,
// target, curTime, bank, uspt and nextDelta, then state.
constexpr std::uint32_t kPlayerStateOffset = 0x2C;
constexpr std::int32_t kAlPlaying = 1;

gpr guest(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

// What the loaded level plays. Immutable once published, apart from `pcm`,
// which the decoder fills in under g_mutex.
struct Binding {
    custom_tracks::MusicInfo info;
    std::string identity;      // file digest plus every playback setting
    LoopRange loop;
    float volume = 1.0F;       // the author's level, 0..2
    std::shared_ptr<const DecodedMusic> pcm;
};

std::mutex g_mutex;
std::shared_ptr<Binding> g_binding;            // null: the level has no music file
std::shared_ptr<const DecodedMusic> g_cache;   // the last file decoded, kept for restarts
std::string g_cache_key;

// The decoder: one worker at a time, joined before the next starts and at exit.
struct Decoder {
    std::thread thread;
    std::atomic<bool> cancel{false};

    void stop() {
        cancel.store(true, std::memory_order_relaxed);
        if (thread.joinable()) {
            thread.join();
        }
        cancel.store(false, std::memory_order_relaxed);
    }
    ~Decoder() { stop(); }
};
Decoder g_decoder;

// Published by the game thread, read by the audio output.
std::atomic<std::uint8_t> g_carrier{0};        // 0 when nothing is bound
std::atomic<bool> g_carrier_playing{false};
std::atomic<float> g_game_gain{1.0F};
std::atomic<float> g_tempo{1.0F};
std::atomic<std::uint32_t> g_restart_serial{0};

// Set at level load (which Track Select previews run off the main thread).
std::atomic<bool> g_follow_tempo{true};

// Game thread only.
bool g_was_playing = false;
std::int32_t g_base_bpm = 0;

// Audio output only.
std::shared_ptr<Binding> g_render_binding;
double g_position = 0.0;
float g_applied_gain = 0.0F;
std::uint32_t g_render_serial = 0;

// A native sequence bound to the loaded level. `info` is immutable once
// published; the checks against the ROM run on the game thread at the first
// start, the only thread that touches `checked` and `usable`.
struct SequenceSong {
    custom_tracks::MusicInfo info;
    std::string identity;
    bool checked = false;
    bool usable = false;
    std::uint32_t starts = 0;
};

std::shared_ptr<SequenceSong> g_sequence;      // under g_mutex

// ALCSPlayer_Custom's bank (0x20); ALBank's instCount (0) and instArray (0x0C);
// ALSeqFile's seqCount (2); MusicData is {volume, tempo, reverb}.
constexpr std::uint32_t kPlayerBankOffset = 0x20;
constexpr std::uint32_t kBankInstrumentsOffset = 0x0C;
constexpr std::uint32_t kSeqFileCountOffset = 0x02;
constexpr std::uint32_t kMusicDataBytes = 3;

// The carrier's gSeqSoundTable row while the track's own stands in for it.
// Game thread only.
struct SavedRow {
    bool held = false;
    std::uint32_t address = 0;
    std::array<std::uint8_t, kMusicDataBytes> bytes{};
};
SavedRow g_saved_row;

std::string sequence_identity(const custom_tracks::MusicInfo& info) {
    return info.sha256 + "|" + std::to_string(info.carrier) + "|" +
           std::to_string(info.volume) + "|" + std::to_string(info.tempo_bpm) + "|" +
           std::to_string(info.reverb) + "|" + std::to_string(info.channel_mask);
}

void restore_row(std::uint8_t* rdram) {
    if (!g_saved_row.held) {
        return;
    }
    for (std::uint32_t index = 0; index < kMusicDataBytes; ++index) {
        MEM_B(index, guest(g_saved_row.address)) =
            static_cast<std::int8_t>(g_saved_row.bytes[index]);
    }
    g_saved_row.held = false;
}

// What the game can actually play, read from guest memory: the music buffer is
// as large as the longest song the ROM's table lists, and a program change to
// a slot the bank leaves null crashes the player.
bool check_against_rom(std::uint8_t* rdram, std::uint32_t player, SequenceSong& song) {
    std::uint32_t capacity = 0;
    const auto table = static_cast<std::uint32_t>(MEM_W(0, guest(addresses::SequenceTable)));
    const auto lengths = static_cast<std::uint32_t>(MEM_W(0, guest(addresses::SequenceLengths)));
    if (table != 0U && lengths != 0U) {
        const auto count = static_cast<std::int16_t>(MEM_H(kSeqFileCountOffset, guest(table)));
        for (std::int32_t index = 0; index < count; ++index) {
            capacity = std::max(capacity, static_cast<std::uint32_t>(
                MEM_W(static_cast<std::int32_t>(index * 4), guest(lengths))));
        }
    }
    if (capacity == 0U) {
        capacity = dkr::runtime::custom_music::kRetailSequenceCapacity;
    }

    std::array<bool, 128> programs{};
    const std::array<bool, 128>* known = nullptr;
    const auto bank = static_cast<std::uint32_t>(MEM_W(kPlayerBankOffset, guest(player)));
    if (bank != 0U) {
        const auto count = static_cast<std::int16_t>(MEM_H(0, guest(bank)));
        for (std::int32_t index = 0; index < 128 && index < count; ++index) {
            programs[static_cast<std::size_t>(index)] =
                MEM_W(static_cast<std::int32_t>(kBankInstrumentsOffset + index * 4U),
                      guest(bank)) != 0;
        }
        known = &programs;
    }

    const auto& bytes = *song.info.sequence;
    const dkr::runtime::custom_music::SequenceCheck check =
        dkr::runtime::custom_music::validate_sequence(bytes, capacity, known);
    if (!check.ok()) {
        std::fprintf(stderr, "[custom-music] %s cannot play in this game: %s (%s); the "
                     "track plays sequence %d instead\n",
                     song.info.file.filename().string().c_str(), check.message.c_str(),
                     check.code.c_str(), static_cast<int>(song.info.carrier));
        return false;
    }
    std::fprintf(stderr, "[custom-music] %s: %u bytes of %u, %u notes, %d BPM\n",
                 song.info.file.filename().string().c_str(),
                 static_cast<unsigned>(bytes.size()), static_cast<unsigned>(capacity),
                 static_cast<unsigned>(check.report.notes),
                 static_cast<int>(song.info.tempo_bpm));
    return true;
}

std::string cache_key(const custom_tracks::MusicInfo& info) {
    return info.sha256 + "|" + info.file.string();
}

std::string identity_of(const custom_tracks::MusicInfo& info) {
    return cache_key(info) + "|" + std::to_string(info.volume) + "|" +
           std::to_string(info.loop_start) + "|" + std::to_string(info.loop_end) + "|" +
           std::to_string(info.carrier) + "|" + (info.final_lap_speedup ? "s" : "c");
}

void decode_for(std::weak_ptr<Binding> target, custom_tracks::MusicInfo info) {
    auto decoded = std::make_shared<DecodedMusic>();
    std::string error;
    if (!dkr::runtime::custom_music::decode_file(info.file, info.codec, info.sha256,
                                                 *decoded, error, &g_decoder.cancel)) {
        if (error != "cancelled") {
            std::fprintf(stderr, "[custom-music] %s: %s; the track plays its game song "
                         "silently instead\n", info.file.filename().string().c_str(),
                         error.c_str());
        }
        return;
    }
    if (decoded->frames != info.frames) {
        // The addon measured with the same decoders, so this is a file it did
        // not write. Play it anyway; the loop is clamped to what decoded.
        std::fprintf(stderr, "[custom-music] %s decoded to %llu frames, the manifest "
                     "says %llu\n", info.file.filename().string().c_str(),
                     static_cast<unsigned long long>(decoded->frames),
                     static_cast<unsigned long long>(info.frames));
    }
    std::scoped_lock lock(g_mutex);
    g_cache = decoded;
    g_cache_key = cache_key(info);
    if (auto binding = target.lock()) {
        binding->loop = dkr::runtime::custom_music::loop_range(
            decoded->frames, info.loop_start, info.loop_end);
        binding->pcm = decoded;
        std::fprintf(stderr, "[custom-music] ready: %s, %.1f s at %u Hz\n",
                     info.file.filename().string().c_str(),
                     static_cast<double>(decoded->frames) / decoded->sample_rate,
                     decoded->sample_rate);
    }
}

// Registered with custom_tracks at static initialisation, so the level-load
// hook reaches this module without custom_tracks depending on it.
[[maybe_unused]] const bool g_registered = [] {
    custom_tracks::set_level_load_observer(&dkr::runtime::custom_music::on_level_load);
    return true;
}();

} // namespace

namespace dkr::runtime::custom_music {

void on_level_load(std::int32_t level) {
    const std::optional<custom_tracks::MusicInfo> info = custom_tracks::music_for_level(level);
    std::unique_lock lock(g_mutex);
    if (!info || info->kind == custom_tracks::MusicKind::Sequence) {
        if (g_binding) {
            std::fprintf(stderr, "[custom-music] level %d has no music file\n",
                         static_cast<int>(level));
        }
        g_binding.reset();
        g_carrier.store(0, std::memory_order_release);
    }
    if (!info || info->kind != custom_tracks::MusicKind::Sequence) {
        if (g_sequence) {
            std::fprintf(stderr, "[custom-music] level %d has no sequence of its own\n",
                         static_cast<int>(level));
        }
        g_sequence.reset();
    }
    if (!info) {
        return;
    }
    if (info->kind == custom_tracks::MusicKind::Sequence) {
        const std::string identity = sequence_identity(*info);
        if (g_sequence && g_sequence->identity == identity) {
            return;  // A restart of the same track: keep what was checked.
        }
        g_sequence.reset();
        // Read at scan; a digest that disagrees means the manifest was edited
        // by hand, and the bytes are not the ones the addon validated.
        const auto& bytes = *info->sequence;
        if (dkr::runtime::custom_music::sha256_hex(bytes.data(), bytes.size()) != info->sha256) {
            std::fprintf(stderr, "[custom-music] %s does not match the manifest's sha256; "
                         "the track plays sequence %d instead\n",
                         info->file.filename().string().c_str(),
                         static_cast<int>(info->carrier));
            return;
        }
        auto song = std::make_shared<SequenceSong>();
        song->info = *info;
        song->identity = identity;
        g_sequence = std::move(song);
        std::fprintf(stderr, "[custom-music] level %d plays %s in place of sequence %d\n",
                     static_cast<int>(level), info->file.filename().string().c_str(),
                     static_cast<int>(info->carrier));
        return;
    }
    g_follow_tempo.store(info->final_lap_speedup, std::memory_order_release);
    g_carrier.store(info->carrier, std::memory_order_release);
    const std::string identity = identity_of(*info);
    if (g_binding && g_binding->identity == identity) {
        return;  // A restart of the same track: keep what is decoded.
    }

    auto binding = std::make_shared<Binding>();
    binding->info = *info;
    binding->identity = identity;
    binding->volume = static_cast<float>(info->volume) / 100.0F;
    std::fprintf(stderr, "[custom-music] level %d plays %s over sequence %d\n",
                 static_cast<int>(level), info->file.filename().string().c_str(),
                 static_cast<int>(info->carrier));
    if (g_cache && g_cache_key == cache_key(*info)) {
        binding->pcm = g_cache;
        binding->loop = loop_range(g_cache->frames, info->loop_start, info->loop_end);
        g_binding = std::move(binding);
        return;
    }
    g_binding = binding;
    lock.unlock();
    // Joining here waits at most for the decoder's current chunk.
    g_decoder.stop();
    g_decoder.thread = std::thread(decode_for, std::weak_ptr<Binding>(binding), *info);
}

bool intercept_music_volume(std::uint8_t* rdram, std::int32_t requested) {
    const std::uint8_t carrier = g_carrier.load(std::memory_order_acquire);
    if (carrier == 0U) {
        return false;
    }
    // music_sequence_init sets the volume before it records the new song as
    // current, while the song is still the pending one; checking both covers
    // the first set as well as every later fade.
    const std::uint8_t current = MEM_BU(0, guest(addresses::CurrentSequence));
    const std::uint8_t next = MEM_BU(0, guest(addresses::MusicNextSequence));
    if (current != carrier && next != carrier) {
        return false;
    }
    const std::uint32_t base = MEM_BU(0, guest(addresses::MusicBaseVolume));
    g_game_gain.store(game_gain(requested, base), std::memory_order_release);
    return true;
}

void tick(std::uint8_t* rdram) {
    if(dkr::runtime::legacy::frozen_music_hook("tick",rdram,nullptr))return;
    const std::uint8_t carrier = g_carrier.load(std::memory_order_acquire);
    bool playing = false;
    if (carrier != 0U) {
        const auto player = static_cast<std::uint32_t>(MEM_W(0, guest(addresses::MusicPlayer)));
        if (player != 0U) {
            const auto state = static_cast<std::int32_t>(
                MEM_W(kPlayerStateOffset, guest(player)));
            playing = state == kAlPlaying &&
                      MEM_BU(0, guest(addresses::CurrentSequence)) == carrier;
        }
    }
    if (playing && !g_was_playing) {
        // The carrier (re)started: the file starts from the top with it, and
        // the tempo it starts at is the one later changes are measured from.
        g_restart_serial.fetch_add(1U, std::memory_order_acq_rel);
        g_base_bpm = 0;
        std::fprintf(stderr, "[custom-music] sequence %d started; the music file plays in its place\n",
                     static_cast<int>(carrier));
    }
    g_was_playing = playing;
    if (playing) {
        const std::int32_t bpm = static_cast<std::int16_t>(MEM_H(0, guest(addresses::MusicTempo)));
        if (g_base_bpm <= 0 && bpm > 0) {
            g_base_bpm = bpm;
        }
        g_tempo.store(static_cast<float>(tempo_ratio(bpm, g_base_bpm,
                                                         g_follow_tempo.load(std::memory_order_acquire))),
                      std::memory_order_release);
    }
    g_carrier_playing.store(playing, std::memory_order_release);
}

void render(float* out, std::size_t frames, std::uint32_t output_rate) {
    if(dkr::runtime::legacy::frozen_music_render(out,frames,output_rate))return;
    if (out == nullptr || frames == 0U || output_rate == 0U) {
        return;
    }
    std::shared_ptr<Binding> binding;
    std::shared_ptr<const DecodedMusic> pcm;
    LoopRange loop;
    {
        std::scoped_lock lock(g_mutex);
        binding = g_binding;
        if (binding) {
            pcm = binding->pcm;
            loop = binding->loop;
        }
    }
    if (binding != g_render_binding) {
        g_render_binding = binding;
        g_position = 0.0;
        g_applied_gain = 0.0F;
        g_render_serial = g_restart_serial.load(std::memory_order_acquire);
    }
    if (!binding || !pcm || pcm->frames == 0U) {
        g_applied_gain = 0.0F;
        return;
    }

    const std::uint32_t serial = g_restart_serial.load(std::memory_order_acquire);
    const bool restarting = serial != g_render_serial;
    const bool playing = g_carrier_playing.load(std::memory_order_acquire) && !restarting;
    const float target = playing
        ? g_game_gain.load(std::memory_order_acquire) * binding->volume : 0.0F;
    if (g_applied_gain <= 0.0F && target <= 0.0F) {
        // Silent: a restart can rewind without a click.
        g_applied_gain = 0.0F;
        if (restarting) {
            g_position = 0.0;
            g_render_serial = serial;
        }
        return;
    }
    const double step = static_cast<double>(pcm->sample_rate) / output_rate *
                        g_tempo.load(std::memory_order_acquire);
    dkr::runtime::custom_music::render(pcm->samples.data(), pcm->frames, loop, g_position,
                                       step, g_applied_gain, target, out, frames);
    g_applied_gain = target;
}

bool active() {
    if(dkr::runtime::legacy::frozen_online_resources())return true;
    std::scoped_lock lock(g_mutex);
    return static_cast<bool>(g_binding);
}

bool sequence_active() {
    std::scoped_lock lock(g_mutex);
    return static_cast<bool>(g_sequence);
}

void sequence_loaded(std::uint8_t* rdram, std::uint32_t player, std::uint32_t buffer,
                     std::uint32_t sequence_id_address) {
    restore_row(rdram);  // Never carry a swapped row past one start.
    std::shared_ptr<SequenceSong> song;
    {
        std::scoped_lock lock(g_mutex);
        song = g_sequence;
    }
    if (!song || player == 0U || buffer == 0U ||
        player != static_cast<std::uint32_t>(MEM_W(0, guest(addresses::MusicPlayer))) ||
        MEM_BU(0, guest(sequence_id_address)) != song->info.carrier) {
        return;  // A jingle, a menu song, or another level's music.
    }
    if (!song->checked) {
        song->checked = true;
        song->usable = check_against_rom(rdram, player, *song);
    }
    if (!song->usable) {
        return;
    }

    // Over whatever asset_load put there. The player reads the song from this
    // buffer as it plays and writes loop counters into it, so every start
    // copies the original bytes again. An odd length gets the pad byte the
    // retail loader's even lengths imply.
    const auto& bytes = *song->info.sequence;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        MEM_B(static_cast<std::int32_t>(index), guest(buffer)) =
            static_cast<std::int8_t>(bytes[index]);
    }
    if ((bytes.size() & 1U) != 0U) {
        MEM_B(static_cast<std::int32_t>(bytes.size()), guest(buffer)) = 0;
    }

    // music_sequence_init then sets the volume, tempo and reverb from the
    // carrier's row. The tempo matters most: almost every race song has one
    // there, and music_tempo_set would play this song at the carrier's.
    const auto table = static_cast<std::uint32_t>(MEM_W(0, guest(addresses::SequenceSoundTable)));
    if (table != 0U) {
        g_saved_row.address = table + song->info.carrier * kMusicDataBytes;
        for (std::uint32_t index = 0; index < kMusicDataBytes; ++index) {
            g_saved_row.bytes[index] = MEM_BU(index, guest(g_saved_row.address));
        }
        g_saved_row.held = true;
        MEM_B(0, guest(g_saved_row.address)) = static_cast<std::int8_t>(song->info.volume);
        MEM_B(1, guest(g_saved_row.address)) = static_cast<std::int8_t>(song->info.tempo_bpm);
        MEM_B(2, guest(g_saved_row.address)) = static_cast<std::int8_t>(song->info.reverb);
    }
    if (song->starts++ == 0U) {
        std::fprintf(stderr, "[custom-music] sequence %d started with the track's own song\n",
                     static_cast<int>(song->info.carrier));
    }
}

void sequence_started(std::uint8_t* rdram) {
    restore_row(rdram);
}

} // namespace dkr::runtime::custom_music

// music_sequence_init, after asset_load has filled the buffer: s0 is the
// player, s1 the buffer and s3 the address of the pending song id (0x800023B4
// in both US revisions).
extern "C" void dkr_custom_music_sequence_loaded(std::uint8_t* rdram, recomp_context* ctx) {
    if(dkr::runtime::legacy::frozen_music_hook("loaded",rdram,ctx))return;
    dkr::runtime::custom_music::sequence_loaded(rdram, static_cast<std::uint32_t>(ctx->r16),
                                                static_cast<std::uint32_t>(ctx->r17),
                                                static_cast<std::uint32_t>(ctx->r19));
}

// music_sequence_init, once sound_reverb_set has returned: the row has been
// read for the last time in this call (0x8000247C in both US revisions).
extern "C" void dkr_custom_music_sequence_started(std::uint8_t* rdram, recomp_context* ctx) {
    if(dkr::runtime::legacy::frozen_music_hook("started",rdram,ctx))return;
    dkr::runtime::custom_music::sequence_started(rdram);
}
