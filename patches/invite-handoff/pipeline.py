"""Actionable invitations, composed only into isolated Patch Pipeline output."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Invite handoff anchor changed: '+a[:160])
 return t.replace(a,b,1)
def write(p,data):
 p.parent.mkdir(parents=True,exist_ok=True)
 if not p.is_file() or p.read_bytes()!=data:p.write_bytes(data)
def ui(t,out):
 t='#include "invite_handoff.hpp"\n#include <deque>\n'+t
 anchor='std::optional<dkr::runtime::netplay::FriendLobbyInviteView> g_joining_friend_invite;'
 t=once(t,anchor,anchor+'''
bool InviteNoticeArrival(const dkr::runtime::netplay::FriendLobbyInviteView&);
void RequestLobbyInvitation(std::uint64_t,const std::string&);
void DeclineLobbyInvitation(std::uint64_t,const std::string&);
void PumpInviteLauncher(bool,bool);
bool InviteUiVisible();
void DrawInviteUi();
void DrawInviteInbox(float);
''')
 old='''        if (g_online_notifications.push(
            dkr::runtime::ui_notifications::Kind::LobbyInvite,
            "LOBBY INVITE",
            invite.friend_display_name +
                " invited you to race. Open DKR-R ONLINE - OPEN LOBBIES to accept.",
            std::chrono::steady_clock::now()))'''
 t=once(t,old,'        if (InviteNoticeArrival(invite))')
 match=re.search(r'^#include "([^"\n]*runtime_online_ui[.]inl)"',t,re.M)
 if not match:raise ValueError('Final online UI include is missing')
 source=Path(match[1]);s=source.read_text(encoding='utf-8')
 s=once(s,'                room.action_disabled = !can_join || !compatible;',
  '                room.action_disabled = !compatible;')
 old='''                g_online_page.join_attempted = true;
                g_online_page.join_error.clear();
                ConfigurePartyJoin();
                if (session().join_friend_invite(invite.lobby_code, g_online_player_name,
                                                 invite.admission, error)) {
                    g_joining_friend_invite = invite;
                } else {
                    OlNotify(error);
                }'''
 s=once(s,old,'                RequestLobbyInvitation(invite.invite_id,invite.friend_identity);')
 old='''                FriendLobbyInviteView declined{};
                if (!friend_service().respond_lobby_invite(invite.invite_id, false, declined, error)) {
                    OlNotify(error);
                }'''
 s=once(s,old,'                DeclineLobbyInvitation(invite.invite_id,invite.friend_identity);')
 anchor='    const OnlineFrame frame{view, launcher, rom_ready, active, joining, in_lobby, busy, has_space};'
 s=once(s,anchor,anchor+'\n    if(!launcher||active)DrawInviteInbox(std::max(available_width,1.F));')
 dest=out/'runtime_online_ui.inl';write(dest,s.encode())
 t=once(t,match[0],'#include "'+dest.as_posix()+'"\n#include "'+(out/'invite_ui.inl').as_posix()+'"')
 t=once(t,'        PumpOnlineMods();\n        if (rom_ready &&',
  '        PumpOnlineMods();\n        PumpInviteLauncher(rom_ready,online_manifest.has_value());\n        if (rom_ready &&')
 # Both launcher and running-game notification passes share this entry.
 t=once(t,'void DrawOnlineNotification() {','void DrawOnlineNotification() {\n    DrawInviteUi();')
 t=once(t,'    return g_online_notifications.current(std::chrono::steady_clock::now()) !=',
  '    return InviteUiVisible() || g_online_notifications.current(std::chrono::steady_clock::now()) !=')
 t=once(t,'    const bool show_online_notification = OnlineNotificationActive();',
  '    const bool show_online_notification = OnlineNotificationActive() || InviteUiVisible();')
 t=once(t,'    if (show_overlay || show_camera_tuning) {',
  '    if (show_overlay || show_camera_tuning || dkr::runtime::invites::dialog()) {')
 anchor='''            ImGui_ImplSDL2_ProcessEvent(&event);
            if (HandleInputCaptureEvent(&event)) {'''
 t=once(t,anchor,'''            ImGui_ImplSDL2_ProcessEvent(&event);
            if(dkr::runtime::invites::dialog()&&(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP||
               event.type==SDL_TEXTINPUT||event.type==SDL_MOUSEMOTION||event.type==SDL_MOUSEBUTTONDOWN||
               event.type==SDL_MOUSEBUTTONUP||event.type==SDL_MOUSEWHEEL||event.type==SDL_CONTROLLERBUTTONDOWN||
               event.type==SDL_CONTROLLERBUTTONUP||event.type==SDL_CONTROLLERAXISMOTION)) {
                if((event.type==SDL_KEYDOWN&&!event.key.repeat&&event.key.keysym.scancode==SDL_SCANCODE_ESCAPE)||
                   (event.type==SDL_CONTROLLERBUTTONDOWN&&event.cbutton.button==SDL_CONTROLLER_BUTTON_B))dkr::runtime::invites::cancel_dialog();
                return;
            }
            if (HandleInputCaptureEvent(&event)) {''')
 t=once(t,'           dkr::runtime::hud::editor::active() || (g_capture_action != -1 && !g_capture_finished) ||',
  '           dkr::runtime::invites::dialog() || dkr::runtime::hud::editor::active() || (g_capture_action != -1 && !g_capture_finished) ||')
 t=once(t,'    const bool next = !g_overlay_visible.load(std::memory_order_acquire);',
  '    if(!g_overlay_visible.load(std::memory_order_acquire))dkr::runtime::invites::focus_visible();\n    const bool next = !g_overlay_visible.load(std::memory_order_acquire);')
 t=once(t,'    return g_overlay_visible.load(std::memory_order_acquire)||dkr::sdk2::tools_open.load();',
  '    return dkr::runtime::invites::capture()||g_overlay_visible.load(std::memory_order_acquire)||dkr::sdk2::tools_open.load();')
 t=once(t,'void dkr::runtime::ui::update_camera_mouse() {\n    UpdateCameraMouseOrbit();',
  'void dkr::runtime::ui::update_camera_mouse() {\n    if(dkr::runtime::invites::capture()&&!g_overlay_visible.load())return;\n    UpdateCameraMouseOrbit();')
 # Focus belongs to the modal. Other overlay shortcut/sidebar handlers must
 # not reinterpret its back/confirm events as racing/menu commands.
 anchor='''    if (event == nullptr) {
        return false;
    }'''
 start=t.index('bool dkr::runtime::ui::handle_runtime_event(');head,tail=t[:start],t[start:]
 tail=once(tail,anchor,anchor+'''
    if(dkr::runtime::invites::dialog()) {
        if((event->type==SDL_KEYDOWN&&!event->key.repeat&&event->key.keysym.scancode==SDL_SCANCODE_ESCAPE)||
           (event->type==SDL_CONTROLLERBUTTONDOWN&&event->cbutton.button==SDL_CONTROLLER_BUTTON_B)) {
            dkr::runtime::invites::cancel_dialog();return true;
        }
        std::scoped_lock guard(g_inspector_guard);if(!g_inspector)return true;
        std::scoped_lock frame_lock(g_inspector->frameMutex);g_inspector->handleSdlEvent(event);return true;
    }
''');t=head+tail
 t=once(t,'void dkr::runtime::ui::report_online_game_end(std::string message, bool lobby_retained) {',
  'void dkr::runtime::ui::report_online_game_end(std::string message, bool lobby_retained) {\n    if(dkr::runtime::invites::return_pending())return;')
 return t
def main_source(t,out):
 t='#include "invite_handoff.hpp"\n'+t
 t=once(t,'        std::atomic<bool> runtime_finished{false};',
  '        dkr::runtime::invites::game_running(true);\n        std::atomic<bool> runtime_finished{false};')
 t=once(t,'            if(runtime_failure==nullptr) {',
  '            if(runtime_failure==nullptr&&dkr::runtime::ui::lifecycle_request()!=dkr::runtime::ui::LifecycleRequest::StopGame) {')
 # There is another retirement in startup error recovery. Restrict the hook
 # to the post-runtime teardown, including its separate records writer.
 anchor='        const auto save_failure = dkr::runtime::saves::runtime_save_failure();'
 if t.count(anchor)!=1:raise ValueError('Final save failure collection changed')
 pos=t.index(anchor);head,tail=t[:pos],t[pos:]
 tail=once(tail,anchor,anchor+'\n        std::string invite_records_failure;')
 tail=once(tail,'        {std::string records_error;if(!dkr::runtime::saves::records::finish(records_error)) {',
  '        {std::string records_error;if(!dkr::runtime::saves::records::finish(records_error)) {\n            invite_records_failure=records_error;')
 tail=once(tail,'        dkr::runtime::saves::retire_runtime_save_context();',
  '        dkr::runtime::saves::retire_runtime_save_context();\n        dkr::runtime::invites::game_running(false);')
 # This is after worker destruction, Pak route retirement and save failure
 # collection, never in the rendering callback which accepted the invitation.
 tail=once(tail,'            dkr::runtime::ui::reset_lifecycle_request();',
  '''            dkr::runtime::invites::finish_return(!save_failure.empty()?save_failure:!invite_records_failure.empty()?invite_records_failure:!mod_failure.empty()?mod_failure:
                owned_failed?std::string("The previous online game did not finish safely. Review its error before joining."):std::string{});
            dkr::runtime::ui::reset_lifecycle_request();''')
 return head+tail
def platform(t,out):
 t='#include "netplay/session_chat/input_policy.hpp"\n#include "invite_handoff.hpp"\n'+t
 t=once(t,'const bool blocked = dkr::runtime::ui::overlay_visible() || dkr::runtime::camera::editor_open.load();',
  'const bool blocked = dkr::runtime::ui::overlay_visible() || dkr::runtime::camera::editor_open.load() || dkr::chat::captures_game_input();')
 anchor='    const bool blocked = dkr::runtime::ui::overlay_visible()'
 return once(t,anchor,(AUTH/'native/release_inputs.inl').read_text()+'\n'+anchor)
def main(a):
 out=a.output.resolve()
 if out.is_relative_to(ROOT):raise ValueError('Invite handoff stage must be outside the checkout')
 out.mkdir(parents=True,exist_ok=True)
 for p in (AUTH/'native').iterdir():
  if p.is_file():write(out/p.name,p.read_bytes())
 rows=json.loads(a.inputs.read_text(encoding='utf-8'));swaps=[]
 functions={'runtime_ui.cpp':ui,'game_main.cpp':main_source,'runtime_platform.cpp':platform}
 for row in rows:
  old=Path(row['path'])
  if old.name not in functions:continue
  raw=old.read_bytes();t=functions[old.name](raw.decode().replace('\r\n','\n'),out)
  def adjacent(m):
   p=old.parent/m[1];return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
  t=re.sub(r'^#include "([^"\n]+)"',adjacent,t,flags=re.M)
  new=out/old.name;write(new,t.encode());swaps.append({**row,'new':str(new),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':sha(new)})
 if {Path(r['new']).name for r in swaps}!=set(functions):raise ValueError('Missing final invite UI, lifecycle or platform sources')
 (out/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 print('Actionable lobby invitations and post-retirement joining staged. No game/tests executed.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
