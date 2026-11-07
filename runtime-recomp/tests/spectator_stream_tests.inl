// Compiled against the final production party runtime by DKROnlinePartyChecks.
void live_host_rules() {
    net::Lobby lobby;net::Rules rules;rules.synchronization=net::SynchronizationMode::ExperimentalRollback;
    net::CompatibilityManifest manifest;manifest.owned_engine_hash=std::string(64,'a');std::string error;
    check(lobby.create("room","TEST01","Fixture",net::Visibility::Private,"host","Host",manifest,rules,error),"create timing policy fixture");
    check(bool(lobby.join("client","Client",manifest,error)),"admit timing fixture client");
    check(lobby.set_ready("host",true,error)&&lobby.set_ready("client",true,error),"ready timing fixture");
    check(lobby.begin_loading("host",error),"load timing fixture");
    const auto loading=lobby.room().generation;
    check(!lobby.set_live_timing("host",false,3,20,error)&&lobby.room().generation==loading,"loading cannot alter live timing");
    check(lobby.set_loaded("host",true)&&lobby.set_loaded("client",true)&&lobby.begin_running("host",error),"start timing fixture");
    const auto before=lobby.room();
    check(!lobby.set_live_timing("client",false,3,20,error),"client cannot author host rules");
    check(!lobby.set_live_timing("host",false,10,20,error)&&!lobby.set_live_timing("host",false,3,21,error),"reject out-of-range live rules");
    check(lobby.room().generation==before.generation,"rejected rules leave generation unchanged");
    check(lobby.set_live_timing("host",false,3,20,error),"host changes running timing");
    check(lobby.room().generation==before.generation+1&&lobby.room().phase==net::RoomPhase::Running,"change publishes a new running lobby generation");
    check(lobby.room().rules.rollback_window==20&&!lobby.room().rules.automatic_input_delay&&lobby.room().rules.manual_input_delay==3,"running rules carry requested timing");
    check(lobby.room().manifest==before.manifest&&lobby.room().rules.host_control==before.rules.host_control&&lobby.room().players[1].loaded,"save, control and loading state stay admitted");
    check(lobby.set_live_timing("host",false,3,20,error)&&lobby.room().generation==before.generation+1,"repeated Apply is idempotent");
}
void live_prediction_resizing() {
    for(bool during_replay:{false,true}) {
    Fixture f(false,true,false,0,4);f.runtime->activate({91,0});
    auto& driver=*f.runtime->driver;
    const auto launch_hash=net::launch_descriptor_hash(f.runtime->launch);
    for(unsigned frame=0;frame<4;++frame) {
        check(driver.receive(91,0,frame,{})==ex::InputResult::Accepted,"author local input before resize");
        check(driver.step()==ex::Step::Advanced,"fill original prediction window");
    }
    check(driver.set_prediction_window(20,f.error)&&driver.statistics().next_frame==4,"grow live window without restart");
    for(unsigned frame=4;frame<14;++frame) {
        check(driver.receive(91,0,frame,{})==ex::InputResult::Accepted,"author local input after growth");
        check(driver.step()==ex::Step::Advanced,"new history permits further prediction");
    }
    unsigned requested=during_replay?20:2;dkr::online_stability::PredictionScope limit(requested);
    if(!during_replay)check(driver.step()==ex::Step::PredictionLimit&&driver.prediction_window()==2&&driver.statistics().next_frame==14,
        "shrink holds new prediction and retains speculative history");
    check(driver.receive(91,1,0,{0x8000,0,0})==ex::InputResult::Accepted,"first late correction after shrink");
    for(unsigned n=0;n<14;++n) {
        if(during_replay&&n==1)requested=2;
        check(driver.step(false)!=ex::Step::Failed,"shrinking cannot strand an already admitted correction");
    }
    check(driver.statistics().next_frame==14&&driver.statistics().confirmed_frames==1,"old correction can replay beyond the smaller limit without losing checkpoints");
    for(unsigned frame=1;frame<14;++frame)
        check(driver.receive(91,1,frame,{0x8000,0,0})==ex::InputResult::Accepted,"retain the remaining late actual inputs");
    check(driver.step(false)!=ex::Step::Failed,"confirm old prediction after shrink");
    check(driver.statistics().confirmed_frames==14&&driver.statistics().next_frame==14&&driver.statistics().replayed_frames==14,
        "all old predicted frames correct and commit under the smaller limit");
    check(net::launch_descriptor_hash(f.runtime->launch)==launch_hash&&driver.active(),"live limits preserve admitted launch identity");
    check(!driver.set_prediction_window(21,f.error)&&driver.prediction_window()==2,"reject invalid live limits without disturbing history");
    }
}
void buffered_spectator_delivery() {
    Fixture f(false,true,true,1,20,4);
    if(f.runtime->encoding.valid())(void)f.runtime->encoding.get();
    f.runtime->encoded.reset();f.runtime->encode_pending.reset();f.runtime->needing.clear();
    for(unsigned id=6;id<=12;++id)check(f.hub->party.add(id,"Watcher "+std::to_string(id),{},f.error),"eight spectator roster");
    check(f.runtime->roster.replace_verified(f.hub->party.snapshot(),f.error),"full roster admitted");
    auto raw=f.baseline({91,0});f.runtime->held=raw;f.runtime->feed.clear();
    check(f.runtime->feed.baseline(raw)==FeedResult::Accepted,"confirmed baseline");
    for(unsigned frame=0;frame<400;++frame){ConfirmedFrame item;item.cursor={91,frame};check(f.runtime->feed.append(item)==FeedResult::Accepted,"confirmed history");}
    for(unsigned id=5;id<=12;++id){
        f.runtime->start_delivery(id,raw);auto& d=f.runtime->deliveries.at(id);
        while(!d.sender.complete())d.sender.sent();d.accepted=d.admitted=true;
    }
    const auto start=Clock::now();
    for(unsigned pulse=0;pulse<30;++pulse){
        f.runtime->transfer(start+std::chrono::milliseconds(50*pulse));
        for(auto& q:f.hub->outgoing)q.clear();f.hub->outgoing_bytes=0;
    }
    for(unsigned id=5;id<=12;++id){
        auto& d=f.runtime->deliveries.at(id);
        check(d.sent.frame==0&&d.queued.frame==120,"all eight observers pipeline a bounded four seconds without an ACK");
        Write ack;ack.n(d.sender.offer().transfer).at({91,121});
        Write packet;packet.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(Msg::FrameAck),1).n(f.runtime->match).n(1).data(ack.b);
        f.runtime->message(id,packet.b,start);
        check(d.sent.frame==0,"observer cannot acknowledge unsent history");
        ack.b.clear();ack.n(d.sender.offer().transfer).at({91,64});packet.b.resize(21);packet.data(ack.b);
        f.runtime->message(id,packet.b,start);check(d.sent.frame==64,"cumulative ACK retires delivered history");
    }
    f.runtime->transfer(start+std::chrono::seconds(2));
    for(unsigned id=5;id<=12;++id)check(f.runtime->deliveries.at(id).queued.frame>=120,"a delayed ACK never rewinds fresh delivery");
    auto fresh=raw;fresh.next.frame=300;fresh.digest[0]^=1;
    check(f.runtime->feed.baseline(fresh,true)==FeedResult::Accepted,"fresh late-join checkpoint retains old viewers' history");
    check(f.runtime->feed.covers({91,64})&&f.runtime->feed.after({91,64},8).size()==8,"new snapshot does not invalidate existing deliveries");
    f.runtime->phase=decltype(f.runtime->phase)::Running;f.runtime->encoded=raw;
    check(f.runtime->current_checkpoint(),"checkpoint at current frontier remains reusable");
    f.runtime->encoded=fresh;
    check(!f.runtime->current_checkpoint(),"future checkpoint is never eligible for delivery");
    f.runtime->activate({91,0});
    f.runtime->base=100;f.runtime->encoded=raw;
    check(!f.runtime->current_checkpoint(),"late joins refresh a snapshot older than two seconds");
    f.runtime->base=0;f.hub->stopping=true;f.hub->enabled=false;
    f.runtime->send(2,Msg::Progress);f.hub->stopping=false;f.hub->enabled=true;
    f.runtime->sample({},0);f.runtime->sample({},1);
    check(f.runtime->driver->step()==ex::Step::Advanced,"speculative step for confirmed export");
    std::array<std::uint8_t,16> copy{};const auto captures=f.world.captures;
    check(f.runtime->driver->copy_confirmed_checkpoint(copy),"retained confirmed frontier is exportable while live state is speculative");
    check(f.world.captures==captures&&copy==f.world.state,"export never captures or rewinds the live world");
}

