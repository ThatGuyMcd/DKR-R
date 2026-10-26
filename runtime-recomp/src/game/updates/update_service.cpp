#include "update_service.hpp"
#include "update_install.hpp"
#include "update_trust.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <json/json.hpp>
#include <random>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <gnu/libc-version.h>
#endif

namespace dkr::updates {
#if defined(__APPLE__) || defined(__ANDROID__)
std::string native_os_version();
#endif
#ifdef __ANDROID__
void android_install_update(const std::filesystem::path&, const std::string&, const std::string&);
#endif
namespace {
using Json = nlohmann::json;
std::unique_ptr<Service> owner;
std::vector<std::string> launch_arguments;
Version normalized_requirement(std::string value) {
    while (std::count(value.begin(), value.end(), '.') < 2)
        value += ".0";
    return Version::parse(value);
}
bool compatible_system(const Artifact& artifact) {
    // Tests may inject an explicit target; production always uses platform().
    if (artifact.platform != platform())
        return true;
#if defined(__linux__) && !defined(__ANDROID__)
    if (!artifact.minimum_glibc.empty() &&
        normalized_requirement(gnu_get_libc_version()).compare(normalized_requirement(artifact.minimum_glibc)) < 0)
        return false;
#endif
    if (artifact.minimum_os.empty())
        return true;
    std::string current;
#ifdef _WIN32
    using QueryVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto query = reinterpret_cast<QueryVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!query || query(&info) != 0)
        return false;
    current = std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." +
              std::to_string(info.dwBuildNumber);
#elif defined(__APPLE__) || defined(__ANDROID__)
    current = native_os_version();
#else
    return false;
#endif
    return normalized_requirement(current).compare(normalized_requirement(artifact.minimum_os)) >= 0;
}
std::uint64_t now() {
    return std::uint64_t(std::time(nullptr));
}
std::string date() {
    auto n = std::time(nullptr);
    std::tm value{};
#ifdef _WIN32
    gmtime_s(&value, &n);
#else
    gmtime_r(&n, &value);
#endif
    char out[40]{};
    std::strftime(out, sizeof(out), "%Y-%m-%d %H:%M UTC", &value);
    return out;
}
Json parse(std::string_view bytes) {
    if (bytes.size() > kMaxMetadataBytes)
        throw std::runtime_error("Update response is too large.");
    return Json::parse(bytes.begin(), bytes.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 32)
            throw std::runtime_error("Update JSON is too deeply nested.");
        return true;
    });
}
std::string legacy_version(std::string tag) {
    if (tag.starts_with("Version"))
        tag.erase(0, 7);
    else if (tag.starts_with('v'))
        tag.erase(0, 1);
    return tag;
}
std::uint64_t header_number(const HttpResponse& response, const char* name) {
    auto f = response.headers.find(name);
    std::uint64_t n = 0;
    if (f != response.headers.end())
        std::from_chars(f->second.data(), f->second.data() + f->second.size(), n);
    return n;
}
std::filesystem::path cache(const Configuration& c) {
    return c.data / "updates";
}
} // namespace
Service::Service(Configuration c, Transport t) : config(std::move(c)), transport(std::move(t)) {
    Version::parse(config.version);
    unhex(config.key, 32);
    std::filesystem::create_directories(cache(config));
    require_private_tree(cache(config));
    try {
        auto p = parse(read_text(cache(config) / "preferences.json", 16384));
        view.preferences.automatic = p.value("automatic", true);
        view.preferences.beta = p.value("beta", false);
        view.preferences.skipped = p.value("skipped", std::string{});
        retry_after = p.value("retry_after", std::uint64_t{});
    } catch (const std::exception&) {
    }
    read_installation_result();
}
Service::~Service() {
    cancel();
    if (worker.joinable())
        worker.join();
}
Snapshot Service::snapshot() const {
    std::scoped_lock lock(mutex);
    return view;
}
void Service::publish(State state, std::string message) {
    std::scoped_lock lock(mutex);
    view.state = state;
    view.message = std::move(message);
    std::fprintf(stderr, "[updates] state=%u status=%s\n", unsigned(state), view.message.c_str());
}
void Service::save_preferences() {
    auto s = snapshot();
    durable_write(cache(config) / "preferences.json", Json{{"automatic", s.preferences.automatic},
                                                           {"beta", s.preferences.beta},
                                                           {"skipped", s.preferences.skipped},
                                                           {"retry_after", retry_after}}
                                                          .dump());
}
void Service::read_installation_result() {
    try {
        auto record = parse(read_text(cache(config) / "last-install.json", 4096));
        auto id = record.at("id").get<std::string>();
        unhex(id, 16);
        auto result = parse(read_text(cache(config) / ("handoff-" + id) / "result.json", 8192));
        auto state = result.at("state").get<std::string>();
        std::string message;
        if (state == "complete")
            message = "Previous update installed successfully.";
        else if (state == "recovered")
            message = "The previous update could not start. Your earlier application was restored.";
        else if (state == "failed")
            message = "The previous update failed; your application and data were kept. " +
                      result.value("error", std::string{});
        if (!message.empty()) {
            std::scoped_lock lock(mutex);
            view.installation_result = std::move(message);
        }
    } catch (const std::exception&) {
        // Missing/in-progress or damaged diagnostics never block normal play.
    }
}
void Service::run(std::function<void(std::stop_token)> action) {
    {
        std::scoped_lock lock(mutex);
        if (view.busy)
            return;
        view.busy = true;
    }
    if (worker.joinable())
        worker.join();
    worker = std::jthread([this, action = std::move(action)](std::stop_token stop) {
        try {
            action(stop);
        } catch (const std::exception& e) {
            publish(State::Failed, stop.stop_requested()
                                       ? "Update work paused. You can check or resume from the launcher."
                                       : e.what());
        }
        std::scoped_lock lock(mutex);
        view.busy = false;
    });
}
HttpResponse Service::request(HttpRequest r, std::stop_token stop, const Progress& progress) {
    auto response = transport(r, stop, progress);
    if (response.status == 403 || response.status == 429) {
        auto after = header_number(response, "retry-after");
        auto reset = header_number(response, "x-ratelimit-reset");
        retry_after = std::min<std::uint64_t>(
            now() + 86400, std::max<std::uint64_t>(now() + std::max<std::uint64_t>(after, 60), reset));
        save_preferences();
        throw std::runtime_error("GitHub has limited update requests. DKR-R will wait before "
                                 "checking again; offline play is unaffected.");
    }
    return response;
}
void Service::startup() {
    if (startup_checked)
        return;
    startup_checked = true;
    if (snapshot().preferences.automatic)
        check();
}
void Service::check() {
    if (snapshot().busy)
        return;
    if (now() < retry_after) {
        publish(State::Failed, "Update checks are cooling down after GitHub rate limiting. Try later.");
        return;
    }
    run([this](std::stop_token stop) {
        publish(State::Checking, "Checking GitHub for updates...");
        discover(stop);
        read_installation_result();
    });
}
void Service::discover(std::stop_token stop) {
    auto prefs = snapshot().preferences;
    auto cached_response = [&](const std::string& query, const std::string& tag) {
        auto body_file = cache(config) / (std::string(tag) + "-releases.json"),
             etag_file = cache(config) / (std::string(tag) + "-etag.txt");
        HttpRequest r;
        r.url = query;
        try {
            r.etag = read_text(etag_file, 2048);
        } catch (...) {
        }
        auto response = request(r, stop);
        std::string bytes;
        if (response.status == 304) {
            try {
                bytes = read_text(body_file);
            } catch (...) {
                r.etag.clear();
                response = request(r, stop);
            }
        }
        if (bytes.empty()) {
            if (response.status != 200)
                throw std::runtime_error("GitHub release check failed (HTTP " + std::to_string(response.status) +
                                         "). Offline play is unaffected.");
            bytes = response.body;
            parse(bytes);
            durable_write(body_file, bytes);
            if (auto etag = response.headers.find("etag"); etag != response.headers.end())
                durable_write(etag_file, etag->second);
        }
        return parse(bytes);
    };
    // GitHub's list is publication-ordered, not SemVer-ordered. Always include
    // latest stable even when many playtests have pushed it out of the list.
    const auto latest = cached_response("https://api.github.com/repos/ThatGuyMcd/DKR-R/releases/latest", "stable");
    Json rows = Json::array({latest});
    if (prefs.beta) {
        rows = cached_response("https://api.github.com/repos/ThatGuyMcd/DKR-R/releases?per_page=20", "beta");
        if (!rows.is_array())
            throw std::runtime_error("GitHub did not return a release list.");
        bool included = false;
        for (const auto& row : rows)
            included |= row.value("tag_name", std::string{}) == latest.value("tag_name", std::string{});
        if (!included)
            rows.push_back(latest);
    }
    std::optional<Manifest> best;
    std::string manual_version, manual_notes, manual_reason;
    std::string best_bytes, best_signature;
    for (auto& row : rows) {
        if (stop.stop_requested())
            throw std::runtime_error("Update check paused.");
        if (row.value("draft", false) || (!prefs.beta && row.value("prerelease", false)))
            continue;
        try {
            if (Version::parse(legacy_version(row.at("tag_name").get<std::string>()))
                    .compare(Version::parse(config.version)) <= 0)
                continue;
        } catch (const std::exception&) {
            // Non-standard old tags may still carry a valid signed manifest.
        }
        std::string manifest_url, signature_url;
        for (auto& asset : row.at("assets")) {
            auto name = asset.at("name").get<std::string>();
            if (name == "DKR-R-update.json")
                manifest_url = asset.at("browser_download_url").get<std::string>();
            if (name == "DKR-R-update.sig")
                signature_url = asset.at("browser_download_url").get<std::string>();
        }
        if (manifest_url.empty() || signature_url.empty()) {
            try {
                auto version = legacy_version(row.at("tag_name").get<std::string>());
                if (Version::parse(version).compare(Version::parse(config.version)) > 0 &&
                    (manual_version.empty() || Version::parse(version).compare(Version::parse(manual_version)) > 0)) {
                    auto notes = row.at("html_url").get<std::string>();
                    if (!notes.starts_with("https://github.com/ThatGuyMcd/DKR-R/releases/"))
                        continue;
                    manual_version = version;
                    manual_notes = std::move(notes);
                    manual_reason = "A newer release is available, but has no signed updater "
                                    "manifest. Open the release page to install it manually.";
                }
            } catch (...) {
            }
            continue;
        }
        auto manifest = request(HttpRequest{manifest_url, {}, {}, kMaxMetadataBytes}, stop);
        auto signature = request(HttpRequest{signature_url, {}, {}, 128}, stop);
        if (manifest.status != 200 || signature.status != 200)
            throw std::runtime_error("The published update manifest is incomplete. Nothing was installed.");
        auto verified = verify_manifest(manifest.body, signature.body, config.key);
        if (verified.tag != row.at("tag_name").get<std::string>() || (!prefs.beta && verified.channel != "stable"))
            continue;
        if (!verified.find(config.platform))
            continue;
        if (!compatible_system(*verified.find(config.platform))) {
            if (Version::parse(verified.version).compare(Version::parse(config.version)) > 0) {
                manual_version = verified.version;
                manual_notes = verified.notes;
                manual_reason = "This release requires a newer operating system/runtime. Your current "
                                "application was kept; view the release requirements before updating.";
            }
            continue;
        }
        if (Version::parse(verified.version).compare(Version::parse(config.version)) <= 0)
            continue;
        if (!best || Version::parse(verified.version).compare(Version::parse(best->version)) > 0) {
            best = verified;
            best_bytes = manifest.body;
            best_signature = signature.body;
        }
    }
    if (best) {
        durable_write(cache(config) / "manifest.json", best_bytes);
        durable_write(cache(config) / "manifest.sig", best_signature);
    }
    {
        std::scoped_lock lock(mutex);
        view.last_check = date();
        view.received = 0;
        view.total = 0;
        view.version.clear();
        view.notes.clear();
        view.manual_only = false;
        release = best;
        if (best) {
            view.version = best->version;
            view.notes = best->notes;
            view.total = best->find(config.platform)->bytes;
        } else if (!manual_version.empty()) {
            view.version = manual_version;
            view.notes = manual_notes;
            view.manual_only = true;
        }
    }
    auto s = snapshot();
    if (!s.version.empty())
        publish(State::Available, s.preferences.skipped == s.version
                                      ? "This release has been skipped. You can still update here."
                                  : s.manual_only ? manual_reason
                                                  : "A verified DKR-R update is available.");
    else
        publish(State::Current, "You are running the latest available release for this platform/channel.");
}
void Service::download() {
    run([this](std::stop_token stop) {
        if (!release)
            throw std::runtime_error("Check for a signed update first.");
        auto a = *release->find(config.platform);
        auto directory = cache(config) / ("download-" + a.sha256);
        std::filesystem::create_directory(directory);
        require_private_tree(directory);
        auto partial = directory / "package.part";
        package = directory / a.name;
        if (std::filesystem::exists(package) && std::filesystem::file_size(package) == a.bytes &&
            hash_file(package, kMaxPackageBytes) == a.sha256) {
            publish(State::Ready, "Verified update is ready to install.");
            return;
        }
        auto space = std::filesystem::space(directory);
        if (space.available < a.bytes * 3 + kMaxMetadataBytes)
            throw std::runtime_error("Not enough free space to download and safely stage this update.");
        publish(State::Downloading, "Downloading the complete release package...");
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            HttpRequest r;
            r.url = artifact_url(*release, a);
            r.file = partial;
            r.limit = a.bytes;
            r.timeout_seconds = 1800;
            r.etag_file = directory / "etag.txt";
            try {
                r.etag = read_text(directory / "etag.txt", 2048);
                if (!r.etag.empty() && std::filesystem::exists(partial)) {
                    r.offset = std::filesystem::file_size(partial);
                    if (r.offset >= a.bytes) {
                        r.offset = 0;
                        r.etag.clear();
                    }
                }
            } catch (...) {
            }
            try {
                auto response = request(r, stop, [this](std::uint64_t bytes) {
                    std::scoped_lock lock(mutex);
                    view.received = bytes;
                });
                if (auto etag = response.headers.find("etag"); etag != response.headers.end())
                    durable_write(directory / "etag.txt", etag->second);
                if (response.status != 200 && response.status != 206)
                    throw std::runtime_error("Package download failed (HTTP " + std::to_string(response.status) + ").");
                if (std::filesystem::file_size(partial) != a.bytes || hash_file(partial, a.bytes) != a.sha256)
                    throw std::runtime_error("Downloaded update failed checksum verification. Nothing was installed.");
                replace_file(partial, package);
                publish(State::Ready, "Verified update is ready. Install and restart when you are finished playing.");
                return;
            } catch (const std::exception&) {
                if (stop.stop_requested() || attempt == 2)
                    throw;
                for (unsigned i = 0; i < 20 && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}
void Service::cancel() {
    if (worker.joinable())
        worker.request_stop();
}
void Service::preferences(Preferences p) {
    if (snapshot().busy)
        return;
    {
        std::scoped_lock lock(mutex);
        view.preferences = std::move(p);
    }
    try {
        save_preferences();
    } catch (const std::exception& e) {
        publish(State::Failed, e.what());
    }
}
void Service::skip() {
    auto s = snapshot();
    s.preferences.skipped = s.version;
    preferences(s.preferences);
}
bool Service::install(std::string& error) {
    auto s = snapshot();
    if (s.state != State::Ready || s.busy || !release) {
        error = "Download and verify the update first.";
        return false;
    }
    if (auto reason = installation_unavailable(config); !reason.empty()) {
        error = reason;
        return false;
    }
    run([this](std::stop_token stop) {
        publish(State::Preparing, "Preparing verified update and recovery backup...");
        if (stop.stop_requested())
            throw std::runtime_error("Installation preparation paused.");
#if defined(__ANDROID__)
        auto* artifact = release->find(config.platform);
        if (!artifact || artifact->kind != "apk")
            throw std::runtime_error("This release has no compatible APK.");
        if (hash_file(package, kMaxPackageBytes) != artifact->sha256)
            throw std::runtime_error("APK changed before installation.");
        android_install_update(package, release->version, artifact->sha256);
        publish(State::Ready, "Android's installer has been opened. Confirm the update there; the "
                              "current app/data are kept if you cancel.");
        return;
#else
        prepare_install(config, *release, package, stop);
#endif
        publish(State::Preparing, "Update helper ready. DKR-R will close normally and restart.");
    });
    return true;
}
bool Service::install_handoff_ready() const {
    return snapshot().state == State::Preparing && !snapshot().busy;
}
void Service::launcher_ready() {
    acknowledge_updated_launcher(config);
}
void configure(const std::filesystem::path& data, const std::filesystem::path& application,
               const std::filesystem::path& helper, std::string version) {
    if (owner)
        return;
    try {
        owner = std::make_unique<Service>(Configuration{data, application, helper, std::move(version),
                                                        std::string(kUpdatePublicKey), platform(), launch_arguments});
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[updates] configuration disabled: %s\n", e.what());
    }
}
Service* service() {
    return owner.get();
}
void remember_arguments(int argc, char** argv) {
    launch_arguments.clear();
    for (int i = 1; i < argc; ++i)
        launch_arguments.emplace_back(argv[i]);
}
} // namespace dkr::updates
