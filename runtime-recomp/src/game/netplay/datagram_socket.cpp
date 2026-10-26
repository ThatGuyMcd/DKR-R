#include "datagram_socket.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dkr::runtime::netplay {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
int last_error() {
    return WSAGetLastError();
}
void close_socket(SocketHandle socket) {
    closesocket(socket);
}
bool would_block(int error) {
    return error == WSAEWOULDBLOCK;
}

bool initialise_sockets(std::string& error) {
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready)
        error = "Windows Sockets could not be initialized.";
    return ready;
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
int last_error() {
    return errno;
}
void close_socket(SocketHandle socket) {
    ::close(socket);
}
bool would_block(int error) {
    return error == EAGAIN || error == EWOULDBLOCK;
}
bool initialise_sockets(std::string&) {
    return true;
}
#endif

const sockaddr* address_ptr(const PeerAddress& address) {
    return reinterpret_cast<const sockaddr*>(address.storage.data());
}

sockaddr* address_ptr(PeerAddress& address) {
    return reinterpret_cast<sockaddr*>(address.storage.data());
}

std::string socket_error(std::string_view operation, int code) {
    return std::string(operation) + " failed (socket error " + std::to_string(code) + ").";
}

} // namespace

struct DatagramSocket::Impl {
    SocketHandle handle = kInvalidSocket;
    std::uint16_t port = 0;
};

bool PeerAddress::operator==(const PeerAddress& other) const {
    return size == other.size && size <= storage.size() &&
           std::equal(storage.begin(), storage.begin() + size, other.storage.begin());
}

DatagramSocket::DatagramSocket() : impl_(std::make_unique<Impl>()) {}
DatagramSocket::~DatagramSocket() {
    close();
}

bool DatagramSocket::open(std::uint16_t port, std::string& error) {
    close();
    if (!initialise_sockets(error))
        return false;
    const SocketHandle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == kInvalidSocket) {
        error = socket_error("Creating the UDP socket", last_error());
        return false;
    }
    // Full-state recovery checkpoints are fragmented into several datagrams.
    // A generous kernel queue prevents a burst from evicting the input/frame
    // packets that immediately follow it on Windows, Linux and SteamOS.
    constexpr int requested_buffer_bytes = 1 << 20;
    if (::setsockopt(handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&requested_buffer_bytes),
                     sizeof(requested_buffer_bytes)) != 0 ||
        ::setsockopt(handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&requested_buffer_bytes),
                     sizeof(requested_buffer_bytes)) != 0) {
        error = socket_error("Configuring the UDP queue", last_error());
        close_socket(handle);
        return false;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        error = socket_error("Binding the UDP socket", last_error());
        close_socket(handle);
        return false;
    }
#if defined(_WIN32)
    u_long nonblocking = 1;
    if (ioctlsocket(handle, FIONBIO, &nonblocking) != 0) {
#else
    const int flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0 || fcntl(handle, F_SETFL, flags | O_NONBLOCK) != 0) {
#endif
        error = socket_error("Making the UDP socket non-blocking", last_error());
        close_socket(handle);
        return false;
    }
    sockaddr_in actual{};
#if defined(_WIN32)
    int actual_size = sizeof(actual);
#else
    socklen_t actual_size = sizeof(actual);
#endif
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&actual), &actual_size) != 0) {
        error = socket_error("Reading the UDP socket address", last_error());
        close_socket(handle);
        return false;
    }
    impl_->handle = handle;
    impl_->port = ntohs(actual.sin_port);
    error.clear();
    return true;
}

void DatagramSocket::close() {
    if (impl_->handle != kInvalidSocket)
        close_socket(impl_->handle);
    impl_->handle = kInvalidSocket;
    impl_->port = 0;
}

bool DatagramSocket::is_open() const {
    return impl_->handle != kInvalidSocket;
}
std::uint16_t DatagramSocket::local_port() const {
    return impl_->port;
}

bool DatagramSocket::send(const PeerAddress& destination, std::span<const std::uint8_t> bytes, std::string& error) {
    return send_status(destination, bytes, error) == DatagramSendStatus::Sent;
}

