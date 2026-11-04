void PumpFeedbackUi(bool menu=false) {
    static auto next=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(now>=next){dkr::feedback::service().analogue(bool(LibrarySessionAnalogue()));next=now+std::chrono::milliseconds(250);}
    if(dkr::feedback::overlay_requested.exchange(false))dkr::runtime::ui::toggle_overlay();
    if(menu&&ImGui::GetCurrentContext()) {
        static bool was_active=false;const bool active=ImGui::IsAnyItemActive();
        if(active&&!was_active)dkr::feedback::service().menu(unsigned(g_selected_player));was_active=active;
    }
}
void DrawFeedbackSettings(float width,const std::function<void()>& commit) {
    namespace f=dkr::feedback;
    static bool open=false;
    const bool fixture=g_library_navigation_check&&std::getenv("DKR_OWNED_CHECK_FEEDBACK_UI")!=nullptr;
    if(fixture)open=true;
    if(!ControlsDisclosure("##controller-feedback","Controller feedback",open,width))return;
    ControlsScope(false,width);
    const unsigned player=unsigned(g_selected_player);
    auto status=f::service().status(player);
    if(fixture) {
        status.settings.mode=2;auto& d=status.device;d.instance=-1;d.name="Isolated controller feedback UI fixture";d.route="Rendering only; no hardware output";
        d.kind=player==1?f::Kind::SteamDeck:player==2?f::Kind::DualSense:player==3?f::Kind::SteamVirtual:f::Kind::Generic;
        d.capabilities=f::Rumble|(player==1?f::Deck:player==2?f::Adaptive|f::Led|f::Waveform|f::Touch|f::Motion:0);
        d.transport=player==2?f::Transport::Usb:f::Transport::Virtual;if(player==2)d.audio={{1,"Virtual USB endpoint (UI only)"}};
    }
    auto s=status.settings;const auto& d=status.device;
    bool changed=SettingsDropdown("feedback-mode","Feedback mode",&s.mode,{"Off","Classic Rumble Pak","Enhanced feedback"},width);
    const auto percent=[](int value){return SettingsRangeValue{std::to_string(value),"%"};};
    ControlsNote(d.name.empty()?"Connect and assign a controller for this player.":d.name,width);
    ControlsNote(d.route,width);
    if(!dkr::runtime::platform::rumble_enabled()) {
        ControlsNote("The shared Rumble Pak switch is off. Finite tests still work.",width);
        if(SettingsButton("Enable Rumble Pak for gameplay",width)){dkr::runtime::platform::set_rumble_enabled(true);commit();}
    }
    const char* transport=d.transport==f::Transport::Usb?"USB":d.transport==f::Transport::Bluetooth?"Bluetooth":d.transport==f::Transport::Virtual?"Steam virtual":"Unreported connection";
    ControlsNote(std::string(transport)+(d.battery>=0?" | Battery "+std::to_string(d.battery)+"%":""),width);
    changed|=SettingsRange("##feedback-strength","Feedback strength",&s.strength,0,100,width,percent);
    changed|=SettingsRange("##feedback-balance","Left / right balance",&s.balance,-100,100,width,percent);
    if(s.mode==2) {
        changed|=SettingsRange("##feedback-impact","Impacts and landings",&s.impacts,0,100,width,percent);
        changed|=SettingsRange("##feedback-boost","Boost",&s.boost,0,100,width,percent);
        changed|=SettingsRange("##feedback-environment","Skid / environment",&s.environment,0,100,width,percent);
        changed|=SettingsRange("##feedback-engine","Engine texture",&s.engine,0,100,width,percent);
        changed|=SettingsRange("##feedback-menus","Menu feedback",&s.menus,0,100,width,percent);
    }
    if(s.mode!=2)ControlsNote("Select Enhanced feedback to configure device effects.",width);
    ImGui::BeginDisabled(s.mode!=2);
    if(d.capabilities&f::Adaptive) {
        changed|=SettingsDropdown("feedback-triggers","DualSense trigger resistance",&s.resistance,{"Off","Light","Balanced","Firm"},width);
        ControlsNote("Pedal resistance follows your analogue bindings during a race. The analogue driving mod must be active.",width);
        if(d.transport==f::Transport::Bluetooth)ControlsNote("Advanced Bluetooth reports can affect other controller applications. Reconnect the pad to leave enhanced report mode.",width);
    }
    if(d.kind==f::Kind::DualSense&&(d.capabilities&f::Led)) {
        changed|=SettingsDropdown("feedback-lights","Controller lights",&s.lights,{"Default","Local player colour","Race cues"},width);
        changed|=SettingsRange("##feedback-brightness","Light brightness",&s.brightness,0,100,width,percent);
    }
    if(d.capabilities&f::Deck) {
        changed|=SettingsDropdown("feedback-deck-pattern","Deck output",&s.deck_pattern,{"Standard haptic rumble","Experimental native patterns"},width);
        ControlsNote("LCD Deck feedback comes from its trackpad haptics. System intensity still applies. Native patterns need real-device testing.",width);
    } else if(d.kind==f::Kind::SteamVirtual)ControlsNote("Steam controls this output route. Native Deck patterns require the physical Deck device; current Steam bindings and gyro are preserved.",width);
    if(d.capabilities&f::Waveform) {
        static std::array<int,4> selected{};
        static std::array<std::string,4> selected_key{};
        if(selected_key[player]!=d.key){selected[player]=0;selected_key[player]=d.key;}
        std::vector<SettingsChoice> names{{"Select controller audio endpoint"}};for(const auto& a:d.audio)names.push_back({a.second+" ["+std::to_string(a.first)+"]"});
        auto& index=selected[player];index=std::clamp(index,0,int(d.audio.size()));
        if(SettingsDropdown("feedback-audio-endpoint","USB actuator audio (experimental)",&index,names,width)) {
            f::service().endpoint(player,index?d.audio[index-1].first:0);s.waveform=0;changed=true;
        }
        bool waveform=s.waveform!=0;ImGui::BeginDisabled(index==0);
        if(SettingsCheck("##feedback-waveform","Use authored USB waveform haptics",&waveform,width)){s.waveform=waveform?1:0;changed=true;}
        ImGui::EndDisabled();
        ControlsNote("Choose the matching pad, then test it. Speaker channels stay silent. This opt-in resets when DKR-R closes; USB hardware qualification is still required.",width);
    } else if(d.kind==f::Kind::DualSense)ControlsNote("Full waveform haptics needs an exposed four-channel USB audio endpoint. Bluetooth uses timed rumble effects.",width);
    ImGui::EndDisabled();
    if(d.capabilities&f::Touch) {
        changed|=SettingsDropdown("feedback-touch","Touch shortcut",&s.touch_action,{"Off","Touch and hold: Look back","Two-finger tap: overlay","Touch and hold: gyro"},width);
        ControlsNote("Standard touchpad clicks and exposed rear buttons remain bindable in Controls. Touch gestures are local shortcuts.",width);
        ControlsNote("Gyro hold uses your existing Motion steering setup and selected sensor. Enable gyro there first.",width);
    }
    if(changed){f::service().settings(player,s);commit();}
    static int side=0,level=1;
    SettingsDropdown("feedback-test-side","Test side",&side,{"Both","Left","Right"},width);
    SettingsDropdown("feedback-test-level","Test level",&level,{"25%","50%","100%"},width);
    ImGui::BeginDisabled(d.instance<0||!(d.capabilities&f::Rumble));
    if(SettingsButton("Test feedback (450 ms)",width))f::service().test(player,unsigned(side),level==0?25U:level==1?50U:100U);
    ImGui::EndDisabled();
    if(SettingsButton("Stop feedback",width))f::service().stop(player);
    ControlsNote("Stop silences racing effects until you change a feedback setting. Test buttons remain available.",width);
    ControlsNote("Requested L/R: "+std::to_string(status.requested.left)+" / "+std::to_string(status.requested.right)+
        " | Hardware acknowledgement: "+std::to_string(d.acknowledged)+" | Failures: "+std::to_string(d.failures),width);
    ControlsNote(d.detail,width);
    if(SettingsButton("Copy feedback diagnostics",width)) {
        const auto text="DKR-R controller feedback\nProfile: "+std::to_string(player+1)+"\nDevice: "+d.name+"\nRoute: "+d.route+
            "\nConnection: "+transport+"\nCapabilities: "+std::to_string(d.capabilities)+"\nAck: "+std::to_string(d.acknowledged)+
            "\nVID/PID/Firmware: "+std::to_string(d.vendor)+"/"+std::to_string(d.product)+"/"+std::to_string(d.firmware)+
            "\nFailures: "+std::to_string(d.failures)+"\nResult: "+d.detail+"\n";SDL_SetClipboardText(text.c_str());
    }
    PaddockGap(16.F);
    if(fixture) {
        static std::array<unsigned,2> seen{};auto& bits=seen[g_feedback_live_surface?1:0];
        if(!(bits&(1U<<player))){bits|=1U<<player;std::fprintf(stderr,"[feedback][isolated-check] surface=%s profile=%u capabilities=%u rendered\n",g_feedback_live_surface?"overlay":"launcher",player,d.capabilities);}
    }
}
