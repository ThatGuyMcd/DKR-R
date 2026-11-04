void DrawPartyPage(float width,const OnlineFrame& frame) {
    namespace party=dkr::runtime::spectators;
    auto& online=dkr::runtime::netplay::session();const auto v=party::party_link_view(&online);
    const auto me=std::find_if(v.party.members.begin(),v.party.members.end(),[&](const auto& m){return m.id==v.local;});
    const bool watcher=me!=v.party.members.end()&&me->role==party::Role::Spectator;
    const bool live=v.running||frame.view.state==dkr::runtime::netplay::ConnectionState::Running;
    const auto consent_key=frame.view.match_id^v.party.host;
    if(g_online_development_room!=consent_key){g_online_development_room=consent_key;g_online_development_consent=false;}
    DrawPageHeading("ONLINE");PaddockGap(12.F);
    DrawOlFeature(width,live?"Race in progress":"Your lobby",v.status.c_str(),[&](float inner) {
        const auto code=g_online_hide_lobby_code?std::string("*****"):NormalizeOnlineInvite(frame.view.invite).substr(0,5);
        OlParagraph("Lobby code: "+code,20.F,kOlCream,inner,true);
        if(OlButton(g_online_hide_lobby_code?"Show code":"Hide code")){g_online_hide_lobby_code=!g_online_hide_lobby_code;SaveSettings();}
        PaddockGap(8.F);if(OlButton("Copy code"))SDL_SetClipboardText(frame.view.invite.c_str());
        if(live) {PaddockGap(8.F);OlParagraph(watcher?"You are watching. No racing controller is assigned to you.":"You are racing. Open this panel to arrange a player swap.",15.F,kOlSoft,inner);}
    });PaddockGap(16.F);
    DrawOlFeature(width,"Racers","Up to four racing places. Spectators do not take one.",[&](float inner) {
        for(unsigned p=0;p<4;++p) {
            const auto id=v.party.controls[p].member;
            const auto racer=std::find_if(v.party.members.begin(),v.party.members.end(),[&](const auto& m){return m.id==id;});
            if(racer==v.party.members.end())continue;
            const bool ready=v.ready.contains(id)&&v.ready.at(id);
            const auto label="P"+std::to_string(p+1)+"  "+racer->name+(id==v.local?" (you)":"")+(racer->host?" (host)":"")+(live?"":ready?" - Ready":" - Not ready");
            OlParagraph(label,17.F,kOlCream,inner,true);PaddockGap(6.F);
            ImGui::PushID(int(p));
            if(live&&watcher&&OlButton("Request this racing place"))party::party_request_swap(&online,p,v.local);
            if(!live&&v.host&&OlButton("Make spectator")){std::string e;if(!party::party_waiting_role(&online,id,false,e))OlNotify(e);}
            ImGui::PopID();PaddockGap(8.F);
        }
        if(!live&&!v.stopping) {
            if(frame.view.room.rules.development)ImGui::Checkbox("I agree to development commands",&g_online_development_consent);
            if(!watcher) {
                const bool ready=v.ready.contains(v.local)&&v.ready.at(v.local);
                const OlDisabled disabled(frame.view.room.rules.development&&!g_online_development_consent);
                if(OlButton(ready?"Not ready":"Ready to race",ready?OlTone::Plain:OlTone::Go))party::party_ready(&online,!ready);
            }
            if(v.host) {PaddockGap(8.F);if(OlButton("Start race",OlTone::Go))party::party_start(&online);}
        }
    });PaddockGap(16.F);
    DrawOlFeature(width,"Spectators","Watch without taking a controller port.",[&](float inner) {
        unsigned count=0;
        for(const auto& m:v.party.members)if(m.role==party::Role::Spectator) {
            ++count;ImGui::PushID(std::to_string(m.id).c_str());
            OlParagraph(m.name+(m.id==v.local?" (you)":"")+(m.host?" (host)":""),17.F,kOlCream,inner,true);
            if(live&&!watcher&&me!=v.party.members.end()&&me->port&&OlButton("Offer my place to this spectator"))party::party_request_swap(&online,*me->port,m.id);
            if(!live&&v.host&&OlButton("Make racer")){std::string e;if(!party::party_waiting_role(&online,m.id,true,e))OlNotify(e);}
            ImGui::PopID();PaddockGap(8.F);
        }
        if(!count)OlParagraph("No spectators yet. Share the code and ask friends to choose Watch Race.",15.F,kOlSoft,inner);
        if(watcher) {
            static const std::vector<std::string> modes{"All splits","Follow player","T.T.-style director"};
            int mode=int(v.camera);if(OlSelect("watch-camera","Camera",&mode,modes,inner))party::party_camera(&online,party::WatchCamera(mode),v.target);
            if(mode==int(party::WatchCamera::Follow)) {
                std::vector<std::string> targets;
                for(unsigned p=0;p<4;++p)if(v.party.controls[p].member) {
                    const auto m=std::find_if(v.party.members.begin(),v.party.members.end(),[&](const auto& member){return member.id==v.party.controls[p].member;});
                    targets.push_back("P"+std::to_string(p+1)+(m==v.party.members.end()?"":" - "+m->name));
                }
                int target=std::min(int(v.target),std::max(0,int(targets.size())-1));
                if(!targets.empty()&&OlSelect("watch-player","Player",&target,targets,inner))party::party_camera(&online,v.camera,unsigned(target));
            }
            PaddockGap(8.F);OlParagraph("D-pad left/right: player. C-up: change camera. F1 / launcher shortcut: lobby controls.",14.F,kOlSoft,inner);
        }
    });PaddockGap(16.F);
    DrawOlJoiningProgress(width,frame);
    DrawOlLiveDelay(width);
    if(v.host&&!frame.view.pending_joins.empty()) {
        OnlineFrame gate=frame;gate.busy=false;gate.has_space=v.party.members.size()<party::MaximumMembers;DrawOlGate(width,gate);PaddockGap(16.F);
    }
    if(v.host&&live&&!v.swaps.empty()) {
        DrawOlFeature(width,"Player swaps","The replacement keeps the same racer, vehicle and race progress.",[&](float inner) {
            for(const auto& [port,incoming]:v.swaps) {
                const auto m=std::find_if(v.party.members.begin(),v.party.members.end(),[&](const auto& member){return member.id==incoming;});
                if(m==v.party.members.end())continue;ImGui::PushID(std::to_string(incoming).c_str());ImGui::PushID(int(port));
                OlParagraph(m->name+" to take P"+std::to_string(port+1),16.F,kOlCream,inner,true);
                if(OlButton("Approve swap",OlTone::Go))party::party_approve_swap(&online,port,incoming);
                ImGui::PopID();ImGui::PopID();PaddockGap(8.F);
            }
            if(OlButton("Cancel pending swap"))party::party_cancel_swap(&online);
        });PaddockGap(16.F);
    }
    if(v.host&&live&&OlButton("Return everyone to the lobby"))party::party_stop(&online,"The host returned to the lobby.");
    PaddockGap(8.F);
    if(OlButton(v.host?"Close lobby":"Leave lobby"))online.disconnect(v.host?"The host closed the lobby.":"Left the lobby.");
}
