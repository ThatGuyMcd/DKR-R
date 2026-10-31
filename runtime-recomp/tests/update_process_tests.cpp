#include "update_process.hpp"
#include "../src/game/application_layout.hpp"
#include "../src/game/runtime_log_handles.hpp"
#include "../src/game/runtime_log_archive.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace dkr::updates;
int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc == 4 && std::string_view(argv[1]) == "--child-log-handle") {
        const auto handle = reinterpret_cast<HANDLE>(std::stoull(argv[2]));
        BY_HANDLE_FILE_INFORMATION file{};
        if (!GetFileInformationByHandle(handle, &file))
            return 0;
        // The child may reuse the same handle number for a different object.
        const auto identity = std::to_string(file.dwVolumeSerialNumber) + ":" + std::to_string(file.nFileIndexHigh) +
                              ":" + std::to_string(file.nFileIndexLow);
        return identity == argv[3] ? 1 : 0;
    }
#endif
    if (argc >= 2 && std::string_view(argv[1]) == "--child-arguments") {
#ifdef _WIN32
        const bool unicode = std::wstring_view(GetCommandLineW()).find(L"caf\u00e9") != std::wstring_view::npos;
#else
        const bool unicode = argc == 5 && std::string_view(argv[4]) == "caf\xC3\xA9";
#endif
        return argc == 5 && std::string_view(argv[2]).empty() &&
                       std::string_view(argv[3]) == "quoted \"text\" with spaces\\" && unicode
                   ? 42
                   : 2;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--child-failure") {
#ifdef _WIN32
        ExitProcess(0xC0000005);
#else
        return 31;
#endif
    }
    auto root = std::filesystem::temp_directory_path() / ("dkrr-update-process-" + unique_id());
    std::filesystem::create_directory(root);
    using dkr::runtime::layout::windows_application_directory;
    assert(windows_application_directory(root / "DKR-R.exe") == root);
    assert(windows_application_directory(root / "DKR-R-Runtime.exe") == root);
    assert(windows_application_directory(root / "libexec/dkr-r/DKR-R-Runtime.exe") == root);
    assert(windows_application_directory(root / "LIBEXEC/DkR-R/DKR-R-Runtime.exe") == root);
    assert(windows_application_directory(root / "libexec/other/program.exe") == root / "libexec/other");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::filesystem::remove_all(root);
        }
    } cleanup{root};
    {
        InstallLock first(root, 100, true), second(root, 100, true);
        bool rejected = false;
        try {
            InstallLock exclusive(root, 100);
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
    }
    { InstallLock exclusive(root, 100); }
    // Lock storage is outside the application, including a read-only install.
#ifndef _WIN32
    std::filesystem::permissions(root, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    { InstallLock read_only(root, 100, true); }
    std::filesystem::permissions(root, std::filesystem::perms::owner_all);
#endif
    const auto log = root / "fixture.log";
#ifdef _WIN32
    FILE* stream = nullptr;
    _wfopen_s(&stream, log.c_str(), L"w");
#else
    FILE* stream = std::fopen(log.c_str(), "w");
#endif
    assert(stream && dkr::runtime::support::logging::protect_log(stream));
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream)));
    DWORD flags = HANDLE_FLAG_INHERIT;
    assert(GetHandleInformation(handle, &flags) && !(flags & HANDLE_FLAG_INHERIT));
    // Even an explicitly inheritable CRT handle must not leak into an updater.
    assert(SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT));
    BY_HANDLE_FILE_INFORMATION file{};
    assert(GetFileInformationByHandle(handle, &file));
    const auto identity = std::to_string(file.dwVolumeSerialNumber) + ":" + std::to_string(file.nFileIndexHigh) + ":" +
                          std::to_string(file.nFileIndexLow);
    Child log_child(application_executable(),
                    {"--child-log-handle", std::to_string(reinterpret_cast<std::uintptr_t>(handle)), identity}, root);
    assert(log_child.wait(10000) == 0);
#else
    assert((fcntl(fileno(stream), F_GETFD) & FD_CLOEXEC) != 0);
#endif
    std::fclose(stream);
    const auto first_log = dkr::runtime::support::logging::fallback_log(root);
    const auto second_log = dkr::runtime::support::logging::fallback_log(root);
    assert(first_log != second_log && first_log.parent_path() == root / "history");
    assert(dkr::runtime::support::history::owned_name(first_log.filename().string()));
    assert(dkr::runtime::support::history::owned_name(second_log.filename().string()));
    // An unusable archive directory must not produce a path that truncates the current log.
    assert(dkr::runtime::support::logging::fallback_log(log).empty());
    Child arguments(application_executable(), {"--child-arguments", "", "quoted \"text\" with spaces\\", "caf\xC3\xA9"},
                    root);
    assert(arguments.wait(10000) == 42);
    assert(arguments.wait(0) == 42);
    Child failure(application_executable(), {"--child-failure"}, root);
    assert(failure.wait(10000) > 0);
    assert(failure.wait(0) > 0);
    std::cout << "PASS: multiple-instance/read-only locks, quoted/Unicode/empty arguments and "
                 "crash exits, isolated log handles and fallback log names\n";
}
