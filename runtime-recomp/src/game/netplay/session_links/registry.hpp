#pragma once
#include "netplay/datagram_socket.hpp"
#include "netplay/secure_channel.hpp"
#include <optional>

namespace dkr::runtime::netplay::links {
// Installed only by the native owner after an authenticated host introduction.
// This key grants no host privileges; the party decoder still checks message
// kind, current controller owner and immutable input lease after AEAD validation.
bool install(const void* session, std::uint64_t room, std::uint64_t peer, const PeerAddress&, const secure::Key&);
std::optional<secure::Key> receive_key(const void* session, std::uint64_t room, std::uint64_t peer, const PeerAddress&);
void revoke(const void* session, std::uint64_t peer);
bool retire_route(const void* session, std::uint64_t room, const PeerAddress&);
void forget(const void* session);
} // namespace dkr::runtime::netplay::links
