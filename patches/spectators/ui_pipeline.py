"""Use the launcher's established online controls for party/spectator UI."""
from pathlib import Path
import re
AUTH=Path(__file__).resolve().parent
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Party UI anchor changed: '+a[:140])
 return t.replace(a,b,1)
def online(text):
 text=once(text,'void DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {',
  (AUTH/'native/party_ui.inl').read_text(encoding='utf-8')+'\nvoid DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {')
 anchor='    const OnlineFrame frame{view, launcher, rom_ready, active, joining, in_lobby, busy, has_space};'
 text=once(text,anchor,anchor+'''
    if(dkr::runtime::spectators::party_enabled(&online)&&in_lobby) {
        g_ol_page_width=available_width;
        std::vector<OlTab> party_tabs{{OlSection::Lobby,"Lobby"},{OlSection::Connection,"Connection"},{OlSection::Overlays,"Overlays"}};
        if(view.host)party_tabs.push_back({OlSection::Friends,"Invite friends"});
        if(std::none_of(party_tabs.begin(),party_tabs.end(),[&](const auto& tab){return tab.id==state.section;}))state.section=OlSection::Lobby;
        DrawOlTabs(party_tabs,std::max(available_width,1.F));PaddockGap(16.F);
        if(state.section==OlSection::Lobby)DrawPartyPage(std::max(available_width,1.F),frame);
        else DrawOlMain(std::max(available_width,1.F),frame);
        return;
    }
''')
 text=once(text,'        DrawOlOpenLobbies(width, frame);','''        static const std::vector<std::string> join_roles{"Racer","Watch Race"};
        int join_role=g_online_join_watching?1:0;
        if(OlSelect("browse-join-role","Join as",&join_role,join_roles,width))g_online_join_watching=join_role!=0;
        PaddockGap(12.F);
        DrawOlOpenLobbies(width, frame);''')
 anchor='''        bool changed = false;
        static const std::vector<std::string> ownership'''
 text=once(text,anchor,'''        bool changed = false;
        changed|=OlSwitchRow("##spectators","Spectator-capable lobby","Allow up to eight spectators. Requires Auto or Experimental Rollback.",&g_online_allow_spectators,inner,false);
        static const std::vector<std::string> ownership''')
 text=once(text,'           OlJoinActionsHeight(inner) + 10.0F + 52.0F + board.pad_bottom + 2.0F;',
  '           OlJoinActionsHeight(inner) + 82.0F + 10.0F + 52.0F + board.pad_bottom + 2.0F;')
 anchor='''    OlParagraph("Type the code your host sent you.", 15.0F, 0xD3E5EC, text);'''
 text=once(text,anchor,anchor+'''
    PaddockGap(8.F);
    static const std::vector<std::string> join_modes{"Racer","Watch Race"};
    int join_mode=g_online_join_watching?1:0;
    if(OlSelect("party-join-role","Join as",&join_mode,join_modes,inner))g_online_join_watching=join_mode!=0;
''')
 # All normal, friend and public-list join entry points share the same role.
 text=text.replace('if (!session().join(racer.lobby_code, g_online_player_name, error))',
  'ConfigurePartyJoin();if (!session().join(racer.lobby_code, g_online_player_name, error))')
 text=once(text,'                if (session().join_friend_invite(invite.lobby_code, g_online_player_name,',
  '                ConfigurePartyJoin();\n                if (session().join_friend_invite(invite.lobby_code, g_online_player_name,')
 text=once(text,'    if (!dkr::runtime::netplay::session().join(code, g_online_player_name, error)) {',
  '    ConfigurePartyJoin();\n    if (!dkr::runtime::netplay::session().join(code, g_online_player_name, error)) {')
 return text
