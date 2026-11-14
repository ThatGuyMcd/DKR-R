#include "port_mapping.hpp"

#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
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
#include <natupnp.h>
#include <oleauto.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace dkr::runtime::netplay {
namespace {

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

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((data[0] << 8U) | data[1]);
}

std::uint32_t read32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) | data[3];
}

void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 8U);
    data[1] = static_cast<std::uint8_t>(value);
}

void write32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 24U);
    data[1] = static_cast<std::uint8_t>(value >> 16U);
    data[2] = static_cast<std::uint8_t>(value >> 8U);
    data[3] = static_cast<std::uint8_t>(value);
}

bool default_gateway(sockaddr_in& gateway) {
    gateway = {};
    gateway.sin_family = AF_INET;
    gateway.sin_port = htons(5351U);
#if defined(_WIN32)
    MIB_IPFORWARDROW route{};
    if (GetBestRoute(inet_addr("1.1.1.1"), 0U, &route) != NO_ERROR || route.dwForwardNextHop == 0U)
        return false;
    gateway.sin_addr.s_addr = route.dwForwardNextHop;
    return true;
#elif defined(__linux__)
    std::ifstream routes("/proc/net/route");
    std::string line;
    std::getline(routes, line);
    while (std::getline(routes, line)) {
        std::istringstream fields(line);
        std::string interface_name;
        std::string destination;
        std::string gateway_hex;
        unsigned flags = 0U;
        if (!(fields >> interface_name >> destination >> gateway_hex >> std::hex >> flags))
            continue;
        if (destination != "00000000" || (flags & 0x2U) == 0U)
            continue;
        unsigned long raw = 0U;
        const auto parsed = std::from_chars(gateway_hex.data(), gateway_hex.data() + gateway_hex.size(), raw, 16);
        if (parsed.ec != std::errc{})
            continue;
        gateway.sin_addr.s_addr = static_cast<std::uint32_t>(raw);
        return true;
    }
    return false;
#else
    return false;
#endif
}

bool exchange_nat_pmp(const sockaddr_in& gateway, std::span<const std::uint8_t> request,
                      std::vector<std::uint8_t>& response) {
#if defined(_WIN32)
    static const bool sockets_ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!sockets_ready)
        return false;
#endif
    Socket socket_value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_value == kInvalidSocket)
        return false;
#if defined(_WIN32)
    DWORD timeout = 700U;
#else
    timeval timeout{0, 700000};
#endif
    setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    const int sent =
        sendto(socket_value, reinterpret_cast<const char*>(request.data()), static_cast<int>(request.size()), 0,
               reinterpret_cast<const sockaddr*>(&gateway), sizeof(gateway));
    if (sent != static_cast<int>(request.size())) {
        close_socket(socket_value);
        return false;
    }
    std::array<std::uint8_t, 64> buffer{};
    sockaddr_in source{};
#if defined(_WIN32)
    int source_size = sizeof(source);
#else
    socklen_t source_size = sizeof(source);
#endif
    const int received = recvfrom(socket_value, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
                                  0, reinterpret_cast<sockaddr*>(&source), &source_size);
    close_socket(socket_value);
    if (received <= 0)
        return false;
    response.assign(buffer.begin(), buffer.begin() + received);
    return true;
}

bool try_nat_pmp(std::uint16_t local_port, std::uint32_t lifetime, std::string& external_address,
                 std::uint16_t& external_port) {
    sockaddr_in gateway{};
    if (!default_gateway(gateway))
        return false;
    std::vector<std::uint8_t> response;
    const std::array<std::uint8_t, 2> address_request{0U, 0U};
    if (!exchange_nat_pmp(gateway, address_request, response) || response.size() < 12U || response[0] != 0U ||
        response[1] != 128U || read16(response.data() + 2U) != 0U)
        return false;
    char address[INET_ADDRSTRLEN]{};
    in_addr external{};
    std::memcpy(&external.s_addr, response.data() + 8U, 4U);
    if (inet_ntop(AF_INET, &external, address, sizeof(address)) == nullptr)
        return false;
    std::array<std::uint8_t, 12> mapping{};
    mapping[1] = 1U;
    write16(mapping.data() + 4U, local_port);
    write16(mapping.data() + 6U, local_port);
    write32(mapping.data() + 8U, lifetime);
    response.clear();
    if (!exchange_nat_pmp(gateway, mapping, response) || response.size() < 16U || response[0] != 0U ||
        response[1] != 129U || read16(response.data() + 2U) != 0U || read16(response.data() + 8U) != local_port)
        return false;
    external_address = address;
    external_port = read16(response.data() + 10U);
    return external_port != 0U;
}

