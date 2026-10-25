// Included after FinishOnlineLobby. Session selection is temporary: the
// offline libraries and their enable/hide/order files are never written here.
std::string g_stability_retry_invite;
bool g_stability_retry_requested=false,g_stability_retry_watch=false;
bool g_online_keep_downloads = true;
bool g_online_approve_code=false;
std::string g_online_code_offer;
std::string g_online_kept_profile;
bool OnlineModSelectionLocked() {
    return g_online_mod_host_request.has_value();
}

void ReportOnlinePreparationFailure(std::string_view error) {
    if(g_online_mod_notice.fail(error,dkr::runtime::rom::revision_name(g_online_mod_identity.revision),
        dkr::runtime::netplay::rollback_policy::name(CurrentOnlineSynchronization())))
        std::fprintf(stderr,"[online-mods][failure] revision=%s backend=%s stage=%s error=%s\n",
            g_online_mod_notice.revision.c_str(),g_online_mod_notice.backend.c_str(),
            g_online_mod_notice.stage.c_str(),g_online_mod_notice.error.c_str());
}

dkr::mods::online::RuntimePreparation OnlineModRuntimePreparation(
    std::vector<std::filesystem::path> roms,std::optional<dkr::runtime::netplay::SynchronizationMode> host_mode={}) {
    return [roms=std::move(roms),host_mode](auto profile,std::stop_token stop)->std::shared_ptr<const dkr::mods::online::RuntimeResources> {
        // A joining client's RoomSnapshot is not published until AFTER mod
        // admission. Its default mode is not the host's selected backend.
        // Only the host has authoritative rules at this stage. Clients still
        // link/validate the owned dispatcher here, verify identical resources,
        // and enforce the admitted launch descriptor in game_main.
        if((!profile->manifest.code.empty()||dkr::mods::online::car_count(profile->manifest))&&host_mode &&
           *host_mode!=dkr::runtime::netplay::SynchronizationMode::ExperimentalRollback)
            throw dkr::mods::Error("Custom cars and Magic Codes require Rollback.");
        if(!profile->manifest.code.empty())std::fprintf(stderr,"[online-mods][code-admission] role=%s backend=%s packages=%zu\n",
            host_mode?"host":"joining-client",host_mode?dkr::runtime::netplay::rollback_policy::name(*host_mode):"awaiting-admitted-rules",
            profile->manifest.code.size());
        return dkr::mods::online::with_matching_game_pak(roms,profile->manifest.revision,stop,
            [](const auto& rom){return dkr::runtime::rom::inspect(rom);},
            [&](const auto& rom){
                const auto resources=dkr::mods::online::RuntimeResources::prepare(profile,rom,stop,DKR_RELEASE_VERSION);
                if(resources->code())dkr::runtime::code_mods::validate_online(*resources->code(),dkr::runtime::rom::inspect(rom).revision);
                return resources;
            });
    };
}

dkr::mods::online::SyncSettings OnlineModSettings() {
    auto settings=g_online_mod_settings;
    settings.offline_legacy_root=g_config_directory/"mods"/"legacy";
    settings.offline_tracks_root=dkr::runtime::custom_tracks::directory();
    settings.offline_code_root=g_config_directory/"mods"/"custom-magic-codes";
    settings.host_version=DKR_RELEASE_VERSION;
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
       OnlineModSelectionLocked() || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || g_mod_launch.snapshot().modal) {
        g_online_action_status="Finish the current operation and select a supported ROM before hosting.";return false;
    }
    if(const char* reason=dkr::runtime::netplay::rollback_policy::admission_error(CurrentOnlineSynchronization(),session().owned_backend_available())) {
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
    request.rules.development=g_online_development&&request.rules.synchronization==SynchronizationMode::ExperimentalRollback;
    request.manifest=BuildNetplayManifest(g_online_mod_identity);
    request.save_mode=static_cast<dkr::runtime::saves::OnlineSaveSeedMode>(g_online_save_seed_mode);
    request.room=g_online_room_name;request.name=g_online_player_name;
    std::vector<std::filesystem::path> labs;
    for(const auto& track:dkr::runtime::custom_tracks::tracks())if(track.enabled)labs.push_back(track.source);
    auto settings=OnlineModSettings();
    if(!g_online_mod_sync->prepare_host(settings,g_config_directory/"mods"/"legacy",std::move(labs),
            std::string(dkr::mods::online::asset_revision(g_online_mod_identity.revision)),true,
            OnlineModRuntimePreparation(settings.imported_roms,request.rules.synchronization))) {
        g_online_action_status="The previous mod operation is stopping. Try creating the lobby again in a moment.";return false;
    }
    g_online_mod_host_request=std::move(request);
    g_online_mod_notice.clear();
    std::fprintf(stderr,"[online-mods][host] revision=%s asset-revision=%s backend=%s preparation-start\n",
        dkr::runtime::rom::revision_id(g_online_mod_identity.revision).data(),
        dkr::mods::online::asset_revision(g_online_mod_identity.revision).data(),
        dkr::runtime::netplay::rollback_policy::name(CurrentOnlineSynchronization()));
    g_online_action_status="Preparing lobby mods...";
    return true;
}

