// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_graphics_ui.inl. Presentation only: backups, imports, exports,
// repairs, resets and the Save Builder go through dkr::runtime::saves exactly
// as before.
//
// The slot list and editor share each scope's draft. File operations and
// quick actions keep the same confirmation and backup flow.

enum class SaveEditorSection { Progress, Unlocks, Files };

struct SavesPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    SaveEditorSection section = SaveEditorSection::Progress;
    int scope = 0; // Vanilla, Online, Modded, Recovery
    bool personal_modded = false;
    bool import_modded = false;
    int selected_slot = 0;
    bool scroll_editor_to_top = false;
    // The confirmation in flight.
    enum class Ask {
        None,
        Repair,
        Reset,
        Restore,
        MaxOut,
        UnlockAll,
        Discard,
        Apply,
        Import,
        Bundle,
        Pak,
        RepairUse,
        Host,
        DevelopmentCopy
    } ask = Ask::None;
    std::filesystem::path restore;
    int slot = 0;
};
SavesPageState g_saves_page;

constexpr unsigned kSavesAccent = 0x1AC2A3;

enum class SavesTone { Plain, Primary, Featured, Danger };

// .saves-button: a race button on the page's quiet #123e50, or the accent
// primary, or the danger red.
bool SavesRaceButton(const char* label, float width = 0.0F, SavesTone tone = SavesTone::Plain, bool disabled = false,
                     const char* reason = nullptr) {
    RaceButtonLook look;
    look.hover_in = 0.12F;
    if (tone == SavesTone::Primary) {
        look.fill = kSavesAccent;
        look.border = kSavesAccent;
        look.text = 0x062C30;
        look.text_shadow = false;
    } else if (tone == SavesTone::Featured) {
        look.fill = 0xAD481C;
        look.border = kSetWarm;
    } else if (tone == SavesTone::Danger) {
        look.fill = 0x73171A;
        look.border = 0xD97773;
    } else {
        look.fill = 0x123E50;
    }
    if (width <= 0.0F)
        width = PaddockRaceButtonWidth(label);
    OlDisabled scope(disabled, 0.6F);
    const bool pressed = PaddockRaceButton(label, {width, 44.0F}, look);
    if (disabled && reason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
    return pressed && !disabled;
}

// A heading between parts (.saves-separator): a hairline 20 px above it.
void SavesSeparator(std::string_view text, float width, bool reading = true) {
    PaddockGap(24.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
    PaddockGap(1.0F + 20.0F);
    if (reading) {
        PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), text, width);
    } else {
        PaddockText(PaddockSign(19.0F, 1.2F, 0.0F), PaddockRgb(kSetText), text, width);
    }
    PaddockGap(12.0F);
}

void SavesNote(std::string_view text, float width, float px = 16.0F, unsigned colour = kSetMuted) {
    if (text.empty())
        return;
    PaddockText(PaddockReading(px, false, 1.45F), PaddockRgb(colour), text, width);
    PaddockGap(13.0F);
}

std::string SavesBackupTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error)
        return PathUtf8(path.filename());
    const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        written - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[64]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
    return text;
}

bool SavesDraftDirty(const AdventureDraft& draft) {
    using dkr::runtime::saves::codec::encode;
    return draft.image && draft.original && encode(*draft.image) != encode(*draft.original);
}

// ------------------------------------------------------------ editor

struct SavesGrid {
    int count = 0;
    ImVec2 origin{};
    float column = 0.0F;
    int per_row = 3;
    float row_top = 0.0F;
    float row_bottom = 0.0F;

    SavesGrid(float width, float panel) : origin(ImGui::GetCursorScreenPos()) {
        per_row = panel < 375.0F ? 1 : panel < 545.0F ? 2 : 3;
        column = std::floor((width - 16.0F * static_cast<float>(per_row - 1)) / static_cast<float>(per_row));
        row_top = row_bottom = origin.y;
    }
    void Begin() {
        const int slot = count % per_row;
        if (slot == 0 && count > 0)
            row_top = row_bottom + 16.0F;
        ImGui::SetCursorScreenPos({origin.x + (column + 16.0F) * static_cast<float>(slot), row_top});
        ImGui::BeginGroup();
    }
    void End() {
        ImGui::EndGroup();
        const float bottom = ImGui::GetItemRectMax().y;
        row_bottom = count % per_row == 0 ? bottom : std::max(row_bottom, bottom);
        ++count;
    }
    void Finish(float width) {
        ImGui::SetCursorScreenPos({origin.x, std::max(row_bottom, origin.y)});
        ImGui::Dummy({width, 0.0F});
        PaddockGap(20.0F);
    }
};