#if defined(_WIN32)
bool try_upnp(std::string_view local_address, std::uint16_t local_port, IStaticPortMappingCollection** retained,
              std::string& error) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return false;
    IUPnPNAT* nat = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(UPnPNAT), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nat));
    if (FAILED(result) || nat == nullptr)
        return false;
    IStaticPortMappingCollection* mappings = nullptr;
    result = nat->get_StaticPortMappingCollection(&mappings);
    nat->Release();
    if (FAILED(result) || mappings == nullptr)
        return false;
    const int wide_size =
        MultiByteToWideChar(CP_UTF8, 0, local_address.data(), static_cast<int>(local_address.size()), nullptr, 0);
    std::wstring wide_address(static_cast<std::size_t>(wide_size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, local_address.data(), static_cast<int>(local_address.size()), wide_address.data(),
                        wide_size);
    BSTR protocol = SysAllocString(L"UDP");
    BSTR client = SysAllocStringLen(wide_address.data(), static_cast<UINT>(wide_address.size()));
    BSTR description = SysAllocString(L"DKR-R private multiplayer");
    IStaticPortMapping* mapping = nullptr;
    result = mappings->Add(local_port, protocol, local_port, client, VARIANT_TRUE, description, &mapping);
    SysFreeString(protocol);
    SysFreeString(client);
    SysFreeString(description);
    if (mapping != nullptr)
        mapping->Release();
    if (FAILED(result)) {
        mappings->Release();
        error = "The router refused the UPnP mapping request.";
        return false;
    }
    *retained = mappings;
    return true;
}
#endif

} // namespace

struct PortMapping::Impl {
    PortMappingView view{};
    std::string local_address;
    std::uint16_t local_port = 0U;
#if defined(_WIN32)
    IStaticPortMappingCollection* upnp_mappings = nullptr;
#endif
};

PortMapping::PortMapping() : impl_(std::make_unique<Impl>()) {}
PortMapping::~PortMapping() {
    close();
}

bool PortMapping::open(std::string local_address, std::uint16_t local_port, std::string& error) {
    close();
    impl_->local_address = std::move(local_address);
    impl_->local_port = local_port;
    std::string external_address;
    std::uint16_t external_port = 0U;
    if (try_nat_pmp(local_port, 7200U, external_address, external_port)) {
        impl_->view = {PortMappingState::Mapped, "NAT-PMP", external_address, external_port,
                       "Temporary router mapping created."};
        error.clear();
        return true;
    }
#if defined(_WIN32)
    std::string upnp_error;
    if (try_upnp(impl_->local_address, local_port, &impl_->upnp_mappings, upnp_error)) {
        // Windows' static UPnP mapping API does not report the router's
        // Internet-facing address. Keep that field empty so the launcher
        // retains the public address supplied by the host instead of placing
        // an unroutable LAN address in the invitation.
        impl_->view = {PortMappingState::Mapped, "UPnP", {}, local_port, "Temporary UPnP router mapping created."};
        error.clear();
        return true;
    }
#endif
    impl_->view = {PortMappingState::ManualRequired,
                   "Manual",
                   {},
                   local_port,
                   "Automatic router setup was unavailable. Use Virtual LAN or forward the UDP port manually."};
    error = impl_->view.message;
    return false;
}

void PortMapping::close() {
    if (!impl_)
        return;
    if (impl_->view.method == "NAT-PMP" && impl_->local_port != 0U) {
        std::string ignored_address;
        std::uint16_t ignored_port = 0U;
        try_nat_pmp(impl_->local_port, 0U, ignored_address, ignored_port);
    }
#if defined(_WIN32)
    if (impl_->upnp_mappings != nullptr && impl_->local_port != 0U) {
        BSTR protocol = SysAllocString(L"UDP");
        impl_->upnp_mappings->Remove(impl_->local_port, protocol);
        SysFreeString(protocol);
        impl_->upnp_mappings->Release();
        impl_->upnp_mappings = nullptr;
    }
#endif
    impl_->view = {};
    impl_->local_address.clear();
    impl_->local_port = 0U;
}

const PortMappingView& PortMapping::view() const {
    return impl_->view;
}

} // namespace dkr::runtime::netplay
