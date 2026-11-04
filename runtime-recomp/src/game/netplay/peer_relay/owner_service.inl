// DirectSession's owner mutex and the existing PartyLink lock are held here.
{
    namespace proxy=dkr::relay;
    proxy::Context context;
    context.room=match_id_;context.local=sender_id_;context.host=is_host_?sender_id_:host_sender_id_;
    context.active=match_id_&&sender_id_&&context.host&&
        (state_==ConnectionState::Hosting||state_==ConnectionState::Lobby||state_==ConnectionState::Loading||state_==ConnectionState::Running);
    context.allow=proxy::host_enabled.load();context.volunteer=proxy::volunteer_enabled.load();context.quick=transport_->quick_join();
    context.building=(state_==ConnectionState::Hosting||state_==ConnectionState::Lobby)&&!launch_descriptor_;
    std::vector<proxy::Address> addresses;
    if(is_host_) {
        context.members.push_back({sender_id_,local_name_,{},!party->local_watching()});
        if(party->enabled) {
            for(const auto& peer:party->peers)if(const auto* m=party->party.find(peer.id)) {
                context.members.push_back({peer.id,m->name,context.quick?std::string{}:DatagramSocket::describe(peer.address),m->role==watch::Role::Racer});
                addresses.push_back({peer.id,peer.address});
            }
        } else for(const auto& peer:peers_)if(peer.active){
            context.members.push_back({peer.sender_id,peer.slot<room_view_.players.size()?room_view_.players[peer.slot].display_name:std::string{"Racer"},context.quick?std::string{}:DatagramSocket::describe(peer.address),true});
            addresses.push_back({peer.sender_id,peer.address});
        }
    } else {context.members.push_back({sender_id_,"You",{},!party->local_watching()});context.members.push_back({host_sender_id_,"Host",{},true});addresses.push_back({host_sender_id_,host_address_});}
    context.generation=party->enabled?party->party.snapshot().generation:room_view_.generation;
    for(auto& m:context.members) {
        if(party->enabled){if(const auto* entry=party->party.find(m.id);entry&&entry->port)m.slot=*entry->port;}
        else if(m.id==sender_id_)m.slot=local_slot_;
        else if(is_host_){if(const auto* peer=peer_by_sender(m.id))m.slot=peer->slot;}
        if(is_host_&&m.id!=sender_id_) {
            if(party->enabled){if(const auto* peer=party->peer(m.id);peer&&peer->rtt_count&&
                peer->rtt_at.time_since_epoch().count()&&now-peer->rtt_at<std::chrono::seconds(3))
                m.ping_ms=peer->rtts[(peer->rtt_index-1)%peer->rtts.size()];}
            else if(const auto* peer=peer_by_sender(m.id);peer&&peer->rtt_sample_count&&
                now-peer->rtt_sample_times[(peer->rtt_sample_cursor+peer->rtt_samples.size()-1)%peer->rtt_samples.size()]<std::chrono::seconds(3))
                m.ping_ms=unsigned(std::clamp(peer->rtt_ms,1.,4999.));
        }
    }
    proxy::configure(this,std::move(context),std::move(addresses));
    for(auto& command:proxy::controls(this)) {
        auto payload=watch::link_encode(watch::LinkKind::Extension,command.bytes);
        if(is_host_){
            if(party->enabled){if(const auto* peer=party->peer(command.target))send_with_key(peer->address,peer->key,protocol::MessageType(watch::PartyMessageType),payload);}
            else if(const auto* peer=peer_by_sender(command.target))send_with_key(peer->address,peer->key,protocol::MessageType(watch::PartyMessageType),payload);
        } else if(command.target==host_sender_id_)send_with_key(host_address_,key_,protocol::MessageType(watch::PartyMessageType),payload);
    }
}
