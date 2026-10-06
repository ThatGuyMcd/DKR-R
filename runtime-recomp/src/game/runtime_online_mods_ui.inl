// Included after FinishOnlineLobby. Session selection is temporary: the
// offline libraries and their enable/hide/order files are never written here.
bool g_online_keep_downloads = true;
std::string g_online_kept_profile;
bool OnlineModSelectionLocked() {
    return g_online_mod_host_request.has_value();
}

void ReportOnlinePreparationFailure(std::string_view error) {
    if(g_online_mod_notice.fail(error,dkr::runtime::rom::revision_name(g_online_mod_identity.revision),
        dkr::runtime::netplay::synchronization_name(CurrentOnlineSynchronization())))
        std::fprintf(stderr,"[online-mods][failure] revision=%s backend=%s stage=%s error=%s\n",
            g_online_mod_notice.revision.c_str(),g_online_mod_notice.backend.c_str(),
            g_online_mod_notice.stage.c_str(),g_online_mod_notice.error.c_str());
}

dkr::mods::online::RuntimePreparation OnlineModRuntimePreparation(
    std::vector<std::filesystem::path> roms) {
    return [roms=std::move(roms)](auto profile,std::stop_token stop)->std::shared_ptr<const void> {
        return dkr::mods::online::with_matching_game_pak(roms,profile->manifest.revision,stop,
            [](const auto& rom){return dkr::runtime::rom::inspect(rom);},
            [&](const auto& rom){return dkr::mods::online::RuntimeResources::prepare(profile,rom,stop);});
    };
}

dkr::mods::online::SyncSettings OnlineModSettings() {
    auto settings=g_online_mod_settings;
    settings.offline_legacy_root=g_config_directory/"mods"/"legacy";
    settings.offline_tracks_root=dkr::runtime::custom_tracks::directory();
    if(!g_online_mod_selected_rom.empty())settings.imported_roms.push_back(g_online_mod_selected_rom);
    for(const auto& entry:LoadRomCatalog()) {
        if(settings.imported_roms.size()==8)break;
        if(std::ranges::find(settings.imported_roms,entry.path)==settings.imported_roms.end())
            settings.imported_roms.push_back(entry.path);
    }
    return settings;
}

bool CreateOnlineLobby() {
    using namespace dkr::runtime::netplay;
    if(!g_online_mod_sync || !g_online_mod_identity.supported() || session().active() ||
       OnlineModSelectionLocked() || g_legacy_imports.snapshot().busy || g_mod_launch.snapshot().modal) {
        g_online_action_status="Finish the current operation and select a supported Game Pak before hosting.";return false;
    }
    if(const char* reason=experimental::runtime_admission_error(CurrentOnlineSynchronization(),session().owned_backend_available())) {
        g_online_action_status=reason;ReportOnlinePreparationFailure(reason);return false;
    }
    OnlineHostRequest request;
    request.rules.host_control=static_cast<HostControlPolicy>(g_online_host_control);
    request.rules.maximum_players=static_cast<std::uint8_t>(g_online_maximum_players);
    request.rules.synchronization=CurrentOnlineSynchronization();
    request.rules.rollback_window=synchronization_has_prediction_window(request.rules.synchronization)
        ? static_cast<std::uint8_t>(g_online_rollback_window):0;
    request.rules.automatic_input_delay=g_online_automatic_delay;
    request.rules.manual_input_delay=static_cast<std::uint8_t>(g_online_manual_delay);
    request.rules.record_replay=g_online_record_replay;
    request.manifest=BuildNetplayManifest(g_online_mod_identity);
    request.save_mode=static_cast<dkr::runtime::saves::OnlineSaveSeedMode>(g_online_save_seed_mode);
    request.room=g_online_room_name;request.name=g_online_player_name;
    std::vector<std::filesystem::path> labs;
    for(const auto& track:dkr::runtime::custom_tracks::tracks())if(track.enabled)labs.push_back(track.source);
    auto settings=OnlineModSettings();
    if(!g_online_mod_sync->prepare_host(settings,g_config_directory/"mods"/"legacy",std::move(labs),
            std::string(dkr::mods::online::asset_revision(g_online_mod_identity.revision)),true,
            OnlineModRuntimePreparation(settings.imported_roms))) {
        g_online_action_status="The previous mod operation is stopping. Try creating the lobby again in a moment.";return false;
    }
    g_online_mod_host_request=std::move(request);
    g_online_mod_notice.clear();
    std::fprintf(stderr,"[online-mods][host] revision=%s asset-revision=%s backend=%s preparation-start\n",
        dkr::runtime::rom::revision_id(g_online_mod_identity.revision).data(),
        dkr::mods::online::asset_revision(g_online_mod_identity.revision).data(),
        synchronization_name(CurrentOnlineSynchronization()));
    g_online_action_status="Preparing the host's session mod set in the background. Offline saves and mods are unchanged.";
    return true;
}