void DrawSavesSlotEditor(SavesPageState& state, float outer, int slot_index) {
    using namespace dkr::runtime::saves::codec;
    AdventureSlot& slot = g_adventure_drafts[state.personal_modded].image->slots[static_cast<std::size_t>(slot_index)];
    ImGui::PushID(slot_index);
    const auto paint = [](ImDrawList* draw, ImVec2 min, ImVec2 max) {
        PaddockPanel(draw, min, max, {PaddockRound(9.0F), PaddockRgb(0x102E40), PaddockRgb(kSetBorder), 1.0F});
    };
    const auto title = [](const char* caption, float width) {
        PaddockText(PaddockSign(19.0F, 1.2F, 0.0F), PaddockRgb(kSetText), caption, width);
        PaddockGap(8.0F);
    };

    PaddockBox balloon_box(outer, {10.0F, 10.0F});
    const float width = balloon_box.Inner();
    title("Golden balloons", width);
    const auto summary = ImGui::GetCursorScreenPos();
    const float initials_width = std::min(width, 290.0F);
    PaddockText(PaddockReading(16.0F, true, 1.25F), PaddockRgb(kSetText), "Racer initials", initials_width);
    PaddockGap(6.0F);
    {
        // Three letter pickers sized for the widest initial (.saves-initials).
        static const std::vector<SettingsChoice> kLetters = [] {
            std::vector<SettingsChoice> letters;
            for (char letter = 'A'; letter <= 'Z'; ++letter)
                letters.push_back({std::string(1, letter), {}, false});
            letters.push_back({".", {}, false});
            letters.push_back({"?", {}, false});
            letters.push_back({"SPACE", {}, false});
            return letters;
        }();
        std::string padded = sanitise_name(slot.name);
        padded.resize(3U, ' ');
        const float cell = std::floor((initials_width - 16.0F) / 3.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        for (int character = 0; character < 3; ++character) {
            ImGui::SetCursorScreenPos({at.x + (cell + 8.0F) * static_cast<float>(character), at.y});
            ImGui::BeginGroup();
            ImGui::PushID(character);
            const char current = padded[static_cast<std::size_t>(character)];
            int selected = current >= 'A' && current <= 'Z' ? current - 'A'
                           : current == '.'                 ? 26
                           : current == '?'                 ? 27
                                                            : 28;
            if (SettingsDropdown("letter", {}, &selected, kLetters, cell)) {
                padded[static_cast<std::size_t>(character)] = selected < 26    ? static_cast<char>('A' + selected)
                                                              : selected == 26 ? '.'
                                                              : selected == 27 ? '?'
                                                                               : ' ';
                slot.name = sanitise_name(padded);
            }
            ImGui::PopID();
            ImGui::EndGroup();
        }
        ImGui::SetCursorScreenPos({at.x, at.y + 52.0F});
        ImGui::Dummy({width, 0.0F});
    }
    const float letters_bottom = ImGui::GetCursorScreenPos().y;
    const bool inline_total = width >= initials_width + 170.0F;
    const float total_width = inline_total ? width - initials_width - 16.0F : width;
    if (inline_total)
        ImGui::SetCursorScreenPos({summary.x + initials_width + 16.0F, summary.y + 24.0F});
    else
        PaddockGap(8.0F);
    int hub = static_cast<int>(hub_balloons(slot));
    const unsigned bonus = bonus_balloons(slot);
    {
        // The total (.saves-progress): a dark bar that fills amber.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PaddockFill(draw, at, {at.x + total_width, at.y + 32.0F}, PaddockRound(6.0F), PaddockCol(0x153E50));
        const float t =
            std::min(1.0F, static_cast<float>(slot.balloons[0]) / static_cast<float>(kMaximumTotalBalloons));
        if (t > 0.0F) {
            draw->PushClipRect(at, {at.x + total_width * t, at.y + 32.0F}, true);
            PaddockFill(draw, at, {at.x + total_width, at.y + 32.0F}, PaddockRound(6.0F), PaddockCol(0x806000));
            draw->PopClipRect();
        }
        std::string total = std::to_string(slot.balloons[0] - bonus) + " / " + std::to_string(kMaximumTotalBalloons);
        if (bonus > 0U)
            total += " + " + std::to_string(bonus) + " bonus";
        const PaddockType type = PaddockReading(16.0F, true, 1.0F);
        const float text_width = PaddockMeasure(type, total);
        PaddockDrawRun(
            draw, type,
            {std::round(at.x + (total_width - text_width) * 0.5F), std::round(at.y + (32.0F - type.line) * 0.5F)},
            PaddockCol(kSetText), total.data(), total.data() + total.size());
        ImGui::Dummy({total_width, 32.0F});
        const float total_bottom = ImGui::GetItemRectMax().y;
        ImGui::SetCursorScreenPos({summary.x, std::max(letters_bottom, total_bottom)});
        ImGui::Dummy({width, 0.0F});
        PaddockGap(18.0F);
    }
    constexpr std::array<const char*, kWorldCount> kAreas{
        {"Central island", "Dino Domain", "Sherbet Island", "Snowflake Mountain", "Dragon Forest", "Future Fun Land"}};
    const auto count = [](int value) { return SettingsRangeValue{std::to_string(value), ""}; };
    SavesGrid balloons(width, width);
    for (std::size_t area = 0; area < kWorldCount; ++area) {
        balloons.Begin();
        ImGui::PushID(static_cast<int>(area));
        int value = area == 0U ? hub : slot.balloons[area];
        // Bonus balloons only exist on top of a full island, so the island stays full while any are held.
        const bool island_locked = area == 0U && bonus > 0U;
        if (SettingsRange("##balloons", kAreas[area], &value, island_locked ? kMaximumHubBalloons : 0,
                          area == 0U ? kMaximumHubBalloons : kMaximumWorldBalloons, balloons.column, count, {},
                          island_locked)) {
            if (area == 0U)
                hub = value;
            else
                slot.balloons[area] = static_cast<std::uint8_t>(value);
            set_balloon_total(slot, static_cast<unsigned>(hub), bonus);
        }
        ImGui::PopID();
        balloons.End();
    }
    balloons.Finish(width);

    const auto bonus_row = ImGui::GetCursorScreenPos();
    const float remove_width = SettingsButtonWidth("Remove bonus balloons");
    const bool inline_bonus = width >= remove_width + 180.0F;
    int bonus_value = static_cast<int>(bonus_balloons(slot));
    const int bonus_limit = static_cast<int>(maximum_bonus_balloons(slot));
    if (SettingsRange("##bonus-balloons", "Bonus balloons", &bonus_value, 0, bonus_limit,
                      inline_bonus ? width - remove_width - 9.0F : width, count, {}, bonus_limit == 0)) {
        set_balloon_total(slot, hub_balloons(slot), static_cast<unsigned>(bonus_value));
    }
    const float bonus_bottom = ImGui::GetCursorScreenPos().y;
    if (inline_bonus)
        ImGui::SetCursorScreenPos({bonus_row.x + width - remove_width, bonus_row.y});
    if (SettingsButton("Remove bonus balloons", remove_width, bonus_balloons(slot) == 0U))
        set_balloon_total(slot, hub_balloons(slot), 0U);
    ImGui::SetCursorScreenPos({bonus_row.x, std::max(bonus_bottom, ImGui::GetItemRectMax().y)});
    ImGui::Dummy({width, 0.0F});
    balloon_box.End(paint);

    PaddockGap(9.0F);
    PaddockBox trophy_box(outer, {10.0F, 10.0F});
    title("Trophy races", width);
    constexpr std::array<const char*, 5> kTrophies{"Dino Domain", "Sherbet Island", "Snowflake Mountain",
                                                   "Dragon Forest", "Future Fun Land"};
    SavesGrid trophies(width, width);
    for (std::size_t world = 0; world < kTrophies.size(); ++world) {
        trophies.Begin();
        ImGui::PushID(static_cast<int>(world));
        const unsigned shift = static_cast<unsigned>(world) * 2;
        int place = (slot.trophies >> shift) & 3;
        if (SettingsDropdown("##trophy", kTrophies[world], &place, {"None", "Bronze", "Silver", "Gold"},
                             trophies.column))
            slot.trophies =
                static_cast<std::uint16_t>((slot.trophies & ~(3U << shift)) | (static_cast<unsigned>(place) << shift));
        ImGui::PopID();
        trophies.End();
    }
    trophies.Finish(width);

    trophy_box.End(paint);
    PaddockGap(9.0F);
    PaddockBox keys_box(outer, {10.0F, 10.0F});
    title("Amulets and keys", width);
    SavesGrid keys(width, width);
    keys.Begin();
    int tt = slot.tt_amulet;
    if (SettingsRange("##tt-amulet", "T.T. amulet pieces", &tt, 0, kMaximumAmuletPieces, keys.column, count)) {
        slot.tt_amulet = static_cast<std::uint8_t>(tt);
    }
    keys.End();
    keys.Begin();
    int wizpig = slot.wizpig_amulet;
    if (SettingsRange("##wizpig-amulet", "Wizpig amulet pieces", &wizpig, 0, kMaximumAmuletPieces, keys.column,
                      count)) {
        slot.wizpig_amulet = static_cast<std::uint8_t>(wizpig);
    }
    keys.End();
    constexpr std::array<const char*, 4> kKeys{
        {"Dino Domain key", "Snowflake Mountain key", "Sherbet Island key", "Dragon Forest key"}};
    for (std::size_t key = 0; key < kKeys.size(); ++key) {
        keys.Begin();
        const auto mask = static_cast<std::uint8_t>(1U << (key + 1U));
        bool value = (slot.keys & mask) != 0U;
        ImGui::PushID(static_cast<int>(key));
        if (SettingsCheck("##key", kKeys[key], &value, keys.column)) {
            if (value)
                slot.keys |= mask;
            else
                slot.keys &= static_cast<std::uint8_t>(~mask);
        }
        ImGui::PopID();
        keys.End();
    }
    keys.Finish(width);

    keys_box.End(paint);
    PaddockGap(9.0F);
    PaddockBox courses_box(outer, {10.0F, 10.0F});
    title("Course progress", width);
    constexpr std::array<int, kCourseCount> kCourses{3,  1,  20, 5, 6,  8,  2,  21, 10, 4,  7,  19, 14, 13, 15, 22, 12,
                                                     23, 11, 24, 9, 17, 18, 16, 26, 29, 27, 31, 0,  30, 28, 32, 25, 33};
    const auto& names = course_names();
    for (std::size_t group = 0; group <= kTrophies.size(); ++group) {
        const char* title = group < kTrophies.size() ? kTrophies[group] : "Challenges and bosses";
        PaddockText(PaddockReading(16.0F, true), PaddockRgb(kSetWarm), title, width);
        PaddockGap(8.0F);
        SavesGrid courses(width, width);
        // Course IDs index the Adventure save's course status array.
        const std::size_t last = group < kTrophies.size() ? (group + 1) * 4 : kCourses.size();
        for (std::size_t index = group * 4; index < last; ++index) {
            const int course = kCourses[index];
            courses.Begin();
            ImGui::PushID(course);
            int status = slot.course_status[course];
            if (SettingsDropdown("course", names[course], &status,
                                 {"Not started", "Race won", "Silver Coins won", "Complete"}, courses.column))
                slot.course_status[course] = static_cast<std::uint8_t>(status);
            ImGui::PopID();
            courses.End();
        }
        courses.Finish(width);
        PaddockGap(14.0F);
    }
    courses_box.End(paint);
    ImGui::PopID();
}

void DrawSavesUnlocks(SavesPageState& state, float width, float panel) {
    auto& settings = g_adventure_drafts[state.personal_modded].image->settings;
    SavesGrid grid(width, panel);
    grid.Begin();
    SettingsCheck("##adventure-two", "Adventure Two", &settings.adventure_two, grid.column);
    grid.End();
    grid.Begin();
    SettingsCheck("##drumstick", "Drumstick", &settings.drumstick, grid.column);
    grid.End();
    grid.Begin();
    SettingsCheck("##subtitles", "Subtitles", &settings.subtitles, grid.column);
    grid.End();
    grid.Begin();
    int language = settings.language;
    if (SettingsDropdown("language", "Language", &language, {"English", "German", "French", "Japanese"}, grid.column)) {
        settings.language = static_cast<std::uint8_t>(language);
    }
    grid.End();
    grid.Finish(width);

    SavesSeparator("T.T. time-trial victories", width, false);
    constexpr std::array<const char*, 20> kTrials{
        {"Ancient Lake",    "Fossil Canyon",   "Jungle Falls",     "Hot Top Volcano",   "Whale Bay",
         "Crescent Island", "Pirate Lagoon",   "Treasure Caves",   "Everfrost Peak",    "Walrus Cove",
         "Snowball Valley", "Frosty Village",  "Boulder Canyon",   "Greenwood Village", "Windmill Plains",
         "Haunted Woods",   "Spacedust Alley", "Darkmoon Caverns", "Star City",         "Spaceport Alpha"}};
    SavesGrid trials(width, panel);
    for (std::size_t trial = 0; trial < kTrials.size(); ++trial) {
        trials.Begin();
        ImGui::PushID(static_cast<int>(trial));
        SettingsCheck("##trial", kTrials[trial], &settings.tt_trials[trial], trials.column);
        ImGui::PopID();
        trials.End();
    }
    trials.Finish(width);
    if (SavesRaceButton("UNLOCK ALL RACERS AND MODES"))
        state.ask = SavesPageState::Ask::UnlockAll;
}

bool LoadSavesEditorDraft(SavesPageState& state, bool valid) {
    auto& draft = g_adventure_drafts[state.personal_modded];
    if (draft.image || !valid)
        return draft.image.has_value();
    dkr::runtime::saves::codec::SaveImage image;
    std::string error;
    if (!dkr::runtime::saves::shared::load_draft(image, draft.revision, error, state.personal_modded)) {
        g_save_manager_status = error;
        return false;
    }
    draft.original = image;
    draft.image = std::move(image);
    return true;
}

struct SavesFooterLayout {
    float discard_width;
    float apply_width;
    bool stacked;
    float height;
};

SavesFooterLayout MeasureSavesFooter(float width) {
    const float discard_width = PaddockRaceButtonWidth("DISCARD CHANGES");
    const float apply_width = PaddockRaceButtonWidth("APPLY CHANGES");
    const bool stacked = width - discard_width - apply_width - 24.0F < 150.0F;
    return {discard_width, apply_width, stacked, stacked ? 106.0F : 66.0F};
}

void DrawSavesDraftBar(const SavesPageState& state, float width, float top, const SavesFooterLayout& layout, bool dirty,
                       bool locked, SavesPageState::Ask& ask) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const auto panel = PanelFooterBounds(window);
    const float left = ImGui::GetCursorScreenPos().x;
    DrawPanelFooterBackground(window, top, PaddockCol(0x0A202C), PaddockCol(kSetBorder));
    if (SaveDraftLayoutCheck()) {
        g_save_draft_geometry.panel = panel;
        g_save_draft_geometry.bounds = {
            {window->Pos.x + window->WindowBorderSize, window->Pos.y + window->WindowBorderSize},
            {window->Pos.x + window->Size.x - window->WindowBorderSize - window->ScrollbarSizes.x,
             window->Pos.y + window->Size.y - window->WindowBorderSize - window->ScrollbarSizes.y}};
        g_save_draft_geometry.top = top;
        g_save_draft_geometry.dirty = dirty;
    }
    ImGui::SetCursorScreenPos({left, top + (layout.stacked ? 10.0F : 17.0F)});
    const float text_width = layout.stacked ? width : width - layout.discard_width - layout.apply_width - 24.0F;
    const std::string_view status = state.personal_modded
                                        ? (dirty ? "Modded Adventure · Unsaved changes" : "Modded Adventure")
                                        : (dirty ? "Vanilla Adventure · Unsaved changes" : "Vanilla Adventure");
    PaddockText(PaddockReading(16.0F, true, 1.25F), PaddockRgb(kSetText), status, text_width);

    const float buttons_y = top + (layout.stacked ? 48.0F : 11.0F);
    const float actions_width = layout.discard_width + layout.apply_width + 12.0F;
    ImGui::SetCursorScreenPos({left + std::max(0.0F, width - actions_width), buttons_y});
    if (SavesRaceButton("DISCARD CHANGES", layout.discard_width, SavesTone::Plain, locked || !dirty))
        ask = SavesPageState::Ask::Discard;
    ImGui::SameLine(0.0F, 12.0F);
    if (SavesRaceButton("APPLY CHANGES", layout.apply_width, SavesTone::Primary, locked || !dirty))
        ask = SavesPageState::Ask::Apply;
    LibraryNavigationPinnedAction(!locked && dirty ? ImGui::GetItemID() : 0);
}

