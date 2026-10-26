// Controller-first cup workspace. The shell never scrolls; each screen has
// one body viewport. Round IDs are UI-only and move with their rounds.
float CupActionWidth(std::string_view label) {
    return std::ceil(PaddockMeasure(PaddockReading(16, true, 1.3F), label) + 24);
}
float CupSelectWidth(std::initializer_list<const char*> labels) {
    float width = 0;
    for (const auto* label : labels)
        width = std::max(width, PaddockMeasure(PaddockReading(16, false, 1.4F), label));
    return std::ceil(width + 46);
}
void CupNextAction(std::string_view label, float gap = 8) {
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + gap + CupActionWidth(label) <= right)
        ImGui::SameLine(0, gap);
}
bool CupAction(const char* id, const std::string& label, float width, bool primary = false, float height = 44,
               bool selected = false, const std::string& detail = {}, bool single_line = false) {
    width = std::max(1.0F, std::min(width, ImGui::GetContentRegionAvail().x));
    const auto type = PaddockReading(16, true, 1.3F);
    const float text_width = std::max(1.0F, width - 24);
    const auto shown = single_line || !detail.empty() ? PaddockEllipsize(type, label, text_width) : label;
    const auto lines = PaddockWrap(type, shown, text_width);
    const float text_height = type.line * lines.size();
    height = std::max(height, text_height + 16 + (detail.empty() ? 0 : 28));
    const auto press = PaddockBeginPress(id, {width, height});
    auto* draw = ImGui::GetWindowDrawList();
    const unsigned fill = primary ? 0xFFCA62 : selected ? 0x153E50 : press.hovered ? 0x1C4658 : 0x0B2636;
    draw->AddRectFilled(press.min, press.max, PaddockCol(fill), 8);
    draw->AddRect(press.min, press.max, PaddockCol(primary || selected ? 0xFFCA62 : 0x396174), 8);
    draw->PushClipRect(press.min, press.max, true);
    PaddockTextStyle style;
    style.centre = true;
    style.colour = PaddockCol(primary ? 0x15222B : selected ? 0xFFCA62 : 0xFFF0C2);
    PaddockDrawLines(
        draw, type,
        {press.min.x + 12, std::round(press.min.y + (detail.empty() ? (height - text_height) * 0.5F : 14.0F))},
        text_width, lines, style);
    if (!detail.empty()) {
        const auto detail_type = PaddockReading(14, false, 1.3F);
        const auto text = PaddockEllipsize(detail_type, detail, text_width);
        const float measured = PaddockMeasure(detail_type, text);
        PaddockDrawRun(draw, detail_type, {press.min.x + (width - measured) * 0.5F, press.max.y - 28},
                       PaddockCol(0xB8CBD2), text.data(), text.data() + text.size());
    }
    draw->PopClipRect();
    PaddockEndPress(press, 8, 1);
    if (press.hovered && shown != label) {
        const auto item = GImGui->LastItemData;
        ImGui::SetTooltip("%s", label.c_str());
        GImGui->LastItemData = item;
    }
    return press.pressed;
}
bool CupKey(ImGuiKey key, bool repeat = false) {
    const auto owner = ImGui::GetID("cup-shortcuts");
    ImGui::SetKeyOwner(key, owner, ImGuiInputFlags_LockThisFrame);
    return ImGui::IsKeyPressed(key, owner, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None);
}
bool CupTopmost() {
    return GImGui->BeginPopupStack.Size == GImGui->OpenPopupStack.Size &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
}
void CupBackButton(const char* label = "Back") {
    if (CupAction(label, label, CupActionWidth(label)) ||
        (CupTopmost() && (CupKey(ImGuiKey_GamepadFaceRight) || CupKey(ImGuiKey_Escape))))
        ImGui::CloseCurrentPopup();
}
void CupPopupSize(float width = 760, float height = 560) {
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(
        {std::min(width, std::max(240.0F, display.x - 40)), std::min(height, std::max(200.0F, display.y - 40))},
        ImGuiCond_Appearing);
}
void CupChoose(unsigned selected) {
    auto& e = g_cup_editor;
    if (e.selected != selected) {
        e.selected = selected;
        e.dirty = true;
    }
    e.focused_round = 0;
    e.focused_control = 0;
    e.focus_round = true;
    e.page = 1;
}
void CupRename() {
    const auto id = g_cup_editor.cups[g_cup_editor.selected].id;
    const auto digest = g_code_details;
    RequestCustomTextKeyboard(
        "Name your cup", g_cup_editor.cups[g_cup_editor.selected].name, 161,
        [id, digest](const std::string& text) {
            if (g_code_details != digest || text.empty())
                return;
            auto& e = g_cup_editor;
            const auto cup = std::find_if(e.cups.begin(), e.cups.end(), [&](const auto& c) { return c.id == id; });
            if (cup != e.cups.end() && cup->name != text) {
                cup->name = text;
                e.dirty = true;
            }
        },
        false);
}
void CupMoveRound(int from, int to) {
    auto& e = g_cup_editor;
    auto& c = e.cups[e.selected];
    auto& keys = e.round_keys[c.id];
    if (from < 0 || to < 0 || from >= int(c.rounds.size()) || to >= int(c.rounds.size()) || from == to)
        return;
    auto round = std::move(c.rounds[from]);
    const auto key = keys[from];
    c.rounds.erase(c.rounds.begin() + from);
    keys.erase(keys.begin() + from);
    c.rounds.insert(c.rounds.begin() + to, std::move(round));
    keys.insert(keys.begin() + to, key);
    e.focused_round = to;
    e.focused_control = 0;
    e.focus_round = true;
}
void CupRoundAction(int action) {
    auto& e = g_cup_editor;
    auto& c = e.cups[e.selected];
    auto& keys = e.round_keys[c.id];
    const int i = e.focused_round;
    if (i < 0 || i >= int(c.rounds.size()))
        return;
    if (action == 0) {
        e.move_origin = i;
        e.focused_control = 0;
        e.focus_round = true;
    }
    if (action == 1 && c.rounds.size() < 128) {
        const auto copy = c.rounds[i];
        c.rounds.insert(c.rounds.begin() + i + 1, copy);
        keys.insert(keys.begin() + i + 1, e.next_key++);
        e.focused_round = i + 1;
        e.focused_control = 0;
        e.focus_round = true;
        e.dirty = true;
    }
    if (action == 2) {
        c.rounds.erase(c.rounds.begin() + i);
        keys.erase(keys.begin() + i);
        e.focused_round = std::max(0, std::min(i, int(c.rounds.size()) - 1));
        e.focused_control = 0;
        e.focus_round = true;
        e.dirty = true;
    }
}
void DrawCupTrackPicker() {
    auto& e = g_cup_editor;
    auto& cup = e.cups[e.selected];
    const auto courses = g_code_library.courses();
    CupPopupSize(1000, 760);
    if (!BeginPaddockModalWindow(
            "Choose a track",
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, true))
        return;
    const float width = ImGui::GetContentRegionAvail().x;
    const bool inline_filter = width > 640;
    const auto search = PaddockSearch("cup-track-search", e.search.data(), e.search.size(), "Search tracks...",
                                      inline_filter ? width - 210 : width);
    if (search == PaddockFieldResult::GamepadActivated) {
        const auto digest = g_code_details;
        RequestCustomTextKeyboard("Search tracks", e.search.data(), e.search.size(), [digest](const std::string& text) {
            if (digest == g_code_details) {
                g_cup_editor.search.fill(0);
                std::copy(text.begin(), text.end(), g_cup_editor.search.begin());
            }
        });
    }
    if (inline_filter)
        ImGui::SameLine(0, 10);
    PaddockSelect("track-filter", "", &e.course_filter, {"All tracks", "Original tracks", "Custom tracks"},
                  inline_filter ? 200 : width);
    auto query = std::string(e.search.data());
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char v) { return char(std::tolower(v)); });
    PaddockGap(8);
    ImGui::BeginChild("Track results", {0, -88}, false);
    bool picked = false;
    unsigned matches = 0;
    for (const auto& c : *courses) {
        if (!c.race || (e.course_filter == 1 && !c.id.starts_with("stock:")) ||
            (e.course_filter == 2 && c.id.starts_with("stock:")))
            continue;
        auto lower = c.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char v) { return char(std::tolower(v)); });
        if (lower.find(query) == lower.npos)
            continue;
        ++matches;
        ImGui::PushID(c.id.c_str());
        if (CupAction("track", c.name, ImGui::GetContentRegionAvail().x, false, 44, false, {}, true)) {
            if (e.replace_round >= 0 && e.replace_round < int(cup.rounds.size())) {
                auto& round = cup.rounds[e.replace_round];
                round.course = c.id;
                round.vehicle = 255;
                e.focused_round = e.replace_round;
            } else if (cup.rounds.size() < 128) {
                cup.rounds.push_back({c.id, 3, 255});
                e.round_keys[cup.id].push_back(e.next_key++);
                e.focused_round = int(cup.rounds.size()) - 1;
            }
            e.dirty = true;
            e.focused_control = 0;
            e.focus_round = true;
            picked = true;
        }
        if (matches == 1 && ImGui::IsWindowAppearing())
            ImGui::SetItemDefaultFocus();
        ImGui::PopID();
        PaddockGap(5);
    }
    if (!matches)
        PaddockModalText("No matching tracks. Enable custom tracks in Mods to add them here.");
    ImGui::EndChild();
    PaddockGap(10);
    if (picked) {
        e.replace_round = -1;
        ImGui::CloseCurrentPopup();
    }
    CupBackButton();
    DrawTextEntryKeyboard();
    ImGui::EndPopup();
}
void DrawCupImportReview(const dkr::mods::code::LibraryItem& item) {
    namespace trophy = dkr::mods::code::trophy;
    auto& e = g_cup_editor;
    CupPopupSize(900, 720);
    if (!BeginPaddockModalWindow("Import trophy cups", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                           ImGuiWindowFlags_NoScrollWithMouse))
        return;
    const auto incoming = g_code_library.imported_cups();
    const auto requirements = trophy::share_requirements(*incoming, *g_code_library.courses());
    auto existing = e.cups;
    if (!item.data.contains("trophies") && existing.size() == 1 && existing.front().rounds.empty() &&
        existing.front().name == "My Trophy Race")
        existing.clear();
    auto merged = existing;
    std::string error;
    try {
        merged = trophy::merge_shared(existing, *incoming);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    PaddockModalText("Your existing cups are kept. Matching cups are skipped.");
    ImGui::BeginChild("Import preview", {0, -88}, false);
    for (const auto& c : incoming->cups)
        PaddockModalText(c.name + " / " + std::to_string(c.rounds.size()) + " rounds");
    PaddockGap(12);
    PaddockHeading("Required tracks", 21, 0xFFCA62, ImGui::GetContentRegionAvail().x);
    for (const auto& t : incoming->tracks)
        PaddockModalText(t.name + (t.id.starts_with("stock:") ? " / original" : " / custom"));
    if (!requirements.empty())
        PaddockModalText("Install and enable the missing tracks before importing:");
    for (const auto& issue : requirements)
        PaddockModalText(issue);
    if (!error.empty())
        PaddockModalText(error);
    ImGui::EndChild();
    PaddockGap(10);
    ImGui::BeginDisabled(!requirements.empty() || !error.empty() || incoming->cups.empty());
    if (PaddockButton("Add cups", PaddockButtonKind::Primary)) {
        if (merged != e.cups) {
            e.cups = std::move(merged);
            e.selected = 0;
            e.dirty = true;
            e.page = 0;
            e.notice = "Cups added. Save changes to use them in Track Select.";
        } else
            e.notice = "These cups are already in your collection.";
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    CupNextAction("Cancel");
    CupBackButton("Cancel");
    ImGui::EndPopup();
}
std::string CupPathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
void CupBrowse(const std::filesystem::path& folder) {
    auto& e = g_cup_editor;
    const auto generation = g_code_library.view()->generation;
    if (g_code_library.browse_cups(folder)) {
        e.browse_pending = true;
        e.browse_generation = generation;
        e.error.clear();
    }
}
void CupFileSubmit() {
    auto& e = g_cup_editor;
    std::string filename = e.filename.data();
    if (filename.empty() || filename == "." || filename == ".." ||
        filename.find_first_of("/\\:") != std::string::npos) {
        e.error = "Choose a filename without path separators.";
        return;
    }
    auto path = e.file_folder / std::filesystem::u8path(filename);
    if (e.file_mode && path.extension() != ".dkrcup")
        path += ".dkrcup";
    const auto generation = g_code_library.view()->generation;
    const bool submitted =
        e.file_mode ? g_code_library.export_cups(
                          path, e.file_mode == 2 ? e.cups
                                                 : std::vector<dkr::mods::code::trophy::Definition>{e.cups[e.selected]})
                    : g_code_library.import_cups(path);
    if (submitted) {
        e.transfer_pending = e.file_mode ? 2 : 1;
        e.transfer_generation = generation;
        e.error.clear();
        e.notice.clear();
    }
}
void DrawCupFilePicker() {
    auto& e = g_cup_editor;
    if (e.file_requested) {
        e.file_requested = false;
        ImGui::OpenPopup("Cup files");
        CupBrowse(e.file_folder.empty() ? g_config_directory : e.file_folder);
    }
    CupPopupSize(1000, 800);
    if (!BeginPaddockModalWindow("Cup files", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                  ImGuiWindowFlags_NoScrollWithMouse))
        return;
    const auto view = g_code_library.view();
    const auto listing = g_code_library.cup_directory();
    if (e.browse_pending && !view->busy && view->generation > e.browse_generation) {
        e.browse_pending = false;
        if (!view->error.empty())
            e.error = view->error;
        else {
            e.file_folder = listing->path;
            const auto text = CupPathText(listing->path);
            e.folder.fill(0);
            std::copy_n(text.begin(), std::min(text.size(), e.folder.size() - 1), e.folder.begin());
        }
    }
    const float width = ImGui::GetContentRegionAvail().x;
    PaddockModalText(e.file_mode ? "Choose an export folder and filename." : "Choose a .dkrcup file to import.");
    ImGui::BeginDisabled(view->busy);
    const float go_width = CupActionWidth("Go");
    const auto field = PaddockSearch("cup-folder", e.folder.data(), e.folder.size(), "Folder path",
                                     std::max(1.0F, width - go_width - 8));
    if (field == PaddockFieldResult::GamepadActivated) {
        RequestCustomTextKeyboard("Folder path", e.folder.data(), e.folder.size(), [](const std::string& text) {
            auto& e = g_cup_editor;
            e.folder.fill(0);
            std::copy(text.begin(), text.end(), e.folder.begin());
        });
    }
    ImGui::SameLine(0, 8);
    if (CupAction("go", "Go", go_width))
        CupBrowse(std::filesystem::u8path(e.folder.data()));
    if (CupAction("parent", "Up a folder", CupActionWidth("Up a folder")))
        CupBrowse(e.file_folder.parent_path());
    CupNextAction("Downloads");
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home)
        home = std::getenv("HOME");
    if (CupAction("downloads", "Downloads", CupActionWidth("Downloads")) && home && *home)
        CupBrowse(std::filesystem::u8path(home) / "Downloads");
    CupNextAction("System picker");
    if (CupAction("system", "System picker", CupActionWidth("System picker"))) {
        CupFileDialog(e.file_mode != 0, e.file_mode == 2);
        if (e.transfer_pending)
            ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    PaddockGap(8);
    ImGui::BeginChild("Cup folders and files", {0, -180}, false);
    if (view->busy)
        PaddockModalText("Reading folder...");
    else
        for (const auto& entry : listing->entries) {
            const auto text = CupPathText(entry.path.filename());
            ImGui::PushID(text.c_str());
            if (CupAction("entry", (entry.directory ? ">  " : "    ") + text, ImGui::GetContentRegionAvail().x, false,
                          44, false, {}, true)) {
                if (entry.directory)
                    CupBrowse(entry.path);
                else {
                    e.filename.fill(0);
                    std::copy_n(text.begin(), std::min(text.size(), e.filename.size() - 1), e.filename.begin());
                }
            }
            ImGui::PopID();
            PaddockGap(4);
        }
    if (listing->limited)
        PaddockModalText("This folder is large. Use a subfolder or the System picker to find other files.");
    ImGui::EndChild();
    PaddockGap(8);
    ImGui::BeginDisabled(view->busy || e.file_folder.empty());
    const auto file = PaddockSearch("cup-filename", e.filename.data(), e.filename.size(), "Filename.dkrcup", width);
    if (file == PaddockFieldResult::GamepadActivated) {
        RequestCustomTextKeyboard("Cup filename", e.filename.data(), e.filename.size(), [](const std::string& text) {
            auto& e = g_cup_editor;
            e.filename.fill(0);
            std::copy(text.begin(), text.end(), e.filename.begin());
        });
    }
    if (PaddockButton(e.file_mode ? "Export" : "Import", PaddockButtonKind::Primary)) {
        if (e.file_mode)
            ImGui::OpenPopup("Export cup file?");
        else {
            CupFileSubmit();
            if (e.transfer_pending)
                ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    CupNextAction("Cancel");
    CupBackButton("Cancel");
    if (!e.error.empty())
        PaddockModalText(e.error);
    CupPopupSize(700, 350);
    if (BeginPaddockModalWindow("Export cup file?", ImGuiWindowFlags_NoSavedSettings)) {
        PaddockModalText("Export to " + CupPathText(e.file_folder / std::filesystem::u8path(e.filename.data())) + "?");
        PaddockModalText("An existing file with this name will be replaced.");
        if (PaddockButton("Export", PaddockButtonKind::Primary)) {
            CupFileSubmit();
            ImGui::CloseCurrentPopup();
        }
        CupNextAction("Cancel");
        CupBackButton("Cancel");
        ImGui::EndPopup();
    }
    // An accepted export closes the picker on the following frame, after its
    // nested confirmation has relinquished the popup stack.
    if (e.transfer_pending == 2 && GImGui->OpenPopupStack.Size == GImGui->BeginPopupStack.Size)
        ImGui::CloseCurrentPopup();
    DrawTextEntryKeyboard();
    ImGui::EndPopup();
}
void DrawCupOptions(const dkr::mods::code::LibraryItem& item) {
    auto& e = g_cup_editor;
    CupPopupSize(700, 660);
    if (BeginPaddockModalWindow("Cup options",
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse,
                                true)) {
        ImGui::BeginChild("Cup options body", {0, -88}, false);
        auto& cup = e.cups[e.selected];
        const float width = ImGui::GetContentRegionAvail().x;
        std::array<char, 161> name{};
        std::copy_n(cup.name.data(), std::min(cup.name.size(), name.size() - 1), name.data());
        PaddockModalText("Cup name");
        const auto field = PaddockSearch("cup-name", name.data(), name.size(), "Cup name", width);
        if (field == PaddockFieldResult::Edited) {
            cup.name = name.data();
            e.dirty = true;
        }
        if (field == PaddockFieldResult::GamepadActivated)
            CupRename();
        PaddockGap(12);
        ImGui::BeginDisabled(e.cups.size() >= 32);
        if (CupAction("duplicate-cup", "Duplicate cup", width)) {
            auto copy = cup;
            copy.id = NewCupId();
            copy.name = copy.name.substr(0, TextEntryPrefix(copy.name.c_str(), copy.name.size(), 150)) + " copy";
            e.cups.push_back(std::move(copy));
            CupChoose(unsigned(e.cups.size() - 1));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        PaddockGap(8);
        ImGui::BeginDisabled(e.cups.size() <= 1);
        if (CupAction("delete-cup", "Delete cup", width))
            ImGui::OpenPopup("Delete this cup?");
        ImGui::EndDisabled();
        PaddockGap(8);
        if (e.cups[e.selected].shuffle && CupAction("shuffle", "Shuffle again", width)) {
            ++e.seed;
            e.dirty = true;
        }
        if (width < 740) {
            int shuffled = e.cups[e.selected].shuffle ? 1 : 0;
            if (PaddockSelect("order", "Race order", &shuffled, {"Lineup order", "Shuffled order"}, width)) {
                e.cups[e.selected].shuffle = shuffled == 1;
                e.dirty = true;
            }
        }
        bool enabled = item.enabled;
        ImGui::BeginDisabled(!item.data.contains("trophies"));
        if (ImGui::Checkbox("Enabled for next game", &enabled))
            g_code_library.enable(item.digest, enabled);
        ImGui::EndDisabled();
        if (CupAction("refresh", "Refresh tracks", width))
            RequestCupCourses();
        PaddockGap(8);
        if (CupAction("permission", "Code mod options", width))
            ImGui::OpenPopup("Code mod options");
        PaddockGap(12);
        CupPopupSize(620, 340);
        if (BeginPaddockModalWindow("Delete this cup?", ImGuiWindowFlags_NoSavedSettings)) {
            PaddockModalText("Delete " + e.cups[e.selected].name + "?");
            PaddockGap(12);
            if (PaddockButton("Delete cup", PaddockButtonKind::Stop)) {
                e.round_keys.erase(e.cups[e.selected].id);
                e.cups.erase(e.cups.begin() + e.selected);
                CupChoose(0);
                e.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            CupNextAction("Keep cup");
            CupBackButton("Keep cup");
            ImGui::EndPopup();
        }
        CupPopupSize(700, 430);
        if (BeginPaddockModalWindow("Code mod options", ImGuiWindowFlags_NoSavedSettings)) {
            PaddockModalText(item.name + " / " + item.version);
            if (e.dirty)
                PaddockModalText("Save or discard your cup changes before removing this mod or its permission.");
            ImGui::BeginDisabled(e.dirty);
            if (PaddockButton("Revoke code permission", PaddockButtonKind::Plain) &&
                g_code_library.approve(item.digest, false))
                ImGui::ClosePopupToLevel(GImGui->BeginPopupStack.Size - 2, true);
            if (PaddockButton("Remove from library", PaddockButtonKind::Stop))
                ImGui::OpenPopup("Remove trophy code mod?");
            ImGui::EndDisabled();
            CupBackButton();
            if (BeginPaddockModalWindow("Remove trophy code mod?", ImGuiWindowFlags_AlwaysAutoResize)) {
                PaddockModalText(
                    "Remove this mod and its saved cups from your library? Export cups first if you want to keep them.");
                if (PaddockButton("Remove mod", PaddockButtonKind::Stop) && g_code_library.remove(item.digest)) {
                    e.close_requested = true;
                    ImGui::CloseCurrentPopup();
                }
                CupNextAction("Keep mod");
                CupBackButton("Keep mod");
                ImGui::EndPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        PaddockGap(8);
        CupBackButton();
        DrawTextEntryKeyboard();
        ImGui::EndPopup();
    }
}
void DrawCupRoundActions() {
    auto& e = g_cup_editor;
    auto& cup = e.cups[e.selected];
    CupPopupSize(620, 660);
    if (!BeginPaddockModalWindow(
            "Round actions",
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, true))
        return;
    ImGui::BeginChild("Round options body", {0, -88}, false);
    const float width = ImGui::GetContentRegionAvail().x;
    PaddockModalText("Round " + std::to_string(e.focused_round + 1));
    if (!cup.rounds.empty()) {
        auto& round = cup.rounds[std::clamp(e.focused_round, 0, int(cup.rounds.size()) - 1)];
        int laps = int(round.laps);
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderInt("##round-laps", &laps, 1, 99, "%d laps",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            round.laps = unsigned(laps);
            e.dirty = true;
        }
        const auto courses = g_code_library.courses();
        const auto course =
            std::find_if(courses->begin(), courses->end(), [&](const auto& c) { return c.id == round.course; });
        std::vector<std::string> names = {"Track default"};
        std::vector<unsigned> values = {255};
        int selected = 0;
        constexpr const char* vehicles[] = {"Car", "Hovercraft", "Plane"};
        for (unsigned v = 0; v < 3; ++v)
            if (course == courses->end() || (course->vehicles & (1U << v))) {
                if (round.vehicle == v)
                    selected = int(values.size());
                names.push_back(vehicles[v]);
                values.push_back(v);
            }
        if (PaddockSelect("round-vehicle", "Vehicle", &selected, names, width)) {
            round.vehicle = values[selected];
            e.dirty = true;
        }
        PaddockGap(8);
    }
    if (CupAction("move", "Move round", width)) {
        CupRoundAction(0);
        ImGui::CloseCurrentPopup();
    }
    ImGui::BeginDisabled(cup.rounds.size() >= 128);
    if (CupAction("copy", "Copy round", width)) {
        CupRoundAction(1);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (CupAction("remove", "Remove round", width)) {
        CupRoundAction(2);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndChild();
    PaddockGap(8);
    CupBackButton();
    ImGui::EndPopup();
}
void DrawCupLineup() {
    namespace trophy = dkr::mods::code::trophy;
    auto& e = g_cup_editor;
    auto& cup = e.cups[e.selected];
    auto& keys = e.round_keys[cup.id];
    const auto courses = g_code_library.courses();
    const float width = ImGui::GetContentRegionAvail().x;
    const float gap = 6, index = std::ceil(PaddockMeasure(PaddockReading(16, true, 1.3F), "128")) + 2;
    const float step = 44, number = std::max(44.0F, CupActionWidth("99"));
    const float vehicle = width >= 680 ? CupSelectWidth({"Default", "Hovercraft", "Unavailable"}) : 0;
    const float laps = width >= 780 ? step * 2 + number + 6 : width >= 420 ? number : 0;
    const float full_fixed =
        index + laps + vehicle + 94 + CupActionWidth("Copy") + CupActionWidth("Remove") + 20 + 6 * gap;
    const bool full = width >= full_fixed + 220;
    const float move = full ? 94.0F : 0, copy = full ? CupActionWidth("Copy") : 0;
    const float remove = CupActionWidth(full ? "Remove" : "Actions");
    const int columns = 3 + (laps > 0 ? 1 : 0) + (vehicle > 0 ? 1 : 0) + (full ? 2 : 0);
    const float track =
        std::max(1.0F, width - 20 - index - laps - vehicle - move - copy - remove - gap * (columns - 1));
    const float x = ImGui::GetCursorPosX();
    const float y = ImGui::GetCursorPosY();
    const std::array<float, 7> sizes = {index, track, laps, vehicle, move, copy, remove};
    const std::array<const char*, 7> labels = {
        "#", "Track", "Laps", "Vehicle", "Move", "Copy", full ? "Remove" : "Actions"};
    float at = x + 10;
    for (unsigned col = 0; col < sizes.size(); ++col)
        if (sizes[col] > 0) {
            ImGui::SetCursorPos({at, y});
            PaddockText(PaddockReading(14, true, 1.3F), PaddockRgb(0xB8CBD2), labels[col], sizes[col]);
            at += sizes[col] + gap;
        }
    ImGui::SetCursorPos({x, y + 28});
    int erase = -1, duplicate = -1, move_from = -1, move_to = -1;
    for (unsigned i = 0; i < cup.rounds.size(); ++i) {
        auto& round = cup.rounds[i];
        ImGui::PushID(cup.id.c_str());
        ImGui::PushID(int(keys[i]));
        const auto origin = ImGui::GetCursorScreenPos();
        const auto local = ImGui::GetCursorPos();
        auto* draw = ImGui::GetWindowDrawList();
        const bool selected = e.focused_round == int(i);
        draw->AddRectFilled(origin, {origin.x + width, origin.y + 60}, PaddockCol(selected ? 0x153E50 : 0x103449), 8);
        draw->AddRect(origin, {origin.x + width, origin.y + 60}, PaddockCol(selected ? 0xFFCA62 : 0x396174), 8, 0,
                      selected ? 2.0F : 1.0F);
        const auto course =
            std::find_if(courses->begin(), courses->end(), [&](const auto& c) { return c.id == round.course; });
        const auto name = course == courses->end() ? "Missing track" : course->name;
        float col_x = local.x + 10;
        const auto pos = [&](float size) {
            ImGui::SetCursorPos({col_x, local.y + 8});
            col_x += size + gap;
        };
        const auto focus = [&](int col) {
            if (e.focus_round && e.focused_round == int(i) && e.focused_control == col &&
                !(GImGui->LastItemData.InFlags & ImGuiItemFlags_Disabled) && ImGui::GetItemID() != 0) {
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                ImGui::SetScrollHereY(0.5F);
                GImGui->NavDisableHighlight = false;
                e.focus_round = false;
            }
            if (ImGui::IsItemFocused()) {
                e.focused_round = int(i);
                e.focused_control = col;
            }
        };
        pos(index);
        PaddockText(PaddockReading(16, true, 1.3F), PaddockRgb(0xFFCA62), std::to_string(i + 1), index);
        pos(track);
        if (CupAction("track", name, track, false, 44, false, {}, true)) {
            e.replace_round = int(i);
            e.pick_requested = true;
            e.search.fill(0);
        }
        focus(0);
        if (laps > number) {
            pos(laps);
            ImGui::BeginGroup();
            ImGui::BeginDisabled(round.laps <= 1);
            if (CupAction("less", "-", step)) {
                --round.laps;
                e.dirty = true;
            }
            ImGui::EndDisabled();
            focus(1);
            ImGui::SameLine(0, 3);
            if (CupAction("laps", std::to_string(round.laps), number))
                ImGui::OpenPopup("Set laps");
            focus(2);
            ImGui::SameLine(0, 3);
            ImGui::BeginDisabled(round.laps >= 99);
            if (CupAction("more", "+", step)) {
                ++round.laps;
                e.dirty = true;
            }
            ImGui::EndDisabled();
            focus(3);
            ImGui::EndGroup();
        } else if (laps > 0) {
            pos(laps);
            if (CupAction("laps", std::to_string(round.laps), laps))
                ImGui::OpenPopup("Set laps");
            focus(2);
        }
        if (vehicle > 0) {
            pos(vehicle);
            std::vector<std::string> names = {"Default"};
            std::vector<unsigned> values = {255};
            int chosen = 0;
            constexpr const char* vehicles[] = {"Car", "Hovercraft", "Plane"};
            for (unsigned v = 0; v < 3; ++v)
                if (course == courses->end() || (course->vehicles & (1U << v))) {
                    if (round.vehicle == v)
                        chosen = int(values.size());
                    names.push_back(vehicles[v]);
                    values.push_back(v);
                }
            if (round.vehicle != 255 && std::find(values.begin(), values.end(), round.vehicle) == values.end()) {
                chosen = int(values.size());
                names.push_back("Unavailable");
                values.push_back(round.vehicle);
            }
            if (PaddockSelect("vehicle", "", &chosen, names, vehicle)) {
                round.vehicle = values[chosen];
                e.dirty = true;
            }
            focus(4);
        }
        if (full) {
            pos(move);
            ImGui::BeginGroup();
            ImGui::BeginDisabled(i == 0);
            if (CupAction("up", "\xE2\x86\x91", 44)) {
                move_from = int(i);
                move_to = int(i) - 1;
            }
            ImGui::EndDisabled();
            focus(5);
            ImGui::SameLine(0, 6);
            ImGui::BeginDisabled(i + 1 == cup.rounds.size());
            if (CupAction("down", "\xE2\x86\x93", 44)) {
                move_from = int(i);
                move_to = int(i) + 1;
            }
            ImGui::EndDisabled();
            focus(6);
            ImGui::EndGroup();
            pos(copy);
            ImGui::BeginDisabled(cup.rounds.size() >= 128);
            if (CupAction("copy", "Copy", copy))
                duplicate = int(i);
            ImGui::EndDisabled();
            focus(7);
        }
        pos(remove);
        if (CupAction("remove", full ? "Remove" : "Actions", remove)) {
            if (full)
                erase = int(i);
            else {
                e.focused_round = int(i);
                e.round_actions = true;
            }
        }
        focus(8);
        CupPopupSize(600, 340);
        if (BeginPaddockModalWindow("Set laps", ImGuiWindowFlags_NoSavedSettings)) {
            PaddockModalText(name);
            int count = int(round.laps);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##lap-count", &count, 1, 99, "%d laps",
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                round.laps = unsigned(count);
                e.dirty = true;
            }
            CupBackButton("Done");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
        ImGui::SetCursorPos({local.x, local.y + 68});
    }
    if (cup.rounds.empty()) {
        PaddockGap(20);
        PaddockHeading("Your lineup starts here", 24, 0xFFF0C2, width);
        PaddockModalText("Choose Add track to build your cup.");
    }
    if (erase >= 0) {
        e.focused_round = erase;
        CupRoundAction(2);
    } else if (duplicate >= 0) {
        e.focused_round = duplicate;
        CupRoundAction(1);
    } else if (move_from >= 0) {
        CupMoveRound(move_from, move_to);
        e.dirty = true;
    }
    ImGui::Dummy({0, 0});
}
void DrawCustomCupEditor(const dkr::mods::code::LibraryItem& item, bool locked) {
    namespace trophy = dkr::mods::code::trophy;
    auto& e = g_cup_editor;
    if (!e.loaded) {
        e = CupEditor{};
        e.loaded = true;
        try {
            if (item.data.contains("trophies")) {
                const auto& data = item.data.at("trophies");
                e.seed = data.at("seed").get<std::uint64_t>();
                for (const auto& cup : data.at("cups")) {
                    const auto text = cup.dump();
                    e.cups.push_back(trophy::decode(
                        dkr::mods::View(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
                    if (e.cups.back().id == data.at("selected").get<std::string>())
                        e.selected = unsigned(e.cups.size() - 1);
                }
            }
        } catch (const std::exception& ex) {
            e.error = ex.what();
        }
        if (e.cups.empty()) {
            e.cups.push_back({"cup_1", "My Trophy Race", false, {}});
            e.dirty = true;
        }
        e.focus_round = true;
    }
    const auto view = g_code_library.view();
    if (!e.catalog_requested && !view->busy)
        RequestCupCourses();
    if (e.catalog_pending && !view->busy && view->generation > e.job_generation) {
        e.catalog_pending = false;
        e.catalog_ready = view->error.empty();
        if (!e.catalog_ready)
            e.error = view->error;
    }
    if (e.save_pending && !view->busy) {
        e.save_pending = false;
        const auto committed = std::find_if(view->items.begin(), view->items.end(),
                                            [&](const auto& c) { return c.digest == item.digest; });
        if (committed != view->items.end() && committed->data == e.pending) {
            e.dirty = false;
            e.error.clear();
        } else
            e.error = view->error.empty() ? "Your changes could not be saved. Please try again." : view->error;
    }
    if (e.transfer_pending && !view->busy && view->generation > e.transfer_generation) {
        if (!view->error.empty())
            e.error = view->error;
        else if (e.transfer_pending == 1)
            ImGui::OpenPopup("Import trophy cups");
        else
            e.notice = "Cups exported. Share the .dkrcup file with the required track mods.";
        e.transfer_pending = 0;
    }
    e.selected = std::min(e.selected, unsigned(e.cups.size() - 1));
    for (const auto& c : e.cups) {
        auto& keys = e.round_keys[c.id];
        while (keys.size() < c.rounds.size())
            keys.push_back(e.next_key++);
        keys.resize(c.rounds.size());
    }
    const bool top = CupTopmost(), moving = e.move_origin >= 0;
    if (top && moving) {
        if (CupKey(ImGuiKey_GamepadDpadUp, true) || CupKey(ImGuiKey_UpArrow, true))
            CupMoveRound(e.focused_round, e.focused_round - 1);
        if (CupKey(ImGuiKey_GamepadDpadDown, true) || CupKey(ImGuiKey_DownArrow, true))
            CupMoveRound(e.focused_round, e.focused_round + 1);
        if (CupKey(ImGuiKey_GamepadFaceRight) || CupKey(ImGuiKey_Escape)) {
            CupMoveRound(e.focused_round, e.move_origin);
            e.move_origin = -1;
        } else if (CupKey(ImGuiKey_GamepadFaceDown) || CupKey(ImGuiKey_Enter)) {
            e.dirty |= e.focused_round != e.move_origin;
            e.move_origin = -1;
        }
    } else if (top && !locked && !view->busy) {
        if (CupKey(ImGuiKey_GamepadL1))
            CupChoose((e.selected + unsigned(e.cups.size()) - 1) % unsigned(e.cups.size()));
        if (CupKey(ImGuiKey_GamepadR1))
            CupChoose((e.selected + 1) % unsigned(e.cups.size()));
        if (CupKey(ImGuiKey_GamepadStart) && e.catalog_ready)
            SaveCustomCups(item);
        if (CupKey(ImGuiKey_GamepadFaceLeft) && e.catalog_ready && e.cups[e.selected].rounds.size() < 128) {
            e.page = 1;
            e.replace_round = -1;
            e.pick_requested = true;
            e.search.fill(0);
        }
        if (CupKey(ImGuiKey_GamepadFaceUp)) {
            if (e.page == 1 && !e.cups[e.selected].rounds.empty())
                e.round_actions = true;
            else
                e.options_requested = true;
        }
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const bool compact = ImGui::GetContentRegionAvail().y < 470;
    const float save_width = CupActionWidth("Save changes"), back_width = CupActionWidth("Back");
    const bool status_below = width < save_width + back_width + 24 + 140;
    const float move_total = CupActionWidth("Up") + CupActionWidth("Down") + CupActionWidth("Place round") +
                             CupActionWidth("Cancel move") + 24;
    const bool move_wrap = moving && move_total > width;
    const float footer_height = 104 + (status_below && !moving ? 26 : 0) + (move_wrap ? 52 : 0);
    const float start = ImGui::GetCursorPosY(), footer = start + ImGui::GetContentRegionAvail().y - footer_height;
    {
        auto* window = ImGui::GetCurrentWindow();
        auto* draw = window->DrawList;
        const auto type = PaddockReading(16, true, 1.3F);
        const std::string state = item.enabled ? "Enabled" : "Off";
        draw->PushClipRect(window->Rect().Min, window->Rect().Max, false);
        PaddockDrawRun(draw, type, {window->Pos.x + window->Size.x - 120, window->Pos.y + 16},
                       PaddockCol(item.enabled ? 0x16BCA4 : 0xB8CBD2), state.data(), state.data() + state.size());
        draw->PopClipRect();
    }
    const float tab = (width - 16) / 3;
    ImGui::BeginDisabled(view->busy || moving);
    for (int page = 0; page < 3; ++page) {
        if (page)
            ImGui::SameLine(0, 8);
        constexpr const char* names[] = {"Cups", "Lineup", "Share"};
        if (CupAction(names[page], names[page], tab, false, compact ? 36.0F : 44.0F, e.page == page))
            e.page = page;
    }
    ImGui::EndDisabled();
    PaddockGap(compact ? 6 : 10);
    ImGui::BeginDisabled(locked || view->busy || moving);
    const float previous = CupActionWidth("LB <"), next = CupActionWidth("> RB"),
                options = CupActionWidth("Cup options");
    const float order_width = CupSelectWidth({"Lineup order", "Shuffled order"});
    const float order = width >= previous + next + options + order_width + 180 + 32 ? order_width : 0;
    if (CupAction("previous-cup", "LB <", previous))
        CupChoose((e.selected + unsigned(e.cups.size()) - 1) % unsigned(e.cups.size()));
    ImGui::SameLine(0, 8);
    std::vector<std::string> names;
    for (const auto& c : e.cups)
        names.push_back(c.name);
    int selected = int(e.selected);
    if (PaddockSelect("current-cup", "", &selected, names,
                      std::max(1.0F, width - previous - next - options - order - 8 * (order > 0 ? 4 : 3))))
        CupChoose(unsigned(selected));
    ImGui::SameLine(0, 8);
    if (CupAction("next-cup", "> RB", next))
        CupChoose((e.selected + 1) % unsigned(e.cups.size()));
    ImGui::SameLine(0, 8);
    if (order > 0) {
        int shuffled = e.cups[e.selected].shuffle ? 1 : 0;
        if (PaddockSelect("cup-order", "", &shuffled, {"Lineup order", "Shuffled order"}, order)) {
            e.cups[e.selected].shuffle = shuffled == 1;
            e.dirty = true;
        }
        ImGui::SameLine(0, 8);
    }
    if (CupAction("cup-options", "Cup options", options))
        e.options_requested = true;
    ImGui::EndDisabled();
    PaddockGap(compact ? 6 : 12);
    auto& cup = e.cups[e.selected];
    const float sub = ImGui::GetCursorPosY();
    if (e.page == 1) {
        const float add_width = CupActionWidth("X / Add track");
        const auto heading = "Race lineup / " + std::to_string(cup.rounds.size()) + " rounds";
        const float heading_width = std::max(1.0F, width - add_width - 12);
        const float heading_height = PaddockTextHeight(PaddockSign(22), heading, heading_width, true);
        const float sub_left = ImGui::GetCursorPosX();
        PaddockHeading(heading, 22, 0xFFCA62, heading_width);
        ImGui::SetCursorPos({sub_left + width - add_width, sub});
        ImGui::BeginDisabled(locked || view->busy || moving || !e.catalog_ready || cup.rounds.size() >= 128);
        if (CupAction("add-track", "X / Add track", add_width, true)) {
            e.replace_round = -1;
            e.pick_requested = true;
            e.search.fill(0);
        }
        ImGui::EndDisabled();
        ImGui::SetCursorPos({sub_left, sub + std::max(44.0F, heading_height) + 10});
    }
    const float body_height = std::max(1.0F, footer - ImGui::GetCursorPosY() - 8);
    ImGui::BeginChild(e.page == 0   ? "Cup collection"
                      : e.page == 1 ? "Race lineup"
                                    : "Share cups",
                      {0, body_height}, false);
    ImGui::BeginDisabled(locked || view->busy || moving);
    if (e.page == 0) {
        const int columns = width >= 950 ? 3 : width >= 600 ? 2 : 1;
        const float card = (ImGui::GetContentRegionAvail().x - 12 * (columns - 1)) / columns;
        for (unsigned i = 0; i <= e.cups.size(); ++i) {
            if (i % columns)
                ImGui::SameLine(0, 12);
            ImGui::PushID(int(i));
            ImGui::BeginDisabled(i == e.cups.size() && e.cups.size() >= 32);
            const auto label = i == e.cups.size() ? "+ New cup" : e.cups[i].name;
            const auto detail = i == e.cups.size() ? "Create a race lineup"
                                                   : std::to_string(e.cups[i].rounds.size()) + " rounds / " +
                                                         (e.cups[i].shuffle ? "shuffled" : "lineup order");
            if (CupAction("cup", label, card, false, 86, i == e.selected, detail)) {
                if (i == e.cups.size()) {
                    e.cups.push_back({NewCupId(), "New Trophy Race", false, {}});
                    e.dirty = true;
                }
                CupChoose(i);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    } else if (e.page == 1)
        DrawCupLineup();
    else {
        const float body = ImGui::GetContentRegionAvail().x;
        PaddockHeading("Share your cups", 24, 0xFFCA62, body);
        PaddockModalText(
            "Cup files contain the lineup and settings, not the tracks. Share any required track packs alongside them.");
        PaddockGap(14);
        ImGui::BeginDisabled(!e.catalog_ready);
        if (CupAction("import", "Import cups", body, false, 66))
            BeginCupFilePicker(false);
        PaddockGap(10);
        if (CupAction("export", "Export this cup", body, false, 66))
            BeginCupFilePicker(true);
        PaddockGap(10);
        if (CupAction("export-all", "Export all cups", body, false, 66))
            BeginCupFilePicker(true, true);
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SetCursorPosY(std::max(start, footer));
    ImGui::Separator();
    PaddockGap(8);
    const auto footer_origin = ImGui::GetCursorScreenPos();
    const bool move_footer = e.move_origin >= 0;
    if (move_footer) {
        ImGui::BeginDisabled(e.focused_round == 0);
        if (CupAction("move-up", "Up", CupActionWidth("Up")))
            CupMoveRound(e.focused_round, e.focused_round - 1);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        ImGui::BeginDisabled(e.focused_round + 1 >= int(cup.rounds.size()));
        if (CupAction("move-down", "Down", CupActionWidth("Down")))
            CupMoveRound(e.focused_round, e.focused_round + 1);
        ImGui::EndDisabled();
        if (!move_wrap)
            ImGui::SameLine(0, 8);
        if (CupAction("place", "Place round", CupActionWidth("Place round"), true)) {
            e.dirty |= e.focused_round != e.move_origin;
            e.move_origin = -1;
        }
        CupNextAction("Cancel move");
        if (CupAction("cancel-move", "Cancel move", CupActionWidth("Cancel move"))) {
            CupMoveRound(e.focused_round, e.move_origin);
            e.move_origin = -1;
        }
    } else {
        ImGui::BeginDisabled(locked || view->busy || !e.catalog_ready);
        if (CupAction("save-cups", "Save changes", save_width, true))
            SaveCustomCups(item);
        ImGui::EndDisabled();
        const std::string status = locked              ? "Leave the game and lobby to edit"
                                   : e.save_pending    ? "Saving..."
                                   : e.catalog_pending ? "Loading tracks..."
                                   : e.dirty           ? "Unsaved changes"
                                                       : "All changes saved";
        ImGui::SetCursorScreenPos({footer_origin.x + width - back_width, footer_origin.y});
        ImGui::BeginDisabled(e.save_pending);
        if (CupAction("back", "Back", back_width) ||
            (top && !moving && (CupKey(ImGuiKey_GamepadFaceRight) || CupKey(ImGuiKey_Escape))))
            e.close_requested = true;
        ImGui::EndDisabled();
        const auto type = PaddockReading(14, false, 1.3F);
        PaddockTextStyle status_style;
        status_style.colour = PaddockCol(0xB8CBD2);
        if (status_below) {
            ImGui::SetCursorScreenPos({footer_origin.x, footer_origin.y + 50});
            PaddockText(type, PaddockRgb(0xB8CBD2), status, width);
        } else {
            const auto position = ImVec2{footer_origin.x + save_width + 12, footer_origin.y + (44 - type.line) * 0.5F};
            PaddockTextAt(ImGui::GetWindowDrawList(), type, position, width - save_width - back_width - 24, status,
                          status_style);
            ImGui::SetCursorScreenPos({footer_origin.x, footer_origin.y + 50});
            ImGui::Dummy({width, 0});
        }
    }
    const auto note = !e.error.empty()    ? e.error
                      : !e.notice.empty() ? e.notice
                      : moving            ? "Move: Up/Down / A place / B cancel"
                                          : "A edit / X add / Y actions / LB-RB cups / Start save / B back";
    PaddockText(PaddockReading(14, false, 1.3F), PaddockRgb(e.error.empty() ? 0xB8CBD2 : 0xFFCA62),
                PaddockEllipsize(PaddockReading(14, false, 1.3F), note, width), width);
    if (ImGui::IsItemHovered() && (!e.error.empty() || !e.notice.empty()))
        ImGui::SetTooltip("%s", note.c_str());
    if (e.pick_requested) {
        e.pick_requested = false;
        ImGui::OpenPopup("Choose a track");
    }
    if (e.options_requested) {
        e.options_requested = false;
        ImGui::OpenPopup("Cup options");
    }
    if (e.round_actions) {
        e.round_actions = false;
        ImGui::OpenPopup("Round actions");
    }
    ImGui::BeginDisabled(locked || view->busy);
    DrawCupTrackPicker();
    DrawCupOptions(item);
    DrawCupRoundActions();
    DrawCupImportReview(item);
    ImGui::EndDisabled();
    DrawCupFilePicker();
}