void PumpOnlineMods() {
    if(!g_online_mod_sync)return;
    using namespace dkr::mods::online;
    using namespace dkr::runtime::netplay;
    auto view=g_online_mod_sync->snapshot();
    g_online_mod_notice.progress(view.stage);
    const auto network=session().view();
    if(view.phase==SyncPhase::Verified && view.profile && g_online_kept_profile!=view.profile->digest) {
        g_online_kept_profile=view.profile->digest;
        if(view.kept_offline) {
            g_legacy_imports.refresh();dkr::runtime::custom_tracks::reload();
            g_online_action_status="Verified mods kept in Mods/Hacks. New copies are inactive for offline play.";
            std::fprintf(stderr,"[online-mods][offline-keep] complete manifest=%s\n",view.profile->digest.c_str());
        } else if(!view.keep_error.empty()) {
            g_online_action_status="Joined with verified session mods, but offline copies could not be kept: "+view.keep_error;
            std::fprintf(stderr,"[online-mods][offline-keep] failed error=%s\n",view.keep_error.c_str());
        }
    }
    // Pre-offer ROM/settings rejections belong to the existing one-click
    // compatibility UI, not a blocking mod-download failure modal.
    if(view.phase==SyncPhase::Failed && !network.compatibility_sync_offer)
        ReportOnlinePreparationFailure(view.error);
    if(g_online_mod_host_request) {
        if(network.state!=ConnectionState::Offline ||
           g_online_mod_host_request->manifest.canonical_rom_hash!=g_online_mod_identity.canonical_xxh3 ||
           g_online_mod_host_request->manifest.magic_codes_hash!=dkr::runtime::magic_codes::selected_mask()) {
            g_online_mod_sync->cancel();g_online_mod_host_request.reset();
            g_online_action_status="Host preparation cancelled because the selected lobby, Game Pak or Magic Codes changed.";
        } else if(view.phase==SyncPhase::HostReady) {
            const bool modded=view.manifest && !view.manifest->content.empty();
            const auto request=*g_online_mod_host_request;g_online_mod_host_request.reset();
            if(modded && (!view.profile || !view.runtime)) {
                g_online_action_status="The host's playable mod resources were not prepared.";g_online_mod_sync->cancel();
                ReportOnlinePreparationFailure(g_online_action_status);
            } else if(!FinishOnlineLobby(request,modded?view.profile->digest:std::string{},
                    modded?static_cast<std::uint32_t>(encode(*view.manifest).size()):0)) {
                ReportOnlinePreparationFailure(g_online_action_status);g_online_mod_sync->cancel();
            }
        } else if(view.phase==SyncPhase::Failed) {
            g_online_action_status=view.error;g_online_mod_host_request.reset();
        }
        return;
    }
    const bool joining=network.state==ConnectionState::Connecting || network.state==ConnectionState::AwaitingApproval;
    if(joining && !g_online_mod_watching) {
        auto settings=OnlineModSettings();
        g_online_mod_notice.clear();
        g_online_keep_downloads=true;g_online_kept_profile.clear();
        g_online_mod_watching=g_online_mod_sync->watch_client(settings,OnlineModRuntimePreparation(settings.imported_roms));
    } else if(!session().active() && !joining) {
        if(view.phase==SyncPhase::Failed && !view.error.empty())g_online_action_status=view.error;
        g_online_mod_sync->cancel();g_online_mod_watching=false;
        if(network.state==ConnectionState::Offline)session().configure_mod_admission({},0);
    }
}

