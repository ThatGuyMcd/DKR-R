#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace dkr::mods {
// MemoryExceeded: the supervisor stopped the worker for exceeding its memory
// budget where the kernel cannot enforce RLIMIT_AS (macOS).
enum class WorkerOutcome { Completed, Cancelled, TimedOut, MemoryExceeded, Failed };
struct WorkerResult {
    WorkerOutcome outcome = WorkerOutcome::Failed;
    unsigned exit_code = 0;
};
// Runs only the application-supplied executable, with one argument. No shell.
// Call from an import job, never while holding a UI/network/session lock.
WorkerResult run_worker(const std::filesystem::path& executable, const std::filesystem::path& request,
                        const std::function<bool()>& keep_running,
                        std::chrono::milliseconds timeout = std::chrono::minutes(3));
void constrain_import_worker();
// Where the platform can (macOS Seatbelt), deny network, fork/exec and
// filesystem writes outside the transaction directory (except /dev/null).
// No-op elsewhere.
void sandbox_import_worker(const std::filesystem::path& writable_directory);
} // namespace dkr::mods
