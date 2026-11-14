#include "file_picker/protocol.hpp"
#include <cassert>
#include <iostream>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
unsigned pumped = 0;
LRESULT CALLBACK picker_test_window(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 7) {
        ++pumped;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
#endif
int main(int argc, char** argv) {
    using namespace dkr::file_picker;
    if (argc != 2)
        return 2;
    Json request{{"schema", 1},
                 {"kind", "open"},
                 {"initial", ""},
                 {"name", ""},
                 {"filters", Json::array({{{"name", "Game"}, {"spec", "z64,n64"}}})}};
    auto result = exchange(std::filesystem::u8path(argv[1]), request, 5000);
    assert(result["status"] == "okay" && result["path"] == "/tmp/My Games/été 日本語.z64");
#ifndef _WIN32
    setenv("APPDIR", "/tmp/dkr-picker-test.AppDir", 1);
    setenv("LD_LIBRARY_PATH", "/tmp/dkr-picker-test.AppDir/usr/lib:/usr/lib:/vendor/driver", 1);
    result = exchange(std::filesystem::u8path(argv[1]), request, 5000);
    assert(result["loader"] == "/usr/lib:/vendor/driver");
    assert(std::string(std::getenv("LD_LIBRARY_PATH")) ==
           "/tmp/dkr-picker-test.AppDir/usr/lib:/usr/lib:/vendor/driver");
#endif
    request["name"] = "cancel";
    assert(exchange(std::filesystem::u8path(argv[1]), request, 5000)["status"] == "cancel");
#ifdef _WIN32
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = picker_test_window;
    window_class.lpszClassName = L"DKRPickerWaitTest";
    window_class.hInstance = GetModuleHandleW(nullptr);
    assert(RegisterClassW(&window_class));
    const auto window = CreateWindowW(window_class.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                      window_class.hInstance, nullptr);
    assert(window && PostMessageW(window, WM_APP + 7, 0, 0));
#endif
    for (const auto* mode : {"crash", "malformed", "timeout"}) {
        request["name"] = mode;
        bool failed = false;
        try {
            exchange(std::filesystem::u8path(argv[1]), request, std::string_view(mode) == "timeout" ? 50 : 5000);
        } catch (const std::exception&) {
            failed = true;
        }
        assert(failed);
    }
#ifdef _WIN32
    assert(pumped == 1);
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
#endif
    request["filters"] = Json::array();
    for (unsigned i = 0; i < 33; ++i)
        request["filters"].push_back({{"name", ""}, {"spec", ""}});
    bool failed = false;
    try {
        validate(request);
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed);
    std::cout << "File picker selection/cancellation/Unicode/crash/malformed/timeout checks passed.\n";
}
