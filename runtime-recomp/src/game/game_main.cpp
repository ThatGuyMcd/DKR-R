#include "game_registration.hpp"
#include "revision_addresses.hpp"
#include "null_renderer.hpp"
#include "rev_a_asset_mutex.hpp"
#include "runtime_magic_codes.hpp"
#include "runtime_platform.hpp"
#include "runtime_portable.hpp"
#include "runtime_netplay.hpp"
#include "runtime_support.hpp"
#include "save_manager.hpp"
#include "runtime_save_routing.hpp"
#include "startup_performance.hpp"
#include "virtual_pak.hpp"
#include "runtime_legacy_mods.hpp"
#include "host_task_lifetime.hpp"
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "replay_probe_capture.hpp"
#include "runtime_enhancements.hpp"
#include "netplay/experimental_runtime.hpp"
#include <cwchar>
#endif
#if defined(__ANDROID__)
#include "graphics_health.hpp"
#include "../android/android_platform.hpp"
#endif
#if DKR_LEGACY_QUALIFICATION
#include "legacy_runtime_qualification.hpp"
#endif
#if DKR_RUNTIME_HAS_RT64
#include "custom_tracks.hpp"
#include "rt64_renderer.hpp"
#include "runtime_texture_packs.hpp"
#include "runtime_ui.hpp"
#include "runtime_hud_layout.hpp"
#include <SDL.h>
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <cerrno>
#if defined(__linux__) && !defined(__ANDROID__)
#include <execinfo.h>
#endif
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "netplay/experimental_arguments.hpp"
#endif
#endif

extern RspUcodeFunc dkrAspMain;

