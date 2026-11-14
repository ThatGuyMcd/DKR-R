// Reuses the existing token-authenticated, disposable native UI harness.
bool LibraryManualHandoffCheck() {
    const char* mode = std::getenv("DKR_OWNED_CHECK_STARTUP_UI");
    return !g_launcher_layout_root.empty() && mode &&
           (std::string_view(mode) == "manual" || std::string_view(mode) == "cancel");
}
void LibraryManualHandoffRequest(bool ready, bool& launch) {
    static bool requested = false;
    if (LibraryManualHandoffCheck() && ready && !requested && ImGui::GetFrameCount() > 5) {
        requested = true;
        launch = true;
    }
}
void LibraryCaptureHandoff(SDL_Renderer* renderer, const dkr::library_ui::LauncherFade& fade, bool active) {
    if (!LibraryManualHandoffCheck())
        return;
    static int step = 0;
    const float alpha = fade.opacity(PaddockClock());
    const bool cancel = std::string_view(std::getenv("DKR_OWNED_CHECK_STARTUP_UI")) == "cancel";
    if (!((step == 0 && fade.started()) || (step == 1 && alpha < .85F && alpha > .15F) ||
          (step == 2 && (cancel ? !active : alpha < .1F))))
        return;
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) || w < 1 || h < 1 || w > 4096 || h > 2160)
        return;
    std::vector<unsigned char> pixels(std::size_t(w) * h * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4))
        return;
    auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return;
    const auto path = g_launcher_layout_root / ("handoff-" + std::to_string(step) + ".bmp");
    const bool saved = SDL_SaveBMP(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);
    if (!saved)
        return;
    std::fprintf(stderr, "[startup][handoff-check] captured=%d alpha=%.3f active=%d\n", step, alpha, int(active));
    if (step++ == 1 && cancel) {
        SDL_Event event{};
        event.type = SDL_KEYDOWN;
        event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        event.key.keysym.sym = SDLK_ESCAPE;
        SDL_PushEvent(&event);
    }
}
