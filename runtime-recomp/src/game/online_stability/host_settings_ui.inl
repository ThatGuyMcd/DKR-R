void DrawOlActiveHostSettings(float width,const dkr::online_stability::HostSettingsView& view) {
    namespace stability=dkr::online_stability;
    struct Editor {std::uint64_t room=0,reply=0;stability::HostSettings draft;};
    static Editor edit;
    if(edit.room!=view.room){edit={view.room,0,view.queued.value_or(view.current)};}
    if(view.reply&&edit.reply!=view.reply) {
        edit.reply=view.reply;
        if(view.accepted) {
            const auto& saved=view.submitted;
            g_online_host_control=int(saved.menu_control);g_online_maximum_players=int(saved.maximum_players);
            g_online_rollback_window=int(saved.prediction_window);g_online_manual_delay=int(saved.input_delay);
            g_online_automatic_delay=saved.automatic_delay;g_online_record_replay=saved.record_replay;
            g_online_allow_spectators=saved.spectators;SaveSettings();
        }
    }
    DrawOlFeature(width,"Host settings","Applies to your open lobby, including while you are playing.",[&](float inner) {
        if(!view.status.empty()){OlParagraph(view.status,15.F,view.accepted?kOlCream:kOlAmber,inner);PaddockGap(16.F);}
        auto& draft=edit.draft;
        OlSwitchRow("##spectators","Allow new spectators","Existing spectators can keep watching.",&draft.spectators,inner,!view.context.spectator_capable);
        if(!view.context.spectator_capable)OlParagraph("Create a new spectator-capable lobby to enable watching.",14.F,kOlSoft,inner,true);
        static const std::vector<std::string> ownership{"Host until character select","Host for shared menus","All players"};
        static const std::vector<std::string> racers{"2","3","4"};
        int menu=int(draft.menu_control),maximum=int(draft.maximum_players)-2;
        PaddockGap(16.F);if(OlSelect("menu-ownership","Who controls the menus?",&menu,ownership,inner))draft.menu_control=unsigned(menu);
        PaddockGap(16.F);if(OlSelect("maximum-racers","Maximum players",&maximum,racers,inner))draft.maximum_players=unsigned(maximum+2);
        PaddockGap(16.F);OlParagraph("Online mode: Rollback",15.F,kOlCream,inner);
        int window=int(draft.prediction_window),delay=int(draft.input_delay);
        PaddockGap(16.F);if(OlRange("experimental-window","Prediction window (frames)",&window,2,20,inner))draft.prediction_window=unsigned(window);
        OlParagraph("Updates during this game. Reducing the window lets inputs already in flight catch up before more prediction.",14.F,kOlSoft,inner,true);
        PaddockGap(16.F);OlSwitchRow("##automatic-delay","Automatic input delay","Adjusts input delay for the lobby's connection.",&draft.automatic_delay,inner,false);
        PaddockGap(16.F);if(OlRange("input-delay","Input delay (frames)",&delay,0,9,inner,draft.automatic_delay))draft.input_delay=unsigned(delay);
        PaddockGap(16.F);OlSwitchRow("##record-replay","Record game replay","Records game inputs for replay tools, not a video.",&draft.record_replay,inner,false);
        if(view.context.playing){PaddockGap(12.F);OlParagraph("Player capacity, menu control and replay changes apply when everyone returns to the lobby.",14.F,kOlSoft,inner);}
        PaddockGap(16.F);bool development=draft.development;
        OlSwitchRow("##development-lobby","Development lobby","Create a new lobby to change development mode.",&development,inner,true);
        PaddockGap(16.F);
        const auto plan=stability::plan_host_settings(view.current,draft,view.context);
        if(!plan){OlParagraph(plan.error,14.F,kOlAmber,inner);PaddockGap(12.F);}
        const auto apply=[&](const stability::HostSettings& desired) {
            std::uint64_t serial=0;std::string error;
            if(!stability::control(&dkr::runtime::netplay::session())->request_host_settings(view.room,desired,serial,error))OlNotify(error);
        };
        {const OlDisabled disabled(!plan);if(OlButton("Apply changes",OlTone::Go))apply(draft);}
        PaddockGap(8.F);if(OlButton("Reset changes"))draft=view.queued.value_or(view.current);
        if(view.queued){PaddockGap(8.F);if(OlButton("Cancel queued changes")){draft=view.current;apply(draft);}}
    });
}