// File tools shared by both Adventure saves.
void SharedPakDialog(int port, bool importing) {
    if (SharedSaveBusy())
        return;
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be opened.";
        return;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR-R Controller Pak", "mpk"}};
    const auto dialog = importing ? NFD_OpenDialogU8(&result, filters, 1, nullptr)
                                  : NFD_SaveDialogU8(&result, filters, 1, nullptr, "dkr-controller-pak.mpk");
    if (dialog == NFD_OKAY) {
        auto path = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (importing)
            g_shared_save_import = {path, 2, port};
        else
            BeginSharedSaveJob(
                [path, port](std::string& error) {
                    return dkr::runtime::saves::export_controller_pak(port, path, error);
                },
                "Controller Pak exported.");
    } else if (dialog == NFD_ERROR)
        g_save_manager_status = NFD_GetError();
    NFD_Quit();
}
void SharedRawExportDialog(const std::filesystem::path& source) {
    if (NFD_Init() != NFD_OKAY)
        return;
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Original recovery image", "bin"}};
    if (NFD_SaveDialogU8(&result, filters, 1, nullptr, "original-save-image.bin") == NFD_OKAY) {
        auto dest = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        BeginSharedSaveJob(
            [source, dest](std::string& error) { return dkr::runtime::saves::shared::export_raw(source, dest, error); },
            "Original image exported unchanged. It has not been repaired.");
    }
    NFD_Quit();
}
struct SavesFileActions {
    float width;
    float used = 0.0F;

