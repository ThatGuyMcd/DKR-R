#pragma once
#include "party_link.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <span>
#include <vector>

namespace dkr::runtime::party_departure {
using MemberId = spectators::MemberId;
inline constexpr auto SilenceLimit = std::chrono::seconds(10);
inline constexpr auto CloseGrace = std::chrono::milliseconds(400);
struct Close {
    MemberId member = 0;
    bool acknowledged = false;
};
struct Registry {
    std::mutex mutex;
    std::map<const void*, Close> entries;
};
inline Registry& registry() {
    static auto* value = new Registry;
    return *value;
}
inline void begin(const void* owner, MemberId id) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    r.entries[owner] = {id, false};
}
inline bool waiting(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    return r.entries.contains(owner);
}
inline bool acknowledged(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto p = r.entries.find(owner);
    return p != r.entries.end() && p->second.acknowledged;
}
inline void acknowledge(const void* owner, MemberId id) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    const auto p = r.entries.find(owner);
    if (p != r.entries.end() && p->second.member == id)
        p->second.acknowledged = true;
}
inline void forget(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    r.entries.erase(owner);
}
// Outer room, sender, key and replay validation are still owned by DirectSession.
// A receipt names the closing member, never a controller port or a new owner.
inline std::vector<std::uint8_t> notice(unsigned stage, MemberId id) {
    std::vector<std::uint8_t> b{'D', 'K', 'L', 'V', 1, std::uint8_t(stage)};
    for (unsigned i = 0; i < 8; ++i)
        b.push_back(std::uint8_t(id >> (i * 8)));
    return b;
}
inline bool notice(std::span<const std::uint8_t> b, unsigned stage, MemberId id) {
    if (!id || b.size() != 14 || b[0] != 'D' || b[1] != 'K' || b[2] != 'L' || b[3] != 'V' || b[4] != 1 || b[5] != stage)
        return false;
    MemberId value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= MemberId(b[6 + i]) << (i * 8);
    return value == id;
}
} // namespace dkr::runtime::party_departure
