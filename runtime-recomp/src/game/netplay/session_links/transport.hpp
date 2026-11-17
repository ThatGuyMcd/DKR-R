#pragma once
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {
class SessionTransport;
struct PeerAddress;
namespace links {
// Optional first-party interface. The frozen SessionTransport ABI is intact.
// Only the authenticated native owner may introduce routes or change roles.
class IntroducedTransport {
  public:
    virtual ~IntroducedTransport() = default;
    virtual std::string local_peer_name() const = 0;
    virtual std::string peer_name(const PeerAddress&) const = 0;
    virtual bool introduce_peer(std::string_view, bool, PeerAddress&, std::string&) = 0;
    virtual bool prepare_host(std::string, std::string&) = 0;
    virtual bool alias_peer(const PeerAddress&, std::string_view, std::string&) = 0;
    virtual bool apply_host_role(bool, std::string_view, std::string&) = 0;
};
std::string local_peer_name(const SessionTransport&);
std::string peer_name(const SessionTransport&, const PeerAddress&);
bool introduce_peer(SessionTransport&, std::string_view, bool, PeerAddress&, std::string&);
bool prepare_host(SessionTransport&, std::string, std::string&);
bool alias_peer(SessionTransport&, const PeerAddress&, std::string_view, std::string&);
bool apply_host_role(SessionTransport&, bool, std::string_view, std::string&);
} // namespace links
} // namespace dkr::runtime::netplay