namespace {

#ifdef _WIN32
std::atomic_flag g_crash_filter_active = ATOMIC_FLAG_INIT;
std::filesystem::path g_crash_directory;
#endif

bool ConfigurePersistentRuntimeLog(
    const std::filesystem::path& config_directory) {
    if (!dkr::runtime::support::diagnostic_logging_enabled()) {
        return true;
    }
    std::error_code error;
    const std::filesystem::path& log_directory =
        dkr::runtime::support::log_directory();
    std::filesystem::create_directories(log_directory, error);
    if (error) {
        return false;
    }
    const std::filesystem::path current = log_directory / "runtime.log";
    const std::filesystem::path previous =
        log_directory / "runtime-previous.log";
    std::filesystem::remove(previous, error);
    error.clear();
    if (std::filesystem::exists(current, error)) {
        error.clear();
        std::filesystem::rename(current, previous, error);
    }
#ifdef _WIN32
    FILE* stream = nullptr;
    if (_wfreopen_s(&stream, current.c_str(), L"w", stderr) != 0 ||
        stream == nullptr) {
        return false;
    }
#else
    if (std::freopen(current.c_str(), "w", stderr) == nullptr) {
        return false;
    }
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#if defined(__ANDROID__)
    // RT64 writes GPU/driver identification to stdout. Keep it in the same
    // private log as startup errors so phone users can export both together.
    dup2(STDERR_FILENO, STDOUT_FILENO);
#endif
    std::fprintf(stderr, "[boot] DKR-R %s persistent runtime log\n",
                 DKR_RELEASE_VERSION);
    return true;
}

std::filesystem::path DefaultConfigDirectory(const char* executable_argument) {
    (void)executable_argument;
    const auto& executable_directory = dkr::runtime::portable::application_directory();
    if (dkr::runtime::portable::enabled_at_startup()) {
        return executable_directory / "dkr-runtime-data";
    }
#if defined(_WIN32)
    if (const char* app_data = std::getenv("APPDATA"); app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "DKRPort";
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
        xdg_config != nullptr && *xdg_config != '\0') {
        return std::filesystem::path(xdg_config) / "dkr-port";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "dkr-port";
    }
#endif
    return executable_directory / "dkr-runtime-data";
}

std::vector<std::filesystem::path> RecoverySaveLocations(const char* executable_argument) {
    // Read-only, bounded discovery of the known legacy locations. In particular
    // do not search the user's home directory or every previous download folder.
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    const auto executable = std::filesystem::absolute(std::filesystem::u8path(executable_argument), ec);
    if (!ec) roots.push_back(executable.parent_path() / "dkr-runtime-data");
#if defined(_WIN32)
    if (const char* app_data = std::getenv("APPDATA"); app_data && *app_data)
        roots.emplace_back(std::filesystem::path(app_data) / "DKRPort");
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        roots.emplace_back(std::filesystem::path(xdg) / "dkr-port");
    if (const char* user_home = std::getenv("HOME"); user_home && *user_home)
        roots.emplace_back(std::filesystem::path(user_home) / ".config" / "dkr-port");
    if (const char* app_image = std::getenv("APPIMAGE"); app_image && *app_image)
        roots.emplace_back(std::filesystem::path(app_image).parent_path() / "dkr-runtime-data");
#endif
    const auto working = std::filesystem::current_path(ec);
    if (!ec) roots.push_back(working); // only known save subdirectories are scanned
    return roots;
}

RspExitReason EmptyAudioTask(std::uint8_t*, std::uint32_t) {
    return RspExitReason::Broke;
}

RspUcodeFunc* GetRspMicrocode(const OSTask* task) {
    if (task->t.type == M_AUDTASK &&
        task->t.ucode == dkr::runtime::revision_addresses::AspMainTextStart) {
        // DKR can submit a zero-command audio frame when the host-reported AI
        // queue already satisfies the synthesizer's requested frame size. The
        // original scheduler treats that as completed work; entering the ABI
        // dispatcher with a zero-byte list would DMA and execute stale memory.
        if (task->t.data_size == 0) {
            return EmptyAudioTask;
        }
        return +[](std::uint8_t* rdram, std::uint32_t ucode_address) {
            return dkrAspMain(rdram, ucode_address);
        };
    }
    std::fprintf(stderr,
                 "[boot][rsp] unsupported task type=%u flags=0x%08X "
                 "ucode=0x%08X ucode_size=%u ucode_data=0x%08X data=0x%08X data_size=%u\n",
                 task->t.type, task->t.flags, task->t.ucode, task->t.ucode_size,
                 task->t.ucode_data, task->t.data_ptr, task->t.data_size);
    return nullptr;
}

void MessageBox(const char* message) {
    std::fprintf(stderr, "[boot][runtime-error] %s\n", message);
#if defined(__ANDROID__)
    const auto failure = dkr::runtime::graphics_health::failure.load();
    if (failure == 0) dkr::runtime::android::report_graphics_failure(message);
#endif
}

std::string GetThreadName(const OSThread* thread) {
    return "DKR-" + std::to_string(thread->id);
}

#ifdef _WIN32
void WriteWindowsMinidump(EXCEPTION_POINTERS* exception) {
    if (!dkr::runtime::support::crash_dumps_enabled() ||
        g_crash_directory.empty()) {
        return;
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t name[96]{};
    swprintf_s(name, L"DKR-R-crash-%04u%02u%02u-%02u%02u%02u.dmp",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    const std::filesystem::path path = g_crash_directory / name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;
    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file,
        MiniDumpWithIndirectlyReferencedMemory,
        exception != nullptr ? &information : nullptr, nullptr, nullptr);
    CloseHandle(file);
    std::fprintf(stderr, "[boot][crash] minidump=%ls status=%s\n",
                 path.c_str(), written ? "written" : "failed");
}

LONG WINAPI RuntimeCrashFilter(EXCEPTION_POINTERS* exception) {
    if (g_crash_filter_active.test_and_set()) {
        Sleep(5000);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    const DWORD64 fault_address = reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress);
    const DWORD64 module_base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[boot][crash] exception=0x%08lX address=0x%016llX\n",
                 exception->ExceptionRecord->ExceptionCode,
                 static_cast<unsigned long long>(fault_address));
    WriteWindowsMinidump(exception);
    if (exception->ExceptionRecord->NumberParameters >= 2U) {
        const ULONG_PTR operation = exception->ExceptionRecord->ExceptionInformation[0];
        const char* operation_name = operation == 0U ? "read" :
            operation == 1U ? "write" : operation == 8U ? "execute" : "unknown";
        std::fprintf(stderr,
                     "[boot][crash] memory-operation=%s(%llu) "
                     "memory-address=0x%016llX\n",
                     operation_name,
                     static_cast<unsigned long long>(operation),
                     static_cast<unsigned long long>(
                         exception->ExceptionRecord->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "[boot][crash] module-base=0x%016llX rva=0x%llX\n",
                 static_cast<unsigned long long>(module_base),
                 static_cast<unsigned long long>(fault_address - module_base));
    std::fprintf(stderr,
                 "[boot][crash] registers rcx=0x%016llX rdx=0x%016llX "
                 "r8=0x%016llX r9=0x%016llX rsp=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rcx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdx),
                 static_cast<unsigned long long>(exception->ContextRecord->R8),
                 static_cast<unsigned long long>(exception->ContextRecord->R9),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsp));
    std::fprintf(stderr,
                 "[boot][crash] registers rax=0x%016llX rbx=0x%016llX "
                 "rbp=0x%016llX rsi=0x%016llX rdi=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rax),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbp),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsi),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdi));
    std::fprintf(stderr,
                 "[boot][crash] registers r10=0x%016llX r11=0x%016llX "
                 "r12=0x%016llX r13=0x%016llX r14=0x%016llX "
                 "r15=0x%016llX rip=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->R10),
                 static_cast<unsigned long long>(exception->ContextRecord->R11),
                 static_cast<unsigned long long>(exception->ContextRecord->R12),
                 static_cast<unsigned long long>(exception->ContextRecord->R13),
                 static_cast<unsigned long long>(exception->ContextRecord->R14),
                 static_cast<unsigned long long>(exception->ContextRecord->R15),
                 static_cast<unsigned long long>(exception->ContextRecord->Rip));

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (unsigned index = 0; index < 32; ++index) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                         &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            std::fprintf(stderr, "[boot][crash] #%u %s+0x%llX\n", index, symbol->Name,
                         static_cast<unsigned long long>(displacement));
        } else {
            std::fprintf(stderr, "[boot][crash] #%u 0x%016llX\n", index,
                         static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
            std::fprintf(stderr, "[boot][crash]     %s:%lu+0x%lX\n",
                         line.FileName, line.LineNumber, line_displacement);
        }
    }
    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

#else

void RuntimeSignalHandler(int signal_number) {
    std::fprintf(stderr, "[boot][crash] signal=%d\n", signal_number);
#if defined(__linux__) && !defined(__ANDROID__)
    void* frames[48]{};
    const int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
    std::fflush(stderr);
    _exit(128 + signal_number);
}

void InstallRuntimeSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = RuntimeSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (const int signal_number : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) {
        sigaction(signal_number, &action, nullptr);
    }
}

#endif

struct LaunchOptions {
    std::filesystem::path rom_path;
    std::filesystem::path config_directory;
    unsigned timeout_seconds = 0;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    std::filesystem::path bootstrap_output;
    std::filesystem::path owned_check_invite;
    bool owned_check_host=false;
    unsigned owned_check_players=2;
#endif
};

bool ParseLaunchOptions(int argc, char** argv, LaunchOptions& options,
                        std::string& error) {
    unsigned positional_index = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto require_value = [&](const char* option) -> const char* {
            if (index + 1 >= argc) {
                error = std::string(option) + " requires a value.";
                return nullptr;
            }
            return argv[++index];
        };

        if (argument == "--rom") {
            const char* value = require_value("--rom");
            if (value == nullptr) {
                return false;
            }
            options.rom_path = std::filesystem::u8path(value);
        } else if (argument == "--config") {
            const char* value = require_value("--config");
            if (value == nullptr) {
                return false;
            }
            options.config_directory = std::filesystem::u8path(value);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        } else if (argument == "--rollback-bootstrap-output") {
            const char* value=require_value("--rollback-bootstrap-output");
            if (!value) return false;
            options.bootstrap_output=std::filesystem::u8path(value);
        } else if(argument=="--self-test-owned-host"||argument=="--self-test-owned-join") {
            const char* value=require_value(argv[index]);if(!value)return false;
            if(!options.owned_check_invite.empty()){error="Only one owned check role is allowed.";return false;}
            options.owned_check_host=argument=="--self-test-owned-host";
            options.owned_check_invite=std::filesystem::u8path(value);
        } else if(argument=="--self-test-owned-players") {
            const char* value=require_value("--self-test-owned-players");if(!value)return false;
            if(value[0]<'2'||value[0]>'4'||value[1]){error="Owned check requires 2, 3 or 4 players.";return false;}
            options.owned_check_players=unsigned(value[0]-'0');
#endif
        } else if (argument == "--timeout") {
            const char* value = require_value("--timeout");
            if (value == nullptr) {
                return false;
            }
            try {
                const unsigned long parsed = std::stoul(value);
                if (parsed > std::numeric_limits<unsigned>::max()) {
                    throw std::out_of_range("timeout");
                }
                options.timeout_seconds = static_cast<unsigned>(parsed);
            } catch (...) {
                error = "--timeout requires a non-negative whole number.";
                return false;
            }
        } else if (argument.starts_with("--")) {
            error = "Unknown DKR-R option: " + std::string(argument);
            return false;
        } else {
            // Preserve the original positional invocation for developer and
            // diagnostic scripts: ROM, config directory, optional timeout.
            if (positional_index == 0U) {
                options.rom_path = std::filesystem::u8path(argv[index]);
            } else if (positional_index == 1U) {
                options.config_directory = std::filesystem::u8path(argv[index]);
            } else if (positional_index == 2U) {
                try {
                    const unsigned long parsed = std::stoul(argv[index]);
                    if (parsed > std::numeric_limits<unsigned>::max()) {
                        throw std::out_of_range("timeout");
                    }
                    options.timeout_seconds = static_cast<unsigned>(parsed);
                } catch (...) {
                    error = "The timeout must be a non-negative whole number.";
                    return false;
                }
            } else {
                error = "Too many positional arguments.";
                return false;
            }
            ++positional_index;
        }
    }
    if (options.config_directory.empty()) {
        options.config_directory = DefaultConfigDirectory(argv[0]);
    }
    return true;
}

bool PrepareCanonicalRomPath(std::filesystem::path& rom_path,
                             dkr::runtime::rom::Identity& identity,
                             const std::filesystem::path& config_directory,
                             std::string& error) {
    if (identity.byte_order == dkr::runtime::rom::ByteOrder::BigEndian) {
        return true;
    }
    std::filesystem::path canonical_path;
    if (!dkr::runtime::rom::materialize_canonical(
            rom_path, identity, config_directory / "rom-cache",
            canonical_path, error)) {
        return false;
    }
    rom_path = canonical_path;
    identity = dkr::runtime::rom::inspect(rom_path);
    if (!identity.supported() ||
        identity.byte_order != dkr::runtime::rom::ByteOrder::BigEndian) {
        error = "The prepared ROM cache did not retain the selected revision.";
        return false;
    }
    std::fprintf(stderr,
                 "[boot][rom] normalised selected ROM into the local big-endian cache\n");
    error.clear();
    return true;
}

} // namespace

