// Shared launcher/overlay look; update work runs only from the launcher.
std::string g_update_notice_seen;
std::string g_update_action_status;
bool g_update_notice_requested = false;
bool UpdatesLocked(bool launcher);

void DrawPlayUpdateButton(float width) {
    auto* updates = dkr::updates::service();
    const bool busy = updates && updates->snapshot().busy;
    const bool locked = !updates || UpdatesLocked(true);
    ImGui::BeginDisabled(busy || locked);
    const char* label = width < PaddockImportButtonWidth("Check for updates") ? "Updates###play-update-check"
                                                                              : "Check for updates###play-update-check";
    const bool pressed = PaddockImportButton(label, width, true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!updates)
            ImGui::SetTooltip("Automatic checks aren't available here. Open Releases in About DKR-R.");
        else if (locked)
            ImGui::SetTooltip("Finish your lobby or import first.");
        else if (busy)
            ImGui::SetTooltip("An update check or download is already running.");
    }
    if (pressed && !busy && !locked) {
        g_update_action_status.clear();
        updates->check();
        // Manual checks always show a result, including up-to-date and errors.
        g_update_notice_requested = true;
    }
}

std::string UpdateDisplayMessage(const dkr::updates::Snapshot& snapshot) {
    using dkr::updates::State;
    switch (snapshot.state) {
    case State::Idle:
        return "Not checked yet.";
    case State::Checking:
        return "Checking for updates...";
    case State::Current:
        return "You're up to date.";
    case State::Available:
        return snapshot.manual_only ? "Open Releases to install this update manually." : "An update is available.";
    case State::Downloading:
        return "Downloading update...";
    case State::Ready:
        return snapshot.message.find("installer has been opened") != std::string::npos
                   ? "Confirm the update in Android's installer."
                   : "Update ready to install.";
    case State::Preparing:
        return "Preparing update...";
    case State::Failed:
        return dkr::runtime::copy::error_summary(snapshot.message, dkr::runtime::copy::Area::Updates);
    }
    return {};
}

