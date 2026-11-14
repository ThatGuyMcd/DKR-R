#include "legacy_mod_library.hpp"
#include "legacy_mod_launch.hpp"
#include "../src/game/dkr_save_codec.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <json/json.hpp>

using namespace dkr::mods;
namespace {
unsigned checks = 0;
void check(bool b) {
    ++checks;
    if (!b)
        throw Error("Unified library assertion " + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected);
}
void wait(ModLibrary& library) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
    do {
        library.tick();
        if (!library.snapshot().busy)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    throw Error("Unified import did not finish in time.");
}
void wait(ModLaunch& launch) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (launch.snapshot().busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(!launch.snapshot().busy);
}
void write(const std::filesystem::path& path, const nlohmann::json& value) {
    const auto bytes = value.dump();
    write_new_file(path, View(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
} // namespace
int main(int argc, char** argv) {
    const auto root = private_storage_path(
        std::filesystem::current_path() /
        ("unified-mod-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        check(std::filesystem::create_directory(root));
        const auto worker = argc > 1 ? std::filesystem::absolute(utf8_path(argv[1])) : std::filesystem::path{};
        {
            ModLibrary library;
            library.configure(root / "empty" / "mods" / "legacy", worker);
            wait(library);
            check(library.snapshot().succeeded);
            check(library.snapshot().tracks->tracks.empty());
            check(library.snapshot().characters->tracks.empty());
            check(library.import_file(root / "missing.zip", {}));
            check(!library.refresh());
            wait(library);
            check(!library.snapshot().succeeded);
            check(library.snapshot().modal);
            library.dismiss();
            check(!library.snapshot().modal);
            check(!library.prepare_review("bad", {}));
            check(!library.prepare_all_reviews({root / "missing.z64"}));
            check(!library.prepare_all_reviews({}));
            check(library.set_enabled(TrackCatalog::Kind::Track, std::string(64, 'f'), true));
            wait(library);
            check(!library.snapshot().succeeded);
            check(!prepare_mod_launch(root / "empty", root / "missing.z64", false)->session); // no mod -> no ROM IO
            write(root / "empty" / "mods" / "legacy" / "character-catalog.json",
                  {{"schema", 2}, {"enabled", nlohmann::json::array({"bad"})}});
            rejects([&] { prepare_mod_launch(root / "empty", {}, false); });
            check(!prepare_mod_launch(root / "empty", {}, true)->session); // online bypasses even corrupt mod metadata
            check(library.disable_all(TrackCatalog::Kind::Character));
            wait(library);
            check(library.snapshot().succeeded);
            check(!prepare_mod_launch(root / "empty", {}, false)->session); // recover without deleting content
            ModLaunch launch;
            check(launch.start(root / "empty", {}, false));
            wait(launch);
            check(launch.snapshot().prepared != nullptr);
            launch.dismiss();
            check(!launch.snapshot().modal);
            launch.report_error("test");
            check(launch.snapshot().error == "test");
            check(launch.start(root / "empty", {}, false));
            launch.cancel();
            wait(launch);
            check(!launch.snapshot().modal);
            check(!launch.snapshot().prepared);
            check(launch.start(root / "empty", {}, false));
            wait(launch);
            launch.cancel();
            check(!launch.snapshot().modal);
            check(!launch.snapshot().prepared);
        }
        if (argc >= 5) {
            const auto config = root / "profile", storage = config / "mods" / "legacy";
            const auto rom = std::filesystem::absolute(utf8_path(argv[2]));
            std::filesystem::create_directories(config / "saves");
            const auto original = dkr::runtime::saves::codec::blank_bytes();
            write_new_file(config / "saves" / "dkr.us.v77.bin", original);
            write_new_file(config / "controller-pak-1.mpk", Bytes{1, 2, 3, 4});
            const auto stock_hash = sha256(original);
            ModLibrary library;
            library.configure(storage, worker);
            wait(library);
            for (int i = 3; i < argc; ++i) {
                check(library.import_file(utf8_path(argv[i]), {rom}));
                check(!library.import_file(utf8_path(argv[i]), {rom}));
                wait(library);
                if (!library.snapshot().succeeded)
                    throw Error(library.snapshot().result);
            }
            auto snapshot = library.snapshot();
            check(!snapshot.tracks->tracks.empty());
            check(snapshot.characters->tracks.size() == 2);
            for (const auto& c : snapshot.characters->tracks)
                check(!c.enabled);
            for (const auto& t : snapshot.tracks->tracks)
                check(!t.enabled);
            for (const auto& c : snapshot.characters->tracks) {
                check(library.set_enabled(TrackCatalog::Kind::Character, c.id, true));
                wait(library);
                check(library.snapshot().succeeded);
            }
            for (const auto& t : snapshot.tracks->tracks) {
                check(library.set_enabled(TrackCatalog::Kind::Track, t.id, true));
                wait(library);
                check(library.snapshot().succeeded);
            }
            auto prepared = prepare_mod_launch(config, rom, false);
            check(prepared->session != nullptr);
            check(prepared->session->characters()->characters.size() == 2);
            check(prepared->session->tracks().size() == snapshot.tracks->tracks.size());
            check(prepared->save_path != config / "saves" / "dkr.us.v77.bin");
            check(read_file(prepared->save_path, 512) == original);
            check(!std::filesystem::exists(prepared->pak_directory / "controller-pak-1.mpk"));
            check(read_file(config / "controller-pak-1.mpk", 4) == Bytes({1, 2, 3, 4}));
            check(!prepare_mod_launch(config, {}, true)->session);
            auto again = prepare_mod_launch(config, rom, false);
            check(again->fingerprint == prepared->fingerprint);
            check(again->save_path == prepared->save_path);
            for (int i = 3; i < argc; ++i) {
                check(library.import_file(utf8_path(argv[i]), {rom}));
                wait(library);
                check(library.snapshot().succeeded);
            }
            check(library.snapshot().tracks->tracks.size() == snapshot.tracks->tracks.size());
            check(library.snapshot().characters->tracks.size() == 2);
            check(prepare_mod_launch(config, rom, false)->fingerprint == prepared->fingerprint);
            // Bulk refresh visits each retained source once, leaves active
            // editions/save routes intact, and cannot overlap other work.
            check(library.prepare_all_reviews({rom}));
            check(library.snapshot().imports_total >= 2);
            check(!library.prepare_all_reviews({rom}));
            check(!library.refresh());
            wait(library);
            check(library.snapshot().succeeded);
            check(library.snapshot().result.find("source imports") != std::string::npos);
            check(prepare_mod_launch(config, rom, false)->fingerprint == prepared->fingerprint);
            check(library.prepare_all_reviews({rom}));
            library.cancel();
            wait(library);
            check(!library.snapshot().succeeded);
            check(prepare_mod_launch(config, rom, false)->fingerprint == prepared->fingerprint);
            // Missing ROMs fail independently; every source is accounted for,
            // without deleting or deactivating already prepared content.
            check(library.prepare_all_reviews({root / "missing.z64"}));
            wait(library);
            check(!library.snapshot().succeeded);
            check(library.snapshot().result.find("needs attention") != std::string::npos);
            check(prepare_mod_launch(config, rom, false)->fingerprint == prepared->fingerprint);
            // A failed/cancelled worker never changes enablement or stock saves.
            check(library.import_file(root / "missing.xdelta", {rom}));
            library.cancel();
            wait(library);
            check(!library.snapshot().succeeded);
            check(prepare_mod_launch(config, rom, false)->fingerprint == prepared->fingerprint);
            check(library.set_enabled(TrackCatalog::Kind::Character, snapshot.characters->tracks.front().id, false));
            wait(library);
            check(prepare_mod_launch(config, rom, false)->fingerprint != prepared->fingerprint);
            check(sha256(read_file(config / "saves" / "dkr.us.v77.bin", 512)) == stock_hash);
            const auto item = snapshot.tracks->tracks.front();
            const auto artifact = storage / "prepared" / item.group / item.id / "track.json";
            std::filesystem::rename(artifact, root / "retained-manifest");
            rejects([&] { prepare_mod_launch(config, rom, false); });
            check(library.disable_all(TrackCatalog::Kind::Track));
            wait(library); // scan may still report damaged inactive content
            check(library.disable_all(TrackCatalog::Kind::Character));
            wait(library);
            check(!prepare_mod_launch(config, {}, false)->session);
            std::filesystem::rename(root / "retained-manifest", artifact);
            // Optional explicit fixture export for private runtime qualification;
            // never part of packaged binaries or automatic CTest execution.
            if (const char* destination = std::getenv("DKR_TEST_EXPORT_MOD_PROFILE")) {
                const auto target = private_storage_path(utf8_path(destination));
                if (std::filesystem::exists(target))
                    throw Error("Refusing to overwrite exported test profile.");
                std::filesystem::create_directories(target.parent_path());
                for (const auto& c : snapshot.characters->tracks) {
                    library.set_enabled(TrackCatalog::Kind::Character, c.id, true);
                    wait(library);
                }
                for (const auto& t : snapshot.tracks->tracks) {
                    library.set_enabled(TrackCatalog::Kind::Track, t.id, true);
                    wait(library);
                }
                std::filesystem::copy(config, target, std::filesystem::copy_options::recursive);
            }
        }
        std::filesystem::remove_all(root);
        std::cout << "Unified mod library and launch: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixture retained at " << root << ")\n";
        return 1;
    }
}