    bool Button(const char* label, bool disabled = false, SettingsButtonTone tone = SettingsButtonTone::Plain) {
        constexpr float gap = 8.0F;
        const float size = std::min(width, SettingsButtonWidth(label));
        if (used > 0.0F && used + gap + size <= width) {
            ImGui::SameLine(0.0F, gap);
            used += gap;
        } else {
            if (used > 0.0F)
                PaddockGap(gap);
            used = 0.0F;
        }
        used += size;
        return SettingsButton(label, size, disabled, tone);
    }
};

void DrawSavesFiles(SavesPageState& state, float width, const AdventureView& adventure,
                    const SaveManagerViewCache& view) {
    using namespace dkr::runtime::saves;
    const bool modded = state.personal_modded;
    const bool busy = SharedSaveBusy();
    const bool valid = adventure.info.valid;
    const auto heading = [](const char* label, float available) {
        PaddockText(PaddockSign(20.0F, 1.2F, 0.0F), PaddockRgb(kSetText), label, available);
        PaddockGap(9.0F);
    };

    constexpr float gap = 12.0F;
    const bool side_by_side = width >= 460.0F;
    const float column = side_by_side ? std::floor((width - gap) * 0.5F) : width;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    constexpr std::string_view adventure_help = "Back up, export or replace this Adventure.";
    constexpr std::string_view bundle_help =
        "Transfer this Adventure with shared custom-course records and Controller Paks.";
    const auto note_type = PaddockReading(16.0F, false, 1.45F);
    const float note_height = std::max(PaddockTextHeight(note_type, adventure_help, column),
                                       PaddockTextHeight(note_type, bundle_help, column));
    const auto transfer_note = [&](std::string_view note) {
        SavesNote(note, column);
        if (side_by_side)
            PaddockGap(note_height - PaddockTextHeight(note_type, note, column));
    };
    ImGui::BeginGroup();
    heading("Adventure save", column);
    transfer_note(adventure_help);
    SavesFileActions adventure_actions{column};
    if (adventure_actions.Button("Back up save", busy || !valid))
        BeginSharedSaveJob(
            [modded](std::string& error) {
                std::filesystem::path created;
                return backup_adventure(created, error, modded);
            },
            "Adventure backed up.");
    if (adventure_actions.Button("Export save", busy || !valid))
        ExportAdventureWithDialog(adventure.info.path);
    if (adventure_actions.Button("Import save", busy))
        ImportAdventureWithDialog(modded);
    if (!adventure.info.exists && adventure_actions.Button("Create personal save", busy))
        state.ask = SavesPageState::Ask::Reset;
    if (adventure.info.exists && !valid && adventure.info.size == 512 &&
        adventure_actions.Button("Back up & repair checksums", busy))
        state.ask = SavesPageState::Ask::Repair;
    ImGui::EndGroup();
    const float first_bottom = ImGui::GetItemRectMax().y;
    ImGui::SetCursorScreenPos(side_by_side ? ImVec2{top.x + column + gap, top.y} : ImVec2{top.x, first_bottom + 16.0F});
    ImGui::BeginGroup();
    heading("Save bundle", column);
    transfer_note(bundle_help);
    SavesFileActions bundle_actions{column};
    if (bundle_actions.Button("Export save bundle", busy))
        ExportSaveBundleWithDialog(modded);
    if (bundle_actions.Button("Import save bundle", busy))
        ImportSaveBundleWithDialog(modded);
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos({top.x, std::max(first_bottom, ImGui::GetItemRectMax().y)});
    ImGui::Dummy({width, 0.0F});

    SavesSeparator("Controller Paks", width, false);
    SavesNote("Use DKR-R-format .mpk files for these virtual memory cards.", width);
    for (int port = 0; port < 4; ++port) {
        ImGui::PushID(port);
        const auto& pak = view.controller_paks[port];
        const std::string label = "Controller " + std::to_string(port + 1) + ": " +
                                  (!pak.exists ? "not created"
                                   : pak.valid ? "ready"
                                               : "needs recovery");
        const float actions_width = SettingsButtonWidth("Back up Pak") + SettingsButtonWidth("Export Pak") +
                                    SettingsButtonWidth("Import Pak") + 16.0F;
        constexpr float label_width = 192.0F;
        const bool inline_actions = width >= label_width + gap + actions_width;
        const ImVec2 row = ImGui::GetCursorScreenPos();
        SavesNote(label, inline_actions ? label_width : width, 16.0F, kSetText);
        if (inline_actions)
            ImGui::SetCursorScreenPos({row.x + label_width + gap, row.y});
        ImGui::BeginGroup();
        SavesFileActions pak_actions{inline_actions ? width - label_width - gap : width};
        if (pak_actions.Button("Back up Pak", busy || !pak.valid))
            BeginSharedSaveJob(
                [port](std::string& error) {
                    std::filesystem::path created;
                    return backup_controller_pak(port, created, error);
                },
                "Controller Pak backed up.");
        if (pak_actions.Button("Export Pak", busy || !pak.valid))
            SharedPakDialog(port, false);
        if (pak_actions.Button("Import Pak", busy))
            SharedPakDialog(port, true);
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos({row.x, ImGui::GetItemRectMax().y + 9.0F});
        ImGui::Dummy({width, 0.0F});
        ImGui::PopID();
    }

    SavesSeparator("Recovery and development", width, false);
    SavesNote("Dev Menu uses a separate copy. Refreshing it keeps a backup of its previous progress.", width);
    SavesFileActions recovery_actions{width};
    if (recovery_actions.Button("Open Backups & Recovery", busy)) {
        state.scope = 3;
        state.ask = SavesPageState::Ask::None;
    }
    if (recovery_actions.Button("Copy personal save to development", busy || !valid))
        state.ask = SavesPageState::Ask::DevelopmentCopy;
    if (adventure.info.exists) {
        SavesSeparator("Reset Adventure", width, false);
        if (SavesRaceButton("RESET ADVENTURE", 0.0F, SavesTone::Danger, busy))
            state.ask = SavesPageState::Ask::Reset;
    }
}

