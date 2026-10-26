#define WIN32_LEAN_AND_MEAN
#include "update_process.hpp"
#include "../application_layout.hpp"
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    using namespace dkr::updates;
    try {
        auto root = application_directory();
        // Recovery requires exclusive access; normal play only a shared lock,
        // so multiple local instances remain possible.
        if (pending_files(root)) {
            auto* token = std::getenv("DKR_UPDATE_TOKEN");
            if (!token || !updated_launch_authorized(root, token)) {
                InstallLock recovery(root, 5000);
                if (!finalize_healthy_update(root)) {
                    auto state = transaction_state(root);
                    if (state != "committed" && state != "recovered")
                        recover_files(root);
                    archive_transaction(root);
                }
            }
        }
        InstallLock running(root, 5000, true);
        int argc = 0;
        auto** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!wide)
            return 2;
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string text(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), n, nullptr, nullptr);
            text.pop_back();
            arguments.push_back(std::move(text));
        }
        LocalFree(wide);
        auto runtime = dkr::runtime::layout::windows_runtime(root);
        if (!std::filesystem::is_regular_file(runtime))
            runtime = root / "DKR-R-Runtime.exe"; // Recover/launch preceding flat-layout candidates.
        Child game(runtime, arguments, root, true);
        for (;;) {
            int result = game.wait(1000);
            if (result >= 0)
                return result;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[updates][bootstrap] %s\n", error.what());
        if (MessageBoxW(nullptr, L"DKR-R couldn't start. Show details?", L"DKR-R couldn't start",
                        MB_YESNO | MB_ICONERROR) == IDYES) {
            const int length = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
            std::wstring detail(static_cast<std::size_t>(length > 0 ? length : 1), L'\0');
            if (length > 0)
                MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, detail.data(), length);
            else
                detail = L"No additional details.";
            MessageBoxW(nullptr, detail.c_str(), L"DKR-R startup details", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