bool RelaunchApplication(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command_line = GetCommandLineW();
    if (command_line.empty() ||
        !CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "[boot][restart] CreateProcessW failed: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::absolute(std::filesystem::u8path(argv[0]), error);
    const std::string executable_utf8 = error
        ? std::string(argv[0])
        : executable.string();
    execv(executable_utf8.c_str(), argv);
    std::fprintf(stderr, "[boot][restart] execv failed: errno=%d\n", errno);
    return false;
#endif
}

int DkrMain(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dkr::runtime::portable::configure(argv[0]);
#if defined(__ANDROID__)
    std::set_terminate([] {
        // Preserve the exception's cause in the user-exportable log, then let
        // Android capture the native tombstone through its own signal handler.
        try {
            if (const auto failure = std::current_exception()) std::rethrow_exception(failure);
            std::fprintf(stderr, "[boot][android] terminate without an active exception\n");
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[boot][android] unhandled exception: %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "[boot][android] unhandled non-standard exception\n");
        }
        std::fflush(stderr);
        std::abort();
    });
#endif
    dkr::runtime::startup_performance::mark("process-entry");
#if defined(_WIN32)
    SetUnhandledExceptionFilter(RuntimeCrashFilter);
#endif

    if (argc >= 2 && std::string_view(argv[1]) == "--self-test-pak") {
        const std::filesystem::path test_directory = argc >= 3
            ? std::filesystem::u8path(argv[2])
            : DefaultConfigDirectory(argv[0]) / "pak-self-test";
        std::string error;
        if (!dkr::runtime::pak::self_test(test_directory, error)) {
            std::fprintf(stderr, "[test][pak] FAILED: %s\n", error.c_str());
            return 1;
        }
        std::fprintf(stderr, "[test][pak] PASS: round-trip and backup recovery\n");
        return 0;
    }

#if DKR_RUNTIME_HAS_RT64
    if (argc >= 3 && std::string_view(argv[1]) == "--self-test-hud-settings") {
        const bool passed = dkr::runtime::hud::self_test_basic_settings(std::filesystem::u8path(argv[2]));
        std::fprintf(stderr,"[test][hud-settings] %s: preset switching, restart, custom suppression and save failure\n",passed?"PASS":"FAILED");
        return passed ? 0 : 1;
    }
    if (argc >= 2 &&
        std::string_view(argv[1]) == "--self-test-input-switch") {
        const std::filesystem::path test_directory = argc >= 3
            ? std::filesystem::u8path(argv[2])
            : DefaultConfigDirectory(argv[0]) / "input-switch-self-test";
        std::filesystem::create_directories(test_directory);
        dkr::runtime::platform::configure_input(test_directory);
        dkr::runtime::platform::set_requested_input_backend(
            dkr::runtime::platform::InputBackend::SDL2Compatibility);
        if (!dkr::runtime::platform::initialise()) {
            std::fprintf(stderr,
                         "[test][input-switch] FAILED: platform initialization\n");
            return 1;
        }
        constexpr Uint32 kInvariantSubsystems = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
        constexpr Uint32 kControllerSubsystems =
            SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_SENSOR;
        const auto fail = [](const char* reason) {
            std::fprintf(stderr, "[test][input-switch] FAILED: %s (%s)\n",
                         reason,
                         dkr::runtime::platform::input_backend_detail().c_str());
            dkr::runtime::platform::shutdown();
            return 1;
        };
        const auto wait_for_backend = [](auto expected) {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(5);
            do {
                dkr::runtime::platform::pump_input_backend_events();
                if (dkr::runtime::platform::active_input_backend() == expected &&
                    !dkr::runtime::platform::input_backend_switch_pending()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            return false;
        };
        for (int cycle = 0; cycle < 2; ++cycle) {
            dkr::runtime::platform::set_requested_input_backend(
                dkr::runtime::platform::InputBackend::SDL3Native);
            if (!wait_for_backend(
                    dkr::runtime::platform::InputBackend::SDL3Native)) {
                return fail("SDL2 to SDL3 handover");
            }
            if ((SDL_WasInit(kInvariantSubsystems) & kInvariantSubsystems) !=
                kInvariantSubsystems) {
                return fail("video or audio subsystem changed during SDL3 handover");
            }
            if ((SDL_WasInit(kControllerSubsystems) &
                 kControllerSubsystems) != 0U) {
                return fail("SDL2 retained controller subsystem ownership");
            }

            dkr::runtime::platform::set_requested_input_backend(
                dkr::runtime::platform::InputBackend::SDL2Compatibility);
            if (!wait_for_backend(
                    dkr::runtime::platform::InputBackend::SDL2Compatibility)) {
                return fail("SDL3 to SDL2 handover");
            }
            if ((SDL_WasInit(kInvariantSubsystems) & kInvariantSubsystems) !=
                kInvariantSubsystems) {
                return fail("video or audio subsystem changed during SDL2 handover");
            }
            if ((SDL_WasInit(kControllerSubsystems) &
                 kControllerSubsystems) != kControllerSubsystems) {
                return fail("SDL2 controller subsystems were not restored");
            }
        }
        dkr::runtime::platform::shutdown();
        std::fprintf(stderr,
                     "[test][input-switch] PASS: two live round trips\n");
        return 0;
    }
#endif

#if DKR_RUNTIME_HAS_RT64
    if (argc == 4 && std::string_view(argv[1]) == "--self-test-rice-pack") {
        const std::filesystem::path source = std::filesystem::u8path(argv[2]);
        const std::filesystem::path test_directory = std::filesystem::u8path(argv[3]);
        dkr::runtime::texture_packs::configure(test_directory);
        std::string status;
        if (!dkr::runtime::texture_packs::import_archive(source, status)) {
            std::fprintf(stderr, "[test][rice] FAILED: %s\n", status.c_str());
            return 1;
        }
        const auto packs = dkr::runtime::texture_packs::snapshot();
        if (packs.size() != 1 || !packs.front().compatible ||
            packs.front().format != dkr::runtime::texture_packs::Format::RiceRt64) {
            std::fprintf(stderr, "[test][rice] FAILED: converted pack did not validate natively.\n");
            return 1;
        }
        const std::string pack_id = packs.front().id;
        const std::filesystem::path managed_path = packs.front().path;
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, true, status) ||
            !dkr::runtime::texture_packs::snapshot().empty()) {
            std::fprintf(stderr, "[test][rice] FAILED: hide-from-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        const auto hidden_packs = dkr::runtime::texture_packs::snapshot(true);
        if (hidden_packs.size() != 1 || !hidden_packs.front().hidden) {
            std::fprintf(stderr, "[test][rice] FAILED: hidden pack was not retained for restoration.\n");
            return 1;
        }
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, false, status) ||
            dkr::runtime::texture_packs::snapshot().size() != 1) {
            std::fprintf(stderr, "[test][rice] FAILED: restore-to-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        if (!dkr::runtime::texture_packs::delete_managed(pack_id, status) ||
            !dkr::runtime::texture_packs::snapshot(true).empty() ||
            std::filesystem::exists(managed_path)) {
            std::fprintf(stderr, "[test][rice] FAILED: permanent managed deletion failed: %s\n",
                         status.c_str());
            return 1;
        }
        std::fprintf(stderr,
                     "[test][rice] PASS: import, hide, restore and permanent deletion\n");
        return 0;
    }
#endif

    LaunchOptions launch{};
    std::string rom_error;
    if (!ParseLaunchOptions(argc, argv, launch, rom_error)) {
        std::fprintf(stderr,
                     "Usage: DKR-R [rom.z64] [config-directory] [timeout-seconds]\n"
                     "       DKR-R --rom <path> [--config <path>] [--timeout <seconds>]\n"
                     "[boot][arguments] %s\n", rom_error.c_str());
        return 2;
    }
    std::filesystem::path& rom_path = launch.rom_path;
    std::shared_ptr<const dkr::mods::PreparedModLaunch> prepared_mods;
    // Resolve an explicitly selected profile root once. Descendant save paths
    // remain subject to the no-link policy; ordinary Android storage aliases
    // and portable roots must not accidentally select another profile later.
    const std::filesystem::path config_directory = std::filesystem::weakly_canonical(launch.config_directory);
    const unsigned timeout_seconds = launch.timeout_seconds;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    const bool rollback_bootstrap = !launch.bootstrap_output.empty();
    const bool owned_check=!launch.owned_check_invite.empty();
    if(owned_check&&(rollback_bootstrap||rom_path.empty()||!config_directory.is_absolute()||
        std::filesystem::exists(config_directory)||!launch.owned_check_invite.is_absolute()||
        timeout_seconds<20||timeout_seconds>180)) {
        std::fprintf(stderr,"[rollback][check] Requires a ROM, NEW absolute profile, absolute invitation file, and a 20-180 second timeout.\n");return 2;
    }
    if (rollback_bootstrap) {
        // Reject BEFORE any config/save/profile writes. Only a fresh directory
        // beside the requested image is accepted, never a normal user profile.
        if (rom_path.empty() || !config_directory.is_absolute() ||
            std::filesystem::exists(config_directory) ||
            launch.bootstrap_output.lexically_normal().parent_path()!=config_directory.lexically_normal() ||
            !dkr_experimental_bootstrap_arm(launch.bootstrap_output,rom_error)) {
            std::fprintf(stderr,"[rollback-test][prepare] Refusing nonisolated bootstrap directory: %s\n",rom_error.c_str());
            return 2;
        }
    }
#endif
    std::filesystem::create_directories(config_directory);
    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "support-configure");
        dkr::runtime::support::configure(config_directory);
    }
    bool log_configured = ConfigurePersistentRuntimeLog(config_directory);
    if (!log_configured) {
        std::fprintf(stderr,
                     "[boot][log] could not create the persistent runtime log\n");
    }
    dkr::runtime::startup_performance::mark("persistent-log-ready");
    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "rom-identity-cache-configure");
        dkr::runtime::rom::configure_identity_cache(config_directory);
    }
