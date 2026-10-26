#include "legacy_track_catalog.hpp"
#include "legacy_import_library.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

namespace {
using namespace dkr::mods;
unsigned checks = 0;
void check(bool value) {
    ++checks;
    if (!value)
        throw Error("Track catalogue assertion " + std::to_string(checks));
}
template <class F> void rejects(F&& f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected);
}
template <class Library> void wait(Library& library) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(185);
    while (library.snapshot()->busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(!library.snapshot()->busy);
}
template <class Library> void success(Library& library) {
    wait(library);
    if (!library.snapshot()->succeeded)
        throw Error(library.snapshot()->result);
    check(library.snapshot()->succeeded);
}
} // namespace
int main(int argc, char** argv) {
    using namespace dkr::mods;
    const auto root = private_storage_path(
        std::filesystem::current_path() /
        ("track-catalog-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        check(std::filesystem::create_directory(root));
        const auto worker = argc > 1 ? std::filesystem::absolute(utf8_path(argv[1])) : std::filesystem::path{};
        {
            TrackCatalog catalog;
            catalog.configure(root / "library", worker);
            success(catalog);
            check(catalog.snapshot()->tracks.empty());
            check(!catalog.snapshot()->modal);
            const auto original = catalog.snapshot();
            check(catalog.set_enabled(std::string(64, 'a'), true));
            wait(catalog);
            check(!catalog.snapshot()->succeeded);
            check(catalog.snapshot()->tracks.empty());
            check(original->succeeded);
            catalog.dismiss();
            check(!catalog.snapshot()->modal);
            check(catalog.prepare_review("not-an-id", {}));
            wait(catalog);
            check(!catalog.snapshot()->succeeded);
            check(std::filesystem::is_empty(root / "library" / "prepare-jobs"));
        }
        if (argc > 3) {
            const auto library_root = root / "corpus";
            const std::vector<std::filesystem::path> roms{utf8_path(argv[2])};
            auto rom = read_file(roms.front(), MaxImage);
            canonicalize_rom(rom);
            const auto stock = AssetBank::stock(rom);
            {
                ImportLibrary imports;
                imports.configure(library_root, worker);
                success(imports);
                TrackCatalog catalog;
                catalog.configure(library_root, worker);
                success(catalog);
                for (int i = 3; i < argc; ++i) {
                    check(imports.import_file(utf8_path(argv[i]), roms));
                    success(imports);
                }
                for (const auto& review : std::filesystem::directory_iterator(library_root / "reviews")) {
                    check(catalog.prepare_review(review.path().filename().string(), roms));
                    check(!catalog.refresh());
                    success(catalog);
                }
                const auto prepared = catalog.snapshot();
                check(!prepared->tracks.empty());
                check(TrackCatalog::load_enabled(library_root, stock).empty());
                std::set<std::string> ids;
                for (const auto& item : prepared->tracks) {
                    check(!item.enabled);
                    check(ids.insert(item.id).second);
                    check(catalog.set_enabled(item.id, true));
                    success(catalog);
                }
                check(TrackCatalog::load_enabled(library_root, stock).size() == ids.size());
                const auto active = catalog.snapshot();
                for (const auto& item : active->tracks)
                    check(item.enabled);
                const auto first = *std::filesystem::directory_iterator(library_root / "reviews");
                check(catalog.prepare_review(first.path().filename().string(), roms));
                success(catalog);
                check(catalog.snapshot()->tracks.size() == prepared->tracks.size());
                check(TrackCatalog::load_enabled(library_root, stock).size() == ids.size());
                const auto& item = active->tracks.front();
                const auto folder = library_root / "prepared" / item.group / item.id;
                const auto artifact = read_track_artifact(folder, item.artifact, stock);
                const auto header = folder / "blobs" / sha256(artifact.bank->record(23, item.carrier));
                const auto missing = root / "retained-header";
                std::filesystem::rename(header, missing);
                rejects([&] { TrackCatalog::load_enabled(library_root, stock); });
                std::filesystem::rename(missing, header);
                for (const auto& id : ids) {
                    check(catalog.set_enabled(id, false));
                    success(catalog);
                }
                check(TrackCatalog::load_enabled(library_root, stock).empty());
                check(catalog.set_enabled(*ids.begin(), true));
                success(catalog);
                check(std::filesystem::is_empty(library_root / "prepare-jobs"));
            }
            TrackCatalog reopened;
            reopened.configure(library_root, worker);
            success(reopened);
            check(TrackCatalog::load_enabled(library_root, stock).size() == 1);
        }
        std::filesystem::remove_all(root);
        std::cout << checks << " track catalogue persistence, activation, worker and integrity checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "; private fixture retained at " << root << '\n';
        return 1;
    }
}
