#include "transport.hpp"
#include "netplay/session_transport.hpp"
#include <utility>

namespace dkr::runtime::netplay::links {
std::string local_peer_name(const SessionTransport& transport) {
    if (const auto* native = dynamic_cast<const IntroducedTransport*>(&transport))
        return native->local_peer_name();
    return {};
}
std::string peer_name(const SessionTransport& transport, const PeerAddress& address) {
    if (const auto* native = dynamic_cast<const IntroducedTransport*>(&transport))
        return native->peer_name(address);
    return transport.quick_join() ? std::string{} : DatagramSocket::describe(address);
}
bool introduce_peer(SessionTransport& transport, std::string_view endpoint, bool initiate, PeerAddress& address,
                    std::string& error) {
    if (auto* native = dynamic_cast<IntroducedTransport*>(&transport))
        return native->introduce_peer(endpoint, initiate, address, error);
    if (transport.quick_join()) {
        error = "This transport does not support introduced peers.";
        return false;
    }
    const auto colon = endpoint.rfind(':');
    if (colon == endpoint.npos) {
        error = "Invalid introduced endpoint.";
        return false;
    }
    unsigned port = 0;
    for (const auto c : endpoint.substr(colon + 1)) {
        if (c < '0' || c > '9' || port > 6553) {
            error = "Invalid introduced port.";
            return false;
        }
        port = port * 10 + unsigned(c - '0');
    }
    if (!port || port > 65535) {
        error = "Invalid introduced port.";
        return false;
    }
    return DatagramSocket::resolve(endpoint.substr(0, colon), std::uint16_t(port), address, error);
}
bool prepare_host(SessionTransport& transport, std::string bootstrap, std::string& error) {
    if (auto* native = dynamic_cast<IntroducedTransport*>(&transport))
        return native->prepare_host(std::move(bootstrap), error);
    if (!transport.quick_join() && transport.is_open()) {
        error.clear();
        return true;
    }
    error = "This transport cannot prepare a host handover.";
    return false;
}
bool alias_peer(SessionTransport& transport, const PeerAddress& address, std::string_view name, std::string& error) {
    if (auto* native = dynamic_cast<IntroducedTransport*>(&transport))
        return native->alias_peer(address, name, error);
    if (!transport.quick_join()) {
        error.clear();
        return true;
    }
    error = "This transport cannot prepare a peer alias.";
    return false;
}
bool apply_host_role(SessionTransport& transport, bool host, std::string_view remote, std::string& error) {
    if (auto* native = dynamic_cast<IntroducedTransport*>(&transport))
        return native->apply_host_role(host, remote, error);
    if (!transport.quick_join()) {
        error.clear();
        return true;
    }
    error = "This transport cannot transfer host ownership.";
    return false;
}
} // namespace dkr::runtime::netplay::links
