// The peer relay's pump glue as data: the Context each kind of session builds
// (slots, pings, racers, flags) with the clock and the measured round trips as
// inputs, and which control packets go where.
#include "netplay/peer_relay/owner_glue.hpp"

#include <cstdio>
#include <cstdlib>

using namespace dkr::relay;
using namespace std::chrono_literals;

namespace {
void check(bool ok, const char* reason) {
    if (!ok) {
        std::fprintf(stderr, "relay glue: %s\n", reason);
        std::abort();
    }
}
const Clock::time_point kNow = Clock::time_point{} + 100s;

PartyRtt party_rtt(unsigned count, unsigned index, std::uint16_t newest, Clock::time_point at) {
    PartyRtt rtt;
    rtt.count = count;
    rtt.index = index;
    rtt.samples[(index - 1) % rtt.samples.size()] = newest;
    rtt.at = at;
    return rtt;
}
const Member* find(const Context& context, Id id) {
    for (const auto& member : context.members)
        if (member.id == id)
            return &member;
    return nullptr;
}
RelayRoster base(bool host) {
    RelayRoster r;
    r.room = 90;
    r.local = host ? 1 : 2;
    r.host = host ? 0 : 1;
    r.is_host = host;
    r.connected = true;
    r.forming = true;
    r.allow = true;
    r.volunteer = true;
    r.local_slot = host ? 0 : 1;
    r.room_generation = 7;
    r.party_generation = 70;
    return r;
}
} // namespace