bool UpdatesLocked(bool launcher) {
    return !launcher || dkr::runtime::netplay::session().active() || g_legacy_imports.snapshot().busy ||
           g_mod_launch.snapshot().modal || OnlineModSelectionLocked() || DialogJobRunning();
}
void DrawUpdateActions(float width, bool launcher) {
    auto* updates = dkr::updates::service();
    if (!updates)
        return;
    const auto snapshot = updates->snapshot();
    bool locked = UpdatesLocked(launcher);
    if (!snapshot.notes.empty() && DetailRaceButton("VIEW RELEASE NOTES", width))
        SDL_OpenURL(snapshot.notes.c_str());
    if (!snapshot.notes.empty())
        PaddockGap(8.0F);
    if (snapshot.state == dkr::updates::State::Available && !snapshot.manual_only) {
        if (DetailRaceButton("DOWNLOAD UPDATE", width, 0x14BFA3, locked || snapshot.busy))
            updates->download();
    } else if (snapshot.state == dkr::updates::State::Ready) {
        if (DetailRaceButton("INSTALL AND RESTART", width, 0x14BFA3, locked || snapshot.busy)) {
            if (dkr::runtime::portable::supported() &&
                dkr::runtime::portable::enabled() != dkr::runtime::portable::enabled_at_startup())
                g_update_action_status = "Restart DKR-R before installing this update.";
            else if (!updates->install(g_update_action_status) && !snapshot.notes.empty())
                g_update_action_status += " Your download is kept. Open Releases for manual installation.";
        }
    } else if (snapshot.state == dkr::updates::State::Failed && !snapshot.version.empty() && !snapshot.manual_only) {
        if (DetailRaceButton("RETRY DOWNLOAD", width, 0x0A6EA1, locked || snapshot.busy))
            updates->download();
    }
    if (snapshot.busy && snapshot.state != dkr::updates::State::Preparing) {
        PaddockGap(8.0F);
        if (DetailRaceButton(snapshot.state == dkr::updates::State::Downloading ? "PAUSE DOWNLOAD" : "CANCEL CHECK",
                             width))
            updates->cancel();
    }
    if (locked) {
        PaddockGap(8.0F);
        DetailText(!launcher                                   ? "Return to the launcher to update."
                   : dkr::runtime::netplay::session().active() ? "Leave the lobby before updating."
                                                               : "Finish the current import before updating.",
                   width, kSetMuted, 0);
    }
    if (!g_update_action_status.empty()) {
        PaddockGap(8.0F);
        DrawFriendlyStatus(g_update_action_status, width, dkr::runtime::copy::Area::Updates);
    }
}
void DrawUpdatesCard(float width) {
    AboutSectionTitle("Updates", width);
    DetailCard(width, {20.0F, 18.0F}, [&](float inner) {
        auto* updates = dkr::updates::service();
        if (!updates) {
            DetailText("Automatic updates aren't available here. Open Releases to update manually.", inner, kSetMuted);
            if (DetailRaceButton("OPEN RELEASES", inner))
                SDL_OpenURL("https://github.com/ThatGuyMcd/DKR-R/releases");
            return;
        }
        const auto snapshot = updates->snapshot();
        const bool launcher = g_launcher_rom != nullptr;
        auto preferences = snapshot.preferences;
        {
            OlDisabled disabled(snapshot.busy || !launcher, 0.6F);
            if (SettingsCheck("##updates-startup", "Check for updates at startup", &preferences.automatic, inner))
                updates->preferences(preferences);
            PaddockGap(8.0F);
            if (SettingsCheck("##updates-beta", "Include preview updates", &preferences.beta, inner)) {
                preferences.skipped.clear();
                updates->preferences(preferences);
                updates->check();
            }
        }
        PaddockGap(16.0F);
        DetailText("Installed: " + std::string(DKR_RELEASE_VERSION), inner, kSetMuted, 8);
        if (!snapshot.version.empty())
            DetailText("Available: " + snapshot.version, inner, kSetText, 8);
        DetailText(UpdateDisplayMessage(snapshot), inner, kSetMuted, 8);
        if (snapshot.state == dkr::updates::State::Failed || snapshot.manual_only)
            DrawTechnicalDetails(snapshot.message);
        if (!snapshot.installation_result.empty())
            DrawFriendlyStatus(snapshot.installation_result, inner, dkr::runtime::copy::Area::Updates);
        if (!snapshot.last_check.empty())
            DetailText("Last checked: " + snapshot.last_check, inner, kSetMuted, 8);
        if (snapshot.state == dkr::updates::State::Downloading) {
            float fraction = snapshot.total ? float(double(snapshot.received) / double(snapshot.total)) : 0;
            ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), {inner, 24});
            PaddockGap(8.0F);
        }
        if (DetailRaceButton("CHECK FOR UPDATES", inner, 0x0A6EA1, !launcher || snapshot.busy))
            updates->check();
        PaddockGap(12.0F);
        DrawUpdateActions(inner, launcher);
        PaddockGap(12.0F);
        DetailText("Your saves, mods, texture packs and settings are kept.", inner, kSetMuted, 0);
    });
}
void DrawUpdateNotice(dkr::runtime::ui::StartupResult& result, bool& running) {
    auto* updates = dkr::updates::service();
    if (!updates)
        return;
    const auto snapshot = updates->snapshot();
    if (updates->install_handoff_ready()) {
        SaveSettings();
        result.lifecycle_request = dkr::runtime::ui::LifecycleRequest::Exit;
        running = false;
        return;
    }
    if (dkr::runtime::copy::open_update_result(g_update_notice_requested,
                                               snapshot.state == dkr::updates::State::Available,
                                               snapshot.version == snapshot.preferences.skipped,
                                               snapshot.version == g_update_notice_seen, UpdatesLocked(true))) {
        if (snapshot.state == dkr::updates::State::Available)
            g_update_notice_seen = snapshot.version;
        g_update_notice_requested = true;
    }
    if (g_update_notice_requested) {
        ImGui::OpenPopup("DKR-R updates");
        g_update_notice_requested = false;
    }
    ImGui::SetNextWindowSize({std::min(620.0F, ImGui::GetIO().DisplaySize.x - 32.0F), 0}, ImGuiCond_Appearing);
    if (!BeginPaddedModal("DKR-R updates", ImGuiWindowFlags_AlwaysAutoResize))
        return;
    auto width = ImGui::GetContentRegionAvail().x;
    DetailHeading("DKR-R UPDATES", width, kSetWarm);
    if (!snapshot.version.empty() && snapshot.state != dkr::updates::State::Current)
        DetailText("Version " + snapshot.version, width);
    DetailText(UpdateDisplayMessage(snapshot), width);
    if (snapshot.state == dkr::updates::State::Failed || snapshot.manual_only)
        DrawTechnicalDetails(snapshot.message);
    if (snapshot.total)
        DetailText("Download size: " + std::to_string(snapshot.total / 1024 / 1024) + " MiB", width, kSetMuted);
    if (snapshot.state == dkr::updates::State::Downloading) {
        ImGui::ProgressBar(snapshot.total ? float(double(snapshot.received) / double(snapshot.total)) : 0, {width, 24});
        PaddockGap(12.0F);
    }
    DrawUpdateActions(width, true);
    PaddockGap(12.0F);
    if (DetailRaceButton(snapshot.state == dkr::updates::State::Current || snapshot.state == dkr::updates::State::Failed
                             ? "CLOSE"
                             : "LATER",
                         width, 0x0A6EA1, snapshot.state == dkr::updates::State::Preparing))
        ImGui::CloseCurrentPopup();
    if (snapshot.state == dkr::updates::State::Available) {
        PaddockGap(8.0F);
        if (DetailRaceButton("SKIP THIS VERSION", width)) {
            updates->skip();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}
