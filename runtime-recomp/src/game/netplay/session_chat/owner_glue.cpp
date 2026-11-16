#include "netplay/session_chat/owner_glue.hpp"

#include <algorithm>
#include <utility>

namespace dkr::chat {

Context build_context(const RosterSnapshot& roster) {
    Context context;
    context.room = roster.room;
    context.local = roster.local;
    context.host = roster.is_host ? roster.local : roster.host;
    context.active = roster.room && roster.local && context.host && roster.connected;
    context.allow = roster.allowed;
    context.members.push_back({roster.local, roster.local_name});
    switch (roster.source) {
    case RosterSnapshot::Source::Party:
        for (const auto& member : roster.party)
            if (member.id != roster.local)
                context.members.push_back(member);
        break;
    case RosterSnapshot::Source::HostLobby:
        for (const auto& peer : roster.peers)
            if (peer.active)
                context.members.push_back(
                    {peer.id, peer.slot < roster.slots.size() ? roster.slots[peer.slot].name : std::string{"Racer"}});
        break;
    case RosterSnapshot::Source::ClientLobby:
        // Classic lobby payloads carry slot names but no peer identities. Reuse the
        // host-authenticated live roster already used by route cards.
        for (const auto& member : roster.relay)
            if (member.id != roster.local)
                context.members.push_back(member);
        for (const auto& slot : roster.slots)
            if (slot.occupied) {
                const auto id = peer_id(slot.peer_id);
                if (id && id != roster.local &&
                    std::none_of(context.members.begin(), context.members.end(),
                                 [&](const auto& member) { return member.id == id; }))
                    context.members.push_back({id, slot.name});
            }
        break;
    }
    return context;
}

std::vector<Send> route(std::vector<Packet> packets, const RouteTable& table) {
    std::vector<Send> sends;
    for (auto& packet : packets) {
        const bool deliverable = table.is_host ? std::find(table.reachable.begin(), table.reachable.end(),
                                                           packet.target) != table.reachable.end()
                                               : packet.target == table.host;
        if (deliverable)
            sends.push_back({packet.target, std::move(packet.bytes)});
    }
    return sends;
}

} // namespace dkr::chat