#if defined(_WIN32)
    g_crash_directory = dkr::runtime::support::crash_dump_directory();
    if (dkr::runtime::support::crash_dumps_enabled()) {
        std::error_code crash_directory_error;
        std::filesystem::create_directories(g_crash_directory,
                                            crash_directory_error);
        if (crash_directory_error) {
            g_crash_directory.clear();
        }
    }
#endif
#if !defined(_WIN32) && !defined(__ANDROID__)
    InstallRuntimeSignalHandlers();
#endif
    // Android keeps its own fatal-signal handler: replacing it with _exit
    // suppresses the native tombstone available through ApplicationExitInfo.

    dkr::runtime::rom::Identity rom_identity{};
    bool rom_identified = false;
    if (!rom_path.empty()) {
        if (!dkr::runtime::ValidateRomForLauncher(
                rom_path, rom_identity, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            return 3;
        }
        if (!PrepareCanonicalRomPath(rom_path, rom_identity,
                                     config_directory, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            return 3;
        }
        rom_identified = true;
    }

    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "pak-save-input-configure");
        dkr::runtime::pak::configure(config_directory);
        dkr::runtime::saves::configure(config_directory);
        dkr::runtime::saves::configure_recovery_locations(RecoverySaveLocations(argv[0]));
        dkr::runtime::platform::configure_input(config_directory);
#if DKR_RUNTIME_HAS_RT64
        // Custom tracks are user content beside the other imported assets, so
        // they live in the configuration directory rather than next to the
        // executable. Scanning here keeps the registry populated before the
        // first asset table load reaches the Patch Pipeline hooks.
        //
        // Deliberately not "mods": librecomp owns that directory for its own
        // .nrm mod format and reports "Mod is missing a mod.json" for anything
        // else it finds there, which would surface as an error for every user
        // who installs a track.
        dkr::runtime::custom_tracks::scan(config_directory / "custom-tracks");
#endif
    }

    {
        dkr::runtime::startup_performance::ScopedPhase phase(
            "platform-initialise");
        if (!dkr::runtime::platform::initialise()) {
            return 4;
        }
    }
    // N64ModernRuntime intentionally leaves its process-wide graphics options
    // value-initialized for applications with a settings frontend. Supply
    // parity-first defaults here so RT64 does not silently remain at 320x240.
    ultramodern::renderer::GraphicsConfig graphics_config{};
    graphics_config.developer_mode = false;
    graphics_config.res_option = ultramodern::renderer::Resolution::Auto;
    graphics_config.wm_option = ultramodern::renderer::WindowMode::Windowed;
    graphics_config.hr_option = ultramodern::renderer::HUDRatioMode::Original;
    graphics_config.api_option = ultramodern::renderer::GraphicsApi::Auto;
    graphics_config.ar_option = ultramodern::renderer::AspectRatio::Original;
    graphics_config.msaa_option = ultramodern::renderer::Antialiasing::None;
    graphics_config.rr_option = ultramodern::renderer::RefreshRate::Original;
    graphics_config.hpfb_option =
        ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    graphics_config.rr_manual_value = 30;
    graphics_config.ds_option = 1;
#if defined(__ANDROID__)
    // First-run mobile budget. ui::configure below restores saved settings.
    graphics_config.res_option = ultramodern::renderer::Resolution::Original2x;
    graphics_config.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Off;
#endif
    ultramodern::renderer::set_graphics_config(graphics_config);

#if DKR_RUNTIME_HAS_RT64
    {
        dkr::runtime::startup_performance::ScopedPhase phase("ui-configure");
        dkr::runtime::ui::configure(config_directory);
    }
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (rollback_bootstrap) {
        dkr::runtime::enhancements::set_presentation_profile(dkr::runtime::enhancements::PresentationProfile::Accurate);
        graphics_config.res_option=ultramodern::renderer::Resolution::Original2x;
        graphics_config.api_option=ultramodern::renderer::GraphicsApi::Vulkan;
        ultramodern::renderer::set_graphics_config(graphics_config);
        dkr::runtime::platform::set_master_volume(0.0F);
    }
#endif
    dkr::runtime::ui::reset_lifecycle_request();
    const auto window_started_at =
        dkr::runtime::startup_performance::Clock::now();
    auto window_handle = dkr::runtime::platform::create_window();
    dkr::runtime::startup_performance::report("window-create",
                                               window_started_at);
