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
    int profile = static_cast<int>(enh::presentation_profile());
    int window = static_cast<int>(config.wm_option);
    int resolution = static_cast<int>(config.res_option);
    int aspect = static_cast<int>(config.ar_option);
    int aa = static_cast<int>(config.msaa_option);
    int hpfb = static_cast<int>(config.hpfb_option);
    int downsample = std::clamp(config.ds_option, 1, 4);

    const float width = std::min(available_width, 1240.0F);
    // Container queries on the content panel.
    const bool stacked = available_width <= 850.0F;
    const bool single = available_width <= 540.0F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    DrawPageHeading("GRAPHICS");
    PaddockGap(6.0F);
    PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetMuted),
                "Adjust the game display and visual detail.", width);
    PaddockGap(6.0F);
    PaddockText(SetHelpType(), PaddockRgb(kSetMuted),
                live ? "Most changes apply to the running game; settings that need a new launch say so."
                     : "Changes made before launch are applied when the adventure begins.",
                width);
    PaddockGap(18.0F);

    // ------------------------------------------------------------ game display
    bool modern = enh::modern_options_visible(enh::presentation_profile());
    SettingsCard(width, [&](float inner) {
        SettingsCardHead("Game display",
                         modern ? "Presentation, window, resolution and frame rate."
                                : "Presentation and window.",
                         inner);
        SettingsColumns fields(inner, single);
        fields.Begin();
        if (SettingsDropdown("presentation", "Presentation style", &profile, {"Accurate", "Modern"},
                             fields.column, false,
                             "Accurate keeps the original look at 30 FPS. Modern unlocks the options on this page.")) {
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
                                    fields.column, false, "Play in a window or use the full screen.");
        config.wm_option = static_cast<WindowMode>(window);
        fields.End();

        if (modern) {
            fields.Begin();
            changed |= SettingsDropdown("resolution", "Resolution", &resolution,
                                        {"Original (240p)", "Original 2x", "Automatic integer scale"},
                                        fields.column, false,
                                        "Higher resolutions draw more pixels and can increase graphics load.");
            fields.End();
            fields.Begin();
            changed |= SettingsDropdown("aspect", "Aspect ratio", &aspect, {"Original 4:3", "Fit to window"},
                                        fields.column, false,
                                        "Fit to window fills wide screens; Original keeps the 4:3 picture.");
            fields.End();
        }
        if (enh::modern_presentation_enabled()) {
            int refresh_mode = g_modern_refresh_mode == RefreshRate::Manual ? 1 : 0;
            fields.Begin();
            if (SettingsDropdown("presentation-rate", "Presentation rate", &refresh_mode,
                                 {"Match display", "Manual target"}, fields.column, false,
                                 "Match display follows your monitor's refresh rate.")) {
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
                                  "Targets above the monitor refresh can reduce input-to-present latency, but "
                                  "cannot add visible refreshes and use more CPU/GPU power.")) {
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
                              "Fit to window, graphics tuning, the HUD layout and scenery options appear in "
                              "Modern. Accurate always uses the release-proven graphics API.",
                              inner);
            return;
        }
        config.res_option = static_cast<Resolution>(resolution);
        config.ar_option = static_cast<AspectRatio>(aspect);

        PaddockGap(18.0F);
        if (!SettingsDisclosure("##renderer", "Renderer & performance", state.renderer_open, inner)) {
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
                                    "Smooths jagged edges. Higher settings cost more GPU time.");
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
                                 "Improves angled track textures. Samplers are rebuilt on the next game launch.")) {
                enh::set_anisotropy_level(kAnisotropyLevels[static_cast<std::size_t>(anisotropy_index)]);
                changed = true;
            }
        }
        renderer.End();
        renderer.Begin();
        changed |= SettingsDropdown("high-precision", "High precision framebuffer", &hpfb,
                                    {"Automatic", "On", "Off"}, renderer.column, false,
                                    "More colour precision for effects and fades.");
        renderer.End();
        renderer.Begin();
        changed |= SettingsRange("##downsample", "Downsampling quality", &downsample, 1, 4, renderer.column,
                                 [](int value) {
                                     return value <= 1 ? SettingsRangeValue{"Off", ""}
                                                       : SettingsRangeValue{std::to_string(value), "x"};
                                 },
                                 "Renders extra pixels before the final image is reduced. Higher values are "
                                 "expensive; Off is recommended for high refresh rates.");
        renderer.End();
        renderer.Begin();
        {
            int bias = static_cast<int>(std::lround(enh::texture_lod_bias() * 20.0F));
            if (SettingsRange("##lod-bias", "Texture LOD bias", &bias, -40, 40, renderer.column,
                              [](int value) {
                                  char text[16]{};
                                  std::snprintf(text, sizeof(text), "%+.2f", static_cast<double>(value) / 20.0);
                                  return SettingsRangeValue{text, ""};
                              },
                              "Applies live. 0.00 is the unbiased default; negative values favour sharper mip "
                              "levels and positive values softer ones. Only textures with mipmaps change.")) {
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
                                 "Applied at the next game launch. Automatic remains the recovery choice.")) {
                config.api_option = api_choice == 1 ? GraphicsApi::D3D12
                                  : api_choice == 2 ? GraphicsApi::Vulkan : GraphicsApi::Auto;
                changed = true;
            }
