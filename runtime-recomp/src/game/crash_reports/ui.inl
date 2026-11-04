// Included after the shared support-page helpers in both ImGui contexts.
void DrawCrashReportingSettings(float width) {
    using namespace dkr::crash_reports;
    auto state = view(); bool enabled = state.consent.choice == 1;
    if (SettingsCheck("##automatic-crash-reports", dkr::localisation::text("support.reporting.enable").data(), &enabled, width))
        choose(enabled, enabled && state.consent.dumps);
    PaddockGap(6.F);
    DetailText(dkr::localisation::text("support.reporting.privacy"), width, kSetMuted);
    if (enabled) {
        bool dumps = state.consent.dumps;
        if (SettingsCheck("##automatic-raw-dumps", dkr::localisation::text("support.reporting.dumps").data(), &dumps, width))
            choose(true, dumps);
        DetailText(dkr::localisation::text("support.reporting.dump_privacy"), width, kSetMuted);
    }
    if (!state.status.empty()) {
        const auto status = dkr::localisation::text(state.sending ? "support.reporting.sending" : state.status);
        DetailText(status, width, kSetMuted, 0.F);
        if (state.queued) DetailText(dkr::localisation::format("support.reporting.count", {{"count", std::to_string(state.queued)}}), width, kSetMuted, 0.F);
    }
    PaddockGap(16.F);
}
void DrawCrashReportingConsent() {
    using namespace dkr::crash_reports;
    if (view().consent.choice != -1) return;
    constexpr const char* name = "##crash-report-consent";
    if (!ImGui::IsPopupOpen(name)) ImGui::OpenPopup(name);
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(740.F, std::max(240.F, display.x - 32.F)), 0.F}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({display.x * .5F, display.y * .5F}, ImGuiCond_Always, {.5F, .5F});
    if (!BeginPaddedModal(name, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        choose(false, false); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return;
    }
    const auto width = ImGui::GetContentRegionAvail().x;
    DetailHeading(dkr::localisation::text("support.reporting.title"), width);
    DetailText(dkr::localisation::text("support.reporting.question"), width);
    DetailText(dkr::localisation::text("support.reporting.privacy"), width, kSetMuted);
    static bool raw_dumps = false;
    SettingsCheck("##consent-raw-dumps", dkr::localisation::text("support.reporting.dumps").data(), &raw_dumps, width);
    DetailText(dkr::localisation::text("support.reporting.dump_privacy"), width, kSetMuted);
    DetailText(dkr::localisation::text("support.reporting.later"), width, kSetMuted);
    const bool narrow = width < 540.F;
    const auto button_width = narrow ? width : (width - 12.F) * .5F;
    if (DetailRaceButton(dkr::localisation::text("support.reporting.decline").data(), button_width) && choose(false, false)) ImGui::CloseCurrentPopup();
    ImGui::SetItemDefaultFocus();
    if (!narrow) ImGui::SameLine(0.F, 12.F);
    if (DetailRaceButton(dkr::localisation::text("support.reporting.accept").data(), button_width) && choose(true, raw_dumps)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
