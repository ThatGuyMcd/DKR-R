// This owner-only path is shared by authenticated leave/disconnect notices,
// current transport closure and the heartbeat watchdog. Spectators never own
// a racing port. A lost racer must stop the old control roster, not simulate on.
auto retire_party_member=[&](std::uint64_t id,const char* cause){
    namespace watch=dkr::runtime::spectators;
    auto party=watch::party_link(this);std::scoped_lock party_lock(party->mutex);
    if(!is_host_||!party->enabled||id==party->local)return false;
    const auto* member=party->party.find(id);auto* peer=party->peer(id);
    if(!member||!peer)return false;
    const auto name=member->name;const auto address=peer->address;
    const bool racer=member->role==watch::Role::Racer,live=racer&&bool(party->launch);
    std::string reason;
    if(!party->party.remove(id,reason)){
        party->stopping=party->stop_requested=true;party->running=false;
        party->status="The disconnected member could not be retired safely: "+reason;return true;
    }
    party->ready.erase(id);party->pending_watch.erase(id);
    std::erase_if(party->swap_requests,[&](const auto& item){return item.second==id;});
    if(party->requested_swap&&party->requested_swap->second==id)party->requested_swap.reset();
    if(party->approved_swap&&party->approved_swap->second==id)party->approved_swap.reset();
    for(auto& pending:party->outgoing)std::erase_if(pending,[&](const auto& item){
        if(item.target!=id)return false;party->outgoing_bytes-=item.bytes.size();return true;
    });
    std::erase_if(party->incoming,[&](const auto& item){
        if(item.source!=id)return false;party->incoming_bytes-=item.bytes.size();return true;
    });
    for(auto& item:party->peers)if(item.id==id)crypto_wipe(item.key.data(),item.key.size());
    std::erase_if(party->peers,[&](const auto& item){return item.id==id;});
    transport_->release_peer_route(address);party->advertised={};
    if(live){party->stopping=party->stop_requested=true;party->running=false;
        party->status=name+" disconnected. Returning safely to the lobby.";}
    else party->status=name+(racer?" left the lobby.":" stopped spectating.");
    status_=party->status;
    std::fprintf(stderr,"[lobby][departure] role=%s live=%u cause=%s; roster retired\n",racer?"racer":"spectator",unsigned(live),cause);
    return true;
};