#elif defined(__linux__)
            int api_choice = config.api_option == GraphicsApi::Vulkan ? 1 : 0;
            if (SettingsDropdown("graphics-api", "Graphics API", &api_choice,
                                 {"Automatic (recommended)", "Vulkan"}, renderer.column, false,
                                 "Applied at the next game launch. Automatic remains the recovery choice.")) {
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
        if (SettingsCheck("##mipmaps", "Generate texture mipmaps", &generate_mips, inner,
                          "Default off. Generates mipmaps for eligible original and PNG pack textures on the "
                          "next game launch; LOD bias then adjusts them live. HUD, shadows, alpha cutouts and "
                          "unsafe subtiles keep their original sampling. Authored DDS mipmaps are preserved.")) {
            enh::set_generated_mipmaps_requested(generate_mips);
            changed = true;
        }
        if (live) {
            ImGui::Indent(36.0F);
            PaddockGap(4.0F);
            char active[160]{};
            std::snprintf(active, sizeof(active), "Active this game: %s.%s",
                          RT64::generatedMipSessionEnabled() ? "on" : "off",
                          generate_mips != RT64::generatedMipSessionEnabled()
                              ? " Change pending: launch a new game to apply." : "");
            SettingsHelp(active, inner - 36.0F, kSetText);
            const auto stats = RT64::generatedMipStatistics();
            char uploads[256]{};
            std::snprintf(uploads, sizeof(uploads),
                          "Texture uploads this game: %llu generated originals, %llu generated replacements, "
                          "%llu authored mip chains, %llu single-level.",
                          static_cast<unsigned long long>(stats.originals),
                          static_cast<unsigned long long>(stats.replacements),
                          static_cast<unsigned long long>(stats.authored),
                          static_cast<unsigned long long>(stats.singleLevel));
            SettingsHelp(uploads, inner - 36.0F);
            ImGui::Unindent(36.0F);
        }

        PaddockGap(16.0F);
        bool fps_overlay = g_fps_overlay_enabled;
        if (SettingsCheck("##fps-overlay", "Show performance overlay", &fps_overlay, inner,
                          "Display game performance statistics while playing.")) {
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
            SettingsCardHead("HUD & effects", "Readability and the look of the game image.", inner);
            namespace hud = dkr::runtime::hud;
            int hud_layout = hud::basic_mode(hud::mode()) == hud::LayoutMode::FitToViewport ? 1 : 0;
            if (SettingsDropdown("hud-layout", "HUD layout", &hud_layout, {"4:3", "Fit to window"}, inner, false,
                                 "For one and two players. HUD size stays at 100%; three and four-player HUDs "
                                 "are unchanged.")) {
                state.hud_save_failed = !hud::apply_basic_mode(
                    hud_layout == 1 ? hud::LayoutMode::FitToViewport : hud::LayoutMode::Original);
            }
            if (state.hud_save_failed) {
                PaddockGap(6.0F);
                SettingsHelp("Could not save HUD settings. The previous layout is still active.", inner, 0xFFCF83);
            }
            PaddockGap(16.0F);
            int fov_offset = enh::fov_offset();
            if (SettingsRange("##fov", "Field of view", &fov_offset, enh::kMinimumFovOffset,
                              enh::kMaximumFovOffset, inner,
                              [](int value) {
                                  return SettingsRangeValue{value > 0 ? "+" + std::to_string(value)
                                                          : value < 0 ? "-" + std::to_string(-value) : "0",
                                                            "\xC2\xB0"};
                              },
                              "Adjusts each gameplay level from its authored camera. Menus, character select "
                              "and cutscenes keep their original framing.")) {
                enh::set_fov_offset(fov_offset);
                SaveSettings();
            }
            PaddockGap(16.0F);
            bool crt = g_crt_enabled;
            if (SettingsCheck("##crt", "Enable CRT overlay", &crt, inner,
                              "Apply a CRT filter to the game image. Launcher text and performance overlays "
                              "stay clear. Pick the filter under Textures.")) {
                g_crt_enabled = crt;
                SaveSettings();
            }
        }, [&](float inner) {
            SettingsCardHead("Vehicles & scenery", "More visible detail can increase CPU and GPU load.", inner);
            const auto check = [&](const char* id, const char* label, bool value, auto setter,
                                   const char* help, bool first = false) {
                if (!first) PaddockGap(16.0F);
                if (SettingsCheck(id, label, &value, inner, help)) {
                    setter(value);
                    SaveSettings();
                }
            };
            check("##max-detail", "Maximum vehicle detail", enh::maximum_detail_requested(),
                  enh::set_maximum_detail_enabled, "Keep racers on their most detailed models.", true);
            check("##hub-scenery", "Keep hub scenery rendered", enh::keep_hub_scenery_requested(),
                  enh::set_keep_hub_scenery_enabled, "Keep forward-visible scenery in the adventure hub.");
            check("##track-scenery", "Keep track and boss scenery rendered", enh::keep_track_scenery_requested(),
                  enh::set_keep_track_scenery_enabled,
                  "Keep forward-visible scenery during races and boss challenges.");
            check("##minigame-scenery", "Keep minigame and battle scenery rendered",
                  enh::keep_minigame_scenery_requested(), enh::set_keep_minigame_scenery_enabled,
                  "Keep forward-visible scenery in minigames and battle arenas.");
            check("##ultrawide", "Ultrawide scenery guard", enh::extended_culling_requested(),
                  enh::set_extended_culling_enabled,
                  "Expand visibility checks to reduce scenery disappearing near wide-screen edges.");
            if (enh::extended_culling_requested()) {
                PaddockGap(16.0F);
                ImGui::Indent(36.0F);
                int guard = enh::frustum_guard_percent();
                if (SettingsRange("##guard", "Culling safety margin", &guard, 0, 20, inner - 36.0F,
                                  [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; },
                                  "Objects directly behind the camera still cull normally.")) {
                    enh::set_frustum_guard_percent(guard);
                    SaveSettings();
                }
                ImGui::Unindent(36.0F);
            }
            PaddockGap(18.0F);
            if (!SettingsDisclosure("##distances", "View distances", state.distances_open, inner)) return;
            PaddockGap(16.0F);
            const auto distance = [&](const char* id, const char* label, int value, int maximum, auto setter) {
                if (SettingsRange(id, label, &value, enh::kMinimumViewDistanceMultiplier, maximum, inner,
                                  GraphicsTimes)) {
                    setter(value);
                    SaveSettings();
                }
                PaddockGap(16.0F);
            };
            distance("##view-distance", "Scenery and object view distance", enh::view_distance_multiplier(),
                     enh::kMaximumViewDistanceMultiplier, enh::set_view_distance_multiplier);
            int retention = static_cast<int>(enh::scenery_retention_mode());
            if (SettingsDropdown("retention", "Scenery retention", &retention,
                                 {"Authored", "Current region", "Visible + adjacent", "Full forward view"}, inner)) {
                enh::set_scenery_retention_mode(enh::normalise_scenery_retention_mode(retention));
                SaveSettings();
            }
            PaddockGap(16.0F);
            distance("##animated-distance", "Animated scenery distance",
                     enh::animated_scenery_distance_multiplier(), enh::kMaximumAnimatedSceneryMultiplier,
                     enh::set_animated_scenery_distance_multiplier);
            distance("##billboard-distance", "Billboard and effect distance",
                     enh::billboard_effect_distance_multiplier(), enh::kMaximumBillboardEffectMultiplier,
                     enh::set_billboard_effect_distance_multiplier);
            distance("##water-distance", "Water and lava detail distance", enh::water_lava_detail_multiplier(),
                     enh::kMaximumWaterLavaDetailMultiplier, enh::set_water_lava_detail_multiplier);
            SettingsHelp("Only forward-visible regions are retained; objects behind the camera still cull. "
                         "Higher settings can increase CPU and GPU load on handheld systems.",
                         inner);
        });
    }

    // ------------------------------------------------------------ footer
    PaddockGap(18.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
        PaddockGap(1.0F + 18.0F);
        PaddockText(PaddockSign(19.0F, 1.0F, 0.0F), PaddockRgb(kSetText), "Restore graphics defaults", width);
        PaddockGap(6.0F);
        SettingsHelp("Switch back to Accurate, the original presentation. Your Modern settings are kept, and "
                     "you can undo until your next graphics change or until you leave this page.",
                     width);
        PaddockGap(12.0F);
        const bool accurate = !enh::modern_options_visible(enh::presentation_profile());
        if (SettingsRaceButton("RESTORE GRAPHICS DEFAULTS", accurate,
                               "Graphics settings already match the defaults.")) {
            RememberModernGraphics(config);
            enh::set_presentation_profile(enh::PresentationProfile::Accurate);
            ApplyProfileGraphics(config, enh::PresentationProfile::Accurate);
            ultramodern::renderer::set_graphics_config(config);
            SaveSettings();
            state.can_undo = true;
            state.status = "Accurate restored. Your Modern settings are kept.";
        }
        if (state.can_undo) {
            ImGui::SameLine(0.0F, 12.0F);
            if (SettingsRaceButton("UNDO", false)) {
                enh::set_presentation_profile(enh::PresentationProfile::Modern);
                ApplyProfileGraphics(config, enh::PresentationProfile::Modern);
                ultramodern::renderer::set_graphics_config(config);
                SaveSettings();
                state.can_undo = false;
                state.status = "Your previous graphics settings have been restored.";
            }
        }
        if (!state.status.empty()) {
            PaddockGap(12.0F);
            SettingsHelp(state.status, width, kSetWarm);
        }
    }
    ImGui::PopStyleVar();
}
