void DrawCloudStatusGlyph(ImDrawList* draw, ImVec2 centre, dkr::cloud_saves::SyncStatus status, bool enabled) {
    using dkr::cloud_saves::SyncStatus;
    const bool animate = !dkr::runtime::achievements::view()->preferences.reduced_motion;
    const float phase = animate ? float(std::fmod(ImGui::GetTime(), 10.0)) * 6.2831853F : 0.F;
    const auto point = [&](float x, float y) { return ImVec2{centre.x + x, centre.y + y}; };
    const unsigned colour = status == SyncStatus::Synced        ? 0x5CE1B8
                            : status == SyncStatus::Error       ? 0xFFB366
                            : status == SyncStatus::Unreachable ? 0x8EBDD3
                            : enabled                           ? 0xB9E7F5
                                                                : 0x829EAB;
    const auto ink = PaddockCol(colour);
    draw->PathLineTo(point(-7.F, 5.F));
    draw->PathBezierCubicCurveTo(point(-15.F, 5.F), point(-15.F, -5.F), point(-8.F, -5.F));
    draw->PathBezierCubicCurveTo(point(-5.F, -13.F), point(5.F, -12.F), point(8.F, -5.F));
    draw->PathBezierCubicCurveTo(point(15.F, -5.F), point(15.F, 5.F), point(8.F, 5.F));
    draw->PathStroke(ink, ImDrawFlags_Closed, 1.6F);

    const auto badge = point(8.F, 5.F);
    if (status == SyncStatus::Unreachable) {
        const float offset = std::sin(phase * 0.2F) * 1.8F;
        const auto first = point(-11.F + offset, 9.F);
        const auto last = point(11.F + offset, -10.F);
        draw->AddLine(first, last, PaddockCol(0x0A2938), 4.F);
        draw->AddLine(first, last, ink, 2.F);
    } else if (status != SyncStatus::Idle) {
        draw->AddCircleFilled(badge, 6.F, PaddockCol(0x0A2938));
        if (status == SyncStatus::Syncing) {
            draw->PathArcTo(badge, 4.5F, phase, phase + 4.5F, 14);
            draw->PathStroke(ink, 0, 1.7F);
        } else if (status == SyncStatus::Error) {
            const float radius = 4.8F + std::sin(phase * 0.2F) * 0.9F;
            draw->AddCircle(badge, radius, ink, 16, 1.25F);
            draw->AddLine(point(8.F, 2.3F), point(8.F, 5.3F), ink, 1.4F);
            draw->AddCircleFilled(point(8.F, 7.2F), 0.8F, ink);
        } else {
            const ImVec2 tick[]{point(4.5F, 5.F), point(7.F, 7.4F), point(12.F, 2.3F)};
            draw->AddPolyline(tick, 3, ink, 0, 1.8F);
        }
    }
}

