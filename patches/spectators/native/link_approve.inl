// Inside approve_join_locked, after provisional mod/content proof succeeds.
if(auto party=dkr::runtime::spectators::party_link(this);party->enabled) {
    namespace watch=dkr::runtime::spectators;
    std::scoped_lock party_lock(party->mutex);
    const auto kind=party->pending_watch.find(pending->sender_id);
    if(kind==party->pending_watch.end()){error="Use this build's Racer or Watch Race join option.";return false;}
    if(party->launch&&!kind->second){error="The race is running. Join as a spectator, then request a racing slot.";return false;}
    if(!save_sync_available()||session_save_.size()!=512){error="The host's online save is not ready.";return false;}
    std::optional<std::uint8_t> port;
    if(!kind->second){for(unsigned i=0;i<std::min<unsigned>(4,room_view_.rules.maximum_players);++i)if(!party->party.snapshot().controls[i].member){port=std::uint8_t(i);break;}
        if(!port){error="All four racing places are occupied. Join as a spectator instead.";return false;}}
    if(!party->party.add(pending->sender_id,pending->display_name,port,error))return false;
    party->peers.push_back({pending->sender_id,pending->address,pending->key,pending->next_sequence,std::chrono::steady_clock::now()});
    party->ready[pending->sender_id]=false;
    if(!pending->mod_route_retained)transport_->retain_peer_route(pending->address);
    const auto accepted=protocol::encode_hello_ack({true,0,"Joined the lobby",session_save_,{},session_save_generation_,manifest_.session_save_hash});
    const auto response=watch::link_encode(watch::LinkKind::Accepted,accepted);
    send_with_key(pending->address,pending->key,static_cast<protocol::MessageType>(watch::PartyMessageType),response);
    party->pending_watch.erase(pending->sender_id);party->advertised={};
    status_=pending->display_name+(port?" joined as a racer.":" joined to watch.");
    *pending={};error.clear();return true;
}
