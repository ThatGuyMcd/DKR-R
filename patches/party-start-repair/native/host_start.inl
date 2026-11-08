// Within the DirectSession and PartyLink owner locks.
auto gate=dkr::runtime::party_start::gate(this);
using StartPhase=dkr::runtime::party_start::Phase;
auto required_start=[&](const auto& acknowledgements){
    for(const auto& m:party->party.snapshot().members)
        if((m.role==watch::Role::Racer||m.host)&&m.id!=sender_id_&&!acknowledgements.contains(m.id))return false;
    return true;
};
auto cancel_start=[&](std::string reason){
    if(gate->hash){queue(0,watch::LinkKind::Start,dkr::runtime::party_start::receipt(3,*gate));gate->retired=gate->hash;}
    gate->phase=StartPhase::Cancelled;gate->launch.reset();gate->prepared.clear();gate->committed.clear();
    reset_launch_transaction_locked();party->launch.reset();launch_descriptor_.reset();launch_requested_=false;
    party->running=party->stopping=false;state_=ConnectionState::Hosting;
    party->ready.clear();party->advertised={};party->status=status_=std::move(reason);
    std::fprintf(stderr,"[lobby][start-cancel] %s\n",status_.c_str());
};
if(party->start_requested){
    party->start_requested=false;
    if(!party->launch&&!party->stopping&&gate->phase!=StartPhase::Countdown&&gate->phase!=StartPhase::Committing){
        std::string reason;bool all=true;
        for(const auto& m:party->party.snapshot().members)if(m.role==watch::Role::Racer){
            if(!party->ready[m.id]){all=false;reason="Every racer must be ready before starting.";break;}
            if(m.id!=sender_id_){
                const auto* peer=party->peer(m.id);
                if(!peer||!peer->rtt_count||now-peer->heard>=std::chrono::seconds(5)){
                    all=false;reason="Waiting for a completed two-way racer connection. Please try Start again in a moment.";break;
                }
                // An open lobby/control channel alone cannot deliver the
                // initial rollback checkpoint. Prove every required route.
                for(auto lane:{TransportTrafficClass::Control,TransportTrafficClass::Authoritative,
                               TransportTrafficClass::Realtime,TransportTrafficClass::Checkpoint})
                    if(!transport_->traffic_ready(peer->address,lane)){all=false;reason="A racer's Rollback channels are still connecting. Please try Start again in a moment.";break;}
            }
        }
        if(all&&session_save_.size()==512&&watch::prepare_racing_ports(party->party,reason)){
            unsigned count=0;for(const auto& m:party->party.snapshot().members)if(m.role==watch::Role::Racer)++count;
            LaunchDescriptor launch;launch.match_id=match_id_;launch.lobby_generation=++party->launch_serial;
            launch.compatibility_hash=manifest_hash(manifest_);launch.occupied_mask=std::uint8_t((1U<<count)-1);launch.player_count=std::uint8_t(count);
            launch.input_delay_frames=input_delay_;launch.rollback_window=room_view_.rules.rollback_window;
            launch.synchronization=SynchronizationMode::ExperimentalRollback;launch.host_control=room_view_.rules.host_control;launch.development=room_view_.rules.development;
            if(valid_launch_descriptor(launch)&&!protocol::encode_start({0,launch,launch_descriptor_hash(launch)}).empty()){
                const auto retired=gate->retired;*gate={};gate->retired=retired;gate->launch=launch;gate->hash=launch_descriptor_hash(launch);
                gate->roster=party->party.snapshot().generation;gate->save_generation=session_save_generation_;gate->save_hash=manifest_.session_save_hash;
                gate->rules=room_view_.rules;gate->phase=StartPhase::Countdown;gate->deadline=now+std::chrono::seconds(30);
                gate->prepared.insert(sender_id_);gate->committed.insert(sender_id_);
                reset_launch_transaction_locked();launch_countdown_active_=true;launch_stage_=LaunchStage::Countdown;
                launch_countdown_deadline_=now+std::chrono::seconds(5);++launch_countdown_generation_;
                if(!launch_countdown_generation_)++launch_countdown_generation_;
                party->advertised={};party->status=status_="DKR-R Online starts in five seconds.";
                std::fprintf(stderr,"[lobby][countdown] five seconds; racers=%u\n",count);
            }else reason="The lobby settings cannot start this race.";
        }else if(reason.empty())reason="The verified online save is not ready.";
        if(!reason.empty())party->status=status_=reason;
    }
}
if(gate->phase==StartPhase::Countdown||gate->phase==StartPhase::Committing){
    bool intact=gate->launch&&gate->roster==party->party.snapshot().generation&&gate->rules==room_view_.rules&&
        gate->save_generation==session_save_generation_&&gate->save_hash==manifest_.session_save_hash&&
        gate->launch->compatibility_hash==manifest_hash(manifest_);
    for(const auto& m:party->party.snapshot().members)if(m.role==watch::Role::Racer&&!party->ready[m.id])intact=false;
    if(!intact)cancel_start("The lobby changed. The five-second start was cancelled safely; racers must ready again.");
    else if(party->stop_requested){party->stop_requested=false;cancel_start("The synchronized start was cancelled. Racers must ready again.");}
    else if(now>=gate->deadline)cancel_start("A racer did not acknowledge the synchronized start in time. The lobby is still open; check the connection and try again.");
    else{
        if(gate->phase==StartPhase::Countdown&&now>=launch_countdown_deadline_&&required_start(gate->prepared)){
            gate->phase=StartPhase::Committing;gate->deadline=now+std::chrono::seconds(15);gate->retry={};
            launch_countdown_active_=false;launch_stage_=LaunchStage::Committing;
            party->status=status_="Countdown complete. Waiting for every racer to accept the same launch.";
        }
        if(gate->phase==StartPhase::Committing&&required_start(gate->committed)){
            gate->phase=StartPhase::Released;launch_stage_=LaunchStage::Idle;launch_countdown_active_=false;
            party->launch=*gate->launch;party->seed=session_save_;party->running=party->stopping=party->returned=party->launch_sent=false;
            launch_descriptor_=*gate->launch;launch_requested_=true;state_=ConnectionState::Loading;party->advertised={};
            const auto* me=party->party.find(sender_id_);local_slot_=me&&me->port?*me->port:255;
            party->status=status_="Every racer accepted the launch. Preparing the synchronized game.";
            std::fprintf(stderr,"[lobby][start-release] all required racers acknowledged; racers=%u\n",unsigned(gate->launch->player_count));
        }else if(now>=gate->retry){
            // Control is reliable; retries also make validation insensitive to
            // roster/Room delivery order and duplicate messages idempotent.
            queue(0,watch::LinkKind::Roster,watch::wire::roster(party->party.snapshot()));
            const auto stage=std::uint8_t(gate->phase==StartPhase::Countdown?0:1);
            queue(0,watch::LinkKind::Launch,protocol::encode_start({stage,*gate->launch,gate->hash}));
            gate->retry=now+std::chrono::milliseconds(250);
            if(now>=launch_countdown_deadline_&&gate->phase==StartPhase::Countdown)
                party->status=status_="Countdown complete. Waiting for every racer to validate the starting grid.";
        }
    }
}
