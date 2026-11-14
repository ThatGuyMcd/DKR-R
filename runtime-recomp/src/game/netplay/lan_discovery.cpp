#include "lan_discovery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dkr::runtime::netplay {
namespace {

constexpr std::uint16_t kDiscoveryPort = 24871U;
constexpr std::string_view kPrefix = "DKRL3|";
constexpr std::string_view kQuery = "DKRL3?";
constexpr auto kAdvertiseInterval = std::chrono::milliseconds(650);
constexpr auto kListingLifetime = std::chrono::seconds(3);

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void close_socket(Socket value) {
    closesocket(value);
}
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void close_socket(Socket value) {
    close(value);
}
#endif

bool safe_text(std::string_view value, std::size_t limit) {
    return !value.empty() && value.size() <= limit && std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return byte >= 0x20U && byte != 0x7FU && byte != '|';
    });
}

std::vector<std::string_view> split(std::string_view text) {
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t separator = text.find('|');
        fields.push_back(text.substr(0U, separator));
        if (separator == std::string_view::npos)
            break;
        text.remove_prefix(separator + 1U);
    }
    return fields;
}

template <typename T> bool number(std::string_view text, T& output, int base = 10) {
    unsigned long long value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value > static_cast<unsigned long long>((std::numeric_limits<T>::max)()))
        return false;
    output = static_cast<T>(value);
    return true;
}

std::string encode(const LanLobby& lobby) {
    if (!safe_text(lobby.room_name, kMaximumLobbyNameBytes) || !safe_text(lobby.host_name, kMaximumPlayerNameBytes) ||
        lobby.invite.empty() || lobby.invite.size() > 768U)
        return {};
    return std::string(kPrefix) + std::to_string(lobby.match_id) + "|" +
           std::to_string(static_cast<unsigned>(lobby.revision)) + "|" + std::to_string(lobby.players) + "|" +
           std::to_string(lobby.maximum_players) + "|" + (lobby.in_progress ? "1" : "0") + "|" + lobby.room_name + "|" +
           lobby.host_name + "|" + lobby.invite;
}

std::optional<LanLobby> decode(std::string_view packet) {
    if (!packet.starts_with(kPrefix))
        return std::nullopt;
    packet.remove_prefix(kPrefix.size());
    const auto fields = split(packet);
    if (fields.size() != 8U)
        return std::nullopt;
    LanLobby lobby{};
    std::uint8_t revision = 0U;
    std::uint8_t in_progress = 0U;
    if (!number(fields[0], lobby.match_id) || !number(fields[1], revision) ||
        revision > static_cast<std::uint8_t>(Revision::UsV80) || !number(fields[2], lobby.players) ||
        !number(fields[3], lobby.maximum_players) || !number(fields[4], in_progress) || in_progress > 1U ||
        lobby.players > lobby.maximum_players || lobby.maximum_players < 2U ||
        lobby.maximum_players > kSupportedOnlinePlayers || !safe_text(fields[5], kMaximumLobbyNameBytes) ||
        !safe_text(fields[6], kMaximumPlayerNameBytes) || fields[7].size() > 768U ||
        !fields[7].starts_with("dkr-r://v7/"))
        return std::nullopt;
    lobby.revision = static_cast<Revision>(revision);
    lobby.in_progress = in_progress != 0U;
    lobby.room_name = fields[5];
    lobby.host_name = fields[6];
    lobby.invite = fields[7];
    lobby.last_seen = std::chrono::steady_clock::now();
    return lobby;
}

} // namespace

struct LanDiscovery::Impl {
    Socket socket = kInvalidSocket;
    std::optional<LanLobby> advertised;
    std::chrono::steady_clock::time_point last_advertised{};
    std::chrono::steady_clock::time_point last_query{};
    std::vector<std::string> probe_addresses;
    bool refresh_requested = true;
};

LanDiscovery::LanDiscovery() : impl_(std::make_unique<Impl>()) {}
LanDiscovery::~LanDiscovery() {
    stop();
}

bool LanDiscovery::start(std::string& error) {
    if (running())
        return true;
#if defined(_WIN32)
    static const bool sockets_ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!sockets_ready) {
        error = "Windows Sockets could not start LAN discovery.";
        return false;
    }
