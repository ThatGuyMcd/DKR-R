#include "legacy_mod_format.hpp"
#include "legacy_character_artifact.hpp"
#include "legacy_track_artifact.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_audio_bank.hpp"
#include "legacy_mod_geometry.hpp"
#include "legacy_mod_library.hpp"
#include "legacy_runtime_session.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

namespace {
using namespace dkr::mods;
unsigned checks = 0;
void require(bool ok, const char* why) {
    if (!ok)
        throw Error(why);
    ++checks;
}
template <class F> void rejects(F&& f) {
    bool caught = false;
    try {
        f();
    } catch (const Error&) {
        caught = true;
    }
    require(caught, "Invalid content was accepted.");
}
struct Temp {
    std::filesystem::path path =
        std::filesystem::current_path() /
        ("compatibility-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() {
        require(std::filesystem::create_directory(path), "Could not create isolated test folder.");
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
struct Fixture {
    const char* name;
    int character;
    std::set<unsigned> tracks;
};
void wait(ModLibrary& library) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
    while (std::chrono::steady_clock::now() < deadline) {
        library.tick();
        if (!library.snapshot().busy) {
            if (!library.snapshot().succeeded)
                throw Error(library.snapshot().result);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw Error("Isolated library operation timed out.");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::cerr << "Usage: DKRLegacyCompatibilityTests owned-v1.0 owned-v1.1 supplied-mod-folder [mod-worker]\n";
        return 2;
    }
    try {
        Temp temp;
        auto source = read_file(utf8_path(argv[1]), MaxImage), other = read_file(utf8_path(argv[2]), MaxImage);
        canonicalize_rom(source);
        canonicalize_rom(other);
        require(verified_revision(source) == "us.v77" && verified_revision(other) == "us.v80",
                "Expected verified US revisions.");
        const auto source_hash = sha256(source), other_hash = sha256(other);
        const auto native0 = AssetBank::stock(source), native1 = AssetBank::stock(other);
        const Fixture fixtures[]{{"Rexy(sixty-four) v1.zip", 8, {}},
                                 {"bond_in_dkr-3.zip", 5, {}},
                                 {"dkr-oot64-1.1(sixtyfour).zip", 8, {5, 8, 13, 19}},
                                 {"DKR-GoldenEye(sixty-four).zip", -1, {3, 5, 7, 29}},
                                 {"Rainbow Road 1.1(sixtyfour).zip", -1, {4}},
                                 {"mario.xdelta", 9, {}},
                                 {"dixie kong.xdelta", 9, {}}};
        unsigned fixture_id = 0;
        for (const auto& fixture : fixtures) {
            std::cout << fixture.name << std::endl;
            const auto patches = read_patch_inputs(utf8_path(argv[3]) / fixture.name);
            require(patches.size() == 1, "Unexpected fixture patch count.");
            const auto& patch = patches.front();
            const auto digest = sha256(patch.data);
            const auto target = decode_patch(source, patch.data);
            const auto analysis = analyze(source, target, digest);
            require(analysis.blockers.empty(), "Fixture failed analysis.");
            std::set<unsigned> actual;
            for (const auto& root : analysis.tracks)
                actual.insert(root.carrier);
            require(actual == fixture.tracks, "Incorrect playable roots / collateral course edits exposed.");
            require(analysis.character_roots.size() == (fixture.character < 0 ? 0 : 1),
                    "Incorrect racer roots / incidental edits exposed.");
            const auto folder = temp.path / std::to_string(fixture_id++);
            std::filesystem::create_directory(folder);
            if (fixture.tracks.contains(7)) {
                const AssetImage image(target, "us.v77");
                const auto packed = image.record(27, 7);
                const auto raw = inflate_asset(packed, 4 * MiB);
                const auto tree = be32(raw, 0x14);
                const auto summary = validate_geometry(raw, packed.size());
                std::vector<unsigned> pending{0};
                bool tested = false;
                for (unsigned examined = 0; !pending.empty() && examined < 127; ++examined) {
                    const auto node = pending.back();
                    pending.pop_back();
                    const auto pos = tree + 8 * node;
                    const auto left = be16(raw, pos), right = be16(raw, pos + 2);
                    if (left == 0xffff && right == 0xffff) {
                        auto variant = raw;
                        variant[pos + 5] = 255;
                        require(validate_geometry(variant, packed.size()).segments == summary.segments,
                                "Unused leaf split was incorrectly rejected.");
                        tested = true;
                        break;
                    }
                    if (left != 0xffff)
                        pending.push_back(left);
                    if (right != 0xffff)
                        pending.push_back(right);
                }
                require(tested, "No real BSP leaf used by the geometry regression check.");
                auto cycle = raw;
                cycle[tree] = cycle[tree + 1] = 0;
                rejects([&] { validate_geometry(cycle, packed.size()); });
                auto outside = raw;
                outside[tree + 5] = 255;
                rejects([&] { validate_geometry(outside, packed.size()); });
            }
            if (fixture.character >= 0) {
                auto c = prepare_character(source, target, digest, fixture.character);
                require(c.root.base_character == unsigned(fixture.character), "Wrong inherited racer behaviour.");
                const auto manifest = write_character_artifact(folder / "character", c, *native0);
                for (const auto& native : {native0, native1}) {
                    const auto loaded = read_character_artifact(folder / "character", manifest, *native);
                    require(loaded.root.content_id == c.root.content_id, "Character identity changed across revision.");
                    const auto ns = allocate_characters(native, {loaded});
                    const auto bank = ns.apply(native);
                    require(ns.characters.size() == 1 && bank->augmented(), "Additive racer allocation failed.");
                    for (unsigned section : {2U, 4U, 12U, 29U, 32U, 34U})
                        for (unsigned i = 0; i < native->record_count(section); ++i)
                            require(std::ranges::equal(bank->record(section, i), native->record(section, i)),
                                    "A stock racer resource was replaced.");
                    AssetBus bus;
                    require(bool(ResidentBank::prepare(bank, bank, bus)),
                            "Character boot bank failed residency validation.");
                    auto bad = loaded;
                    bad.base_fingerprint = std::string(64, 'f');
                    rejects([&] { allocate_characters(native, {bad}); });
                    std::cout << "  character " << native->revision() << " prepare/persist/allocate/resident PASS\n";
                }
            }
            for (auto carrier : fixture.tracks) {
                std::string identity;
                for (const auto& native : {native0, native1}) {
                    const auto track =
                        prepare_track_bank(source, target, digest, carrier, native == native0 ? source : other);
                    const auto artifact_folder = folder / (std::to_string(carrier) + native->revision());
                    const auto manifest = write_track_artifact(artifact_folder, track, digest, analysis.profile);
                    const auto loaded = read_track_artifact(artifact_folder, manifest, native);
                    require(track.bank->fingerprint() == loaded.bank->fingerprint(),
                            "Persisted track identity changed.");
                    if (identity.empty())
                        identity = track.root.content_id;
                    require(identity == track.root.content_id, "Public track identity changed across revisions.");
                    for (unsigned i = 0; i < native->record_count(23); ++i)
                        if (i != carrier)
                            require(std::ranges::equal(native->record(23, i), loaded.bank->record(23, i)),
                                    "Unrelated level header changed.");
                    for (unsigned i = 0; i < native->record_count(39); ++i)
                        if (i != 5)
                            require(std::ranges::equal(native->record(39, i), loaded.bank->record(39, i)),
                                    "Live audio instrument/sample bank changed.");
                    AssetBus bus;
                    require(bool(ResidentBank::prepare(native, loaded.bank, bus)),
                            "Custom track failed resident allocation validation.");
                    const auto music = inspect_sequence_directory(native->record(39, 5));
                    const auto imported = inspect_sequence_directory(loaded.bank->record(39, 5));
                    const auto selected = loaded.bank->record(23, carrier)[0x52];
                    for (unsigned i = 0; i < music.records.size(); ++i)
                        if (i != selected)
                            require(music.records[i].digest == imported.records[i].digest,
                                    "Unrelated song was overwritten.");
                    std::cout << "  track " << carrier << " " << native->revision()
                              << " prepare/persist/resident PASS\n";
                }
            }
            // Rejected executable regions stay out of all derived resources;
            // original Game Paks are read-only inputs throughout this test.
            require(sha256(source) == source_hash && sha256(other) == other_hash,
                    "Source image mutated during preparation.");
        }
        if (argc == 5) {
            const auto storage = temp.path / "library";
            ModLibrary library;
            library.configure(storage, utf8_path(argv[4]));
            wait(library);
            for (const auto& fixture : fixtures) {
                require(
                    library.import_file(utf8_path(argv[3]) / fixture.name, {utf8_path(argv[1]), utf8_path(argv[2])}),
                    "Could not dispatch importer.");
                wait(library);
                std::cout << "  isolated worker import " << fixture.name << " PASS" << std::endl;
            }
            auto catalog = library.snapshot();
            require(catalog.characters->tracks.size() == 5, "Unified library contains the wrong racer count.");
            require(catalog.tracks->tracks.size() == 18, "Unified library is missing a track revision variant.");
            std::set<std::string> ids;
            for (const auto& item : catalog.tracks->tracks)
                if (ids.insert(item.id).second) {
                    require(!item.enabled, "Import enabled itself.");
                    require(library.set_enabled(TrackCatalog::Kind::Track, item.id, true), "Track activation failed.");
                    wait(library);
                }
            require(ids.size() == 9, "Expected nine unique custom courses.");
            for (const auto& character : catalog.characters->tracks) {
                require(library.set_enabled(TrackCatalog::Kind::Character, character.id, true),
                        "Racer activation failed.");
                wait(library);
                for (const auto& native : {native0, native1}) {
                    auto chars = TrackCatalog::load_enabled_characters(storage, native);
                    require(chars.size() == 1, "Enabled racer catalogue was not portable.");
                    require(!chars.front().race_audio.control.empty(),
                            "Retained race audio was not reconstructed for this revision.");
                    validate_character_race_audio(chars.front().race_audio);
                    auto names = std::make_shared<CharacterNamespace>(allocate_characters(native, std::move(chars)));
                    RuntimeSession session(native, names);
                    auto tracks = TrackCatalog::load_enabled(storage, native);
                    require(tracks.size() == 9, "Wrong courses selected for native revision.");
                    for (auto& track : tracks)
                        session.admit(std::move(track));
                    require(session.tracks().size() == 9, "Native session admission lost a course.");
                }
                require(library.set_enabled(TrackCatalog::Kind::Character, character.id, false),
                        "Racer deactivation failed.");
                wait(library);
            }
            require(
                library.import_file(utf8_path(argv[3]) / fixtures[2].name, {utf8_path(argv[1]), utf8_path(argv[2])}),
                "Reimport dispatch failed.");
            wait(library);
            require(library.snapshot().tracks->tracks.size() == 18 && library.snapshot().characters->tracks.size() == 5,
                    "Reimport duplicated content.");
            // Beta 8 library operations against real, playable mixed content.
            const auto before = library.snapshot();
            const auto selected = std::find_if(before.tracks->tracks.begin(), before.tracks->tracks.end(),
                                               [](const auto& item) { return item.name == "Temple of Time"; });
            require(selected != before.tracks->tracks.end(), "Missing OOT removal fixture.");
            const auto removed_id = selected->id;
            require(library.set_hidden(TrackCatalog::Kind::Track, removed_id, true), "Hide dispatch failed.");
            wait(library);
            for (const auto& item : library.snapshot().tracks->tracks)
                if (item.id == removed_id)
                    require(item.hidden && !item.enabled, "Hide failed to deactivate both variants.");
            require(library.set_hidden(TrackCatalog::Kind::Track, removed_id, false), "Restore dispatch failed.");
            wait(library);
            require(library.set_enabled(TrackCatalog::Kind::Track, removed_id, true), "Reactivation failed.");
            wait(library);
            require(library.remove(TrackCatalog::Kind::Track, removed_id), "Removal dispatch failed.");
            wait(library);
            require(library.snapshot().tracks->tracks.size() == 16 && library.snapshot().characters->tracks.size() == 5,
                    "Removal affected sibling tracks or Link.");
            for (const auto& native : {native0, native1}) {
                auto tracks = TrackCatalog::load_enabled(storage, native);
                require(tracks.size() == 8, "Removal changed other activations.");
                RuntimeSession session(native);
                for (auto& track : tracks)
                    session.admit(std::move(track));
                require(session.tracks().size() == 8, "Surviving native banks failed admission.");
            }
            require(library.refresh(), "Refresh after removal failed.");
            wait(library);
            require(library.snapshot().tracks->tracks.size() == 16, "Removed card returned on refresh.");
            require(
                library.import_file(utf8_path(argv[3]) / fixtures[2].name, {utf8_path(argv[1]), utf8_path(argv[2])}),
                "Restore source import failed.");
            wait(library);
            require(library.snapshot().tracks->tracks.size() == 18 && library.snapshot().characters->tracks.size() == 5,
                    "Reimport failed to restore removed entry without duplicates.");
            // Removing it a second time covers duplicate survivor groups left
            // by intentionally re-preparing the same multi-course import.
            require(library.remove(TrackCatalog::Kind::Track, removed_id), "Repeated removal failed.");
            wait(library);
            require(library.snapshot().tracks->tracks.size() == 16, "Repeated removal retained duplicate variants.");
            const auto character = before.characters->tracks.front();
            require(library.set_enabled(TrackCatalog::Kind::Character, character.id, true),
                    "Character activation failed.");
            wait(library);
            require(library.remove(TrackCatalog::Kind::Character, character.id), "Character removal failed.");
            wait(library);
            require(library.snapshot().characters->tracks.size() == 4 && library.snapshot().tracks->tracks.size() == 16,
                    "Character removal damaged other content.");
            for (const auto& native : {native0, native1})
                require(TrackCatalog::load_enabled_characters(storage, native).empty(),
                        "Removed character still active.");
            std::cout
                << "  real mixed-pack hide/remove/recovery-safe publication/reimport and survivor admission PASS\n";
            std::cout << "  complete worker/catalogue/activation/session and reimport PASS\n";
        }
        std::cout << checks << " compatibility assertions passed across seven supplied mods and both US revisions.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
