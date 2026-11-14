// Diagnostic presentation only. One body scrolls; the controller actions stay
// outside it. This does not alter any other lobby dialog or launcher section.
struct OlScoreboardState {std::uint64_t room=0,expanded=0,focus=0;unsigned revision=0;};
OlScoreboardState g_ol_scoreboard;
unsigned OlScoreboardColour(const dkr::runtime::rollback_check::Assessment& a){return !a.rated()?kOlSoft:a.score>=8?kOlGo:a.score>=5?kOlAmber:kOlError;}
bool BeginOlScoreboard(const char* name){
    const ImVec2 display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*.5F,display.y*.5F},ImGuiCond_Appearing,{.5F,.5F});
    ImGui::SetNextWindowSize({std::max(1.F,std::min(940.F,display.x-32.F)),std::max(140.F,std::min(800.F,display.y*.85F))},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,display.x<600?ImVec2{16,16}:ImVec2{24,20});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,18.F);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1.F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,PaddockRgb(0x091D29));ImGui::PushStyleColor(ImGuiCol_Border,PaddockRgb(0x32657A));
    const bool visible=ImGui::BeginPopupModal(name,nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);ImGui::PopStyleVar(3);if(visible)g_paddock_modal_frame=ImGui::GetFrameCount();return visible;
}
void OlScoreboardDetails(const dkr::runtime::rollback_check::View& v,const dkr::runtime::rollback_check::Row& row,float width){
    namespace check=dkr::runtime::rollback_check;const auto a=check::assessment(v,row);
    PaddockGap(8.F);OlParagraph(std::string(a.label)+" - "+a.reason,14.F,OlScoreboardColour(a),width);
    if(row.host){OlParagraph("Shared race estimate: the weakest measured racer route, not a self-ping, host upload benchmark or PC performance score.",13.F,kOlSoft,width);return;}
    OlParagraph("Measured round trip to the host. Not one-way latency or gameplay packet loss.",13.F,kOlSoft,width);
    const auto metric=[&](const char* label,const std::string& value){
        if(ImGui::BeginTable(label,2,ImGuiTableFlags_SizingStretchProp,{width,0})){
            ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthStretch,2.F);ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,1.F);
            ImGui::TableNextColumn();OlParagraph(label,13.F,kOlSoft,std::max(1.F,ImGui::GetContentRegionAvail().x));
            ImGui::TableNextColumn();OlParagraph(value,13.F,kOlCream,std::max(1.F,ImGui::GetContentRegionAvail().x));ImGui::EndTable();
        }
    };
    metric("Typical round trip",row.replies?OlRollbackMs(row.median):"Not measured");
    metric("Slower round trips (p95)",row.replies?OlRollbackMs(row.p95):"Not measured");
    metric("Timing variation",row.replies?OlRollbackMs(row.jitter):"Not measured");
    metric("Probe replies",std::to_string(row.replies)+" / "+std::to_string(row.sent));
    if(row.repair_sent)metric("Repair-channel replies",std::to_string(row.repair_replies)+" / "+std::to_string(row.repair_sent)+"; p95 "+OlRollbackMs(row.repair_p95));
    if(row.sent>=row.replies)metric("Unanswered probes",std::to_string(row.sent-row.replies)+" after the drain period");
    if(row.blocked||row.errors)metric("Deferred / failed sends",std::to_string(row.blocked+row.errors));
    if(!row.spectator&&row.replies){
        const double headroom=check::arrival_budget(v)-(std::max(row.p95,row.repair_p95)+8.);
        metric("Conservative timing margin",(headroom<0?"-":"+")+OlRollbackMs(std::abs(headroom)));
    }
    if(row.spectator)OlParagraph("Playback timing only. No controller port is used; checkpoint download and catch-up speed are not benchmarked.",13.F,kOlSoft,width);
    OlParagraph("Policy v1: arrival timing 35%, probe reliability 35%, consistency 20%, latency 10%. Serious weaknesses cap the grade; incomplete or locally overloaded samples are not rated.",13.F,kOlSoft,width);
}
void DrawOlRollbackResults(const OnlineFrame& frame){
    namespace check=dkr::runtime::rollback_check;const auto& v=g_ol_rollback_check;auto& state=g_ol_scoreboard;
    if(state.room!=v.room||state.revision!=v.revision){state={v.room,0,0,v.revision};}
    const bool appearing=ImGui::IsWindowAppearing();if(appearing)state.expanded=state.focus=0;
    if(!v.complete)state.expanded=state.focus=0;
    const bool back=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)||ImGui::IsKeyPressed(ImGuiKey_Escape,false));
    const bool had_details=state.expanded!=0;
    if(back&&had_details){state.focus=state.expanded;state.expanded=0;}
    const float width=std::max(1.F,ImGui::GetContentRegionAvail().x);
    OlParagraph("ROLLBACK CONNECTION CHECK",21.F,kOlAmber,width,true);
    OlParagraph(v.complete?"Complete - host-measured results for this lobby":v.active?"Checking connections - scores appear when the test finishes":"Report no longer current",13.F,kOlSoft,width);
    PaddockGap(10.F);
    const bool stacked=width<400.F;
    const float footer=stacked?140.F:92.F;
    const float body_height=std::max(24.F,ImGui::GetContentRegionAvail().y-footer);
    ImGui::BeginChild("##rollback-scoreboard-body",{width,body_height},false);
    const float inner=std::max(1.F,ImGui::GetContentRegionAvail().x);
    if(!v.complete){
        OlParagraph(v.status.empty()?"This report is no longer current. Run a fresh Rollback check from the lobby.":v.status,15.F,kOlAmber,inner);
        if(v.active)OlParagraph("Checking... "+OlTestCountdown(frame)+". The host is measuring racer and spectator timing separately.",14.F,kOlSoft,inner);
    }else{
        const auto overall=check::race_assessment(v);
        const std::string score=overall.rated()?std::to_string(overall.score)+" / 10":"NOT RATED";
        const bool summary_columns=inner>=550.F&&ImGui::BeginTable("##scoreboard-summary",2,ImGuiTableFlags_SizingStretchProp,{inner,0});
        if(summary_columns){ImGui::TableSetupColumn("Score",ImGuiTableColumnFlags_WidthFixed,145.F);ImGui::TableSetupColumn("Experience",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextColumn();}
        OlParagraph(score,32.F,OlScoreboardColour(overall),std::max(1.F,ImGui::GetContentRegionAvail().x),true);
        if(summary_columns)ImGui::TableNextColumn();
        const float summary_width=std::max(1.F,ImGui::GetContentRegionAvail().x);
        OlParagraph(std::string(overall.label)+(overall.rated()?" racing expected":""),18.F,kOlCream,summary_width,true);
        OlParagraph(overall.reason,14.F,kOlSoft,summary_width);
        if(summary_columns)ImGui::EndTable();
        PaddockGap(9.F);
        OlParagraph("Rollback | Input delay: "+std::to_string(v.delay)+" frames "+(v.automatic?"(automatic at test start)":"(manual)")+" | Prediction: "+std::to_string(v.window)+" frames",13.F,kOlInk,inner);
        OlParagraph("Configured input delay: about "+std::to_string(unsigned(std::round(v.delay*1000./30.)))+" ms at 30 Hz. More delay can cover lateness, but makes controls less immediate.",13.F,kOlSoft,inner);
        bool first=true;
        for(bool spectators:{false,true}){
            PaddockGap(15.F);OlParagraph(spectators?"SPECTATORS - PLAYBACK TIMING":"RACERS - CONNECTION TO HOST",15.F,kOlCream,inner,true);
            unsigned count=0;
            for(const auto& row:v.rows){
                if(row.spectator!=spectators)continue;++count;const auto a=check::assessment(v,row);
                const float score_width=std::min(116.F,inner*.34F),text_width=std::max(1.F,inner-score_width-34.F);
                const std::string name=row.name+(row.id==v.local?"  (YOU)":"");
                const std::string role=row.host?"HOST - SHARED RACE ESTIMATE":row.spectator?"SPECTATOR":"RACER";
                const std::string brief=row.host?"Based on measured racer routes, not a self-ping.":std::string(a.label)+" - "+a.reason;
                const float name_h=OlParagraphHeight(name,16.F,text_width,true,1.25F);
                const float role_h=OlParagraphHeight(role,12.F,text_width,false,1.25F);
                const float reason_h=OlParagraphHeight(brief,13.F,text_width,false,1.3F);
                const float height=std::max(76.F,name_h+role_h+reason_h+27.F);
                ImGui::PushID(std::to_string(row.id).c_str());
                if(state.focus==row.id){ImGui::SetKeyboardFocusHere();state.focus=0;}
                ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
                const bool pressed=ImGui::Selectable("##participant-details",state.expanded==row.id,ImGuiSelectableFlags_None,{inner,height});
                ImGui::PopStyleColor(3);const bool focused=ImGui::IsItemFocused(),hovered=ImGui::IsItemHovered();
                if(first&&appearing)ImGui::SetItemDefaultFocus();first=false;
                if(pressed)state.expanded=state.expanded==row.id?0:row.id;
                const auto at=ImGui::GetItemRectMin(),end=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
                PaddockFill(draw,at,end,PaddockRound(10.F),PaddockCol(hovered||state.expanded==row.id?0x153E50:0x0B2B3E));
                float y=at.y+10.F;
                y+=OlParagraphAt(draw,{at.x+12.F,y},name,16.F,kOlCream,text_width,true,1.25F);
                y+=OlParagraphAt(draw,{at.x+12.F,y+2.F},role,12.F,kOlSoft,text_width,false,1.25F)+3.F;
                OlParagraphAt(draw,{at.x+12.F,y+2.F},brief,13.F,kOlSoft,text_width,false,1.3F);
                const float sx=end.x-score_width-12.F;
                const std::string grade=a.rated()?std::to_string(a.score)+" / 10":"-- / 10";
                const float grade_px=PaddockMeasure(OlRead(23.F,true,1.15F),grade)>score_width?19.F:23.F;
                OlParagraphAt(draw,{sx,at.y+12.F},grade,grade_px,OlScoreboardColour(a),score_width,true,1.15F,true);
                const float segment=(score_width-27.F)/10.F;
                for(unsigned i=0;i<10;++i)draw->AddRectFilled({sx+i*(segment+3.F),at.y+44.F},{sx+i*(segment+3.F)+segment,at.y+49.F},PaddockCol(a.rated()&&i<a.score?OlScoreboardColour(a):0x294A59),2.F);
                OlParagraphAt(draw,{sx,at.y+56.F},state.expanded==row.id?"Hide details":"Details",12.F,kOlSoft,score_width,false,1.25F,true);
                if(focused)PaddockFocusRing(draw,at,end,10.F);
                if(state.expanded==row.id){PaddockGap(6.F);OlScoreboardDetails(v,row,inner);PaddockGap(8.F);}
                ImGui::PopID();PaddockGap(5.F);
            }
            if(!count)OlParagraph(spectators?"No spectators connected.":"No racers connected.",13.F,kOlSoft,inner);
        }
        PaddockGap(10.F);
        OlParagraph("A short network estimate, not an FPS or desync guarantee. The weakest rated racer determines the racing score. Spectators are assessed separately.",13.F,kOlSoft,inner);
        bool rated=true,remote=false;double worst=0;
        for(const auto& r:v.rows)if(!r.host&&!r.spectator){remote=true;rated=rated&&check::remote_assessment(v,r).rated();worst=std::max(worst,std::max(r.p95,r.repair_p95));}
        if(remote&&rated){
            const unsigned suggested=dkr::runtime::netplay::experimental::automatic_delay(worst,v.window);
            OlParagraph("Existing automatic-delay policy suggests "+std::to_string(suggested)+" frame(s) for these sampled racer routes. This is a starting point, not a guarantee. Nothing has been changed automatically.",13.F,kOlInk,inner);
        }
        OlParagraph("Test conditions: queue peak "+std::to_string(v.peak_queued/1024)+" KiB, final "+std::to_string(v.final_queued/1024)+" KiB; late local probe schedules "+std::to_string(v.scheduler_gaps)+". Policy v1.",13.F,kOlSoft,inner);
        if(v.scheduler_gaps||v.final_queued>4096)OlParagraph("Local scheduling or queue pressure was observed. Retry when the lobby and connection are quiet.",13.F,kOlAmber,inner);
    }
    ImGui::EndChild();PaddockGap(8.F);
    const bool can_test=OlCanTestConnection(frame)&&!v.active;
    if(frame.view.host){const OlDisabled disabled(!can_test);if(OlButton(v.active?"Checking...":"Re-run test",OlTone::Plain,std::min(width,OlButtonWidth("Re-run test"))))OlRequestConnectionTest();}
    else OlParagraph("The host can re-run this shared check.",13.F,kOlSoft,stacked?width:std::max(1.F,width-OlButtonWidth("Close")-12.F));
    if(!stacked)ImGui::SameLine();
    if(OlButton("Close",OlTone::Strong,std::min(width,OlButtonWidth("Close")))||(back&&!had_details))ImGui::CloseCurrentPopup();
    OlParagraph("D-pad: navigate   A: details   B: back / close",12.F,kOlSoft,width);
}