void PumpOnlineMods() {
    if(!g_online_mod_sync)return;
    using namespace dkr::mods::online;
    using namespace dkr::runtime::netplay;
    auto view=g_online_mod_sync->snapshot();
    g_online_mod_notice.progress(view.stage);
    const auto network=session().view();
    if(g_stability_retry_requested&&!dkr::lobby_stability::snapshot(g_online_mod_sync.get()).running) {
        g_stability_retry_requested=false;std::string error;
        {auto p=dkr::runtime::spectators::party_link(&session());std::scoped_lock lock(p->mutex);p->join_watch=g_stability_retry_watch;}
        if(session().join(g_stability_retry_invite,g_online_player_name,error)){g_online_mod_notice.clear();g_online_mod_watching=false;}
        else g_online_action_status=error;
        return;
    }
    if(view.phase==SyncPhase::Verified && view.profile && (view.kept_offline||!view.keep_error.empty()) &&
       network.state!=ConnectionState::Loading && network.state!=ConnectionState::Running && g_online_kept_profile!=view.profile->digest) {
        g_online_kept_profile=view.profile->digest;
        if(view.kept_offline) {
            g_legacy_imports.refresh();g_code_library.refresh();dkr::runtime::custom_tracks::reload();
            g_online_action_status="Mods saved in Mods / Hacks. New offline copies start disabled.";
            std::fprintf(stderr,"[online-mods][offline-keep] complete manifest=%s\n",view.profile->digest.c_str());
        } else if(!view.keep_error.empty()) {
            g_online_action_status="Joined the lobby, but couldn't keep offline mod copies: "+view.keep_error;
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
            g_online_action_status="Preparation cancelled because the lobby, ROM or Magic Codes changed.";
        } else if(view.phase==SyncPhase::HostReady) {
            const bool modded=view.manifest && has_mods(*view.manifest);
            const auto request=*g_online_mod_host_request;g_online_mod_host_request.reset();
            if(modded && (!view.profile || !view.runtime)) {
                g_online_action_status="Couldn't prepare the host's mods.";g_online_mod_sync->cancel();
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
    if(joining&&!network.invite.empty()) {
        g_stability_retry_invite=network.invite;
        auto p=dkr::runtime::spectators::party_link(&session());std::scoped_lock lock(p->mutex);g_stability_retry_watch=p->join_watch;
    }
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
    ImGui::TextUnformatted(failed?"COULDN'T PREPARE LOBBY MODS":review?"THIS LOBBY NEEDS MODS":"PREPARING LOBBY MODS");
    ImGui::Dummy({0,8});
    if(failed) {
        DrawColoredWrapped(kWarm, dkr::runtime::copy::error_summary(g_online_mod_notice.error, dkr::runtime::copy::Area::Mods));
        DrawTechnicalDetails("ROM: " + g_online_mod_notice.revision + "\nOnline mode: " +
            g_online_mod_notice.backend + "\nStage: " + g_online_mod_notice.stage + "\n" + g_online_mod_notice.error);
        ImGui::TextWrapped("Offline saves and mod selections weren't changed.");
        if(ImGui::Button("COPY JOIN DIAGNOSTICS",{ImGui::GetContentRegionAvail().x,36})) {
            const auto details=std::string("DKR-R ")+DKR_RELEASE_VERSION+"\n"+dkr::lobby_stability::report(g_online_mod_sync.get())+
                "\nROM: "+g_online_mod_notice.revision+"\nBackend: "+g_online_mod_notice.backend+"\n"+g_online_mod_notice.error;
            ImGui::SetClipboardText(details.c_str());
        }
        if(!g_stability_retry_invite.empty()&&view.phase==SyncPhase::Failed&&!g_online_mod_host_request&&dkr::lobby_stability::snapshot(g_online_mod_sync.get()).role=="client") {
            ImGui::TextWrapped("Retry reuses verified files and repairs damaged private cache entries. Review the host's current mods again.");
            if(ImGui::Button("RETRY JOIN",{ImGui::GetContentRegionAvail().x,42})) {
                g_online_mod_sync->cancel();g_stability_retry_requested=true;ImGui::CloseCurrentPopup();
            }
        }
        if(ImGui::Button("DISMISS",{ImGui::GetContentRegionAvail().x,42})) {
            g_online_mod_notice.clear();g_online_mod_sync->cancel();
            g_online_mod_host_request.reset();g_online_mod_watching=false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();return;
    }
    ImGui::TextWrapped("%s",dkr::runtime::copy::progress(view.stage).c_str());
    if(busy) {
        const auto d=dkr::lobby_stability::snapshot(g_online_mod_sync.get());const auto now=std::chrono::steady_clock::now();
        if(view.stage!=dkr::runtime::copy::progress(view.stage))ImGui::TextWrapped("%s",view.stage.c_str());
        ImGui::Text("Elapsed %.0fs | Stage %.0fs",std::chrono::duration<double>(now-d.began).count(),std::chrono::duration<double>(now-d.phase_began).count());
        if(view.phase==SyncPhase::Downloading&&view.total) {
            ImGui::Text("%.1f / %.1f MiB | Downloaded %.1f MiB | Reused %.1f MiB",double(view.received)/dkr::mods::MiB,double(view.total)/dkr::mods::MiB,double(d.wire)/dkr::mods::MiB,double(d.cached)/dkr::mods::MiB);
            ImGui::Text("Transfer rate %.2f MiB/s | Gap repairs %llu",d.bytes_per_second/dkr::mods::MiB,static_cast<unsigned long long>(d.retries));
        }
    }
    if(review && view.manifest) {
        if(g_online_code_offer!=view.offer){g_online_code_offer=view.offer;g_online_approve_code=false;}
        ImGui::TextWrapped("Download these mods from the host to join?");
        BeginPaddedChild("Required mods",{ImGui::GetContentRegionAvail().x,std::min(230.0F,40.0F+(view.manifest->content.size()+view.manifest->code.size())*28.0F)},true);
        for(const auto& mod:view.manifest->content) {
            const char* kind=mod.kind==dkr::mods::online::CarContent?"Car":mod.kind==Kind::Character?"Character":mod.kind==Kind::TrackLab?"Track Lab":"Track";
            ImGui::TextWrapped("%s: %s",kind,mod.name.c_str());
        }
        for(const auto& code:view.manifest->code)ImGui::TextWrapped("Custom Magic Code: %s (%s)",code.name.c_str(),code.version.c_str());
        ImGui::EndChild();
        ImGui::Text("Required files: %.1f MiB (installed files are reused)",double(view.total)/dkr::mods::MiB);
        ImGui::TextWrapped("Offline copies are saved after joining and start disabled. Your current selections and saves are kept.");
        ImGui::Checkbox("Keep downloaded mods for offline play",&g_online_keep_downloads);
        if(!view.manifest->code.empty()) {
            ImGui::TextWrapped("This lobby runs executable mod code. It can change gameplay and game memory. Only join if you trust the host and mod authors.");
            ImGui::Checkbox("Allow this lobby's exact code mods and settings",&g_online_approve_code);
        }
        ImGui::Dummy({0,8});
        ImGui::BeginDisabled(!view.manifest->code.empty()&&!g_online_approve_code);
        if(ImGui::Button("DOWNLOAD & JOIN",{ImGui::GetContentRegionAvail().x,42}))g_online_mod_sync->consent(true,g_online_keep_downloads,g_online_approve_code?view.offer:std::string{});
        ImGui::EndDisabled();
        if(ImGui::Button("CANCEL",{ImGui::GetContentRegionAvail().x,42}))g_online_mod_sync->consent(false);
    } else {
        const auto cursor=ImGui::GetCursorScreenPos();const float angle=static_cast<float>(ImGui::GetTime()*4);
        auto* draw=ImGui::GetWindowDrawList();draw->PathArcTo({cursor.x+12,cursor.y+12},9,angle,angle+4.5F,24);
        draw->PathStroke(ImGui::GetColorU32(kWarm),0,3);ImGui::Dummy({28,26});
        if(view.phase==SyncPhase::Downloading && view.total)
            ImGui::ProgressBar(std::clamp(float(view.received)/float(view.total),0.0F,1.0F),{ImGui::GetContentRegionAvail().x,28});
        if(ImGui::Button("CANCEL",{ImGui::GetContentRegionAvail().x,42})) {
            g_online_mod_sync->cancel();g_online_mod_host_request.reset();g_online_mod_watching=false;
            if(dkr::runtime::netplay::session().active())dkr::runtime::netplay::session().disconnect("Session mod preparation cancelled.");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}
