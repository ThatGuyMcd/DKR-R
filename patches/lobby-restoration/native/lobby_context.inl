namespace lobby_ui=dkr::runtime::lobby_ui;
namespace lobby_party=dkr::runtime::spectators;
lobby_ui::Snapshot g_ol_party;
bool OlPartyActive(){return g_ol_party.link.enabled&&g_ol_party.link.admitted;}
bool OlPartyWatching(){
    for(const auto& m:g_ol_party.link.party.members)if(m.id==g_ol_party.link.local)return m.role==lobby_party::Role::Spectator;
    return false;
}
bool OlPartyLive(){return OlPartyActive()&&(g_ol_party.launching||g_ol_party.link.running);}
unsigned OlPartySpectators(){return unsigned(std::count_if(g_ol_party.link.party.members.begin(),g_ol_party.link.party.members.end(),[](const auto& m){return m.role==lobby_party::Role::Spectator;}));}
const lobby_party::Member* OlPartyMember(lobby_party::MemberId id){
    for(const auto& m:g_ol_party.link.party.members)if(m.id==id)return &m;return nullptr;
}
std::string OlPartyHostName(){const auto* m=OlPartyMember(g_ol_party.link.party.host);return m?m->name:"Host";}
struct OlPartyProbe {
    struct Row {std::string name;unsigned index=0;std::vector<std::uint16_t> samples;};
    std::uint64_t room=0,generation=0;bool active=false,complete=false;
    unsigned revision=0;double started=0,last_sample=0;
    std::map<lobby_party::MemberId,Row> rows;
} g_ol_party_probe;
bool g_ol_member_open=false,g_ol_manage_open=false,g_ol_return_open=false,g_ol_remove_open=false;
lobby_party::MemberId g_ol_member_id=0;
std::uint64_t g_ol_member_room=0,g_ol_member_generation=0;
void OlPartyOpenMember(lobby_party::MemberId id){
    g_ol_member_id=id;g_ol_member_room=g_ol_party.room;g_ol_member_generation=g_ol_party.link.party.generation;g_ol_member_open=true;
}
void OlPartyBeginProbe(){
    auto& test=g_ol_party_probe;test.rows.clear();test.room=g_ol_party.room;test.generation=g_ol_party.link.party.generation;
    test.active=true;test.complete=false;test.started=test.last_sample=PaddockClock();
    for(const auto& metric:g_ol_party.metrics)if(const auto* m=OlPartyMember(metric.member))test.rows[m->id]={m->name,metric.index,{}};
    OlNotify("Sampling the lobby's authenticated heartbeat probes for 7 seconds.");
}
void OlPartyProject(dkr::runtime::netplay::SessionView& view) {
    using namespace dkr::runtime::netplay;
    const auto previous_room=g_ol_party.room;
    g_ol_party=lobby_ui::snapshot(&session());
    if(previous_room!=g_ol_party.room){g_ol_member_open=g_ol_manage_open=g_ol_return_open=g_ol_remove_open=false;}
    auto& test=g_ol_party_probe;
    if(!OlPartyActive()||test.room!=g_ol_party.room){test.active=test.complete=false;test.rows.clear();}
    if(!OlPartyActive())return;
    if(test.active&&test.generation!=g_ol_party.link.party.generation){test.active=test.complete=false;test.rows.clear();OlNotify("Lobby membership changed. Run the connection check again.");}
    if(test.active&&PaddockClock()-test.last_sample>2.){test.active=test.complete=false;test.rows.clear();OlNotify("The connection check was interrupted. Keep the Online page open and try again.");}
    if(test.active){
        test.last_sample=PaddockClock();
        for(const auto& metric:g_ol_party.metrics)if(auto it=test.rows.find(metric.member);it!=test.rows.end()){
            auto& row=it->second;const unsigned delta=std::min<unsigned>(metric.index-row.index,std::min<unsigned>(metric.count,16));
            for(unsigned i=0;i<delta&&row.samples.size()<32;++i)row.samples.push_back(metric.rtts[(metric.index-delta+i)%16]);
            row.index=metric.index;
        }
        if(PaddockClock()-test.started>=7.){test.active=false;test.complete=true;++test.revision;if(!test.revision)++test.revision;}
    }
    view.room.players={};view.online_save_ready={};view.local_slot=255;
    view.local_online_save_ready=g_ol_party.save_ready;
    for(unsigned port=0;port<4;++port){
        const auto* m=OlPartyMember(g_ol_party.link.party.controls[port].member);if(!m||m->role!=lobby_party::Role::Racer)continue;
        auto& player=view.room.players[port];player.occupied=player.compatible=m->connected;player.host=m->host;
        player.slot=std::uint8_t(port);player.peer_id=std::to_string(m->id);player.display_name=m->name;
        const auto ready=g_ol_party.link.ready.find(m->id);player.ready=ready!=g_ol_party.link.ready.end()&&ready->second;
        player.loaded=g_ol_party.link.running;view.online_save_ready[port]=g_ol_party.save_ready;
        if(m->id==g_ol_party.link.local)view.local_slot=std::uint8_t(port);
        for(const auto& metric:g_ol_party.metrics)if(metric.member==m->id&&metric.count){player.ping_ms=metric.rtts[(metric.index-1)%16];}
        if(!g_ol_party.link.host&&m->host)player.ping_ms=g_ol_party.host_rtt;
    }
    // A host may be watching; host identity does not imply ownership of P1.
    view.host=g_ol_party.link.host;
    if(g_ol_party.link.stopping)view.state=view.host?ConnectionState::Hosting:ConnectionState::Lobby;
    else if(g_ol_party.link.running)view.state=ConnectionState::Running;
    else if(g_ol_party.launching)view.state=ConnectionState::Loading;
    view.connection_test_active=test.active;
    view.connection_test_remaining_ms=test.active?std::uint32_t(std::max(0.,7000.-(PaddockClock()-test.started)*1000.)):0;
    view.connection_test_results={};
    view.connection_test_result_generation=test.complete?(0x80000000U|test.revision):0;
}
bool OlPartyHasSpace(const OnlineFrame& frame){return OlPartyActive()&&frame.view.host&&!frame.view.lobby_locked&&!g_ol_party.link.stopping&&!g_ol_party_probe.active&&g_ol_party.link.party.members.size()<lobby_party::MaximumMembers;}
bool OlPartyJoinSpace(const OnlineFrame& frame,const dkr::runtime::netplay::PendingJoinView& pending){
    if(!OlPartyActive())return frame.has_space;
    if(!OlPartyHasSpace(frame))return false;
    const auto role=g_ol_party.pending_watch.find(pending.request_id);if(role==g_ol_party.pending_watch.end())return false;
    if(role->second)return OlPartySpectators()<lobby_party::MaximumSpectators;
    if(OlPartyLive())return false;
    unsigned racers=0;for(const auto& m:g_ol_party.link.party.members)if(m.role==lobby_party::Role::Racer)++racers;
    return racers<std::clamp<unsigned>(frame.view.room.rules.maximum_players,2,4);
}
void DrawOlPartySpectators(float width,const OnlineFrame&);
void DrawOlPartyStart(float width,const OnlineFrame&);
void DrawOlPartyConnection(float width,const OnlineFrame&);
void DrawOlPartyResults(const OnlineFrame&);
void DrawOlPartyDialogs(const OnlineFrame&);
bool OlPartyChanging(const OnlineFrame&);
void DrawOlLobbyToolbar(float,const OnlineFrame&);
