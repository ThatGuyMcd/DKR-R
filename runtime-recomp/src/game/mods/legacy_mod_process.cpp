#include "legacy_mod_process.hpp"
#include "legacy_mod_format.hpp"
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(__APPLE__) && !defined(DKR_IOS)
#include <libproc.h>
#endif
#if defined(__APPLE__)
#include <sandbox.h>
#include <sys/event.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dkr::mods {
namespace {
constexpr std::size_t WorkerMemory = 1024 * MiB;
#if defined(_WIN32)
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
std::wstring quoted(const std::filesystem::path& path) {
    // Windows CRT command-line rules, including backslashes before quotes.
    std::wstring result = L"\"";
    unsigned slash = 0;
    for (const auto c : path.native()) {
        if (c == L'\\') {
            ++slash;
            continue;
        }
        result.append(c == L'"' ? slash * 2 + 1 : slash, L'\\');
        slash = 0;
        result += c;
    }
    result.append(slash * 2, L'\\');
    return result + L'"';
}
#endif
} // namespace
#if !defined(_WIN32)
namespace {
// Exit with the parent so an abandoned worker never outlives the app.
void exit_with_parent() {
    const auto parent = getppid();
#if defined(__linux__)
    if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
        throw Error("The import worker lost its parent process.");
#elif defined(__APPLE__)
    // PDEATHSIG equivalent: a kqueue watch on the parent, serviced by a thread.
    const int queue = kqueue();
    struct kevent watch {};
    EV_SET(&watch, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (parent == 1 || queue < 0 || kevent(queue, &watch, 1, nullptr, 0, nullptr) != 0 || getppid() != parent) {
        if (queue >= 0)
            close(queue);
        throw Error("The import worker lost its parent process.");
    }
    std::thread([queue] {
        struct kevent fired {};
        while (kevent(queue, nullptr, 0, &fired, 1, nullptr) < 0 && errno == EINTR) {
        }
        _exit(1);
    }).detach();
#else
#error "The import worker needs a parent-death watch on this platform."
#endif
}
// Address-space and privilege limits where the kernel enforces them. macOS
// ignores RLIMIT_AS (run_worker caps the physical footprint instead) and
// sandbox_import_worker stands in for NO_NEW_PRIVS.
void limit_memory_and_privileges() {
#if defined(__linux__)
    const rlimit memory{WorkerMemory, WorkerMemory};
    if (setrlimit(RLIMIT_AS, &memory) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        throw Error("Could not establish the import worker resource limits.");
#endif
}
} // namespace
#endif
void constrain_import_worker() {
#if !defined(_WIN32)
    exit_with_parent();
    const rlimit cpu{120, 120}, output{MaxStaged + 2 * MiB, MaxStaged + 2 * MiB}, no_core{0, 0};
    if (setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_FSIZE, &output) || setrlimit(RLIMIT_CORE, &no_core))
        throw Error("Could not establish the import worker resource limits.");
    limit_memory_and_privileges();
#endif
}
void sandbox_import_worker(const std::filesystem::path& writable_directory) {
#if defined(__APPLE__)
    // Seatbelt matches resolved paths, so name the directory canonically.
    std::error_code ec;
    const auto directory = std::filesystem::canonical(writable_directory, ec).string();
    if (ec)
        throw Error("Could not resolve the import worker's private directory.");
    if (directory.find_first_of("\"\\") != std::string::npos)
        throw Error("The import directory contains characters the sandbox profile cannot quote.");
    const std::string profile = "(version 1)(allow default)(deny network*)(deny process-fork)(deny process-exec*)"
                                "(deny file-write*)(allow file-write* (subpath \"" +
                                directory +
                                "\"))"
                                "(allow file-write-data (literal \"/dev/null\"))";
    char* failure = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // Deprecated but supported and still the only unprivileged API for a
    // custom profile; failing closed is the point.
    const int result = sandbox_init(profile.c_str(), 0, &failure);
    const std::string reason = failure ? failure : "";
    if (failure)
        sandbox_free_error(failure);
#pragma clang diagnostic pop
    if (result != 0)
        throw Error("Could not sandbox the import worker: " + reason);
#else
    (void)writable_directory;
#endif
}
WorkerResult run_worker(const std::filesystem::path& executable, const std::filesystem::path& request,
                        const std::function<bool()>& keep_running, std::chrono::milliseconds timeout) {
#if defined(DKR_IOS)
    (void)executable;
    (void)request;
    (void)keep_running;
    (void)timeout;
    throw Error("Legacy mod import is not available in the iOS app.");
#else
    if (!executable.is_absolute() || !request.is_absolute() || timeout.count() <= 0)
        throw Error("Importer requires absolute private executable/request paths and a bounded timeout.");
    if (keep_running && !keep_running())
        return {WorkerOutcome::Cancelled, 0};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
#if defined(_WIN32)
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    if (!job.value)
        throw Error("Could not create an isolated import job.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                              JOB_OBJECT_LIMIT_PROCESS_TIME | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 120LL * 10000000;
    limits.ProcessMemoryLimit = WorkerMemory;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw Error("Could not set the import job's resource limits.");
    auto command = quoted(executable) + L" " + quoted(request);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, executable.parent_path().c_str(), &startup,
                        &info))
        throw Error("The private mod importer could not be started.");
    Handle process{info.hProcess}, thread{info.hThread};
    if (!AssignProcessToJobObject(job.value, process.value) || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, 5000);
        throw Error("The import worker could not enter its bounded job.");
    }
    // Declared after process so exceptions from callbacks terminate/reap before
    // its handle closes. The job also kills the worker if the app exits.
    struct Reap {
        HANDLE job, process;
        bool done = false;
        ~Reap() {
            if (!done) {
                TerminateJobObject(job, 1);
                WaitForSingleObject(process, 5000);
            }
        }
    } reap{job.value, process.value};
    while (true) {
        const auto status = WaitForSingleObject(process.value, 25);
        if (status == WAIT_OBJECT_0) {
            DWORD code = 1;
            if (!GetExitCodeProcess(process.value, &code))
                throw Error("Could not read import completion.");
            reap.done = true;
            return {code ? WorkerOutcome::Failed : WorkerOutcome::Completed, code};
        }
        if (status == WAIT_FAILED)
            throw Error("Could not monitor the import worker.");
        if (keep_running && !keep_running())
            return {WorkerOutcome::Cancelled, 0};
        if (std::chrono::steady_clock::now() >= deadline)
            return {WorkerOutcome::TimedOut, 0};
    }