void DrawSavesConfirmations(SavesPageState& state) {
    using namespace dkr::runtime::saves;
    using Ask = SavesPageState::Ask;
    static Ask shown = Ask::None;
    if (state.ask == Ask::None) {
        shown = Ask::None;
        return;
    }
    if (shown != state.ask) {
        shown = state.ask;
        ImGui::OpenPopup("##saves-confirm");
    }
    std::string heading = "Confirm save change", detail, confirm = "CONFIRM";
    const bool modded =
        state.ask == Ask::Import || state.ask == Ask::Bundle ? state.import_modded : state.personal_modded;
    auto& draft = g_adventure_drafts[modded];
    const std::string adventure = modded ? "Modded Adventure" : "Vanilla Adventure";
    bool replace = true;
    static std::filesystem::path preview_source;
    static std::string preview_text;
    if (preview_source != state.restore) {
        preview_source = state.restore;
        preview_text = state.restore.empty() ? std::string{} : shared::describe(state.restore);
    }
    switch (state.ask) {
    case Ask::Repair:
        heading = "Repair " + adventure + "?";
        detail =
            "Only checksums can be repaired. Damaged progress cannot be reconstructed. An unchanged original is preserved.";
        confirm = "REPAIR & KEEP ORIGINAL";
        break;
    case Ask::Reset:
        heading = "Reset " + adventure + "?";
        detail =
            "Reset all three slots, unlocks and built-in records in this Adventure. The current Adventure is backed up first.";
        confirm = "BACK UP & RESET";
        break;
    case Ask::Restore:
        heading = "Restore into " + adventure + "?";
        detail = preview_text;
        confirm = "BACK UP & USE THIS SAVE";
        break;
    case Ask::MaxOut:
        heading = state.slot < 0 ? "Complete the entire save?" : "Complete this Adventure?";
        detail =
            state.slot < 0
                ? "Complete all three slots and unlock Drumstick, T.T. and Adventure Two. Existing initials and personal records are kept."
                : "Complete this slot and unlock Drumstick, T.T. and Adventure Two. Other slots and personal records are kept.";
        confirm = "BACK UP & SAVE COMPLETION";
        break;
    case Ask::UnlockAll:
        heading = "Unlock racers and modes?";
        detail = "Unlock Drumstick, T.T. and Adventure Two. Your Adventure progress and personal best times are kept.";
        confirm = "BACK UP & SAVE UNLOCKS";
        break;
    case Ask::Discard:
        heading = "Discard editor changes?";
        detail = "Reload " + adventure + " from disk.";
        confirm = "DISCARD DRAFT";
        replace = false;
        break;
    case Ask::Apply:
        heading = "Apply editor changes?";
        detail = "Save the draft to " + adventure + ". A backup is created first.";
        confirm = "BACK UP & APPLY";
        break;
    case Ask::Import:
        heading = "Import into " + adventure + "?";
        detail = preview_text;
        confirm = "BACK UP & IMPORT";
        break;
    case Ask::Bundle:
        heading = "Import bundle into " + adventure + "?";
        detail =
            "Import the Adventure into " + adventure +
            " and replace any shared records and Controller Paks included in the bundle. Originals are kept in Recovery.";
        confirm = "BACK UP & IMPORT BUNDLE";
        break;
    case Ask::Pak:
        heading = "Import Controller Pak?";
        detail = "Replace Controller " + std::to_string(state.slot + 1) +
                 "'s Pak. An existing valid image is backed up first.";
        confirm = "BACK UP & IMPORT PAK";
        break;
    case Ask::RepairUse:
        heading = "Repair and use a copy?";
        detail =
            "Attempt checksum repair, then replace your personal save with that copy. Corrupt progress cannot be reconstructed. Both originals are retained.";
        confirm = "REPAIR COPY & USE";
        break;
    case Ask::Host:
        heading = "Use this online Adventure?";
        detail = preview_text + ". The current host Adventure is backed up. Personal saves are unchanged.";
        confirm = "USE FOR HOSTING";
        break;
    case Ask::DevelopmentCopy:
        heading = "Refresh the next development save?";
        detail =
            "Copy your saved personal progress into the next offline development session. The existing development save will be preserved before replacement. Your personal save will not be changed. Unapplied editor changes are not included.";
        confirm = "QUEUE DEVELOPMENT COPY";
        replace = false;
        break;
    default:
        break;
    }
    if (!BeginSettingsDialog("##saves-confirm", heading)) {
        if (!ImGui::IsPopupOpen("##saves-confirm")) {
            state.ask = shown = Ask::None;
        }
        return;
    }
    const auto inner = SettingsDialogInner();
    SavesNote(detail, inner);
    if (!state.restore.empty() && (state.ask == Ask::Import || state.ask == Ask::Restore ||
                                   state.ask == Ask::RepairUse || state.ask == Ask::Host))
        SavesNote(PathUtf8(state.restore), inner, 14);
    if (replace && state.ask != Ask::Host && state.ask != Ask::Pak)
        SavesNote("Selected save: " + adventure, inner);
    if (replace && state.ask != Ask::Host && state.ask != Ask::Pak && SavesDraftDirty(draft) &&
        state.ask != Ask::Apply && state.ask != Ask::MaxOut && state.ask != Ask::UnlockAll)
        SavesNote("Your unapplied editor draft will be discarded after this succeeds.", inner);
    constexpr float gap = 12.0F;
    const float cancel_width = PaddockRaceButtonWidth("CANCEL");
    const float confirm_width = PaddockRaceButtonWidth(confirm.c_str());
    const float actions_width = cancel_width + gap + confirm_width;
    const bool stack = actions_width > inner;
    if (!stack) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({at.x + inner - actions_width, at.y});
    }
    bool close = SavesRaceButton("CANCEL", std::min(inner, cancel_width));
    if (stack)
        PaddockGap(8.0F);
    else
        ImGui::SameLine(0.0F, gap);
    const bool confirmed = confirm_width > inner
                               ? SettingsButton(confirm.c_str(), inner, SharedSaveBusy(), SettingsButtonTone::Selected)
                               : SavesRaceButton(confirm.c_str(), confirm_width, SavesTone::Primary, SharedSaveBusy());
    if (confirmed) {
        const auto source = state.restore;
        const auto port = state.slot;
        if (state.ask == Ask::Discard) {
            draft = {};
        } else if (state.ask == Ask::MaxOut || state.ask == Ask::UnlockAll || state.ask == Ask::Apply) {
            codec::SaveImage image;
            std::string error;
            std::string expected = draft.revision;
            bool ready = false;
            if (draft.image) {
                image = *draft.image;
                ready = true;
            } else {
                ready = shared::load_draft(image, expected, error, modded);
            }
            if (ready) {
                if (state.ask == Ask::MaxOut)
                    shared::complete(image, state.slot);
                if (state.ask == Ask::UnlockAll)
                    shared::unlock(image);
                BeginSharedSaveJob([image = std::move(image), expected, modded](
                                       std::string& error) { return shared::apply(image, expected, error, modded); },
                                   adventure + " saved and verified.", modded);
            } else
                g_save_manager_status = error;
        } else if (state.ask == Ask::Reset)
            BeginSharedSaveJob([modded](std::string& error) { return shared::reset(error, modded); },
                               adventure + " reset. Its previous progress is kept in Recovery.", modded);
        else if (state.ask == Ask::Bundle)
            BeginSharedSaveJob([source, modded](std::string& error) { return import_bundle(source, error, modded); },
                               "Save bundle imported. Original images are kept in the transaction archive.", modded);
        else if (state.ask == Ask::Pak)
            BeginSharedSaveJob(
                [source, port](std::string& error) {
                    return shared::prepare(g_config_directory, error) && import_controller_pak(port, source, error);
                },
                "Controller Pak imported.");
        else if (state.ask == Ask::Host)
            BeginSharedSaveJob([source](std::string& error) { return shared::import_host(source, error); },
                               "Online Adventure selected. Personal progress was not changed.");
        else if (state.ask == Ask::DevelopmentCopy)
            BeginSharedSaveJob(
                [modded](std::string& error) { return shared::queue_development_copy(error, modded); },
                "Personal save snapshot queued for the next offline development launch. Existing development progress will be preserved.");
        else if (state.ask == Ask::Repair)
            BeginSharedSaveJob(
                [modded](std::string& error) {
                    bool changed;
                    std::filesystem::path original;
                    return shared::prepare(g_config_directory, error) &&
                           repair_adventure_checksums(changed, original, error, modded);
                },
                "Checksum repair complete. The original was retained.", modded);
        else {
            const bool repair = state.ask == Ask::RepairUse;
            BeginSharedSaveJob(
                [source, repair, modded](std::string& error) { return shared::adopt(source, repair, error, modded); },
                adventure + " replaced. Both the selected original and previous progress were retained.", modded);
        }
        close = true;
    }
    if (close) {
        ImGui::CloseCurrentPopup();
        state.ask = shown = Ask::None;
        state.restore.clear();
    }
    EndSettingsDialog();
}

