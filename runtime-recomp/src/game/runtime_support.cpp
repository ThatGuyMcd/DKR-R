#include "runtime_support.hpp"
#include "runtime_log_archive.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shellapi.h>
#include <dxgi1_6.h>
#include <winioctl.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace dkr::runtime::support {
namespace {

std::filesystem::path g_config_directory;
std::filesystem::path g_log_directory;
std::filesystem::path g_crash_dump_directory;
std::filesystem::path g_support_report_directory;
std::atomic<bool> g_diagnostic_logging{true};
std::atomic<bool> g_crash_dumps{true};
std::mutex g_preferences_mutex;

std::filesystem::path PreferencesPath() {
    return g_config_directory / "support-options.ini";
}

void SavePreferences() {
    std::lock_guard lock(g_preferences_mutex);
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    if (error)
        return;
    std::ofstream output(PreferencesPath(), std::ios::trunc);
    if (!output)
        return;
    output << "diagnostic_logging=" << (g_diagnostic_logging.load(std::memory_order_acquire) ? 1 : 0) << '\n';
    output << "crash_dumps=" << (g_crash_dumps.load(std::memory_order_acquire) ? 1 : 0) << '\n';
}

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::string FormatBytes(std::uint64_t bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(gib >= 10.0 ? 0 : 1) << gib << " GiB";
    return stream.str();
}

#if defined(_WIN32)
std::string WideToUtf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0')
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::string WindowsCpuName() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return "Unknown CPU";
    }
    wchar_t value[256]{};
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG status =
        RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? Trim(WideToUtf8(value)) : "Unknown CPU";
}

std::string WindowsGpuName() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return "Unknown graphics adapter";
    }
    std::string result = "Unknown graphics adapter";
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            result = Trim(WideToUtf8(description.Description));
            adapter->Release();
            break;
        }
        adapter->Release();
    }
    factory->Release();
    return result;
}

std::string WindowsDriveKind(const std::filesystem::path& path) {
    wchar_t volume_path[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volume_path, static_cast<DWORD>(std::size(volume_path)))) {
        return "Unknown storage";
    }
    std::wstring device = L"\\\\.\\";
    if (volume_path[0] == L'\0' || volume_path[1] != L':') {
        return "Unknown storage";
    }
    device.push_back(volume_path[0]);
    device.push_back(L':');
    HANDLE handle =
        CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return "Unknown storage";
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &descriptor,
                                    sizeof(descriptor), &returned, nullptr);
    CloseHandle(handle);
    if (!ok)
        return "Unknown storage";
    return descriptor.IncursSeekPenalty ? "HDD" : "SSD";
}
#elif !defined(__APPLE__)
std::string ReadFirstMatchingLine(const std::filesystem::path& path, const std::string& prefix) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            const auto separator = line.find(':');
            return Trim(separator == std::string::npos ? line.substr(prefix.size()) : line.substr(separator + 1));
        }
    }
    return {};
}

std::string LinuxGpuName() {
    const std::filesystem::path drm{"/sys/class/drm"};
    std::error_code error;
    if (!std::filesystem::exists(drm, error))
        return "Unknown graphics adapter";
    for (const auto& entry : std::filesystem::directory_iterator(drm, error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos)
            continue;
        const auto device = entry.path() / "device";
        std::ifstream product(device / "product_name");
        std::string product_name;
        std::getline(product, product_name);
        if (!Trim(product_name).empty())
            return Trim(product_name);
        std::ifstream uevent(device / "uevent");
        std::string line;
        std::string driver;
        while (std::getline(uevent, line)) {
            if (line.rfind("DRIVER=", 0) == 0)
                driver = line.substr(7);
        }
        if (!driver.empty())
            return "Graphics adapter (" + driver + ")";
    }
    return "Unknown graphics adapter";
}

std::string LinuxDriveKind(const std::filesystem::path&) {
    // Resolving an arbitrary mount through device-mapper, LVM and removable
    // media is platform-specific. Do not guess: this report deliberately says
    // unknown rather than publishing a device path or serial identifier.
    return "Unknown storage";
}
#endif

} // namespace

void configure(const std::filesystem::path& config_directory) {
    g_config_directory = config_directory;
    g_log_directory = config_directory / "logs";
    g_crash_dump_directory = config_directory / "crash-dumps";
    g_support_report_directory = config_directory / "support-reports";
    std::ifstream input(PreferencesPath());
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string key = line.substr(0, separator);
        const bool enabled = line.substr(separator + 1) != "0";
        if (key == "diagnostic_logging")
            g_diagnostic_logging.store(enabled);
        if (key == "crash_dumps")
            g_crash_dumps.store(enabled);
    }
}

bool diagnostic_logging_enabled() {
    return g_diagnostic_logging.load(std::memory_order_acquire);
}
bool crash_dumps_enabled() {
    return g_crash_dumps.load(std::memory_order_acquire);
}
void set_diagnostic_logging_enabled(bool enabled) {
    g_diagnostic_logging.store(enabled, std::memory_order_release);
    SavePreferences();
}
void set_crash_dumps_enabled(bool enabled) {
    g_crash_dumps.store(enabled, std::memory_order_release);
    SavePreferences();
}

