#include "update_process.hpp"
#include "../application_layout.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
namespace dkr::updates {
std::string path_utf8(const std::filesystem::path& p) {
    auto value = p.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::string unique_id() {
    std::random_device random;
    std::array<std::uint8_t, 16> bytes{};
    for (auto& b : bytes)
        b = std::uint8_t(random());
    return hex(bytes);
}
std::uint64_t process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return std::uint64_t(getpid());
#endif
}
std::filesystem::path application_executable() {
#ifdef _WIN32
    std::vector<wchar_t> text(32768);
    auto n = GetModuleFileNameW(nullptr, text.data(), DWORD(text.size()));
    if (n && n < text.size())
        return std::filesystem::path(std::wstring(text.data(), n));
#elif defined(__APPLE__)
    std::uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string text(n, '\0');
    if (!_NSGetExecutablePath(text.data(), &n))
        return std::filesystem::canonical(text.c_str());
#else
    if (auto* image = std::getenv("APPIMAGE"); image && *image) {
        auto p = std::filesystem::u8path(image);
        if (p.is_absolute() && std::filesystem::is_regular_file(p))
            return std::filesystem::canonical(p);
    }
    std::error_code error;
    auto p = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error)
        return p;
#endif
    throw std::runtime_error("Could not determine the actual application location.");
}
std::filesystem::path application_directory() {
#ifdef _WIN32
    return dkr::runtime::layout::windows_application_directory(application_executable());
#else
    return application_executable().parent_path();
#endif
}
struct InstallLock::Impl {
#ifdef _WIN32
    HANDLE handle{};
    OVERLAPPED overlapped{};
    bool acquired = false;
#else
    int handle = -1;
#endif
};
InstallLock::InstallLock(const std::filesystem::path& root, unsigned timeout, bool shared)
    : impl(std::make_unique<Impl>()) {
    auto canonical = std::filesystem::canonical(root);
    require_private_tree(canonical);
    auto identity_text = path_utf8(canonical);
#ifdef _WIN32
    for (auto& c : identity_text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    auto directory = std::filesystem::temp_directory_path() / "DKR-R-InstallLocks";
#else
    auto directory = std::filesystem::temp_directory_path() / ("dkrr-install-locks-" + std::to_string(geteuid()));
#endif
    std::filesystem::create_directory(directory);
    require_private_tree(directory);
#ifndef _WIN32
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
#endif
    auto identity = hash(std::span(reinterpret_cast<const std::uint8_t*>(identity_text.data()), identity_text.size()));
    auto file = directory / identity;
#ifdef _WIN32
    impl->handle =
        CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (impl->handle == INVALID_HANDLE_VALUE) {
        impl->handle = nullptr;
        throw std::runtime_error("Could not open installation lock.");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(impl->handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        info.nNumberOfLinks != 1) {
        CloseHandle(impl->handle);
        impl->handle = nullptr;
        throw std::runtime_error("Installation lock is not an ordinary private file.");
    }
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    do {
        impl->acquired = LockFileEx(impl->handle, LOCKFILE_FAIL_IMMEDIATELY | (shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK), 0,
                                    1, 0, &impl->overlapped) != 0;
        if (impl->acquired)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < end);
    if (!impl->acquired) {
        CloseHandle(impl->handle);
        impl->handle = nullptr;
        throw std::runtime_error("Another DKR-R instance or updater is using this installation. "
                                 "Close it before updating.");
    }
#else
    impl->handle = open(file.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (impl->handle < 0)
        throw std::runtime_error("Could not open installation lock.");
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (flock(impl->handle, (shared ? LOCK_SH : LOCK_EX) | LOCK_NB) != 0) {
        if (std::chrono::steady_clock::now() >= end) {
            close(impl->handle);
            impl->handle = -1;
            throw std::runtime_error("Another application or updater is using this installation.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif
}
InstallLock::~InstallLock() {
#ifdef _WIN32
    if (impl->handle) {
        if (impl->acquired)
            UnlockFileEx(impl->handle, 0, 1, 0, &impl->overlapped);
        CloseHandle(impl->handle);
    }
#else
    if (impl->handle >= 0) {
        flock(impl->handle, LOCK_UN);
        close(impl->handle);
    }
#endif
}
struct Child::Impl {
    int result = -1;
#ifdef _WIN32
    HANDLE handle{};
#else
    pid_t pid = -1;
#endif
};
#ifdef _WIN32
namespace {
std::wstring utf16(std::string_view value) {
    if (value.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid update process argument.");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), out.data(), n);
    return out;
}
std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (auto c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'\"';
    return out;
}
} // namespace
#endif
Child::Child(const std::filesystem::path& exe, const std::vector<std::string>& arguments,
             const std::filesystem::path& directory, bool inherit)
    : impl(std::make_unique<Impl>()) {
#ifdef _WIN32
    (void)inherit;
    std::wstring command = quote(exe.native());
    for (auto& argument : arguments)
        command += L" " + quote(utf16(argument));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // No shell, no command interpolation and no visible console for the helper.
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        directory.empty() ? nullptr : directory.c_str(), &startup, &process))
        throw std::runtime_error("Could not start update process (Windows error " + std::to_string(GetLastError()) +
                                 ").");
    CloseHandle(process.hThread);
    impl->handle = process.hProcess;
#elif defined(__ANDROID__) || defined(DKR_IOS)
    (void)exe;
    (void)arguments;
    (void)directory;
    (void)inherit;
    throw std::runtime_error("This platform uses its system installation route, not a child updater.");
#else
    std::vector<std::string> values{path_utf8(exe)};
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& s : values)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    // posix_spawn avoids unsafe heap/environment operations after fork in the
    // multithreaded launcher. Filter the old mount before spawning its successor.
    std::vector<std::string> environment;
    for (char** e = environ; e && *e; ++e) {
        std::string value = *e;
        if (value.starts_with("APPIMAGE=") || value.starts_with("APPDIR=") || value.starts_with("ARGV0="))
            continue;
        if (!inherit && (value.starts_with("DKR_UPDATE_TOKEN=") || value.starts_with("DKR_UPDATE_DATA=")))
            continue;
        environment.push_back(std::move(value));
    }
    std::vector<char*> env;
    for (auto& e : environment)
        env.push_back(e.data());
    env.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    struct Free {
        posix_spawn_file_actions_t& a;
        ~Free() {
            posix_spawn_file_actions_destroy(&a);
        }
    } free{actions};
    if (!directory.empty() && posix_spawn_file_actions_addchdir_np(&actions, directory.c_str()))
        throw std::runtime_error("Could not set update process directory.");
    if (posix_spawn(&impl->pid, exe.c_str(), &actions, nullptr, argv.data(), env.data()))
        throw std::runtime_error("Could not start update process.");
#endif
}
Child::~Child() {
#ifdef _WIN32
    if (impl->handle)
        CloseHandle(impl->handle);
#else
    if (impl->pid > 0) {
        int status{};
        waitpid(impl->pid, &status, WNOHANG);
    }
#endif
}
int Child::wait(unsigned timeout) {
    if (impl->result >= 0)
        return impl->result;
#ifdef _WIN32
    if (WaitForSingleObject(impl->handle, timeout) != WAIT_OBJECT_0)
        return -1;
    DWORD code = 1;
    if (!GetExitCodeProcess(impl->handle, &code))
        throw std::runtime_error("Could not inspect update process exit status.");
    // Windows exception exits have the high bit set. A negative result is
    // reserved for 'still running', never for an already-signalled child.
    impl->result = code <= DWORD(std::numeric_limits<int>::max()) ? int(code) : 1;
    return impl->result;
#else
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    do {
        int status{};
        auto result = waitpid(impl->pid, &status, WNOHANG);
        if (result == impl->pid) {
            impl->pid = -1;
            impl->result = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
            return impl->result;
        }
        if (result < 0)
            throw std::runtime_error("Could not inspect update process.");
        if (!timeout)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < end);
    return -1;
#endif
}
bool parent_alive(std::uint64_t pid) {
    if (pid == 0 || pid > 0x7fffffff)
        return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h)
        return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}
void wait_for_parent(std::uint64_t pid, const std::filesystem::path& ready) {
    if (pid == 0 || pid > 0x7fffffff)
        throw std::runtime_error("Invalid update parent.");
#ifdef _WIN32
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!parent)
        throw std::runtime_error("The update parent exited before the handoff was established.");
    struct Close {
        HANDLE h;
        ~Close() {
            CloseHandle(h);
        }
    } close{parent};
    durable_write(ready, "ready");
    if (WaitForSingleObject(parent, 120000) != WAIT_OBJECT_0)
        throw std::runtime_error("DKR-R did not close normally; no update files were installed.");
#else
    // Only a direct child's captured parent relationship is accepted. Once
    // reparented, do not probe/kill a potentially reused PID.
    if (std::uint64_t(getppid()) != pid)
        throw std::runtime_error("The update helper lost its parent before handoff.");
    durable_write(ready, "ready");
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::uint64_t(getppid()) == pid) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("DKR-R did not close normally; no update files were installed.");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif
}
} // namespace dkr::updates
