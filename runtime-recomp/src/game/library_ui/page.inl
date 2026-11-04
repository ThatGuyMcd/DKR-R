void OpenModLibrary(int section) {
    g_mod_store.page=section;
    g_page_navigation_request=kPageModLibrary;
}

// Keep old internal links working after Textures leaves the sidebar.
bool LibraryRedirectTextures(int& page) {
    if(page!=kPageTextures)return false;
    page=kPageModsHacks;
    g_mods_page.entry_section=g_mods_page.section=dkr::library_ui::TexturesSection;
    return true;
}

void DrawModLibraryPage(float width,bool game_running) {
    const bool locked=dkr::library_ui::writes_locked(game_running,
        dkr::runtime::netplay::session().active(),g_mod_launch.snapshot().modal,
        OnlineModSelectionLocked(),g_vehicle_library.view()->busy);
    DrawModStore(width,locked,game_running);
}
