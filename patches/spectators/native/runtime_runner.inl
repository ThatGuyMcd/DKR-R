bool run_party_runtime(ultramodern::renderer::WindowHandle window,const std::filesystem::path& canonical_rom,
    std::vector<std::uint8_t> bootstrap,DirectSession& lobby,const LaunchDescriptor& launch,
    unsigned timeout_seconds,std::string& error,bool scripted_check,std::shared_ptr<const dkr::mods::online::RuntimeResources> mods,
    std::vector<std::uint8_t> mod_bootstrap,std::vector<std::uint8_t> code_bootstrap) {
    namespace party=dkr::runtime::spectators;
    auto hub=party::party_link(&lobby);const auto initial=party::party_link_view(&lobby);
    std::vector<std::uint8_t> seed;
    {std::scoped_lock lock(hub->mutex);seed=hub->seed;}
    if(!initial.enabled||!initial.admitted||seed.size()!=512||bootstrap.empty()) {error="The party launch is missing its verified local bootstrap/save.";return false;}
    Shared shared;requested_stop=false;shared.confirmed_save=seed;
    std::atomic<unsigned> local_port{4};std::atomic<bool> watching{true};
    ConfirmedSaveWriter save_writer([&,host=initial.host](const ConfirmedSaveSnapshot& s,std::string& e) {
        // Only the authority stores the communal online bank. Observers never
        // install host state into a personal save, records bank or controller Pak.
        if(host&&!launch.development) {
            if(!saves::commit_online_adventure(true,launch.match_id,s.eeprom,e)||
               !saves::commit_experimental_online_paks(true,launch.match_id,s.paks,e))return false;
            if(!s.records.empty()) {
                std::vector<std::uint8_t> ignored;std::filesystem::path owner;
                if(!saves::read_online_adventure(true,launch.match_id,ignored,owner,e)||
                   !saves::records::persist(owner.parent_path()/"custom-records-v1.bin",s.records,e))return false;
            }
        }
        std::scoped_lock lock(shared.mutex);shared.confirmed_save=s.eeprom;e.clear();return true;
    });
    RuntimeWorkers workers{shared,save_writer};
    const auto saved_layout=online_view::requested_layout.load();
    platform::set_online_input_routing(true,launch.occupied_mask,255);
    workers.render=std::thread([&] {
        WorkerCompletion completed{shared.renderer_done};
        try {
            std::vector<std::uint8_t> parked(PresentationMailbox::kImageBytes);
            RT64Renderer renderer(parked.data(),window,false,true);
            if(!renderer.valid())throw std::runtime_error("The party renderer could not start.");
            auto config=ultramodern::renderer::get_graphics_config();shared.renderer_ready=true;
            std::uint64_t generation=0;unsigned previous_target=99;bool previous_watching=true;party::WatchCamera previous_camera=party::WatchCamera::AllSplits;
            while(!shared.stop) {
                renderer.reap_owned();auto next_config=ultramodern::renderer::get_graphics_config();
                if(config!=next_config){renderer.update_config(config,next_config);config=next_config;}
                const auto v=party::party_link_view(&lobby);const auto p=local_port.load();
                const auto auto_target=unsigned(std::chrono::duration_cast<std::chrono::seconds>(party::PartyRuntime::Clock::now().time_since_epoch()).count()/8)%launch.player_count;
                const auto target=watching.load()?(v.camera==party::WatchCamera::Director?auto_target:v.target):p;
                if(watching.load())online_view::requested_layout=v.camera==party::WatchCamera::AllSplits?online_view::Layout::SplitScreen:online_view::Layout::MyRacer;
                else if(previous_watching)online_view::requested_layout=saved_layout;
                previous_watching=watching.load();
                const auto custom_cars=dkr::mods::vehicles::render_resources();
                // The legacy private camera closure predates GPU-only custom
                // car draws. Keep those cars/seat alignment intact by using
                // the automatic full-screen racing camera for their director.
                party::spectator_director=watching.load()&&v.camera==party::WatchCamera::Director&&
                    (!custom_cars||custom_cars->library.packages.empty());
                if(generation!=v.party.generation||target!=previous_target||previous_camera!=v.camera) {
                    online_view::Session session;session.players=launch.player_count;session.owner=std::min(target,unsigned(launch.player_count-1));
                    for(const auto& m:v.party.members)if(m.port)session.names[*m.port]=m.name;
                    renderer.set_online_view_session(std::move(session));generation=v.party.generation;previous_target=target;previous_camera=v.camera;
                }
                bool presented=false;
                if(renderer.owned_capacity_available())if(auto lease=shared.mailbox.take()){renderer.present_owned(lease,shared.mailbox);presented=true;}
                if(!presented){renderer.repeat_owned();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            }
        }catch(const std::exception& e){shared.fail(e.what());}catch(...){shared.fail("Party presentation worker failed.");}
    });
    workers.simulation=std::thread([&] {
        WorkerCompletion completed{shared.simulation_done};std::unique_ptr<OwnedGame> world;
        try {
            auto bytes=read_rom(canonical_rom);std::string message;constexpr std::uint64_t epoch=91;
            if(!shared.mailbox.begin_epoch(epoch))throw std::runtime_error("Party presentation epoch refused.");
            const auto revision=lobby.view().room.manifest.revision==Revision::UsV80?rom::Revision::UsV80:rom::Revision::UsV77;
            world=make_owned_game_for_revision(revision,bootstrap,bytes,epoch,shared.mailbox,
                [&](ConfirmedAudio a,std::string& e){return shared.audio.push(a.rate,a.pcm,e);},message,false,
                std::uint32_t(lobby.view().room.manifest.magic_codes_hash),seed,
                OwnedBootOptions{true,launch.player_count,launch.host_control,enhancements::multiplayer_race_music_enabled(),mods,
                    std::move(mod_bootstrap),mods?mods->code():nullptr,std::move(code_bootstrap),launch.match_id,launch.development});
            if(!world)throw std::runtime_error(message);
            if(initial.host) {std::vector<std::uint8_t> paks;if(!saves::read_experimental_online_paks(true,launch.match_id,paks,message)||!world->install_initial_paks(epoch,paks,message))throw std::runtime_error(message);}
            dkr::sdk2::authority(initial.host,launch.development);
            world->set_confirmed_rumble_sink([&](unsigned p,bool enabled){if(!watching.load()&&p==local_port.load())shared.confirmed_motor.store(enabled?1:0);});
            party::PartyRuntime runtime(&lobby,*world,shared.mailbox,launch,[](auto b){return digest(b);});
            if(!runtime.begin(message))throw std::runtime_error(message);
            bool prior_watching=true;unsigned prior_port=4;auto next_save=Network::Clock::now()+std::chrono::seconds(5);
            auto observed=world->scene_view();auto observed_cursor=runtime.next();auto menu_since=observed_cursor.frame;
            bool car_selection_sent=false;
            dkr::online_stability::HostSettingsQualification host_settings_check(scripted_check);
            while(!shared.stop) {
                OwnerActivity::Scope stage(shared.simulation_activity,OwnerStage::Pump);
                const auto now=Network::Clock::now();
                auto sampled=unpack(shared.input.load());
                if(scripted_check) {
                    const auto scene=world->scene_view();const auto at=runtime.next();
                    host_settings_check.pulse(&lobby,initial.host,at.epoch,Network::Clock::now());
                    if(scene.menu!=observed.menu||(scene.menu&&scene.menu_id!=observed.menu_id)||observed_cursor.epoch!=at.epoch) {
                        observed=scene;observed_cursor=at;menu_since=at.frame;car_selection_sent=false;
                        std::fprintf(stderr,"[party][scene] epoch=%llu menu=%d id=%d level=%d type=%d viewports=%u\n",static_cast<unsigned long long>(at.epoch),scene.menu,scene.menu_id,scene.level,scene.race_type,unsigned(scene.viewports));
                    }
                    sampled={};const auto age=at.frame>=menu_since?at.frame-menu_since:0;
                    if(scene.menu) {
                        if(scene.menu_id==0&&age>=150&&age%30==0)sampled={0x1000,0,0};
                        if((scene.menu_id==3||scene.menu_id==15)&&age>=50&&age%30==20)sampled={0x8000,0,0};
                        // Opt-in disposable-profile lighting check: move the
                        // host's real cursor down/left to the first custom
                        // stage actor, then use ordinary A confirmation.
                        if(scene.menu_id==3&&dkr::runtime::vehicles::lighting::owned_road_wheel_check&&local_port.load()==0) {
                            sampled={};
                            if(age==5)sampled={0,0,-75};
                            if(age>=20&&age<220&&age%30==5)sampled={0,-75,0};
                            if(age>=220&&age%30==20)sampled={0x8000,0,0};
                        }
                        // The opt-in fresh-profile car check exercises the
                        // ordinary wooden picker with a real direction press.
                        // This branch is unreachable during normal play.
                        if(scene.menu_id==15&&dkr::runtime::vehicles::lighting::owned_car_check&&!car_selection_sent&&
                           dkr::runtime::vehicles::lighting::owned_car_picker.load(std::memory_order_relaxed)==1&&age%30==5) {
                            sampled={0x0100,0,0};car_selection_sent=true;
                            std::fprintf(stderr,"[custom-vehicles][isolated-check] local-port=%u picker-input=d-pad-right; ordinary confirmation follows\n",local_port.load());
                        }
                    } else sampled={0x8000,std::int8_t((at.frame/15)%2?30:-30),0};
                }
                dkr::online_stability::ReplaySlice replay_slice(Network::Clock::now());
                do {
                    if(!runtime.pulse(Network::Clock::now(),sampled,message)) {
                        if(!message.empty())throw std::runtime_error(message);shared.stop=true;break;
                    }
                    if(!scripted_check)sampled=unpack(shared.input.load());
                }while(!shared.stop&&replay_slice.again(runtime.replay_ready(),Network::Clock::now()));
                if(shared.stop)break;
                watching=runtime.watching();local_port=runtime.local_port();
                if(prior_watching!=watching.load()||prior_port!=local_port.load()) {
                    // A role transition starts fresh local achievement context;
                    // no observation from spectating can retroactively unlock.
                    dkr::runtime::achievements::begin(true,local_port.load(),bool(mods),launch.development||watching.load());
                    prior_watching=watching.load();prior_port=local_port.load();
                }
                const auto cursor=runtime.next();
                const auto stats=runtime.statistics();
                const auto cause=runtime.pause_reason();
                using Pause=dkr::online_stability::Pause;
                const auto wait=cause==Pause::Network||cause==Pause::LocalInput?PumpWait::OwnerInput:
                    cause==Pause::Confirmation?PumpWait::Confirmation:cause==Pause::Presentation?PumpWait::PresentationDrain:
                    cause==Pause::ScenePeers?PumpWait::ScenePeers:cause==Pause::ScenePreparation?PumpWait::ScenePreparation:
                    cause==Pause::Pacing?PumpWait::FramePacing:PumpWait::None;
                report({true,wait,cursor.epoch,cursor.frame,runtime.confirmed(),stats.rollbacks,stats.replayed_frames,runtime.status(),cursor.epoch==0});
                if(now>=next_save) {
                    if(initial.host&&!save_writer.submit(world->confirmed_save(),world->confirmed_paks(),message,world->confirmed_records()))throw std::runtime_error(message);
                    next_save=now+std::chrono::seconds(5);
                }
                // Correction already has finite admitted work. Yield after a
                // bounded slice instead of adding a timer sleep to every
                // replayed frame. Pacing/input/GPU waits still sleep normally.
                if(runtime.replay_ready())std::this_thread::yield();
                else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }catch(const std::exception& e){shared.fail(e.what());if(!initial.host&&party::party_watching(&lobby))lobby.disconnect("Stopped watching: "+std::string(e.what()));else party::party_stop(&lobby,e.what());}
         catch(...){shared.fail("Party simulation worker failed.");if(!initial.host&&party::party_watching(&lobby))lobby.disconnect("Stopped watching.");else party::party_stop(&lobby,"Party simulation stopped.");}
        if(world&&initial.host) {std::string message;if(!save_writer.submit(world->confirmed_save(),world->confirmed_paks(),message,world->confirmed_records()))shared.fail(message);}
    });
    const auto start=Network::Clock::now();ConfirmedAudioMailbox::Block block;
    std::array<std::int16_t,ConfirmedAudioMailbox::kMaximumBytes/2> pcm{};unsigned routed=99;
    std::uint16_t prior_buttons=0;auto qualified_approval_at=start;
    while(!shared.stop&&!shared.simulation_done) {
        // Continue the isolated CLI host's existing admission automation after
        // launch, so late spectators exercise the ordinary approval API too.
        // Normal launcher/overlay sessions still require the host's decision.
        if(scripted_check&&initial.host&&Network::Clock::now()>=qualified_approval_at) {
            qualified_approval_at=Network::Clock::now()+std::chrono::milliseconds(100);
            for(const auto& pending:lobby.view().pending_joins) {
                std::string admission_error;
                if(lobby.approve_join(pending.request_id,admission_error))
                    std::fprintf(stderr,"[spectator][qualification] approved-late-member=%llu\n",static_cast<unsigned long long>(pending.request_id));
            }
        }
        const auto port=local_port.load();
        if(port!=routed){for(int p=0;p<4;++p)platform::set_rumble(p,false);platform::set_online_input_routing(true,launch.occupied_mask,port<4?std::uint8_t(port):255);routed=port;}
        const auto motor=shared.confirmed_motor.exchange(-1);if(motor>=0&&port<4&&!watching.load())platform::set_rumble(int(port),motor!=0);
        platform::pump_window_events(nullptr);platform::poll_input();
        std::uint16_t buttons=0;float x=0,y=0;bool blocked=false;platform::get_local_online_input(&buttons,&x,&y,&blocked);
        const auto pressed=std::uint16_t(buttons&~prior_buttons);prior_buttons=buttons;
        if(watching.load()&&!blocked) {
            const auto camera=party::party_link_view(&lobby);
            if(pressed&0x0008)party::party_camera(&lobby,party::WatchCamera((unsigned(camera.camera)+1)%3),camera.target);
            if(pressed&0x0300)party::party_camera(&lobby,party::WatchCamera::Follow,
                (camera.target+((pressed&0x0100)?1:unsigned(launch.player_count-1)))%launch.player_count);
        }
        shared.input=pack(blocked||watching.load()?PackedInput{}:online_input_broker().local_sample().input);
        for(unsigned i=0;i<ConfirmedAudioMailbox::kCapacity&&shared.audio.take(block);++i) {
            for(std::size_t j=0;j<block.bytes/2;++j)pcm[j^1]=std::int16_t(std::uint16_t(block.pcm[2*j])|(std::uint16_t(block.pcm[2*j+1])<<8));
            platform::set_audio_frequency(block.rate);platform::queue_audio(pcm.data(),block.bytes/2);
        }
        if(const auto failure=save_writer.error();!failure.empty())shared.fail(failure);
        const auto state=party::party_link_view(&lobby);if(state.stopping||!state.enabled)shared.stop=true;
        const auto lifecycle=ui::lifecycle_request();
        if(requested_stop||lifecycle!=ui::LifecycleRequest::None||(timeout_seconds&&Network::Clock::now()-start>=std::chrono::seconds(timeout_seconds))) {
            if(lifecycle==ui::LifecycleRequest::Exit||lifecycle==ui::LifecycleRequest::Restart||requested_stop)lobby.disconnect(initial.host?"The host closed the lobby.":"Left the lobby.");
            else if(watching.load()&&!initial.host)lobby.disconnect("Stopped watching.");
            else party::party_stop(&lobby,"Returned to the lobby.");
            shared.stop=true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    workers.retire();
    party::spectator_director=false;online_view::requested_layout=saved_layout;
    {std::scoped_lock lock(shared.mutex);error=shared.failure;party::party_returned(&lobby,shared.confirmed_save);}
    if(ui::lifecycle_request()!=ui::LifecycleRequest::Exit&&ui::lifecycle_request()!=ui::LifecycleRequest::Restart)
        ui::report_online_game_end(error.empty()?party::party_link_view(&lobby).status:error,lobby.active());
    return error.empty();
}
