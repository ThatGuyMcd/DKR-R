#include "update_process.hpp"
#include "../src/game/application_layout.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace dkr::updates;
int main(int argc, char** argv) {
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
    Child arguments(application_executable(), {"--child-arguments", "", "quoted \"text\" with spaces\\", "caf\xC3\xA9"},
                    root);
    assert(arguments.wait(10000) == 42);
    assert(arguments.wait(0) == 42);
    Child failure(application_executable(), {"--child-failure"}, root);
    assert(failure.wait(10000) > 0);
    assert(failure.wait(0) > 0);
    std::cout << "PASS: multiple-instance/read-only locks, quoted/Unicode/empty arguments and "
                 "crash exits\n";
}