#if defined(_WIN32) || defined(__APPLE__)
    if (window_handle.window == nullptr) {
#else
    if (window_handle == nullptr) {
#endif
        std::fprintf(stderr, "[boot][window] failed to create the DKR-R window\n");
        dkr::runtime::platform::shutdown();
        return 4;
    }
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if(owned_check) {
        auto& online=dkr::runtime::netplay::session();
        if(!dkr::runtime::ui::configure_owned_online_check(rom_identity,launch.owned_check_host,launch.owned_check_players,rom_error)) {
            std::fprintf(stderr,"[rollback][check] %s\n",rom_error.c_str());dkr::runtime::platform::shutdown();return 5;
        }
        bool joined=launch.owned_check_host,invitation_written=false,ready=false,started=false,accepted=false;
        auto report_at=std::chrono::steady_clock::now();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(std::chrono::steady_clock::now()<deadline&&!accepted) {
            dkr::runtime::platform::pump_window_events(nullptr);
            const auto state=online.view();
            if(std::chrono::steady_clock::now()>=report_at) {
                std::fprintf(stderr,"[rollback][check] lobby state=%d local=%u status=%s\n",int(state.state),unsigned(state.local_slot),state.status.c_str());
                report_at=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            }
            if(state.state==dkr::runtime::netplay::ConnectionState::Failed){rom_error=state.status;break;}
            if(launch.owned_check_host&&!invitation_written&&dkr::runtime::netplay::valid_quick_join_code(state.invite)) {
                std::ofstream file(launch.owned_check_invite,std::ios::trunc);file<<state.invite;file.close();
                if(!file){rom_error="Cannot write the isolated invitation.";break;}invitation_written=true;
            }
            if(!joined) {
                std::ifstream file(launch.owned_check_invite);std::string invitation;file>>invitation;
                if(dkr::runtime::netplay::valid_quick_join_code(invitation)) {
                    if(!online.join(invitation,"Owned check client",rom_error))break;joined=true;
                }
            }
            if(launch.owned_check_host)for(const auto& pending:state.pending_joins)if(!online.approve_join(pending.request_id,rom_error))break;
            // Roster/save admission can clear Ready after the first call.
            ready=state.local_slot<state.room.players.size()&&state.room.players[state.local_slot].ready;
            if(!ready&&(state.state==dkr::runtime::netplay::ConnectionState::Hosting||state.state==dkr::runtime::netplay::ConnectionState::Lobby)) {
                ready=online.set_ready(true,rom_error);
            }
            if(launch.owned_check_host&&ready&&!started) {
                unsigned occupants=0;bool all_ready=true;
                for(const auto& racer:state.room.players)if(racer.occupied){++occupants;all_ready=all_ready&&racer.ready;}
                if(occupants==launch.owned_check_players&&all_ready)started=online.request_start(rom_error);
            }
            accepted=online.consume_launch_request();
            if(!accepted)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(!accepted){std::fprintf(stderr,"[rollback][check] normal lobby launch failed: %s\n",rom_error.c_str());online.disconnect();dkr::runtime::platform::shutdown();return 5;}
        std::fprintf(stderr,"[rollback][check] normal Quick Join/countdown accepted; owners=%u\n",launch.owned_check_players);
    }
#endif
    if (rom_path.empty()) {
        const auto startup = dkr::runtime::ui::run_startup_screen(
            static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
        if (!startup.start_game) {
            dkr::runtime::platform::shutdown();
            if (startup.lifecycle_request ==
                dkr::runtime::ui::LifecycleRequest::Restart) {
                return RelaunchApplication(argc, argv) ? 0 : 6;
            }
            return 0;
        }
        rom_path = startup.rom_path;
        prepared_mods = startup.mods;
        rom_identified = false;
    }
#else
    const ultramodern::renderer::WindowHandle window_handle{};
    if (rom_path.empty()) {
        std::fprintf(stderr, "The diagnostic runtime requires a ROM path.\n");
        dkr::runtime::platform::shutdown();
        return 2;
    }
#endif

    if (!rom_identified && !dkr::runtime::ValidateRomForLauncher(
            rom_path, rom_identity, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    if (!rom_identified && !PrepareCanonicalRomPath(
            rom_path, rom_identity, config_directory, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    if (!log_configured && !ConfigurePersistentRuntimeLog(config_directory)) {
        std::fprintf(stderr,
                     "[boot][log] could not create the persistent runtime log\n");
    }
#if DKR_RUNTIME_HAS_RT64
    if (!rom_identified) {
        window_handle = dkr::runtime::platform::prepare_window_for_game();
#if defined(_WIN32) || defined(__APPLE__)
        if (window_handle.window == nullptr) {
#else
        if (window_handle == nullptr) {
#endif
            std::fprintf(stderr,
                         "[boot][window] failed to prepare the game renderer window\n");
            dkr::runtime::platform::shutdown();
            return 4;
        }
    }
#endif

    if (!dkr::runtime::RegisterGame(config_directory, rom_identity.revision,
                                    rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        dkr::runtime::platform::shutdown();
        return 3;
    }
    const dkr::runtime::rom::Revision registered_revision = rom_identity.revision;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (rollback_bootstrap && registered_revision!=dkr::runtime::rom::Revision::UsV77) {
        std::fprintf(stderr,"[rollback-test] This first race test requires US v1.0.\n");
        dkr::runtime::platform::shutdown(); return 3;
    }
#endif
    dkr::runtime::startup_performance::mark("game-registered");
    std::fprintf(stderr, "[boot][rom] validated and registered\n");

    const recomp::rsp::callbacks_t rsp_callbacks{.get_rsp_microcode = GetRspMicrocode};
    const ultramodern::renderer::callbacks_t renderer_callbacks{
#if DKR_RUNTIME_HAS_RT64
        .create_render_context = dkr::runtime::CreateRT64Renderer};
#else
        .create_render_context = dkr::runtime::CreateDiagnosticRenderer};
#endif
    const ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = dkr::runtime::platform::queue_audio,
        .get_frames_remaining = dkr::runtime::platform::audio_frames_remaining,
        .set_frequency = dkr::runtime::platform::set_audio_frequency,
        .external_work_allowed = []() {
            return dkr::runtime::netplay::external_side_effects_allowed();
        },
    };
    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = dkr::runtime::platform::poll_input,
        .frame_boundary = dkr::runtime::netplay::on_frame_boundary,
        .physical_poll_allowed = []() {
            return dkr::runtime::netplay::physical_input_poll_allowed();
        },
        .get_input = dkr::runtime::platform::get_input,
        .set_rumble = dkr::runtime::platform::set_rumble,
        .get_connected_device_info = dkr::runtime::platform::get_connected_device_info,
    };
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (rollback_bootstrap) {
        input_callbacks.poll_input=+[](){};
        input_callbacks.get_input=+[](int port,std::uint16_t* buttons,float* x,float* y){
            *buttons=0;*x=*y=0.0F;return port>=0&&port<2;
        };
        input_callbacks.set_rumble=+[](int,bool){};
        input_callbacks.get_connected_device_info=+[](int port){
            return ultramodern::input::connected_device_info_t{
                port>=0&&port<2?ultramodern::input::Device::Controller:ultramodern::input::Device::None,
                ultramodern::input::Pak::None};
        };
    }
#endif
    const ultramodern::renderer::callbacks_t unused_renderer_callbacks = renderer_callbacks;
    (void)unused_renderer_callbacks;
    // SDL's window event queue is serviced explicitly on DkrMain's thread
    // below. Do not rely on librecomp's optional update callback: that path is
    // not consistently serviced by every pinned runtime configuration and can
    // leave Escape, window close and Exit to Desktop unresponsive.
    const ultramodern::gfx_callbacks_t gfx_callbacks{};
    const ultramodern::events::callbacks_t events_callbacks{
        .authored_simulation_pacing_scale_milli_callback = []() {
            return dkr::runtime::netplay::
                authored_simulation_pacing_scale_milli();
        },
        .presentation_allowed_callback = []() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            if(dkr_experimental_bootstrap_active())return false;
#endif
            return dkr::runtime::netplay::external_side_effects_allowed();
        }};
    const ultramodern::error_handling::callbacks_t error_callbacks{.message_box = MessageBox};
    const ultramodern::threads::callbacks_t thread_callbacks{.get_game_thread_name = GetThreadName};

    recomp::Configuration configuration{
        .project_version = {.major = 1, .minor = 0, .patch = 0,
                            .suffix = DKR_RELEASE_VERSION},
        .window_handle = window_handle,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
        .save_write_allowed_callback = []() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            if(dkr_experimental_bootstrap_active())return false;
#endif
            return dkr::runtime::saves::runtime_save_writes_allowed() &&
                dkr::runtime::netplay::external_side_effects_allowed();
        },
        .error_handling_callbacks = error_callbacks,
        .threads_callbacks = thread_callbacks,
        // DKR's scheduler interrupt queue can briefly be full while the VI and
        // audio managers are active. SP/DP completion edges must be retained
        // until the scheduler accepts them or gfxtask_wait can block forever.
        // Immutable snapshots protect display lists. The host-task lifetime
        // hook separately prevents the retail hardware watchdog from retiring
        // work still queued/executing or awaiting guest completion delivery.
        .message_queue_control = {.requeue_sp = true, .requeue_dp = true},
    };

    for (;;) {
#if DKR_RUNTIME_HAS_RT64
        dkr::runtime::ui::reset_lifecycle_request();
#endif
        if (!dkr::runtime::SelectRom(rom_path, rom_error)) {
            std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
            dkr::runtime::platform::shutdown();
            return 3;
        }
        dkr::runtime::startup_performance::mark("runtime-rom-selected");

        // The application can return to its launcher and start another fresh
        // emulated DKR session without restarting the process. Reset only the
        // per-session Magic Code state before the new RDRAM is created.
        // A lobby's accepted manifest, not later overlay preferences, owns
        // the simulation selection on every peer.
        std::optional<std::uint32_t> online_magic_codes;
        if (dkr::runtime::netplay::session().active()) {
            online_magic_codes = static_cast<std::uint32_t>(
                dkr::runtime::netplay::session().view().room.manifest.magic_codes_hash);
        }
        dkr::runtime::magic_codes::begin_game_session(online_magic_codes);
        dkr::runtime::netplay::reset_runtime_state();
        dkr::runtime::rev_a_asset_mutex::reset_statistics();
        dkr::runtime::legacy::begin_session(nullptr);
        bool selecting_save_owner = false;
        std::shared_ptr<const dkr::mods::online::RuntimeResources> online_mods;
        try {
            selecting_save_owner=true; // resource admission failures also return safely to the launcher
            // Launcher preparation already ran on a worker with a progress
            // modal. Explicit command-line launches validate here instead.
            if(!prepared_mods)prepared_mods=dkr::mods::prepare_mod_launch(config_directory,rom_path,
                dkr::runtime::netplay::session().active(),[](const char* stage){std::fprintf(stderr,"[legacy][launch] %s\n",stage);});
            if(prepared_mods->session && dkr::runtime::netplay::session().active())
                throw dkr::mods::Error("Offline custom assets cannot enter an online runtime.");
            if(dkr::runtime::netplay::session().active()) {
#if DKR_RUNTIME_HAS_RT64
                online_mods=dkr::runtime::ui::online_mod_resources();
#endif
                if(online_mods)dkr::runtime::legacy::begin_online(online_mods);
                else dkr::runtime::legacy::begin_prepared(prepared_mods);
            } else dkr::runtime::legacy::begin_prepared(prepared_mods);
            const auto online_save = dkr::runtime::netplay::session().runtime_view();
            selecting_save_owner = true;
            if (!dkr::runtime::saves::prepare_runtime_save_context(config_directory,
                    prepared_mods->save_subfolder, prepared_mods->pak_directory,
                    online_save.active, online_save.host,
                    online_save.launch_descriptor ? online_save.launch_descriptor->match_id : 0,
                    online_save.online_save_hash, rom_error)) throw dkr::mods::Error(rom_error);
            ultramodern::set_save_storage_callbacks({
                dkr::runtime::saves::runtime_save_path,
                dkr::runtime::saves::load_runtime_save,
                dkr::runtime::saves::commit_runtime_save});
            dkr::runtime::pak::begin_session_directory(dkr::runtime::saves::runtime_pak_directory());
        } catch(const std::exception& error) {
            std::fprintf(stderr,"[legacy][launch] %s\n",error.what());
#if DKR_RUNTIME_HAS_RT64
            if (selecting_save_owner) {
                // No guest producer exists yet. A locked/invalid route is a
                // recoverable refusal, never permission to open an offline fallback.
                dkr::runtime::pak::begin_session_directory({});
                dkr::runtime::saves::retire_runtime_save_context();
                dkr::runtime::legacy::begin_session(nullptr);
                prepared_mods.reset();
                if (dkr::runtime::netplay::session().active())
                    dkr::runtime::netplay::session().fail_runtime_start(std::string("Save protection: ") + error.what());
                dkr::runtime::ui::report_mod_error(std::string("Save protection: ") + error.what());
                const auto startup = dkr::runtime::ui::run_startup_screen(
                    static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()), rom_path);
                if (!startup.start_game) {
                    dkr::runtime::platform::shutdown();
                    return startup.lifecycle_request == dkr::runtime::ui::LifecycleRequest::Restart
                        ? (RelaunchApplication(argc, argv) ? 0 : 6) : 0;
                }
                dkr::runtime::rom::Identity next_identity{};
                auto next_rom = startup.rom_path;
                if (!dkr::runtime::ValidateRomForLauncher(next_rom, next_identity, rom_error) ||
                    !PrepareCanonicalRomPath(next_rom, next_identity, config_directory, rom_error)) {
                    std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
                    dkr::runtime::platform::shutdown(); return 3;
                }
                if (next_identity.revision != registered_revision) {
                    dkr::runtime::platform::shutdown();
                    return RelaunchApplication(argc, argv) ? 0 : 6;
                }
                rom_path = std::move(next_rom); rom_identity = next_identity; prepared_mods = startup.mods;
                window_handle = dkr::runtime::platform::prepare_window_for_game();
#if defined(_WIN32) || defined(__APPLE__)
                if (!window_handle.window) {
#else
                if (!window_handle) {
#endif
                    dkr::runtime::platform::shutdown(); return 4;
                }
                configuration.window_handle = window_handle;
                continue;
            }
#endif
            dkr::runtime::platform::shutdown();return 4;
        }
#if DKR_LEGACY_QUALIFICATION
        if (const char* recipe = std::getenv("DKR_LEGACY_QUALIFICATION_RECIPE")) {
            if (dkr::runtime::netplay::session().active()) {
                std::fprintf(stderr, "[legacy][qualification] Refusing to modify an online session.\n");
                return 4;
            }
            try {
                dkr::runtime::legacy::configure_qualification(rom_path, std::filesystem::u8path(recipe));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[legacy][qualification] preparation failed: %s\n", error.what());
                return 4;
            }
        }
#endif
#if defined(DKR_WATER_QUALIFICATION)
        if (std::getenv("DKR_WATER_TEST_MAP") && dkr::runtime::netplay::session().active()) {
            std::fprintf(stderr,"[water][qualification] Refusing an online session.\n");
            return 4;
        }
#endif
        std::fprintf(stderr,
                     "[boot] runtime initialized; waiting for first safe VI state\n");
        std::atomic<bool> runtime_finished{false};
#if defined(__ANDROID__)
        dkr::runtime::graphics_health::reset();
        dkr::runtime::graphics_health::resource_failure_reporter.store(
            +[](dkr::runtime::graphics_health::Stage stage, std::int32_t result) {
                char message[192];
                std::snprintf(message, sizeof(message),
                    "Renderer stopped safely at %s (Vulkan result %d). Export logs here, then close and reopen DKR-R.",
                    dkr::runtime::graphics_health::stage_name(stage), result);
                std::fprintf(stderr, "[android][graphics] %s\n", message);
                std::fflush(stderr);
                dkr::runtime::android::report_graphics_failure(message);
            });
        bool graphics_failure_reported = false;
#endif
        std::exception_ptr runtime_failure;
        bool return_from_owned = false;
        bool owned_failed = false;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        auto* admitted_lobby=&dkr::runtime::netplay::session();
        const auto owned_descriptor=admitted_lobby->launch_descriptor();
        const bool owned_launch=owned_descriptor && owned_descriptor->synchronization==
            dkr::runtime::netplay::SynchronizationMode::ExperimentalRollback;
        std::unique_ptr<dkr::runtime::netplay::DirectSession> local_bootstrap_session;
        auto session_configuration=configuration;
        if(owned_launch) {
            const auto initial_online=admitted_lobby->runtime_view();
            if(!dkr::runtime::netplay::experimental::runtime_available(registered_revision) ||
               !dkr_experimental_bootstrap_arm_memory(owned_descriptor->player_count,
                   initial_online.host,owned_descriptor->match_id,initial_online.online_save_hash,rom_error)) {
                admitted_lobby->fail_runtime_start(rom_error.empty()?"This ROM does not have an owned rollback payload.":rom_error);
                dkr::runtime::platform::shutdown();return 5;
            }
            local_bootstrap_session=std::make_unique<dkr::runtime::netplay::DirectSession>();
            dkr::runtime::netplay::bind_external_session(local_bootstrap_session.get());
            // Local construction has neutral virtual controls. It may neither
            // accept network inputs nor write the user's single-player save.
            session_configuration.input_callbacks.poll_input=+[](){};
            session_configuration.input_callbacks.get_input=+[](int port,std::uint16_t* buttons,float* x,float* y) {
                *buttons=0;*x=*y=0;return port>=0&&port<dkr_experimental_bootstrap_players();
            };
            session_configuration.input_callbacks.set_rumble=+[](int,bool){};
            session_configuration.input_callbacks.get_connected_device_info=+[](int port) {
                return ultramodern::input::connected_device_info_t{
                    port>=0&&port<dkr_experimental_bootstrap_players()
                        ? ultramodern::input::Device::Controller:ultramodern::input::Device::None,
                    ultramodern::input::Pak::None};
            };
            session_configuration.audio_callbacks.queue_samples=+[](std::int16_t*,std::size_t){};
        }
#else
        const auto& session_configuration=configuration;
#endif
#if defined(__ANDROID__)
        const auto report_gpu_failure = [&] {
            const auto gpu_failure = dkr::runtime::graphics_health::failure.load(std::memory_order_relaxed);
            if (gpu_failure == 0 || graphics_failure_reported) return;
            graphics_failure_reported = true;
            char message[192];
            std::snprintf(message, sizeof(message), "Vulkan failure: %s (result %d). Rendering has stopped. Export logs using this dialog, then close and reopen DKR-R.",
                dkr::runtime::graphics_health::stage_name(static_cast<dkr::runtime::graphics_health::Stage>(gpu_failure >> 32)), static_cast<std::int32_t>(gpu_failure));
            std::fprintf(stderr, "[android][graphics] %s\n", message);
            dkr::runtime::android::report_graphics_failure(message);
            // A terminal allocation failure retains resources on the faulting
            // worker. Joining it here would also freeze SDL. Android's native
            // failure dialog provides export/explicit close without a GPU.
            if (!dkr::runtime::graphics_health::resource_quarantined.load(std::memory_order_acquire))
                ultramodern::quit();
        };
#endif
        std::thread runtime_thread([&] {
            try {
                recomp::start(session_configuration);
            } catch (...) {
                runtime_failure = std::current_exception();
            }
            runtime_finished.store(true, std::memory_order_release);
        });
        dkr::runtime::startup_performance::mark("runtime-thread-started");

        const auto runtime_started_at = std::chrono::steady_clock::now();
#if defined(__ANDROID__)
        bool startup_wait_reported = false;
#endif
        bool timeout_requested = false;
        auto next_task_check = runtime_started_at;
        unsigned task_stalls_reported = 0;
        while (!runtime_finished.load(std::memory_order_acquire)) {
            const auto task_check_now = std::chrono::steady_clock::now();
            if (task_check_now >= next_task_check && task_stalls_reported < 8) {
                next_task_check = task_check_now + std::chrono::milliseconds(250);
                dkr::runtime::host_tasks::Task stalled;
                if (dkr::runtime::host_tasks::registry.take_stall(stalled, task_check_now)) {
                    char message[256];
                    std::snprintf(message, sizeof(message),
                        "Task %llu (%08X) has made no progress for 15 seconds at %s (pending=%u). "
                        "It may still finish. No completion has been forced.",
                        static_cast<unsigned long long>(stalled.token.generation), stalled.token.address,
                        dkr::runtime::host_tasks::stage_name(stalled.stage), stalled.pending);
                    std::fprintf(stderr, "[host-task][stall] %s\n", message);
#if defined(__ANDROID__)
                    if (task_stalls_reported == 0) dkr::runtime::android::report_task_stall(message);
#endif
                    ++task_stalls_reported;
                }
            }
#if defined(__ANDROID__)
            report_gpu_failure();
            // An initialization fence can stall before the first guest task
            // exists. Diagnose that wait independently; never fake completion,
            // reuse its resources or kill the process on a timeout.
            if (!startup_wait_reported && !dkr::runtime::graphics_health::failed() &&
                !dkr::runtime::graphics_health::first_presentation.load() &&
                task_check_now - runtime_started_at >= std::chrono::seconds(15)) {
                startup_wait_reported = true;
                constexpr auto message = "No game frame has been presented after 15 seconds. "
                    "The renderer may still be initializing or waiting for the GPU. "
                    "You can keep waiting or export logs; no GPU work has been cancelled.";
                std::fprintf(stderr, "[android][startup-wait] %s\n", message);
                dkr::runtime::android::report_task_stall(message);
            }
#endif
#if DKR_RUNTIME_HAS_RT64
            // The SDL video subsystem and native window were created on this
            // thread. Keep all window/input event pumping here for Windows,
            // X11 and Wayland compatibility while the recompiler owns its
            // worker.
            dkr::runtime::platform::pump_window_events(nullptr);
            dkr::runtime::service_online_wait_presentation();
#endif
            if (!timeout_requested && timeout_seconds != 0 &&
                std::chrono::steady_clock::now() - runtime_started_at >=
                    std::chrono::seconds(timeout_seconds)) {
#if DKR_RUNTIME_HAS_RT64
                std::fprintf(
                    stderr,
                    "[boot][watchdog] completed-f3ddkr-tasks=%llu\n",
                    static_cast<unsigned long long>(
                        dkr::runtime::completed_f3ddkr_task_count()));
#endif
                std::fprintf(stderr,
                             "[boot][watchdog] stopping after %u seconds\n",
                             timeout_seconds);
                timeout_requested = true;
                ultramodern::quit();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        runtime_thread.join();
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        if(owned_launch) {
            dkr::runtime::netplay::bind_external_session(admitted_lobby);
            local_bootstrap_session.reset();
            auto bootstrap=dkr_experimental_bootstrap_take_memory();
            if(runtime_failure==nullptr) {
                std::string owned_error;
                bool owned_ok=false;
                try {
                    auto mod_bootstrap=online_mods?dkr::runtime::legacy::online_bootstrap_checkpoint():dkr::mods::Bytes{};
                    owned_ok=dkr::runtime::netplay::experimental::run_runtime(window_handle,rom_path,std::move(bootstrap),*admitted_lobby,*owned_descriptor,timeout_seconds,owned_error,owned_check,
                        online_mods,std::move(mod_bootstrap));
                } catch(const std::exception& exception) {owned_error=exception.what();}
                catch(...) {owned_error="The owned runtime could not start safely.";}
                if(!owned_ok) {
                    owned_failed=true;
                    std::fprintf(stderr,"[rollback][owned] %s\n",owned_error.c_str());
                    admitted_lobby->fail_runtime_start(owned_error);
                    dkr::runtime::ui::report_mod_error("Experimental online play stopped safely: "+owned_error);
                }
            }
            return_from_owned=true;
        }
#endif
#if defined(__ANDROID__)
        // Setup can finish with an error before the event loop observes it.
        report_gpu_failure();
#endif
        dkr::runtime::pak::begin_session_directory({});
        const auto save_failure = dkr::runtime::saves::runtime_save_failure();
        dkr::runtime::saves::retire_runtime_save_context();
#if DKR_RUNTIME_HAS_RT64
        if (!save_failure.empty()) dkr::runtime::ui::report_mod_error("Save protection: " + save_failure);
#endif
        prepared_mods.reset();
        const auto mod_failure=dkr::runtime::legacy::failure();
        dkr::runtime::legacy::begin_session(nullptr);
        if (registered_revision == dkr::runtime::rom::Revision::UsV80) {
            const auto mutex_stats =
                dkr::runtime::rev_a_asset_mutex::statistics();
            std::fprintf(
                stderr,
                "[perf][v1.1-asset-mutex] fast=%" PRIu64 "/%" PRIu64
                " scheduler=%" PRIu64 "/%" PRIu64 "\n",
                mutex_stats.fast_acquires, mutex_stats.fast_releases,
                mutex_stats.scheduler_acquires,
                mutex_stats.scheduler_releases);
        }
#if DKR_RUNTIME_HAS_RT64
        const auto lifecycle_request = dkr::runtime::ui::lifecycle_request();
#endif
        if (runtime_failure != nullptr) {
            dkr::runtime::platform::shutdown();
            try {
                std::rethrow_exception(runtime_failure);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[boot] runtime failed: %s\n",
                             error.what());
            } catch (...) {
                std::fprintf(
                    stderr,
                    "[boot] runtime failed with an unknown exception\n");
            }
            return 5;
        }
#if DKR_RUNTIME_HAS_RT64
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        if(owned_check){dkr::runtime::platform::shutdown();return owned_failed?5:0;}
#endif
        if (lifecycle_request == dkr::runtime::ui::LifecycleRequest::StopGame || !mod_failure.empty() || !save_failure.empty() ||
            (return_from_owned && lifecycle_request==dkr::runtime::ui::LifecycleRequest::None)) {
            if(!mod_failure.empty())dkr::runtime::ui::report_mod_error("Custom content stopped safely: "+mod_failure);
            std::fprintf(stderr,
                         "[boot][stop] game stopped; returning to launcher\n");
            dkr::runtime::ui::reset_lifecycle_request();
            bool software_launcher_return = owned_failed;
#if defined(_WIN32)
            // Both owned exit paths replace the retired game HWND and request
            // an explicitly non-D3D9 launcher (D3D11 or bounded GDI fallback).
            // Initial startup and legacy returns keep their existing path.
            software_launcher_return = software_launcher_return || return_from_owned;
#endif
            const auto startup = dkr::runtime::ui::run_startup_screen(
                static_cast<SDL_Window*>(
                    dkr::runtime::platform::sdl_window()),
                rom_path, software_launcher_return);
            if (!startup.start_game) {
                dkr::runtime::platform::shutdown();
                if (startup.lifecycle_request ==
                    dkr::runtime::ui::LifecycleRequest::Restart) {
                    return RelaunchApplication(argc, argv) ? 0 : 6;
                }
                std::fprintf(stderr, "[boot] launcher closed cleanly\n");
                return 0;
            }

            dkr::runtime::rom::Identity next_identity{};
            std::filesystem::path next_rom_path = startup.rom_path;
            if (!dkr::runtime::ValidateRomForLauncher(
                    next_rom_path, next_identity, rom_error) ||
                !PrepareCanonicalRomPath(next_rom_path, next_identity,
                                         config_directory, rom_error)) {
                std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
                dkr::runtime::platform::shutdown();
                return 3;
            }
            if (next_identity.revision != registered_revision) {
                std::fprintf(
                    stderr,
                    "[boot][rom] changing ROM revisions requires a clean "
                    "runtime relaunch\n");
                dkr::runtime::platform::shutdown();
                return RelaunchApplication(argc, argv) ? 0 : 6;
            }
            rom_path = std::move(next_rom_path);
            prepared_mods = startup.mods;
            rom_identity = next_identity;
            // The return launcher may have replaced the native window. No
            // previous runtime is alive: refresh BOTH the owned renderer handle
            // and the normal bootstrap configuration before the next launch.
            window_handle=dkr::runtime::platform::prepare_window_for_game();
#if defined(_WIN32) || defined(__APPLE__)
            if(window_handle.window==nullptr) {
#else
            if(window_handle==nullptr) {
#endif
                std::fprintf(stderr,"[boot][window] failed to prepare the next game window\n");
                dkr::runtime::platform::shutdown();return 4;
            }
            configuration.window_handle=window_handle;
            std::fprintf(stderr,
                         "[boot][start] launching a new game session\n");
            continue;
        }
        if (lifecycle_request == dkr::runtime::ui::LifecycleRequest::Restart) {
            dkr::runtime::platform::shutdown();
            std::fprintf(stderr, "[boot][restart] relaunching DKR-R\n");
            return RelaunchApplication(argc, argv) ? 0 : 6;
        }
#endif
        dkr::runtime::platform::shutdown();
        std::fprintf(stderr, "[boot] runtime stopped cleanly\n");
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        if (rollback_bootstrap && !dkr_experimental_bootstrap_completed()) return 5;
#endif
        return 0;
    }
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    // Keep the existing normal-launch path intact. Only the explicit local
    // bootstrap child needs UTF-8 argv, including non-ASCII profile paths.
    if(std::wcsstr(GetCommandLineW(),L"--rollback-bootstrap-output")) {
        dkr::runtime::netplay::experimental::WindowsUtf8Arguments unicode;
        if(!unicode.valid)return 2;
        return DkrMain(int(unicode.values.size()),unicode.values.data());
    }
#endif
    return DkrMain(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return DkrMain(argc, argv);
}
#endif
