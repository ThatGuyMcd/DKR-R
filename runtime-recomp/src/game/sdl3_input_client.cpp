#include "sdl3_input_client.hpp"
#include "sdl3_input_protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stop_token>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

using namespace dkr::runtime::sdl3_input;
using namespace dkr::runtime::sdl3_input::protocol;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

void CloseSocket(NativeSocket& socket) {
    if (socket == kInvalidSocket)
        return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
    socket = kInvalidSocket;
}

void ShutdownSocket(NativeSocket socket) {
    if (socket == kInvalidSocket)
        return;
#if defined(_WIN32)
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

bool SendAll(NativeSocket socket, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size > 0U) {
#if defined(_WIN32)
        const int chunk = send(socket, reinterpret_cast<const char*>(bytes),
                               static_cast<int>(std::min<std::size_t>(size, static_cast<std::size_t>(INT_MAX))), 0);
#else
        const ssize_t chunk = send(socket, bytes, size, MSG_NOSIGNAL);
#endif
        if (chunk <= 0)
            return false;
        bytes += static_cast<std::size_t>(chunk);
        size -= static_cast<std::size_t>(chunk);
    }
    return true;
}

bool ReceiveAll(NativeSocket socket, void* data, std::size_t size) {
    auto* bytes = static_cast<std::uint8_t*>(data);
    while (size > 0U) {
#if defined(_WIN32)
        const int chunk = recv(socket, reinterpret_cast<char*>(bytes),
                               static_cast<int>(std::min<std::size_t>(size, static_cast<std::size_t>(INT_MAX))), 0);
#else
        const ssize_t chunk = recv(socket, bytes, size, 0);
#endif
        if (chunk <= 0)
            return false;
        bytes += static_cast<std::size_t>(chunk);
        size -= static_cast<std::size_t>(chunk);
    }
    return true;
}

bool WaitReadable(NativeSocket socket, std::chrono::milliseconds timeout, std::stop_token stop_token) {
    constexpr auto kCancellationPoll = std::chrono::milliseconds(50);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!stop_token.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto slice = std::min(remaining, kCancellationPoll);
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval value{};
        value.tv_sec = static_cast<long>(slice.count() / 1000);
        value.tv_usec = static_cast<long>((slice.count() % 1000) * 1000);
#if defined(_WIN32)
        const int result = select(0, &readable, nullptr, nullptr, &value);
#else
        const int result = select(socket + 1, &readable, nullptr, nullptr, &value);
#endif
        if (result > 0)
            return true;
        if (result < 0)
            return false;
    }
    return false;
}

std::array<std::uint8_t, kTokenBytes> RandomToken() {
    std::array<std::uint8_t, kTokenBytes> token{};
    std::random_device random;
    for (auto& byte : token) {
        byte = static_cast<std::uint8_t>(random());
    }
    return token;
}

std::string TokenText(const std::array<std::uint8_t, kTokenBytes>& token) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : token) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

template <std::size_t Size> std::string FixedString(const std::array<char, Size>& value) {
    const auto end = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), end);
}

#if defined(_WIN32)
std::wstring QuoteWindowsArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0U;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(slashes * 2U + 1U, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(slashes, L'\\');
            quoted.push_back(character);
        }
        slashes = 0U;
    }
    quoted.append(slashes * 2U, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring WidenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}
#endif

} // namespace

struct dkr::runtime::sdl3_input::Client::Impl {
    mutable std::mutex state_mutex;
    std::mutex send_mutex;
    State latest_state{};
    std::string status_detail = "SDL3 input host is not running.";
    std::array<std::uint8_t, kTokenBytes> token{};
    NativeSocket socket = kInvalidSocket;
    std::thread reader;
    std::atomic<bool> running{false};
    std::atomic<bool> healthy{false};
#if defined(_WIN32)
    HANDLE process = nullptr;
    bool winsock_started = false;
#else
    pid_t process = -1;
#endif

    bool start_socket_runtime(std::string& error) {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WinSock initialization failed.";
            return false;
        }
        winsock_started = true;
#else
        (void)error;
#endif
        return true;
    }

    void stop_socket_runtime() {
#if defined(_WIN32)
        if (winsock_started) {
            WSACleanup();
            winsock_started = false;
        }
#endif
    }

    bool spawn(const std::filesystem::path& host, std::uint16_t port, const std::filesystem::path& mappings,
               std::string& error) {
        const std::string port_text = std::to_string(port);
        const std::string token_text = TokenText(token);
#if defined(_WIN32)
        const std::wstring host_text = host.wstring();
        std::wstring command = QuoteWindowsArgument(host_text) + L" --port " + WidenAscii(port_text) + L" --token " +
                               WidenAscii(token_text);
        if (!mappings.empty()) {
            command += L" --mappings " + QuoteWindowsArgument(mappings.wstring());
        }
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(host_text.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, host.parent_path().wstring().c_str(), &startup, &process_info)) {
            error =
                "Could not start the private SDL3 input host (Windows error " + std::to_string(GetLastError()) + ").";
            return false;
        }
        CloseHandle(process_info.hThread);
        process = process_info.hProcess;