const std::filesystem::path& log_directory() {
    return g_log_directory;
}
const std::filesystem::path& crash_dump_directory() {
    return g_crash_dump_directory;
}
const std::filesystem::path& support_report_directory() {
    return g_support_report_directory;
}

bool open_directory(const std::filesystem::path& directory, std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create that DKR-R support folder.";
        return false;
    }
#if defined(_WIN32)
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        error = "Windows could not open that DKR-R support folder.";
        return false;
    }
#elif defined(DKR_IOS)
    // iOS cannot fork. The directory was created above for the Files app.
    error = "On iPhone, that folder is in the Files app under DKR-R.";
    return false;
#else
    const pid_t child = fork();
    if (child == 0) {
#if defined(__APPLE__)
        // Finder-launched apps get a trimmed PATH; /usr/bin/open is fixed.
        execl("/usr/bin/open", "open", directory.c_str(), static_cast<char*>(nullptr));
#else
        execlp("xdg-open", "xdg-open", directory.c_str(), static_cast<char*>(nullptr));
#endif
        _exit(127);
    }
    if (child < 0) {
        error = "Could not open that DKR-R support folder.";
        return false;
    }
#endif
    error.clear();
    return true;
}

bool export_report(const std::string& report, std::filesystem::path& output, std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(g_support_report_directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the DKR-R support-report folder.";
        return false;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream name;
    name << "DKR-R-Support-" << std::put_time(&local, "%Y%m%d-%H%M%S") << ".txt";
    output = g_support_report_directory / name.str();
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Could not write the DKR-R support report.";
        return false;
    }
    stream << report;
    const auto diagnostic_directory = g_support_report_directory / output.stem();
    std::filesystem::create_directories(diagnostic_directory, filesystem_error);
    if (!filesystem_error) {
        history::snapshot(g_log_directory / "runtime.log", diagnostic_directory / "runtime.log");
        history::snapshot(g_log_directory / "runtime-previous.log", diagnostic_directory / "runtime-previous.log");
        for (const auto& log : history::logs(g_log_directory / "history"))
            history::snapshot(log, diagnostic_directory / log.filename());
        stream << "\nRetained runtime log snapshots: " << diagnostic_directory.string()
               << "\nLogs may contain local paths or session information. Review before sharing.\n";
    }
    if (!stream.good()) {
        error = "The DKR-R support report could not be completed.";
        return false;
    }
    error.clear();
    return true;
}

SystemSummary collect_system_summary() {
    SystemSummary result{};
#if defined(_WIN32)
    result.operating_system = "Windows";
    result.cpu = WindowsCpuName();
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    result.memory = GlobalMemoryStatusEx(&memory) ? FormatBytes(memory.ullTotalPhys) : "Unknown memory";
    result.gpu = WindowsGpuName();
    wchar_t windows_directory[MAX_PATH]{};
    GetWindowsDirectoryW(windows_directory, static_cast<UINT>(std::size(windows_directory)));
    result.boot_drive = WindowsDriveKind(windows_directory);
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    result.application_drive = WindowsDriveKind(executable);
#elif defined(__APPLE__)
    char text[256]{};
    std::size_t size = sizeof(text);
    result.operating_system =
        sysctlbyname("kern.osproductversion", text, &size, nullptr, 0) == 0 ? std::string("macOS ") + text : "macOS";
    size = sizeof(text);
    result.cpu = sysctlbyname("machdep.cpu.brand_string", text, &size, nullptr, 0) == 0 ? Trim(text) : "Unknown CPU";
    std::uint64_t memory = 0;
    size = sizeof(memory);
    result.memory =
        sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) == 0 ? FormatBytes(memory) : "Unknown memory";
    result.gpu = "Unknown graphics adapter";
    result.boot_drive = "Unknown storage";
    result.application_drive = "Unknown storage";
#else
    struct utsname system_name {};
    result.operating_system = uname(&system_name) == 0 ? std::string("Linux ") + system_name.release : "Linux";
    result.cpu = ReadFirstMatchingLine("/proc/cpuinfo", "model name");
    if (result.cpu.empty())
        result.cpu = ReadFirstMatchingLine("/proc/cpuinfo", "Hardware");
    if (result.cpu.empty())
        result.cpu = "Unknown CPU";
    struct sysinfo information {};
    result.memory = sysinfo(&information) == 0
                        ? FormatBytes(static_cast<std::uint64_t>(information.totalram) * information.mem_unit)
                        : "Unknown memory";
    result.gpu = LinuxGpuName();
    result.boot_drive = LinuxDriveKind("/");
    result.application_drive = LinuxDriveKind(".");
#endif
    return result;
}

} // namespace dkr::runtime::support
