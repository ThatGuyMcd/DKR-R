#include "update_install.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
int main() {
    int argc = 0;
    auto** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide)
        return 2;
    std::vector<std::string> values;
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), n, nullptr, nullptr);
        text.pop_back();
        values.push_back(std::move(text));
    }
    LocalFree(wide);
    std::vector<char*> argv;
    for (auto& s : values)
        argv.push_back(s.data());
    return dkr::updates::updater_main(argc, argv.data());
}
#else
int main(int argc, char** argv) {
    return dkr::updates::updater_main(argc, argv);
}
#endif
