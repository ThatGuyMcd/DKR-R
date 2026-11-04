#pragma once
#include "core.hpp"
#include "netplay/session_transport.hpp"
#include <atomic>

namespace dkr::relay {
namespace net=dkr::runtime::netplay;
inline std::atomic<bool> host_enabled{false},volunteer_enabled{false};
struct Address {Id id=0;net::PeerAddress address;};
struct NetworkOptions {std::vector<std::string> ice_servers{"stun:stun.l.google.com:19302"};};
std::unique_ptr<net::SessionTransport> wrap(const void* session,std::unique_ptr<net::SessionTransport> transport,NetworkOptions options={});
void configure(const void* session,Context context,std::vector<Address> addresses);
bool control(const void* session,Id source,std::span<const std::uint8_t> bytes);
std::vector<Control> controls(const void* session);
View view(const void* session);
unsigned rtt(const void* session,Id peer);
void reset(const void* session);
void recover(const void* session,Id peer=0);
} // namespace dkr::relay
