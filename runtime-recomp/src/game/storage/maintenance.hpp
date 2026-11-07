#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <vector>

namespace dkr::storage {
// Publishers can run concurrently. Maintenance needs exclusive access. Nested
// library calls on one worker share its read lease.
inline std::shared_mutex activity_mutex;
inline std::atomic<unsigned> publishers_waiting{0};
struct Activity {
    static inline thread_local unsigned depth = 0;
    std::optional<std::shared_lock<std::shared_mutex>> claim;
    Activity() {
        if (depth++ == 0) {
            ++publishers_waiting;
            try {
                claim.emplace(activity_mutex);
            } catch (...) {
                --publishers_waiting;
                --depth;
                throw;
            }
            --publishers_waiting;
        }
    }
    ~Activity() {
        --depth;
    }
    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;
};
enum class Action { Automatic, Images, Clean, Scan, Preview, ClearCaches, TrimHistory, OldDumps };
struct Options {
    Action action = Action::Automatic;
    std::uint64_t image_budget = 128ULL << 20, online_budget = 1ULL << 30;
    std::uint64_t history_budget = 256ULL << 20;
    unsigned history_count = 20;
    bool dry_run = false;
    std::stop_token stop;
};
struct Row {
    std::string name;
    std::uint64_t bytes = 0, files = 0;
};
struct View {
    std::vector<Row> rows;
    std::uint64_t removed_bytes = 0, removed_files = 0;
    bool busy = false;
    std::string status = "Storage has not been measured yet.";
    bool preview = false;
    bool deferred = false;
};
// Synchronous worker/test entry. Only recognised application-owned files are
// eligible. The optional installation root comes from the running executable,
// never from a downloaded package or a saved request's arbitrary target path.
View maintain(const std::filesystem::path& profile, const Options& = {},
              const std::filesystem::path& installation = {});
std::filesystem::path cached_package(const std::filesystem::path& folder, const std::string& digest,
                                     std::uint64_t bytes);
void start(const std::filesystem::path& profile);
void shutdown();
void gameplay(bool active);
void request(Action action = Action::Automatic);
View view();
bool automatic();
void automatic(bool enabled);
// Called on cache hits, on a background worker; access time is not portable.
inline void used(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
}
} // namespace dkr::storage