struct SharedSaveInventory {
    std::vector<dkr::runtime::saves::StoredAdventure> records;
    bool truncated = false;
};
void DrawStoredSaves(SavesPageState& state, float width, bool locked) {
    using namespace dkr::runtime::saves;
    static std::future<SharedSaveInventory> pending;
    static SharedSaveInventory inventory;
    static std::size_t limit = 256;
    static std::uint64_t generation = 0;
    static auto refresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const bool reload = generation != g_shared_save_generation || now >= refresh;
    if (!pending.valid() && (reload || SavesRaceButton("REFRESH SAVES"))) {
        generation = g_shared_save_generation;
        refresh = now + std::chrono::seconds(10);
        pending = std::async(std::launch::async, [budget = limit] {
            SharedSaveInventory result;
            result.records = stored_adventures(budget, &result.truncated);
            return result;
        });
    }
    if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            inventory = pending.get();
            std::map<std::tuple<int, int, std::string>, std::size_t> groups;
            std::vector<StoredAdventure> compact;
            for (auto& record : inventory.records) {
                const auto bucket = record.scope;
                const auto key = std::tuple{bucket, int(record.kind), record.content_hash};
                const auto [it, added] = groups.emplace(key, compact.size());
                if (added)
                    compact.push_back(std::move(record));
                else
                    ++compact[it->second].copies;
            }
            inventory.records = std::move(compact);
        } catch (const std::exception& e) {
            g_save_manager_status = e.what();
        }
    }
    if (pending.valid())
        SavesNote("Reading saved progress...", width);
    if (state.scope == 1) {
        SavesNote("The host can continue the same Online Adventure with different mods.", width);
    } else {
        SavesNote("Choose which Adventure should receive the backup.", width);
        const int target =
            SettingsSectionSigns({"RESTORE TO VANILLA", "RESTORE TO MODDED"}, state.personal_modded, width);
        if (target >= 0)
            state.personal_modded = target == 1;
    }
    bool found = false;
    for (const auto& record : inventory.records) {
        if (record.scope != state.scope)
            continue;
        found = true;
        ImGui::PushID(PathUtf8(record.info.path).c_str());
        PaddockBox box(width, {19, 19});
        const auto inner = box.Inner();
        const auto label = record.kind == StoredSaveKind::CustomRecords      ? "Custom-course records"
                           : record.kind == StoredSaveKind::ControllerPak    ? "Controller Pak backup"
                           : record.kind == StoredSaveKind::ExperimentalPaks ? "Online Controller Paks"
                           : state.scope == 1                                ? "Online Adventure"
                                                                             : "Adventure backup";
        SavesNote(label, inner, 17, kSetText);
        SavesNote(record.preview, inner);
        SavesNote(SavesBackupTime(record.info.path), inner, 14);
        if (record.copies > 1)
            SavesNote(std::to_string(record.copies) + " identical copies grouped here. All originals are kept.", inner,
                      14);
        const bool adventure = record.kind == StoredSaveKind::Adventure;
        const bool development = record.info.path.generic_string().find("/development/") != std::string::npos;
        if (development)
            SavesNote("Development image - contains tool-session progress, not ordinary play.", inner, 14, kSetWarm);
        if (adventure && !locked) {
            if (SavesRaceButton(
                    record.info.valid ? (state.scope == 1 ? "USE FOR HOSTING" : "RESTORE BACKUP") : "REPAIR & USE COPY",
                    0, SavesTone::Primary, SharedSaveBusy() || (!record.info.valid && record.info.size != 512))) {
                state.restore = record.info.path;
                state.ask = !record.info.valid ? SavesPageState::Ask::RepairUse
                            : state.scope == 1 ? SavesPageState::Ask::Host
                                               : SavesPageState::Ask::Restore;
            }
            PaddockGap(8);
        }
        if (SavesRaceButton(record.info.valid ? "EXPORT COPY" : "EXPORT ORIGINAL IMAGE", 0, SavesTone::Plain,
                            SharedSaveBusy())) {
            if (record.info.valid)
                ExportAdventureWithDialog(record.info.path, record.kind);
            else
                SharedRawExportDialog(record.info.path);
        }
        PaddockGap(8);
        if (SavesRaceButton("COPY LOCATION"))
            SDL_SetClipboardText(PathUtf8(record.info.path).c_str());
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(16), PaddockRgb(0x0B2E40, 245), PaddockRgb(kSetBorder), 1});
        });
        PaddockGap(14);
        ImGui::PopID();
    }
    if (!found && !pending.valid())
        SavesNote("No saved images found here.", width);
    if (inventory.truncated) {
        SavesNote("More saved images are available. The list is limited to keep scanning responsive.", width);
        if (limit < 50000 && SavesRaceButton("LOAD MORE SAVES")) {
            limit = std::min<std::size_t>(limit * 4, 50000);
            refresh = {};
        }
    }
}

