#include "update_service.hpp"
#include "update_trust.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <json/json.hpp>
#include <monocypher-ed25519.h>

using namespace dkr::updates;
using Json = nlohmann::json;
namespace {
void idle(Service& service) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (service.snapshot().busy) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
struct Feed {
    std::string manifest, signature, key, package = "example-appimage";
    Json release;
    std::atomic<unsigned> calls = 0;
    bool blocked = false, rate = false, corrupt = false, manual = false, beta_list = false;
    Feed() {
        std::array<std::uint8_t, 32> seed{}, pub{};
        std::array<std::uint8_t, 64> secret{}, sig{};
        seed[0] = 79;
        crypto_ed25519_key_pair(secret.data(), pub.data(), seed.data());
        key = hex(pub);
        auto sha = hash(std::span(reinterpret_cast<const std::uint8_t*>(package.data()), package.size()));
        Json value = {{"schema", 1},
                      {"minimum_updater", 1},
                      {"version", "1.0.7"},
                      {"tag", "Version1.0.7"},
                      {"channel", "stable"},
                      {"notes", "https://github.com/ThatGuyMcd/DKR-R/releases/tag/Version1.0.7"},
                      {"commit", std::string(40, 'a')},
                      {"artifacts", Json::array({{{"platform", "linux-x86_64"},
                                                  {"name", "example.AppImage"},
                                                  {"kind", "appimage"},
                                                  {"bytes", package.size()},
                                                  {"sha256", sha},
                                                  {"files", Json::array()}}})}};
        manifest = value.dump();
        crypto_ed25519_sign(sig.data(), secret.data(), reinterpret_cast<const std::uint8_t*>(manifest.data()),
                            manifest.size());
        signature = hex(sig);
        release = {{"tag_name", "Version1.0.7"},
                   {"draft", false},
                   {"prerelease", false},
                   {"html_url", value["notes"]},
                   {"assets", Json::array({{{"name", "DKR-R-update.json"},
                                            {"browser_download_url", "https://github.com/ThatGuyMcd/DKR-R/releases/"
                                                                     "download/Version1.0.7/DKR-R-update.json"}},
                                           {{"name", "DKR-R-update.sig"},
                                            {"browser_download_url", "https://github.com/ThatGuyMcd/DKR-R/releases/"
                                                                     "download/Version1.0.7/DKR-R-update.sig"}}})}};
    }
    HttpResponse get(const HttpRequest& r, std::stop_token stop, const Progress& progress) {
        ++calls;
        while (blocked && !stop.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (stop.stop_requested())
            throw std::runtime_error("paused");
        HttpResponse response;
        response.status = 200;
        if (rate) {
            response.status = 429;
            response.headers["retry-after"] = "3600";
            return response;
        }
        if (r.url.find("api.github.com") != r.url.npos) {
            auto row = release;
            if (manual)
                row["assets"] = Json::array();
            if (beta_list && r.url.find("per_page") != r.url.npos) {
                auto older = row;
                older["tag_name"] = "Version1.0.6-beta.99";
                older["prerelease"] = true;
                response.body = Json::array({older}).dump();
            } else
                response.body = row.dump();
            return response;
        }
        if (r.url.ends_with("DKR-R-update.json")) {
            response.body = manifest + (corrupt ? " " : "");
            return response;
        }
        if (r.url.ends_with("DKR-R-update.sig")) {
            response.body = signature;
            return response;
        }
        assert(r.url.ends_with("example.AppImage"));
        HttpSink sink(r, stop, progress);
        sink.response.status = 200;
        sink.header("etag", "\"package-1\"");
        assert(sink.write(std::span(reinterpret_cast<const std::uint8_t*>(package.data()), package.size())));
        sink.finish();
        return sink.response;
    }
};
} // namespace
int main(int argc, char** argv) {
    auto root = std::filesystem::temp_directory_path() /
                std::filesystem::path("dkrr-update-service-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::filesystem::remove_all(p);
        }
    } cleanup{root};
    if (argc == 2 && std::string_view(argv[1]) == "--github-smoke") {
        Service live(Configuration{root, root, {}, "1.0.6", std::string(kUpdatePublicKey), platform(), {}});
        live.check();
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (live.snapshot().busy && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto state = live.snapshot();
        std::cout << state.message << '\n';
        return !state.busy && (state.state == State::Current || state.state == State::Available) ? 0 : 1;
    }
    auto test = [&](const char* name, Feed& feed) {
        auto data = root / name;
        std::filesystem::create_directory(data);
        return std::make_unique<Service>(Configuration{data, root, {}, "1.0.6", feed.key, "linux-x86_64", {}},
                                         [&feed](auto& r, auto stop, auto& p) { return feed.get(r, stop, p); });
    };
    Feed good;
    auto app = test("good", good);
    app->startup();
    idle(*app);
    assert(app->snapshot().state == State::Available);
    assert(app->snapshot().version == "1.0.7");
    auto calls = good.calls.load();
    app->startup();
    assert(good.calls == calls);
    app->download();
    idle(*app);
    assert(app->snapshot().state == State::Ready);
    assert(app->snapshot().received == good.package.size());
    calls = good.calls;
    app->download();
    idle(*app);
    assert(good.calls == calls); // Reuse a fully verified cached package.
    app->skip();
    assert(app->snapshot().preferences.skipped == "1.0.7");
    const std::string previous_id(32, 'a');
    auto previous = root / "previous" / "updates";
    std::filesystem::create_directories(previous / ("handoff-" + previous_id));
    durable_write(previous / "last-install.json", Json{{"id", previous_id}}.dump());
    durable_write(previous / ("handoff-" + previous_id) / "result.json", Json{{"state", "recovered"}}.dump());
    auto restored = test("previous", good);
    assert(restored->snapshot().installation_result.find("restored") != std::string::npos);
    Feed bad;
    bad.corrupt = true;
    auto rejected = test("bad", bad);
    rejected->check();
    idle(*rejected);
    assert(rejected->snapshot().state == State::Failed);
    assert(rejected->snapshot().message.find("signature") != std::string::npos);
    Feed limited;
    limited.rate = true;
    auto waiting = test("limited", limited);
    waiting->check();
    idle(*waiting);
    calls = limited.calls;
    waiting->check();
    assert(limited.calls == calls);
    assert(waiting->snapshot().message.find("cooling") != std::string::npos);
    Feed pause;
    pause.blocked = true;
    auto paused = test("paused", pause);
    auto started = std::chrono::steady_clock::now();
    paused->check();
    assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
    paused->cancel();
    idle(*paused);
    assert(paused->snapshot().state == State::Failed);
    Feed off;
    auto disabled = test("disabled", off);
    auto pref = disabled->snapshot().preferences;
    pref.automatic = false;
    disabled->preferences(pref);
    disabled->startup();
    assert(off.calls == 0);
    Feed manual;
    manual.manual = true;
    auto old_release = test("manual", manual);
    old_release->check();
    idle(*old_release);
    assert(old_release->snapshot().manual_only && old_release->snapshot().state == State::Available);
    Feed beta;
    beta.beta_list = true;
    auto beta_app = test("beta", beta);
    pref = beta_app->snapshot().preferences;
    pref.beta = true;
    beta_app->preferences(pref);
    beta_app->check();
    idle(*beta_app);
    assert(beta_app->snapshot().state == State::Available);
    assert(beta_app->snapshot().version == "1.0.7"); // Stable not lost behind playtests.
    std::cout << "PASS: async checks, signatures, verified cache reuse, preferences, cancellation, "
                 "rate limiting and manual fallback\n";
}
