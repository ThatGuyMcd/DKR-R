// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_saves_ui.inl. Presentation only: texture packs, CRT overlays, links
// and support tools go through the same helpers as before.
//
// TEXTURES and ABOUT DKR-R in the look of tools/launcher-html
// (pages/textures.js, pages/about.js, styles/launcher-details.css): the
// launcher's own sections and cards, set in the reading face.

// .launcher-detail-page p: 16 / 1.5, 13 px under each paragraph.
void DetailText(std::string_view text, float width, unsigned colour = kSetText, float after = 13.0F) {
    PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(colour), text, width);
    PaddockGap(after);
}

// A 600 18 px heading (h2 / h3 on the detail pages).
void DetailHeading(std::string_view text, float width, unsigned colour = kSetText, float after = 13.0F) {
    PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(colour), text, width);
    PaddockGap(after);
}

// A launcher race button (.race-button) on a detail page; blue unless told.
bool DetailRaceButton(const char* label, float width, unsigned fill = 0x0A6EA1, bool disabled = false) {
    RaceButtonLook look;
    look.fill = fill;
    look.hover_in = 0.12F;
    OlDisabled scope(disabled, 0.6F);
    return PaddockRaceButton(label, {width, 44.0F}, look) && !disabled;
}

// Two race buttons side by side, or stacked on a narrow panel
// (.launcher-detail-actions).
template <typename Left, typename Right>
void DetailActionPair(float width, bool stacked, Left&& left, Right&& right) {
    const float half = stacked ? width : std::floor((width - 12.0F) * 0.5F);
    left(half);
    if (stacked) PaddockGap(12.0F); else ImGui::SameLine(0.0F, 12.0F);
    right(stacked ? width : width - half - 12.0F);
}

// A detail card (.card): 18 x 20 px of padding inside a 2 px border.
template <typename Content>
float DetailCard(float width, ImVec2 padding, Content&& content, float min_height = 0.0F) {
    PaddockBox box(width, {padding.x + 2.0F, padding.y + 2.0F}, min_height);
    content(box.Inner());
    return box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
        PaddockPanel(draw, a, b, {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 2.0F});
    });
}

// A wide section sign over a heavy hairline (.texture-disclosure).
bool DetailSectionSign(const char* label, bool& open, float width) {
    PaddockGap(22.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (PaddockSectionTab(label, open, width)) open = !open;
    const float bottom = at.y + PaddockSectionTabHeight() + 16.0F;
    ImGui::GetWindowDrawList()->AddRectFilled({at.x, bottom}, {at.x + width, bottom + 2.0F}, PaddockCol(0x24495B));
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, bottom + 2.0F - at.y});
    PaddockGap(16.0F);
    return open;
}

// ------------------------------------------------------------ textures

struct TexturesPageState {
    bool packs_open = true;
    bool crt_open = false;
};
TexturesPageState g_textures_page;

