// Inside the existing DirectSession owner/party locks. UI requests carry a
// stable member and roster generation, never a potentially reassigned port.
{
    dkr::runtime::lobby_ui::Removal request;
    if(dkr::runtime::lobby_ui::take_removal(this,request)) {
        std::string reason;
        const auto* selected=party->party.find(request.member);
        auto* peer=party->peer(request.member);
        if(!is_host_||request.room!=party->room_id||request.generation!=party->party.snapshot().generation||
           request.member==party->local||!selected||!peer||party->party.snapshot().handover) {
            party->status="The lobby changed before removal. No player was removed.";
        } else {
            const std::string name=selected->name;
            const bool stops_race=party->launch&&selected->role==watch::Role::Racer;
            const auto address=peer->address;const auto key=peer->key;
            if(party->party.remove(request.member,reason)) {
                const auto leave=watch::link_encode(watch::LinkKind::Leave);
                send_with_key(address,key,static_cast<protocol::MessageType>(watch::PartyMessageType),leave);
                flush_outbound_locked();
                transport_->release_peer_route(address);
                for(auto& item:party->peers)if(item.id==request.member)crypto_wipe(item.key.data(),item.key.size());
                std::erase_if(party->peers,[&](const auto& item){return item.id==request.member;});
                party->ready.erase(request.member);party->pending_watch.erase(request.member);
                std::erase_if(party->swap_requests,[&](const auto& item){return item.second==request.member;});
                party->advertised={};party->status=name+" was removed from the lobby.";
                if(stops_race){party->stop_requested=true;party->status="The host removed a racer. Returning safely to the lobby.";}
            } else party->status=reason;
        }
    }
}
