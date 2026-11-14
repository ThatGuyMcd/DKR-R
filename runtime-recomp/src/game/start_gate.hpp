#pragma once
#include "party_link.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

// Authored through the Patch Pipeline. DirectSession's checked-out header and
// ABI stay unchanged. Only its owner accesses a Gate, under its existing mutex.
namespace dkr::runtime::party_start {
using Clock = std::chrono::steady_clock;
using Id = std::uint64_t;
enum class Phase { Idle, Countdown, Committing, Released, Cancelled };
struct Gate {
    Phase phase = Phase::Idle;
    std::optional<netplay::LaunchDescriptor> launch;
    netplay::Rules rules{};
    std::uint64_t roster = 0, save_hash = 0, hash = 0, retired = 0;
    std::uint32_t save_generation = 0;
    std::set<Id> prepared, committed;
    Clock::time_point deadline{}, retry{}, progress{};
};
struct Registry {
    std::mutex mutex;
    std::map<const void*, std::shared_ptr<Gate>> entries;
};
inline Registry& registry() {
    static auto* value = new Registry;
    return *value;
}
inline std::shared_ptr<Gate> gate(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    auto& value = r.entries[owner];
    if (!value)
        value = std::make_shared<Gate>();
    return value;
}
inline void forget(const void* owner) {
    auto& r = registry();
    std::scoped_lock lock(r.mutex);
    r.entries.erase(owner);
}
// Receipt/cancel metadata is inside the authenticated party envelope. It binds
// the descriptor, current roster and verified EEPROM identity, never a slot claim.
inline std::vector<std::uint8_t> receipt(unsigned stage, const Gate& g) {
    if (stage < 1 || stage > 3 || !g.hash || !g.roster || !g.save_generation || !g.save_hash)
        return {};
    std::vector<std::uint8_t> b{'D', 'K', 'S', 'T', 1, std::uint8_t(stage)};
    auto put = [&](std::uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            b.push_back(std::uint8_t(v >> (8 * i)));
    };
    put(g.hash, 8);
    put(g.roster, 8);
    put(g.save_generation, 4);
    put(g.save_hash, 8);
    return b;
}
inline unsigned receipt(std::span<const std::uint8_t> b, const Gate& g) {
    if (b.size() != 34 || b[0] != 'D' || b[1] != 'K' || b[2] != 'S' || b[3] != 'T' || b[4] != 1 || b[5] < 1 || b[5] > 3)
        return 0;
    auto get = [&](unsigned at, unsigned n) {
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(b[at + i]) << (8 * i);
        return v;
    };
    return get(6, 8) == g.hash && get(14, 8) == g.roster && get(22, 4) == g.save_generation && get(26, 8) == g.save_hash
               ? b[5]
               : 0;
}
} // namespace dkr::runtime::party_start
