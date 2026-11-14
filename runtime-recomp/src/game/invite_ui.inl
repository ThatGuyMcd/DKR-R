// UI-owned context survives the game/launcher ImGui context switch. Only IDs
// enter visible notifications; admission keys remain in the private intent.
namespace invite_flow=dkr::runtime::invites;
using LobbyInvitation=dkr::runtime::netplay::FriendLobbyInviteView;
enum class InviteStep { None, Prompt, Stopping, Queued, Joining, Error };
struct InviteNotice {std::uint64_t id=0;std::string owner;std::chrono::steady_clock::time_point started{};};
struct InviteIntent {
    InviteStep step=InviteStep::None;
    std::optional<LobbyInvitation> invitation;
    bool watch=false,restore_overlay=false,open=false,armed=false,cancelled=false;
    std::string error;
    std::chrono::steady_clock::time_point started{};
};
std::deque<InviteNotice> g_invite_notices;
InviteIntent g_invite_intent;
bool g_invite_launcher_rom_ready=false;
bool InviteResourcesBusy() {
    const auto installs=dkr::store::installations().view();
    bool textures=false;{std::lock_guard lock(g_texture_import_mutex);textures=g_texture_import_state.running;}
    return !installs->ready||installs->busy||g_legacy_imports.snapshot().busy||g_code_library.view()->busy||
        g_vehicle_library.view()->busy||g_mod_launch.snapshot().modal||textures||DialogJobRunning();
}

