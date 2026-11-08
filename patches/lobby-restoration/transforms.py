"""Restore the preserved lobby composition over the current party runtime."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent

def once(text,before,after):
 if text.count(before)!=1:raise ValueError('Lobby restoration anchor changed: '+before[:130])
 return text.replace(before,after,1)

def region(text,start,end,change):
 a=text.index(start);b=text.index(end,a)
 return text[:a]+change(text[a:b])+text[b:]

def direct(text,stage):
 text='#include "'+(stage/'lobby_adapter.hpp').as_posix()+'"\n'+text
 anchor='    if(party->enabled) {\n        const auto now=std::chrono::steady_clock::now();'
 text=once(text,anchor,'    if(party->enabled) {\n#include "'+(stage/'remove_member.inl').as_posix()+'"\n        const auto now=std::chrono::steady_clock::now();')
 # The existing lock implementation already clears pending invitations/routes.
 # Permit that same operation in a running spectator-capable lobby only.
 def lock(body):
  return once(body,'    if (!is_host_ || (state_ != ConnectionState::Hosting &&\n                      state_ != ConnectionState::Lobby)) {',
   '    if (!is_host_ || (state_ != ConnectionState::Hosting &&\n                      state_ != ConnectionState::Lobby && !dkr::runtime::spectators::party_enabled(this))) {')
 return region(text,'bool DirectSession::set_lobby_locked(', 'bool DirectSession::kick_player(',lock)

def online(text,stage):
 a=text.index('void DrawPartyPage(');b=text.index('void DrawOnlinePage(',a)
 text=text[:a]+'#include "'+(stage/'lobby_controls.inl').as_posix()+'"\n\n'+text[b:]
 text=once(text,'// Content that slides in after a section or phase change (.is-entering).',
  '#include "'+(stage/'lobby_context.inl').as_posix()+'"\n\n// Content that slides in after a section or phase change (.is-entering).')
 a=text.index('    if(dkr::runtime::spectators::party_enabled(&online)&&in_lobby) {');b=text.index('    // A new session opens its lobby;',a)
 text=text[:a]+text[b:]
 text=once(text,'    const SessionView view = online.presentation_view();','    SessionView view = online.presentation_view();\n    OlPartyProject(view);')
 text=once(text,'    const bool busy = view.owned_match_ending || view.connection_test_active || view.launch_countdown_active ||',
  '    const bool busy = (OlPartyActive()&&g_ol_party.link.stopping) || view.owned_match_ending || view.connection_test_active || view.launch_countdown_active ||')
 text=once(text,'    const bool has_space = in_lobby && view.host && !view.lobby_locked && !busy && occupied < maximum &&',
  '    bool has_space = in_lobby && view.host && !view.lobby_locked && !busy && occupied < maximum &&')
 text=once(text,'    const OnlineFrame frame{view, launcher, rom_ready, active, joining, in_lobby, busy, has_space};',
  '''    if(OlPartyActive())has_space=in_lobby&&view.host&&!view.lobby_locked&&!g_ol_party.link.stopping&&
        !g_ol_party_probe.active&&g_ol_party.link.party.members.size()<lobby_party::MaximumMembers;
    const OnlineFrame frame{view, launcher, rom_ready, active, joining, in_lobby, busy, has_space};''')
 text=once(text,'    DrawOlModals(frame);','    DrawOlModals(frame);\n    DrawOlPartyDialogs(frame);')
 text=once(text,'    const bool invite_space = can_invite && lobby.hosting && lobby.players < lobby.maximum_players && !frame.busy;',
  '    const bool invite_space = can_invite && (OlPartyActive()?frame.has_space:(lobby.hosting && lobby.players < lobby.maximum_players && !frame.busy));')
 text=once(text,'void OlRequestConnectionTest() {\n    std::string error;',
  'void OlRequestConnectionTest() {\n    if(OlPartyActive()){OlPartyBeginProbe();return;}\n    std::string error;')
 text=once(text,'bool OlHasTestResults(const OnlineFrame& frame) {',
  'bool OlHasTestResults(const OnlineFrame& frame) {\n    if(OlPartyActive())return g_ol_party_probe.complete&&g_ol_party_probe.room==g_ol_party.room;')
 text=once(text,'void DrawOlConnection(float width, const OnlineFrame& frame) {',
  'void DrawOlConnection(float width, const OnlineFrame& frame) {\n    if(OlPartyActive()){DrawOlPartyConnection(width,frame);return;}')
 def grid(body):
  body=once(body,'    const int columns = OlNarrow(760.0F) ? 1 : 2;', '    const int columns = width < 660.F ? 1 : 2;')
  body=once(body,'        const char* ready_text = loaded ? "Loaded" : player.ready ? "Ready" : "Not ready yet";',
   '        const char* ready_text = OlPartyActive()&&OlPartyLive()?"Racing":loaded ? "Loaded" : player.ready ? "Ready" : "Not ready yet";')
  body=once(body,'        const char* save_text = view.online_save_ready[slot] ? "Save ready"',
   '        const char* save_text = OlPartyActive()?(g_ol_party.save_ready?"Session ready":"Syncing session..."):view.online_save_ready[slot] ? "Save ready"')
  body=once(body,'            const std::string ping = std::to_string(player.ping_ms) + " ms";',
   '            const std::string ping = OlPartyActive()&&!player.ping_ms?"Measuring...":std::to_string(player.ping_ms) + " ms";')
  body=once(body,'                DrawOlPing(draw, at, meta.line, player.ping_ms);',
   '''                if(!OlPartyActive()||player.ping_ms)DrawOlPing(draw, at, meta.line, player.ping_ms);
                else PaddockDrawRun(draw,meta,at,PaddockCol(kOlSoft),"Measuring...","Measuring..."+12);''')
  body=once(body,'                    ImGui::IsMouseHoveringRect(at, {at.x + 19.0F + 60.0F, at.y + meta.line})) {',
   '                    ImGui::IsMouseHoveringRect(at, {at.x + 19.0F + 60.0F, at.y + meta.line}) && !OlPartyActive()) {')
  before='const bool removable = view.host && '
  if body.count(before)!=2:raise ValueError('Racer card control reservation changed')
  body=body.replace(before,'const bool removable = OlPartyActive() || view.host && ')
  body=once(body,'                const OlDisabled disabled(frame.busy);', '                const OlDisabled disabled(OlPartyActive()?OlPartyChanging(frame):frame.busy);')
  body=once(body,'                if (OlButton("\\xC3\\x97##remove", OlTone::Icon, 40.0F, tooltip.c_str())) {',
   '                if (OlButton(OlPartyActive()?"...##player-options":"\\xC3\\x97##remove", OlTone::Icon, 40.0F, OlPartyActive()?"Player options":tooltip.c_str())) {')
  body=once(body,'                    if (!dkr::runtime::netplay::session().kick_player(static_cast<std::uint8_t>(slot), error)) {',
   '                    if(OlPartyActive())OlPartyOpenMember(g_ol_party.link.party.controls[slot].member);\n                    else if (!dkr::runtime::netplay::session().kick_player(static_cast<std::uint8_t>(slot), error)) {')
  return body
 text=region(text,'void DrawOlGrid(', 'void DrawOlGate(',grid)
 def gate(body):
  body=once(body,'        const ImVec2 at = ImGui::GetCursorScreenPos();',
   '        const ImVec2 at = ImGui::GetCursorScreenPos();\n        const bool pending_space=OlPartyJoinSpace(frame,pending);')
  body=body.replace('frame.has_space','pending_space')
  body=body.replace('{"Decline", OlTone::Plain, frame.busy}','{"Decline", OlTone::Plain, OlPartyActive()?OlPartyChanging(frame):frame.busy}')
  body=body.replace('{"Block for session", OlTone::Plain, frame.busy}','{"Block for session", OlTone::Plain, OlPartyActive()?OlPartyChanging(frame):frame.busy}')
  body=once(body,'        const std::string reason = pending.preparing_mods ?',
   '''        const auto watch=g_ol_party.pending_watch.find(pending.request_id);
        const std::string reason = OlPartyActive()&&!pending.preparing_mods&&pending.compatible ?
            (pending_space?(watch!=g_ol_party.pending_watch.end()&&watch->second?"Wants to watch - no controller port":"Wants a racing place"):
             OlPartyLive()?"A running race accepts spectators only":"No space for this requested role") : pending.preparing_mods ?''')
  return body
 text=region(text,'void DrawOlGate(', 'void DrawOlStartBar(',gate)
 def lobby(body):
  body=once(body,'    OlParagraph("ONLINE SESSION SAVE - supplied by " + host_name,',
   '    if(OlPartyActive())host_name=OlPartyHostName();\n    OlParagraph("ONLINE SESSION SAVE - supplied by " + host_name,')
  body=once(body,' + " characters.",',
   ' + " characters, " + std::to_string(count(static_cast<dkr::mods::online::Kind>(3))) + " vehicles, " +\n            std::to_string(mods.manifest?mods.manifest->code.size():0) + " Magic Codes.",')
  body=once(body,'DrawOlLive(draw, at, host ? "HOSTING" : "JOINED")',
   'DrawOlLive(draw, at, OlPartyActive()?(OlPartyWatching()?"WATCHING":OlPartyLive()?"RACE IN PROGRESS":host?"HOSTING":"JOINED"):host?"HOSTING":"JOINED")')
  body=once(body,'        const std::string line = host', '        std::string line = host')
  body=once(body,'        OlParagraph(line, 15.0F, kOlSoft, title_width);',
   '''        if(OlPartyActive())line=std::to_string(total)+" of "+std::to_string(maximum)+" racers. "+
            std::to_string(OlPartySpectators())+" of 8 spectators. "+(view.lobby_locked?"Lobby locked.":"Friends can join with the code.");
        OlParagraph(line, 15.0F, kOlSoft, title_width);''')
  a=body.index('        const ImVec2 row = ImGui::GetCursorScreenPos();');b=body.index('\n    }\n    if (view.connection_test_active)',a)
  body=body[:a]+'''        DrawOlLobbyToolbar(inner,frame);'''+body[b:]
  body=once(body,'    if (view.connection_test_active) {','    DrawOlPartySpectators(inner,frame);\n    if (view.connection_test_active) {')
  body=once(body,'    if (launched) {','    if(OlPartyActive()&&(launched||OlPartyWatching()||g_ol_party.link.stopping)){DrawOlPartyStart(inner,frame);}\n    else if (launched) {')
  body=once(body,'&locked, lock_width, false, frame.busy)', '&locked, lock_width, false, OlPartyActive()?OlPartyChanging(frame):frame.busy)')
  return body
 text=region(text,'void DrawOlLobby(', '// Knocking on the host',lobby)
 def modals(body):
  body=once(body,'    if (BeginOlDialog(kResults)) {\n        const SessionView& view = frame.view;',
   '    if (BeginOlDialog(kResults)) {\n        if(OlPartyActive()){DrawOlPartyResults(frame);ImGui::EndPopup();}\n        else {\n        const SessionView& view = frame.view;')
  end=body.rfind('        ImGui::EndPopup();\n    }\n}')
  if end<0:raise ValueError('Connection results closing scope changed')
  body=body[:end]+body[end:].replace('        ImGui::EndPopup();\n    }\n}','        ImGui::EndPopup();\n        }\n    }\n}',1)
  return body
 return region(text,'void DrawOlModals(', 'void DrawOnlineGuideModal(',modals)
