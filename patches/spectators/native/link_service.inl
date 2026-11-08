// Inside pump_locked after ordinary receive/flush, before four-port policies.
{
    namespace watch=dkr::runtime::spectators;
    auto party=watch::party_link(this);std::scoped_lock party_lock(party->mutex);
    if(party->enabled) {
        const auto now=std::chrono::steady_clock::now();
        auto queue=[&](watch::MemberId target,watch::LinkKind kind,std::vector<std::uint8_t> body){
            return party->enqueue({target,kind,TransportTrafficClass::Control,std::move(body)});
        };
        auto stop=[&](std::string reason){
            party->running=false;party->stopping=true;party->ready.clear();party->status=std::move(reason);
            launch_descriptor_.reset();launch_requested_=false;state_=is_host_?ConnectionState::Hosting:ConnectionState::Lobby;
            status_=party->status;
        };
        if(is_host_) {
            for(auto& peer:party->peers)if(!peer.ping_sent.time_since_epoch().count()||now-peer.ping_sent>=std::chrono::seconds(1)) {
                peer.ping_token=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());peer.ping_sent=now;
                queue(peer.id,watch::LinkKind::Heartbeat,watch::link_heartbeat(peer.ping_token,false));
            }
            if(!party->launch&&room_view_.rules.automatic_input_delay) {
                std::uint16_t slowest=0;
                for(const auto& peer:party->peers)if(const auto* m=party->party.find(peer.id);m&&m->role==watch::Role::Racer&&peer.rtt_count) {
                    auto sorted=peer.rtts;std::sort(sorted.begin(),sorted.begin()+peer.rtt_count);slowest=std::max(slowest,sorted[peer.rtt_count-1]);
                }
                input_delay_=experimental::automatic_delay(slowest?double(slowest):100.,room_view_.rules.rollback_window);
            }
            for(auto it=party->peers.begin();it!=party->peers.end();) {
                if(now-it->heard<std::chrono::seconds(30)){++it;continue;}
                const auto id=it->id;
                if(party->participant(id)&&party->launch)party->stop_requested=true;
                std::string ignored;party->party.remove(id,ignored);party->ready.erase(id);
                transport_->release_peer_route(it->address);crypto_wipe(it->key.data(),it->key.size());it=party->peers.erase(it);
            }
            if(party->returned) {
                if(party->seed.size()==512) {
                    session_save_=party->seed;manifest_.session_save_hash=stable_hash(std::string_view(reinterpret_cast<const char*>(session_save_.data()),512));
                    ++session_save_generation_;if(!session_save_generation_)++session_save_generation_;
                    party->save_generation=session_save_generation_;party->save_hash=manifest_.session_save_hash;room_view_.manifest=manifest_;
                    const auto refreshed=protocol::encode_hello_ack({true,0,"Back in the lobby",session_save_,{},session_save_generation_,manifest_.session_save_hash});
                    queue(0,watch::LinkKind::Accepted,refreshed);
                }
                party->returned=false;party->launch.reset();party->stopping=false;party->ready.clear();party->advertised={};
            }
            if(party->ready_requested&&!party->launch&&party->participant(sender_id_))party->ready[sender_id_]=party->ready_value;
            party->ready_requested=false;
            if(party->start_requested) {
                party->start_requested=false;std::string reason;
                const auto& members=party->party.snapshot().members;
                const bool all=std::all_of(members.begin(),members.end(),[&](const auto& m){return m.role==watch::Role::Spectator||party->ready[m.id];});
                if(!party->launch&&!party->stopping&&all&&watch::prepare_racing_ports(party->party,reason)&&session_save_.size()==512) {
                    unsigned count=0;for(const auto& m:party->party.snapshot().members)if(m.role==watch::Role::Racer)++count;
                    LaunchDescriptor launch;launch.match_id=match_id_;launch.lobby_generation=++party->launch_serial;
                    launch.compatibility_hash=manifest_hash(manifest_);launch.occupied_mask=std::uint8_t((1U<<count)-1);launch.player_count=std::uint8_t(count);
                    launch.input_delay_frames=input_delay_;launch.rollback_window=room_view_.rules.rollback_window;
                    launch.synchronization=SynchronizationMode::ExperimentalRollback;launch.host_control=room_view_.rules.host_control;launch.development=room_view_.rules.development;
                    if(valid_launch_descriptor(launch)) {
                        party->launch=launch;party->seed=session_save_;party->running=party->stopping=party->returned=false;party->advertised={};
                        launch_descriptor_=launch;launch_requested_=true;state_=ConnectionState::Loading;
                        const auto* me=party->party.find(sender_id_);local_slot_=me&&me->port?*me->port:255;
                    } else party->status="The lobby settings cannot start this race.";
                } else party->status=reason.empty()?"Every racer must be ready before starting.":reason;
            }
            if(party->stop_requested) {
                party->stop_requested=false;auto reason=party->status.empty()?std::string("Returned to the lobby."):party->status;
                if(reason.size()>240)reason.resize(240);queue(0,watch::LinkKind::Stop,{reason.begin(),reason.end()});stop(std::move(reason));
            }
            if(now-party->advertised>=std::chrono::milliseconds(500)) {
                party->advertised=now;
                auto room=lobby_payload();room.generation=party->party.snapshot().generation;room.players={};room.countdown_active=false;
                room.phase=party->launch&&!party->stopping?RoomPhase::Running:RoomPhase::Waiting;
                room.rules.synchronization=SynchronizationMode::ExperimentalRollback;
                for(const auto& m:party->party.snapshot().members)if(m.port) {
                    auto& row=room.players[*m.port];row.occupied=true;row.slot=*m.port;row.display_name=m.name;row.ready=party->ready[m.id];row.loaded=party->running;row.route=Route::Direct;
                }
                queue(0,watch::LinkKind::Roster,watch::wire::roster(party->party.snapshot()));
                queue(0,watch::LinkKind::Room,protocol::encode_lobby_state(room));
                if(party->launch&&!party->stopping)queue(0,watch::LinkKind::Launch,protocol::encode_start({2,*party->launch,launch_descriptor_hash(*party->launch)}));
            }
        } else {
            if(party->admitted&&now-last_admission_response_>std::chrono::seconds(30)) {
                party->stopping=true;party->running=false;fail_locked("The host stopped responding. The lobby has closed.");return;
            }
            if(party->ready_requested){queue(party->authority,watch::LinkKind::Ready,{std::uint8_t(party->ready_value)});party->ready_requested=false;}
            if(party->stop_requested){
                party->stop_requested=false;
                const bool racer=party->participant(party->local);
                queue(party->authority,racer?watch::LinkKind::Stop:watch::LinkKind::Leave,{});
                if(!racer)stop("Stopped watching.");
            }
            if(party->returned){queue(party->authority,watch::LinkKind::Returned,{});party->returned=false;party->launch.reset();party->stopping=false;}
            if(now-party->heartbeat>=std::chrono::seconds(1)){
                party->ping_token=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());party->ping_sent=party->heartbeat=now;
                queue(party->authority,watch::LinkKind::Heartbeat,watch::link_heartbeat(party->ping_token,false));
            }
        }
        if(party->running&&launch_descriptor_)state_=ConnectionState::Running;
        // Independent per-peer SCTP lanes and bounded round-robin bursts.
        // Bulk never fills a racing input/control queue in DirectSession.
        const auto elapsed=party->budget_at.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(now-party->budget_at).count():0;
        if(elapsed>0)party->bulk_tokens=std::min<std::size_t>(256*1024,party->bulk_tokens+std::size_t(elapsed)*4096);
        party->budget_at=now;
        for(unsigned lane: {0U,2U,1U,3U,4U}) {
            auto& pending=party->outgoing[lane];unsigned allowance=lane==4?8:16;
            for(unsigned n=0;n<allowance&&!pending.empty();++n) {
                auto item=std::move(pending.front());pending.pop_front();party->outgoing_bytes-=item.bytes.size();
                auto payload=watch::link_encode(item.kind,item.bytes);bool blocked=false;
                auto send=[&](const PeerAddress& destination,const secure::Key& key){
                    if((lane==3||lane==4)&&payload.size()>party->bulk_tokens){blocked=true;return;}
                    if(!transport_->traffic_ready(destination,item.traffic)){blocked=true;return;}
                    auto& serial=outbound_sequence(destination,key);if(serial==UINT64_MAX){blocked=true;return;}
                    protocol::Datagram record{{static_cast<protocol::MessageType>(watch::PartyMessageType),match_id_,serial++,unsigned(item.traffic)},payload};
                    auto plain=protocol::encode(record,transport_->maximum_plaintext_datagram_bytes());
                    if(plain.empty()){blocked=true;return;}
                    auto encrypted=secure::seal(plain,key,sender_id_,record.header.sequence,match_id_);std::string ignored;
                    const auto sent=transport_->send_status(destination,encrypted,item.traffic,ignored);
                    if(sent!=DatagramSendStatus::Sent)blocked=true;
                    else {party->queue_age.accepted(lane,item.target,item.queued,now);
                        if(lane==3||lane==4)party->bulk_tokens-=std::min(party->bulk_tokens,payload.size());}
                };
                if(is_host_) {
                    if(!item.target) {
                        // Expand broadcasts once: a blocked recipient must not
                        // cause repeats or starvation on already sent routes.
                        for(const auto& peer:party->peers)party->enqueue({peer.id,item.kind,item.traffic,item.bytes,item.queued});
                    } else if(auto* peer=party->peer(item.target))send(peer->address,peer->key);
                } else if(!item.target||item.target==host_sender_id_)send(host_address_,key_);
                if(blocked)party->enqueue(std::move(item));
            }
        }
        party->queue_age.pulse(now);
        expire_pending_joins();state_changed_.notify_all();return;
    }
}
