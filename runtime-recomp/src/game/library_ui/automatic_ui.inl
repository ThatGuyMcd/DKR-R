void DrawSkipLauncherSetting(float width) {
    if (PaddockCheckbox("##skip-launcher", "Skip launcher", &dkr::library_ui::skip_launcher, width))
        SaveSettings();
    PaddockGap(6.F);
    PaddockText(PaddockReading(13.F, false, 1.5F), PaddockRgb(0xABC0CC),
                dkr::localisation::text("play.skip_launcher_help"), width);
    PaddockGap(8.F);
}

bool AutomaticStartHasMods() {
    if (!dkr::session_content::next_enabled.load())
        return false;
    const auto mods = g_legacy_imports.snapshot();
    for (const auto& catalogue : {mods.tracks, mods.characters})
        if (catalogue)
            for (const auto& item : catalogue->tracks)
                if (item.enabled && !item.hidden)
                    return true;
    for (const auto& item : g_code_library.view()->items)
        if (item.selected && item.enabled && item.approved && item.compatibility.empty())
            return true;
    for (const auto& item : g_vehicle_library.view()->items)
        if (item.enabled)
            return true;
    for (const auto& item : dkr::runtime::custom_tracks::tracks())
        if (item.enabled)
            return true;
    for (const auto& item : dkr::runtime::texture_packs::snapshot())
        if (item.enabled && item.compatible)
            return true;
    return dkr::runtime::magic_codes::selected_mask() != 0;
}

bool DrawStartingContent(const dkr::mods::ModLaunchView& state, bool mods, bool syncing, double seconds) {
    auto* draw = ImGui::GetWindowDrawList();
    const auto at = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    auto layout = dkr::library_ui::startup_layout(size.x, size.y, 0);
    const float scale = layout.scale, padding = 26.F * scale, inner = std::max(1.F, layout.panel - 2.F * padding);
    const auto title = dkr::localisation::text(syncing ? "play.syncing_game" : "play.starting_game");
    const auto loading = dkr::localisation::text(syncing ? "play.syncing_status" : "play.loading_mods");
    const auto button = dkr::localisation::text("play.open_launcher");
    const auto stage =
        syncing ? std::string{} : std::string(dkr::localisation::source(dkr::runtime::copy::progress(state.stage)));
    const auto heading = PaddockSign(27.F * scale), subheading = PaddockSign(19.F * scale);
    const auto reading = PaddockReading(13.F * scale, false, 1.4F);
    const auto button_type = PaddockSign(16.F * scale);
    const auto measure = [&](const auto& type, std::string_view text) {
        return type.font->CalcTextSizeA(type.size, FLT_MAX, inner, text.data(), text.data() + text.size());
    };
    const float title_height = measure(heading, title).y, loading_height = measure(subheading, loading).y;
    const float stage_height = mods && !stage.empty() ? std::max(reading.size, measure(reading, stage).y) : 0;
    const float button_height = std::max(44.F, 36.F * scale);
    const float mod_height = mods ? 14.F * scale + loading_height + 11.F * scale + 10.F * scale +
                                        (stage_height > 0 ? 11.F * scale + stage_height : 0)
                                  : 0;
    const float caption = 20.F * scale + title_height + mod_height + 14.F * scale + button_height + 18.F * scale;
    layout = dkr::library_ui::startup_layout(size.x, size.y, caption);
    ImGui::SetCursorPos({(size.x - layout.logo) * .5F, layout.top});
    BrandBlock(layout.logo, layout.logo, false);
    const ImVec2 panel{at.x + (size.x - layout.panel) * .5F, at.y + layout.top + layout.logo + layout.gap};
    draw->AddRectFilled(panel, {panel.x + layout.panel, panel.y + caption}, IM_COL32(42, 44, 48, 173), 9.F * scale);
    float y = panel.y + 20.F * scale;
    const auto line = [&](const auto& type, std::string_view text, float height) {
        const auto extent = measure(type, text);
        const ImVec2 origin{panel.x + padding + std::max(0.F, (inner - extent.x) * .5F), y};
        draw->AddText(type.font, type.size, {origin.x + scale, origin.y + scale}, IM_COL32(0, 0, 0, 140), text.data(),
                      text.data() + text.size(), inner);
        draw->AddText(type.font, type.size, origin, IM_COL32(255, 255, 255, 255), text.data(),
                      text.data() + text.size(), inner);
        y += height;
    };
    line(heading, title, title_height);
    if (mods) {
        y += 14.F * scale;
        line(subheading, loading, loading_height);
        y += 11.F * scale;
        const ImVec2 start{panel.x + padding, y}, end{panel.x + padding + inner, y + 10.F * scale};
        draw->AddRectFilled(start, end, IM_COL32(255, 255, 255, 56), 3.F * scale);
        draw->PushClipRect(start, end, true);
        if (state.progress.measured()) {
            draw->AddRectFilled(start, {start.x + inner * state.progress.fraction(), end.y},
                                IM_COL32(255, 218, 112, 255), 3.F * scale);
        } else {
            const float phase = float(std::fmod(seconds * .45, 1.0));
            const float x = start.x + (phase * 1.3F - .3F) * inner;
            draw->AddRectFilled({x, start.y}, {x + inner * .3F, end.y}, IM_COL32(255, 218, 112, 255), 3.F * scale);
        }
        draw->PopClipRect();
        y += 10.F * scale;
        if (stage_height > 0) {
            y += 11.F * scale;
            line(reading, stage, stage_height);
        }
    }
    y += 14.F * scale;
    const float button_width = std::min(inner, measure(button_type, button).x + 36.F * scale);
    ImGui::SetCursorScreenPos({panel.x + (layout.panel - button_width) * .5F, y});
    ImGui::PushFont(button_type.font);
    const float saved_scale = ImGui::GetCurrentWindow()->FontWindowScale;
    ImGui::SetWindowFontScale(button_type.size / button_type.font->FontSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.F * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.F);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20, 24, 30, 128));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(20, 24, 30, 190));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(20, 24, 30, 220));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 102));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const bool pressed = ImGui::Button(std::string(button).c_str(), {button_width, button_height});
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    ImGui::SetWindowFontScale(saved_scale);
    ImGui::PopFont();
    return pressed;
}

bool DrawAutomaticStart(dkr::library_ui::AutomaticStart& automatic, LauncherBackgroundTexture& background,
                        SDL_Renderer* renderer, double scroll, bool software, dkr::launcher_themes::Theme theme,
                        double seconds, const dkr::mods::ModLaunchView* preview = nullptr, bool syncing = false) {
    BeginMainWindow("DKR-R Starting", ImGuiWindowFlags_NoBackground);
    DrawLauncherBackdrop(background, renderer, scroll, software);
    dkr::launcher_themes::weather(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImGui::GetWindowSize(), theme,
                                  seconds, dkr::launcher_themes::preferences.calendar);
    auto state = preview ? *preview : g_mod_launch.snapshot();
    if (syncing)
        state.stage = "Getting everyone ready...";
    const bool pressed = DrawStartingContent(state, automatic.loading_mods() || syncing, syncing, seconds);
    if (pressed && !syncing) {
        automatic.cancel();
        g_mod_launch.cancel();
    }
    ImGui::End();
    return pressed;
}