void buffered_spectator_playback() {
    struct World:ObserverWorld {
        unsigned ticks=0,published=0;
        std::uint64_t schema()const override{return 101;}
        std::size_t state_bytes()const override{return 16;}
        Work retire(Cursor,std::string&)override{return Work::Ready;}
        bool admit(const Baseline&,std::string&)override{return true;}
        Work prepare(const ConfirmedFrame&,std::string&)override{return Work::Ready;}
        bool tick_and_commit(const ConfirmedFrame&,std::string&)override{++ticks;return true;}
        bool publish(Cursor,std::string&)override{++published;return true;}
        Work prepare_scene(Cursor,std::string&)override{return Work::Ready;}
    } world;
    ObserverPlayback playback(world,123,digest,45);std::string error;
    auto state=std::make_shared<Bytes>(16,0);Baseline baseline{{91,0},123,101,1,digest(*state),state,0};
    check(playback.offer(baseline,error),"buffered playback baseline");const auto start=Clock::now();
    check(playback.pulse(start,error)==Work::Ready,"baseline admitted");
    for(unsigned frame=0;frame<44;++frame){ConfirmedFrame item;item.cursor={91,frame};check(playback.append(std::span(&item,1),error)==FeedResult::Accepted,"buffer confirmed input");}
    check(playback.pulse(start,error)==Work::Pending&&world.ticks==0,"playback waits for 1.5 seconds of history");
    ConfirmedFrame last;last.cursor={91,44};check(playback.append(std::span(&last,1),error)==FeedResult::Accepted,"fill playback buffer");
    check(playback.pulse(start,error)==Work::Ready&&world.ticks==1&&world.published==1,"buffered frames render without fast-forward suppression");
    for(unsigned frame=45;frame<165;++frame){
        ConfirmedFrame item;item.cursor={91,frame};check(playback.append(std::span(&item,1),error)==FeedResult::Accepted,"steady buffered feed");
        check(playback.pulse(start+std::chrono::nanoseconds(1000000000LL*(frame-44)/30),error)==Work::Ready,"steady 30 Hz buffered playback");
    }
    check(world.ticks==121&&world.published==121&&playback.view().queued==44,"steady delay keeps rendering every frame");
}