int main() {
    // Round trip samples: only a fresh sample counts, and a classic one is clamped.
    {
        check(ping_ms(party_rtt(3, 3, 42, kNow - 1s), kNow) == 42, "fresh party sample");
        check(ping_ms(party_rtt(3, 3, 42, kNow - 3s), kNow) == 0, "a sample exactly 3 s old is stale");
        check(ping_ms(party_rtt(3, 3, 42, kNow - 2999ms), kNow) == 42, "just under 3 s is fresh");
        check(ping_ms(party_rtt(0, 3, 42, kNow - 1s), kNow) == 0, "no samples");
        check(ping_ms(party_rtt(3, 3, 42, Clock::time_point{}), kNow) == 0, "never stamped");
        check(ping_ms(party_rtt(5, 0, 9, kNow), kNow) == party_rtt(5, 0, 9, kNow).samples[15],
              "index 0 wraps to the last slot");
        check(ping_ms(ClassicRtt{4, kNow - 1s, 31.4}, kNow) == 31, "classic sample is truncated");
        check(ping_ms(ClassicRtt{4, kNow - 1s, 0.2}, kNow) == 1, "classic floor is 1 ms");
        check(ping_ms(ClassicRtt{4, kNow - 1s, 9000.0}, kNow) == 4999, "classic ceiling is 4999 ms");
        check(ping_ms(ClassicRtt{0, kNow, 20.0}, kNow) == 0, "classic with no samples");
        check(ping_ms(ClassicRtt{4, kNow - 3s, 20.0}, kNow) == 0, "classic stale");
    }
    // A classic host: slots and pings for remote peers, its own slot for itself.
    {
        auto r = base(true);
        r.rows.push_back({1, "Host", {}, true});
        RosterRow blue{2, "Blue", "10.0.0.2:5000", true};
        blue.peer_slot = 1;
        blue.classic_rtt = ClassicRtt{8, kNow - 1s, 24.0};
        RosterRow green{3, "Green", "10.0.0.3:5000", true};
        green.peer_slot = 2;
        green.classic_rtt = ClassicRtt{8, kNow - 10s, 50.0};
        r.rows.push_back(blue);
        r.rows.push_back(green);
        const auto c = build_context(r, kNow);
        check(c.room == 90 && c.local == 1 && c.host == 1 && c.active && c.allow && c.volunteer && !c.quick,
              "host flags");
        check(c.building && c.generation == 7, "forming lobby, room generation");
        check(c.members.size() == 3 && c.members[0].id == 1 && c.members[1].id == 2 && c.members[2].id == 3,
              "member order");
        check(find(c, 1)->slot == 0 && find(c, 1)->ping_ms == 0, "the host's own slot and no ping for itself");
        check(find(c, 2)->slot == 1 && find(c, 2)->ping_ms == 24 && find(c, 2)->address == "10.0.0.2:5000",
              "fresh peer");
        check(find(c, 3)->slot == 2 && find(c, 3)->ping_ms == 0, "stale peer has no ping");
        // Once a launch is accepted the lobby stops building; not connected means inactive.
        r.launch_pending = true;
        check(!build_context(r, kNow).building, "launch accepted");
        r.connected = false;
        check(!build_context(r, kNow).active, "not connected");
        r = base(true);
        r.quick = true;
        check(build_context(r, kNow).quick, "quick join flag");
        r = base(true);
        check(!build_context(r, kNow).force_proxy, "the test-only force flag is off by default");
        r.force_proxy = true;
        check(build_context(r, kNow).force_proxy, "and is carried to the engine when a self-test sets it");
    }
    // A party host: ports come from the party roster, pings from the party ring, generation from the party.
    {
        auto r = base(true);
        r.party_enabled = true;
        RosterRow me{1, "Host", {}, true};
        me.party_port = 0;
        RosterRow racer{2, "Racer", "", true};
        racer.party_port = 1;
        racer.party_rtt = party_rtt(4, 4, 18, kNow - 500ms);
        RosterRow spectator{5, "Watcher", "", false};
        spectator.party_rtt = party_rtt(4, 4, 33, kNow - 4s);
        r.rows = {me, racer, spectator};
        const auto c = build_context(r, kNow);
        check(c.generation == 70, "party generation");
        check(find(c, 1)->slot == 0 && find(c, 2)->slot == 1 && find(c, 5)->slot == 255, "ports; a spectator has none");
        check(find(c, 2)->ping_ms == 18 && find(c, 5)->ping_ms == 0 && find(c, 1)->ping_ms == 0, "party pings");
        check(find(c, 2)->racer && !find(c, 5)->racer, "spectators are not racers");
        // In a party the classic slot rules do not apply, even to the local member.
        r.rows[0].party_port.reset();
        check(build_context(r, kNow).members[0].slot == 255, "no party port, no slot");
    }
    // A client sees only itself and the host, and never shows pings.
    {
        auto r = base(false);
        r.rows.push_back({2, "You", {}, true});
        r.rows.push_back({1, "Host", {}, true});
        auto c = build_context(r, kNow);
        check(c.host == 1 && c.local == 2 && c.active && c.members.size() == 2, "client flags");
        check(find(c, 2)->slot == 1 && find(c, 1)->slot == 255 && find(c, 1)->ping_ms == 0, "client slots");
        r.host = 0;
        check(!build_context(r, kNow).active, "host not learned yet");
        // A watching client is not a racer.
        r = base(false);
        r.rows.push_back({2, "You", {}, false});
        r.rows.push_back({1, "Host", {}, true});
        check(!build_context(r, kNow).members[0].racer, "watching client");
    }
    // Routing: a host reaches only reachable members, in order; a client only the host.
    {
        RouteTable table;
        table.is_host = true;
        table.reachable = {2, 3};
        const auto sends = route({{2, {1}}, {4, {2}}, {3, {3}}, {2, {4}}}, table);
        check(sends.size() == 3 && sends[0].target == 2 && sends[1].target == 3 && sends[2].target == 2,
              "host order and recipients");
        check(sends[0].bytes == Bytes({1}) && sends[1].bytes == Bytes({3}) && sends[2].bytes == Bytes({4}),
              "host payloads");
        table.reachable = {};
        check(route({{2, {1}}}, table).empty(), "removed members get nothing");
        table = {};
        table.host = 1;
        const auto client = route({{1, {1}}, {2, {2}}, {1, {3}}}, table);
        check(client.size() == 2 && client[0].bytes == Bytes({1}) && client[1].bytes == Bytes({3}),
              "client sends to the host only");
    }
    std::puts("peer relay glue passed");
    return 0;
}
