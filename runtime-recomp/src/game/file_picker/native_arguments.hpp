#pragma once
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
namespace dkr::file_picker {
// Windows' narrow CRT argv uses the user's ANSI code page. Decode the actual
// UTF-16 command line so a Unicode profile/temp directory survives the IPC.
inline std::vector<std::string> native_arguments(int argc, char** argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    auto** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        return result;
    for (int i = 0; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (!size) {
            result.clear();
            break;
        }
        std::string text(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, text.data(), size, nullptr, nullptr);
        text.pop_back();
        result.push_back(std::move(text));
    }
    LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i)
        result.emplace_back(argv[i]);
#endif
    return result;
}
} // namespace dkr::file_picker
