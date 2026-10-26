#include "network_interfaces.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dkr::runtime::netplay {
namespace {

bool virtual_hint(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    constexpr const char* hints[] = {"tailscale", "zerotier", "hamachi", "radmin", "wireguard",
                                     "wintun",    "tun",      "tap",     "vpn"};
    return std::any_of(std::begin(hints), std::end(hints),
                       [&text](const char* hint) { return text.find(hint) != std::string::npos; });
}

#if defined(_WIN32)
std::string narrow(const wchar_t* text) {
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

// Tailscale is an optional discovery aid.  It must never create a console
// window or be allowed to hold the launcher indefinitely when its service is
// unavailable.  Shell-based _popen() does both on Windows, so launch the
// executable directly with redirected output and a strict deadline.
std::string tailscale_status_json() {
    std::array<wchar_t, 32768> executable{};
    const DWORD executable_length = SearchPathW(nullptr, L"tailscale.exe", nullptr,
                                                static_cast<DWORD>(executable.size()), executable.data(), nullptr);
    if (executable_length == 0 || executable_length >= executable.size()) {
        return {};
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&output_read, &output_write, &security, 0))
        return {};
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;

    PROCESS_INFORMATION process{};
    std::wstring command = L"\"";
    command.append(executable.data(), executable_length);
    command += L"\" status --json";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const BOOL launched = CreateProcessW(executable.data(), mutable_command.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(output_write);
    if (!launched) {
        CloseHandle(output_read);
        return {};
    }

    constexpr ULONGLONG kDeadlineMs = 2000;
    constexpr std::size_t kMaximumOutput = 4U * 1024U * 1024U;
    const ULONGLONG started = GetTickCount64();
    std::string output;
    std::array<char, 4096> buffer{};
    bool timed_out = false;
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(output_read, nullptr, 0, nullptr, &available, nullptr) && available != 0) {
            DWORD read = 0;
            const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (ReadFile(output_read, buffer.data(), wanted, &read, nullptr) && read != 0 &&
                output.size() < kMaximumOutput) {
                const std::size_t remaining = kMaximumOutput - output.size();
                output.append(buffer.data(), std::min<std::size_t>(read, remaining));
            }
        }

        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD remaining = 0;
            while (PeekNamedPipe(output_read, nullptr, 0, nullptr, &remaining, nullptr) && remaining != 0 &&
                   output.size() < kMaximumOutput) {
                DWORD read = 0;
                const DWORD wanted = std::min<DWORD>(remaining, static_cast<DWORD>(buffer.size()));
                if (!ReadFile(output_read, buffer.data(), wanted, &read, nullptr) || read == 0)
                    break;
                const std::size_t capacity = kMaximumOutput - output.size();
                output.append(buffer.data(), std::min<std::size_t>(read, capacity));
            }
            break;
        }
        if (GetTickCount64() - started >= kDeadlineMs) {
            timed_out = true;
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(process.hProcess, 250);
            break;
        }
        Sleep(5);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(output_read);
    return timed_out ? std::string{} : output;
}
#else
// Match the Windows helper's bounded, shell-free behaviour. Discovery is an
// optional convenience and must not leave an AppImage worker blocked forever
// if the Tailscale daemon or CLI is unhealthy.
std::string tailscale_status_json() {
    int output_pipe[2]{};
    if (pipe(output_pipe) != 0)
        return {};
    const pid_t child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return {};
    }
    if (child == 0) {
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execlp("tailscale", "tailscale", "status", "--json", static_cast<char*>(nullptr));
        _exit(127);
    }

    close(output_pipe[1]);
    const int original_flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (original_flags >= 0) {
        fcntl(output_pipe[0], F_SETFL, original_flags | O_NONBLOCK);
    }
    constexpr std::size_t kMaximumOutput = 4U * 1024U * 1024U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::string output;
    std::array<char, 4096> buffer{};
    bool exited = false;
    bool timed_out = false;
    while (!exited) {
        for (;;) {
            const ssize_t count = read(output_pipe[0], buffer.data(), buffer.size());
            if (count > 0) {
                if (output.size() < kMaximumOutput) {
                    const std::size_t remaining = kMaximumOutput - output.size();
                    output.append(buffer.data(), (std::min<std::size_t>)(static_cast<std::size_t>(count), remaining));
                }
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            break;
        }
        int status = 0;
        const pid_t wait_result = waitpid(child, &status, WNOHANG);
        if (wait_result == child) {
            exited = true;
            break;
        }
        if (wait_result < 0 && errno != EINTR) {
            exited = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            exited = true;
            break;
        }
        pollfd descriptor{output_pipe[0], POLLIN, 0};
        poll(&descriptor, 1, 5);
    }
    for (;;) {
        const ssize_t count = read(output_pipe[0], buffer.data(), buffer.size());
        if (count <= 0)
            break;
        if (output.size() < kMaximumOutput) {
            const std::size_t remaining = kMaximumOutput - output.size();
            output.append(buffer.data(), (std::min<std::size_t>)(static_cast<std::size_t>(count), remaining));
        }
    }
    close(output_pipe[0]);
    return timed_out ? std::string{} : output;
}
#endif

} // namespace

