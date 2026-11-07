#include "storage/maintenance.hpp"
#include "updates/update_core.hpp"
#include "updates/update_process.hpp"
#include <json/json.hpp>
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace dkr::storage;
namespace {
fs::path io(fs::path p) {
#ifdef _WIN32
    p = fs::absolute(p).make_preferred();
    const auto s = p.native();
    if (!s.starts_with(L"\\\\?\\"))
        p = L"\\\\?\\" + s;
#endif
    return p;
}
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("dkr-storage-test-" + dkr::updates::unique_id());
    Fixture() {
        fs::create_directory(root);
    }
    ~Fixture() {
        shutdown();
        assert(root.filename().string().starts_with("dkr-storage-test-") &&
               fs::canonical(root).parent_path() == fs::canonical(fs::temp_directory_path()));
        fs::remove_all(io(root));
    }
    fs::path put(const std::string& relative, const std::string& bytes = "fixture") {
        const auto p = io(root / fs::u8path(relative));
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << bytes;
        return p;
    }
    void json(const std::string& relative, const Json& j) {
        put(relative, j.dump());
    }
};
std::string hash(const std::string& s) {
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}
void age(const fs::path& p, unsigned days = 40) {
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(24ULL * days));
}
Json record(const std::string& digest, const std::string& path, const std::string& file = "",
            const std::string& content = "fixture") {
    return {{"digest", digest},
            {"state", "Installed"},
            {"stored", false},
            {"members",
             Json::array({{{"kind", "texture"}, {"path", path}, {"key", "fixture"}, {"content", hash(content)}}})},
            {"file", file}};
}
void receipts(Fixture& f, const Json& rows) {
    f.json("mod-store/installations.json", {{"schema", 1}, {"records", rows}});
}
void installed_download_and_preview() {
    Fixture f;
    const auto h = hash("archive");
    f.put("texture-packs/example.rtz", "installed");
    const auto file = "mod-store/downloads/" + h + "/package.zip";
    f.put(file, "archive");
    receipts(f, Json::array({record(h, "texture-packs/example.rtz", file, "installed")}));
    f.put("saves/dkr.us.v77.bin");
    f.put("rom-cache/us.v77.z64");
    f.put("texture-packs/installed.zip");
    f.put("texture-dumps/Working/edited.png");
    f.put("mod-store/device.bin");
    Options options;
    options.action = Action::Preview;
    const auto preview = maintain(f.root, options);
    if (preview.removed_bytes != 7)
        std::cerr << "Preview: " << preview.status << " bytes=" << preview.removed_bytes
                  << " files=" << preview.removed_files << "\n";
    assert(preview.preview && preview.removed_bytes == 7 && fs::exists(f.root / file));
    options.action = Action::Clean;
    const auto cleaned = maintain(f.root, options);
    assert(cleaned.removed_bytes == 7 && !fs::exists(f.root / file));
    for (const auto* p : {"saves/dkr.us.v77.bin", "rom-cache/us.v77.z64", "texture-packs/installed.zip",
                          "texture-dumps/Working/edited.png", "mod-store/device.bin"})
        assert(fs::exists(f.root / p));
    assert(maintain(f.root, options).removed_files == 0);
}
void protected_editions_and_pending() {
    Fixture f;
    const auto h = hash("protected");
    const auto folder = "mod-store/downloads/" + h;
    const auto keep = folder + "/keep.zip";
    f.put(keep, "protected");
    f.put(folder + "/package.zip", "protected");
    f.put("texture-packs/a.rtz");
    auto r = record(h, "texture-packs/a.rtz", keep);
    r["previous"] = record(h, "texture-packs/a.rtz", keep);
    receipts(f, Json::array({r}));
    maintain(f.root);
    assert(fs::exists(f.root / keep) && !fs::exists(f.root / folder / "package.zip"));
    assert(!cached_package(f.root / folder, h, 9).empty());
    assert(cached_package(f.root / folder, h, 10).empty());
    f.put(folder + "/download.part");
    age(f.root / folder / "download.part");
    f.json("mod-store/downloads.json", {{"jobs", Json::array({{{"sha256", h}, {"state", "Paused"}}})}});
    maintain(f.root);
    assert(fs::exists(f.root / folder / "download.part"));
}
void transaction_compaction_and_recovery() {
    Fixture f;
    const auto id = std::string(32, 'a'), old = std::string(32, 'b'), open = std::string(32, 'c');
    const auto source = f.put("mod-store/transactions/" + id + "/payload/pack.rtz", "pixels");
    f.put("texture-packs/a.rtz", "pixels");
    f.json("mod-store/transactions/" + id + "/journal.json", {{"schema", 2}, {"id", id}, {"phase", "committed"}});
    auto r = record(hash("zip"), "texture-packs/a.rtz");
    r["transaction"] = id;
    r["prepared"] = {{"outputs", Json::array({{{"source", dkr::updates::path_utf8(source)},
                                               {"target", "texture-packs/a.rtz"},
                                               {"content", hash("pixels")}}})}};
    receipts(f, Json::array({r}));
    f.json("mod-store/transactions/" + old + "/journal.json", {{"id", old}, {"phase", "committed"}});
    age(f.root / "mod-store/transactions" / old);
    f.json("mod-store/transactions/" + open + "/journal.json", {{"id", open}, {"phase", "publishing"}});
    age(f.root / "mod-store/transactions" / open);
    maintain(f.root);
    assert(fs::exists(source) && fs::exists(f.root / "mod-store/transactions" / old));
    f.json("mod-store/transactions/" + open + "/journal.json", {{"id", open}, {"phase", "rolled-back"}});
    age(f.root / "mod-store/transactions" / open);
    const auto result = maintain(f.root);
    assert(result.removed_files >= 2 && !fs::exists(source) && !fs::exists(f.root / "mod-store/transactions" / old));
    assert(fs::exists(f.root / "mod-store/transactions" / id / "journal.json") &&
           !fs::exists(f.root / "mod-store/transactions" / open));
    // A locally edited installation must retain its original preparation copy.
    f.put("mod-store/transactions/" + id + "/payload/pack.rtz", "pixels");
    f.put("texture-packs/a.rtz", "edited");
    maintain(f.root);
    assert(fs::exists(source));
    f.put("texture-packs/a.rtz", "pixels");
    f.put("mod-store/transactions/" + id + "/payload/pack.rtz", "edited source");
    maintain(f.root);
    assert(fs::exists(source));
}
void malformed_and_linked_data() {
    Fixture f;
    const auto h = hash("archive");
    const auto file = "mod-store/downloads/" + h + "/p.zip";
    f.put(file, "archive");
    receipts(f, Json::array({record(h, "../saves/dkr.us.v77.bin", file)}));
    maintain(f.root);
    assert(fs::exists(f.root / file));
    const auto outside = f.put("outside/precious.bin", "precious");
    fs::create_directories(f.root / "mod-store/images");
    std::error_code error;
    fs::create_symlink(outside, f.root / "mod-store/images" / std::string(64, 'd'), error);
    if (!error) {
        Options o;
        o.action = Action::ClearCaches;
        maintain(f.root, o);
        assert(fs::exists(outside));
    }
    const auto hard = f.put("mod-store/images/" + std::string(64, 'e'), "shared");
    fs::create_hard_link(hard, f.root / "outside/hard.bin", error);
    if (!error) {
        Options o;
        o.action = Action::ClearCaches;
        maintain(f.root, o);
        assert(fs::exists(hard) && fs::exists(f.root / "outside/hard.bin"));
    }
}
void budgets_and_staging() {
    Fixture f;
    const auto oldest = f.put("mod-store/images/" + std::string(64, '1'), "12345678"),
               newest = f.put("mod-store/images/" + std::string(64, '2'), "12345678");
    age(oldest, 2);
    Options o;
    o.image_budget = 8;
    maintain(f.root, o);
    assert(!fs::exists(oldest) && fs::exists(newest));
    const auto old = f.put("texture-packs/abandoned.rice.importing/a.png"),
               live = f.put("texture-packs/new.rice.importing/a.png");
    age(old.parent_path(), 8);
    f.put("texture-packs/manual-work/a.png");
    maintain(f.root);
    assert(!fs::exists(old) && fs::exists(live) && fs::exists(f.root / "texture-packs/manual-work/a.png"));
    const auto payload = std::string(64, '3'), profile = std::string(64, '4');
    const auto content = f.put("online-mods/prepared-payloads/" + payload + "/a.bin");
    age(content.parent_path(), 50);
    f.json("online-mods/source-profiles/" + profile + "/sources.json",
           {{"legacy", Json::array({{{"library", dkr::updates::path_utf8(content.parent_path())}}})}});
    maintain(f.root);
    assert(fs::exists(content));
    age(f.root / "online-mods/source-profiles" / profile, 40);
    maintain(f.root);
    assert(!fs::exists(content));
}
void completed_update_only() {
    Fixture f;
    const auto id = std::string(32, 'a'), h = std::string(64, 'b');
    f.put("updates/download-" + h + "/update.AppImage");
    f.json("updates/handoff-" + id + "/manifest.json", {{"artifacts", Json::array({{{"sha256", h}}})}});
    f.json("updates/handoff-" + id + "/result.json", {{"state", "failed"}});
    maintain(f.root);
    assert(fs::exists(f.root / "updates" / ("download-" + h)));
    f.json("updates/handoff-" + id + "/result.json", {{"state", "complete"}});
    maintain(f.root);
    assert(!fs::exists(f.root / "updates" / ("download-" + h)));
    assert(fs::exists(f.root / "updates" / ("handoff-" + id) / "result.json"));
}
void download_expiry_and_missing_registry() {
    Fixture f;
    const auto good = hash("completed"), bad = hash("failed");
    const auto completed = f.put("mod-store/downloads/" + good + "/package.zip", "completed");
    const auto failed = f.put("mod-store/downloads/" + bad + "/package.zip", "failed");
    age(completed.parent_path());
    age(failed.parent_path(), 8);
    f.json(
        "mod-store/downloads.json",
        {{"jobs", Json::array({{{"sha256", good}, {"state", "Installed"}}, {{"sha256", bad}, {"state", "Failed"}}})}});
    const auto id = std::string(32, '9');
    f.json("mod-store/transactions/" + id + "/journal.json", {{"id", id}, {"phase", "committed"}});
    maintain(f.root);
    assert(fs::exists(completed) && fs::exists(failed));
    receipts(f, Json::array());
    maintain(f.root);
    assert(!fs::exists(completed) && !fs::exists(failed));
}
void appimage_recovery_backups() {
    Fixture f;
    const auto install = f.root / "actual-install";
    fs::create_directory(install);
    const auto first = std::string(32, '1'), second = std::string(32, '2');
    for (const auto& id : {first, second}) {
        const auto backup = f.put("actual-install/.dkr-update-backup-" + id, "old-version");
        const auto target = install / "DKR-R.AppImage";
        const auto job = "updates/handoff-" + id + "/";
        f.json(job + "result.json", {{"state", "complete"}});
        f.json(job + "request.json", {{"kind", "appimage"}, {"target", dkr::updates::path_utf8(target)}});
        f.json(job + "single-journal.json", {{"old_hash", hash("old-version")},
                                             {"backup", dkr::updates::path_utf8(backup)},
                                             {"target", dkr::updates::path_utf8(target)}});
    }
    age(f.root / ("updates/handoff-" + first + "/result.json"), 2);
    maintain(f.root, {}, install);
    assert(!fs::exists(install / (".dkr-update-backup-" + first)));
    assert(fs::exists(install / (".dkr-update-backup-" + second)));
    for (const auto& name : {"0", "1", "2"}) {
        f.put(std::string("actual-install/.dkr-update-backups/") + name + "/backup/DKR-R.exe",
              name == std::string("0") ? "edited" : "previous");
        f.json(std::string("actual-install/.dkr-update-backups/") + name + "/journal.json",
               {{"state", "committed"},
                {"files", Json::array({{{"old", true}, {"path", "DKR-R.exe"}, {"old_hash", hash("previous")}}})}});
    }
    age(install / ".dkr-update-backups/0", 3);
    age(install / ".dkr-update-backups/1", 2);
    maintain(f.root, {}, install);
    assert(fs::exists(install / ".dkr-update-backups/0/backup/DKR-R.exe"));
    assert(!fs::exists(install / ".dkr-update-backups/1"));
    assert(fs::exists(install / ".dkr-update-backups/2/backup/DKR-R.exe"));
}
void no_cleanup_during_publication() {
    Fixture f;
    const auto p = f.put("mod-store/images/" + std::string(64, '1'));
    age(p);
    {
        Activity claim;
        const auto blocked = std::async(std::launch::async, [&] { return maintain(f.root); }).get();
        assert(blocked.deferred && fs::exists(p));
    }
    maintain(f.root);
    assert(!fs::exists(p));
    std::stop_source stop;
    stop.request_stop();
    Options o;
    o.stop = stop.get_token();
    const auto q = f.put("mod-store/images/" + std::string(64, '2'));
    age(q);
    const auto cancelled = maintain(f.root, o);
    assert(cancelled.deferred);
    assert(fs::exists(q));
}
void interrupted_cleanup_and_long_paths() {
    Fixture f;
    const auto id = std::string(32, 'e');
    f.json("storage/retired/" + id + "/retirement.json",
           {{"schema", 1}, {"owner", "DKR-R storage"}, {"source", "mod-store/images/old"}});
    f.put("storage/retired/" + id + "/payload/remaining.bin");
    f.put("storage/retired/unknown/payload/user-work.bin");
    const auto p =
        f.put("texture-packs/" + std::string(120, 'x') + ".rice.importing/" + std::string(120, 'y') + ".png");
    age(p.parent_path(), 8);
    const auto result = maintain(f.root);
    assert(result.removed_files >= 2 && !fs::exists(p));
    assert(fs::exists(f.root / "storage/retired/unknown/payload/user-work.bin"));
    f.put("crash-dumps/DKR-R-crash-20200101-010101.dmp");
    age(f.root / "crash-dumps/DKR-R-crash-20200101-010101.dmp");
    f.put("crash-reports/queue/unsent.zip");
    maintain(f.root);
    assert(fs::exists(f.root / "crash-dumps/DKR-R-crash-20200101-010101.dmp"));
    Options o;
    o.action = Action::OldDumps;
    maintain(f.root, o);
    assert(!fs::exists(f.root / "crash-dumps/DKR-R-crash-20200101-010101.dmp") &&
           fs::exists(f.root / "crash-reports/queue/unsent.zip"));
}
bool wait_for(const std::function<bool()>& ready, unsigned ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!ready() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return ready();
}
void service_process_and_gameplay(const fs::path& exe) {
    Fixture f;
    f.json("storage/preferences.json", {{"automatic", false}});
    const auto p = f.put("mod-store/images/" + std::string(64, '1'));
    age(p);
    dkr::updates::Child other(exe, {"--hold", dkr::updates::path_utf8(f.root)}, f.root);
    assert(wait_for([&] { return fs::exists(f.root / "child-ready"); }));
    start(f.root);
    request(Action::Clean);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(fs::exists(p));
    f.put("child-release");
    assert(other.wait(5000) == 0);
    gameplay(true);
    request(Action::Clean);
    assert(wait_for([&] { return !fs::exists(p); }, 8000));
    automatic(false);
    shutdown();
    start(f.root);
    assert(!automatic());
    shutdown();
}
void startup_once_and_gameplay() {
    Fixture f;
    const auto p = f.put("mod-store/images/" + std::string(64, '7'));
    age(p);
    start(f.root);
    gameplay(true);
    assert(wait_for([&] { return !fs::exists(p) && !view().busy; }, 15000));
    const auto kept = f.put("mod-store/images/" + std::string(64, '8'));
    age(kept);
    const auto after = view().removed_files;
    for (unsigned i = 0; i < 128; ++i) {
        request(Action::Images);
        request(Action::Automatic);
    }
    automatic(false);
    automatic(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(fs::exists(kept) && view().removed_files == after && !view().busy);
    shutdown();
    start(f.root);
    gameplay(true);
    assert(wait_for([&] { return !fs::exists(kept) && !view().busy; }, 15000));
    shutdown();
}
void online_files_stay_pinned() {
    Fixture f;
    const auto image = f.put("mod-store/images/" + std::string(64, '1'));
    const auto payload = f.put("online-mods/payloads/" + std::string(64, '2'));
    const auto job = f.put("online-mods/jobs/" + std::string(64, '3') + "/request.json", "{}");
    age(image);
    age(payload);
    age(job.parent_path());
    {
        OnlineActivity online;
        const auto result = std::async(std::launch::async, [&] { return maintain(f.root); }).get();
        assert(!result.deferred && !fs::exists(image) && fs::exists(payload) && fs::exists(job));
    }
    maintain(f.root);
    assert(!fs::exists(payload) && !fs::exists(job));
}
void chunked_content_verification() {
    Fixture f;
    const std::string data(131173, '\x91');
    const auto h = hash(data);
    const auto file = "mod-store/downloads/" + h + "/package.zip";
    f.put(file, data);
    f.put("texture-packs/verified.rtz", data);
    receipts(f, Json::array({record(h, "texture-packs/verified.rtz", file, data)}));
    Options o;
    o.background = true;
    std::atomic<bool> playing = true;
    o.gameplay = &playing;
    unsigned phases = 0;
    o.progress = [&](const View&) { ++phases; };
    const auto result = maintain(f.root, o);
    assert(result.removed_bytes == data.size() && !fs::exists(f.root / file) && phases >= 5);
}
void immutable_history_ownership() {
    Fixture f;
    const auto a = hash("installed-code"), b = hash("retired-code");
    f.json("mods/custom-magic-codes/library.json",
           {{"schema", 2}, {"items", {{a, Json::object()}}}, {"selected", Json::object()}});
    const auto current = f.put("mods/custom-magic-codes/packages/" + a + ".dkrcode", "installed-code");
    const auto prior = f.put("mods/custom-magic-codes/packages/" + b + ".dkrcode", "retired-code");
    age(current);
    age(prior);
    const Json history = {{"schema", 2}, {"items", {{b, Json::object()}}}, {"selected", Json::object()}};
    f.json("mods/custom-magic-codes/history/" + hash(history.dump()) + ".json", history);
    const auto edited =
        f.put("mods/custom-magic-codes/packages/" + std::string(64, 'f') + ".dkrcode", "locally-edited");
    age(edited);
    maintain(f.root);
    assert(fs::exists(current) && fs::exists(prior) && fs::exists(edited));
    Options o;
    o.action = Action::TrimHistory;
    maintain(f.root, o);
    assert(fs::exists(current) && !fs::exists(prior) && fs::exists(edited));
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--hold") {
        const auto root = fs::u8path(argv[2]);
        assert(root.filename().string().starts_with("dkr-storage-test-"));
        start(root);
        std::ofstream(root / "child-ready") << "ready";
        const bool released = wait_for([&] { return fs::exists(root / "child-release"); }, 15000);
        shutdown();
        return released ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "--preview") {
        Options o;
        o.action = Action::Preview;
        const auto report = maintain(fs::u8path(argv[2]), o);
        std::cout << Json{{"status", report.status},
                          {"reclaimable_bytes", report.removed_bytes},
                          {"files", report.removed_files},
                          {"preview", report.preview}}
                         .dump()
                  << "\n";
        return 0;
    }
    installed_download_and_preview();
    protected_editions_and_pending();
    transaction_compaction_and_recovery();
    malformed_and_linked_data();
    budgets_and_staging();
    completed_update_only();
    download_expiry_and_missing_registry();
    appimage_recovery_backups();
    no_cleanup_during_publication();
    interrupted_cleanup_and_long_paths();
    immutable_history_ownership();
    service_process_and_gameplay(fs::absolute(argv[0]));
    online_files_stay_pinned();
    chunked_content_verification();
    startup_once_and_gameplay();
    std::cout << "Storage: committed installs, edition pins, dry run, interrupted recovery, links, budgets, "
                 "dependencies, updates and active writers passed.\n";
}
