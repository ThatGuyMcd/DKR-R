#pragma once
// What DirectSession's owner thread decides for the peer relay each pump pass,
// as pure functions over plain data: the lobby Context (members, their slots,
// pings and which of them are racers) and which of the relay's control packets
// are sent where. The session reads its own state into a RelayRoster under its
// locks, calls these, and does the address and key lookups and the sends.
//
// Two measured values feed these decisions, and both are inputs here: `now`
// (the pump's own clock reading) and each peer's last round trip sample with
// the time it was taken.
#include "netplay/peer_relay/core.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dkr::relay {

// A party peer's round trip ring (PartyLink LinkPeer::rtts / rtt_count / rtt_index / rtt_at).
struct PartyRtt {
    unsigned count = 0, index = 0;
    std::array<std::uint16_t, 16> samples{};
    Clock::time_point at{}; // the epoch means it was never stamped
};
// A classic peer's smoothed round trip (PeerRecord::rtt_ms) and the time of its newest sample.
struct ClassicRtt {
    std::size_t count = 0;
    Clock::time_point newest{};
    double ms = 0;
};

// How old a sample may be and still be shown.
inline constexpr std::chrono::seconds RttFreshness{3};

// 0 when there is no usable fresh sample.
unsigned ping_ms(const PartyRtt&, Clock::time_point now);
unsigned ping_ms(const ClassicRtt&, Clock::time_point now);

// One lobby member as the session saw it, local member first.
struct RosterRow {
    Id id = 0;
    std::string name;
    std::string address; // empty on a quick join; the session fills it from the peer's address otherwise
    bool racer = true;
    std::optional<unsigned> party_port;    // the party roster's port for this member (party lobbies)
    std::optional<unsigned> peer_slot;     // a classic host's slot for this remote peer
    std::optional<PartyRtt> party_rtt;     // a party host's round trip data for this remote peer
    std::optional<ClassicRtt> classic_rtt; // a classic host's round trip data for this remote peer
};

struct RelayRoster {
    Id room = 0, local = 0;
    Id host = 0; // the host's sender id as a client knows it; ignored when is_host
    bool is_host = false, party_enabled = false;
    bool connected = false;      // hosting, in the lobby, loading or running
    bool forming = false;        // hosting or in the lobby
    bool launch_pending = false; // a launch descriptor is already accepted
    bool allow = false, volunteer = false, quick = false;
    bool force_proxy = false; // self-test only, see Context::force_proxy
    unsigned local_slot = 255;
    Id party_generation = 0, room_generation = 0;
    std::vector<RosterRow> rows;
};

Context build_context(const RelayRoster&, Clock::time_point now);

struct RouteTable {
    bool is_host = false;
    Id host = 0;
    std::vector<Id> reachable; // a host's reachable member ids
};
// The control packets that will be sent, in order: a host's to reachable members,
// a client's to the host, nothing else.
std::vector<Control> route(std::vector<Control> controls, const RouteTable&);

} // namespace dkr::relay