void CloudControlTooltip(std::string_view action, std::string_view detail = {}) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || action.empty())
        return;
    const dkr::runtime::ui::ScopedDialogStyle style({12.F, 8.F}, 10.F);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 275.F);
    ImGui::TextUnformatted(action.data(), action.data() + action.size());
    if (!detail.empty() && detail != action)
        ImGui::TextUnformatted(detail.data(), detail.data() + detail.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

constexpr float kPlayCloudControlHeight = 36.F;
constexpr float kPlayCloudGap = 6.F;
constexpr float kPlayCloudIconWidth = 42.F;

float PlayCloudSwitchWidth() {
    return std::ceil(PaddockMeasure(PaddockReading(13.F, true, 1.3F), dkr::localisation::source("Cloud Saves"))) + 61.F;
}

void DrawPlayCloudSaves(float width, bool locked) {
    using dkr::cloud_saves::SyncStatus;
    auto& service = dkr::store::service();
    const auto view = service.view();
    const auto account = dkr::store::text(view->user, "id", 160);
    const bool enabled = !account.empty() && view->cloud_saves_account == account;
    const auto status = account.empty() ? SyncStatus::Idle : view->cloud_sync;
    const bool busy = SharedSaveBusy() || status == SyncStatus::Syncing;
    const bool blocked = locked || busy || view->signing_in;
    const auto caption = dkr::localisation::source("Cloud Saves");
    const auto type = PaddockReading(13.F, true, 1.3F);
    const bool inline_icon = width >= kPlayCloudIconWidth + kPlayCloudGap + 92.F;
    const float switch_width =
        std::min(width - (inline_icon ? kPlayCloudIconWidth + kPlayCloudGap : 0.F), PlayCloudSwitchWidth());

    ImGui::BeginDisabled(blocked);
    const auto toggle = PaddockBeginPress("##play-cloud-opt-in", {switch_width, kPlayCloudControlHeight});
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(toggle.min, toggle.max, PaddockCol(toggle.hovered ? 0x153C4C : 0x0A2938), 8.F);
    draw->AddRect(toggle.min, toggle.max, PaddockCol(0x375F71), 8.F);
    const ImVec2 track{toggle.min.x + 10.F, toggle.min.y + 9.F};
    draw->AddRectFilled(track, {track.x + 34.F, track.y + 18.F}, PaddockCol(enabled ? 0x12B99B : 0x32576A), 9.F);
    draw->AddCircleFilled({track.x + (enabled ? 25.F : 9.F), track.y + 9.F}, 6.5F, PaddockCol(0xFFF3C9), 16);
    const auto label = PaddockEllipsize(type, caption, switch_width - 61.F);
    PaddockDrawRun(draw, type, {toggle.min.x + 52.F, toggle.min.y + (kPlayCloudControlHeight - type.line) * 0.5F},
                   PaddockCol(0xFFF6DA), label.data(), label.data() + label.size());
    PaddockEndPress(toggle, 8.F);
    ImGui::EndDisabled();
    const char* toggle_hint = view->signing_in  ? "Waiting for Discord sign-in."
                              : locked          ? "Return to the launcher to change Cloud Saves."
                              : account.empty() ? "Sign in with Discord to enable Cloud Saves."
                              : enabled         ? "Pause Cloud Saves on this device."
                                                : "Enable Cloud Saves on this device.";
    CloudControlTooltip(dkr::localisation::source(toggle_hint));
    if (toggle.pressed) {
        if (account.empty())
            service.login();
        else
            ToggleCloudSaves(account, !enabled);
    }

    if (inline_icon)
        ImGui::SameLine(0.F, kPlayCloudGap);
    ImGui::BeginDisabled(blocked || !enabled);
    const auto sync =
        PaddockBeginPress("##play-cloud-sync", {std::min(width, kPlayCloudIconWidth), kPlayCloudControlHeight});
    draw->AddRectFilled(sync.min, sync.max, PaddockCol(sync.hovered ? 0x153C4C : 0x0A2938), 8.F);
    draw->AddRect(sync.min, sync.max, PaddockCol(0x375F71), 8.F);
    DrawCloudStatusGlyph(draw, {(sync.min.x + sync.max.x) * 0.5F, (sync.min.y + sync.max.y) * 0.5F}, status, enabled);
    PaddockEndPress(sync, 8.F);
    ImGui::EndDisabled();

    const char* sync_hint = status == SyncStatus::Syncing ? "Syncing Cloud Saves..."
                            : locked                      ? "Return to the launcher to sync."
                            : busy                        ? "Waiting for the current save operation."
                            : enabled                     ? "Sync Cloud Saves now"
                                                          : "Enable Cloud Saves to sync.";
    CloudControlTooltip(dkr::localisation::source(sync_hint),
                        account.empty() ? std::string_view{} : dkr::localisation::source(view->cloud_saves_status));
    if (sync.pressed)
        RunCloudSaveSync(account);
}
