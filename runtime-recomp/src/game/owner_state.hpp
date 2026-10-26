#pragma once
#include "online_directory.hpp"
#include "monocypher.h"
#include "admission_contract.hpp"
#include <map>
#include <set>

// All Owner fields are accessed with the existing DirectSession mutex held.
// Only registry lookup uses its own lock; callbacks never touch Owner fields.
namespace dkr::online::native {
struct Admission : contract::Proof {
    std::string id, account, epoch;
    Clock::time_point expires{};
    bool automatic = false, allow_running = false;
};
struct Owner {
    std::string epoch, policy, authorization;
    std::uint64_t privacy = 1, channel = 0;
    bool guarded = false, public_room = false;
    std::vector<Admission> admissions;
    std::set<std::string> blocked;
    Clock::time_point published{};
};
struct Registry {
    std::mutex mutex;
    std::map<const void*, std::shared_ptr<Owner>> owners;
};
inline Registry& registry() {
    static auto* p = new Registry;
    return *p;
}
inline std::shared_ptr<Owner> owner(const void* session) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    auto& p = r.owners[session];
    if (!p)
        p = std::make_shared<Owner>();
    return p;
}
inline void reset(const void* session) {
    auto p = owner(session);
    for (auto& a : p->admissions)
        crypto_wipe(a.capability.data(), 32);
    *p = {};
    dkr::online::publish(session, Json::object());
}
inline void forget(const void* session) {
    auto& r = registry();
    std::lock_guard lock(r.mutex);
    auto p = r.owners.find(session);
    if (p != r.owners.end()) {
        for (auto& a : p->second->admissions)
            crypto_wipe(a.capability.data(), 32);
        r.owners.erase(p);
    }
    dkr::online::forget(session);
}
inline Admission* binding(Owner& state, std::uint64_t sender) {
    for (auto& admission : state.admissions)
        if (admission.sender == sender && admission.consumed)
            return &admission;
    return nullptr;
}
inline bool authorize(Owner& owner, const secure::Key& capability, const secure::Key& key, std::uint64_t sender,
                      bool watching) {
    const auto now = Clock::now();
    for (auto& a : owner.admissions) {
        if (!contract::permits(a, capability, key, owner.privacy, sender, watching,
                               a.epoch == owner.epoch && !owner.blocked.contains(a.account),
                               a.expires > now && dkr::online::view()->connected))
            continue;
        if (a.consumed)
            return a.sender == sender;
        a.consumed = true;
        a.sender = sender;
        a.expires = now + std::chrono::minutes(3);
        return true;
    }
    return false;
}
} // namespace dkr::online::native
