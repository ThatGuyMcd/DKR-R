// Connection panel only. The accepted lobby cards, code plate, invitations,
// moderation and spectator/role controls remain composed by the prior layer.
void OlRequestConnectionTest(){
    std::string error;
    if(!dkr::runtime::netplay::session().request_connection_test(error))OlNotify(error);
    else OlNotify("Checking Rollback input timing. Please wait about 10 seconds.");
}
bool OlCanTestConnection(const OnlineFrame& frame){
    using dkr::runtime::netplay::ConnectionState;
    return frame.view.host&&!frame.busy&&!OlPartyLive()&&
        (frame.view.state==ConnectionState::Hosting||frame.view.state==ConnectionState::Lobby);
}
bool OlHasTestResults(const OnlineFrame&){return g_ol_rollback_check.complete;}
std::string OlTestCountdown(const OnlineFrame& frame){return std::to_string((frame.view.connection_test_remaining_ms+999U)/1000U)+" seconds";}
std::string OlRollbackMs(double value){char text[32];std::snprintf(text,sizeof(text),"%.1f ms",value);return text;}
void DrawOlRollbackResults(const OnlineFrame&){
    const auto& v=g_ol_rollback_check;
    const float outer_width=std::max(1.F,ImGui::GetContentRegionAvail().x);
    // A report may already be open when a member leaves or a launch begins.
    // Do not keep displaying the old assessment after its context is retired.
    if(!v.complete){
        OlParagraph(v.status.empty()?"This report is no longer current. Run a fresh Rollback check from the lobby.":v.status,15.F,kOlAmber,outer_width);
        PaddockGap(14.F);if(DrawOlDialogActions({{"CLOSE"}})==0)ImGui::CloseCurrentPopup();return;
    }
    OlParagraph("ROLLBACK CONNECTION REPORT",19.F,kOlCream,outer_width,true);PaddockGap(10.F);
    // One scrolling body; the close/controller action stays outside it. The
    // parent modal is capped at 85% of the display even on a small handheld.
    const float height=std::max(70.F,ImGui::GetIO().DisplaySize.y*.85F-ImGui::GetCursorPosY()-92.F);
    ImGui::BeginChild("##rollback-check-report",{outer_width,height},false);
    const float width=std::max(1.F,ImGui::GetContentRegionAvail().x);
    OlParagraph("30 Hz synthetic input probes on this lobby's Rollback channels. Two seconds to negotiate, six to sample, two to drain. This is a network preflight, not a gameplay, FPS or desync guarantee.",14.F,kOlSoft,width);
    OlParagraph(v.party?"Racers: reliable Control input channel. Spectators: Replica playback channel (20 Hz); checkpoint channel readiness only.":
        "Racers: Realtime inputs (30 Hz) plus a separate Authoritative repair-channel check (20 Hz). No legacy state-replication score.",14.F,kOlSoft,width);
    PaddockGap(10.F);
    const double budget=(v.delay+(v.window>2?v.window-2:0))*1000./30.;
    OlParagraph("Settings at start: "+std::to_string(v.delay)+" delay frames"+(v.automatic?" (automatic)":" (manual)")+", "+std::to_string(v.window)+" prediction frames. Conservative arrival budget: "+OlRollbackMs(budget)+" (two prediction frames reserved).",14.F,kOlCream,width);
    double worst=0;bool racers=false,uncertain=false,pressure=false;
    for(const auto& row:v.rows){
        PaddockGap(14.F);const float w=std::max(1.F,ImGui::GetContentRegionAvail().x);
        OlParagraph(row.name+(row.spectator?" - SPECTATOR":" - RACER"),16.F,kOlCream,w,true);
        if(!row.supported){OlParagraph("Inconclusive: no compatible diagnostic acknowledgement. Update this participant's build and check again; this is not a bad-connection verdict.",14.F,kOlAmber,w);if(!row.spectator)uncertain=true;continue;}
        const bool enough=row.replies>=90;
        if(row.replies){
            OlParagraph("Replies "+std::to_string(row.replies)+" / "+std::to_string(row.sent)+" | Median "+OlRollbackMs(row.median)+" | p95 RTT "+OlRollbackMs(row.p95)+" | RTT variation "+OlRollbackMs(row.jitter),14.F,kOlInk,w);
            const unsigned missing=row.sent-row.replies;
            OlParagraph(std::to_string(missing)+" missing probe replies after drain. These are round-trip probe outcomes, not measured gameplay packet loss or one-way latency.",13.F,kOlSoft,w);
        }else OlParagraph("No completed input probes. Check the channel/connection and try again.",14.F,kOlAmber,w);
        if(row.repair_sent)OlParagraph("Repair channel: "+std::to_string(row.repair_replies)+" / "+std::to_string(row.repair_sent)+" replies; p95 "+OlRollbackMs(row.repair_p95)+".",13.F,kOlSoft,w);
        if(row.blocked||row.errors)OlParagraph("Probe sends deferred/failed: "+std::to_string(row.blocked+row.errors)+". No synthetic backlog was retained.",13.F,kOlAmber,w);
        if(!row.spectator){
            racers=true;worst=std::max(worst,row.p95);uncertain=uncertain||!enough;
            pressure=pressure||row.replies<row.sent||row.blocked||row.errors||row.repair_replies<row.repair_sent;
            if(enough){const double headroom=budget-(row.p95+8.);
                OlParagraph(std::string(headroom>=0?"Timing headroom: ":"Outside conservative timing budget by ")+OlRollbackMs(std::abs(headroom))+
                    (headroom>=0?". Leave room for spikes.":". More input delay may help."),14.F,headroom>=0?kOlGo:kOlAmber,w);}
        }else OlParagraph("Playback timing only. Spectators do not consume a controller port or reduce the racer assessment. Checkpoint transfer speed is not benchmarked.",13.F,kOlSoft,w);
    }
    if(v.rows.empty())OlParagraph("No completed report is available for this lobby.",14.F,kOlSoft,width);
    PaddockGap(12.F);
    if(racers&&!uncertain){
        const unsigned delay=dkr::runtime::netplay::experimental::automatic_delay(worst,v.window);
        OlParagraph("Suggested delay for the measured racer routes: "+std::to_string(delay)+" frame(s), using Rollback's existing automatic-delay policy. Nothing has been changed automatically.",14.F,kOlCream,width);
        if(worst+8.>(9+(v.window>2?v.window-2:0))*1000./30.)OlParagraph("Even maximum input delay may not cover these spikes. Check the connection or increase the prediction window with care.",14.F,kOlAmber,width);
    }else OlParagraph("Racer timing assessment is inconclusive: insufficient replies or unsupported clients. No score has been guessed.",14.F,kOlAmber,width);
    OlParagraph("Transport queues: peak "+std::to_string(v.peak_queued/1024)+" KiB, final "+std::to_string(v.final_queued/1024)+" KiB. Late local probe schedules: "+std::to_string(v.scheduler_gaps)+".",13.F,kOlSoft,width);
    if(pressure||v.scheduler_gaps||v.final_queued>4096)OlParagraph("Timing/queue pressure was observed. Local scheduling and other lobby transfers can affect this check; retry when the connection is quiet.",13.F,kOlAmber,width);
    ImGui::EndChild();PaddockGap(12.F);if(DrawOlDialogActions({{"CLOSE"}})==0)ImGui::CloseCurrentPopup();
}
void DrawOlConnection(float width,const OnlineFrame& frame){
    using namespace dkr::runtime::netplay;
    DrawOlFeature(width,"Rollback connection","",[&](float inner){
        PaddockGap(12.F);OlParagraph(OlRulesLine(frame.view.room.rules),16.F,kOlCream,inner,true);
        OlParagraph("Checks the active Rollback transport at the game's 30 Hz input cadence. It does not run a race or change your settings.",14.F,kOlSoft,inner);
        if(!g_ol_rollback_check.status.empty()){PaddockGap(12.F);OlParagraph(g_ol_rollback_check.status,15.F,kOlAmber,inner);}
        if(OlPartyActive()){
            const auto routes=dkr::relay::view(&dkr::runtime::netplay::session());
            for(const auto& m:g_ol_party.link.party.members){
                const auto timing=dkr::relay::lobby_card_ping(routes,std::to_string(m.id),m.host,m.id==g_ol_party.link.local,
                    m.port.value_or(255),g_ol_party.link.party.generation).text;
                PaddockGap(8.F);OlParagraph(m.name+(m.role==lobby_party::Role::Spectator?" (spectator)":" (racer)")+" - "+timing,14.F,kOlSoft,inner);
            }
        }else if(frame.view.network_rtt_ms)OlParagraph("Lobby RTT: "+std::to_string(frame.view.network_rtt_ms)+" ms. Run the Rollback check for input-channel timings.",14.F,kOlSoft,inner);
        const auto live=experimental::runtime_view();
        if(live.active){
            PaddockGap(12.F);OlParagraph("Live Rollback: frame "+std::to_string(live.frame)+", confirmed "+std::to_string(live.confirmed)+
                ", corrections "+std::to_string(live.corrections)+", replayed "+std::to_string(live.replayed)+".",14.F,kOlInk,inner);
            if(!live.status.empty())OlParagraph(live.status,14.F,kOlSoft,inner);
        }else if(frame.view.state==ConnectionState::Running)OlParagraph("The race is running. Preflight checks are disabled until everyone returns to the lobby.",14.F,kOlSoft,inner);
        if(frame.view.host){PaddockGap(14.F);const OlDisabled disabled(!OlCanTestConnection(frame));if(OlButton("Check Rollback connection",OlTone::Plain))OlRequestConnectionTest();}
        else OlParagraph("The host can run a shared Rollback check while everyone is in the lobby. All participants need the updated build.",14.F,kOlSoft,inner);
        if(g_ol_rollback_check.active){PaddockGap(10.F);OlParagraph("Checking... "+OlTestCountdown(frame),15.F,kOlAmber,inner);}
        if(g_ol_rollback_check.complete){PaddockGap(12.F);if(OlButton("View Rollback report"))g_online_page.open_results=true;}
    });
}
void DrawOlPartyConnection(float width,const OnlineFrame& frame){DrawOlConnection(width,frame);}
void DrawOlPartyResults(const OnlineFrame& frame){DrawOlRollbackResults(frame);}