void DrawOnlineModModal() {
    if(!g_online_mod_sync)return;
    using namespace dkr::mods::online;
    const auto view=g_online_mod_sync->snapshot();
    const bool review=view.phase==SyncPhase::Review;
    const bool busy=view.phase==SyncPhase::HostPreparing || view.phase==SyncPhase::Downloading || view.phase==SyncPhase::Preparing;
    const bool failed=g_online_mod_notice.visible();
    constexpr const char* name="Session mods";
    if(!review && !busy && !failed) {
        if(ImGui::IsPopupOpen(name) && BeginPaddedModal(name,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        return;
    }
    ImGui::OpenPopup(name);
    ImGui::SetNextWindowSize({std::min(660.0F,ImGui::GetIO().DisplaySize.x-48.0F),0},ImGuiCond_Appearing);
    if(!BeginPaddedModal(name,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextUnformatted(failed?"ONLINE SESSION PREPARATION FAILED":review?"THIS LOBBY USES CUSTOM MODS":"PREPARING SESSION MODS");
    ImGui::Dummy({0,8});
    if(failed) {
        ImGui::TextWrapped("Game Pak: %s",g_online_mod_notice.revision.c_str());
        ImGui::TextWrapped("Backend: %s",g_online_mod_notice.backend.c_str());
        if(!g_online_mod_notice.stage.empty())ImGui::TextWrapped("Stage: %s",g_online_mod_notice.stage.c_str());
        ImGui::Separator();ImGui::TextWrapped("%s",g_online_mod_notice.error.c_str());
        ImGui::TextWrapped("No unverified content was activated. Your offline saves and mod selection were not changed. Dismiss this message, correct the requirement above, then retry hosting or joining.");
        if(ImGui::Button("DISMISS",{ImGui::GetContentRegionAvail().x,42})) {
            g_online_mod_notice.clear();g_online_mod_sync->cancel();
            g_online_mod_host_request.reset();g_online_mod_watching=false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();return;
    }
    ImGui::TextWrapped("%s",view.stage.c_str());
    if(review && view.manifest) {
        ImGui::TextWrapped("The host controls this session's active mods. Download and verify these files before joining?");
        BeginPaddedChild("Required mods",{ImGui::GetContentRegionAvail().x,std::min(230.0F,40.0F+view.manifest->content.size()*28.0F)},true);
        for(const auto& mod:view.manifest->content) {
            const char* kind=mod.kind==Kind::Character?"Character":mod.kind==Kind::TrackLab?"Track Lab":"Track";
            ImGui::TextWrapped("%s: %s",kind,mod.name.c_str());
        }
        ImGui::EndChild();
        ImGui::Text("Required files: %.1f MiB (verified cached files are reused)",double(view.total)/dkr::mods::MiB);
        ImGui::TextWrapped("Verified session mods use a separate online cache. Optionally keep copies in your own Mods/Hacks library; new copies are inactive until you enable them. Existing mod selections, textures and saves stay unchanged. ROMs and native executable code are never downloaded.");
        ImGui::Checkbox("Keep in Mods/Hacks for offline play",&g_online_keep_downloads);
        ImGui::Dummy({0,8});
        if(ImGui::Button("DOWNLOAD & JOIN",{ImGui::GetContentRegionAvail().x,42}))g_online_mod_sync->consent(true,g_online_keep_downloads);
        if(ImGui::Button("DECLINE",{ImGui::GetContentRegionAvail().x,42}))g_online_mod_sync->consent(false);
    } else {
        const auto cursor=ImGui::GetCursorScreenPos();const float angle=static_cast<float>(ImGui::GetTime()*4);
        auto* draw=ImGui::GetWindowDrawList();draw->PathArcTo({cursor.x+12,cursor.y+12},9,angle,angle+4.5F,24);
        draw->PathStroke(ImGui::GetColorU32(kWarm),0,3);ImGui::Dummy({28,26});
        if(view.phase==SyncPhase::Downloading && view.total)
            ImGui::ProgressBar(std::clamp(float(view.received)/float(view.total),0.0F,1.0F),{ImGui::GetContentRegionAvail().x,28});
        ImGui::TextWrapped("The launcher remains responsive. Each mod is checked against your own imported Game Pak before you are admitted to the lobby.");
        if(ImGui::Button("CANCEL",{ImGui::GetContentRegionAvail().x,42})) {
            g_online_mod_sync->cancel();g_online_mod_host_request.reset();g_online_mod_watching=false;
            if(dkr::runtime::netplay::session().active())dkr::runtime::netplay::session().disconnect("Session mod preparation cancelled.");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}
