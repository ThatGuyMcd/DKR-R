#pragma once
#include "netplay/secure_channel.hpp"
#include <cstdint>
namespace dkr::online::contract {
using Key = dkr::runtime::netplay::secure::Key;
struct Proof {
    Key capability{}, client{};
    std::uint64_t sender = 0, privacy = 0;
    bool watching = false, consumed = false;
};
constexpr bool running_spectator(bool watching, bool allowed, bool enabled, bool running, bool stopping,
                                 bool starting) {
    return watching && allowed && enabled && running && !stopping && !starting;
}
constexpr bool expired_admission(bool expired, bool consumed, bool approved_preparation, bool member) {
    return expired && !(consumed && (approved_preparation || member));
}
constexpr bool permits(const Proof& proof, const Key& capability, const Key& client, std::uint64_t privacy,
                       std::uint64_t sender, bool watching, bool current_scope, bool fresh_connection) {
    return current_scope && proof.capability == capability && proof.client == client && proof.privacy == privacy &&
           proof.watching == watching && (proof.consumed ? proof.sender == sender : fresh_connection);
}
// Compile-only security invariants; no target/test executable is run.
namespace checks {
constexpr Key capability{1}, client{2}, wrong{3};
constexpr Proof pending{capability, client, 0, 7, false, false};
constexpr Proof used{capability, client, 42, 7, false, true};
static_assert(permits(pending, capability, client, 7, 42, false, true, true));
static_assert(!permits(pending, wrong, client, 7, 42, false, true, true));
static_assert(!permits(pending, capability, wrong, 7, 42, false, true, true));
static_assert(!permits(pending, capability, client, 8, 42, false, true, true));
static_assert(!permits(pending, capability, client, 7, 42, true, true, true));
static_assert(!permits(pending, capability, client, 7, 42, false, false, true));
static_assert(!permits(pending, capability, client, 7, 42, false, true, false));
static_assert(permits(used, capability, client, 7, 42, false, true, false));
static_assert(!permits(used, capability, client, 7, 43, false, true, true));
static_assert(running_spectator(true, true, true, true, false, false));
static_assert(!running_spectator(false, true, true, true, false, false));
static_assert(!running_spectator(true, false, true, true, false, false));
static_assert(!running_spectator(true, true, true, false, false, false));
static_assert(!running_spectator(true, true, true, true, true, false));
static_assert(!running_spectator(true, true, true, true, false, true));
static_assert(!expired_admission(true, true, true, false));
static_assert(!expired_admission(true, true, false, true));
static_assert(expired_admission(true, true, false, false));
static_assert(expired_admission(true, false, true, false));
} // namespace checks
} // namespace dkr::online::contract
