#include "registry.hpp"
#include <algorithm>
#include <map>
#include <mutex>

namespace dkr::runtime::netplay::links {
namespace {
struct Remote {
    std::uint64_t room;
    PeerAddress address;
    secure::Key key;
};
struct Registry {
    std::mutex mutex;
    std::map<const void*, std::map<std::uint64_t, Remote>> sessions;
};
Registry& registry() {
    static auto* const value = new Registry;
    return *value;
}
void wipe(Remote& remote) {
    volatile std::uint8_t* p = remote.key.data();
    for (unsigned i = 0; i < remote.key.size(); ++i)
        p[i] = 0;
}
} // namespace
bool install(const void* owner, std::uint64_t room, std::uint64_t peer, const PeerAddress& address,
             const secure::Key& key) {
    if (!owner || !room || !peer || !address || std::all_of(key.begin(), key.end(), [](auto c) { return c == 0; }))
        return false;
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    auto& peers = r.sessions[owner];
    if (peers.size() >= 11 && !peers.contains(peer))
        return false;
    if (auto old = peers.find(peer); old != peers.end()) {
        if (old->second.room == room && old->second.address == address && old->second.key == key)
            return true;
        wipe(old->second);
    }
    peers.insert_or_assign(peer, Remote{room, address, key});
    return true;
}
std::optional<secure::Key> receive_key(const void* owner, std::uint64_t room, std::uint64_t peer,
                                       const PeerAddress& address) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto session = r.sessions.find(owner);
    if (session == r.sessions.end())
        return {};
    const auto remote = session->second.find(peer);
    if (remote == session->second.end() || remote->second.room != room || remote->second.address != address)
        return {};
    return remote->second.key;
}
void revoke(const void* owner, std::uint64_t peer) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto session = r.sessions.find(owner);
    if (session == r.sessions.end())
        return;
    const auto remote = session->second.find(peer);
    if (remote != session->second.end()) {
        wipe(remote->second);
        session->second.erase(remote);
    }
    if (session->second.empty())
        r.sessions.erase(session);
}
void forget(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto session = r.sessions.find(owner);
    if (session == r.sessions.end())
        return;
    for (auto& [id, remote] : session->second)
        wipe(remote);
    r.sessions.erase(session);
}
bool retire_route(const void* owner, std::uint64_t room, const PeerAddress& address) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto session = r.sessions.find(owner);
    if (session == r.sessions.end())
        return false;
    bool removed = false;
    for (auto it = session->second.begin(); it != session->second.end();) {
        if (it->second.room == room && it->second.address == address) {
            wipe(it->second);
            it = session->second.erase(it);
            removed = true;
        } else
            ++it;
    }
    if (session->second.empty())
        r.sessions.erase(session);
    return removed;
}
} // namespace dkr::runtime::netplay::links
