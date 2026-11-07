// Inside DirectSession::pump_locked, after AEAD and replay-window validation.
if (unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType) {
    // Party envelopes carry their transport class in Header::frame.
    if(dkr::online_stability::qualification_delivery::drop(packet.header.frame))continue;
    namespace watch=dkr::runtime::spectators;
    auto party=watch::party_link(this);std::scoped_lock party_lock(party->mutex);
    watch::LinkMessage message;
    if(!watch::link_decode(packet.payload,message))continue;
    if(message.kind==watch::LinkKind::Extension) {
        // Authentication/replay checks already ran. A provisional download
        // route is not an admitted lobby member and cannot publish metadata.
        const bool admitted=is_host_?(party->enabled?party->peer(packet_sender)!=nullptr:peer_by_sender(packet_sender)!=nullptr):
            (packet_sender==host_sender_id_&&source==host_address_);
        if(admitted&&message.body.size()>=6&&message.body[5]==4)dkr::chat::channel(this)->receive(packet_sender,message.body);
        else if(admitted&&message.body.size()>=6&&(message.body[5]==3||message.body[5]==6))dkr::relay::control(this,packet_sender,message.body);
        else dkr::online_stability::control(this)->receive(packet_sender,admitted,message.body);
        continue;
    }
    if(is_host_) {
        auto* member=party->peer(packet_sender);
        if(!party->enabled||!member||member->address!=source)continue;
        const auto now=std::chrono::steady_clock::now();
        const auto* identity=party->party.find(packet_sender);
        const bool observer=identity&&identity->role==watch::Role::Spectator;
        const std::size_t token_cap=observer?16*1024:128*1024,token_rate=observer?16:128;
        member->received_tokens=std::min(member->received_tokens,token_cap);
        if(member->rate_at.time_since_epoch().count()) {
            const auto credit=std::chrono::duration_cast<std::chrono::milliseconds>(now-member->rate_at).count();
            if(credit>0){member->received_tokens=std::min(token_cap,member->received_tokens+std::size_t(credit)*token_rate);member->rate_at=now;}
        } else member->rate_at=now;
        if(packet.payload.size()>member->received_tokens)continue;
        member->received_tokens-=packet.payload.size();member->heard=now;
        switch(message.kind) {
        case watch::LinkKind::Runtime:
            if(observer) {
                // Watchers can acknowledge playback or request a seat, never
                // supply racing input, membership, scene votes or host state.
                if(message.body.size()<21||message.body.size()>128)break;
                const auto kind=message.body[4];
                if(kind!=1&&kind!=4&&kind!=13&&kind!=15&&kind!=16&&kind!=17)break;
            }
            party->admit_incoming(packet_sender,message.body);break;
        case watch::LinkKind::Heartbeat: {
            std::uint64_t token=0;bool reply=false;
            if(!watch::link_heartbeat(message.body,token,reply))break;
            if(!reply)party->enqueue({packet_sender,watch::LinkKind::Heartbeat,TransportTrafficClass::Realtime,watch::link_heartbeat(token,true)});
            else if(const auto ms=member->ping_window.receive(token,now)) {
                member->rtts[member->rtt_index++%member->rtts.size()]=std::uint16_t(*ms);member->rtt_count=std::min<unsigned>(member->rtt_count+1,unsigned(member->rtts.size()));member->rtt_at=now;
            }break;
        }
        case watch::LinkKind::Ready:
            if(!party->launch&&!party->stopping&&message.body.size()==1&&message.body[0]<=1&&party->participant(packet_sender))
                party->ready[packet_sender]=message.body[0]!=0;
            break;
        case watch::LinkKind::Stop:
            if(party->participant(packet_sender)){party->stop_requested=true;party->status="A racer returned to the lobby.";}
            break;
        case watch::LinkKind::Leave: {
            if(party->participant(packet_sender)&&party->launch){party->stop_requested=true;party->status="A racer left the lobby.";}
            std::string ignored;party->party.remove(packet_sender,ignored);party->ready.erase(packet_sender);
            transport_->release_peer_route(member->address);crypto_wipe(member->key.data(),member->key.size());
            std::erase_if(party->peers,[&](const auto& p){return p.id==packet_sender;});break;
        }
        default:break; // No client can publish membership, a launch or a checkpoint as host.
        }
    } else {
        if(packet_sender!=host_sender_id_||source!=host_address_)continue;
        last_admission_response_=std::chrono::steady_clock::now();
        if(message.kind==watch::LinkKind::Accepted) {
            protocol::HelloAckPayload accepted;std::string reason;
            if(!party->requested||!protocol::decode_hello_ack(message.body,accepted,reason)||!accepted.accepted||
               accepted.synchronized_save.size()!=512||
               stable_hash(std::string_view(reinterpret_cast<const char*>(accepted.synchronized_save.data()),512))!=accepted.online_save_hash)continue;
            party->enabled=party->admitted=true;party->host=false;party->local=sender_id_;party->authority=host_sender_id_;
            party->room_id=match_id_;party->seed=accepted.synchronized_save;party->save_generation=accepted.online_save_generation;party->save_hash=accepted.online_save_hash;
            party->status="Joined the lobby";state_=ConnectionState::Lobby;local_slot_=255;
            local_online_save_ready_=local_online_save_acknowledged_=true;
            session_save_generation_=accepted.online_save_generation;manifest_.session_save_hash=accepted.online_save_hash;
            room_view_.manifest.session_save_hash=accepted.online_save_hash;
            transport_->retain_peer_route(host_address_);
        } else if(party->enabled&&party->admitted) {
            if(message.kind==watch::LinkKind::Roster) {
                watch::PartySnapshot roster;std::string reason;
                if(watch::wire::roster(message.body,roster,reason)&&roster.host==host_sender_id_&&
                   std::any_of(roster.members.begin(),roster.members.end(),[&](const auto& m){return m.id==sender_id_;})&&
                   party->party.replace_verified(std::move(roster),reason)) {
                    const auto* local=party->party.find(sender_id_);local_slot_=local&&local->port?*local->port:255;
                }
            } else if(message.kind==watch::LinkKind::Room) {
                protocol::LobbyStatePayload room;std::string reason;
                if(protocol::decode_lobby_state(message.body,room,reason)) {
                    apply_lobby_payload(room);
                    for(const auto& m:party->party.snapshot().members)if(m.port)party->ready[m.id]=room.players[*m.port].ready;
                }
            } else if(message.kind==watch::LinkKind::Launch) {
                protocol::StartPayload start;std::string reason;
                if(protocol::decode_start(message.body,start,reason)&&start.descriptor.match_id==match_id_&&
                   start.descriptor.synchronization==SynchronizationMode::ExperimentalRollback&&
                   start.descriptor.compatibility_hash==manifest_hash(room_view_.manifest)&&
                   (!party->launch||launch_descriptor_hash(*party->launch)!=start.descriptor_hash)) {
                    party->launch=start.descriptor;party->running=party->stopping=party->returned=false;
                    launch_descriptor_=start.descriptor;launch_requested_=true;state_=ConnectionState::Loading;
                    status_="Preparing the online game";
                }
            } else if(message.kind==watch::LinkKind::Runtime)party->admit_incoming(host_sender_id_,message.body);
            else if(message.kind==watch::LinkKind::Heartbeat) {
                std::uint64_t token=0;bool reply=false;
                if(watch::link_heartbeat(message.body,token,reply)) {
                    if(!reply)party->enqueue({host_sender_id_,watch::LinkKind::Heartbeat,TransportTrafficClass::Realtime,watch::link_heartbeat(token,true)});
                    else if(const auto ms=party->ping_window.receive(token,std::chrono::steady_clock::now()))party->rtt_ms=std::uint16_t(*ms);
                }
            }
            else if(message.kind==watch::LinkKind::Stop) {
                party->stopping=true;party->running=false;party->status.assign(message.body.begin(),message.body.end());
                launch_descriptor_.reset();launch_requested_=false;state_=ConnectionState::Lobby;status_=party->status;
            } else if(message.kind==watch::LinkKind::Leave) {
                party->stopping=true;party->running=false;party->status="The host closed the lobby.";fail_locked(party->status);
            }
        }
    }
    continue;
}