#else
        const std::string host_text = host.string();
        const std::string mappings_text = mappings.string();
        std::vector<std::string> arguments{host_text, "--port", port_text, "--token", token_text};
        if (!mappings.empty()) {
            arguments.push_back("--mappings");
            arguments.push_back(mappings_text);
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1U);
        for (auto& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        const int result = posix_spawn(&process, host_text.c_str(), nullptr, nullptr, argv.data(), environ);
        if (result != 0) {
            process = -1;
            error = "Could not start the private SDL3 input host (error " + std::to_string(result) + ").";
            return false;
        }
#endif
        return true;
    }

    void reap_process(bool force) {
#if defined(_WIN32)
        if (process == nullptr)
            return;
        DWORD result = WaitForSingleObject(process, force ? 500U : 0U);
        if (force && result == WAIT_TIMEOUT) {
            TerminateProcess(process, 0U);
            WaitForSingleObject(process, 500U);
        }
        CloseHandle(process);
        process = nullptr;
#else
        if (process <= 0)
            return;
        int status = 0;
        pid_t result = waitpid(process, &status, WNOHANG);
        if (force && result == 0) {
            for (int attempt = 0; attempt < 10 && result == 0; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                result = waitpid(process, &status, WNOHANG);
            }
            if (result == 0) {
                kill(process, SIGTERM);
                waitpid(process, &status, 0);
            }
        }
        process = -1;
#endif
    }

    void set_failed(std::string detail) {
        healthy.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);
        std::scoped_lock lock(state_mutex);
        latest_state = {};
        status_detail = std::move(detail);
    }

    void read_states() {
        while (running.load(std::memory_order_acquire)) {
            StatePacket packet{};
            if (!ReceiveAll(socket, &packet, sizeof(packet))) {
                if (running.load(std::memory_order_acquire)) {
                    set_failed("The SDL3 input host disconnected; SDL2 fallback is required.");
                }
                return;
            }
            if (!valid_header(packet.header, PacketType::State, sizeof(packet), token) ||
                packet.device_count > packet.devices.size()) {
                set_failed("The SDL3 input host sent an invalid protocol packet.");
                return;
            }
            State next{};
            next.sequence = packet.sequence;
            next.devices.reserve(packet.device_count);
            for (std::size_t index = 0U; index < packet.device_count; ++index) {
                const DevicePacket& source = packet.devices[index];
                Device device{};
                device.instance = source.instance;
                device.persistent_key = FixedString(source.persistent_key);
                device.name = FixedString(source.name);
                device.rumble = source.rumble != 0U;
                device.gyro = source.gyro != 0U;
                device.mapped = source.mapped != 0U;
                device.mapping_source = FixedString(source.mapping_source);
                device.snapshot.connected = source.connected != 0U;
                device.snapshot.buttons = source.buttons;
                device.snapshot.button_bound = source.button_bound;
                device.snapshot.axes = source.axes;
                device.snapshot.gyro.available = source.gyro != 0U;
                device.snapshot.gyro.valid = source.gyro_valid != 0U;
                device.snapshot.gyro.sensor_timestamp_us = source.gyro_timestamp_us;
                device.snapshot.gyro.sample_rate_hz = source.gyro_rate_hz;
                device.snapshot.gyro.data = source.gyro_data;
                next.devices.push_back(std::move(device));
            }
            {
                std::scoped_lock lock(state_mutex);
                latest_state = std::move(next);
                status_detail = "SDL3 native input host connected.";
            }
            healthy.store(true, std::memory_order_release);
        }
    }
};

dkr::runtime::sdl3_input::Client::Client() : impl_(std::make_unique<Impl>()) {}

dkr::runtime::sdl3_input::Client::~Client() {
    stop();
}

bool dkr::runtime::sdl3_input::Client::start(const std::filesystem::path& host,
                                             const std::filesystem::path& mapping_database, std::string& error,
                                             std::stop_token stop_token) {
    stop();
    if (!std::filesystem::is_regular_file(host)) {
        error = "SDL3 input host is missing: " + host.string();
        return false;
    }
    if (!impl_->start_socket_runtime(error))
        return false;

    NativeSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        error = "Could not create the SDL3 input host listener.";
        impl_->stop_socket_runtime();
        return false;
    }
    int enabled = 1;
    setsockopt(listener, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 1) != 0) {
        error = "Could not bind the private SDL3 input host listener.";
        CloseSocket(listener);
        impl_->stop_socket_runtime();
        return false;
    }