std::vector<std::string> virtual_lan_peer_addresses() {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
#if defined(_WIN32)
    const std::string json = tailscale_status_json();
#else
    const std::string json = tailscale_status_json();
#endif
    static const std::regex ipv4(R"(\b((?:\d{1,3}\.){3}\d{1,3})\b)", std::regex::optimize);
    for (std::sregex_iterator it(json.begin(), json.end(), ipv4), end; it != end; ++it) {
        const std::string address = (*it)[1].str();
        in_addr parsed{};
        if (inet_pton(AF_INET, address.c_str(), &parsed) != 1)
            continue;
        const std::uint32_t host = ntohl(parsed.s_addr);
        // Tailscale IPv4 addresses are allocated from 100.64.0.0/10. Avoid
        // probing unrelated addresses that may be present in status metadata.
        if ((host & 0xFFC00000U) != 0x64400000U)
            continue;
        if (seen.insert(address).second)
            result.push_back(address);
    }
    return result;
}

std::vector<NetworkInterface> enumerate_network_interfaces() {
    std::vector<NetworkInterface> result;
    std::unordered_set<std::string> seen;
#if defined(_WIN32)
    ULONG size = 16U * 1024U;
    std::vector<std::uint8_t> storage(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG status = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        status =
            GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, adapters, &size);
    }
    if (status == NO_ERROR) {
        for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp)
                continue;
            const std::string friendly = narrow(adapter->FriendlyName);
            const std::string id = adapter->AdapterName != nullptr ? adapter->AdapterName : friendly;
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                char address[INET_ADDRSTRLEN]{};
                const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) == nullptr ||
                    !seen.insert(address).second)
                    continue;
                const bool loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
                result.push_back(
                    {id, friendly.empty() ? id : friendly, address, loopback, virtual_hint(friendly + " " + id)});
            }
        }
    }
#else
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses) == 0) {
        for (const ifaddrs* entry = addresses; entry != nullptr; entry = entry->ifa_next) {
            if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || (entry->ifa_flags & IFF_UP) == 0)
                continue;
            char address[INET_ADDRSTRLEN]{};
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) == nullptr ||
                !seen.insert(address).second)
                continue;
            const std::string name = entry->ifa_name != nullptr ? entry->ifa_name : "Network";
            const bool loopback = (entry->ifa_flags & IFF_LOOPBACK) != 0;
            result.push_back({name, name, address, loopback, virtual_hint(name)});
        }
        freeifaddrs(addresses);
    }
#endif
    std::stable_sort(result.begin(), result.end(), [](const NetworkInterface& left, const NetworkInterface& right) {
        if (left.loopback != right.loopback)
            return !left.loopback;
        if (left.virtual_network != right.virtual_network)
            return left.virtual_network;
        return left.name < right.name;
    });
    if (result.empty())
        result.push_back({"loopback", "Local computer", "127.0.0.1", true, false});
    return result;
}

} // namespace dkr::runtime::netplay
