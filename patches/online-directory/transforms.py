"""Surgical transformations over the final accepted source composition."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Online directory anchor changed: '+old[:150])
    return text.replace(old,new,1)

def direct(text,stage):
    text='#include "'+(stage/'owner_state.hpp').as_posix()+'"\n'+text
    text=once(text,'    if (state_ == ConnectionState::Offline) manifest_ = manifest;',
        '    if (state_ == ConnectionState::Offline) {manifest_ = manifest;dkr::online::compatibility(manifest.release_version);}')
    text=once(text,'    dkr::runtime::party_start::forget(this);\n    dkr::runtime::spectators::forget_party_link(this);',
        '    dkr::runtime::party_start::forget(this);\n    dkr::runtime::spectators::forget_party_link(this);\n    dkr::online::native::forget(this);')
    start=text.index('bool DirectSession::join_friend_invite(')
    opening=text.index('{',start)+1
    text=text[:opening]+'''
    if(valid_quick_join_code(invite)){
        bool watch=false;{auto p=dkr::runtime::spectators::party_link(this);std::scoped_lock lock(p->mutex);watch=p->join_watch;}
        if(dkr::online::begin_code_join(this,std::string(invite),player_name,admission,watch)){
            if(!dkr::online::view()->joining){error=dkr::online::view()->error;return false;}
            error.clear();return true;
        }
    }
'''+text[opening:]
    text=once(text,'    client_key_pair_ = secure::generate_key_pair();','    client_key_pair_ = dkr::online::client_key(this);')
    text=once(text,'void DirectSession::pump_locked() {','void DirectSession::pump_locked() {\n#include "'+(stage/'owner_pump.inl').as_posix()+'"')
    anchor='''    if (lobby_locked_ || ((state_ != ConnectionState::Lobby && state_ != ConnectionState::Hosting)&&!party_join)) {'''
    guard='''    auto directory_owner=dkr::online::native::owner(this);
    if(directory_owner->guarded){
        auto existing=dkr::online::native::binding(*directory_owner,packet_sender);
        const bool repeated=existing&&existing->client==client_public&&existing->watching==watch_join&&existing->privacy==directory_owner->privacy&&!directory_owner->blocked.contains(existing->account);
        if(!dkr::online::native::authorize(*directory_owner,hello.friend_admission,client_public,packet_sender,watch_join)){
            send_with_key(source,peer_key,protocol::MessageType::HelloAck,protocol::encode_hello_ack({false,0,"Sign in with Discord and join through DKR-R Online."}));return;
        }
        if(!repeated){auto* binding=dkr::online::native::binding(*directory_owner,packet_sender);
            if(binding)dkr::online::respond(this,{{"type","consume"},{"id",binding->id},{"sender",std::to_string(binding->sender)}});}
        auto gate=dkr::runtime::party_start::gate(this);
        const auto* watch_binding=dkr::online::native::binding(*directory_owner,packet_sender);
        const bool running_watch=watch_binding&&dkr::online::contract::running_spectator(watch_binding->watching,watch_binding->allow_running&&dkr::online::view()->running_spectators,party->enabled,party->running,party->stopping,launch_countdown_active_||launch_stage_!=LaunchStage::Idle);
        if(lobby_locked_||launch_countdown_active_||launch_stage_!=LaunchStage::Idle||
           gate->phase==dkr::runtime::party_start::Phase::Countdown||gate->phase==dkr::runtime::party_start::Phase::Committing||
           (party->launch&&!running_watch)||party->stopping||dkr::runtime::rollback_check::active(this)){
            send_with_key(source,peer_key,protocol::MessageType::HelloAck,protocol::encode_hello_ack({false,0,"The lobby is changing state. Please join again when it is waiting."}));return;
        }
    }
'''
    text=once(text,anchor,guard+anchor)
    text=once(text,'    if (consume_friend_admission_locked(hello.friend_admission)) {',
        '''    const auto directory_binding=dkr::online::native::binding(*directory_owner,packet_sender);
    if ((directory_owner->guarded&&directory_binding&&directory_binding->automatic)||
        (!directory_owner->guarded&&consume_friend_admission_locked(hello.friend_admission))) {''')
    anchor='''    if (!manifest_.mod_manifest_hash.empty() && !pending->mods_verified) {'''
    text=once(text,anchor,'''    if(auto owner=dkr::online::native::owner(this);owner->guarded){
        const auto* binding=dkr::online::native::binding(*owner,pending->sender_id);
        auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock lock(party->mutex);
        auto gate=dkr::runtime::party_start::gate(this);
        const bool running_watch=binding&&dkr::online::contract::running_spectator(binding->watching,binding->allow_running&&dkr::online::view()->running_spectators,party->enabled,party->running,party->stopping,launch_countdown_active_||launch_stage_!=LaunchStage::Idle);
        if(!binding||binding->client!=pending->client_public||binding->privacy!=owner->privacy||
           owner->blocked.contains(binding->account)||lobby_locked_||launch_countdown_active_||launch_stage_!=LaunchStage::Idle||
           gate->phase==dkr::runtime::party_start::Phase::Countdown||gate->phase==dkr::runtime::party_start::Phase::Committing||
           (party->launch&&!running_watch)||party->stopping||dkr::runtime::rollback_check::active(this)){
            error="This authenticated request is no longer valid for the waiting lobby.";return false;
        }
    }
'''+anchor)
    text=once(text,'        blocked_senders_.insert(pending->sender_id);','''        blocked_senders_.insert(pending->sender_id);
        auto directory_owner=dkr::online::native::owner(this);
        if(auto* binding=dkr::online::native::binding(*directory_owner,pending->sender_id))directory_owner->blocked.insert(binding->account);''')
    text=once(text,'    payload.visibility = room.visibility;',
        '''    payload.visibility = room.visibility;
    if(auto owner=dkr::online::native::owner(this);is_host_&&owner->guarded)payload.visibility=owner->public_room?Visibility::Public:Visibility::Private;''')
    text=once(text,'    const bool semantic_update = payload.generation > room_view_.generation;',
        '''    // Visibility already exists in the authenticated Room codec. It is
    // metadata and must also update without changing party/launch generations.
    room_view_.visibility=payload.visibility;
    const bool semantic_update = payload.generation > room_view_.generation;''')
    text=once(text,'                       match_id_, lobby_locked_};\n    result.owned_match_ending = owned_match_end_ && !owned_match_end_->resumed;',
        '''                       match_id_, lobby_locked_};
    if(auto owner=dkr::online::native::owner(this);is_host_&&owner->guarded)result.room.visibility=owner->public_room?Visibility::Public:Visibility::Private;
    result.owned_match_ending = owned_match_end_ && !owned_match_end_->resumed;''')
    return text

def store(text,stage):
    text='#include "'+(stage/'online_directory.hpp').as_posix()+'"\n#include "'+(stage/'linux_keyring.hpp').as_posix()+'"\n'+text
    text=once(text,'    unsigned timeout = 12000;','    unsigned timeout = 12000;\n    std::string online_instance;')
    text=once(text,'        headers += "Origin: https://positron-portal.com\\r\\n";',
        '        headers += "Origin: https://positron-portal.com\\r\\n";\n        if(!r.online_instance.empty())headers+="X-DKRR-Instance: "+r.online_instance+"\\r\\n";')
    text=once(text,'        add("Origin: https://positron-portal.com");',
        '        add("Origin: https://positron-portal.com");\n        if(!r.online_instance.empty())add("X-DKRR-Instance: "+r.online_instance);')
    text=once(text,'Service::Service() : impl_(std::make_unique<Impl>()) {}',
        'Service::Service() : impl_(std::make_unique<Impl>()) {\n#include "'+(stage/'account_bind.inl').as_posix()+'"\n}')
    text=once(text,'Service::~Service() {','Service::~Service() {\n    dkr::online::shutdown_account();')
    text=once(text,'''    (void)root;
    (void)token;
    throw Error("Secure remembered sign-in is not available on this platform yet.");''',
        '''    if(!dkr::online::keyring::store(root.generic_string(),token))
        throw Error("The desktop keyring could not remember sign-in. You are signed in for this session only.");''')
    text=once(text,'''    (void)root;
    return {};
#endif
}
struct Response''',
        '''    auto token=dkr::online::keyring::load(root.generic_string());
    return token.size()==43&&identifier(token)?token:std::string{};
#endif
}
struct Response''')
    text=once(text,'            s.remember = false;','            if(!dkr::online::keyring::available())s.remember = false;')
    text=once(text,'    remember = false;','    if(!dkr::online::keyring::available())remember = false;')
    text=once(text,'                    p.state.error = remember_warning;',
        '                    p.state.error = remember_warning;\n                    if(!remember_warning.empty())p.state.remember=false;')
    text=once(text,'                {\n                    std::lock_guard lock(p.mutex);\n                    p.token = token;',
        '''                // Account cancellation remains authoritative across keyring prompts.
                if(stop.stop_requested()){
                    try{Request revoke{std::string(Api)+"/auth/launcher/logout","POST",Json{{"device",device}}.dump(),token};
                        revoke.timeout=5000;document(revoke,std::stop_token{});}catch(...){}
#ifdef _WIN32
                    std::error_code cleanup;std::filesystem::remove(p.root/"device.bin",cleanup);
#else
                    dkr::online::keyring::clear(p.root.generic_string());
#endif
                    throw Error("Sign-in was cancelled.");
                }
                {
                    std::lock_guard lock(p.mutex);
                    p.token = token;''')
    text=once(text,'        std::filesystem::remove(p.root / "device.bin", error);',
        '''        std::filesystem::remove(p.root / "device.bin", error);
#ifndef _WIN32
        dkr::online::keyring::clear(p.root.generic_string());
#endif''')
    return text

def online_ui(text,stage):
    text=once(text,'                ImGui::SetCursorScreenPos({row.x + OlButtonWidth(room.action) + 8.0F, row.y});',
        '''                const bool wrap=OlButtonWidth(room.action)+8.F+OlButtonWidth(room.secondary)>inner;
                ImGui::SetCursorScreenPos(wrap?ImVec2{row.x,row.y+48.F}:ImVec2{row.x+OlButtonWidth(room.action)+8.F,row.y});''')
    text=once(text,'void DrawOlProfileCard(float width, const OnlineFrame& frame, bool profile_section) {',
        'void DrawOlDirectoryAccount(float);\nvoid DrawOlProfileCard(float width, const OnlineFrame& frame, bool profile_section) {')
    text=once(text,'        // Presence.','        DrawOlDirectoryAccount(inner);\n        // Presence.')
    anchor='void DrawOlOpenLobbies(float width, const OnlineFrame& frame) {'
    text=once(text,anchor,'#include "'+(stage/'directory_ui.inl').as_posix()+'"\n'+anchor+'''
    static const std::vector<std::string> sources{"Public","Friends"};
    int source=g_directory_friends_view?1:0;
    if(OlSelect("directory-source","Browse lobbies",&source,sources,width))g_directory_friends_view=source!=0;
    PaddockGap(12.F);
    if(!g_directory_friends_view){DrawOlPublicLobbies(width,frame);return;}''')
    text=once(text,'           40.0F + 10.0F + 52.0F + board.pad_bottom + 2.0F;',
        '           DirectoryVisibilityHeight(inner) + 40.0F + 10.0F + 52.0F + board.pad_bottom + 2.0F;')
    text=once(text,'    if (OlButton("Host settings")) g_online_page.section = OlSection::Settings;',
        '    DrawOlDirectoryVisibility(inner,false);\n    if (OlButton("Host settings")) g_online_page.section = OlSection::Settings;')
    text=once(text,'!frame.launcher || !frame.rom_ready || frame.active || OnlineModSelectionLocked())) {',
        '!frame.launcher || !frame.rom_ready || frame.active || OnlineModSelectionLocked() ||\n               (dkr::online::view()->feature_enabled&&!dkr::online::view()->connected) ||\n               (dkr::online::view()->public_desired&&!dkr::online::view()->connected))) {')
    text=once(text,'    const SessionView& view = frame.view;\n    const bool host = view.host;','''    const SessionView& view = frame.view;
    const bool host = view.host;
    const auto directory=dkr::online::view();
    const auto hosting=dkr::online::hosting_mode(&session());
    const char* visibility=!host?(view.room.visibility==Visibility::Public?"PUBLIC lobby":"PRIVATE lobby"):
        hosting==1?"PRIVATE - recreate this lobby to enable public discovery.":
        !directory->connected&&directory->feature_enabled?"Lobby listing unavailable - peer session continues.":
        directory->updating?"Updating lobby visibility...":directory->public_listed?"PUBLIC - listed worldwide":
        directory->public_desired?"PUBLIC - not currently listed":"PRIVATE - friends or lobby code";
    OlParagraph(visibility,14.F,kOlSoft,width);
    if(host)DrawOlDirectoryVisibility(width,frame.busy||!directory->connected||hosting!=2);''')
    text=once(text,'OlParagraph("Create a lobby or join with a code.", 16.0F,',
        'OlParagraph("Find a public lobby, host, or join with a code.", 16.0F,')
    text=once(text,'                    kOlSoft, text_width);\n        ImGui::EndGroup();',
        '''                    kOlSoft, text_width);
        const auto directory=dkr::online::view();
        PaddockGap(6.F);
        OlParagraph(directory->connected&&directory->count_available?std::to_string(directory->online)+" players online":directory->signed_in?"Players online: unavailable":"Sign in with Discord to connect",14.F,kOlGoText,text_width);
        ImGui::EndGroup();''')
    text=once(text,'Friends see you as offline. You can still join with a code.',
        'Friends see you as offline. You still count toward players online. Public hosting is disabled.')
    text=once(text,"Create a lobby and share its 5-character code, or enter a friend's code to join.",
        'Sign in with Discord to browse public lobbies or join with a code.')
    text=once(text,'The host approves joining players. You can also join friends through Open Lobbies.',
        'Choose Public or Private when hosting. The host approves joining players.')
    text=once(text,'Everyone selects Ready, then the host starts the game. Online play uses Rollback.',
        'Every racer selects Ready, then the host starts. Spectators do not need a racing slot. Online play uses Rollback.')
    return text

def invite(text):
    text=once(text,'        if(view.state==ConnectionState::Offline){InviteFailure(',
        '        if(dkr::online::view()->joining)return;\n        if(view.state==ConnectionState::Offline){InviteFailure(')
    return text

def account_ui(text):
    old='''#ifdef _WIN32
            bool remember = store.remember;
            if (StoreCheck("Keep me signed in on this PC", remember, panel.inner()))
                dkr::store::service().remember(remember);
#endif'''
    new='''#ifdef _WIN32
            const bool can_remember=true;
#else
            const bool can_remember=dkr::online::keyring::available();
#endif
            if(can_remember){bool remember=store.remember;
                if(StoreCheck("Keep me signed in on this PC",remember,panel.inner()))dkr::store::service().remember(remember);
            }else StoreNote("Session-only sign-in. A desktop keyring is unavailable.",panel.inner());'''
    text=once(text,old,new)
    return once(text,'        StoreNote("Signed in for this session.", session.inner());',
        '        StoreNote(store.remember?"Your desktop keyring remembers this sign-in.":"Signed in for this session.",session.inner());')

def runtime_ui(text,stage):
    text='#include "'+(stage/'online_directory.hpp').as_posix()+'"\n#include "'+(stage/'linux_keyring.hpp').as_posix()+'"\n'+text
    text=once(text,'    dkr::store::service().configure(g_config_directory, DKR_RELEASE_VERSION);',
        '    dkr::store::service().configure(g_config_directory, DKR_RELEASE_VERSION);\n    dkr::online::configure(g_config_directory);')
    text=once(text,'void PumpFriendPresence() {','''void PumpDirectoryJoin(){
    using namespace dkr::runtime::netplay;
    auto& online=session();
    if(!online.active()&&!OnlineModSelectionLocked()&&!SharedSaveBusy()){
        dkr::online::Bootstrap bootstrap;
        if(dkr::online::take_bootstrap(&online,bootstrap)){
            g_online_join_watching=bootstrap.watch;ConfigurePartyJoin();
            std::string error;
            if(!online.join_friend_invite(bootstrap.code,g_online_player_name,bootstrap.admission,error)){dkr::online::cancel_join();g_online_action_status=error;}
        }
    }
    const auto url=dkr::store::service().take_browser_url();if(!url.empty())SDL_OpenURL(url.c_str());
    dkr::online::hidden(friend_service().appear_offline());
}
void PumpFriendPresence() {
    PumpDirectoryJoin();''')
    return text
