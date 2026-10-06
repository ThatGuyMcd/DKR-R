#include "runtime_portable.hpp"

#include <cstdlib>
#include <cstring>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dkr::runtime::portable {
namespace {
std::filesystem::path g_application_directory;
std::filesystem::path g_marker;
bool g_enabled_at_startup = false;

std::filesystem::path ExecutablePath(const char* argument) {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) break;
        if (length < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    if (const char* image = std::getenv("APPIMAGE"); image && *image) {
        const auto path = std::filesystem::u8path(image);
        std::error_code ec;
        if (path.is_absolute() && std::filesystem::is_regular_file(path, ec)) return path;
    }
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && path.is_absolute()) return path;
#endif
    std::error_code error;
    const auto fallback = std::filesystem::absolute(std::filesystem::u8path(argument ? argument : ""), error);
    return error ? std::filesystem::path{} : fallback;
}

bool SafeExistingMarker(std::string& error) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(g_marker, ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        error = "portable.txt is not an ordinary file; it was left untouched.";
        return false;
    }
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(g_marker.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        error = "portable.txt is a filesystem link or could not be inspected; it was left untouched.";
        return false;
    }
#endif
    if (std::filesystem::hard_link_count(g_marker, ec) != 1 || ec) {
        error = "portable.txt has another filesystem owner or could not be inspected; it was left untouched.";
        return false;
    }
    return true;
}
} // namespace

void configure(const char* executable_argument) {
    g_application_directory = ExecutablePath(executable_argument).parent_path();
    g_marker = g_application_directory.empty() ? std::filesystem::path{} : g_application_directory / "portable.txt";
    g_enabled_at_startup = enabled();
}
const std::filesystem::path& application_directory() { return g_application_directory; }
const std::filesystem::path& marker_path() { return g_marker; }
bool supported() {
#if defined(__ANDROID__)
    return false;
#else
    return !g_marker.empty();
#endif
}
bool enabled() {
    std::error_code ec;
    return supported() && std::filesystem::exists(g_marker, ec);
}
bool enabled_at_startup() { return g_enabled_at_startup; }

bool set_enabled(bool value, std::string& error) {
    error.clear();
    if (!supported()) { error = "Portable mode is not available for this application location."; return false; }
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(g_marker, ec);
    const bool absent = status.type() == std::filesystem::file_type::not_found;
    if (ec && !absent) { error = "Could not inspect portable.txt: " + ec.message(); return false; }
    if (!absent) {
        if (!SafeExistingMarker(error)) return false;
        if (value) return true; // Never truncate an existing marker.
        if (!std::filesystem::remove(g_marker, ec) || ec) {
            error = "Could not remove portable.txt: " + ec.message(); return false;
        }
        return true;
    }
    if (!value) return true;
    constexpr char contents[] = "DKR-R portable mode\n";
#if defined(_WIN32)
    HANDLE file = CreateFileW(g_marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Could not create portable.txt (Windows error " + std::to_string(GetLastError()) + ")."; return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, contents, sizeof(contents) - 1, &written, nullptr) &&
        written == sizeof(contents) - 1 && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
#else
    const int file = open(g_marker.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) { error = "Could not create portable.txt: " + std::string(std::strerror(errno)); return false; }
    std::size_t written = 0;
    bool ok = true;
    while (written < sizeof(contents) - 1) {
        const auto amount = write(file, contents + written, sizeof(contents) - 1 - written);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) { ok = false; break; }
        written += static_cast<std::size_t>(amount);
    }
    ok = ok && fsync(file) == 0;
    const bool closed = close(file) == 0;
#endif
    if (!ok || !closed) {
        error = "portable.txt could not be written completely. Check the application folder permissions.";
        std::filesystem::remove(g_marker, ec); // Only our newly created marker.
        return false;
    }
    return true;
}
} // namespace dkr::runtime::portable
