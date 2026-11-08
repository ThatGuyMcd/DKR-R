"""Isolated Rollback diagnostics over the accepted lobby/runtime sources."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent

def once(text,before,after):
 if text.count(before)!=1:raise ValueError('Rollback check anchor changed: '+before[:130])
 return text.replace(before,after,1)
def region(text,start,end,replacement):
 a=text.index(start);b=text.index(end,a);return text[:a]+replacement+text[b:]
def include(stage,name):return '#include "'+(stage/name).as_posix()+'"\n'
def direct(text,stage):
 text=include(stage,'rollback_check.hpp')+text
 request='''bool DirectSession::request_connection_test(std::string& error) {
    std::scoped_lock lock(mutex_);
'''+include(stage,'owner_context.inl')+'''    const bool ok=dkr::runtime::rollback_check::request(this,rollback_context(),error);
    if(ok){worker_wake_=true;state_changed_.notify_all();}
    return ok;
}

'''
 text=region(text,'bool DirectSession::request_connection_test(', 'void DirectSession::reset_connection_test_locked()',request)
 text=once(text,'void DirectSession::reset_connection_test_locked() {','void DirectSession::reset_connection_test_locked() {\n    dkr::runtime::rollback_check::forget(this);')
 text=region(text,'void DirectSession::service_connection_test_locked(', 'bool DirectSession::all_active_players_acknowledged_locked(',
  '''void DirectSession::service_connection_test_locked(std::chrono::steady_clock::time_point) {
    // Superseded by the isolated Rollback diagnostic serviced before the
    // spectator-capable lobby's early return. Legacy scores are never run.
}

''')
 text=once(text,'void DirectSession::pump_locked() {','void DirectSession::pump_locked() {\n'+include(stage,'owner_context.inl')+include(stage,'owner_send.inl'))
 anchor='// Inside DirectSession::pump_locked, after AEAD and replay-window validation.'
 text=once(text,anchor,'''        if(dkr::runtime::rollback_check::diagnostic_type(packet.header.type)) {
            // Already decrypted, admitted and replay-window validated above.
            bool approved=!is_host_&&packet_sender==host_sender_id_&&source==host_address_;
            if(is_host_){
                const auto p=dkr::runtime::spectators::party_link(this);std::scoped_lock lock(p->mutex);
                if(p->enabled){const auto* peer=p->peer(packet_sender);approved=peer&&peer->address==source;}
                else{const auto* peer=peer_by_sender(packet_sender);approved=peer&&peer->active&&peer->address==source;}
            }
            if(approved)dkr::runtime::rollback_check::receive(this,rollback_context(),packet_sender,packet.header.type,packet.payload,rollback_send,std::chrono::steady_clock::now());
            continue; // Never enter legacy preflight or real game-input handlers.
        }
'''+anchor)
 text=once(text,'// Inside pump_locked after ordinary receive/flush, before four-port policies.',
  '    if(dkr::runtime::rollback_check::engaged(this))\n        dkr::runtime::rollback_check::service(this,rollback_context(),rollback_send,std::chrono::steady_clock::now());\n// Inside pump_locked after ordinary receive/flush, before four-port policies.')
 text=once(text,'        const auto receive_poll =\n            state_',
  '        const auto receive_poll =\n            dkr::runtime::rollback_check::active(this) || state_')
 return text
def context(text):
 a=text.index('struct OlPartyProbe {');b=text.index('bool g_ol_member_open=',a)
 text=text[:a]+'dkr::runtime::rollback_check::View g_ol_rollback_check;\n'+text[b:]
 text=region(text,'void OlPartyBeginProbe(){','void OlPartyProject(', '')
 a=text.index('    auto& test=g_ol_party_probe;');b=text.index('    view.room.players={};',a)
 text=text[:a]+'''    g_ol_rollback_check=dkr::runtime::rollback_check::snapshot(&session());
    view.connection_test_active=g_ol_rollback_check.active;
    view.connection_test_remaining_ms=g_ol_rollback_check.remaining_ms;
    view.connection_test_results={};
    view.connection_test_result_generation=g_ol_rollback_check.complete?g_ol_rollback_check.revision:0;
    if(!OlPartyActive())return;
'''+text[b:]
 a=text.index('    view.connection_test_active=test.active;');b=text.index('\n}',a)
 text=text[:a]+text[b:]
 return text.replace('g_ol_party_probe.active','g_ol_rollback_check.active')
def controls(text):
 text=region(text,'void DrawOlPartyConnection(', 'void DrawOlPartyDialogs(', '')
 return text.replace('g_ol_party_probe.active','g_ol_rollback_check.active').replace('"Test session connection"','"Check Rollback connection"')
def online(text,stage):
 text=region(text,'void OlRequestConnectionTest() {','// ------------------------------------------------------------ lobby',include(stage,'connection_ui.inl')+'\n')
 text=once(text,'void DrawOlModals(const OnlineFrame& frame) {',include(stage,'report_ui.inl')+'\nvoid DrawOlModals(const OnlineFrame& frame) {')
 a=text.index('    if (BeginOlDialog(kResults)) {');b=text.index('\n}\n\nvoid DrawOnlineGuideModal()',a)
 text=text[:a]+'''    if (BeginOlDialog(kResults)) {
        DrawOlRollbackResults(frame);
        ImGui::EndPopup();
    }'''+text[b:]
 text=text.replace('g_ol_party_probe.active','g_ol_rollback_check.active')
 text=once(text,'constexpr const char* kResults = "Connection test results";','constexpr const char* kResults = "Rollback connection report";')
 return text
