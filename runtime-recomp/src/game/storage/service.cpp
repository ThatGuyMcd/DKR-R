#include "maintenance.hpp"
#include "updates/update_core.hpp"
#include "updates/update_process.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <map>
#include <optional>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace dkr::storage {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
fs::path profile_path(fs::path path) {
    path = fs::absolute(path).lexically_normal().make_preferred();
#ifdef _WIN32
    auto s = path.native();
    if (!s.starts_with(L"\\\\?\\"))
        path = s.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + s.substr(2) : L"\\\\?\\" + s;
#endif
    return path;
}
std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
struct BackgroundPriority {
    BackgroundPriority() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#elif defined(__linux__)
        const auto thread = syscall(SYS_gettid);
        setpriority(PRIO_PROCESS, static_cast<id_t>(thread), 19);
#ifdef SYS_ioprio_set
        syscall(SYS_ioprio_set, 1, thread, (2 << 13) | 7);
#endif
#endif
    }
    ~BackgroundPriority() {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
#endif
    }
};
Json read(const fs::path& p) {
    if (!fs::exists(p))
        return Json::object();
    dkr::updates::require_private_tree(p.parent_path());
    if (fs::symlink_status(p).type() != fs::file_type::regular || fs::hard_link_count(p) != 1)
        throw std::runtime_error("Storage preferences were kept.");
    return Json::parse(dkr::updates::read_text(p, 8192));
}
struct Service {
    fs::path profile, root, instance;
    std::unique_ptr<dkr::updates::InstallLock> life;
    std::mutex mutex;
    std::condition_variable_any changed;
    View snapshot;
    std::optional<Action> pending;
    std::map<std::string, std::chrono::steady_clock::time_point> installs;
    bool enabled = true, startup_complete = false, preferences_dirty = false;
    std::atomic<bool> playing{false};
    std::uint64_t cleared_bytes = 0, cleared_files = 0;
    std::chrono::steady_clock::time_point ready_at{};
    std::stop_source cancellation;
    std::jthread worker;
    explicit Service(fs::path path)
        : profile(profile_path(fs::canonical(profile_path(path)))), root(profile / "storage") {
        fs::create_directory(root);
        dkr::updates::require_private_tree(root);
        dkr::updates::InstallLock register_lock(root, 10000);
        fs::create_directory(root / "instances");
        dkr::updates::require_private_tree(root / "instances");
        instance = root / "instances" / dkr::updates::unique_id();
        fs::create_directory(instance);
        life = std::make_unique<dkr::updates::InstallLock>(instance, 0);
        const auto preferences = read(root / "preferences.json");
        enabled = preferences.value("automatic", true);
        worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Service() {
        worker.request_stop();
        {
            std::lock_guard lock(mutex);
            cancellation.request_stop();
        }
        changed.notify_all();
        worker.join();
        if (preferences_dirty)
            try {
                dkr::updates::InstallLock gate(root, 0);
                dkr::updates::durable_write(root / "preferences.json",
                                            Json{{"schema", 1}, {"automatic", enabled}}.dump());
            } catch (...) {
            }
        life.reset();
        std::error_code ec;
        fs::remove(instance, ec);
    }
    void run(std::stop_token stop) noexcept {
        BackgroundPriority priority;
        using Clock = std::chrono::steady_clock;
        const auto startup_due = Clock::now() + std::chrono::seconds(10);
        while (!stop.stop_requested()) {
            Action action;
            std::string installed_digest;
            {
                std::unique_lock lock(mutex);
                auto due = pending ? ready_at : enabled && !startup_complete ? startup_due : Clock::time_point::max();
                for (const auto& [digest, ready] : installs)
                    due = std::min(due, ready);
                const auto ready = [&] {
                    return preferences_dirty || (pending && Clock::now() >= ready_at) ||
                           (!pending && enabled && !startup_complete && Clock::now() >= startup_due) ||
                           std::any_of(installs.begin(), installs.end(),
                                       [&](const auto& job) { return Clock::now() >= job.second; });
                };
                if (due == Clock::time_point::max())
                    changed.wait(lock, stop, ready);
                else
                    changed.wait_until(lock, stop, due, ready);
                if (stop.stop_requested())
                    return;
                if (preferences_dirty) {
                    const auto value = enabled;
                    preferences_dirty = false;
                    lock.unlock();
                    try {
                        dkr::updates::InstallLock gate(root, 0);
                        dkr::updates::durable_write(root / "preferences.json",
                                                    Json{{"schema", 1}, {"automatic", value}}.dump());
                    } catch (const std::exception& error) {
                        lock.lock();
                        preferences_dirty = true;
                        snapshot.status = error.what();
                        changed.wait_for(lock, stop, std::chrono::seconds(5), [] { return false; });
                    }
                    continue;
                }
                const auto install = std::find_if(installs.begin(), installs.end(),
                                                  [&](const auto& job) { return Clock::now() >= job.second; });
                if (pending && Clock::now() >= ready_at) {
                    action = *pending;
                    pending.reset();
                } else if (install != installs.end()) {
                    action = Action::Installed;
                    installed_digest = install->first;
                    installs.erase(install);
                } else if (!pending && enabled && !startup_complete && Clock::now() >= startup_due) {
                    action = Action::Automatic;
                } else {
                    continue;
                }
                if ((action == Action::Automatic || action == Action::Images) && !enabled)
                    continue;
                cancellation = std::stop_source{};
                snapshot.busy = true;
                snapshot.status = "Waiting to start storage cleanup...";
            }
            try {
                // Publishers never wait for an idle cleanup to acquire their
                // barrier. If a publisher already owns it, try maintenance later.
                dkr::updates::InstallLock gate(root, 0);
                for (const auto& e : fs::directory_iterator(root / "instances")) {
                    if (e.path() == instance)
                        continue;
                    const auto n = e.path().filename().string();
                    if (n.size() != 32 || n.find_first_not_of("0123456789abcdef") != n.npos)
                        continue;
                    dkr::updates::require_private_tree(e.path());
                    dkr::updates::InstallLock other(e.path(), 0);
                    // A live second instance holds this lease: construction
                    // throws before any filesystem maintenance takes place.
                    std::error_code ec;
                    fs::remove(e.path(), ec);
                }
                fs::path install;
                try {
                    install = dkr::updates::application_directory();
                } catch (...) {
                }
                Options options;
                options.action = action;
                options.installed_digest = installed_digest;
                {
                    std::lock_guard lock(mutex);
                    options.stop = cancellation.get_token();
                }
                options.background = true;
                options.gameplay = &playing;
                options.progress = [this, action](const View& progress) {
                    std::lock_guard lock(mutex);
                    auto next = progress;
                    next.busy = true;
                    if (!next.preview && action != Action::Scan) {
                        next.removed_bytes += cleared_bytes;
                        next.removed_files += cleared_files;
                    }
                    if (next.rows.empty())
                        next.rows = snapshot.rows;
                    snapshot = std::move(next);
                };
                auto result = maintain(profile, options, install);
                {
                    std::lock_guard lock(mutex);
                    if (!result.preview && action != Action::Scan) {
                        cleared_bytes += result.removed_bytes;
                        cleared_files += result.removed_files;
                        result.removed_bytes = cleared_bytes;
                        result.removed_files = cleared_files;
                    }
                    if (action != Action::Scan && action != Action::Preview)
                        result.rows = snapshot.rows;
                    if (action == Action::Scan) {
                        result.removed_bytes = snapshot.removed_bytes;
                        result.removed_files = snapshot.removed_files;
                    }
                    snapshot = std::move(result);
                    snapshot.busy = false;
                    if (snapshot.deferred) {
                        if (action == Action::Installed) {
                            installs.try_emplace(installed_digest, Clock::now() + std::chrono::seconds(5));
                        } else if (!pending) {
                            pending = action;
                            ready_at = Clock::now() + std::chrono::seconds(5);
                        }
                    } else if (action == Action::Automatic || action == Action::Clean) {
                        startup_complete = true;
                        try {
                            dkr::updates::durable_write(root / "last-cleanup.json",
                                                        Json{{"schema", 1}, {"time", now()}}.dump());
                        } catch (...) {
                            snapshot.status += " The completion timestamp could not be saved.";
                        }
                    }
                }
            } catch (const std::exception& error) {
                std::unique_lock lock(mutex);
                snapshot.busy = false;
                snapshot.status = error.what();
                if (action == Action::Installed) {
                    installs.try_emplace(installed_digest, Clock::now() + std::chrono::seconds(5));
                } else if (!pending) {
                    pending = action;
                    ready_at = Clock::now() + std::chrono::seconds(5);
                }
            }
        }
    }
};
std::mutex owner_mutex;
std::shared_ptr<Service> owner;
std::shared_ptr<Service> current() {
    std::lock_guard lock(owner_mutex);
    return owner;
}
} // namespace
void start(const fs::path& profile) {
    std::lock_guard lock(owner_mutex);
    if (owner)
        return;
    try {
        owner = std::make_shared<Service>(profile);
    } catch (...) {
    }
}
void shutdown() {
    std::shared_ptr<Service> old;
    {
        std::lock_guard lock(owner_mutex);
        old = std::move(owner);
    }
    old.reset();
}
void gameplay(bool active) {
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        p->playing = active;
        p->changed.notify_all();
    }
}
void request(Action action) {
    // Generic hints never schedule another full pass. Completed installs use
    // their own package-scoped queue, independent of the startup preference.
    if (action == Action::Automatic || action == Action::Images || action == Action::Installed)
        return;
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        p->pending = action;
        p->ready_at = std::chrono::steady_clock::now();
        p->changed.notify_all();
    }
}
void installed_package(std::string digest) {
    if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != digest.npos)
        return;
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        p->installs.try_emplace(std::move(digest), std::chrono::steady_clock::now());
        p->changed.notify_all();
    }
}
View view() {
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        return p->snapshot;
    }
    return {{}, 0, 0, false, "Storage maintenance is unavailable for this data folder."};
}
bool automatic() {
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        return p->enabled;
    }
    return false;
}
void automatic(bool enabled) {
    if (auto p = current()) {
        std::lock_guard lock(p->mutex);
        if (p->enabled == enabled)
            return;
        p->enabled = enabled;
        p->preferences_dirty = true;
        if (!enabled)
            p->cancellation.request_stop();
        p->changed.notify_all();
    }
}
} // namespace dkr::storage
