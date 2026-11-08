// DirectSession's authenticated owner holds both session and party locks.
{
    auto read_settings=[&] {
        const auto& r=room_view_.rules;
        return stability::HostSettings{unsigned(r.host_control),r.maximum_players,r.rollback_window,r.manual_input_delay,
            r.automatic_input_delay,r.record_replay,party->enabled&&party->allow_spectators,r.development};
    };
    stability::HostSettingsContext context;
    context.spectator_capable=party->enabled;
    context.playing=bool(party->launch)||state_==ConnectionState::Running||state_==ConnectionState::Loading;
    context.transition=party->stopping||party->returned||state_==ConnectionState::Loading||launch_countdown_active_||
        (!context.playing&&lobby_.room().phase!=RoomPhase::Waiting);
    context.occupied_ports=1;
    if(party->enabled) {
        for(const auto& member:party->party.snapshot().members)if(member.role==watch::Role::Racer&&member.port)
            context.occupied_ports=std::max(context.occupied_ports,unsigned(*member.port)+1);
    } else for(const auto& player:room_view_.players)if(player.occupied)
        context.occupied_ports=std::max(context.occupied_ports,unsigned(player.slot)+1);
    for(const auto& request:pending_joins_)if(request.active&&request.mods_approved&&!request.mods_verified) {
        const auto role=party->pending_watch.find(request.sender_id);
        if(!party->enabled||(role!=party->pending_watch.end()&&!role->second))++context.occupied_ports;
    }
    context.occupied_ports=std::min(context.occupied_ports,4U);
    auto request=extension->take_host_settings();
    const auto previous=extension->host_settings();
    if(!request&&!context.playing&&!context.transition&&previous.queued)
        request=stability::HostSettingsRequest{match_id_,previous.reply,*previous.queued};
    if(request&&is_host_&&request->room==match_id_) {
        const auto current=read_settings();const auto plan=stability::plan_host_settings(current,request->desired,context);
        std::string error=plan.error;
        if(plan) {
            auto next=room_view_.rules;
            next.host_control=HostControlPolicy(plan.immediate.menu_control);
            next.maximum_players=std::uint8_t(plan.immediate.maximum_players);
            next.rollback_window=std::uint8_t(plan.immediate.prediction_window);
            next.manual_input_delay=std::uint8_t(plan.immediate.input_delay);
            next.automatic_input_delay=plan.immediate.automatic_delay;next.record_replay=plan.immediate.record_replay;
            const bool rules_changed=next!=room_view_.rules;
            auto candidate=party->party;
            if(party->enabled&&current!=plan.immediate) {
                auto snapshot=candidate.snapshot();
                if(snapshot.generation==UINT64_MAX)error="Lobby settings generation exhausted.";
                else {++snapshot.generation;(void)candidate.replace_verified(std::move(snapshot),error);}
            }
            if(error.empty()) {
                const auto host=std::to_string(sender_id_);
                const bool changed=context.playing
                    ?lobby_.set_live_timing(host,next.automatic_input_delay,next.manual_input_delay,next.rollback_window,error)
                    :lobby_.set_rules(host,next,error);
                if(changed) {
                    // The admitted controller roster and launch stay unchanged.
                    room_view_.rules=next;party->maximum_racers=next.maximum_players;
                    party->allow_spectators=plan.immediate.spectators;
                    if(!next.automatic_input_delay)input_delay_=next.manual_input_delay;
                    if(party->enabled) {
                        party->party=std::move(candidate);party->advertised={};
                        if(!context.playing&&rules_changed)party->ready.clear();
                    } else {
                        room_view_=lobby_.room();broadcast_lobby();
                    }
                    const auto status=plan.queued?"Timing updated now. Player capacity, menu control and replay changes are queued for your return to the lobby.":"Host settings applied to this lobby.";
                    extension->finish_host_settings(*request,plan.queued,status,true);
                    auto identity=extension->identity();identity.dynamic=next.automatic_input_delay;
                    identity.manual_delay=next.manual_input_delay;identity.prediction_window=next.rollback_window;extension->configure(std::move(identity));
                    std::fprintf(stderr,"[online][host-settings] room=%llu request=%llu playing=%u auto=%u delay=%u window=%u queued=%u spectators=%u\n",
                        static_cast<unsigned long long>(match_id_),static_cast<unsigned long long>(request->serial),unsigned(context.playing),
                        unsigned(next.automatic_input_delay),unsigned(next.manual_input_delay),unsigned(next.rollback_window),unsigned(bool(plan.queued)),unsigned(party->allow_spectators));
                }
            }
        }
        if(!error.empty())extension->finish_host_settings(*request,{},std::move(error),false);
    }
    extension->publish_host_settings(read_settings(),context);
}
