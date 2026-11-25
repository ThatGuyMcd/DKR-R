const char* SessionModsCaption(bool enabled) {
    return enabled ? "Session mods: ON" : "Session mods: OFF";
}
constexpr float kPlayToolHeight = 40.F, kPlayToolGap = 8.F;
void DrawPlayTools(float width, bool live) {
    const char* speedrun_label = live ? "Leaderboards###play-leaderboards" : "Speedrunning###play-leaderboards";
    const std::array widths{PaddockImportButtonWidth("Achievements", true),
                            PaddockImportButtonWidth(speedrun_label, true),
                            std::max(PaddockImportButtonWidth(SessionModsCaption(true), true),
                                     PaddockImportButtonWidth(SessionModsCaption(false), true)),
                            PaddockImportButtonWidth("Save Manager", true)};
    const float widest = *std::max_element(widths.begin(), widths.end());
    const float full = widths[0] + widths[1] + widths[2] + widths[3] + 3.F * kPlayToolGap;
    const int columns = width >= full ? 4 : width >= 2.F * widest + kPlayToolGap ? 2 : 1;
    const int rows = 4 / columns;
    const float fallback_width = std::min(widest, width);
    const float row_width = columns == 4 ? full : float(columns) * fallback_width + float(columns - 1) * kPlayToolGap;
    const auto at = ImGui::GetCursorScreenPos();
    const float left = at.x + (width - row_width) * .5F;
    float next = left;
    const auto draw = [&](int i, const char* label, int icon) {
        const float button_width = columns == 4 ? widths[std::size_t(i)] : fallback_width;
        const float x = columns == 4 ? next : left + float(i % columns) * (fallback_width + kPlayToolGap);
        next += button_width + kPlayToolGap;
        ImGui::SetCursorScreenPos({x, at.y + float(i / columns) * (kPlayToolHeight + kPlayToolGap)});
        return PaddockImportButton(label, button_width, false, false, icon, true);
    };
    if (draw(0, "Achievements###play-achievements", 1))
        g_page_navigation_request = kPageAchievements;
    if (draw(1, speedrun_label, 4))
        g_page_navigation_request = dkr::library_ui::LeaderboardsPage;
    const bool enabled = dkr::session_content::next_enabled.load();
    const auto caption = std::string(SessionModsCaption(enabled)) + "###play-session-mods";
    if (draw(2, caption.c_str(), 3)) {
        dkr::session_content::next_enabled = !enabled;
        SaveSettings();
    }
    if (draw(3, "Save Manager###play-saves", 2))
        g_page_navigation_request = kPageSaveManager;
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, float(rows) * kPlayToolHeight + float(rows - 1) * kPlayToolGap});
}
