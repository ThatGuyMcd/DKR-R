const char* SessionModsCaption(bool enabled) {
    return enabled ? "Session mods: ON" : "Session mods: OFF";
}
float SessionModsWidth() {
    return std::max(PaddockImportButtonWidth(SessionModsCaption(true)),
                    PaddockImportButtonWidth(SessionModsCaption(false)));
}
float PlayToolsHeight(float width) {
    return PaddockImportButtonWidth("Achievements") + SessionModsWidth() + PaddockImportButtonWidth("Save Manager") +
                       32.F <=
                   width
               ? 56.F
               : 192.F;
}
void DrawPlayTools(float width) {
    const float achievements = PaddockImportButtonWidth("Achievements");
    const float saves = PaddockImportButtonWidth("Save Manager");
    const bool row = PlayToolsHeight(width) == 56.F;
    const auto at = ImGui::GetCursorScreenPos();
    if (PaddockImportButton("Achievements###play-achievements", row ? achievements : width, false, false, 1))
        g_page_navigation_request = kPageAchievements;
    ImGui::SetCursorScreenPos(
        row ? ImVec2{at.x + achievements + (width - achievements - saves - SessionModsWidth()) * .5F, at.y}
            : ImVec2{at.x, at.y + 68.F});
    const bool enabled = dkr::session_content::next_enabled.load();
    const auto caption = std::string(SessionModsCaption(enabled)) + "###play-session-mods";
    if (PaddockImportButton(caption.c_str(), row ? SessionModsWidth() : width, false, false, 3)) {
        dkr::session_content::next_enabled = !enabled;
        SaveSettings();
    }
    ImGui::SetCursorScreenPos(row ? ImVec2{at.x + width - saves, at.y} : ImVec2{at.x, at.y + 136.F});
    if (PaddockImportButton("Save Manager###play-saves", row ? saves : width, false, false, 2))
        g_page_navigation_request = kPageSaveManager;
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, PlayToolsHeight(width)});
}
void DrawPlayToolBack(float) {
    if (PaddockTextLink("Back to Play"))
        g_page_navigation_request = kPagePlay;
    PaddockGap(16.F);
}
