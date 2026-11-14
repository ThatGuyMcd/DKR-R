#include "crash_reports/native_context.hpp"
#include <csignal>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <signal.h>
#endif
int main(int argc, char**) {
#ifdef _WIN32
    if (!SymInitialize(GetCurrentProcess(), nullptr, TRUE))
        return 2;
#endif
    dkr::crash_reports::refresh_native_context(true);
    dkr::crash_reports::print_native_frame(reinterpret_cast<std::uintptr_t>(&main), 1);
#ifdef _WIN32
    dkr::crash_reports::print_native_frame(
        reinterpret_cast<std::uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessId")), 2);
    SymCleanup(GetCurrentProcess());
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc > 1) {
        struct sigaction action {};
        action.sa_sigaction = [](int signal, siginfo_t* info, void* context) {
            dkr::crash_reports::fatal_signal(signal, info, context);
        };
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, nullptr);
        raise(SIGSEGV);
    }
#else
    (void)argc;
#endif
}
