// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_settings_ui.inl. Presentation only: every setting goes through the
// same renderer configuration and enhancement calls as before.
//
// GRAPHICS in the look of tools/launcher-html (pages/graphics.js,
// styles/graphics.css): Game display with Renderer & performance folded under
// it, then HUD & effects beside Vehicles & scenery, then the footer that
// restores the defaults. The study has fewer settings than the runtime; the
// extra ones sit in the card they belong to, the rarely used ones behind
// disclosures. Accurate keeps only what it always offered.

struct GraphicsPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    bool renderer_open = false;
    bool distances_open = false;
    // Restoring the defaults can be undone until the next graphics change or
    // until the page is left.
    bool can_undo = false;
    std::string status;
    bool hud_save_failed = false;
};
GraphicsPageState g_graphics_page;

SettingsRangeValue GraphicsTimes(int value) { return {std::to_string(value), "x"}; }

// A colour swatch field for the performance overlay's text.
bool GraphicsColourField(const char* id, std::string_view label, ImVec4& colour, float width) {
    SettingsLabel(label, width);
    ImGui::PushID(id);
    const PaddockPress press = PaddockBeginPress("##swatch", {width, 52.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(8.0F);
    PaddockFill(draw, press.min, press.max, radii,
                PaddockMix(PaddockRgb(0x061D2C), PaddockRgb(0x0A2A3D), press.hover));
    PaddockStroke(draw, press.min, press.max, radii,
                  PaddockMix(PaddockRgb(0x2F6A86), PaddockRgb(0x5A97B1), press.hover), 1.0F);
    const ImVec2 swatch{press.min.x + 12.0F, press.min.y + 12.0F};
    PaddockFill(draw, swatch, {swatch.x + 28.0F, swatch.y + 28.0F}, PaddockRound(6.0F),
                ImGui::ColorConvertFloat4ToU32(colour));
    PaddockStroke(draw, swatch, {swatch.x + 28.0F, swatch.y + 28.0F}, PaddockRound(6.0F),
                  PaddockRgb(0xFFFFFF, 60U), 1.0F);
    char hex[16]{};
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", static_cast<int>(colour.x * 255.0F + 0.5F),
                  static_cast<int>(colour.y * 255.0F + 0.5F), static_cast<int>(colour.z * 255.0F + 0.5F));
    const PaddockType type = PaddockReading(16.0F, true, 1.25F);
    PaddockDrawRun(draw, type, {swatch.x + 28.0F + 12.0F, std::round(press.min.y + (52.0F - type.line) * 0.5F)},
                   PaddockRgb(0xFFFFFF), hex, hex + std::strlen(hex));
    PaddockEndPress(press, 8.0F, 0.98F);
    if (press.pressed) ImGui::OpenPopup("##colour");
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0F, 12.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x071C29));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x3C6478));
    if (ImGui::BeginPopup("##colour")) {
        changed = ImGui::ColorPicker4("##picker", &colour.x,
                                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

void DrawGraphicsPage(float available_width, bool live) {
    namespace enh = dkr::runtime::enhancements;
    GraphicsPageState& state = g_graphics_page;
    const int frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() || state.last_frame != frame - 1;
    if (entered) {
        state.context = ImGui::GetCurrentContext();
        state.can_undo = false;
        state.status.clear();
    }
    state.last_frame = frame;

    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    bool changed = false;
    const bool owned_camera_settings_deferred =
        dkr::runtime::netplay::experimental::owned_runtime_active();
    int profile = static_cast<int>(enh::presentation_profile());
    int window = static_cast<int>(config.wm_option);
    int resolution = static_cast<int>(config.res_option);
    int aspect = static_cast<int>(config.ar_option);
    int aa = static_cast<int>(config.msaa_option);
    int hpfb = static_cast<int>(config.hpfb_option);
    int downsample = std::clamp(config.ds_option, 1, 4);

    // Both menu surfaces use the same full-width responsive cards.
    const float width = std::max(available_width, 1.0F);
    // Container queries on the content panel.
    const bool stacked = available_width <= 850.0F;
    const bool single = available_width <= 540.0F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    DrawPageHeading("GRAPHICS");
    PaddockGap(6.0F);
    PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetMuted),
                "Display, effects and scenery.", width);
    PaddockGap(6.0F);
    PaddockText(SetHelpType(), PaddockRgb(kSetMuted),
                live ? "Changes apply while playing unless marked otherwise."
                     : "",
                width);
    PaddockGap(18.0F);

    // ------------------------------------------------------------ game display
    bool modern = enh::modern_options_visible(enh::presentation_profile());
    SettingsCard(width, [&](float inner) {
        SettingsCardHead("Performance presets", "Choose a preset, then select Apply.", inner);
        namespace graphics_presets = dkr::runtime::mobile_graphics;
#if defined(__ANDROID__)
        constexpr bool low_load = true;
#else
        constexpr bool low_load = false;
#endif
        std::vector<SettingsChoice> preset_choices{{"Battery"}, {"Balanced"}, {"Quality"}};
        if (low_load) {
            preset_choices.push_back({"Low load - 30 FPS"});
            preset_choices.push_back({"Low load - 60 FPS"});
        }
        // Shown when the saved settings match no preset; not something Apply can set.
        const int custom = static_cast<int>(preset_choices.size());
        preset_choices.push_back({"Custom", {}, true});
        const auto applied = enh::modern_presentation_enabled()
            ? graphics_presets::matching_preset(config, enh::anisotropy_level(), low_load)
            : std::nullopt;
        const int applied_index = applied ? static_cast<int>(*applied) : custom;
        // Follow the saved settings, but keep a pending pick until it is applied.
        static int preset = 1;
        static int synced = -1;
        if (applied_index != synced) {
            preset = applied_index;
            synced = applied_index;
        }
        SettingsDropdown("performance-preset", "Preset", &preset, preset_choices, inner);
        PaddockGap(8.0F);
        SettingsHelp("Battery: 480p / 30 FPS. Balanced: 480p / 60 FPS. Quality: display resolution / 60 FPS.", inner);
#if defined(__ANDROID__)
        SettingsHelp("Low load uses 240p for slower devices.", inner);
#endif
        PaddockGap(12.0F);
        if (SettingsRaceButton("APPLY PRESET", preset == custom)) {
            if (!enh::modern_presentation_enabled()) {
                enh::set_presentation_profile(enh::PresentationProfile::Modern);
                ApplyProfileGraphics(config, enh::PresentationProfile::Modern);
            }
            const auto chosen = static_cast<graphics_presets::Preset>(preset);
            graphics_presets::apply_preset(config, chosen);
            enh::set_anisotropy_level(graphics_presets::preset_anisotropy(chosen));
            RememberModernGraphics(config);
            profile = static_cast<int>(enh::PresentationProfile::Modern);
            resolution = static_cast<int>(config.res_option);
            aspect = static_cast<int>(config.ar_option);
            aa = static_cast<int>(config.msaa_option);
            hpfb = static_cast<int>(config.hpfb_option);
            downsample = config.ds_option;
            modern = true;
            changed = true;
        }
        PaddockGap(8.0F);
        SettingsHelp("Applying a preset replaces these graphics settings. Filtering applies next game.", inner);
    });
    PaddockGap(18.0F);
    SettingsCard(width, [&](float inner) {
        SettingsCardHead("Game display",
                         modern ? ""
                                : "",
                         inner);
        SettingsColumns fields(inner, single);
        fields.Begin();
        if (SettingsDropdown("presentation", "Presentation style", &profile, {"Accurate", "Modern"},
                             fields.column, false,
                             "Accurate uses the original 30 FPS presentation. Modern enables graphics enhancements.")) {
            // The player is choosing the profile by hand now; retire any note
            // that Track Lab switched it for them.
            g_track_lab_modern_notice.clear();
            const auto old_profile = enh::presentation_profile();
            if (old_profile == enh::PresentationProfile::Modern) RememberModernGraphics(config);
            const auto next_profile = static_cast<enh::PresentationProfile>(profile);
            enh::set_presentation_profile(next_profile);
            ApplyProfileGraphics(config, next_profile);
            resolution = static_cast<int>(config.res_option);
            aspect = static_cast<int>(config.ar_option);
            aa = static_cast<int>(config.msaa_option);
            hpfb = static_cast<int>(config.hpfb_option);
            downsample = std::clamp(config.ds_option, 1, 4);
            changed = true;
        }
        if (!g_track_lab_modern_notice.empty()) {
            PaddockGap(8.0F);
            SettingsHelp(g_track_lab_modern_notice, fields.column, 0xFFCF83);
        }
        fields.End();
        modern = enh::modern_options_visible(enh::presentation_profile());

        fields.Begin();
        changed |= SettingsDropdown("window-mode", "Window mode", &window, {"Windowed", "Fullscreen"},
                                    fields.column, false, "");
        config.wm_option = static_cast<WindowMode>(window);
        fields.End();

        if (modern) {
            fields.Begin();
            changed |= SettingsDropdown("resolution", "Resolution", &resolution,
                                        {"Original (240p)", "Original 2x", "Automatic integer scale"},
                                        fields.column, false,
                                        "Higher resolutions look sharper but use more GPU power.");
            fields.End();
            fields.Begin();
            changed |= SettingsDropdown("aspect", "Aspect ratio", &aspect, {"Original 4:3", "Fit to window"},
                                        fields.column, false,
                                        "Fit to window fills the available screen.");
            fields.End();
        }
        if (enh::modern_presentation_enabled()) {
            int refresh_mode = g_modern_refresh_mode == RefreshRate::Manual ? 1 : 0;
            fields.Begin();
            if (SettingsDropdown("presentation-rate", "Frame rate", &refresh_mode,
                                 {"Match display", "Manual target"}, fields.column, false,
                                 "Matches your screen's refresh rate.")) {
                g_modern_refresh_mode = refresh_mode == 0 ? RefreshRate::Display : RefreshRate::Manual;
                config.rr_option = g_modern_refresh_mode;
                changed = true;
            }
            fields.End();
            if (g_modern_refresh_mode == RefreshRate::Manual) {
                fields.Begin();
                if (SettingsRange("##refresh-target", "Frame-rate target", &g_modern_refresh_target, 30, 500,
                                  fields.column,
                                  [](int value) { return SettingsRangeValue{std::to_string(value), "FPS"}; },
                                  "Higher targets use more CPU and GPU power.")) {
                    g_modern_refresh_target = enh::clamp_presentation_rate(g_modern_refresh_target);
                    config.rr_manual_value = g_modern_refresh_target;
                    changed = true;
                }
                fields.End();
            }
            config.rr_option = g_modern_refresh_mode;
            config.rr_manual_value = g_modern_refresh_target;
        } else {
            config.rr_option = RefreshRate::Original;
            config.rr_manual_value = 30;
        }
        fields.Finish();

        if (!modern) {
            ApplyProfileGraphics(config, enh::PresentationProfile::Accurate);
            PaddockGap(16.0F);
            PaddockInlineNote("Original 4:3 at 30 FPS.",
                              "Switch to Modern to customise graphics, HUD and scenery.",
                              inner);
            return;
        }
        config.res_option = static_cast<Resolution>(resolution);
        config.ar_option = static_cast<AspectRatio>(aspect);

        PaddockGap(18.0F);
        if (!SettingsDisclosure("##renderer", "Advanced graphics", state.renderer_open, inner)) {
            config.msaa_option = static_cast<Antialiasing>(aa);
            config.hpfb_option = static_cast<HighPrecisionFramebuffer>(hpfb);
            config.hr_option = HUDRatioMode::Original;
            config.ds_option = downsample;
            return;
        }
        PaddockGap(16.0F);
        SettingsColumns renderer(inner, single);
        renderer.Begin();
        changed |= SettingsDropdown("anti-aliasing", "Anti-aliasing", &aa,
                                    {"None", "MSAA 2x", "MSAA 4x", "MSAA 8x"}, renderer.column, false,
                                    "Smooths jagged edges. Higher levels use more GPU power.");
        renderer.End();
        renderer.Begin();
        {
            constexpr std::array<int, 5> kAnisotropyLevels{1, 2, 4, 8, 16};
            int anisotropy_index = 0;
            for (std::size_t index = 0; index < kAnisotropyLevels.size(); ++index) {
                if (kAnisotropyLevels[index] == enh::anisotropy_level()) {
                    anisotropy_index = static_cast<int>(index);
                }
            }
            if (SettingsDropdown("anisotropic", "Anisotropic filtering", &anisotropy_index,
                                 {"1x (off)", "2x", "4x", "8x", "16x"}, renderer.column, false,
                                 "Keeps textures sharper at an angle. Applies next game.")) {
                enh::set_anisotropy_level(kAnisotropyLevels[static_cast<std::size_t>(anisotropy_index)]);
                changed = true;
            }
        }
        renderer.End();
        renderer.Begin();
        changed |= SettingsDropdown("high-precision", "Colour precision", &hpfb,
                                    {"Automatic", "On", "Off"}, renderer.column, false,
                                    "Reduces colour banding in effects and fades.");
        renderer.End();
        renderer.Begin();
        changed |= SettingsRange("##downsample", "Supersampling", &downsample, 1, 4, renderer.column,
                                 [](int value) {
                                     return value <= 1 ? SettingsRangeValue{"Off", ""}
                                                       : SettingsRangeValue{std::to_string(value), "x"};
                                 },
                                 "Renders extra pixels for a cleaner image. Higher values use much more GPU power.");
        renderer.End();
        renderer.Begin();
        {
            int bias = static_cast<int>(std::lround(enh::texture_lod_bias() * 20.0F));
            if (SettingsRange("##lod-bias", "Texture sharpness (LOD bias)", &bias, -40, 40, renderer.column,
                              [](int value) {
                                  char text[16]{};
                                  std::snprintf(text, sizeof(text), "%+.2f", static_cast<double>(value) / 20.0);
                                  return SettingsRangeValue{text, ""};
                              },
                              "Negative values sharpen mipmapped textures; positive values soften them.")) {
                enh::set_texture_lod_bias_hundredths(bias * 5);
                changed = true;
            }
        }
        renderer.End();
        renderer.Begin();
        {
#if defined(_WIN32)
            int api_choice = config.api_option == GraphicsApi::D3D12 ? 1
                           : config.api_option == GraphicsApi::Vulkan ? 2 : 0;
            if (SettingsDropdown("graphics-api", "Graphics API", &api_choice,
                                 {"Automatic (recommended)", "Direct3D 12", "Vulkan"}, renderer.column, false,
                                 "Leave on Automatic unless troubleshooting. Applies next game.")) {
                config.api_option = api_choice == 1 ? GraphicsApi::D3D12
                                  : api_choice == 2 ? GraphicsApi::Vulkan : GraphicsApi::Auto;
                changed = true;
            }
#elif defined(__linux__)
            int api_choice = config.api_option == GraphicsApi::Vulkan ? 1 : 0;
            if (SettingsDropdown("graphics-api", "Graphics API", &api_choice,
                                 {"Automatic (recommended)", "Vulkan"}, renderer.column, false,
                                 "Leave on Automatic unless troubleshooting. Applies next game.")) {
                config.api_option = api_choice == 1 ? GraphicsApi::Vulkan : GraphicsApi::Auto;
                changed = true;
            }
#elif defined(__APPLE__)
            int api_choice = 0;
            SettingsDropdown("graphics-api", "Graphics API", &api_choice, {"Metal (automatic)"},
                             renderer.column, true);
#else
            int api_choice = 0;
            SettingsDropdown("graphics-api", "Graphics API", &api_choice, {"Automatic"}, renderer.column, true);
#endif
        }
        renderer.End();
        renderer.Finish();

        PaddockGap(16.0F);
        bool generate_mips = enh::generated_mipmaps_requested();
        if (SettingsCheck("##mipmaps", "Reduce texture shimmering", &generate_mips, inner,
                          "Smooths distant textures. Applies next game.")) {
            enh::set_generated_mipmaps_requested(generate_mips);
            changed = true;
        }
        if (live) {
            ImGui::Indent(36.0F);
            PaddockGap(4.0F);
            if (generate_mips != RT64::generatedMipSessionEnabled())
                SettingsHelp("Change pending. Applies next game.", inner - 36.0F, kSetText);
            const auto stats = RT64::generatedMipStatistics();
            char uploads[256]{};
            std::snprintf(uploads, sizeof(uploads),
                          "Texture uploads this game: %llu generated originals, %llu generated replacements, "
                          "%llu authored mip chains, %llu single-level.",
                          static_cast<unsigned long long>(stats.originals),
                          static_cast<unsigned long long>(stats.replacements),
                          static_cast<unsigned long long>(stats.authored),
                          static_cast<unsigned long long>(stats.singleLevel));
            DrawTechnicalDetails(uploads, "Texture diagnostics");
            ImGui::Unindent(36.0F);
        }

        PaddockGap(16.0F);
        bool fps_overlay = g_fps_overlay_enabled;
        if (SettingsCheck("##fps-overlay", "Show performance overlay", &fps_overlay, inner,
                          "Show FPS and performance details.")) {
            g_fps_overlay_enabled = fps_overlay;
            SaveSettings();
        }
        if (g_fps_overlay_enabled) {
            PaddockGap(16.0F);
            SettingsColumns overlay(inner, single);
            overlay.Begin();
            if (SettingsDropdown("fps-position", "Overlay position", &g_fps_overlay_position,
                                 {"Top left", "Top right", "Bottom left", "Bottom right"}, overlay.column)) {
                SaveSettings();
            }
            overlay.End();
            overlay.Begin();
            if (SettingsDropdown("fps-detail", "Detail preset", &g_fps_overlay_detail,
                                 {"FPS only", "Standard", "Detailed", "Custom"}, overlay.column)) {
                SaveSettings();
            }
            overlay.End();
            overlay.Begin();
            int fps_layout = g_fps_overlay_single_row ? 1 : 0;
            if (SettingsDropdown("fps-layout", "Metric layout", &fps_layout, {"Stacked", "Single row"},
                                 overlay.column)) {
                g_fps_overlay_single_row = fps_layout == 1;
                SaveSettings();
            }
            overlay.End();
            overlay.Begin();
            if (SettingsRange("##fps-font", "Font size", &g_fps_font_size, 16, 64, overlay.column,
                              [](int value) { return SettingsRangeValue{std::to_string(value), "px"}; })) {
                SaveSettings();
            }
            overlay.End();
            overlay.Begin();
            if (GraphicsColourField("fps-fill", "Font fill colour", g_fps_fill_colour, overlay.column)) {
                SaveSettings();
            }
            overlay.End();
            overlay.Begin();
            if (GraphicsColourField("fps-outline", "Font outline colour", g_fps_outline_colour, overlay.column)) {
                SaveSettings();
            }
            overlay.End();
            overlay.Finish();
            if (g_fps_overlay_detail == 3) {
                PaddockGap(16.0F);
                SettingsColumns metrics(inner, single, 8.0F);
                const std::array<std::pair<const char*, bool*>, 8> kMetrics{{
                    {"Frame time", &g_fps_custom_frame_time},
                    {"Simulation rate", &g_fps_custom_simulation},
                    {"Graphics task rate", &g_fps_custom_graphics},
                    {"VI rate", &g_fps_custom_vi},
                    {"Interpolated frame rate", &g_fps_custom_interpolation},
                    {"Audio sample rate", &g_fps_custom_audio},
                    {"Presentation target", &g_fps_custom_target},
                    {"Viewport resolution", &g_fps_custom_resolution},
                }};
                bool custom_changed = false;
                for (const auto& [label, flag] : kMetrics) {
                    metrics.Begin();
                    ImGui::PushID(label);
                    custom_changed |= SettingsCheck("##metric", label, flag, metrics.column);
                    ImGui::PopID();
                    metrics.End();
                }
                metrics.Finish();
                if (custom_changed) SaveSettings();
            }
        }
        config.msaa_option = static_cast<Antialiasing>(aa);
        config.hpfb_option = static_cast<HighPrecisionFramebuffer>(hpfb);
        config.hr_option = HUDRatioMode::Original;
        config.ds_option = downsample;
    });

    if (changed) {
        if (modern) RememberModernGraphics(config);
        ultramodern::renderer::set_graphics_config(config);
        SaveSettings();
        state.can_undo = false;
        state.status.clear();
    }

    // ------------------------------------------------------------ hud, scenery
    if (modern) {
        PaddockGap(18.0F);
        SettingsCardPair("graphics", width, stacked, 18.0F, [&](float inner) {
            SettingsCardHead("HUD & effects", "", inner);
            namespace hud = dkr::runtime::hud;
            int hud_layout = hud::basic_mode(hud::mode()) == hud::LayoutMode::FitToViewport ? 1 : 0;
            if (SettingsDropdown("hud-layout", "HUD layout", &hud_layout, {"4:3", "Fit to window"}, inner, false,
                                 "Choose the HUD area for 1-2 players.")) {
                state.hud_save_failed = !hud::apply_basic_mode(
                    hud_layout == 1 ? hud::LayoutMode::FitToViewport : hud::LayoutMode::Original);
            }
            if (state.hud_save_failed) {
                PaddockGap(6.0F);
                SettingsHelp("Could not save HUD settings. The previous layout is still active.", inner, 0xFFCF83);
            }
            PaddockGap(16.0F);
            int fov_offset = enh::fov_offset();
            ImGui::BeginDisabled(owned_camera_settings_deferred);
            if (SettingsRange("##fov", "Field of view", &fov_offset, enh::kMinimumFovOffset,
                              enh::kMaximumFovOffset, inner,
                              [](int value) {
                                  return SettingsRangeValue{value > 0 ? "+" + std::to_string(value)
                                                          : value < 0 ? "-" + std::to_string(-value) : "0",
                                                            "\xC2\xB0"};
                              },
                              "See more of the track. Racing only.")) {
                enh::set_fov_offset(fov_offset);
                SaveSettings();
            }
            PaddockGap(16.0F);
            ImGui::EndDisabled();
            const auto camera_provider=dkr::runtime::camera::activation();
            SettingsHelp("ModernCamera++ is available as an optional mod in Mods/Hacks.",inner);
            if(live&&camera_provider&&SettingsButton("Camera++ settings (F7)",inner)) {
                OpenCameraTuning();g_overlay_visible.store(false,std::memory_order_release);
            }

            PaddockGap(16.0F);
            bool crt = g_crt_enabled;
            if (SettingsCheck("##crt", "Enable CRT filter", &crt, inner,
                              "Adds a retro screen effect. Choose a filter in Textures.")) {
                g_crt_enabled = crt;
                SaveSettings();
            }
        }, [&](float inner) {
            SettingsCardHead("Vehicles & scenery", "Higher detail can reduce performance.", inner);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            bool diagnostics = dkr::runtime::netplay::experimental::performance::enabled();
            if (SettingsCheck("##rollback-diagnostics", "Record performance details", &diagnostics, inner,
                              "Records performance details in the log. Enable before starting a match.")) {
                dkr::runtime::netplay::experimental::performance::set_enabled(diagnostics);
            }
            PaddockGap(16.0F);
#endif
            if (owned_camera_settings_deferred) {
                SettingsHelp("These detail and scenery choices are yours alone. Online play keeps the shared course geometry and wave grid unchanged.", inner);
                PaddockGap(16.0F);
            }
            const auto check = [&](const char* id, const char* label, bool value, auto setter,
                                   const char* help, bool first = false, bool disabled = false) {
                if (!first) PaddockGap(16.0F);
                ImGui::BeginDisabled(disabled);
                if (SettingsCheck(id, label, &value, inner, help)) {
                    setter(value);
                    SaveSettings();
                }
                ImGui::EndDisabled();
            };
            check("##max-detail", "Maximum vehicle detail", enh::maximum_detail_requested(),
                  enh::set_maximum_detail_enabled, "Use the highest-detail characters and vehicles in every view, online and offline. Split-screen uses more graphics power.", true
#if !defined(DKR_LOCAL_MODEL_DETAIL)
                  , owned_camera_settings_deferred
#endif
                  );
            check("##hub-scenery", "Keep hub scenery rendered", enh::keep_hub_scenery_requested(),
                  enh::set_keep_hub_scenery_enabled, "Keep visible hub scenery from disappearing.");
            check("##track-scenery", "Keep track and boss scenery rendered", enh::keep_track_scenery_requested(),
                  enh::set_keep_track_scenery_enabled,
                  "Keep visible race and boss scenery from disappearing.");
            check("##minigame-scenery", "Keep minigame and battle scenery rendered",
                  enh::keep_minigame_scenery_requested(), enh::set_keep_minigame_scenery_enabled,
                  "Keep visible minigame scenery from disappearing.");
            check("##ultrawide", "Reduce wide-screen scenery pop-in", enh::extended_culling_requested(),
                  enh::set_extended_culling_enabled,
                  "Keeps scenery visible near wide-screen edges.");
            if (enh::extended_culling_requested()) {
                PaddockGap(16.0F);
                ImGui::Indent(36.0F);
                int guard = enh::frustum_guard_percent();
                if (SettingsRange("##guard", "Visibility margin", &guard, 0, 20, inner - 36.0F,
                                  [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; },
                                  "Extra scenery around the edges of the view.")) {
                    enh::set_frustum_guard_percent(guard);
                    SaveSettings();
                }
                ImGui::Unindent(36.0F);
            }
            PaddockGap(18.0F);
            if (!SettingsDisclosure("##distances", "View distances", state.distances_open, inner)) return;
            PaddockGap(16.0F);
            const auto distance = [&](const char* id, const char* label, int value, int maximum, auto setter,
                                      bool disabled = false) {
                ImGui::BeginDisabled(disabled);
                if (SettingsRange(id, label, &value, enh::kMinimumViewDistanceMultiplier, maximum, inner,
                                  GraphicsTimes)) {
                    setter(value);
                    SaveSettings();
                }
                ImGui::EndDisabled();
                PaddockGap(16.0F);
            };
            distance("##view-distance", "Scenery and object distance", enh::view_distance_multiplier(),
                     enh::kMaximumViewDistanceMultiplier, enh::set_view_distance_multiplier);
            int retention = static_cast<int>(enh::scenery_retention_mode());
            if (SettingsDropdown("retention", "Scenery coverage", &retention,
                                 {"Original", "Current area", "Visible + neighbouring areas", "Full forward view"}, inner)) {
                enh::set_scenery_retention_mode(enh::normalise_scenery_retention_mode(retention));
                SaveSettings();
            }
            PaddockGap(16.0F);
            distance("##animated-distance", "Animated scenery distance",
                     enh::animated_scenery_distance_multiplier(), enh::kMaximumAnimatedSceneryMultiplier,
                     enh::set_animated_scenery_distance_multiplier);
            distance("##billboard-distance", "Sprites and effects distance",
                     enh::billboard_effect_distance_multiplier(), enh::kMaximumBillboardEffectMultiplier,
                     enh::set_billboard_effect_distance_multiplier);
            distance("##water-distance", "Water and lava detail distance", enh::water_lava_detail_multiplier(),
                     enh::kMaximumWaterLavaDetailMultiplier, enh::set_water_lava_detail_multiplier,
                     owned_camera_settings_deferred);
            SettingsHelp("Higher distances and coverage can reduce performance. Scenery behind the camera is not kept.",
                         inner);
        });
    }

#if defined(DKR_ENHANCED_WATER)
    // Shared by launcher and in-game overlay; these are local renderer
    // preferences, not synchronized water physics or guest settings.
    PaddockGap(18.0F);
    SettingsCard(width, [&](float inner) {
        namespace water = dkr::runtime::water::enhanced;
        SettingsCardHead("Ray tracing & water", "Customise water, reflections and lava.", inner);
        ImGui::BeginDisabled(!enh::modern_options_visible(enh::presentation_profile()));
        int quality=int(water::requested_quality.load());
        int preset=water::preset();
        if(SettingsDropdown("water-quality", "Water preset", &preset,
            {"Original", "Performance", "Balanced", "High", "Custom"}, inner)) {
            water::apply_preset(preset);quality=int(water::requested_quality.load());SaveSettings();
        }
        PaddockGap(16.0F);
        int reflections=int(water::requested_reflection.load());
        ImGui::BeginDisabled(quality==0);
        if(SettingsDropdown("water-reflections", "Reflections", &reflections,
            {"Sky", "Screen-space", "Hardware ray tracing"}, inner)) {
            water::set_reflection(reflections); SaveSettings();
        }
        PaddockGap(16.0F);
        bool wakes=water::wakes_enabled.load();
        if(SettingsCheck("##water-wakes", "Racer wakes", &wakes, inner)) {
            water::wakes_enabled.store(wakes); SaveSettings();
        }
        PaddockGap(16.0F);
        bool spray=water::spray_enabled.load();
        if(SettingsCheck("##water-spray", "Water spray", &spray, inner)) {
            water::spray_enabled.store(spray); SaveSettings();
        }
        PaddockGap(16.0F);
        static bool lava_advanced=false;
        if(SettingsDisclosure("##lava-lighting", "Lava lighting", lava_advanced, inner)) {
            bool lava_enabled=water::lava_lighting_enabled.load()!=0;
            if(SettingsCheck("##lava-lighting-enabled","Nearby lava glow",&lava_enabled,inner)) {
                water::lava_lighting_enabled.store(lava_enabled?1:0);SaveSettings();
            }
            SettingsHelp("Adds lava glow to nearby walls and ground. Turning it off keeps enhanced water and the lava surface.",inner);
            PaddockGap(12.0F);
            ImGui::BeginDisabled(!lava_enabled);
            const auto lava_percent=[&](const char* id,const char* label,std::atomic<int>& setting,int low,int high,const char* help) {
                int value=setting.load();
                if(SettingsRange(id,label,&value,low,high,inner,
                    [](int v){return SettingsRangeValue{std::to_string(v),"%"};},help)) {
                    setting.store(value);SaveSettings();
                }
                PaddockGap(12.0F);
            };
            lava_percent("##lava-intensity","Lighting intensity",water::lava_lighting,0,300,
                "Brightness of the surrounding glow. Zero turns it off.");
            lava_percent("##lava-range","Light range",water::lava_light_range,25,200,
                "How far the glow reaches.");
            ImGui::EndDisabled();
            lava_percent("##lava-surface-brightness","Lava brightness",water::lava_surface_brightness,25,200,
                "Brightness of the molten surface.");
            lava_percent("##lava-pulse","Pulse strength",water::lava_pulse_strength,0,100,
                "Blends yellow-white heat into orange-red heat. Zero keeps a steady colour.");
            lava_percent("##lava-speed","Pulse speed",water::lava_pulse_speed,25,200,
                "How quickly the glow pulses.");
            SettingsHelp("Nearby glow is lightweight. It doesn't use ray-traced lighting.",inner);
        }
        PaddockGap(16.0F);
        static bool water_advanced=false;
        if(SettingsDisclosure("##water-advanced", "Advanced water settings", water_advanced, inner)) {
            const auto detail=[&](const char* id,const char* label,std::atomic<int>& setting) {
                int value=setting.load()+1;
                if(SettingsDropdown(id,label,&value,{"Preset default", "Low", "Medium", "High"},inner)) {
                    setting.store(value-1);SaveSettings();
                }
                PaddockGap(12.0F);
            };
            detail("water-reflection-detail","Reflection detail",water::reflection_detail);
            detail("water-wake-detail","Wake quality",water::wake_detail);
            detail("water-spray-detail","Spray detail",water::spray_detail);
            const auto percent=[&](const char* id,const char* label,std::atomic<int>& setting,int low,int high,const char* help) {
                int value=setting.load();
                if(SettingsRange(id,label,&value,low,high,inner,
                    [](int v){return SettingsRangeValue{std::to_string(v),"%"};},help)) {
                    setting.store(value);SaveSettings();
                }
                PaddockGap(12.0F);
            };
            percent("##water-ray-range","Hardware reflection distance",water::reflection_distance,25,200,
                "How far reflected objects can appear. Longer distances use more GPU power.");
            percent("##water-reflect-strength","Reflection strength",water::reflection_strength,0,200,
                "Brightness of reflections. Zero turns reflections off.");
            percent("##water-roughness","Surface roughness",water::roughness_scale,25,200,
                "Higher values soften reflections and highlights.");
            percent("##water-ripple-strength","Fine ripple strength",water::ripple_scale,0,200,
                "Strength of small surface ripples.");
            percent("##water-foam-strength","Foam intensity",water::foam_scale,0,200,
                "Amount of surface and shoreline foam.");
            percent("##water-wake-strength","Wake visibility",water::wake_strength,0,200,
                "Visibility of trails behind racers.");
            percent("##water-clarity","Water clarity",water::clarity_scale,25,200,
                "How clearly you can see below the surface.");
            percent("##water-refraction","Refraction strength",water::refraction_scale,0,200,
                "Strength of underwater distortion.");
            percent("##water-waterfall","Waterfall flowing detail",water::waterfall_detail,0,200,
                "Brightness of flowing waterfall detail. Zero uses the original artwork.");
            bool track_colours=water::use_track_colours.load();
            if(SettingsCheck("##water-track-colours","Use track colours",&track_colours,inner)) {
                water::use_track_colours.store(track_colours);SaveSettings();
            }
            PaddockGap(12.0F);
            if(!track_colours) {
                percent("##water-red","Water tint: red",water::tint_red,0,200,"100% keeps the original red component.");
                percent("##water-green","Water tint: green",water::tint_green,0,200,"100% keeps the original green component.");
                percent("##water-blue","Water tint: blue",water::tint_blue,0,200,"100% keeps the original blue component.");
            }
            if(SettingsButton("Reset water appearance")) {water::reset_appearance();SaveSettings();}
            PaddockGap(8.0F);
            if(SettingsButton("Reset water quality")) {water::reset_tuning();SaveSettings();}
            PaddockGap(12.0F);
            bool timings=water::timing_enabled.load();
            if(SettingsCheck("##water-timings","Record water performance",&timings,inner,
                "Records water performance in the log. Applies next game.")) {
                water::timing_enabled.store(timings);SaveSettings();
            }
            PaddockGap(12.0F);
            SettingsHelp("Water presets don't change resolution, frame-rate targets or the reflection method.",inner);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        PaddockGap(16.0F);
        const int active=water::active_reflection.load();
        if(quality!=0 && active>=0 && active!=reflections)
            SettingsHelp(active==0 ? "Current water reflections: sky." :
                active==1 ? "Current water reflections: screen-space." :
                "Current water reflections: hardware ray tracing.",inner);
        if(water::scratch_fallbacks.load())
            SettingsHelp("Water rendering needed a fallback earlier in this game. Details are in the log.",inner);
        if(reflections==int(water::Reflection::Hardware)) {
            const int capability=water::hardware_ray_capability.load();
            SettingsHelp(capability<0 ? "Hardware support is checked when the game starts." :
                capability==0 ? "Hardware ray tracing isn't supported. Using screen-space reflections." :
                "", inner);
            PaddockGap(8.0F);
        }
    });
#endif

    // ------------------------------------------------------------ footer
    PaddockGap(18.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
        PaddockGap(1.0F + 18.0F);
        PaddockText(PaddockSign(19.0F, 1.0F, 0.0F), PaddockRgb(kSetText), "Restore original graphics", width);
        PaddockGap(6.0F);
        SettingsHelp("Switch to Accurate graphics. Your Modern settings are kept.",
                     width);
        PaddockGap(12.0F);
        const bool accurate = !enh::modern_options_visible(enh::presentation_profile());
        if (SettingsRaceButton("RESTORE ORIGINAL GRAPHICS", accurate,
                               "Already using Accurate graphics.")) {
            RememberModernGraphics(config);
            enh::set_presentation_profile(enh::PresentationProfile::Accurate);
            ApplyProfileGraphics(config, enh::PresentationProfile::Accurate);
            ultramodern::renderer::set_graphics_config(config);
            SaveSettings();
            state.can_undo = true;
            state.status = "Original graphics restored. Modern settings kept.";
        }
        if (state.can_undo) {
            ImGui::SameLine(0.0F, 12.0F);
            if (SettingsRaceButton("UNDO", false)) {
                enh::set_presentation_profile(enh::PresentationProfile::Modern);
                ApplyProfileGraphics(config, enh::PresentationProfile::Modern);
                ultramodern::renderer::set_graphics_config(config);
                SaveSettings();
                state.can_undo = false;
                state.status = "Previous graphics settings restored.";
            }
        }
        if (!state.status.empty()) {
            PaddockGap(12.0F);
            SettingsHelp(state.status, width, kSetWarm);
        }
    }
    ImGui::PopStyleVar();
}