#include "cloud_saves/ui.inl"

void PaintSaveWorkspacePanel(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    PaddockPanel(draw, min, max, {PaddockRound(14), PaddockRgb(0x0B2B3B), PaddockRgb(kSetBorder), 1.0F});
}

float DrawSavesSlotList(SavesPageState& state, float width, float height, bool stacked,
                        const dkr::runtime::saves::codec::SaveImage* image) {
    constexpr float gap = 8.0F, pad = 12.0F;
    PaddockBox box(width, {pad, pad}, stacked ? 0.0F : height);
    PaddockText(PaddockReading(17.0F, true, 1.25F), PaddockRgb(kSetText), "Adventure slots", box.Inner());
    PaddockGap(gap);
    constexpr int count = static_cast<int>(dkr::runtime::saves::codec::kAdventureSlotCount);
    const float item_width = stacked ? (box.Inner() - gap * (count - 1)) / count : box.Inner();
    const auto row = ImGui::GetCursorScreenPos();
    const float heading_height = row.y - box.Origin().y;
    const float row_height =
        stacked ? 68.0F : std::clamp((height - heading_height - pad - gap * (count - 1)) / count, 60.0F, 72.0F);
    for (int index = 0; index < count; ++index) {
        if (stacked)
            ImGui::SetCursorScreenPos({row.x + static_cast<float>(index) * (item_width + gap), row.y});
        ImGui::PushID(index);
        const auto press = PaddockBeginPress("##adventure-slot", {item_width, row_height});
        const bool active = index == state.selected_slot;
        PaddockPanel(ImGui::GetWindowDrawList(), press.min, press.max,
                     {PaddockRound(9),
                      PaddockRgb(active          ? 0xA33620
                                 : press.hovered ? 0x1C4F65
                                                 : 0x12394B),
                      PaddockRgb(active ? kSetWarm : kSetBorder), 1.0F});
        const auto* slot = image ? &image->slots[static_cast<std::size_t>(index)] : nullptr;
        const bool named = slot && slot->name.find_first_not_of(' ') != std::string::npos;
        const bool occupied = slot && (named || slot->balloons[0] || slot->trophies);
        const std::string name =
            named ? slot->name : std::string(dkr::localisation::source(occupied ? "Unnamed" : "Empty"));
        const std::string progress =
            occupied ? std::to_string(slot->balloons[0]) + " " + std::string(dkr::localisation::source("balloons"))
                     : std::string{};
        const auto slot_label = std::string(dkr::localisation::source("Slot")) + " " + std::to_string(index + 1);
        const auto small_text = PaddockReading(13.0F, false, 1.2F);
        const auto large = PaddockReading(16.0F, true, 1.15F);
        auto* draw = ImGui::GetWindowDrawList();
        const float text_width = item_width - 18.0F;
        const float x = press.min.x + 9.0F;
        const PaddockTextStyle muted{PaddockCol(active ? 0xFFE1C5 : kSetMuted)};
        const PaddockTextStyle ink{PaddockCol(kSetText)};
        PaddockTextAt(draw, small_text, {x, press.min.y + 6.0F}, text_width, slot_label, muted);
        PaddockTextAt(draw, large, {x, press.min.y + 25.0F}, text_width, name, ink);
        PaddockTextAt(draw, small_text, {x, press.max.y - 20.0F}, text_width, progress, muted);
        PaddockEndPress(press, 9.0F, 1.0F, true);
        if (press.pressed) {
            state.selected_slot = index;
            state.section = SaveEditorSection::Progress;
            state.scroll_editor_to_top = true;
        }
        ImGui::PopID();
        if (!stacked && index + 1 < count)
            PaddockGap(gap);
    }
    return box.End(PaintSaveWorkspacePanel);
}