DatagramSendStatus DatagramSocket::send_status(const PeerAddress& destination, std::span<const std::uint8_t> bytes,
                                               std::string& error) {
    if (!is_open() || !destination || bytes.empty() ||
        bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        error = "The UDP send request is invalid.";
        return DatagramSendStatus::Error;
    }
    const int sent =
        ::sendto(impl_->handle, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                 address_ptr(destination), static_cast<int>(destination.size));
    if (sent != static_cast<int>(bytes.size())) {
        const int code = last_error();
        if (would_block(code)) {
            error.clear();
            return DatagramSendStatus::WouldBlock;
        }
        error = socket_error("Sending a UDP datagram", code);
        return DatagramSendStatus::Error;
    }
    error.clear();
    return DatagramSendStatus::Sent;
}

bool DatagramSocket::receive(PeerAddress& source, std::vector<std::uint8_t>& bytes, std::string& error) {
    bytes.clear();
    source = {};
    if (!is_open()) {
        error = "The UDP socket is not open.";
        return false;
    }
    std::array<std::uint8_t, 2048> buffer{};
#if defined(_WIN32)
    int source_size = static_cast<int>(source.storage.size());
    // An oversized datagram fails with WSAEMSGSIZE, handled below.
    const int received = ::recvfrom(impl_->handle, reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0, address_ptr(source), &source_size);
#else
    // MSG_TRUNC as an input flag (return the real length) is Linux-only.
    // recvmsg reports truncation portably through msg_flags.
    iovec segment{buffer.data(), buffer.size()};
    msghdr message{};
    message.msg_name = address_ptr(source);
    message.msg_namelen = static_cast<socklen_t>(source.storage.size());
    message.msg_iov = &segment;
    message.msg_iovlen = 1;
    const auto received = ::recvmsg(impl_->handle, &message, 0);
    const socklen_t source_size = message.msg_namelen;
#endif
    if (received < 0) {
        const int code = last_error();
        // These errors do not invalidate this unconnected UDP socket.
        // Oversized datagrams are discarded before authentication. ICMP
        // errors cannot identify an authenticated peer; the session's
        // progress/liveness policy, not an arbitrary datagram, owns expiry.
#if defined(_WIN32)
        const bool transient =
            code == WSAEMSGSIZE || code == WSAEINTR || code == WSAECONNRESET || code == WSAECONNREFUSED;
#else
        const bool transient = code == EINTR || code == EMSGSIZE || code == ECONNREFUSED;
#endif
        if (would_block(code) || transient) {
            error.clear();
            return false;
        }
        error = socket_error("Receiving a UDP datagram", code);
        return false;
    }
#if !defined(_WIN32)
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        error.clear();
        return false;
    }
#endif
    source.size = static_cast<std::uint32_t>(source_size);
    bytes.assign(buffer.begin(), buffer.begin() + received);
    error.clear();
    return true;
}

bool DatagramSocket::resolve(std::string_view host, std::uint16_t port, PeerAddress& address, std::string& error) {
    address = {};
    if (!initialise_sockets(error))
        return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* results = nullptr;
    const std::string host_text(host);
    const std::string port_text = std::to_string(port);
    const int result = getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &results);
    if (result != 0 || results == nullptr || results->ai_addrlen > address.storage.size()) {
        if (results != nullptr)
            freeaddrinfo(results);
        error = "The host name could not be resolved.";
        return false;
    }
    address.size = static_cast<std::uint32_t>(results->ai_addrlen);
    std::memcpy(address.storage.data(), results->ai_addr, address.size);
    freeaddrinfo(results);
    error.clear();
    return true;
}

std::string DatagramSocket::describe(const PeerAddress& address) {
    if (!address)
        return {};
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    if (getnameinfo(address_ptr(address), static_cast<socklen_t>(address.size), host.data(),
                    static_cast<socklen_t>(host.size()), service.data(), static_cast<socklen_t>(service.size()),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return {};
    }
    return std::string(host.data()) + ":" + service.data();
}

std::string DatagramSocket::describe_host(const PeerAddress& address) {
    if (!address)
        return {};
    std::array<char, NI_MAXHOST> host{};
    if (getnameinfo(address_ptr(address), static_cast<socklen_t>(address.size), host.data(),
                    static_cast<socklen_t>(host.size()), nullptr, 0, NI_NUMERICHOST) != 0) {
        return {};
    }
    return host.data();
}

} // namespace dkr::runtime::netplay
