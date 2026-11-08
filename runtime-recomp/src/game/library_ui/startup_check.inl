// Pixel qualification is restricted to the existing authenticated disposable
// profile. It uses the real screen and never starts a preparation worker.
bool LibraryStartupCheckEnabled() {
    const char* value=std::getenv("DKR_OWNED_CHECK_STARTUP_UI");
    return !g_launcher_layout_root.empty()&&value&&(std::string_view(value)=="1"||std::string_view(value)=="sync");
}
void DrawAutomaticStartFrame(dkr::library_ui::AutomaticStart& automatic,
    LauncherBackgroundTexture& background,SDL_Renderer* renderer,double scroll,bool software,
    dkr::launcher_themes::Theme theme,double seconds) {
    if(!LibraryStartupCheckEnabled()) {
        DrawAutomaticStart(automatic,background,renderer,scroll,software,theme,seconds);return;
    }
    bool seen=false;dkr::library_ui::AutomaticStart preview(false,seen,false,false);
    preview.loading_mods(g_launcher_layout_step<2);
    dkr::mods::ModLaunchView state;
    state.busy=state.modal=true;
    state.stage="Preparing custom Track Select previews";
    if(g_launcher_layout_step==1)state.progress={7,11};
    const bool sync=std::string_view(std::getenv("DKR_OWNED_CHECK_STARTUP_UI"))=="sync";
    DrawAutomaticStart(preview,background,renderer,scroll,software,theme,seconds,&state,sync);
}
