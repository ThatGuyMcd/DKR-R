// Production music preparation/checkpoint code with tiny in-memory WAVs and
// synthetic guest fields. No game, ROM, RSP, filesystem or device execution.
#include "observer_music.hpp"
#include "admission_retry.hpp"
#include "mods/legacy_checkpoint.hpp"
#include <iostream>
#include <optional>
#include <thread>

using namespace dkr::mods;
namespace music = dkr::mods::online;
namespace resident = music::resident_music;
namespace repair = dkr::spectator_admission;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value)
        throw Error(why);
}
template <class F> void rejects(F&& f, const char* why) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, why);
}
void le16(Bytes& out, unsigned v) {
    out.push_back(v);
    out.push_back(v >> 8);
}
void le32(Bytes& out, unsigned v) {
    le16(out, v);
    le16(out, v >> 16);
}
Bytes wav(int shift) {
    Bytes out{'R', 'I', 'F', 'F'};
    le32(out, 36 + 32);
    for (char c : std::string("WAVEfmt "))
        out.push_back(c);
    le32(out, 16);
    le16(out, 1);
    le16(out, 1);
    le32(out, 32000);
    le32(out, 64000);
    le16(out, 2);
    le16(out, 16);
    for (char c : std::string("data"))
        out.push_back(c);
    le32(out, 32);
    for (unsigned i = 0; i < 16; ++i)
        le16(out, 1000 + i * 100 + shift);
    return out;
}
void be_word(Bytes& b, unsigned at, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = std::uint8_t(value >> (24 - 8 * i));
}
music::MusicSong song(int shift) {
    music::MusicSong s;
    s.info.kind = dkr::runtime::custom_tracks::MusicKind::Recording;
    s.info.codec = dkr::runtime::custom_tracks::MusicCodec::Wav;
    s.info.frames = 16;
    s.info.sample_rate = 32000;
    s.info.carrier = 1;
    s.info.volume = 100;
    s.info.loop_start = 1;
    s.info.loop_end = 16;
    s.pcm = resident::prepare(wav(shift), s.info, {}).metadata;
    return s;
}
music::MusicSong decoded(music::MusicSong source, int shift) {
    auto pcm = std::make_shared<dkr::runtime::custom_music::DecodedMusic>();
    pcm->frames = 16;
    pcm->sample_rate = 32000;
    for (unsigned i = 0; i < 16; ++i)
        for (unsigned channel = 0; channel < 2; ++channel)
            pcm->samples.push_back(1000 + i * 100 + shift);
    source.pcm = pcm;
    return source;
}
void revision(bool rev80) {
    const auto& addresses = rev80 ? dkr::runtime::revision_addresses::kUsV80 : dkr::runtime::revision_addresses::kUsV77;
    auto sources = std::make_shared<music::MusicLibrary>();
    sources->emplace(65, song(0));
    sources->emplace(66, song(300));
    music::MusicSong sequence;
    sequence.info.kind = dkr::runtime::custom_tracks::MusicKind::Sequence;
    sources->emplace(67, sequence);
    check(sources->at(65).pcm->samples.empty() && sources->at(66).pcm->samples.empty(),
          "Recordings must begin as metadata only");
    Bytes ram(8 * MiB);
    CharacterMenuFields fields;
    fields.fill(0x80000000U);
    CharacterMenuMemory guest(ram, fields);
    guest.write(addresses.MusicPlayer, 0x80400000);
    guest.write(0x8040002c, 1);
    guest.write(addresses.CurrentSequence, 1, 1);
    guest.write(addresses.MusicBaseVolume, 127, 1);
    guest.write(addresses.MusicTempo, 120, 2);
    auto host_sources = std::make_shared<music::MusicLibrary>(*sources);
    host_sources->at(65) = decoded(sources->at(65), 0);
    host_sources->at(66) = decoded(sources->at(66), 300);
    music::MusicState host(host_sources, rev80);
    host.load(guest, 65);
    recomp_context context{};
    context.r4 = 0x80400000;
    context.r5 = 127 * 256;
    check(host.volume(guest, context) && context.r5 == 0, "Host native volume policy changed");
    host.tick(guest);
    const auto checkpoint = host.checkpoint();
    resident::State spectator(sources, rev80);
    rejects([&] { spectator.stage_checkpoint(checkpoint); }, "Cold rollback restore opened a decoder");
    check(!repair::preparing_music(), "Unexpected observer permission");
    // Negative control: compiling this SAME fixture with the old resident
    // implementation fails here with the observed missing-boundary error.
    {
        auto admitted = repair::prepare_verified_music([&] { return spectator.stage_checkpoint(checkpoint); });
        check(!repair::preparing_music(), "Observer permission escaped successful staging");
        check(admitted.checkpoint() == checkpoint, "Admission changed the confirmed music checkpoint");
        auto replay = spectator.stage_checkpoint(checkpoint); // cache hit, no decoder authority
        auto reference = host.stage_checkpoint(checkpoint);
        Bytes expected(128), actual(128), rollback(128);
        reference.mix_s16le(expected, 32000);
        admitted.mix_s16le(actual, 32000);
        replay.mix_s16le(rollback, 32000);
        check(actual == expected && actual != Bytes(128), "Admitted spectator audio differs from the host");
        check(rollback == expected, "Ordinary rollback differs after observer preparation");
        check(admitted.checkpoint() == reference.checkpoint(), "Observer music position diverged");
        for (unsigned n = 0; n < 3; ++n) {
            auto again = repair::prepare_verified_music([&] { return spectator.stage_checkpoint(checkpoint); });
            check(again.checkpoint() == checkpoint, "Repeated admission changed state");
        }
    }
    rejects([&] { spectator.stage_checkpoint(checkpoint); },
            "Released PCM lease remained resident or rollback decoded");
    for (std::size_t size = 0; size < checkpoint.size(); ++size) {
        rejects(
            [&] {
                repair::prepare_verified_music(
                    [&] { return spectator.stage_checkpoint(View(checkpoint).first(size)); });
            },
            "Truncated checkpoint accepted");
        check(!repair::preparing_music(), "Permission escaped failed staging");
    }
    for (unsigned field : {0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U, 36U, 40U, 44U, 48U, 52U}) {
        auto forged = checkpoint;
        be_word(forged, field, 0xffffffffU);
        rejects([&] { repair::prepare_verified_music([&] { return spectator.stage_checkpoint(forged); }); },
                "Forged checkpoint accepted");
        check(!repair::preparing_music(), "Forged checkpoint leaked permission");
    }
    check(spectator.checkpoint() == resident::State(sources, rev80).checkpoint(),
          "Failed staging mutated the live music state");
    rejects([&] { repair::prepare_verified_music([&]() -> int { throw Error("synthetic staging exception"); }); },
            "Exception did not propagate");
    check(!repair::preparing_music(), "Scope exception leaked permission");
    bool isolated = false, cold_rejected = false;
    repair::prepare_verified_music([&] {
        check(repair::preparing_music(), "Scope missing on owner thread");
        std::thread other([&] {
            isolated = !repair::preparing_music();
            try {
                spectator.stage_checkpoint(checkpoint);
            } catch (const Error&) {
                cold_rejected = true;
            }
        });
        other.join();
    });
    check(isolated && cold_rejected, "Observer permission crossed threads");
    // Stock/results and sequence checkpoints need no PCM decoder.
    const auto stock = resident::State(sources, rev80).checkpoint();
    check(repair::prepare_verified_music([&] { return spectator.stage_checkpoint(stock); }).checkpoint() == stock,
          "Results checkpoint failed");
    host.load(guest, 67);
    const auto sequence_state = host.checkpoint();
    check(repair::prepare_verified_music([&] { return spectator.stage_checkpoint(sequence_state); }).checkpoint() ==
              sequence_state,
          "Sequence checkpoint failed");
    host.load(guest, 66);
    const auto second = host.checkpoint();
    rejects([&] { spectator.stage_checkpoint(second); }, "Other track decoded outside admission");
    {
        auto a = repair::prepare_verified_music([&] { return spectator.stage_checkpoint(checkpoint); });
        auto b = repair::prepare_verified_music([&] { return spectator.stage_checkpoint(second); });
        check(a.checkpoint() == checkpoint && b.checkpoint() == second, "Track transition lost a retained recording");
        check(spectator.stage_checkpoint(checkpoint).checkpoint() == checkpoint,
              "Retained old recording was unavailable during rollback");
    }
    check(sources->at(65).pcm->samples.empty() && sources->at(66).pcm->samples.empty(),
          "Shared admitted metadata was mutated");
    auto info = sources->at(65).info;
    info.frames = 32;
    rejects([&] { resident::prepare(wav(0), info, {}); }, "Decoded shape mismatch accepted");
    info.frames = std::uint64_t(64 * MiB) / 4 + 1;
    rejects([&] { resident::prepare(wav(0), info, {}); }, "Music allocation budget bypassed");
    info = sources->at(65).info;
    rejects([&] { resident::prepare(Bytes{1, 2, 3}, info, {}); }, "Invalid encoded recording accepted");
    std::stop_source cancel;
    cancel.request_stop();
    rejects([&] { resident::prepare(wav(0), info, cancel.get_token()); }, "Cancelled preparation accepted");
}
} // namespace
int main() {
    try {
        revision(false);
        revision(true);
        repair::AdmissionRetry retry;
        for (unsigned n = 1; n <= 5; ++n) {
            check(retry.failed(92) == (n < 5), "Observer retry bound changed");
            check(retry.failures() == n, "Observer failure counter changed");
        }
        check(!retry.failed(92) && retry.failures() == 5, "Retry limit overflowed");
        retry.progressed();
        check(retry.failed(92) && retry.failures() == 1, "Confirmed playback did not clear failure streak");
        check(retry.failed(93) && retry.failures() == 1, "New epoch did not reset failures");
        std::cout << checks << " spectator admission checks passed; synthetic WAV/fields only.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SPECTATOR ADMISSION FAILURE: " << e.what() << '\n';
        return 1;
    }
}
