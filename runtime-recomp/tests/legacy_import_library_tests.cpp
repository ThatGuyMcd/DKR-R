#include "legacy_import_library.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>
using namespace dkr::mods;
namespace {
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition)
        throw Error("Library assertion " + std::to_string(checks));
}
void wait(ImportLibrary& library) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(185);
    while (library.snapshot()->busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(!library.snapshot()->busy);
}
void write(const std::filesystem::path& path, const nlohmann::json& value) {
    const auto s = value.dump();
    write_new_file(path, View(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}
nlohmann::json receipt() {
    using nlohmann::json;
    const json track = {{"id", std::string(64, 'b')}, {"name", "Big Boo's Haunt"}, {"blockers", json::array()}};
    const json package = {{"enabled", false},
                          {"online_certified", false},
                          {"source_revision", "us.v77"},
                          {"asset_sha256", std::string(64, 'a')},
                          {"blockers", json::array()},
                          {"characters", json::array({"Haunter"})},
                          {"tracks", json::array({track})}};
    return {{"schema", 1},
            {"state", "awaiting-review"},
            {"runtime_activated", false},
            {"packages", json::array({package})}};
}
void store(const std::filesystem::path& root, const nlohmann::json& value) {
    const auto s = value.dump();
    const auto path = root / "reviews" / sha256(View(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
    std::filesystem::create_directory(path);
    write(path / "review.json", value);
}
} // namespace
int main(int argc, char** argv) {
    const auto root =
        std::filesystem::current_path() /
        ("legacy-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(std::filesystem::create_directory(root));
        {
            ImportLibrary library;
            library.configure(root / "library",
                              argc > 1 ? std::filesystem::absolute(argv[1]) : std::filesystem::path{});
            wait(library);
            check(library.snapshot()->succeeded);
            check(library.snapshot()->items.empty());
            check(!library.snapshot()->modal);
            const auto retained = library.snapshot();
            store(root / "library", receipt());
            check(library.refresh());
            wait(library);
            check(library.snapshot()->succeeded);
            check(library.snapshot()->items.size() == 2);
            check(retained->items.empty());
            check(library.snapshot()->items[0].name == "Big Boo's Haunt");
            check(library.import_file(root / "missing.zip", {}));
            wait(library);
            check(!library.snapshot()->succeeded);
            check(library.snapshot()->modal);
            check(library.snapshot()->items.size() == 2);
            library.dismiss();
            check(!library.snapshot()->modal);
            // A worker failure cannot publish its incomplete transaction.
            if (argc > 1) {
                check(library.import_file(root / "missing.zip", {root / "missing.z64"}));
                wait(library);
                check(!library.snapshot()->succeeded);
                check(library.snapshot()->items.size() == 2);
                check(std::filesystem::is_empty(root / "library" / "jobs"));
            }
            auto changed = receipt();
            changed["packages"][0]["enabled"] = true;
            store(root / "library", changed);
            check(library.refresh());
            wait(library);
            check(!library.snapshot()->succeeded);
            check(library.snapshot()->items.size() == 2); // Last valid UI view remains.
        }
        // Recovery never promotes interrupted jobs or parses arbitrary folders.
        {
            const auto recovery = root / "recovery";
            std::filesystem::create_directories(recovery / "jobs" / std::string(64, 'c') / "content");
            write(recovery / "jobs" / std::string(64, 'c') / "content" / "review.json", receipt());
            ImportLibrary library;
            library.configure(recovery, {});
            wait(library);
            check(library.snapshot()->items.empty());
            check(library.snapshot()->succeeded);
        }
        if (argc == 7) {
            const auto corpus = root / "corpus";
            const std::vector<std::filesystem::path> roms = {utf8_path(argv[2]), utf8_path(argv[3])};
            {
                ImportLibrary library;
                library.configure(corpus, std::filesystem::absolute(argv[1]));
                wait(library);
                for (int i = 4; i < 7; ++i) {
                    check(library.import_file(utf8_path(argv[i]), roms));
                    // There can only be one worker; a second action is rejected,
                    // not queued onto the UI/network thread.
                    check(!library.import_file(utf8_path(argv[i]), roms));
                    wait(library);
                    if (!library.snapshot()->succeeded)
                        throw Error(library.snapshot()->result);
                    check(library.snapshot()->succeeded);
                }
                check(library.snapshot()->items.size() == 8);
                check(library.import_file(utf8_path(argv[4]), roms));
                wait(library);
                check(library.snapshot()->succeeded);
                check(library.snapshot()->items.size() == 8);
                check(library.snapshot()->result.find("already") != std::string::npos);
                check(std::filesystem::is_empty(corpus / "jobs"));
                check(library.import_file(utf8_path(argv[4]), roms));
                library.cancel();
                wait(library);
                check(!library.snapshot()->succeeded);
                check(library.snapshot()->items.size() == 8);
                check(std::filesystem::is_empty(corpus / "jobs"));
            }
            ImportLibrary reopened;
            reopened.configure(corpus, std::filesystem::absolute(argv[1]));
            wait(reopened);
            check(reopened.snapshot()->succeeded);
            check(reopened.snapshot()->items.size() == 8);
        }
        std::filesystem::remove_all(root);
        std::cout << "Legacy import library: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (fixture retained at " << root << ")\n";
        return 1;
    }
}
