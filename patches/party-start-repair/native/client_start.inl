protocol::StartPayload start;std::string reason;
auto gate=dkr::runtime::party_start::gate(this);
using StartPhase=dkr::runtime::party_start::Phase;
if(!protocol::decode_start(message.body,start,reason)){
    party->status="The host sent an invalid starting grid. Please recreate the lobby.";
    std::fprintf(stderr,"[lobby][start-reject] invalid descriptor: %s\n",reason.c_str());
}else{
    const auto& launch=start.descriptor;
    const auto& roster=party->party.snapshot();
    unsigned count=0;std::uint8_t mask=0;
    for(const auto& m:roster.members)if(m.role==watch::Role::Racer&&m.port){++count;mask|=std::uint8_t(1U<<*m.port);}
    const bool valid=launch.match_id==match_id_&&launch.synchronization==SynchronizationMode::ExperimentalRollback&&
        launch.compatibility_hash==manifest_hash(room_view_.manifest)&&room_view_.generation==roster.generation&&
        launch.player_count==count&&launch.occupied_mask==mask&&party->seed.size()==512&&
        party->save_generation==session_save_generation_&&party->save_hash==manifest_.session_save_hash&&
        start.descriptor_hash!=gate->retired;
    if(!valid){
        // A roster or Room can be in flight. Do not arm a different grid or
        // silently start; the host retries and its bounded gate names failure.
        if(gate->hash!=start.descriptor_hash){
            gate->hash=start.descriptor_hash;
            std::fprintf(stderr,"[lobby][start-wait] roster=%llu room=%llu racers=%u/%u manifest=%u seed=%u\n",
                static_cast<unsigned long long>(roster.generation),static_cast<unsigned long long>(room_view_.generation),count,unsigned(launch.player_count),
                unsigned(launch.compatibility_hash==manifest_hash(room_view_.manifest)),unsigned(party->seed.size()==512));
        }
        party->status="Validating the host's current roster, settings and save before starting...";
    }else if(start.stage==0){
        if(!party->launch){
            if(!gate->launch||gate->hash!=start.descriptor_hash){
                gate->launch=launch;gate->hash=start.descriptor_hash;gate->roster=roster.generation;
                gate->save_generation=session_save_generation_;gate->save_hash=manifest_.session_save_hash;
                gate->phase=StartPhase::Countdown;gate->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
                std::fprintf(stderr,"[lobby][start-prepare] starting grid verified; awaiting countdown and commit\n");
            }
            party->enqueue({host_sender_id_,watch::LinkKind::Start,TransportTrafficClass::Control,dkr::runtime::party_start::receipt(1,*gate)});
        }
    }else if(start.stage==1){
        const bool prepared=gate->launch&&*gate->launch==launch&&gate->hash==start.descriptor_hash&&gate->roster==roster.generation;
        // A late spectator may join an already running match. It never blocks
        // racers, never owns a port, and still validates the complete descriptor.
        if(prepared||party->local_watching()){
            gate->launch=launch;gate->hash=start.descriptor_hash;gate->roster=roster.generation;
            gate->save_generation=session_save_generation_;gate->save_hash=manifest_.session_save_hash;gate->phase=StartPhase::Released;
            party->enqueue({host_sender_id_,watch::LinkKind::Start,TransportTrafficClass::Control,dkr::runtime::party_start::receipt(2,*gate)});
            if(!party->launch||*party->launch!=launch){
                party->launch=launch;party->running=party->stopping=party->returned=false;
                launch_countdown_active_=false;launch_stage_=LaunchStage::Idle;
                launch_descriptor_=launch;launch_requested_=true;state_=ConnectionState::Loading;
                party->status=status_="Preparing the online game";
                std::fprintf(stderr,"[lobby][launch] accepted committed stage=1 racers=%u\n",unsigned(launch.player_count));
            }
        }
    }
}
