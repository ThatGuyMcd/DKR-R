#include "mod_store/download_history.hpp"
#include <json/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dkr::store::history;
using Json = nlohmann::json;
struct Job {
    std::string key, item, release, title, category, state, error, digest, version, intent, instance;
    std::filesystem::path file;
    std::uint64_t received = 0, total = 0;
};
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition)
        throw std::runtime_error("Mod Store history check " + std::to_string(checks) + " failed");
}
const std::string hash(64, 'a');
Json row(std::string state = "Installed") {
    return {{"item", "mod_one"},
            {"release", "release_one"},
            {"category", "characters"},
            {"title", "Test character"},
            {"state", state},
            {"sha256", hash},
            {"bytes", 100ULL},
            {"version", "1.0.0"},
            {"intent", ""},
            {"instance", ""}};
}
Json receipt() {
    return {{"schema", 1},
            {"records", Json::array({{{"item", "mod_one"},
                                      {"release", "release_one"},
                                      {"category", "characters"},
                                      {"state", "Installed"},
                                      {"stored", false},
                                      {"digest", hash},
                                      {"bytes", 100ULL},
                                      {"members", Json::array({{{"key", "character:one"},
                                                                {"kind", "legacy-character"},
                                                                {"path", "mods/character-one"}}})}}})}};
}
Json saved(const Json& job, unsigned schema = 1) {
    return {{"schema", schema}, {"jobs", Json::array({job})}};
}
auto load(const Json& data, const Json& receipts = Json::object(), bool present = true) {
    return restore<Job>(data, receipts, [present](const std::string& path) { return present && !path.empty(); });
}
struct Temp {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("dkrr-history-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() {
        std::filesystem::create_directories(path / "mods");
    }
    ~Temp() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace

int main(int argc, char** argv) try {
    auto jobs = load(saved(row()));
    check(jobs.size() == 1 && jobs[0].state == "Installed" && jobs[0].error.empty());
    check(jobs[0].digest == hash && jobs[0].received == 100 && jobs[0].total == 100);
    for (unsigned restart = 0; restart < 5; ++restart) {
        jobs = load(Json::parse(save<Json>(jobs).dump()));
        check(jobs.size() == 1 && jobs[0].state == "Installed" && jobs[0].digest == hash);
    }
    auto legacy = row("Paused");
    legacy["sha256"] = "";
    legacy["bytes"] = 0ULL;
    jobs = load(saved(legacy), receipt());
    check(jobs.size() == 1 && jobs[0].state == "Installed" && jobs[0].digest == hash && jobs[0].total == 100);
    check(load(saved(legacy), receipt(), false)[0].state == "Paused");
    check(load(saved(legacy), Json::object())[0].state == "Paused");
    check(load(saved(legacy, 2), receipt())[0].state == "Paused");
    for (const auto* field : {"item", "release", "category", "digest", "state"}) {
        auto r = receipt();
        r["records"][0][field] = "different";
        check(load(saved(legacy), r)[0].state == "Paused");
    }
    for (const auto* state : {"Missing", "Needs repair", "Stored edition"}) {
        auto r = receipt();
        r["records"][0]["state"] = state;
        check(load(saved(legacy), r)[0].state == "Paused");
    }
    auto r = receipt();
    r["records"][0]["stored"] = true;
    check(load(saved(legacy), r)[0].state == "Paused");
    r = receipt();
    r["records"][0]["members"] = Json::array();
    check(load(saved(legacy), r)[0].state == "Paused");
    r = receipt();
    r["records"][0]["members"].push_back({{"key", "second"}, {"kind", "legacy-character"}, {"path", ""}});
    check(load(saved(legacy), r)[0].state == "Paused");
    for (const auto* field : {"intent", "instance"}) {
        auto pending = legacy;
        pending[field] = "explicit-operation";
        check(load(saved(pending), receipt())[0].state == "Paused");
    }
    auto different_hash = legacy;
    different_hash["sha256"] = std::string(64, 'b');
    check(load(saved(different_hash), receipt())[0].state == "Paused");
    for (const auto* state : {"Queued", "Downloading", "Verifying", "Installing", "Preparing download",
                              "Ready to install", "Paused for gameplay", "Paused"}) {
        jobs = load(saved(row(state), 2), receipt());
        check(jobs[0].state == "Paused" && jobs[0].file.empty() && !jobs[0].error.empty());
    }
    for (const auto* state : {"Failed", "Install failed"}) {
        auto failed = row(state);
        failed["error"] = "The package could not be verified.";
        jobs = load(saved(failed), receipt());
        check(jobs[0].state == "Failed" && jobs[0].error == failed["error"].get<std::string>());
        check(load(Json::parse(save<Json>(jobs).dump()), receipt())[0].error == failed["error"].get<std::string>());
    }
    for (const auto& malformed : {Json(), Json::array(), Json("invalid"), Json{{"jobs", "invalid"}}, saved(row(), 99)})
        check(load(malformed).empty());
    for (const auto* field : {"item", "release"}) {
        auto invalid = row();
        invalid[field] = "../outside";
        check(load(saved(invalid)).empty());
    }
    auto mixed = saved(row());
    mixed["jobs"].push_back(nullptr);
    mixed["jobs"].push_back(row());
    check(load(mixed).size() == 1);
    for (const auto& size : {Json(-1), Json("100"), Json(max_package_bytes + 1)}) {
        auto invalid = row();
        invalid["bytes"] = size;
        check(load(saved(invalid))[0].total == 0);
    }
    Temp temp;
    std::ofstream(temp.path / "mods/character-one") << "fixture";
    check(member_present(temp.path, "mods/character-one"));
    check(member_present(temp.path, "mods"));
    for (const auto* path : {"", "../outside", "/outside", "C:/outside", "mods/../character-one", "mods\\character-one",
                             "mods/./character-one", "mods/missing"})
        check(!member_present(temp.path, path));
    std::error_code error;
    std::filesystem::create_symlink(temp.path / "mods/character-one", temp.path / "mods/link", error);
    if (!error)
        check(!member_present(temp.path, "mods/link"));
    jobs = restore<Job>(saved(legacy), receipt(),
                        [&](const std::string& path) { return member_present(temp.path, path); });
    check(jobs[0].state == "Installed");
    std::filesystem::remove(temp.path / "mods/character-one");
    jobs = restore<Job>(saved(legacy), receipt(),
                        [&](const std::string& path) { return member_present(temp.path, path); });
    check(jobs[0].state == "Paused");
    std::cout << checks << " Mod Store history checks passed; no network or user profile used.\n";
    if (argc == 4) {
        // Optional read-only inspection of copied records. Never save the
        // migrated result or open a launcher, account or network connection.
        std::ifstream history_file(utf8_path(argv[1])), receipt_file(utf8_path(argv[2]));
        const auto data = Json::parse(history_file), receipts = Json::parse(receipt_file);
        const auto config = utf8_path(argv[3]);
        const auto restored =
            restore<Job>(data, receipts, [&](const std::string& path) { return member_present(config, path); });
        Json counts = Json::object();
        for (const auto& job : restored) {
            if (!counts.contains(job.state))
                counts[job.state] = 0;
            counts[job.state] = counts[job.state].get<unsigned>() + 1;
        }
        std::cout << "Read-only history inspection: " << counts.dump() << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
