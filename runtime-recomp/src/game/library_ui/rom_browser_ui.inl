struct RomBrowserState {
    dkr::library_ui::RomBrowser folders;
    ImGuiWindow* opener = nullptr;
    ImGuiID opener_id = 0;
    ImGuiID opener_scope = 0;
    ImRect opener_bounds;
    bool open = false;
    std::string message;
};

void CloseRomBrowser(RomBrowserState& browser, bool restore_focus) {
    browser.open = false;
    ImGui::CloseCurrentPopup();
    if (restore_focus && browser.opener && browser.opener_id) {
        ImGuiWindow* window = browser.opener;
        ImGui::FocusWindow(window);
        ImGui::SetNavID(browser.opener_id, ImGuiNavLayer_Main, browser.opener_scope,
                        ImGui::WindowRectAbsToRel(window, browser.opener_bounds));
    }
}

bool DrawRomBrowserEntry(const dkr::library_ui::RomBrowser::Entry& entry, bool focus) {
    const auto name = PathUtf8(entry.path.filename().empty() ? entry.path : entry.path.filename());
    const auto label = entry.directory ? "[Folder]  " + name : name;
    const float height = std::max(40.F, ImGui::GetTextLineHeight() + 12.F);
    if (focus)
        ImGui::SetKeyboardFocusHere();
    const bool picked = ImGui::Selectable("##entry", false, ImGuiSelectableFlags_None, {0.F, height});
    const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, max, true);
    draw->AddText({min.x + 8.F, min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * .5F},
                  ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
    draw->PopClipRect();
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label.c_str()).x > max.x - min.x - 16.F)
        ImGui::SetTooltip("%s", name.c_str());
    return picked;
}

// A candidate path uses the library's validator. A missing path requests the
// platform picker, which applies the same validation after choosing a file.
template <typename SelectRom> bool DrawRomBrowser(RomBrowserState& browser, bool already_ready, SelectRom select_rom) {

    constexpr const char* popup = "Choose a game ROM";
    if (!ImGui::IsPopupOpen(popup))
        ImGui::OpenPopup(popup);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(800.F, viewport->Size.x * .94F), std::min(660.F, viewport->Size.y * .92F)},
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, {.5F, .5F});
    if (!BeginPaddedModal(popup,
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return false;

    auto& folders = browser.folders;
    const bool owns_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (owns_focus && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        CloseRomBrowser(browser, true);
        ImGui::EndPopup();
        return false;
    }
    if (owns_focus && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        if (!folders.back()) {
            CloseRomBrowser(browser, true);
            ImGui::EndPopup();
            return false;
        }
        browser.message.clear();
    }

    const bool in_folder = !folders.directory().empty();
    // Use the launcher's reading face so filenames retain their original case.
    const auto reading = PaddockReading(15.F, false, 1.2F);
    ImGui::PushFont(reading.font);
    ImGui::TextDisabled("US v1.0 or US Rev A / v1.1  (.z64, .v64, .n64)");

    const auto location = in_folder ? PathUtf8(folders.directory()) : std::string("Locations");
    const auto shown = PaddockEllipsize(reading, location, ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(shown.c_str());
    if (shown != location && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", location.c_str());

    auto focus_row = folders.take_focus_row();
    if (focus_row)
        ImGui::GetCurrentContext()->NavDisableHighlight = false;
    if (focus_row && folders.entries().empty() && in_folder)
        ImGui::SetKeyboardFocusHere();
    ImGui::BeginDisabled(!in_folder);
    const bool show_locations = ImGui::Button("Locations");
    ImGui::SameLine();
    const bool go_up = ImGui::Button("Up one level");
    ImGui::EndDisabled();
    if (!browser.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextWrapped("%s", browser.message.c_str());
        ImGui::PopStyleColor();
    }

    const float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() +
                                2.F * ImGui::GetStyle().ItemSpacing.y;
    std::optional<dkr::library_ui::RomBrowser::Entry> picked;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.02F, 0.12F, 0.23F, 0.98F});
    if (BeginPaddedChild("rom-browser-files", {0.F, -footer_height}, true, ImGuiWindowFlags_NavFlattened,
                         {10.F, 8.F})) {
        const auto& entries = folders.entries();
        if (folders.error())
            ImGui::TextWrapped("Could not read this folder: %s", folders.error().message().c_str());
        else if (entries.empty())
            ImGui::TextWrapped("No folders or ROM files here. Go up a level or choose another location.");

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(entries.size()));
        if (focus_row && !entries.empty())
            clipper.IncludeItemByIndex(*focus_row);
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                const auto& entry = entries[static_cast<std::size_t>(index)];
                ImGui::PushID(index);
                if (DrawRomBrowserEntry(entry, focus_row == index))
                    picked = entry;
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (focus_row && folders.entries().empty() && !in_folder)
        ImGui::SetKeyboardFocusHere();
    const bool cancel = ImGui::Button("Cancel");
    ImGui::SameLine();
    const bool system_picker = ImGui::Button("System file picker");
    ImGui::TextDisabled("A / Cross: Select     B / Circle: Back     Esc: Cancel");
    ImGui::PopFont();

    bool accepted = false;
    if (cancel) {
        CloseRomBrowser(browser, true);
    } else if (system_picker || (picked && !picked->directory)) {
        accepted = select_rom(system_picker ? std::nullopt : std::optional(picked->path));
        if (accepted)
            CloseRomBrowser(browser, already_ready);
    } else if (show_locations || go_up || (picked && picked->directory)) {
        if (show_locations)
            folders.visit({});
        else if (go_up)
            folders.up();
        else
            folders.visit(picked->path);
        browser.message.clear();
    }
    ImGui::EndPopup();
    return accepted;
}