std::optional<LobbyInvitation> FindLobbyInvitation(std::uint64_t id,const std::string& owner,std::string& error) {
    using namespace dkr::runtime::netplay;
    const auto snapshot=friend_service().snapshot();
    std::optional<LobbyInvitation> found;
    for(const auto& i:snapshot->incoming_lobby_invites)if(i.invite_id==id&&(owner.empty()||i.friend_identity==owner)) {
        if(found){error="That invitation is ambiguous. Open your invitation list again.";return {};}
        found=i;
    }
    if(!found||found->status!=FriendLobbyInviteStatus::Delivered||found->expires_unix<=std::uint64_t(std::time(nullptr))) {
        error="That invitation has expired or was cancelled.";return {};
    }
    if(found->compatibility!=DKR_NETWORK_RELEASE_VERSION){error="You and your friend need the same DKR-R build.";return {};}
    if(std::any_of(snapshot->friends.begin(),snapshot->friends.end(),[&](const auto& f){return f.identity==found->friend_identity&&f.blocked;})) {
        error="Invitations from this player are blocked.";return {};
    }
    if(found->lobby_code.empty()||found->admission==secure::Key{}){error="This invitation no longer has a valid admission token.";return {};}
    error.clear();return found;
}
void HideInviteNotice(std::uint64_t id) {
    std::erase_if(g_invite_notices,[&](const auto& n){return n.id==id;});
    if(!g_invite_notices.empty())g_invite_notices.front().started=std::chrono::steady_clock::now();
    invite_flow::visible(g_invite_notices.empty()?0:g_invite_notices.front().id);
}
bool InviteNoticeArrival(const LobbyInvitation& invitation) {
    if(std::any_of(g_invite_notices.begin(),g_invite_notices.end(),[&](const auto& n){return n.id==invitation.invite_id&&n.owner==invitation.friend_identity;}))return true;
    if(g_invite_notices.size()>=8)return false;
    g_invite_notices.push_back({invitation.invite_id,invitation.friend_identity,std::chrono::steady_clock::now()});
    invite_flow::visible(g_invite_notices.front().id);return true;
}
void CloseInviteIntent() {
    const bool restore=g_invite_intent.restore_overlay;
    if(g_invite_intent.invitation)g_invite_intent.invitation->admission.fill(0);
    g_invite_intent={};invite_flow::dialog(false);invite_flow::clear_return();(void)invite_flow::take_cancel();
    if(invite_flow::game_running())g_overlay_visible.store(restore,std::memory_order_release);
}
void InviteFailure(std::string error) {
    if(g_invite_intent.step==InviteStep::None)g_invite_intent.restore_overlay=g_overlay_visible.load();
    g_invite_intent.error=std::move(error);g_invite_intent.step=InviteStep::Error;
    g_invite_intent.open=true;g_invite_intent.armed=false;invite_flow::dialog(true);
    if(invite_flow::game_running())g_overlay_visible=true;
}
void QueueInviteJoin() {
    g_invite_intent.step=InviteStep::Queued;g_invite_intent.started=std::chrono::steady_clock::now();
    g_invite_intent.open=false;invite_flow::dialog(false);invite_flow::fence_inputs();
    g_play_offline_after_leave=false;g_online_mod_host_request.reset();
    if(g_online_mod_sync)g_online_mod_sync->cancel();g_online_mod_watching=false;g_online_mod_notice.clear();
    g_page_navigation_request=kPageOnlineMp;g_online_page.section=OlSection::Play;
    g_online_page.open_match_end=false;g_online_failure_modal_requested=g_online_failure_modal_active=false;
    g_online_error_notification.clear();g_online_action_status="Preparing to join your friend's lobby...";
    if(dkr::runtime::netplay::session().presentation_view().state==dkr::runtime::netplay::ConnectionState::Failed)
        dkr::runtime::netplay::session().disconnect("Accepting a new lobby invitation.");
}
void RequestLobbyInvitation(std::uint64_t id,const std::string& owner) {
    if(g_invite_intent.step!=InviteStep::None)return;
    std::string error;auto invitation=FindLobbyInvitation(id,owner,error);
    if(!invitation){InviteFailure(error);return;}
    const auto session=dkr::runtime::netplay::session().presentation_view();
    if(dkr::runtime::netplay::session().presentation_active()&&NormalizeOnlineInvite(session.invite)==NormalizeOnlineInvite(invitation->lobby_code)) {
        g_page_navigation_request=kPageOnlineMp;g_overlay_page=kPageOnlineMp;g_online_page.section=OlSection::Lobby;
        if(invite_flow::game_running())g_overlay_visible=true;HideInviteNotice(id);return;
    }
    if(!invite_flow::game_running()&&!g_invite_launcher_rom_ready){InviteFailure("Choose a valid ROM in Play, then accept this invitation again.");return;}
    if(!invite_flow::game_running()&&InviteResourcesBusy()){InviteFailure("Finish the current mod, save or file dialog first, then accept this invitation again.");return;}
    g_invite_intent.invitation=std::move(invitation);g_invite_intent.watch=g_online_join_watching;
    g_invite_intent.restore_overlay=g_overlay_visible.load();g_invite_intent.started=std::chrono::steady_clock::now();
    HideInviteNotice(id);
    if(invite_flow::game_running()||dkr::runtime::netplay::session().presentation_active()) {
        g_invite_intent.step=InviteStep::Prompt;g_invite_intent.open=true;invite_flow::dialog(true);g_overlay_visible=true;
    }else QueueInviteJoin();
}
void DeclineLobbyInvitation(std::uint64_t id,const std::string& owner) {
    if(g_invite_intent.step!=InviteStep::None)return;
    std::string error;if(!FindLobbyInvitation(id,owner,error)){InviteFailure(error);return;}
    LobbyInvitation declined;
    if(!dkr::runtime::netplay::friend_service().respond_lobby_invite(id,false,declined,error))InviteFailure(error);
    else HideInviteNotice(id);
}
void PumpInviteLauncher(bool rom_ready,bool manifest_ready) {
    g_invite_launcher_rom_ready=rom_ready;
    const auto returned=invite_flow::return_view();
    if(g_invite_intent.step==InviteStep::Stopping&&returned.phase!=invite_flow::ReturnPhase::Stopping) {
        invite_flow::clear_return();
        if(g_invite_intent.cancelled){CloseInviteIntent();return;}
        if(returned.phase!=invite_flow::ReturnPhase::Ready){InviteFailure(returned.error.empty()?"The previous game did not finish safely. The invitation was not joined.":returned.error);return;}
        QueueInviteJoin();
    }
    if(g_invite_intent.step!=InviteStep::Queued&&g_invite_intent.step!=InviteStep::Joining)return;
    using namespace dkr::runtime::netplay;
    auto& online=session();const auto view=online.presentation_view();
    if(g_invite_intent.step==InviteStep::Joining) {
        if(view.state==ConnectionState::Lobby||view.state==ConnectionState::Loading||view.state==ConnectionState::Running){CloseInviteIntent();return;}
        if(view.state==ConnectionState::Failed){InviteFailure(view.status.empty()?"The lobby could not be joined.":view.status);return;}
        if(dkr::online::view()->joining)return;
        if(view.state==ConnectionState::Offline){InviteFailure("Joining was cancelled. You are still in the launcher.");return;}
        return; // Mod downloads and code consent retain their original UI.
    }
    if(!g_invite_intent.invitation){InviteFailure("The pending invitation was lost. Please accept it again.");return;}
    std::string error;auto current=FindLobbyInvitation(g_invite_intent.invitation->invite_id,g_invite_intent.invitation->friend_identity,error);
    if(!current){InviteFailure(error);return;}
    if(!rom_ready){InviteFailure("The selected ROM is unavailable. Choose it in Play and accept the invitation again.");return;}
    if(!manifest_ready||SharedSaveBusy()||InviteResourcesBusy())return;
    if(g_online_mod_sync&&g_online_mod_sync->snapshot().phase!=dkr::mods::online::SyncPhase::Idle) {
        g_online_mod_sync->cancel();
        if(std::chrono::steady_clock::now()-g_invite_intent.started>std::chrono::seconds(60))InviteFailure("The previous mod job is still closing. The new lobby was not joined. Try again once it finishes.");
        return;
    }
    if(online.active()){InviteFailure("Another lobby became active. Leave it before accepting this invitation again.");return;}
    g_online_join_watching=g_invite_intent.watch;ConfigurePartyJoin();
    g_online_page.join_attempted=true;g_online_page.join_error.clear();
    if(!online.join_friend_invite(current->lobby_code,g_online_player_name,current->admission,error)){InviteFailure(error);return;}
    g_joining_friend_invite=*current;g_invite_intent.step=InviteStep::Joining;
    g_online_action_status="Joining "+current->friend_display_name+"'s lobby...";
}
bool InviteUiVisible(){return !g_invite_notices.empty()||g_invite_intent.step!=InviteStep::None;}
void DrawInviteInbox(float width) {
    const auto snapshot=dkr::runtime::netplay::friend_service().snapshot();
    bool heading=false;
    for(const auto& i:snapshot->incoming_lobby_invites)if(i.status==dkr::runtime::netplay::FriendLobbyInviteStatus::Delivered&&i.expires_unix>std::uint64_t(std::time(nullptr))) {
        if(!heading){ImGui::TextUnformatted("LOBBY INVITATIONS");ImGui::Dummy({0,8});heading=true;}
        ImGui::PushID(std::to_string(i.invite_id).c_str());
        ImGui::TextWrapped("%s invited you to their lobby.",i.friend_display_name.c_str());
        ImGui::BeginDisabled(g_invite_intent.step!=InviteStep::None);
        if(ImGui::Button("Accept invitation",{std::max(1.F,std::min(width,280.F)),42}))RequestLobbyInvitation(i.invite_id,i.friend_identity);
        ImGui::EndDisabled();ImGui::PopID();ImGui::Dummy({0,8});
    }
    if(heading)ImGui::Separator();
}
std::string InviteMenuShortcut() {
    using namespace dkr::runtime::input;
    const auto c=shortcut_controller_binding(ShortcutAction::ToggleOverlay);
    auto name=[](ShortcutBinding b,bool controller){
        if(b.primary==kUnbound)return std::string{};
        auto n=controller?controller_binding_name(b.primary):keyboard_binding_name(b.primary);
        if(b.secondary!=kUnbound)n+=" + "+(controller?controller_binding_name(b.secondary):keyboard_binding_name(b.secondary));return n;
    };
    std::string label="F1 / Esc";const auto keyboard=name(shortcut_keyboard_binding(ShortcutAction::ToggleOverlay),false);
    if(!keyboard.empty())label+=" / "+keyboard;const auto controller=name(c,true);if(!controller.empty())label+=" / "+controller;
    return label+": respond";
}
void DrawInviteUi() {
    const auto now=std::chrono::steady_clock::now();
    if(const auto focus=invite_flow::take_focus())RequestLobbyInvitation(focus,{});
    constexpr const char* popup="Lobby invitation";
    if(g_invite_intent.open){ImGui::OpenPopup(popup);g_invite_intent.open=false;}
    const bool prompt=g_invite_intent.step==InviteStep::Prompt,error=g_invite_intent.step==InviteStep::Error;
    if(prompt||error) {
        ImGui::SetNextWindowSize({std::min(580.F,std::max(260.F,ImGui::GetIO().DisplaySize.x-36.F)),0},ImGuiCond_Appearing);
        if(BeginPaddedModal(popup,ImGuiWindowFlags_AlwaysAutoResize)) {
            bool held=false;
            for(int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_NamedKey_END;++key)held|=ImGui::IsKeyDown(ImGuiKey(key));
            for(int button=0;button<5;++button)held|=ImGui::IsMouseDown(button);
            if(!held)g_invite_intent.armed=true;
            ImGui::TextUnformatted(error?"COULDN'T JOIN LOBBY":"LOBBY INVITATION");ImGui::Dummy({0,8});
            if(error)ImGui::TextWrapped("%s",g_invite_intent.error.c_str());
            else {
                const auto session=dkr::runtime::netplay::session().presentation_view();
                ImGui::TextWrapped("Leave your current %s and join %s?",invite_flow::game_running()?"game":"lobby",g_invite_intent.invitation->friend_display_name.c_str());
                if(session.host&&dkr::runtime::netplay::session().presentation_active())ImGui::TextWrapped("You are hosting. This will close your lobby for everyone.");
                else if(invite_flow::game_running()&&dkr::runtime::netplay::session().presentation_active()&&!dkr::runtime::spectators::party_watching(&dkr::runtime::netplay::session()))ImGui::TextWrapped("Leaving your racing place will end the current match under its normal departure rules.");
                else if(invite_flow::game_running()&&!dkr::runtime::netplay::session().presentation_active())ImGui::TextWrapped("This stops the game for all local players. Progress since the last save may be lost.");
                ImGui::Dummy({0,8});int role=g_invite_intent.watch?1:0;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if(ImGui::Combo("##invite-role",&role,"Racer\0Watch Race\0"))g_invite_intent.watch=role!=0;
            }
            ImGui::Dummy({0,12});const auto width=ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(!g_invite_intent.armed);
            const bool clicked_back=ImGui::Button(error?"Close":"Keep playing",{width,42});
            const bool back=g_invite_intent.armed&&(clicked_back||invite_flow::take_cancel());
            ImGui::SetItemDefaultFocus();
            if(back){CloseInviteIntent();ImGui::CloseCurrentPopup();}
            if(!error&&g_invite_intent.step==InviteStep::Prompt) {
                ImGui::Dummy({0,8});
                if(ImGui::Button("Leave and join",{width,42})) {
                    std::string failure;const auto current=FindLobbyInvitation(g_invite_intent.invitation->invite_id,g_invite_intent.invitation->friend_identity,failure);
                    if(!current){InviteFailure(failure);}
                    else if(invite_flow::game_running()) {
                        if(invite_flow::begin_return()) {
                            g_invite_intent.step=InviteStep::Stopping;invite_flow::dialog(false);g_play_offline_after_leave=false;
                            g_lifecycle_request.store(dkr::runtime::ui::LifecycleRequest::StopGame,std::memory_order_release);
                            g_overlay_visible=false;ImGui::CloseCurrentPopup();
                        }
                    } else {
                        dkr::runtime::netplay::session().disconnect("Leaving this lobby to accept an invitation.");QueueInviteJoin();ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndDisabled();ImGui::EndPopup();
        }
    }
    if(g_invite_intent.step==InviteStep::Stopping||g_invite_intent.step==InviteStep::Queued) {
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x*.5F,20},ImGuiCond_Always,{.5F,0});
        ImGui::SetNextWindowBgAlpha(.94F);
        if(ImGui::Begin("##invite-progress",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::TextUnformatted(g_invite_intent.step==InviteStep::Stopping?"Returning safely to the launcher...":"Preparing to join your friend's lobby...");
            if(ImGui::Button("Cancel joining")) {
                if(g_invite_intent.step==InviteStep::Stopping)g_invite_intent.cancelled=true;
                else CloseInviteIntent();
            }
        }ImGui::End();
    }
    if(g_invite_notices.empty())return;
    const auto notice=g_invite_notices.front();std::string failure;const auto invitation=FindLobbyInvitation(notice.id,notice.owner,failure);
    if(!invitation){HideInviteNotice(notice.id);return;}
    const auto screen=ImGui::GetIO().DisplaySize;const float width=std::min(460.F,std::max(270.F,screen.x-36.F));
    const int position=std::clamp(g_friend_online_notification_position,0,3);
    ImVec2 at{18,18+OverlayStackOffset(position,true,true)},pivot{0,0};
    if(position==1||position==3){at.x=screen.x-18;pivot.x=1;}
    if(position>=2){at.y=screen.y-18-OverlayStackOffset(position,true,true);pivot.y=1;}
    ImGui::SetNextWindowPos(at,ImGuiCond_Always,pivot);ImGui::SetNextWindowSize({width,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,14);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18,14});
    ImGui::SetNextWindowBgAlpha(.94F);
    if(ImGui::Begin("##actionable-invite",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextColored(kWarm,"LOBBY INVITE");ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width-36);
        ImGui::TextWrapped("%s invited you to their lobby.",invitation->friend_display_name.c_str());
        ImGui::TextWrapped("%s",InviteMenuShortcut().c_str());ImGui::PopTextWrapPos();ImGui::Dummy({0,8});
        const float button=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x*2)/3;
        ImGui::BeginDisabled(g_invite_intent.step!=InviteStep::None);
        if(ImGui::Button("Accept",{button,40}))RequestLobbyInvitation(notice.id,notice.owner);ImGui::SameLine();
        if(ImGui::Button("Decline",{button,40}))DeclineLobbyInvitation(notice.id,notice.owner);ImGui::SameLine();
        if(ImGui::Button("Later",{button,40}))HideInviteNotice(notice.id);
        ImGui::EndDisabled();
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)&&!g_invite_notices.empty())g_invite_notices.front().started=now;
        else if(now-notice.started>std::chrono::seconds(10)&&g_invite_intent.step==InviteStep::None)HideInviteNotice(notice.id);
    }ImGui::End();ImGui::PopStyleVar(2);
}
