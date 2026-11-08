#include "reporting.hpp"
#include "updates/update_process.hpp"
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace dkr::crash_reports {
namespace {
struct Service {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::filesystem::path profile, root;
    std::string version;
    View snapshot;
    std::stop_source sending;
    unsigned generation = 0;
    bool playing = false;
    std::jthread worker;
    Service(const std::filesystem::path& path, std::string build) : profile(std::filesystem::canonical(path)),
        root(profile / "crash-reports"), version(std::move(build)) {
        std::error_code error;
        std::filesystem::create_directory(root, error);
        dkr::updates::require_private_tree(root);
        snapshot.consent = read_consent(root);
        worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Service() { worker.request_stop(); { std::lock_guard lock(mutex); sending.request_stop(); } changed.notify_all(); worker.join(); }
    void run(std::stop_token stop) noexcept {
        try {
            std::vector<std::string> identities;
            for (const auto* name : {"USERNAME", "USER", "COMPUTERNAME", "HOSTNAME"})
                if (const auto* value = std::getenv(name)) identities.emplace_back(value);
            Queue queue(profile, version, std::move(identities));
            bool discover = true;
            while (!stop.stop_requested()) {
                unsigned epoch;
                { std::unique_lock lock(mutex);
                  changed.wait(lock, stop, [&] { return !playing || snapshot.consent.choice != 1; });
                  if (stop.stop_requested()) return;
                  epoch = generation; }
                try {
                    // Short timeout: another process may already own this queue.
                    dkr::updates::InstallLock claim(queue.directory(), 0);
                    auto consent = read_consent(root);
                    { std::lock_guard lock(mutex); if (epoch != generation) continue; snapshot.consent = consent; }
                    queue.reconcile(consent, now_ms());
                    if (discover) { queue.discover(consent, now_ms()); discover = false; }
                    const auto job = consent.choice == 1 ? queue.due(now_ms()) : Pending{};
                    std::stop_token upload_stop;
                    {
                        std::lock_guard lock(mutex);
                        if (epoch != generation || (playing && consent.choice == 1)) continue;
                        snapshot.queued = queue.count(); snapshot.sending = !job.id.empty();
                        snapshot.status = consent.choice != 1 ? "support.reporting.disabled" : queue.blocked() ? "support.reporting.attention" : snapshot.queued ? "support.reporting.queued" : "support.reporting.ready";
                        sending = std::stop_source{}; upload_stop = sending.get_token();
                        if (stop.stop_requested() || snapshot.consent.choice != 1) sending.request_stop();
                    }
                    if (!job.id.empty()) {
                        const auto result = upload(job, upload_stop);
                        queue.delivered(job.id, result, now_ms());
                        std::lock_guard lock(mutex);
                        snapshot.sending = false; snapshot.queued = queue.count();
                        if (epoch == generation) snapshot.status = result.confirmed ? "support.reporting.sent" : queue.blocked() ? "support.reporting.attention" : "support.reporting.queued";
                    }
                } catch (...) {
                    std::lock_guard lock(mutex); snapshot.sending = false; snapshot.status = "support.reporting.local_error";
                }
                std::unique_lock lock(mutex);
                changed.wait_for(lock, stop, std::chrono::seconds(30), [&] { return epoch != generation; });
            }
        } catch (...) { std::lock_guard lock(mutex); snapshot.status = "support.reporting.local_error"; }
    }
};
std::unique_ptr<Service> owner;
} // namespace
void start(const std::filesystem::path& profile, std::string version) {
    if (owner) return;
    try { owner = std::make_unique<Service>(profile, std::move(version)); }
    catch (...) { /* support must never prevent the game from starting */ }
}
void shutdown() { owner.reset(); }
View view() {
    if (!owner) return {{0, false, 0}, 0, false, "support.reporting.local_error"};
    std::lock_guard lock(owner->mutex); return owner->snapshot;
}
bool choose(bool enabled, bool dumps) {
    if (!owner) return false;
    std::lock_guard lock(owner->mutex);
    try {
        dkr::updates::InstallLock preference_lock(owner->root, 0);
        const auto old = owner->snapshot.consent;
        const Consent next{enabled ? 1 : 0, enabled && dumps, enabled && old.choice == 1 ? old.since : now_ms()};
        write_consent(owner->root, next);
        owner->sending.request_stop(); owner->snapshot.consent = next;
        owner->snapshot.status = enabled ? "support.reporting.ready" : "support.reporting.disabled";
        ++owner->generation; owner->changed.notify_all(); return true;
    } catch (...) {
        // A read-only/broken support folder must not trap Skip Launcher behind
        // a consent modal. Fail closed for this process and expose the error.
        owner->sending.request_stop(); owner->snapshot.consent = {0, false, 0};
        owner->snapshot.status = "support.reporting.local_error";
        ++owner->generation; owner->changed.notify_all(); return true;
    }
}
void gameplay(bool active) {
    if (!owner) return;
    std::lock_guard lock(owner->mutex);
    owner->playing = active;
    if (active) owner->sending.request_stop();
    ++owner->generation; owner->changed.notify_all();
}
} // namespace dkr::crash_reports
