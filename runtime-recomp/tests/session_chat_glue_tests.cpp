// The session chat glue as data: which Context each kind of session builds, and
// who each packet is routed to. DirectSession only fills the inputs and sends.
#include "netplay/session_chat/owner_glue.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace dkr::chat;

namespace {
void check(bool ok, const char* reason) {
    if (!ok) {
        std::fprintf(stderr, "chat glue: %s\n", reason);
        std::abort();
    }
}
std::string names(const Context& context) {
    std::string text;
    for (const auto& member : context.members)
        text += std::to_string(member.id) + ":" + member.name + " ";
    return text;
}
RosterSnapshot host_lobby() {
    RosterSnapshot r;
    r.source = RosterSnapshot::Source::HostLobby;
    r.room = 90;
    r.local = 1;
    r.is_host = true;
    r.connected = true;
    r.local_name = "Host";
    r.slots = {{true, "1", "Host"}, {true, "2", "Blue"}, {true, "3", "Green"}};
    r.peers = {{2, true, 1}, {3, false, 2}, {4, true, 9}};
    return r;
}
RosterSnapshot client_lobby() {
    RosterSnapshot r;
    r.source = RosterSnapshot::Source::ClientLobby;
    r.room = 90;
    r.local = 2;
    r.host = 1;
    r.connected = true;
    r.local_name = "Blue";
    r.slots = {{true, "1", "Host"},
               {true, "2", "Blue"},
               {false, "7", "Gone"},
               {true, "not-a-number", "Odd"},
               {true, "3", "Green"}};
    r.relay = {{1, "RelayHost"}, {2, "RelayBlue"}, {5, "Spectator"}};
    return r;
}
Packet packet(Id target, std::uint8_t tag) {
    return {target, Bytes{tag, 1, 2}};
}
} // namespace

int main() {
    // A host in a classic lobby: itself, then active peers named from their slot.
    {
        const auto c = build_context(host_lobby());
        check(c.room == 90 && c.local == 1 && c.host == 1 && c.active && c.allow, "host flags");
        check(names(c) == "1:Host 2:Blue 4:Racer ",
              "host members: inactive peers are skipped, an unknown slot is 'Racer'");
    }
    // The chat switch is the host's: off means the context says so, nothing else changes.
    {
        auto r = host_lobby();
        r.allowed = false;
        const auto c = build_context(r);
        check(!c.allow && c.active && c.members.size() == 3, "chat switched off");
    }
    // Not connected, or no room or no host identity yet: inactive, but still configured.
    {
        auto r = host_lobby();
        r.connected = false;
        check(!build_context(r).active, "not connected");
        r = client_lobby();
        r.host = 0;
        const auto c = build_context(r);
        check(!c.active && c.host == 0, "joined, host not learned yet");
        r = host_lobby();
        r.room = 0;
        check(!build_context(r).active, "no room");
    }
    // A party lobby (racers and spectators alike): the party roster, this device once.
    {
        RosterSnapshot r;
        r.source = RosterSnapshot::Source::Party;
        r.room = 77;
        r.local = 3;
        r.host = 1;
        r.connected = true;
        r.local_name = "Me";
        r.party = {{1, "Host"}, {2, "Racer"}, {3, "Me-in-roster"}, {6, "Spectator"}};
        const auto c = build_context(r);
        check(c.host == 1 && c.active, "party flags");
        check(names(c) == "3:Me 1:Host 2:Racer 6:Spectator ",
              "party members: local first from the session, never twice");
        r.is_host = true;
        r.local = 1;
        check(build_context(r).host == 1, "a party host is its own host");
    }
    // A joined classic lobby: the relay's authenticated roster, then slot players it does not list.
    {
        const auto c = build_context(client_lobby());
        check(c.host == 1 && c.local == 2 && c.active, "client flags");
        check(
            names(c) == "2:Blue 1:RelayHost 5:Spectator 3:Green ",
            "client members: relay roster first, local excluded, unoccupied/unparseable slots skipped, no duplicates");
    }

    // Routing. A host sends only to members it can reach right now.
    {
        RouteTable table;
        table.is_host = true;
        table.host = 0;
        table.reachable = {2, 4};
        const auto sends = route({packet(2, 0xA), packet(3, 0xB), packet(4, 0xC), packet(2, 0xD)}, table);
        check(sends.size() == 3, "host: the unreachable member gets nothing");
        check(sends[0].target == 2 && sends[0].bytes[0] == 0xA && sends[1].target == 4 && sends[1].bytes[0] == 0xC &&
                  sends[2].target == 2 && sends[2].bytes[0] == 0xD,
              "host: order and bytes are kept");
        check(sends[0].bytes == Bytes({0xA, 1, 2}), "host: payload untouched");
        // Nobody is sent their own packet: the host is not in its own reachable list.
        table.reachable = {2};
        check(route({packet(1, 0xE)}, table).empty(), "host: a packet addressed to the host itself goes nowhere");
        // A member that left is no longer reachable.
        table.reachable = {};
        check(route({packet(2, 0xF)}, table).empty(), "host: removed members get nothing");
    }
    // A client talks to the host only.
    {
        RouteTable table;
        table.host = 1;
        const auto sends = route({packet(1, 0x1), packet(5, 0x2), packet(1, 0x3), packet(2, 0x4)}, table);
        check(sends.size() == 2 && sends[0].bytes[0] == 0x1 && sends[1].bytes[0] == 0x3,
              "client: only the host is addressed");
        check(route({packet(5, 0x9)}, table).empty(), "client: other members are not reachable directly");
    }
    check(route({}, RouteTable{}).empty(), "no packets, no sends");
    std::puts("session chat glue passed");
    return 0;
}