void DrawTexturePackLibrary(float width) {
    using namespace dkr::runtime;
    using namespace dkr::runtime::texture_browser;
    texture_packs::request_background_refresh();
    DetailText("Search, arrange and activate managed RT64 and Rice texture packs. Technical import details stay "
               "out of the library cards.",
               width, kSetMuted);

    // Search (.field): a field that opens the on-screen keyboard.
    PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetText), "Search", width);
    PaddockGap(13.0F);
    {
        const PaddockPress press = PaddockBeginPress("##texture-search", {width, 48.0F});
        if (press.pressed) RequestTextEntryKeyboard(TextEntryTarget::TexturePackSearch);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const PaddockRadii radii = PaddockRound(12.0F);
        PaddockFill(draw, press.min, press.max, radii, PaddockCol(0x0B2E40, 245U));
        PaddockStroke(draw, press.min, press.max, radii,
                      PaddockApply(PaddockMix(PaddockRgb(kSetBorder), PaddockRgb(0x5A97B1), press.hover)), 1.0F);
        const bool empty = g_texture_pack_search[0] == '\0';
        const PaddockType type = PaddockReading(16.0F, false, 1.4F);
        const std::string shown = PaddockEllipsize(type, empty ? "Search texture packs..." : g_texture_pack_search,
                                                   width - 34.0F);
        PaddockDrawRun(draw, type, {press.min.x + 17.0F, std::round(press.min.y + (48.0F - type.line) * 0.5F)},
                       PaddockCol(empty ? 0x7F9DABU : kSetText), shown.data(), shown.data() + shown.size());
        PaddockEndPress(press, 12.0F, 0.98F);
        PaddockGap(13.0F);
    }

    // The filters (.texture-filters): five, three or two to a row.
    {
        struct Filter {
            const char* id;
            const char* label;
            int* value;
            std::vector<SettingsChoice> items;
        };
        const std::array<Filter, 5> kFilters{{
            {"sort", "Sort", &g_texture_pack_sort,
             {"Name A-Z", "Name Z-A", "Largest first", "Smallest first", "Newest first", "Oldest first", "Type"}},
            {"state", "State", &g_texture_pack_state_filter, {{"All", {}, false}, {"Active", {}, false}, {"Inactive", {}, false}}},
            {"compatibility", "Compatibility", &g_texture_pack_compatibility_filter,
             {"All", "Compatible", "Incompatible"}},
            {"type", "Type", &g_texture_pack_type_filter,
             {"All", "Native RT64", "Rice / RT64 Bridge", "Legacy Rice", "Legacy Jabo"}},
            {"visibility", "Visibility", &g_texture_pack_visibility_filter,
             {"Visible", "All", "Hidden", "Track packs"}},
        }};
        const int per_row = width >= 1100.0F ? 5 : width >= 700.0F ? 3 : width >= 460.0F ? 2 : 1;
        const float column = std::floor((width - 12.0F * static_cast<float>(per_row - 1)) / static_cast<float>(per_row));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const PaddockType label_type = PaddockReading(14.0F, false, 1.5F);
        float row_top = at.y;
        for (std::size_t index = 0; index < kFilters.size(); ++index) {
            const int slot = static_cast<int>(index) % per_row;
            if (slot == 0 && index > 0U) row_top += label_type.line + 7.0F + 52.0F + 12.0F;
            ImGui::SetCursorScreenPos({at.x + (column + 12.0F) * static_cast<float>(slot), row_top});
            ImGui::BeginGroup();
            PaddockText(label_type, PaddockRgb(kSetMuted), kFilters[index].label, column);
            PaddockGap(7.0F);
            SettingsDropdown(kFilters[index].id, {}, kFilters[index].value, kFilters[index].items, column);
            ImGui::EndGroup();
        }
        ImGui::SetCursorScreenPos({at.x, row_top + label_type.line + 7.0F + 52.0F});
        ImGui::Dummy({width, 0.0F});
    }

    Filters filters{};
    filters.query = g_texture_pack_search;
    filters.state = static_cast<StateFilter>(std::clamp(g_texture_pack_state_filter, 0, 2));
    filters.compatibility = static_cast<CompatibilityFilter>(std::clamp(g_texture_pack_compatibility_filter, 0, 2));
    filters.visibility = static_cast<VisibilityFilter>(std::clamp(g_texture_pack_visibility_filter, 0, 3));
    switch (std::clamp(g_texture_pack_type_filter, 0, 4)) {
        case 1: filters.format = texture_packs::Format::NativeRt64; break;
        case 2: filters.format = texture_packs::Format::RiceRt64; break;
        case 3: filters.format = texture_packs::Format::LegacyRice; break;
        case 4: filters.format = texture_packs::Format::LegacyJabo; break;
        default: filters.format = texture_packs::Format::Unknown; break;
    }
    // The library and its filtered view change only with the library or a
    // filter, not every frame.
    struct LibraryCache {
        std::uint64_t generation = 0U;
        std::string query;
        std::array<int, 5> filters{-1, -1, -1, -1, -1};
        std::vector<texture_packs::PackInfo> all;
        std::vector<texture_packs::PackInfo> shown;
    };
    static LibraryCache cache;
    const std::uint64_t generation = texture_packs::generation();
    const std::array<int, 5> current{std::clamp(g_texture_pack_sort, 0, 6),
                                     std::clamp(g_texture_pack_state_filter, 0, 2),
                                     std::clamp(g_texture_pack_compatibility_filter, 0, 2),
                                     std::clamp(g_texture_pack_type_filter, 0, 4),
                                     std::clamp(g_texture_pack_visibility_filter, 0, 3)};
    if (cache.generation != generation) {
        cache.all = texture_packs::snapshot(true);
        cache.generation = generation;
        cache.filters[0] = -1;
    }
    if (cache.filters != current || cache.query != g_texture_pack_search) {
        cache.shown = select(cache.all, filters, static_cast<SortMode>(current[0]));
        cache.filters = current;
        cache.query = g_texture_pack_search;
    }

    PaddockGap(16.0F);
    const std::string count = std::to_string(cache.shown.size()) +
                              (cache.shown.size() == 1U ? " PACK SHOWN" : " PACKS SHOWN");
    PaddockText(PaddockReading(13.0F, true, 1.5F), PaddockRgb(kSetMuted), count, width);
    PaddockGap(16.0F);
    const bool busy = TexturePackImportRunning() || DialogJobRunning();
    DetailActionPair(width, width <= 520.0F, [&](float cell) {
        if (DetailRaceButton("IMPORT TEXTURE PACK", cell, 0x0A6EA1, busy)) ImportTexturePackWithDialog();
    }, [&](float cell) {
        if (DetailRaceButton("REFRESH PACKS", cell, 0x0A6EA1, busy)) texture_packs::refresh();
    });
    PaddockGap(16.0F);

    if (cache.shown.empty()) {
        DetailText(cache.all.empty() ? "No texture packs have been imported yet."
                                     : "No texture packs match the current search and filters.",
                   width, kSetMuted);
    } else {
        // The library (.texture-library): cards of at least 260 px, 14 apart;
        // a row's cards share the tallest one's height.
        const int columns = std::max(1, static_cast<int>((width + 14.0F) / (260.0F + 14.0F)));
        const float column = std::floor((width - 14.0F * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        const PaddockType name_type = PaddockReading(16.0F, true, 1.5F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        float row_top = at.y;
        for (std::size_t first = 0; first < cache.shown.size(); first += static_cast<std::size_t>(columns)) {
            const std::size_t last = std::min(cache.shown.size(), first + static_cast<std::size_t>(columns));
            float name_height = 44.0F;
            for (std::size_t index = first; index < last; ++index) {
                name_height = std::max(name_height,
                                       PaddockTextHeight(name_type, cache.shown[index].name, column - 32.0F - 36.0F));
            }
            const float card_height = std::max(166.0F, 16.0F + name_height + 8.0F + 24.0F + 13.0F + 44.0F + 16.0F);
            for (std::size_t index = first; index < last; ++index) {
                const auto& pack = cache.shown[index];
                ImGui::PushID(pack.id.c_str());
                const ImVec2 card{at.x + (column + 14.0F) * static_cast<float>(index - first), row_top};
                ImDrawList* draw = ImGui::GetWindowDrawList();
                PaddockPanel(draw, card, {card.x + column, card.y + card_height},
                             {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 2.0F});
                ImGui::SetCursorScreenPos({card.x + 16.0F, card.y + 16.0F});
                bool enabled = pack.enabled;
                if (SettingsCheck("##enabled", pack.name, &enabled, column - 32.0F, {},
                                  pack.hidden || !pack.compatible)) {
                    texture_packs::set_enabled(pack.id, enabled);
                }
                ImGui::SetCursorScreenPos({card.x + 16.0F, card.y + 16.0F + name_height + 8.0F});
                const std::string type = texture_packs::format_name(pack.format);
                PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(pack.compatible ? 0x1AC2A3U : kSetWarm),
                            type, column - 32.0F);
                if (!pack.compatible && ImGui::IsItemHovered()) ImGui::SetTooltip("Incompatible with live activation");
                ImGui::SetCursorScreenPos({card.x + 16.0F, card.y + card_height - 16.0F - 44.0F});
                if (DetailRaceButton("MANAGE...", column - 32.0F)) {
                    g_texture_pack_manage_id = pack.id;
                    g_texture_pack_manage_request = true;
                }
                ImGui::PopID();
            }
            row_top += card_height + 14.0F;
        }
        ImGui::SetCursorScreenPos({at.x, row_top - 14.0F});
        ImGui::Dummy({width, 0.0F});
    }
    if (!g_texture_pack_status.empty()) {
        PaddockGap(16.0F);
        DetailText(g_texture_pack_status, width, kSetMuted);
    }
    const std::string texture_status = texture_packs::status();
    if (!texture_status.empty()) {
        PaddockGap(g_texture_pack_status.empty() ? 16.0F : 0.0F);
        DetailText(texture_status, width, kSetMuted);
    }
}

void DrawCrtOverlaySection(float width) {
    bool crt = g_crt_enabled;
    if (SettingsCheck("##crt-enabled", "Enable CRT overlay", &crt, width)) {
        g_crt_enabled = crt;
        SaveSettings();
    }
    PaddockGap(8.0F);
    DetailText("Applied only to the game image. DKR-R\xE2\x80\x99s settings and performance overlays remain clear "
               "above it.",
               width, kSetMuted);
    if (g_crt_enabled) {
        const float field = std::min(width, 680.0F);
        if (!g_crt_filters.empty()) {
            std::vector<SettingsChoice> filters;
            for (const auto& filter : g_crt_filters) filters.push_back({filter.label, {}, false});
            if (SettingsDropdown("crt-filter", "Filter image", &g_crt_filter_index, filters, field)) SaveSettings();
            PaddockGap(13.0F);
        }
        if (SettingsDropdown("crt-scaling", "Scaling", &g_crt_scale_mode,
                             {"Stretch to viewport", "Tile at native size"}, field)) {
            SaveSettings();
        }
        PaddockGap(13.0F);
        int density = static_cast<int>(std::round(std::clamp(g_crt_strength, 0.0F, 1.0F) * 100.0F));
        if (SettingsRange("##crt-density", "Filter density", &density, 0, 100, field,
                          [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; })) {
            g_crt_strength = static_cast<float>(density) / 100.0F;
            SaveSettings();
        }
        PaddockGap(13.0F);
    }
    if (DetailRaceButton("IMPORT CUSTOM CRT FILTER", width)) ImportCrtFilterWithDialog();
    if (!g_crt_status.empty()) {
        PaddockGap(13.0F);
        DetailText(g_crt_status, width, kSetMuted);
    }
}

void DrawTexturesPage(float width) {
    TexturesPageState& state = g_textures_page;
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    ++g_settings_regular_labels;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    DrawPageHeading("TEXTURES");
    PaddockGap(13.0F);
    DetailText("Personalise the look of DKR-R with texture packs and CRT filters.", width, kSetMuted, 0.0F);
    if (DetailSectionSign("TEXTURE PACKS##textures", state.packs_open, width)) {
        if (modern) {
            DrawTexturePackLibrary(width);
        } else {
            DetailText("Texture-pack management is available in the Modern presentation profile. Accurate mode "
                       "remains unchanged.",
                       width, kSetMuted);
        }
    }
    if (DetailSectionSign("CRT OVERLAYS##crt", state.crt_open, width)) {
        if (modern) {
            DrawCrtOverlaySection(width);
        } else {
            DetailText("CRT overlays are available in the Modern presentation profile. Accurate mode remains "
                       "unchanged.",
                       width, kSetMuted);
        }
    }
    ImGui::PopStyleVar();
    --g_settings_regular_labels;
    // The pack modals are shared with MODS / HACKS and keep its look.
    const PaddockFlatScope paddock;
    ++g_paddock_modal_windows;
    DrawSharedTexturePackModal();
    --g_paddock_modal_windows;
}

// ------------------------------------------------------------ about

// A titled part of the page (.about-section-title): a hairline 20 px above.
void AboutSectionTitle(std::string_view text, float width) {
    PaddockGap(28.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
    PaddockGap(1.0F + 20.0F);
    DetailHeading(text, width, kSetText, 16.0F);
}

void DrawAboutPatchNotes() {
    constexpr const char* kPopup = "##patch-notes";
    if (g_patch_notes_requested) {
        ImGui::OpenPopup(kPopup);
        g_patch_notes_requested = false;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(900.0F, display.x - 32.0F);
    const float height = std::min(std::max(display.y * 0.85F, 360.0F), display.y - 32.0F);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.5F}, ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {26.0F, 26.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x0B2E40));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(kSetBorder));
    const bool visible = ImGui::BeginPopupModal(kPopup, nullptr,
                                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                    ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!visible) return;
    g_paddock_modal_frame = ImGui::GetFrameCount();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    const float inner = width - 52.0F;
    PaddockText(PaddockSign(19.0F, 1.2F, 0.0F), PaddockRgb(kSetText), "WHAT\xE2\x80\x99S NEW SINCE 1.0.0", inner);
    PaddockGap(13.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + inner, at.y + 1.0F}, PaddockCol(kSetWarm));
        PaddockGap(1.0F + 22.0F);
    }
    constexpr std::array<std::pair<const char*, const char*>, 7> kNotes{{
        {"GAME PAK COMPATIBILITY",
         "Unified support for US v1.0, US Rev A / v1.1 and byte-swapped supported images through one launcher and "
         "one runtime. Revision selection no longer opens a second application instance."},
        {"DKR-R ONLINE",
         "Added secure five-character Quick Join, host approval, Online Profiles, friends, friend invites, Open "
         "Lobbies, presence and notifications. Added Lockstep and Rollback synchronization, host-authoritative race "
         "state, recovery barriers, connection and controller overlays, synchronized saves and cross-platform build "
         "compatibility checks. Water, hovercraft height, racer orientation, moving actors, RNG and CPU racers now "
         "follow authoritative state. Network catch-up and bounded recovery keep unstable connections responsive "
         "without accumulating permanent frame debt."},
        {"MODERN PRESENTATION",
         "Added high-refresh interpolation without changing game speed, widescreen and ultrawide presentation, "
         "revised skyboxes and water, split-screen viewport handling, adjustable FOV, view distance, scenery "
         "controls, maximum vehicle detail and HUD sizing. Stabilized wheels, propellers, steering wheels, shadows, "
         "billboards, doors, trails, animated water and post-race cameras."},
        {"GRAPHICS AND TEXTURES",
         "Added RT64 and Rice texture-pack import, live pack selection, CRT overlays, anisotropic filtering, "
         "downsampling, anti-aliasing, high-precision framebuffer controls and a configurable performance overlay. "
         "Corrected texture-edge sampling and high-resolution UI tile seams."},
        {"CONTROLS",
         "Added independent Player 1-4 controller assignment, primary and secondary bindings, per-vehicle inversion, "
         "per-player gyro, quick race restart, texture-pack and fullscreen shortcuts, background input, live "
         "stick/gyro previews and broader modern/N64 controller database support."},
        {"SAVES AND GAMEPLAY",
         "Added automatic EEPROM validation and repair, virtual Controller Paks alongside rumble, save "
         "backup/import/export, an Adventure Save Builder, course progress, unlockables and Magic Code management. "
         "Added independent music, vehicle, effects, ambience and EQ controls, plus optional music for three- and "
         "four-player races."},
        {"LAUNCHER AND STABILITY",
         "Redesigned the controller-first launcher and in-game overlay, added animated DKR-R branding, friends and "
         "controller-friendly text entry, restart/exit/fullscreen handling, support diagnostics and clearer online "
         "errors. Fixed transition crashes, intro-loop crashes, race-end vertex explosions, audio pops, black screens "
         "and Linux/Steam Deck startup and layout problems."},
    }};
    const float footer = 24.0F + 44.0F;
    ImGui::BeginChild("##patch-body", {inner, std::max(ImGui::GetContentRegionAvail().y - footer, 80.0F)}, false,
                      ImGuiWindowFlags_NavFlattened);
    const float text_width = inner - ImGui::GetStyle().ScrollbarSize - 8.0F;
    DetailText("Current development and Online Beta changes", text_width, kSetMuted);
    for (std::size_t index = 0; index < kNotes.size(); ++index) {
        if (index > 0U) PaddockGap(11.0F);
        DetailHeading(kNotes[index].first, text_width, kSetWarm);
        DetailText(kNotes[index].second, text_width);
    }
    ImGui::EndChild();
    PaddockGap(24.0F);
    const float close_width = PaddockRaceButtonWidth("CLOSE PATCH NOTES");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inner - close_width);
    if (DetailRaceButton("CLOSE PATCH NOTES", close_width)) ImGui::CloseCurrentPopup();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void DrawAboutSupport(float width) {
    PollSupportSystemSummary();
    AboutSectionTitle("Support summary", width);
    DetailText("Privacy-safe settings and system details for troubleshooting.", width, kSetMuted);
    DetailCard(width, {20.0F, 18.0F}, [&](float inner) {
        // The facts (.launcher-detail-facts): a muted label, then its value.
        const GraphicsConfig config = ultramodern::renderer::get_graphics_config();
        char hud[32]{};
        std::snprintf(hud, sizeof(hud), "%.0f%%", static_cast<double>(dkr::runtime::hud::global_scale() * 100.0F));
        std::vector<std::pair<std::string, std::string>> facts{
            {"Release", DKR_RELEASE_VERSION},
            {"Presentation", SupportPresentationName()},
            {"Graphics API", SupportGraphicsApiName(config.api_option)},
            {"HUD size", hud},
            {"Enabled texture packs", std::to_string(EnabledTexturePackCount())},
            {"Online synchronization",
             dkr::runtime::netplay::synchronization_name(CurrentOnlineSynchronization())},
        };
#if defined(__ANDROID__)
        facts.push_back({"Android build", dkr::runtime::android::build_identity()});
#endif
        if (g_support_summary) {
            facts.push_back({"OS", g_support_summary->operating_system});
            facts.push_back({"CPU", g_support_summary->cpu});
            facts.push_back({"Memory", g_support_summary->memory});
            facts.push_back({"GPU", g_support_summary->gpu});
            facts.push_back({"Storage", "boot " + g_support_summary->boot_drive + "; DKR-R " +
                                            g_support_summary->application_drive});
        } else {
            facts.push_back({"System", "Collecting system details..."});
        }
        // minmax(120px, 1fr) minmax(0, 2fr) with a 16 px gap.
        const float label_width = std::max(120.0F, std::floor((inner - 16.0F) / 3.0F));
        const float value_width = std::max(inner - label_width - 16.0F, 1.0F);
        const PaddockType type = PaddockReading(16.0F, false, 1.5F);
        for (std::size_t index = 0; index < facts.size(); ++index) {
            if (index > 0U) PaddockGap(8.0F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            PaddockText(type, PaddockRgb(kSetMuted), facts[index].first, label_width);
            const float label_bottom = ImGui::GetItemRectMax().y;
            ImGui::SetCursorScreenPos({at.x + label_width + 16.0F, at.y});
            PaddockText(type, PaddockRgb(kSetText), facts[index].second, value_width);
            const float bottom = std::max(label_bottom, ImGui::GetItemRectMax().y);
            ImGui::SetCursorScreenPos({at.x, bottom});
            ImGui::Dummy({inner, 0.0F});
        }
        PaddockGap(20.0F);
        bool logging = dkr::runtime::support::diagnostic_logging_enabled();
        if (SettingsCheck("##diagnostic-logging", "Diagnostic logging", &logging, inner)) {
            dkr::runtime::support::set_diagnostic_logging_enabled(logging);
            g_support_action_status = logging ? "Diagnostic logging will be enabled at the next launch."
                                              : "Diagnostic logging will be disabled at the next launch.";
        }
        PaddockGap(8.0F);
        bool dumps = dkr::runtime::support::crash_dumps_enabled();
        if (SettingsCheck("##crash-dumps", "Create crash dumps", &dumps, inner)) {
            dkr::runtime::support::set_crash_dumps_enabled(dumps);
            g_support_action_status = dumps ? "Crash dumps are enabled." : "Crash dumps are disabled.";
        }
        PaddockGap(16.0F);
        const bool stacked = inner <= 520.0F;
#if defined(__ANDROID__)
        if (DetailRaceButton("RECORD PERFORMANCE REPORT", inner)) {
            dkr::runtime::android::performance_tools();
        }
        PaddockGap(12.0F);
        if (DetailRaceButton("EXPORT ANDROID STARTUP LOGS", inner)) {
            dkr::runtime::android::export_diagnostics();
        }
        PaddockGap(8.0F);
        DetailText("Save a diagnostic ZIP using Android's file picker. Review logs before sharing; "
                   "they may contain paths and session details.", inner, kSetMuted, 0.0F);
        PaddockGap(12.0F);
#endif
        if (DetailRaceButton("EXPORT SUPPORT SUMMARY", inner)) {
            std::filesystem::path output;
            std::string error;
            g_support_action_status = dkr::runtime::support::export_report(BuildSupportReport(), output, error)
                ? "Support summary exported to the DKR-R support-reports folder." : error;
        }
        PaddockGap(12.0F);
        DetailActionPair(inner, stacked, [&](float cell) {
            if (DetailRaceButton("OPEN LOGS", cell)) {
                dkr::runtime::support::open_directory(dkr::runtime::support::log_directory(), g_support_action_status);
            }
        }, [&](float cell) {
            if (DetailRaceButton("OPEN CRASH DUMPS", cell)) {
                dkr::runtime::support::open_directory(dkr::runtime::support::crash_dump_directory(),
                                                      g_support_action_status);
            }
        });
        PaddockGap(12.0F);
        if (DetailRaceButton("OPEN SUPPORT REPORTS", inner)) {
            dkr::runtime::support::open_directory(dkr::runtime::support::support_report_directory(),
                                                  g_support_action_status);
        }
        if (!g_support_action_status.empty()) {
            PaddockGap(16.0F);
            DetailText(g_support_action_status, inner, kSetMuted, 0.0F);
        }
        PaddockGap(16.0F);
    });
}

void DrawAboutPage(float width, bool support) {
    struct Credit {
        const char* name;
        const char* repository;
    };
    constexpr std::array<Credit, 7> kTechnology{{
        {"N64Recomp", "https://github.com/N64Recomp/N64Recomp"},
        {"N64ModernRuntime", "https://github.com/N64Recomp/N64ModernRuntime"},
        {"RT64", "https://github.com/rt64/rt64"},
        {"Monocypher", "https://github.com/LoupVaillant/Monocypher"},
        {"Mbed TLS", "https://github.com/Mbed-TLS/mbedtls"},
        {"GekkoNet", "https://github.com/HeatXD/GekkoNet"},
        {"Diddy Kong Racing Decomp", "https://github.com/DavidSM64/Diddy-Kong-Racing"},
    }};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    DrawPageHeading("ABOUT DKR-R");
    PaddockGap(13.0F);
    DetailText("Diddy Kong Racing - Recompiled", width, kSetMuted, 0.0F);
    PaddockGap(24.0F);
    DetailCard(width, {22.0F, 22.0F}, [&](float inner) {
        DetailText("Wizpig has invaded DKR-R. Race across land, water and sky, collect Golden Balloons and help Diddy "
                   "and his friends send the intergalactic pig wizard packing.",
                   inner);
        DetailText("DKR-R runs the original game logic through a native PC runtime. Accurate preserves the original "
                   "presentation; Modern adds carefully isolated PC quality-of-life options.",
                   inner);
        DetailHeading("CREATED BY THATGUYMCD", inner, kSetWarm);
        DetailText("If you did not download DKR-R from ThatGuyMcd\xE2\x80\x99s GitHub repository, this build may have "
                   "been modified or tampered with.",
                   inner);
        DetailText("Official source: github.com/ThatGuyMcd/DKR-R", inner, kSetMuted, 3.0F);
        PaddockGap(16.0F);
        DetailActionPair(inner, inner <= 520.0F, [&](float cell) {
            if (DetailRaceButton("VISIT GITHUB PAGE", cell)) SDL_OpenURL("https://github.com/ThatGuyMcd/DKR-R");
        }, [&](float cell) {
            if (DetailRaceButton("VIEW PATCH NOTES", cell, 0xE82E21)) g_patch_notes_requested = true;
        });
        PaddockGap(16.0F);
    });

    AboutSectionTitle("Release information", width);
    DetailText(std::string("DKR-R ") + DKR_RELEASE_VERSION, width);
    DetailText("Windows, Linux and macOS builds", width);
    DetailText("No copyrighted game data is distributed. A legally obtained supported Diddy Kong Racing Game Pak is "
               "required.",
               width, kSetMuted, 0.0F);

    AboutSectionTitle("Credits", width);
    DetailCard(width, {20.0F, 18.0F}, [&](float inner) {
        DetailHeading("POOTERMAN - DKR-R ICON", inner, kSetWarm);
        DetailText("DKR-R\xE2\x80\x99s application icon was created by POOTERMAN.", inner);
        if (DetailRaceButton("VISIT POOTERMAN ON DEVIANTART", inner)) SDL_OpenURL("https://www.deviantart.com/pooterman");
        PaddockGap(26.0F);
        DetailHeading("CORE TECHNOLOGY AND RESEARCH", inner, kSetWarm);
        // .about-technology: the name, then a 190 px button (stacked when narrow).
        const bool stacked = inner <= 520.0F;
        for (const Credit& credit : kTechnology) {
            ImGui::PushID(credit.repository);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const PaddockType type = PaddockReading(16.0F, false, 1.5F);
            if (stacked) {
                PaddockText(type, PaddockRgb(kSetText), credit.name, inner);
                PaddockGap(8.0F);
                if (DetailRaceButton("VISIT GITHUB", inner)) SDL_OpenURL(credit.repository);
            } else {
                PaddockDrawRun(ImGui::GetWindowDrawList(), type, {at.x, std::round(at.y + (44.0F - type.line) * 0.5F)},
                               PaddockCol(kSetText), credit.name, credit.name + std::strlen(credit.name));
                ImGui::SetCursorScreenPos({at.x + inner - 190.0F, at.y});
                if (DetailRaceButton("VISIT GITHUB", 190.0F)) SDL_OpenURL(credit.repository);
            }
            PaddockGap(12.0F);
            ImGui::PopID();
        }
        DetailText("Thanks to all developers and contributors.", inner, kSetMuted, 0.0F);
        PaddockGap(26.0F);
        DetailText("Golden Balloon - HUD layout reference", inner);
        if (DetailRaceButton("VISIT GOLDEN BALLOON ON GITHUB", inner)) {
            SDL_OpenURL("https://github.com/akratch/goldenballoon");
        }
        PaddockGap(26.0F);
        DetailHeading("DKR-R HDR TEXTURE PACK PROJECT", inner, kSetWarm);
        DetailText("A community project re-imagining Diddy Kong Racing in crisp HD while remaining faithful to the "
                   "original art direction. Project lead: sr.gu. Thank you to every artist, tester and contributor "
                   "involved.",
                   inner);
        if (DetailRaceButton("JOIN THE DKR-R HDR DISCORD", inner)) SDL_OpenURL("https://discord.gg/AMWfXdBjNP");
    });
    if (support) DrawAboutSupport(width);
    ImGui::PopStyleVar();
    DrawAboutPatchNotes();
}
