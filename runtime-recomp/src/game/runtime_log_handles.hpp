#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace dkr::runtime::support::logging {
inline bool protect_log(FILE* stream) {
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream)));
    return handle != INVALID_HANDLE_VALUE && SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) != 0;
#else
    const auto fd = fileno(stream);
    const auto flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
#endif
}
inline std::filesystem::path fallback_log(const std::filesystem::path& directory) {
    static std::atomic<unsigned> serial{0};
    const auto stamp =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    const auto archive = directory / "history";
    std::error_code error;
    std::filesystem::create_directories(archive, error);
    if (error)
        return {};
    return archive / ("runtime-" + std::to_string(stamp) + "-" + std::to_string(pid) + "-" +
                      std::to_string(serial.fetch_add(1)) + ".log");
}
} // namespace dkr::runtime::support::logging