#else
    auto exe = executable.native(), argument = request.native();
    char* arguments[] = {exe.data(), argument.data(), nullptr};
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes))
        throw Error("Could not initialize the import process.");
    struct Attr {
        posix_spawnattr_t& value;
        ~Attr() {
            posix_spawnattr_destroy(&value);
        }
    } attr{attributes};
    if (posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) || posix_spawnattr_setpgroup(&attributes, 0))
        throw Error("Could not isolate the import process group.");
    pid_t child = 0;
    if (posix_spawn(&child, exe.c_str(), nullptr, &attributes, arguments, environ))
        throw Error("The private mod importer could not be started.");
    struct Reap {
        pid_t child;
        bool done = false;
        ~Reap() {
            if (!done) {
                kill(-child, SIGKILL);
                int status;
                while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
                }
            }
        }
    } reap{child};
    while (true) {
        int status = 0;
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            reap.done = true;
            if (WIFEXITED(status))
                return {WEXITSTATUS(status) ? WorkerOutcome::Failed : WorkerOutcome::Completed,
                        static_cast<unsigned>(WEXITSTATUS(status))};
            return {WorkerOutcome::Failed, static_cast<unsigned>(WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1)};
        }
        if (waited < 0 && errno != EINTR)
            throw Error("Could not monitor the import worker.");
#if defined(__APPLE__)
        // RLIMIT_AS isn't enforced on macOS; cap the physical footprint here.
        rusage_info_v4 usage{};
        if (proc_pid_rusage(child, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) == 0 &&
            usage.ri_phys_footprint > WorkerMemory)
            return {WorkerOutcome::MemoryExceeded, 0};
#endif
        if (keep_running && !keep_running())
            return {WorkerOutcome::Cancelled, 0};
        if (std::chrono::steady_clock::now() >= deadline)
            return {WorkerOutcome::TimedOut, 0};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
#endif
#endif
}
} // namespace dkr::mods
