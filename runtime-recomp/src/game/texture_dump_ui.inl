// Included after the existing shared Textures page inside its UI namespace.
namespace dumping = dkr::runtime::texture_dump;
dumping::Options g_dump_options;
std::filesystem::path g_dump_config;
std::array<char, 161> g_dump_search{};
std::string g_dump_ui_status;
std::string g_dump_browser_status;
bool g_dump_browser = false, g_dump_sources = false;
// Keep the existing Texture Packs and CRT page completely unchanged. These
// session-local controls belong only to the new disclosure beneath that page.
bool g_dump_open = false, g_dump_folder_info = false;
enum class DumpPanel { Export, Capture, Dumps };
DumpPanel g_dump_panel = DumpPanel::Export;
bool g_dump_clear_previews = false;
std::uint64_t g_dump_preview_generation = 0;
struct DumpPreviewTexture {
    SDL_Texture* texture = nullptr;
    int used = 0;
};
std::map<std::string, DumpPreviewTexture> g_dump_previews;
void ReleaseTextureDumpTextures() {
    for (auto& [key, t] : g_dump_previews)
        if (t.texture)
            SDL_DestroyTexture(t.texture);
    g_dump_previews.clear();
}
void InvalidateTextureDumpPreviews() {
    g_dump_clear_previews = true;
}
void TrimDumpPreviews() {
    // Evict only between frames, before this frame can reference a texture.
    if (g_dump_clear_previews) {
        ReleaseTextureDumpTextures();
        g_dump_clear_previews = false;
    }
    while (g_dump_previews.size() > 128) {
        auto oldest = std::min_element(g_dump_previews.begin(), g_dump_previews.end(),
                                       [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        if (oldest->second.texture)
            SDL_DestroyTexture(oldest->second.texture);
        g_dump_previews.erase(oldest);
    }
}
// Content-sized buttons wrap as a group instead of squeezing their labels.
struct DumpActionRow {
    float width, left;
    bool first = true;
    explicit DumpActionRow(float available) : width(available), left(ImGui::GetCursorScreenPos().x) {}
    bool Button(const char* label, bool disabled = false, SettingsButtonTone tone = SettingsButtonTone::Plain,
                bool primary = false) {
        const float natural = primary ? PaddockRaceButtonWidth(label) : SettingsButtonWidth(label);
        const float size = std::min(width, natural);
        if (!first) {
            if (ImGui::GetItemRectMax().x + 12.F + size <= left + width)
                ImGui::SameLine(0.F, 12.F);
            else
                PaddockGap(10.F);
        }
        first = false;
        if (primary && natural <= width)
            return DetailRaceButton(label, size, 0x12BFA6, disabled);
        // Very narrow overlay panels use the shared wrapping text button.
        return SettingsButton(label, size, disabled, primary ? SettingsButtonTone::Selected : tone);
    }
};
std::string DumpPathText(const std::filesystem::path& path) {
    const auto name = path.u8string();
    return std::string(name.begin(), name.end());
}
void ChooseDumpFolder(bool existing = false) {
    StartDialogJob([existing]() -> std::function<void()> {
        std::filesystem::path folder;
        std::string error;
        const bool chosen = PickFolder(folder, error);
        return [chosen, folder, error, existing] {
            if (chosen) {
                g_dump_ui_status.clear();
                g_dump_options.destination = folder;
                if (existing) {
                    g_dump_browser_status.clear();
                    g_dump_panel = DumpPanel::Dumps;
                    g_dump_browser = true;
                    InvalidateTextureDumpPreviews();
                    dumping::gallery(folder, g_dump_search.data(), 0);
                }
            } else if (!error.empty())
                g_dump_ui_status = error;
        };
    });
}
void OpenDumpFolder(const std::filesystem::path& path) {
    auto folder = path;
#if defined(_WIN32)
    // Shell paths must not retain the extended spelling used for file I/O.
    auto name = folder.native();
    if (name.starts_with(L"\\\\?\\UNC\\"))
        folder = std::filesystem::path(L"\\\\" + name.substr(8));
    else if (name.starts_with(L"\\\\?\\"))
        folder = std::filesystem::path(name.substr(4));
#endif
    dkr::runtime::support::open_directory(folder, g_dump_ui_status);
}
void DrawDumpDestination(float width, bool locked) {
    PaddockGap(14.F);
    SettingsLabel("Save to", width);
    const bool standard = g_dump_options.destination == g_config_directory / "texture-dumps";
    DetailText(standard ? "Default texture dump folder" : DumpPathText(g_dump_options.destination), width, kSetMuted,
               10.F);
    DumpActionRow actions(width);
    if (actions.Button("Change folder", locked))
        ChooseDumpFolder();
    if (actions.Button("Show path"))
        g_dump_folder_info = true;
    PaddockGap(18.F);
}
void DrawDumpJob(float width, const std::shared_ptr<const dumping::View>& state) {
    if (!state->busy && !state->capturing && state->status.empty() && state->error.empty() && g_dump_ui_status.empty())
        return;
    PaddockGap(18.F);
    DetailCard(width, {16.F, 14.F}, [&](float inner) {
        const char* title = state->capturing ? (state->paused ? "CAPTURE PAUSED" : "CAPTURING TEXTURES")
                            : state->busy    ? "WORKING"
                                             : "DUMP STATUS";
        DetailHeading(title, inner, kSetWarm, 8.F);
        DetailText(state->status, inner, kSetMuted, 8.F);
        if (state->exported || state->reused || state->failed)
            DetailText("Saved: " + std::to_string(state->exported) + "    Already saved: " +
                           std::to_string(state->reused) + "    Skipped: " + std::to_string(state->failed),
                       inner, kSetText, 8.F);
        DetailText(state->error, inner, kSetWarm, 8.F);
        DetailText(g_dump_ui_status, inner, kSetMuted, 8.F);
        DumpActionRow actions(inner);
        if (state->capturing) {
            if (state->paused && actions.Button("Resume capture"))
                dumping::resume_capture();
            if (actions.Button("Stop capture"))
                dumping::stop_capture();
        } else if (state->busy) {
            if (actions.Button("Stop job"))
                dumping::cancel();
        } else if (!state->folder.empty() && g_dump_panel != DumpPanel::Dumps) {
            if (actions.Button("View dump"))
                g_dump_panel = DumpPanel::Dumps;
        }
    });
}
void DrawDumpPreviewCard(const dumping::Thumbnail& item, float width) {
    constexpr float height = 220.F;
    const auto at = ImGui::GetCursorScreenPos();
    const ImVec2 end{at.x + width, at.y + height};
    ImGui::Dummy({width, height});
    if (!ImGui::IsRectVisible(at, end))
        return;
    auto* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, at, end, PaddockRound(12.F), PaddockCol(0x0B2E40));
    PaddockStroke(draw, at, end, PaddockRound(12.F), PaddockCol(0x296B70), 1.F);
    const float image_x = at.x + (width - 96.F) * .5F, image_y = at.y + 12.F;
    for (unsigned y = 0; y < 6; ++y)
        for (unsigned x = 0; x < 6; ++x)
            draw->AddRectFilled({image_x + x * 16.F, image_y + y * 16.F},
                                {image_x + (x + 1) * 16.F, image_y + (y + 1) * 16.F},
                                (x + y) & 1 ? PaddockCol(0x344C56) : PaddockCol(0x213843));
    if (g_mod_store_renderer && !item.rgba.empty() && item.preview_width && item.preview_height) {
        auto found = g_dump_previews.find(item.preview_key);
        if (found == g_dump_previews.end()) {
            auto* texture = SDL_CreateTexture(g_mod_store_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                              int(item.preview_width), int(item.preview_height));
            if (texture && SDL_UpdateTexture(texture, nullptr, item.rgba.data(), int(item.preview_width * 4))) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
            if (texture) {
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                found = g_dump_previews.emplace(item.preview_key, DumpPreviewTexture{texture, ImGui::GetFrameCount()})
                            .first;
            }
        }
        if (found != g_dump_previews.end()) {
            found->second.used = ImGui::GetFrameCount();
            const float x = image_x + (96.F - item.preview_width) * .5F,
                        y = image_y + (96.F - item.preview_height) * .5F;
            draw->AddImage(reinterpret_cast<ImTextureID>(found->second.texture), {x, y},
                           {x + item.preview_width, y + item.preview_height});
        }
    }
    const auto title = PaddockReading(14.F, true, 1.35F), detail = PaddockReading(13.F, false, 1.4F);
    const float text_width = std::max(1.F, width - 24.F);
    const auto line = [&](const PaddockType& type, const std::string& text, float y, unsigned colour) {
        const auto shown = PaddockEllipsize(type, text, text_width);
        PaddockDrawRun(draw, type, {at.x + 12.F, at.y + y}, PaddockCol(colour), shown.data(),
                       shown.data() + shown.size());
    };
    line(title, item.name, 120.F, kSetText);
    line(detail, item.owner, 144.F, kSetMuted);
    line(detail,
         std::to_string(item.width) + " x " + std::to_string(item.height) + " | frame " +
             std::to_string(item.frame + 1),
         166.F, kSetMuted);
    line(detail, item.preview_error.empty() ? item.status : "Preview unavailable", 190.F,
         item.preview_error.empty() ? kSetMuted : kSetWarm);
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(at, end)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320.F);
        ImGui::TextUnformatted(item.name.c_str());
        if (!item.preview_error.empty())
            ImGui::TextWrapped("%s", item.preview_error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
void RequestDumpPage(const std::filesystem::path& folder, unsigned page, bool reload = false) {
    g_dump_browser_status.clear();
    dumping::gallery(folder, g_dump_search.data(), page, reload);
}
void DrawDumpBrowser(float width, const std::shared_ptr<const dumping::View>& state) {
    TrimDumpPreviews();
    if (!g_dump_browser)
        return;
    ImGui::OpenPopup("Texture browser##dump");
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(1200.F, display.x - 32.F), std::min(display.y - 32.F, display.y * .9F)},
                             ImGuiCond_Always);
    ImGui::SetNextWindowPos({display.x * .5F, display.y * .5F}, ImGuiCond_Always, {.5F, .5F});
    // Use the actual padded/rounded launcher frame, not a raw ImGui popup.
    if (BeginPaddockModalWindow("Texture browser##dump", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                             ImGuiWindowFlags_NoScrollbar |
                                                             ImGuiWindowFlags_NoScrollWithMouse)) {
        const float available = ImGui::GetContentRegionAvail().x;
        const auto folder = state->gallery_folder.empty() ? state->folder : state->gallery_folder;
        const bool blocked = state->busy || state->capturing;
        const unsigned target = state->gallery_loading ? state->gallery_requested_page : state->gallery_page;
        SettingsLabel("Search textures", available);
        {
            const PaddockPress press = PaddockBeginPress("##dump-search", {available, 48.F});
            auto* draw = ImGui::GetWindowDrawList();
            PaddockFill(draw, press.min, press.max, PaddockRound(12.F), PaddockCol(0x0B2E40));
            PaddockStroke(draw, press.min, press.max, PaddockRound(12.F), PaddockCol(kSetBorder), 1.F);
            const auto type = PaddockReading(16.F, false, 1.4F);
            const auto shown =
                PaddockEllipsize(type, g_dump_search[0] ? g_dump_search.data() : "Find a texture...", available - 32.F);
            PaddockDrawRun(draw, type, {press.min.x + 16.F, press.min.y + (48.F - type.line) * .5F},
                           PaddockCol(kSetText), shown.data(), shown.data() + shown.size());
            PaddockEndPress(press, 12.F);
            if (press.pressed && !blocked)
                RequestCustomTextKeyboard(
                    "Find a texture", g_dump_search.data(), g_dump_search.size(), [folder](const std::string& value) {
                        std::snprintf(g_dump_search.data(), g_dump_search.size(), "%s", value.c_str());
                        RequestDumpPage(folder, 0);
                    });
        }
        PaddockGap(12.F);
        DumpActionRow pages(available);
        if (pages.Button("First", blocked || !target))
            RequestDumpPage(folder, 0);
        if (pages.Button("Previous", blocked || !target))
            RequestDumpPage(folder, target - 1);
        if (pages.Button("Next", blocked || !state->gallery_pages || target + 1 >= state->gallery_pages))
            RequestDumpPage(folder, target + 1);
        if (pages.Button("Last", blocked || !state->gallery_pages || target + 1 >= state->gallery_pages))
            RequestDumpPage(folder, state->gallery_pages - 1);
        if (pages.Button("Go to page", blocked || !state->gallery_pages)) {
            RequestCustomTextKeyboard(
                "Go to page", std::to_string(target + 1), 12,
                [folder, total = state->gallery_pages](const std::string& value) {
                    unsigned page = 0;
                    for (const char digit : value) {
                        if (digit < '0' || digit > '9' || page > total) {
                            g_dump_browser_status = "Enter a page number from 1 to " + std::to_string(total) + ".";
                            return;
                        }
                        page = page * 10 + unsigned(digit - '0');
                    }
                    if (!page || page > total) {
                        g_dump_browser_status = "Enter a page number from 1 to " + std::to_string(total) + ".";
                        return;
                    }
                    RequestDumpPage(folder, page - 1);
                },
                false);
        }
        if (pages.Button("Refresh", blocked))
            RequestDumpPage(folder, target, true);
        if (g_dump_search[0] && pages.Button("Clear search", blocked)) {
            g_dump_search[0] = 0;
            RequestDumpPage(folder, 0);
        }
        PaddockGap(10.F);
        std::string page_label = "Page " + std::to_string(state->gallery_pages ? state->gallery_page + 1 : 0) + " / " +
                                 std::to_string(state->gallery_pages) + "   |   " +
                                 std::to_string(state->gallery_matches) + " textures";
        if (state->gallery_loading)
            page_label += "   |   Loading" +
                          (state->gallery_pages ? " page " + std::to_string(target + 1) : std::string(" textures")) +
                          "...";
        DetailText(page_label, available, kSetMuted, 10.F);
        DetailText(state->gallery_error, available, kSetWarm, 8.F);
        DetailText(g_dump_browser_status, available, kSetWarm, 8.F);
        ImGui::BeginChild("##dump-images", {0, std::max(60.F, ImGui::GetContentRegionAvail().y - 64.F)}, false,
                          ImGuiWindowFlags_NavFlattened);
        if (g_dump_preview_generation != state->gallery_generation) {
            ImGui::SetScrollY(0.F);
            g_dump_preview_generation = state->gallery_generation;
        }
        const float grid_width = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, int(grid_width / 216.F));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6.F, 6.F});
        if (ImGui::BeginTable("##dump-grid", columns, ImGuiTableFlags_SizingStretchSame)) {
            ImGuiListClipper clipper;
            clipper.Begin(int((state->thumbnails.size() + columns - 1) / columns), 232.F);
            while (clipper.Step())
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (int column = 0; column < columns; ++column) {
                        const auto index = std::size_t(row) * columns + column;
                        if (index >= state->thumbnails.size())
                            break;
                        ImGui::TableSetColumnIndex(column);
                        DrawDumpPreviewCard(state->thumbnails[index], ImGui::GetContentRegionAvail().x);
                    }
                }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        if (state->thumbnails.empty() && !state->gallery_loading && state->gallery_error.empty())
            DetailText("No textures match your search.", grid_width, kSetMuted);
        if (!g_mod_store_renderer)
            DetailText("Image previews are available in the launcher.", grid_width, kSetMuted);
        ImGui::EndChild();
        PaddockGap(14.F);
        bool close = false;
        {
            ImGui::PushID("browser-footer");
            DumpActionRow actions(available);
            close = actions.Button("Close");
            const auto hint_type = PaddockReading(13.F, false, 1.4F);
            const char* hint = "LB / RB or Page Up / Down: change page";
            if (SettingsButtonWidth("Close") + PaddockMeasure(hint_type, hint) + 24.F < available) {
                const auto a = ImGui::GetItemRectMin();
                const auto b = ImGui::GetItemRectMax();
                PaddockDrawRun(ImGui::GetWindowDrawList(), hint_type,
                               {b.x + 20.F, a.y + (b.y - a.y - hint_type.line) * .5F}, PaddockCol(kSetMuted), hint,
                               hint + std::strlen(hint));
            }
            ImGui::PopID();
        }
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused && !blocked) {
            if (target &&
                (ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)))
                RequestDumpPage(folder, target - 1);
            if (state->gallery_pages && target + 1 < state->gallery_pages &&
                (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)))
                RequestDumpPage(folder, target + 1);
        }
        if (focused &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
            close = true;
        if (close) {
            g_dump_browser = false;
            dumping::close_gallery();
            InvalidateTextureDumpPreviews();
            ImGui::CloseCurrentPopup();
        }
        // The keyboard must render at the same popup depth as its search field.
        DrawTextEntryKeyboard();
        ImGui::EndPopup();
    }
}
void DrawTextureDumping(float width) {
    if (g_dump_config != g_config_directory) {
        g_dump_options = {};
        g_dump_config = g_config_directory;
        g_dump_options.config = g_config_directory;
        g_dump_options.destination = g_config_directory / "texture-dumps";
    }
    g_dump_options.rom = g_online_mod_selected_rom;
    auto state = dumping::snapshot();
    ++g_settings_regular_labels;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.F, 0.F});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {.5F, .5F});
    const bool locked = state->busy || state->capturing || state->gallery_loading;
    ImGui::PushID("texture-dumping");
    const ImVec2 header = ImGui::GetCursorScreenPos();
    const bool expanded = DetailSectionSign("TEXTURE DUMPING##texture-dump", g_dump_open, width);
    const std::string summary =
        state->capturing
            ? (state->paused ? "Capture paused" : "Capturing - " + std::to_string(state->exported) + " saved")
        : state->busy           ? "Working..."
        : !state->error.empty() ? "Needs attention"
        : state->folder.empty() ? "Ready"
                                : "Dump ready";
    const auto summary_type = PaddockReading(13.F, true, 1.4F);
    const float summary_width = PaddockMeasure(summary_type, summary);
    if (PaddockSectionTabWidth("TEXTURE DUMPING") + summary_width + 24.F <= width) {
        PaddockDrawRun(ImGui::GetWindowDrawList(), summary_type,
                       {header.x + width - summary_width - 16.F,
                        header.y + 22.F + (PaddockSectionTabHeight() - summary_type.line) * .5F},
                       PaddockCol(kSetText), summary.data(), summary.data() + summary.size());
    } else if (locked || !state->error.empty())
        DetailText(summary, width, kSetMuted, 8.F);
    if (expanded) {
        DumpActionRow tabs(width);
        if (tabs.Button("Export", false,
                        g_dump_panel == DumpPanel::Export ? SettingsButtonTone::Selected : SettingsButtonTone::Plain))
            g_dump_panel = DumpPanel::Export;
        if (tabs.Button("Capture", false,
                        g_dump_panel == DumpPanel::Capture ? SettingsButtonTone::Selected : SettingsButtonTone::Plain))
            g_dump_panel = DumpPanel::Capture;
        if (tabs.Button("My Dumps", false,
                        g_dump_panel == DumpPanel::Dumps ? SettingsButtonTone::Selected : SettingsButtonTone::Plain))
            g_dump_panel = DumpPanel::Dumps;
        PaddockGap(18.F);
        if (g_dump_panel == DumpPanel::Export) {
            DetailCard(width, {18.F, 16.F}, [&](float inner) {
                DetailHeading("WHAT TO EXPORT", inner, kSetWarm, 10.F);
                SettingsColumns columns(inner, inner < 400.F, 10.F);
                const auto check = [&](const char* id, const char* label, bool& value) {
                    columns.Begin();
                    SettingsCheck(id, label, &value, columns.column, {}, locked);
                    columns.End();
                };
                check("##dump-game", "Original game", g_dump_options.game);
                check("##dump-characters", "Characters", g_dump_options.characters);
                check("##dump-tracks", "Tracks", g_dump_options.tracks);
                check("##dump-vehicles", "Vehicles", g_dump_options.vehicles);
                columns.Finish();
                PaddockGap(12.F);
                SettingsCheck("##dump-disabled", "Include disabled mods", &g_dump_options.include_disabled, inner, {},
                              locked);
                SettingsCheck("##dump-artwork", "Include source artwork", &g_dump_options.source_artwork, inner, {},
                              locked);
                PaddockGap(10.F);
                DetailText(g_dump_options.only_selected
                               ? std::to_string(g_dump_options.selected.size()) + " selected sources"
                               : "All sources in the selected categories",
                           inner, kSetMuted, 8.F);
                DumpActionRow actions(inner);
                if (actions.Button("Choose mods", locked)) {
                    g_dump_sources = true;
                    dumping::refresh(g_dump_options);
                }
            });
            DrawDumpDestination(width, locked);
            DumpActionRow actions(width);
            const bool any =
                g_dump_options.game || g_dump_options.characters || g_dump_options.tracks || g_dump_options.vehicles;
            if (actions.Button("EXPORT TEXTURES", locked || !any, SettingsButtonTone::Plain, true)) {
                g_dump_ui_status.clear();
                dumping::export_installed(g_dump_options);
            }
        } else if (g_dump_panel == DumpPanel::Capture) {
            DetailCard(width, {18.F, 16.F}, [&](float inner) {
                DetailHeading("CAPTURE AS YOU PLAY", inner, kSetWarm, 10.F);
                DetailText(
                    "Collect original textures as you encounter them in game, including custom tracks, characters and vehicles.",
                    inner, kSetMuted, 0.F);
            });
            DrawDumpDestination(width, locked);
            DumpActionRow actions(width);
            if (actions.Button("CAPTURE NEXT GAME", locked, SettingsButtonTone::Plain, true)) {
                g_dump_ui_status.clear();
                dumping::capture(g_dump_options);
            }
        } else {
            DetailCard(width, {18.F, 16.F}, [&](float inner) {
                DetailHeading("CURRENT DUMP", inner, kSetWarm, 10.F);
                if (state->folder.empty())
                    DetailText("Open an existing dump, or export textures to get started.", inner, kSetMuted, 0.F);
                else {
                    DetailText(DumpPathText(state->folder.filename()), inner, kSetText, 8.F);
                    DumpActionRow actions(inner);
                    if (actions.Button("Browse textures", locked)) {
                        g_dump_browser_status.clear();
                        g_dump_browser = true;
                        InvalidateTextureDumpPreviews();
                        dumping::gallery(state->folder, g_dump_search.data(), 0);
                    }
                    if (actions.Button("Create starter pack", locked))
                        dumping::starter_pack(state->folder);
                    if (actions.Button("Open folder"))
                        OpenDumpFolder(state->folder);
                }
            });
            PaddockGap(14.F);
            DumpActionRow actions(width);
            if (actions.Button("Open existing dump", locked))
                ChooseDumpFolder(true);
        }
        DrawDumpJob(width, state);
    } else if (!state->error.empty()) {
        DetailText(state->error, width, kSetWarm, 8.F);
    }
    // Modal ownership remains independent of the disclosure/tab visibility.
    {
        const PaddockFlatScope paddock;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.F, 0.F});
        ++g_paddock_modal_windows;
        if (g_dump_sources)
            ImGui::OpenPopup("Choose texture sources##dump");
        auto display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize({std::min(850.F, display.x - 32.F), std::min(display.y - 32.F, display.y * .75F)},
                                 ImGuiCond_Always);
        ImGui::SetNextWindowPos({display.x * .5F, display.y * .5F}, ImGuiCond_Always, {.5F, .5F});
        if (ImGui::BeginPopupModal("Choose texture sources##dump", nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
            const float inner = ImGui::GetContentRegionAvail().x;
            DetailText(state->busy ? "Loading installed sources..." : "Choose the sources to export.", inner, kSetMuted,
                       8.F);
            bool all = !g_dump_options.only_selected;
            if (SettingsCheck("##all-dump-sources", "All sources", &all, inner, {}, locked)) {
                g_dump_options.only_selected = !all;
                g_dump_options.selected.clear();
                if (!all)
                    for (const auto& source : state->sources)
                        g_dump_options.selected.insert(source.id);
            }
            ImGui::BeginChild("##dump-source-list", {0, std::max(80.F, ImGui::GetContentRegionAvail().y - 58.F)}, false,
                              ImGuiWindowFlags_NavFlattened);
            for (const auto& source : state->sources) {
                bool selected = !g_dump_options.only_selected || g_dump_options.selected.contains(source.id);
                const auto label = source.name + " (" + source.kind + (source.enabled ? "" : ", disabled") + ")";
                ImGui::PushID(source.id.c_str());
                if (SettingsCheck("##source", label, &selected, ImGui::GetContentRegionAvail().x, {}, locked)) {
                    if (!g_dump_options.only_selected) {
                        g_dump_options.only_selected = true;
                        for (const auto& item : state->sources)
                            g_dump_options.selected.insert(item.id);
                    }
                    if (selected)
                        g_dump_options.selected.insert(source.id);
                    else
                        g_dump_options.selected.erase(source.id);
                }
                ImGui::PopID();
                PaddockGap(6.F);
            }
            ImGui::EndChild();
            PaddockGap(10.F);
            if (SettingsButton("Done", std::min(inner, SettingsButtonWidth("Done")))) {
                g_dump_sources = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (g_dump_folder_info)
            ImGui::OpenPopup("Dump folder##dump");
        if (BeginSettingsDialog("Dump folder##dump", "DUMP FOLDER", 720.F)) {
            DetailText(DumpPathText(g_dump_options.destination), ImGui::GetContentRegionAvail().x);
            if (SettingsButton("Close")) {
                g_dump_folder_info = false;
                ImGui::CloseCurrentPopup();
            }
            EndSettingsDialog();
        }
        DrawDumpBrowser(width, state);
        --g_paddock_modal_windows;
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    --g_settings_regular_labels;
}
void DrawTexturesPage(float width) {
    DrawTexturesPageWithoutDump(width);
    DrawTextureDumping(width);
}
