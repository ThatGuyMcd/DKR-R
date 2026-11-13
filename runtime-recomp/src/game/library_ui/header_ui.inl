void DrawLibraryPageHeader(float width,const dkr::store::Snapshot& store) {
    const float refresh=PaddockImportButtonWidth("Refreshing...");
    const float website=PaddockImportButtonWidth("Website");
    const float actions=refresh+website+12.F;
    DrawSharedPageHeader(width,"MOD LIBRARY",store.user.is_null()?"Free community mods for DKR-R":
        dkr::localisation::format("library.signed_in",{{"name",StoreText(store.user,"displayName",100)}}),actions,[&]{
    ImGui::BeginDisabled(store.loading);
    if(PaddockImportButton(store.writing?"Saving...###library-refresh":
            store.loading?"Refreshing...###library-refresh":"Refresh###library-refresh",std::min(width,refresh),true)) {
        g_mod_store.message_page=std::to_string(g_mod_store.page);
        dkr::store::service().refresh();
    }
    if(width>=actions)ImGui::SameLine(0,12.F);else PaddockGap(12.F);
    if(PaddockImportButton("Website###library-website",std::min(width,website),false,true))
        SDL_OpenURL("https://positron-portal.com/dkr-r/index.html");
    ImGui::EndDisabled();
    });
}