#if defined(_WIN32)
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
        error = "Could not determine the SDL3 input host listener port.";
        CloseSocket(listener);
        impl_->stop_socket_runtime();
        return false;
    }

    impl_->token = RandomToken();
    if (!impl_->spawn(host, ntohs(address.sin_port), mapping_database, error)) {
        CloseSocket(listener);
        impl_->stop_socket_runtime();
        return false;
    }
    if (!WaitReadable(listener, std::chrono::milliseconds(4000), stop_token)) {
        error = stop_token.stop_requested() ? "SDL3 input initialization was cancelled."
                                            : "The private SDL3 input host did not connect within four seconds.";
        CloseSocket(listener);
        impl_->reap_process(true);
        impl_->stop_socket_runtime();
        return false;
    }
    impl_->socket = accept(listener, nullptr, nullptr);
    CloseSocket(listener);
    if (impl_->socket == kInvalidSocket) {
        error = "The private SDL3 input host connection was not accepted.";
        impl_->reap_process(true);
        impl_->stop_socket_runtime();
        return false;
    }
    setsockopt(impl_->socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    if (!WaitReadable(impl_->socket, std::chrono::milliseconds(4000), stop_token)) {
        error = stop_token.stop_requested() ? "SDL3 input initialization was cancelled."
                                            : "The SDL3 input host did not finish initialization.";
        stop();
        return false;
    }
    HelloPacket hello{};
    if (!ReceiveAll(impl_->socket, &hello, sizeof(hello)) ||
        !valid_header(hello.header, PacketType::Hello, sizeof(hello), impl_->token)) {
        error = "The SDL3 input host handshake was invalid.";
        stop();
        return false;
    }
    if (hello.ready == 0U) {
        error = FixedString(hello.detail);
        if (error.empty())
            error = "SDL3 input initialization failed.";
        stop();
        return false;
    }
    {
        std::scoped_lock lock(impl_->state_mutex);
        impl_->status_detail = "SDL3 " + std::to_string(hello.sdl_major) + "." + std::to_string(hello.sdl_minor) + "." +
                               std::to_string(hello.sdl_micro) + " native input host connected.";
    }
    impl_->running.store(true, std::memory_order_release);
    impl_->healthy.store(true, std::memory_order_release);
    impl_->reader = std::thread([this]() { impl_->read_states(); });
    return true;
}

void dkr::runtime::sdl3_input::Client::stop() {
    if (!impl_)
        return;
    const bool was_running = impl_->running.exchange(false, std::memory_order_acq_rel);
    if (impl_->socket != kInvalidSocket) {
        if (was_running) {
            ShutdownPacket packet{};
            initialise(packet, PacketType::Shutdown, impl_->token);
            std::scoped_lock lock(impl_->send_mutex);
            SendAll(impl_->socket, &packet, sizeof(packet));
        }
        ShutdownSocket(impl_->socket);
    }
    if (impl_->reader.joinable())
        impl_->reader.join();
    CloseSocket(impl_->socket);
    impl_->healthy.store(false, std::memory_order_release);
    impl_->reap_process(true);
    impl_->stop_socket_runtime();
    {
        std::scoped_lock lock(impl_->state_mutex);
        impl_->latest_state = {};
    }
}

bool dkr::runtime::sdl3_input::Client::healthy() const {
    return impl_ && impl_->healthy.load(std::memory_order_acquire);
}

std::string dkr::runtime::sdl3_input::Client::detail() const {
    std::scoped_lock lock(impl_->state_mutex);
    return impl_->status_detail;
}

dkr::runtime::sdl3_input::State dkr::runtime::sdl3_input::Client::state() const {
    std::scoped_lock lock(impl_->state_mutex);
    return impl_->latest_state;
}

bool dkr::runtime::sdl3_input::Client::rumble(int instance, std::uint16_t low_frequency, std::uint16_t high_frequency,
                                              std::uint32_t duration_ms) {
    if (!healthy() || impl_->socket == kInvalidSocket)
        return false;
    RumblePacket packet{};
    initialise(packet, PacketType::Rumble, impl_->token);
    packet.instance = instance;
    packet.low_frequency = low_frequency;
    packet.high_frequency = high_frequency;
    packet.duration_ms = duration_ms;
    std::scoped_lock lock(impl_->send_mutex);
    return SendAll(impl_->socket, &packet, sizeof(packet));
}
