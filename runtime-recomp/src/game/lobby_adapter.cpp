#include "lobby_adapter.hpp"
#include <algorithm>

namespace dkr::runtime::lobby_ui {
namespace {
struct RemovalRegistry {std::mutex mutex;std::map<const void*,Removal> removals;};
RemovalRegistry& removal_registry(){static auto* value=new RemovalRegistry;return *value;}
}
Snapshot snapshot(const void* session) {
    const auto p=spectators::party_link(session);
    std::scoped_lock lock(p->mutex);
    Snapshot out;
    out.link={p->enabled,p->host,p->admitted,p->running,p->stopping,p->local,p->party.snapshot(),
              p->ready,p->status,p->camera,p->target,p->swap_requests};
    out.room=p->room_id;out.maximum_racers=std::clamp(p->maximum_racers,2U,4U);
    out.launching=bool(p->launch);out.host_rtt=p->rtt_ms;
    out.save_ready=p->enabled&&p->admitted&&!p->stopping&&p->seed.size()==512&&p->save_generation!=0;
    out.pending_watch=p->pending_watch;
    for(const auto& peer:p->peers)out.metrics.push_back({peer.id,peer.rtts,peer.rtt_count,peer.rtt_index});
    return out; // No credentials, transport handles or mutable guest data escape.
}
bool request_removal(const void* session,const Snapshot& seen,spectators::MemberId id,std::string& error) {
    const auto p=spectators::party_link(session);
    {std::scoped_lock lock(p->mutex);
     if(!p->enabled||!p->host||p->room_id!=seen.room||p->party.snapshot().generation!=seen.link.party.generation||
        id==p->local||!p->party.find(id)||p->party.snapshot().handover) {
        error="The lobby changed. Open this player's menu again.";return false;
     }}
    auto& registry=removal_registry();std::scoped_lock lock(registry.mutex);
    if(!registry.removals.contains(session)&&registry.removals.size()>=8)registry.removals.erase(registry.removals.begin());
    registry.removals[session]={seen.room,seen.link.party.generation,id};error.clear();return true;
}
bool take_removal(const void* session,Removal& result) {
    auto& registry=removal_registry();std::scoped_lock lock(registry.mutex);const auto it=registry.removals.find(session);
    if(it==registry.removals.end())return false;
    result=it->second;registry.removals.erase(it);return true;
}
}
