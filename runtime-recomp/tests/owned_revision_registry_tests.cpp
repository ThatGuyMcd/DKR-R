#include "netplay/experimental_performance.hpp"
#include <exception>
#include "owned_game.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cstring>
#include "netplay/netplay_types.hpp"
#include "recomp.h"
#include "mods/legacy_memory_safety.h"
#include "runtime_legacy_mods.hpp"
#include "probe_audio.h"
extern "C" {
#define DECLARE_MEMORY_PROBE(revision)                                                                                 \
    dkr_probe_result dkr_experimental_v##revision##_dkr_probe_run(dkr_probe_entry, uint8_t*, size_t, recomp_context*,  \
                                                                  uint64_t);                                           \
    void dkr_experimental_v##revision##_dkr_probe_offline_services(int);                                               \
    void dkr_memory_probe_fallback##revision(uint8_t*, recomp_context*);                                               \
    void dkr_memory_probe_required##revision(uint8_t*, recomp_context*);                                               \
    void dkr_memory_probe_queue##revision(uint8_t*, recomp_context*);
DECLARE_MEMORY_PROBE(77)
#if DKR_OWNED_EXPECT_V80
DECLARE_MEMORY_PROBE(80)
#endif
#define DECLARE_AUDIO_PROBE(revision)                                                                                  \
    void dkr_experimental_v##revision##_dkr_probe_audio_queue_initialize(uint8_t*, recomp_context*);                   \
    void dkr_experimental_v##revision##___CSPPostNextSeqEvent(uint8_t*, recomp_context*);                              \
    void dkr_experimental_v##revision##_dkr_probe_audio_configure(int);                                                \
    void dkr_experimental_v##revision##_dkr_probe_audio_guards_initialize(void);                                       \
    void dkr_experimental_v##revision##_dkr_probe_audio_guards_capture(dkr_probe_audio_guard[8]);                      \
    void dkr_experimental_v##revision##_dkr_probe_audio_guards_restore(const dkr_probe_audio_guard[8]);                \
    uint32_t dkr_experimental_v##revision##_dkr_probe_audio_event_next(uint8_t*, recomp_context*);
