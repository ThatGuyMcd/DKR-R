#include "observer_music.hpp"
#include "resident_music.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace dkr::mods::online::resident_music {
namespace {
using PCM = dkr::runtime::custom_music::DecodedMusic;
constexpr std::size_t MaxDecoded = 64 * MiB;
thread_local unsigned preparation_depth = 0;
thread_local unsigned silent_preparation_depth = 0;
struct Recording {
    Bytes encoded;
    dkr::runtime::custom_tracks::MusicCodec codec;
    PCM descriptor;
    std::string proof;
    mutable std::mutex mutex;
    mutable std::weak_ptr<const PCM> cached;
};
std::mutex recordings_mutex;
std::map<const PCM*, std::weak_ptr<const Recording>> recordings;

std::shared_ptr<const Recording> recording(const PCM* p) {
    std::lock_guard lock(recordings_mutex);
    const auto at = recordings.find(p);
    return at == recordings.end() ? nullptr : at->second.lock();
}
std::shared_ptr<const PCM> acquire(const std::shared_ptr<const Recording>& r, bool allow_decode) {
    std::lock_guard lock(r->mutex);
    if (auto pcm = r->cached.lock())
        return pcm;
    if (!allow_decode || !preparation_depth)
        throw Error("Custom music was not prepared at a confirmed loading boundary.");
    auto pcm = std::make_shared<PCM>();
    std::string error;
    pcm->samples.reserve(std::size_t(r->descriptor.frames) * 2);
    if (!dkr::runtime::custom_music::decode_bytes(r->encoded.data(), r->encoded.size(), r->codec, *pcm, error, nullptr,
                                                  MaxDecoded))
        throw Error("Selected course music cannot be decoded: " + error);
    if (pcm->frames != r->descriptor.frames || pcm->sample_rate != r->descriptor.sample_rate ||
        pcm->samples.size() != pcm->frames * 2 || sha256_pcm_le(pcm->samples) != r->proof)
        throw Error("Selected course music differs from its pre-match decoded proof.");
    r->cached = pcm;
    std::fprintf(
        stderr, "[online-mods][music-residency] selected-frames=%llu rate=%u decoded-bytes=%zu boundary-only=1\n",
        static_cast<unsigned long long>(pcm->frames), pcm->sample_rate, pcm->samples.size() * sizeof(std::int16_t));
    return pcm;
}
} // namespace
PreviewScope::PreviewScope(bool enabled) noexcept : enabled_(enabled) {
    if (enabled_)
        ++silent_preparation_depth;
}
PreviewScope::~PreviewScope() {
    if (enabled_)
        --silent_preparation_depth;
}

PreparationScope::PreparationScope(bool enabled, bool audible) noexcept
    : enabled_(enabled), silent_(enabled && !audible) {
    if (enabled_)
        ++preparation_depth;
    if (silent_)
        ++silent_preparation_depth;
}
PreparationScope::~PreparationScope() {
    if (silent_)
        --silent_preparation_depth;
    if (enabled_)
        --preparation_depth;
}

Prepared prepare(Bytes encoded, const dkr::runtime::custom_tracks::MusicInfo& info, std::stop_token stop) {
    if (!info.frames || info.frames > MaxDecoded / 4 || info.sample_rate < 8000 || info.sample_rate > 192000 ||
        info.frames > std::uint64_t(info.sample_rate) * 15 * 60)
        throw Error("A recording exceeds the supported 15-minute or 64 MiB per-song limit.");
    auto owner = std::make_shared<Recording>();
    owner->encoded = std::move(encoded);
    owner->codec = info.codec;
    PCM pcm;
    pcm.samples.reserve(std::size_t(info.frames) * 2);
    std::string error;
    std::atomic<bool> cancel{stop.stop_requested()};
    std::stop_callback cancellation(stop, [&] { cancel = true; });
    if (!dkr::runtime::custom_music::decode_bytes(owner->encoded.data(), owner->encoded.size(), owner->codec, pcm,
                                                  error, &cancel, MaxDecoded))
        throw Error(error);
    if (stop.stop_requested())
        throw Error("Online music preparation cancelled.");
    if (pcm.frames != info.frames || pcm.sample_rate != info.sample_rate || pcm.samples.size() != pcm.frames * 2)
        throw Error("The decoded frame count or sample rate differs from the track manifest.");
    owner->descriptor.frames = pcm.frames;
    owner->descriptor.sample_rate = pcm.sample_rate;
    owner->proof = sha256_pcm_le(pcm.samples);
    std::shared_ptr<const PCM> metadata(owner, &owner->descriptor);
    {
        std::lock_guard lock(recordings_mutex);
        for (auto it = recordings.begin(); it != recordings.end();)
            it = it->second.expired() ? recordings.erase(it) : std::next(it);
        if (recordings.size() >= 1024)
            throw Error("Too many live immutable music recordings.");
        recordings[metadata.get()] = owner;
    }
    // pcm is released here. Only the encoded recording, shape and proof live
    // until a confirmed course load. Identical recordings share this owner.
    return {std::move(metadata), owner->proof};
}
State::State(std::shared_ptr<const MusicLibrary> source, bool revision80)
    : sources_(std::move(source)), revision80_(revision80), active_(activate(UINT32_MAX, false)),
      native_(active_, revision80) {}
std::shared_ptr<const MusicLibrary> State::activate(unsigned level, bool allow_decode) const {
    auto next = std::make_shared<MusicLibrary>();
    if (!sources_)
        return next;
    // Copy ONLY the selected row. No mutation of a shared immutable library,
    // and no accidental use of a metadata-only descriptor by the mixer.
    const auto at = sources_->find(level);
    if (at == sources_->end())
        return next;
    auto song = at->second;
    if (song.pcm)
        if (const auto r = recording(song.pcm.get()))
            song.pcm = acquire(r, allow_decode || dkr::spectator_admission::preparing_music());
    if (song.pcm && song.pcm->samples.size() != song.pcm->frames * 2)
        throw Error("Selected custom music has no resident samples.");
    next->emplace(level, std::move(song));
    return next;
}
void State::load(CharacterMenuMemory& guest, unsigned level) {
    // Track Select's silent background loader must not decode a whole song
    // each time the cursor moves. Its checkpoint has no active custom song.
    // The actual race continuation loads it before the next epoch is ready.
    auto prepared = silent_preparation_depth ? std::shared_ptr<const MusicLibrary>(std::make_shared<MusicLibrary>())
                                             : activate(level, true);
    native_.load(guest, level); // Restore any native sequence row first.
    MusicState next(prepared, revision80_);
    next.load(guest, level);
    active_ = std::move(prepared);
    native_ = std::move(next);
}
State State::stage_checkpoint(View bytes) const {
    CheckpointReader reader(bytes);
    if (reader.u32() != 1)
        throw Error("Unsupported custom music checkpoint.");
    const auto level = reader.u32();
    // Validate the complete music checkpoint against admitted metadata before
    // spending decoder work. This reads no guest RAM and changes no state.
    if (dkr::spectator_admission::preparing_music())
        (void)MusicState(sources_, revision80_).stage_checkpoint(bytes);
    State next = *this;
    next.active_ = activate(level, false); // Weak-cache hit only; NEVER decode during rollback.
    next.native_ = MusicState(next.active_, revision80_).stage_checkpoint(bytes);
    if (dkr::spectator_admission::preparing_music())
        std::fprintf(stderr, "[spectator][music] checkpoint-ready level=%u active=%u\n", level,
                     unsigned(!next.active_->empty()));
    return next;
}
} // namespace dkr::mods::online::resident_music
