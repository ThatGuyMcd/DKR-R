#include "legacy_track_catalog.hpp"
#include "legacy_import_library.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace dkr::mods;
namespace {
unsigned checks = 0;
void check(bool b) {
    ++checks;
    if (!b)
        throw Error("Character library assertion " + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception&) {
        caught = true;
    }
    check(caught);
}
template <class T> void wait(T& t, bool success = true) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (t.snapshot()->busy && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(!t.snapshot()->busy);
    if (success && !t.snapshot()->succeeded)
        throw Error(t.snapshot()->result);
}
} // namespace
int main(int argc, char** argv) {
    const auto root = private_storage_path(
        std::filesystem::current_path() /
        ("character-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        check(std::filesystem::create_directory(root));
        const auto worker = argc > 1 ? std::filesystem::absolute(utf8_path(argv[1])) : std::filesystem::path{};
        {
            TrackCatalog catalog(TrackCatalog::Kind::Character);
            catalog.configure(root / "library", worker);
            wait(catalog);
            check(catalog.snapshot()->tracks.empty());
            check(catalog.set_enabled(std::string(64, 'f'), true));
            wait(catalog, false);
            check(!catalog.snapshot()->succeeded);
            check(catalog.prepare_review("bad", {}));
            wait(catalog, false);
            check(!catalog.snapshot()->succeeded);
        }
        if (argc > 3) {
            const auto library = root / "corpus";
            const auto rom = read_file(utf8_path(argv[2]), MaxImage);
            const auto stock = AssetBank::stock(rom);
            const std::vector<std::filesystem::path> roms{utf8_path(argv[2])};
            {
                ImportLibrary imports;
                imports.configure(library, worker);
                wait(imports);
                TrackCatalog catalog(TrackCatalog::Kind::Character);
                catalog.configure(library, worker);
                wait(catalog);
                for (int i = 3; i < argc; ++i) {
                    check(imports.import_file(utf8_path(argv[i]), roms));
                    wait(imports);
                }
                for (const auto& review : std::filesystem::directory_iterator(library / "reviews")) {
                    check(catalog.prepare_review(review.path().filename().string(), roms));
                    check(!catalog.refresh());
                    wait(catalog);
                }
                const auto prepared = catalog.snapshot();
                check(prepared->tracks.size() == 2);
                check(TrackCatalog::load_enabled_characters(library, stock).empty());
                for (const auto& item : prepared->tracks) {
                    check(!item.enabled);
                    check(catalog.set_enabled(item.id, true));
                    wait(catalog);
                }
                auto loaded = TrackCatalog::load_enabled_characters(library, stock);
                check(loaded.size() == 2);
                for (const auto& character : loaded) {
                    check(!character.race_audio.control.empty());
                    validate_character_race_audio(character.race_audio);
                    check(character.root.content_id.size() == 64);
                }
                const auto namespace_before = allocate_characters(stock, loaded).apply(stock)->fingerprint();
                for (const auto& item : prepared->tracks) {
                    const auto folder = library / "prepared-characters" / item.group / item.id;
                    auto c = read_character_artifact(folder, item.artifact, *stock);
                    check(c.root.content_id == item.id);
                    check(!c.runtime_certified);
                    auto forged = c;
                    forged.root.base_character = (c.root.base_character + 1) % 10;
                    rejects([&] { validate_prepared_character(forged, *stock); });
                    forged = c;
                    forged.records[{4, 30000}] = {1};
                    rejects([&] { validate_prepared_character(forged, *stock); });
                    forged = c;
                    forged.model_animations.clear();
                    rejects([&] { validate_prepared_character(forged, *stock); });
                    const auto dependency = folder / "blobs" / sha256(c.records.begin()->second);
                    const auto retained = root / "retained-dependency";
                    std::filesystem::rename(dependency, retained);
                    rejects([&] { TrackCatalog::load_enabled_characters(library, stock); });
                    std::filesystem::rename(retained, dependency);
                }
                for (const auto& review : std::filesystem::directory_iterator(library / "reviews")) {
                    check(catalog.prepare_review(review.path().filename().string(), roms));
                    wait(catalog);
                }
                check(catalog.snapshot()->tracks.size() == 2);
                check(allocate_characters(stock, TrackCatalog::load_enabled_characters(library, stock))
                          .apply(stock)
                          ->fingerprint() == namespace_before);
                check(TrackCatalog::load_enabled(library, stock).empty()); // Independent track activation catalogue.
                check(!std::filesystem::exists(library / "track-catalog.json"));
                for (const auto& item : prepared->tracks) {
                    check(catalog.set_enabled(item.id, false));
                    wait(catalog);
                }
                check(TrackCatalog::load_enabled_characters(library, stock).empty());
                check(catalog.set_enabled(prepared->tracks.front().id, true));
                wait(catalog);
            }
            TrackCatalog reopened(TrackCatalog::Kind::Character);
            reopened.configure(library, worker);
            wait(reopened);
            check(TrackCatalog::load_enabled_characters(library, stock).size() == 1);
        }
        // Only this uniquely created, resolved test leaf is removed.
        std::filesystem::remove_all(root);
        std::cout << checks << " character library/artifact/worker checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "; retained private fixture: " << root << '\n';
        return 1;
    }
}
