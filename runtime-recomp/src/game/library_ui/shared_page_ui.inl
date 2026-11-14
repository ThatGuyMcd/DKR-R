// One page router owns both surfaces. Live capability checks stay in each
// service; presentation does not grant save/import/update permissions.
struct SharedPlaySnapshot {
    std::filesystem::path rom;
    std::string status;
    bool ready = false, launch = false;
    std::vector<RomCatalogEntry> catalogue;
};
SharedPlaySnapshot g_shared_play;
bool LibrarySidebarInputBlocked() {
    return ImGui::GetCurrentContext()->OpenPopupStack.Size != 0 || (g_capture_action != -1 && !g_capture_finished) ||
           g_sdk2_key_capture || g_online_code_keyboard_visible.load() || dkr::runtime::hud::editor::active();
}
bool LibraryContentInputCaptured() {
    return (g_capture_action != -1 && !g_capture_finished) || g_sdk2_key_capture ||
           g_online_code_keyboard_visible.load() || dkr::runtime::hud::editor::active();
}
void DrawSharedUiPage(float width, int page, bool live, const PlayPageContext* launcher = nullptr) {
    LibraryNavigationContentWindow();
    const float top = ImGui::GetCursorPosY();
    // Paired SDL readbacks use the real live page branch inside the existing
    // authenticated, disposable layout harness. Normal startup cannot arm it.
    if (!live && !g_launcher_layout_root.empty()) {
        const char* surface = std::getenv("DKR_OWNED_CHECK_LAYOUT_SURFACE");
        live = surface && std::string_view(surface) == "overlay";
    }
    g_store_in_game = live;
    if (DrawLibraryControlsFieldCheck(width)) {
        LibrarySurfaceProbe(live, page, width, ImGui::GetCursorPosY() - top);
        ImGui::Dummy({0.F, 54.F});
        return;
    }
    if (launcher) {
        g_shared_play.rom = launcher->selected_rom;
        g_shared_play.status = launcher->rom_status;
        g_shared_play.ready = launcher->rom_ready;
        g_shared_play.catalogue = launcher->rom_catalog;
    }
    if (live && !g_shared_play.ready && dkr::runtime::saves::runtime_save_context_ready()) {
        // A direct command-line launch can bypass the launcher's ROM card.
        // Show the running Game Pak without inventing or persisting a filename.
        g_shared_play.ready = true;
        g_shared_play.status = "Running Game Pak";
        g_shared_play.catalogue = {{std::filesystem::path{}, "Loaded Game Pak", RomPathKey({})}};
    }
    PlayPageContext current{g_shared_play.rom, g_shared_play.status, g_shared_play.ready, g_shared_play.catalogue,
                            g_shared_play.launch};
    const auto* play = launcher ? launcher : &current;
    const auto* previous = g_launcher_rom;
    g_launcher_rom = live && page != kPagePlay && page != kPageOnlineMp ? nullptr : play;
    if (page == kPagePlay)
        DrawPlayPage(width, *play, live);
    else if (page == kPageGraphics)
        DrawGraphicsPage(width, live);
    else if (page == kPageSound)
        DrawSoundPage(width, live);
    else if (page == kPageControls)
        DrawControlsPage(width, live);
    else if (page == kPageSaveManager) {
        DrawPlayToolBack(width);
        DrawSaveManagerPage(width, live);
    } else if (page == kPageAchievements) {
        DrawPlayToolBack(width);
        DrawAchievementsPage(width);
    } else if (page == kPageOnlineMp)
        DrawOnlinePage(width, !live, live || play->rom_ready);
    else if (page == kPageModLibrary)
        DrawModLibraryPage(width, live);
    else if (page == kPageModsHacks) {
        DrawModsHacks(width, live);
        LibraryNavigationCheckRendered(live);
        if (g_mods_page.section == 3) {
            PaddockGap(16.F);
            ImGui::BeginDisabled(!live || !dkr::sdk2::available());
            if (PaddockImportButton("Custom Magic Code tools",
                                    std::min(width, PaddockImportButtonWidth("Custom Magic Code tools")))) {
                dkr::sdk2::tools_open = true;
                g_overlay_visible = false;
            }
            ImGui::EndDisabled();
        }
    } else if (page == kPageTextures)
        DrawTexturesPage(width, true);
    else if (page == kPageAbout)
        DrawAboutPage(width, true);
    if ((!g_launcher_layout_root.empty() || g_surface_probe) && g_surface_scroll_bottom)
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
    LibrarySurfaceProbe(live, page, width, ImGui::GetCursorPosY() - top);
    g_launcher_rom = previous;
    if (page != kPagePlay)
        ImGui::Dummy({0.F, 54.F});
}
