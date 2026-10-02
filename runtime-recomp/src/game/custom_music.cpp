#include "custom_music.hpp"

#include "custom_music_decode.hpp"
#include "custom_music_policy.hpp"
#include "custom_tracks.hpp"
#include "revision_addresses.hpp"

#include "recomp.h"

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
    if (!info) {
        if (g_binding) {
            std::fprintf(stderr, "[custom-music] level %d has no music file\n",
                         static_cast<int>(level));
        }
        g_binding.reset();
        g_carrier.store(0, std::memory_order_release);
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
    std::scoped_lock lock(g_mutex);
    return static_cast<bool>(g_binding);
}

} // namespace dkr::runtime::custom_music