#endif
    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == kInvalidSocket) {
        error = "DKR-R could not open the LAN discovery socket.";
        return false;
    }
    int enabled = 1;
    setsockopt(impl_->socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(kDiscoveryPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(impl_->socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        close_socket(impl_->socket);
        impl_->socket = kInvalidSocket;
        error = "Another application is preventing LAN lobby discovery.";
        return false;
    }
#if defined(_WIN32)
    u_long nonblocking = 1U;
    ioctlsocket(impl_->socket, FIONBIO, &nonblocking);
#else
    const int flags = fcntl(impl_->socket, F_GETFL, 0);
    fcntl(impl_->socket, F_SETFL, flags | O_NONBLOCK);
#endif
    error.clear();
    return true;
}

void LanDiscovery::stop() {
    if (running())
        close_socket(impl_->socket);
    impl_->socket = kInvalidSocket;
    impl_->advertised.reset();
    lobbies_.clear();
}

bool LanDiscovery::running() const {
    return impl_->socket != kInvalidSocket;
}
void LanDiscovery::advertise(const LanLobby& lobby) {
    impl_->advertised = lobby;
}
void LanDiscovery::clear_advertisement() {
    impl_->advertised.reset();
}
void LanDiscovery::set_probe_addresses(std::vector<std::string> addresses) {
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
    impl_->probe_addresses = std::move(addresses);
    impl_->refresh_requested = true;
}
void LanDiscovery::request_refresh() {
    impl_->refresh_requested = true;
}

void LanDiscovery::pump() {
    if (!running())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (impl_->advertised && now - impl_->last_advertised >= kAdvertiseInterval) {
        const std::string packet = encode(*impl_->advertised);
        if (!packet.empty()) {
            sockaddr_in destination{};
            destination.sin_family = AF_INET;
            destination.sin_port = htons(kDiscoveryPort);
            destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(impl_->socket, packet.data(), static_cast<int>(packet.size()), 0,
                   reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        }
        impl_->last_advertised = now;
    }
    if (impl_->refresh_requested || now - impl_->last_query >= kAdvertiseInterval) {
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(kDiscoveryPort);
        destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(impl_->socket, kQuery.data(), static_cast<int>(kQuery.size()), 0,
               reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        for (const std::string& address : impl_->probe_addresses) {
            destination.sin_addr.s_addr = inet_addr(address.c_str());
            if (destination.sin_addr.s_addr == INADDR_NONE)
                continue;
            sendto(impl_->socket, kQuery.data(), static_cast<int>(kQuery.size()), 0,
                   reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
        }
        impl_->last_query = now;
        impl_->refresh_requested = false;
    }
    std::array<char, 1024> buffer{};
    for (int count = 0; count < 32; ++count) {
        sockaddr_in source{};
#if defined(_WIN32)
        int source_size = sizeof(source);
#else
        socklen_t source_size = sizeof(source);
#endif
        const int received = recvfrom(impl_->socket, buffer.data(), static_cast<int>(buffer.size()), 0,
                                      reinterpret_cast<sockaddr*>(&source), &source_size);
        if (received <= 0)
            break;
        const std::string_view packet(buffer.data(), static_cast<std::size_t>(received));
        if (packet == kQuery) {
            if (impl_->advertised) {
                const std::string response = encode(*impl_->advertised);
                if (!response.empty()) {
                    sendto(impl_->socket, response.data(), static_cast<int>(response.size()), 0,
                           reinterpret_cast<const sockaddr*>(&source), source_size);
                }
            }
            continue;
        }
        auto lobby = decode(packet);
        if (!lobby || (impl_->advertised && lobby->match_id == impl_->advertised->match_id))
            continue;
        auto existing = std::find_if(lobbies_.begin(), lobbies_.end(),
                                     [&lobby](const LanLobby& value) { return value.match_id == lobby->match_id; });
        if (existing == lobbies_.end())
            lobbies_.push_back(std::move(*lobby));
        else
            *existing = std::move(*lobby);
    }
    std::erase_if(lobbies_, [now](const LanLobby& lobby) { return now - lobby.last_seen > kListingLifetime; });
}

} // namespace dkr::runtime::netplay