DECLARE_AUDIO_PROBE(77)
#if DKR_OWNED_EXPECT_V80
DECLARE_AUDIO_PROBE(80)
#endif
}
static void audio_next77(uint8_t* ram, recomp_context* context) {
    context->r2 = dkr_experimental_v77_dkr_probe_audio_event_next(ram, context);
}
#if DKR_OWNED_EXPECT_V80
static void audio_next80(uint8_t* ram, recomp_context* context) {
    context->r2 = dkr_experimental_v80_dkr_probe_audio_event_next(ram, context);
}
#endif
bool dkr_experimental_v77_dkr_probe_owned_adapter_check(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                                                        unsigned);
bool dkr_experimental_v77_dkr_probe_menu_flow_check(std::span<const std::uint8_t>, bool);
extern "C" void dkr_experimental_v77_dkr_probe_rom(const std::uint8_t*, std::size_t);
#if DKR_OWNED_EXPECT_V80
bool dkr_experimental_v80_dkr_probe_owned_adapter_check(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                                                        unsigned);
bool dkr_experimental_v80_dkr_probe_menu_flow_check(std::span<const std::uint8_t>, bool);
extern "C" void dkr_experimental_v80_dkr_probe_rom(const std::uint8_t*, std::size_t);
#endif
int main(int argc, char** argv) try {
    // Report only opt-in diagnostic counters on ordinary exits. This scope
    // does not change guest state or emit a misleading final report on unwind.
    struct FinalPerformanceReport {
        FinalPerformanceReport() {
            if (dkr::runtime::netplay::experimental::performance::enabled())
                dkr::runtime::netplay::experimental::performance::begin_report_window();
        }
        ~FinalPerformanceReport() {
            if (std::uncaught_exceptions() == 0)
                dkr::runtime::netplay::experimental::performance::report();
        }
    } final_performance_report;

    using namespace dkr::runtime::netplay::experimental;
    using dkr::runtime::rom::Revision;
    unsigned checks = 0;
    const auto check = [&](bool ok) {
        if (!ok)
            throw std::runtime_error("Owned revision registry failed at check " + std::to_string(checks + 1));
        ++checks;
    };
    check(owned_adapter_available(Revision::UsV77));
    check(owned_adapter_available(Revision::UsV80) == bool(DKR_OWNED_EXPECT_V80));
    check(!owned_adapter_available(Revision::Unsupported));
    check(owned_adapter_identity(Revision::UsV77).size() == 64);
    check(owned_adapter_identity(Revision::Unsupported).empty());
    if (DKR_OWNED_EXPECT_V80) {
        check(owned_adapter_identity(Revision::UsV80).size() == 64);
        check(owned_adapter_identity(Revision::UsV77) != owned_adapter_identity(Revision::UsV80));
    }
    PresentationMailbox box;
    std::string error;
    for (auto revision : {Revision::Unsupported, Revision::UsV77, Revision::UsV80}) {
        check(!make_owned_game_for_revision(revision, {}, {}, 91, box, {}, error));
        check(!error.empty());
    }
    check(!make_owned_game({}, {}, 91, box, {}, error));
    // Exercise each production C bridge WITHOUT a ModWorld. These synthetic
    // private heaps never run gameplay or touch a renderer/network/user data.
    const auto memory_probe = [&](unsigned revision, auto run, auto offline, dkr_probe_entry fallback,
                                  dkr_probe_entry required, dkr_probe_entry queue) {
        std::vector<uint8_t> ram(8U * 1024U * 1024U);
        const dkr_memory_view g{ram.data(), ram.size(), revision};
        const uint32_t pool = dkr_memory_pools(g), base = 0x80400000U;
        dkr_memory_write(g, pool, 1600, 4);
        dkr_memory_write(g, pool + 4, 1, 4);
        dkr_memory_write(g, pool + 8, base, 4);
        dkr_memory_write(g, pool + 12, 0x100000U, 4);
        dkr_memory_write(g, base, base + 1600U * 20U, 4);
        dkr_memory_write(g, base + 4, 0x100000U - 1600U * 20U, 4);
        dkr_memory_write(g, base + 10, 65535, 2);
        dkr_memory_write(g, base + 12, 65535, 2);
        check(dkr_memory_install(g, 0x80600000U, 1600));
        dkr_memory_write(g, dkr_memory_count_address(g) + 4, 2, 4);
        check(dkr_memory_measure(g).valid);
        recomp_context context{};
        context.f_odd = &context.f0.u32h;
        offline(1);
        const auto initial = ram;
        auto result = run(fallback, ram.data(), ram.size(), &context, 1000);
        check(result.completed && !result.blocked);
        check(dkr_memory_read(g, dkr_memory_descriptor(g) + 24, 4) == 100);
        const auto after = ram;
        std::copy(initial.begin(), initial.end(), ram.begin());
        result = run(fallback, ram.data(), ram.size(), &context, 1000);
        check(result.completed && ram == after);
        result = run(required, ram.data(), ram.size(), &context, 1000);
        check(!result.completed && result.blocked && std::string(result.blocked) == "required-guest-allocation-failed");
        check(ram == after);
        dkr_memory_write(g, dkr_memory_count_address(g), 1601, 4);
        const auto corrupt = ram;
        result = run(queue, ram.data(), ram.size(), &context, 1000);
        check(!result.completed && result.blocked && std::string(result.blocked) == "guest-memory-safety");
        check(ram == corrupt);
        offline(0);
    };
    memory_probe(77, dkr_experimental_v77_dkr_probe_run, dkr_experimental_v77_dkr_probe_offline_services,
                 dkr_memory_probe_fallback77, dkr_memory_probe_required77, dkr_memory_probe_queue77);
#if DKR_OWNED_EXPECT_V80
    memory_probe(80, dkr_experimental_v80_dkr_probe_run, dkr_experimental_v80_dkr_probe_offline_services,
                 dkr_memory_probe_fallback80, dkr_memory_probe_required80, dkr_memory_probe_queue80);
#endif
    // Real pool-registration bridge and emitted __CSPPostNextSeqEvent, but no game loop,
    // instrument/synth, SDL device or audible output. Both revision bridges
    // must retain their native pool/API ownership and replay the same bytes.
    const auto audio_probe = [&](unsigned revision, auto run, auto offline, auto configure, auto initialize,
                                 auto capture, auto restore, dkr_probe_entry construct, dkr_probe_entry next,
                                 dkr_probe_entry post_sequence) {
        std::vector<uint8_t> ram(8U * 1024U * 1024U);
        const dkr_memory_view g{ram.data(), ram.size(), revision};
        const uint32_t globals = revision == 77 ? 0x800DC630U : 0x800DCBA0U;
        constexpr uint32_t player = 0x80100000U, pool = 0x80101000U, sequence = 0x80200000U;
        const uint32_t queue = player + 0x48, destination = player + 0x38;
        const auto put = [&](uint32_t address, uint32_t value) { dkr_memory_write(g, address, value, 4); };
        const auto get = [&](uint32_t address) { return dkr_memory_read(g, address, 4); };
        const auto empty_context = [&] {
            recomp_context context{};
            context.r4 = int32_t(queue);
            context.r5 = int32_t(destination);
            context.r29 = int32_t(0x80004000U);
            return context;
        };
        const auto invoke = [&](dkr_probe_entry entry, recomp_context& context) {
            context.f_odd = &context.f0.u32h;
            return run(entry, ram.data(), ram.size(), &context, 10000);
        };
        offline(1);
        configure(1);
        initialize();
        put(globals, player);
        put(player + 0x5c, 16667);
        // alEvtqNew ran before the production owned boot handoff. Recreate
        // its exact reverse-inserted free partition in this private fixture.
        put(queue, pool + 119 * 28);
        for (unsigned i = 0; i < 120; ++i) {
            put(pool + i * 28, i ? pool + (i - 1) * 28 : 0);
            put(pool + i * 28 + 4, i < 119 ? pool + (i + 1) * 28 : queue);
        }
        auto context = empty_context();
        context.r5 = int32_t(pool);
        context.r6 = 120;
        check(invoke(construct, context).completed);
        std::array<dkr_probe_audio_guard, 8> saved{}, after{};
        capture(saved.data());
        check(saved[0].queue == queue && saved[0].items == pool && saved[0].item_count == 120);
        // Warm native checkpoint: discard host metadata only, then prove the
        // complete native free partition can be adopted without guessed nodes.
        initialize();
        context = empty_context();
        check(invoke(next, context).completed && context.r2 == 16667);
        check(dkr_memory_read(g, destination, 2) == 9 && !get(queue + 8));
        capture(saved.data());
        check(dkr_audio_guard_valid(&saved[0]) && saved[0].item_count == 120);
        // A known playing sequence has a future delta of five ticks. Recovery
        // must use the real native callback, scheduling REF before API, and
        // keep API pending at its remaining 11667us rather than rewinding music.
        put(player + 0x18, sequence);
        put(player + 0x24, 1000);
        put(player + 0x2c, 1);
        put(sequence + 4, 1);
        put(sequence + 0xb8, 5);
        // First consume an event actually linked by native alEvtqPostEvent.
        // Its nonempty allocList.prev is NULL, not a synthetic tail pointer.
        initialize();
        context = empty_context();
        context.r4 = int32_t(player);
        check(invoke(post_sequence, context).completed && get(queue + 8) && !get(queue + 12));
        context = empty_context();
        check(invoke(next, context).completed && context.r2 == 5000);
        capture(after.data());
        check(after[0].item_count == 120 && after[0].logged_corrupt == 0 && !get(queue + 8));
        const auto baseline = ram;
        capture(saved.data());
        context = empty_context();
        check(invoke(next, context).completed && context.r2 == 5000);
        check(dkr_memory_read(g, destination, 2) == 0);
        const auto pending = get(queue + 8);
        check(pending && get(pending + 8) == 11667 && dkr_memory_read(g, pending + 12, 2) == 9);
        const auto expected = ram;
        capture(after.data());
        ram = baseline;
        restore(saved.data());
        context = empty_context();
        check(invoke(next, context).completed && context.r2 == 5000 && ram == expected);
        capture(saved.data());
        check(!std::memcmp(saved.data(), after.data(), sizeof(saved)));
        configure(0);
        offline(0);
        std::cout << "Production audio v" << revision
                  << ": native pool registration, warm adoption, sequence resume and checkpoint rewind passed.\n";
    };
    audio_probe(77, dkr_experimental_v77_dkr_probe_run, dkr_experimental_v77_dkr_probe_offline_services,
                dkr_experimental_v77_dkr_probe_audio_configure, dkr_experimental_v77_dkr_probe_audio_guards_initialize,
                dkr_experimental_v77_dkr_probe_audio_guards_capture,
                dkr_experimental_v77_dkr_probe_audio_guards_restore,
                dkr_experimental_v77_dkr_probe_audio_queue_initialize, audio_next77,
                dkr_experimental_v77___CSPPostNextSeqEvent);
#if DKR_OWNED_EXPECT_V80
    audio_probe(80, dkr_experimental_v80_dkr_probe_run, dkr_experimental_v80_dkr_probe_offline_services,
                dkr_experimental_v80_dkr_probe_audio_configure, dkr_experimental_v80_dkr_probe_audio_guards_initialize,
                dkr_experimental_v80_dkr_probe_audio_guards_capture,
                dkr_experimental_v80_dkr_probe_audio_guards_restore,
                dkr_experimental_v80_dkr_probe_audio_queue_initialize, audio_next80,
                dkr_experimental_v80___CSPPostNextSeqEvent);
#endif
    if (argc == 5 || argc == 6) {
        const auto read = [](const char* path) {
            std::ifstream stream(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
            if (!stream || stream.tellg() <= 0 || stream.tellg() > 32 * 1024 * 1024)
                throw std::runtime_error("Private fixture size invalid");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stream.tellg()));
            stream.seekg(0);
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
                throw std::runtime_error("Private fixture unreadable");
            return bytes;
        };
        const auto fixture77 = read(argv[1]), rom77 = read(argv[2]), fixture80 = read(argv[3]), rom80 = read(argv[4]);
        // Private menu dispatch must not reach the process-global network
        // session. The linked native boundary throws on any such access.
        for (const auto* rom : {&rom77, &rom80}) {
            auto assets = std::make_shared<dkr::mods::RuntimeSession>(dkr::mods::AssetBank::stock(*rom));
            dkr::runtime::legacy::ModWorld native(assets, {}, nullptr, nullptr, false);
            std::vector<std::uint8_t> ram(8U * 1024U * 1024U);
            const auto before = ram;
            recomp_context context{};
            context.r4 = 1;
            const std::uint64_t observed = 0;
            std::uint64_t result = 99;
            check(native.dispatch("dkr_legacy_track_menu", ram.data(), &context, &observed, 1, nullptr, 12, result) ==
                  1);
            check(result == 0 && ram == before);
        }
        if (argc == 6 && std::string(argv[5]) == "--menu-flow") {
            dkr_experimental_v77_dkr_probe_rom(rom77.data(), rom77.size());
#if DKR_OWNED_EXPECT_V80
            dkr_experimental_v80_dkr_probe_rom(rom80.data(), rom80.size());
#endif
            check(dkr_experimental_v77_dkr_probe_menu_flow_check(fixture77, false));
            check(dkr_experimental_v77_dkr_probe_menu_flow_check(fixture77, true));
#if DKR_OWNED_EXPECT_V80
            check(dkr_experimental_v80_dkr_probe_menu_flow_check(fixture80, false));
#endif
            std::cout << "Production CPU menu prediction/resource fences: race and Adventure flow passed.\n";
            return 0;
        }
        const auto sink = [](ConfirmedAudio, std::string&) { return true; };
        check(!make_owned_game_for_revision(Revision::UsV77, fixture80, rom77, 91, box, sink, error));
        check(!make_owned_game_for_revision(Revision::UsV80, fixture77, rom80, 91, box, sink, error));
        check(!make_owned_game_for_revision(Revision::UsV77, fixture77, rom80, 91, box, sink, error));
        check(dkr_experimental_v77_dkr_probe_owned_adapter_check(fixture77, rom77, 24));
#if DKR_OWNED_EXPECT_V80
        check(dkr_experimental_v80_dkr_probe_owned_adapter_check(fixture80, rom80, 24));
#endif
        // Keep both revision capsules alive and alternate execution. Their
        // private ROM/input/audio bridges must not leak into the other engine.
        std::array<PresentationMailbox, 2> boxes;
        for (auto& mailbox : boxes)
            check(mailbox.begin_epoch(91));
        auto first = make_owned_game_for_revision(Revision::UsV77, fixture77, rom77, 91, boxes[0], sink, error);
        auto second = make_owned_game_for_revision(Revision::UsV80, fixture80, rom80, 91, boxes[1], sink, error);
        check(bool(first) && bool(second));
        std::array<OwnedGame*, 2> worlds{first.get(), second.get()};
        std::array<std::vector<std::uint8_t>, 2> baseline;
        for (unsigned i = 0; i < 2; ++i) {
            baseline[i].resize(worlds[i]->contract().state_bytes);
            check(worlds[i]->capture(baseline[i], error));
        }
        std::array<std::vector<std::uint8_t>, 2> after;
        // tick produces the canonical PCM inside its deferred effects. This
        // comparison must not fingerprint an empty, never-committed sink.
        std::array<std::vector<std::uint8_t>, 2> after_effects;
        for (unsigned pass = 0; pass < 2; ++pass) {
            std::array<std::vector<std::uint8_t>, 2> effects;
            for (unsigned frame = 0; frame < 6; ++frame)
                for (unsigned i = 0; i < 2; ++i) {
                    TickOutput output;
                    check(worlds[i]->tick(frame, {}, output, error) && !output.scene_boundary);
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        effects[i].push_back(std::uint8_t(output.effects.size() >> shift));
                    effects[i].insert(effects[i].end(), output.effects.begin(), output.effects.end());
                }
            for (unsigned i = 0; i < 2; ++i) {
                std::vector<std::uint8_t> state(baseline[i].size());
                check(worlds[i]->capture(state, error));
                if (!pass) {
                    after[i] = state;
                    after_effects[i] = effects[i];
                } else {
                    check(state == after[i]);
                    check(effects[i] == after_effects[i]);
                }
                check(worlds[i]->restore(baseline[i], error));
            }
        }
        for (unsigned i = 0; i < 2; ++i) {
            const auto fingerprint = [](const auto& bytes) {
                return dkr::runtime::netplay::stable_hash(
                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            };
            std::cout << "Cross-platform replay v" << (i ? 80 : 77) << " state=" << std::hex << fingerprint(after[i])
                      << " effects-with-pcm=" << fingerprint(after_effects[i]) << std::dec << '\n';
        }
        std::cout
            << "Both production revision engines: real gameplay corrections, exact state/PCM and interleaved replay isolation passed.\n";
    } else if (argc != 1)
        throw std::runtime_error("Expected zero or four private fixture paths");
    std::cout << checks << " production owned revision dispatch checks passed.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