def ui(text,origin,out):
 text='#include "party_link.hpp"\n#include "online_presentation/test_receive_delay.hpp"\n'+text
 text=once(text,'    auto& online=netplay::session();\n    if(!identity.supported()',
  '''    if(const char* missing=std::getenv("DKR_OWNED_CHECK_MISSING_TARGET");missing&&std::string_view(missing)=="1")
        dkr::online_presentation::test_fixture::missing_gpu_target=true;
    if(const char* delay=std::getenv("DKR_OWNED_CHECK_RECEIVE_MS")) {
        const unsigned value=unsigned(std::strtoul(delay,nullptr,10));
        dkr::online_presentation::test_receive_delay::arm(value,15);
        std::fprintf(stderr,"[online][isolated-check] receive-delay-ms=%u jitter-ms=15; test profile only\\n",value);
    }
    auto& online=netplay::session();
    if(!identity.supported()''')
 # This branch is reachable only by the existing new/empty-profile owned
 # check. Normal launcher preferences and the user's profile are untouched.
 text=once(text,'        ultramodern::renderer::set_graphics_config(config);\n    }\n    online.configure_manifest',
  '''        if(const char* focus=std::getenv("DKR_OWNED_CHECK_FULLSCREEN");focus&&std::string_view(focus)=="1") {
            config.ar_option=ultramodern::renderer::AspectRatio::Expand;
            dkr::runtime::online_view::requested_layout=dkr::runtime::online_view::Layout::MyRacer;
        }
        ultramodern::renderer::set_graphics_config(config);
    }
    online.configure_manifest''')
 # The isolated rendered check must advertise the same Racer capability as
 # the normal UI. Otherwise its client is rejected before any game executes.
 text=once(text,'    if(!host)return true;',
  '    if(!host){dkr::runtime::spectators::request_party_join(&online,false);return true;}')
 text=once(text,'    g_online_automatic_delay=false;g_online_manual_delay=1;g_online_rollback_window=6;',
  '''    g_online_automatic_delay=std::getenv("DKR_OWNED_CHECK_DYNAMIC")&&std::string_view(std::getenv("DKR_OWNED_CHECK_DYNAMIC"))=="1";
    g_online_allow_spectators=!(std::getenv("DKR_OWNED_CHECK_PARTY")&&std::string_view(std::getenv("DKR_OWNED_CHECK_PARTY"))=="0");
    g_online_manual_delay=1;g_online_rollback_window=6;''')
 text=once(text,'    request.rules.automatic_input_delay=false;request.rules.manual_input_delay=1;request.rules.rollback_window=6;',
  '    request.rules.automatic_input_delay=g_online_automatic_delay;request.rules.manual_input_delay=1;request.rules.rollback_window=6;')
 text=once(text,'struct OnlineHostRequest {','''bool g_online_allow_spectators=true,g_online_join_watching=false;
void ConfigurePartyJoin(){dkr::runtime::spectators::request_party_join(&dkr::runtime::netplay::session(),g_online_join_watching);}
struct OnlineHostRequest {
    bool spectators=g_online_allow_spectators;
''')
 text=once(text,'        output << "online_maximum_players=" << g_online_maximum_players << \'\\n\';',
  '        output << "online_allow_spectators=" << (g_online_allow_spectators ? 1 : 0) << \'\\n\';\n        output << "online_maximum_players=" << g_online_maximum_players << \'\\n\';')
 # The compatibility else-if chain already reaches MSVC's nesting limit.
 # Consume this independent setting before entering it.
 text=once(text,'            if (key == "online_experimental_rollback") {',
  '            if (key == "online_allow_spectators") {\n                g_online_allow_spectators = number != 0;\n                continue;\n            }\n            if (key == "online_experimental_rollback") {')
 text=once(text,'    const auto& rules=request.rules;', '''    const auto& rules=request.rules;
    if(request.spectators&&rules.synchronization!=SynchronizationMode::ExperimentalRollback) {
        g_online_action_status="Choose Auto or Experimental Rollback for a spectator-capable lobby, or turn spectator support off in Host settings.";
        return false;
    }''')
 text=once(text,'    if (!session().host(0U, {}, request.room,',
  '    dkr::runtime::spectators::request_party_host(&session(),request.spectators&&rules.synchronization==SynchronizationMode::ExperimentalRollback);\n    if (!session().host(0U, {}, request.room,')
 text=once(text,'                            const bool joined = g_joining_friend_invite',
  '                            ConfigurePartyJoin();\n                            const bool joined = g_joining_friend_invite') if '                            const bool joined = g_joining_friend_invite' in text else text
 # Resolve the inl against the actual final UI's include search/capsule, not a
 # historical runtime-recomp source with a different settings/menu contract.
 leaf='runtime_online_ui.inl'
 candidates=[origin.parent/leaf]
 rows=__import__('json').loads((out/'inputs.json').read_text(encoding='utf-8'))
 for row in rows:
  p=Path(row['path'])
  if 'roles/SDK2_BASE/' in p.as_posix():candidates.append(Path(p.as_posix().split('/native/')[0])/'native/src/game'/leaf)
 source=next((p for p in candidates if p.is_file()),None)
 if not source:raise ValueError('Cannot resolve accepted online UI include')
 dest=out/leaf;dest.write_text(online(source.read_text(encoding='utf-8')),encoding='utf-8')
 text=once(text,'#include "runtime_online_ui.inl"','#include "'+dest.as_posix()+'"')
 # Ensure friend-dialog joins use the role selected on the Quick Join card.
 needle='? online.join_friend_invite(invitation, g_online_player_name, g_joining_friend_invite->admission, sync_error)'
 if needle in text:
  pos=text.index(needle);line=text.rfind('\n',0,text.rfind('\n',0,pos))+1
  if 'ConfigurePartyJoin();' not in text[max(0,line-100):pos]:text=text[:line]+'                            ConfigurePartyJoin();\n'+text[line:]
 return text
