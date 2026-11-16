#include "netplay/peer_relay/owner_glue.hpp"

#include <algorithm>
#include <utility>

namespace dkr::relay {

unsigned ping_ms(const PartyRtt& rtt, Clock::time_point now) {
    if (rtt.count && rtt.at.time_since_epoch().count() && now - rtt.at < RttFreshness)
        return rtt.samples[(rtt.index - 1) % rtt.samples.size()];
    return 0;
}

unsigned ping_ms(const ClassicRtt& rtt, Clock::time_point now) {
    if (rtt.count && now - rtt.newest < RttFreshness)
        return unsigned(std::clamp(rtt.ms, 1., 4999.));
    return 0;
}

Context build_context(const RelayRoster& roster, Clock::time_point now) {
    Context context;
    context.room = roster.room;
    context.local = roster.local;
    context.host = roster.is_host ? roster.local : roster.host;
    context.active = roster.room && roster.local && context.host && roster.connected;
    context.allow = roster.allow;
    context.volunteer = roster.volunteer;
    context.quick = roster.quick;
    context.force_proxy = roster.force_proxy;
    context.building = roster.forming && !roster.launch_pending;
    context.generation = roster.party_enabled ? roster.party_generation : roster.room_generation;
    for (const auto& row : roster.rows) {
        Member member{row.id, row.name, row.address, row.racer};
        if (roster.party_enabled) {
            if (row.party_port)
                member.slot = *row.party_port;
        } else if (row.id == roster.local)
            member.slot = roster.local_slot;
        else if (roster.is_host && row.peer_slot)
            member.slot = *row.peer_slot;
        if (roster.is_host && row.id != roster.local) {
            if (roster.party_enabled) {
                if (row.party_rtt)
                    member.ping_ms = ping_ms(*row.party_rtt, now);
            } else if (row.classic_rtt)
                member.ping_ms = ping_ms(*row.classic_rtt, now);
        }
        context.members.push_back(std::move(member));
    }
    return context;
}

std::vector<Control> route(std::vector<Control> controls, const RouteTable& table) {
    std::vector<Control> sends;
    for (auto& control : controls) {
        const bool deliverable = table.is_host ? std::find(table.reachable.begin(), table.reachable.end(),
                                                           control.target) != table.reachable.end()
                                               : control.target == table.host;
        if (deliverable)
            sends.push_back(std::move(control));
    }
    return sends;
}

} // namespace dkr::relay
