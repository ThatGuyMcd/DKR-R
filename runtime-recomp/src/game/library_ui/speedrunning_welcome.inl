void DrawSpeedrunningWelcome(bool entered, bool signed_in, bool staff, const dkr::store::Json* catalogue) {
    constexpr const char* popup = "Welcome to Speedrunning!";
    static std::filesystem::path acknowledged;
    const auto marker = g_config_directory / "speedrunning-welcome-seen";
    std::error_code ec;
    if (entered && acknowledged != marker && !std::filesystem::is_regular_file(marker, ec))
        ImGui::OpenPopup(popup);
    if (!ImGui::IsPopupOpen(popup))
        return;

    const auto viewport = ImGui::GetMainViewport()->WorkSize;
    const float modal_width = std::min(790.F, viewport.x - 38.F);
    ImGui::SetNextWindowSize({modal_width, 0.F}, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!BeginPaddedModal(popup, flags))
        return;

    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10.F, 5.F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("Speedrunning guide", {width, std::min(405.F, viewport.y * .62F)}, false);
    const float body = ImGui::GetContentRegionAvail().x;
    OlParagraph("Fancy getting a time on the leaderboards? Use the tabs across the top to have a look around. "
                "Here's how to get your first run posted.",
                16.F, kOlInk, body);
    PaddockGap(9.F);

    const bool columns = body > 630.F;
    const float column = columns ? (body - 22.F) / 2.F : body;
    ImGui::BeginGroup();
    OlParagraph("Where to find things", 18.F, kOlAmber, column, true, 1.25F);
    PaddockGap(3.F);
    for (const auto* line : {"Top Players — See who's leading overall and how the points work.",
                             "Vanilla Tracks — Check the times for the original tracks.",
                             "Custom Tracks — Check the times for tracks made by the community.",
                             "Full Game — See runs for categories like Any%, 100% and All Trophy Races.",
                             "Runs — Start a run under Run. My Submitted Runs shows how your posts are getting on.",
                             "Replays (under Runs) — Watch your saved runs, race a ghost or post an eligible time."}) {
        OlParagraph(line, 15.F, kOlSoft, column, false, 1.3F);
        PaddockGap(2.F);
    }
    if (staff)
        OlParagraph("Review Runs — This is where mods and admins can watch and review verified runs.", 15.F, kOlSoft,
                    column, false, 1.3F);
    ImGui::EndGroup();

    if (columns)
        ImGui::SameLine(0.F, 22.F);
    else
        PaddockGap(8.F);
    ImGui::BeginGroup();
    OlParagraph("Posting your first run", 18.F, kOlAmber, column, true, 1.25F);
    PaddockGap(3.F);
    for (
        const auto* step :
        {"1. Sign in with Discord before you start a run you want to post. Once you're signed in, come back to the Launcher.",
         "2. Go to Runs > Run, pick a track or a Full Game category and hit Start Run.",
         "3. When you finish, hit Post to Leaderboard if it's there. You can also do this from Runs > Replays. "
         "Check Runs > My Submitted Runs to see the status."}) {
        OlParagraph(step, 15.F, kOlSoft, column, false, 1.3F);
        PaddockGap(3.F);
    }
    ImGui::EndGroup();
    PaddockGap(10.F);
    OlParagraph("Just having a go? You can race without signing in. Local runs stay on your device.", 15.F, kOlInk,
                body);
    if (catalogue && catalogue->value("canClaimLocal", false))
        OlParagraph("You can link an eligible local run to your Discord account from Runs > Replays.", 15.F, kOlSoft,
                    body);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    PaddockGap(10.F);
    const bool dismiss = OlButton("Got it", OlTone::Go) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    bool login = false;
    if (!signed_in) {
        ImGui::SameLine();
        login = OlButton("Sign in with Discord");
    }
    if (dismiss || login) {
        acknowledged = marker;
        try {
            dkr::updates::durable_write(marker, "seen\n");
        } catch (const std::exception&) {
            // Keep the acknowledgement for this session if the profile is read-only.
        }
        if (login)
            dkr::store::service().login();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