void DrawSavesWorkspace(SavesPageState& state, float width, const SaveManagerViewCache& view,
                        const AdventureView& adventure, bool locked) {
    constexpr float pad = 12.0F, gap = 12.0F;
    const bool valid = adventure.info.valid;
    const bool loaded = LoadSavesEditorDraft(state, valid);
    const auto& draft = g_adventure_drafts[state.personal_modded];
    const auto* image = loaded ? &*draft.image : nullptr;
    const auto footer = MeasureSavesFooter(width);
    const auto start = ImGui::GetCursorScreenPos();
    const float bottom = PanelFooterBounds(ImGui::GetCurrentWindow()).Max.y;
    const float footer_top = bottom - footer.height;
    const bool stacked = width < 530.0F;
    const float list_width = stacked ? width : std::clamp(width * 0.28F, 154.0F, 292.0F);
    const float right_width = stacked ? width : width - list_width - gap;
    const float workspace_height = std::max(180.0F, footer_top - start.y - gap);

    ImGui::PushID(state.personal_modded ? "modded-adventure" : "vanilla-adventure");
    const float list_height = DrawSavesSlotList(state, list_width, workspace_height, stacked, image);
    const ImVec2 editor = {stacked ? start.x : start.x + list_width + gap,
                           stacked ? start.y + list_height + gap : start.y};
    const float editor_height = std::max(100.0F, start.y + workspace_height - editor.y);
    PaintSaveWorkspacePanel(ImGui::GetWindowDrawList(), editor, {editor.x + right_width, editor.y + editor_height});
    ImGui::SetCursorScreenPos({editor.x + pad, editor.y + pad});
    const float inner = right_width - pad * 2.0F;
    const int section = SettingsSectionSigns({"PROGRESS", "UNLOCKS", "FILES"}, int(state.section), inner, true);
    if (section >= 0 && section != int(state.section)) {
        state.section = static_cast<SaveEditorSection>(section);
        state.scroll_editor_to_top = true;
    }
    if (SaveDraftLayoutCheck()) {
        g_save_draft_geometry.slots = {start, {start.x + list_width, start.y + list_height}};
        g_save_draft_geometry.editor = {editor, {editor.x + right_width, editor.y + editor_height}};
        g_save_draft_geometry.selected_slot = state.selected_slot;
        g_save_draft_geometry.modded = state.personal_modded;
    }
    PaddockGap(8.0F);
    if (state.section == SaveEditorSection::Progress && valid && loaded) {
        constexpr const char* all_label = "MAX OUT THIS SAVE";
        constexpr const char* slot_label = "Max out this slot";
        const float all_width = PaddockRaceButtonWidth(all_label);
        const float slot_width = SettingsButtonWidth(slot_label);
        const auto summary_type = PaddockReading(15.0F, true, 1.25F);
        const std::string summary =
            std::string(dkr::localisation::source(state.personal_modded ? "Modded Adventure" : "Vanilla Adventure")) +
            " / Slot " + std::to_string(state.selected_slot + 1);
        const float summary_width = PaddockMeasure(summary_type, summary) + 2.0F;
        const float action_width = all_width + slot_width + gap;
        const ImVec2 header = {editor.x + pad, ImGui::GetCursorScreenPos().y};
        const bool inline_summary = summary_width + action_width + gap <= inner;
        ImGui::SetCursorScreenPos({header.x, header.y + (inline_summary ? 11.0F : 0.0F)});
        PaddockText(summary_type, PaddockRgb(kSetText), summary, inner);
        const ImVec2 actions = {header.x + (inline_summary ? inner - action_width : 0.0F),
                                inline_summary ? header.y : ImGui::GetItemRectMax().y + 5.0F};
        ImGui::SetCursorScreenPos(actions);
        if (SavesRaceButton(all_label, std::min(inner, all_width), SavesTone::Featured, locked)) {
            state.slot = -1;
            state.ask = SavesPageState::Ask::MaxOut;
        }
        if (action_width <= inner)
            ImGui::SameLine(0.0F, gap);
        else
            ImGui::SetCursorScreenPos({actions.x, actions.y + 50.0F});
        if (SettingsButton(slot_label, std::min(inner, slot_width), locked)) {
            state.slot = state.selected_slot;
            state.ask = SavesPageState::Ask::MaxOut;
        }
        PaddockGap(10.0F);
    }
    const float body_top = ImGui::GetCursorScreenPos().y;
    const float height = std::max(24.0F, editor.y + editor_height - pad - body_top);
    ImGui::SetCursorScreenPos({editor.x + pad, body_top});
    const bool visible = ImGui::BeginChild("##adventure-editor", {inner, height}, false,
                                           ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoBackground);
    LibraryNavigationScrollWindow(ImGui::GetCurrentWindow());
    if (std::exchange(state.scroll_editor_to_top, false) || (SaveDraftLayoutCheck() && g_launcher_layout_step == 0))
        ImGui::SetScrollY(0.0F);
    if (SaveDraftLayoutCheck()) {
        g_save_draft_geometry.editor_scroll = ImGui::GetScrollY();
        g_save_draft_geometry.editor_body = ImGui::GetCurrentWindow()->OuterRectClipped;
    }
    if (visible) {
        const float content_width = ImGui::GetContentRegionAvail().x - 4.0F;
        ImGui::BeginDisabled(locked);
        if (state.section == SaveEditorSection::Files) {
            DrawSavesFiles(state, content_width, adventure, view);
        } else if (!valid || !loaded) {
            SavesNote(adventure.info.exists ? "This Adventure needs recovery." : "No Adventure save yet.",
                      content_width, 17.0F, kSetWarm);
            if (SavesRaceButton("OPEN FILES"))
                state.section = SaveEditorSection::Files;
        } else if (state.section == SaveEditorSection::Progress) {
            DrawSavesSlotEditor(state, content_width, state.selected_slot);
        } else if (state.section == SaveEditorSection::Unlocks) {
            DrawSavesUnlocks(state, content_width, content_width);
        }
        ImGui::EndDisabled();
        PaddockGap(8.0F);
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::SetCursorScreenPos({start.x, editor.y + editor_height});
    DrawSavesDraftBar(state, width, footer_top, footer, SavesDraftDirty(draft), locked, state.ask);
}

void DrawSaveManagerPage(float available_width, bool live) {
    using namespace dkr::runtime::saves;
    auto& state = g_saves_page;
    const auto width = available_width;
    static std::uint64_t generation = 0;
    const auto frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() || state.last_frame != frame - 1;
    if (entered || generation != g_shared_save_generation) {
        InvalidateSaveManagerViewCache();
        ++g_shared_save_generation;
        generation = g_shared_save_generation;
        for (auto& draft : g_adventure_drafts)
            if (!SavesDraftDirty(draft))
                draft = {};
    }
    state.context = ImGui::GetCurrentContext();
    state.last_frame = frame;
    PumpSharedSaveJob();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    const auto heading = ImGui::GetCursorScreenPos();
    DrawPageHeading("SAVE MANAGER");
    const auto after_heading = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({heading.x + width - 116.0F, heading.y + 12.0F});
    if (PaddockTextLink("Back to Play"))
        g_page_navigation_request = kPagePlay;
    ImGui::SetCursorScreenPos(after_heading);
    PaddockGap(8);
    if (!g_save_manager_status.empty())
        DrawFriendlyStatus(g_save_manager_status, width, dkr::runtime::copy::Area::Saves);
    const bool lobby = dkr::runtime::netplay::session().active();
    const bool locked = live || lobby || SharedSaveBusy();
    constexpr std::array scopes{0, 2, 1, 3, 4};
    const int current = static_cast<int>(std::ranges::find(scopes, state.scope) - scopes.begin());
    const int picked = SettingsSectionSigns(
        {"VANILLA SAVE", "MODDED SAVE", "ONLINE SAVES", "BACKUPS & RECOVERY", "CLOUD SAVES"}, current, width, true);
    if (picked >= 0 && picked != current) {
        state.scope = scopes[static_cast<std::size_t>(picked)];
        state.ask = SavesPageState::Ask::None;
        state.restore.clear();
        state.scroll_editor_to_top = true;
        ImGui::SetScrollY(0.0F);
    }
    if (state.scope == 0 || state.scope == 2)
        state.personal_modded = state.scope == 2;
    PaddockGap(8);
    if (live || lobby)
        SavesNote("Return to the launcher and leave your lobby before changing saves.", width);
    if (state.scope == 4)
        DrawCloudSaves(width, locked);
    else if (state.scope != 0 && state.scope != 2)
        DrawStoredSaves(state, width, locked);
    else {
        const auto& view = CachedSaveManagerView();
        const auto& adventure = view.adventures[state.personal_modded];
        if (!view.valid) {
            SavesNote("Reading your Adventure save...", width);
            ImGui::PopStyleVar();
            return;
        }
        DrawSavesWorkspace(state, width, view, adventure, locked);
    }
    if (!g_shared_save_import.path.empty() && !locked) {
        state.restore = std::move(g_shared_save_import.path);
        state.slot = g_shared_save_import.port;
        state.import_modded = g_shared_save_import.modded;
        state.ask = g_shared_save_import.kind == 1   ? SavesPageState::Ask::Bundle
                    : g_shared_save_import.kind == 2 ? SavesPageState::Ask::Pak
                                                     : SavesPageState::Ask::Import;
        g_shared_save_import = {};
    }
    ImGui::PopStyleVar();
    if (!locked)
        DrawSavesConfirmations(state);
}
