const char* SessionModsCaption(bool enabled) {
    return enabled ? "Session mods: ON" : "Session mods: OFF";
}
constexpr float kPlayToolHeight = 40.F, kPlayToolGap = 8.F;
void DrawPlayTools(float width, bool live) {
    constexpr const char* leaderboard_label = "Leaderboards###play-leaderboards";
    const int count = live ? 4 : 3;
    const std::array widths{PaddockImportButtonWidth("Achievements", true),
                            std::max(PaddockImportButtonWidth(SessionModsCaption(true), true),
                                     PaddockImportButtonWidth(SessionModsCaption(false), true)),
                            PaddockImportButtonWidth("Save Manager", true),
                            live ? PaddockImportButtonWidth(leaderboard_label, true) : 0.F};
    const float widest = *std::max_element(widths.begin(), widths.end());
    const float full = widths[0] + widths[1] + widths[2] + widths[3] + float(count - 1) * kPlayToolGap;
    const int columns = width >= full ? count : width >= 2.F * widest + kPlayToolGap ? 2 : 1;
    const int rows = (count + columns - 1) / columns;
    const float fallback_width = std::min(widest, width);
    const float row_width =
        columns == count ? full : float(columns) * fallback_width + float(columns - 1) * kPlayToolGap;
    const auto at = ImGui::GetCursorScreenPos();
    const float left = at.x + (width - row_width) * .5F;
    float next = left;
    const auto draw = [&](int i, float natural_width, const char* label, int icon) {
        const float button_width = columns == count ? natural_width : fallback_width;
        const int row_buttons = std::min(columns, count - (i / columns) * columns);
        const float indent = float(columns - row_buttons) * (fallback_width + kPlayToolGap) * .5F;
        const float x = columns == count ? next : left + indent + float(i % columns) * (fallback_width + kPlayToolGap);
        next += button_width + kPlayToolGap;
        ImGui::SetCursorScreenPos({x, at.y + float(i / columns) * (kPlayToolHeight + kPlayToolGap)});
        return PaddockImportButton(label, button_width, false, false, icon, true);
    };
    if (draw(0, widths[0], "Achievements###play-achievements", 1))
        g_page_navigation_request = kPageAchievements;
    if (live && draw(1, widths[3], leaderboard_label, 4))
        g_page_navigation_request = dkr::library_ui::LeaderboardsPage;
    const bool enabled = dkr::session_content::next_enabled.load();
    const auto caption = std::string(SessionModsCaption(enabled)) + "###play-session-mods";
    if (draw(count - 2, widths[1], caption.c_str(), 3)) {
        dkr::session_content::next_enabled = !enabled;
        SaveSettings();
    }
    if (draw(count - 1, widths[2], "Save Manager###play-saves", 2))
        g_page_navigation_request = kPageSaveManager;
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, float(rows) * kPlayToolHeight + float(rows - 1) * kPlayToolGap});
}
