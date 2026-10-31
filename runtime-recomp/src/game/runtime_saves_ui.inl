// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_graphics_ui.inl. Presentation only: backups, imports, exports,
// repairs, resets and the Save Builder go through dkr::runtime::saves exactly
// as before.
//
// SAVE MANAGER in the look of tools/launcher-html (pages/saves.js,
// styles/saves.css): the current save, then Overview / Edit progress /
// Transfer & Paks. Edits stay in a draft until they are applied, and every
// replacing action asks first.

struct SavesPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    int section = 0;  // Overview, Edit progress, Transfer & Paks
    int scope = 0;    // Offline, Online, Modded, Recovery
    int tab = 0;      // Slot 1-3, Unlocks, Records
    std::array<bool, 3> courses_open{};
    std::array<bool, 2> records_open{};
    bool all_backups = false;
    // The save as it was loaded into the builder, to tell a draft's changes.
    std::optional<dkr::runtime::saves::codec::SaveImage> original;
    // The slot summaries on Overview, read again with the view cache.
    std::optional<dkr::runtime::saves::codec::SaveImage> summary;
    std::chrono::steady_clock::time_point summary_at{};
    // The confirmation in flight.
    enum class Ask { None, Repair, Reset, Restore, MaxOut, UnlockAll, Discard, Apply, Import, Bundle, Pak, RepairUse, Host, DevelopmentCopy } ask = Ask::None;
    std::filesystem::path restore;
    int slot = 0;
};
SavesPageState g_saves_page;

constexpr unsigned kSavesAccent = 0x1AC2A3;
constexpr unsigned kSavesError = 0xFFAAA0;

enum class SavesTone { Plain, Primary, Danger };

// .saves-button: a race button on the page's quiet #123e50, or the accent
// primary, or the danger red.
bool SavesRaceButton(const char* label, float width = 0.0F, SavesTone tone = SavesTone::Plain,
                     bool disabled = false, const char* reason = nullptr) {
    RaceButtonLook look;
    look.hover_in = 0.12F;
    if (tone == SavesTone::Primary) {
        look.fill = kSavesAccent;
        look.border = kSavesAccent;
        look.text = 0x062C30;
        look.text_shadow = false;
    } else if (tone == SavesTone::Danger) {
        look.fill = 0x73171A;
        look.border = 0xD97773;
    } else {
        look.fill = 0x123E50;
    }
    if (width <= 0.0F) width = PaddockRaceButtonWidth(label);
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
    if (text.empty()) return;
    PaddockText(PaddockReading(px, false, 1.45F), PaddockRgb(colour), text, width);
    PaddockGap(13.0F);
}

std::string SavesBackupTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error) return PathUtf8(path.filename());
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

bool SavesDraftDirty() {
    using dkr::runtime::saves::codec::encode;
    return g_save_builder_image && g_saves_page.original &&
           encode(*g_save_builder_image) != encode(*g_saves_page.original);
}

void SavesForgetDraft() {
    g_save_builder_image.reset();
    g_saves_page.original.reset();
    g_saves_page.summary.reset();
}

// ------------------------------------------------------------ editor

// A row of quiet tabs over a hairline (.saves-tabs).
int SavesEditorTabs(const std::vector<const char*>& labels, int current, float width) {
    const PaddockType type = PaddockSign(17.0F, 1.2F, 0.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float natural = 8.0F + 4.0F * static_cast<float>(labels.size() - 1U);
    std::vector<float> widths;
    for (const char* label : labels) {
        widths.push_back(std::ceil(PaddockMeasure(type, label) + 28.0F));
        natural += widths.back();
    }
    // flex: 1 0 auto shares the spare room between the tabs.
    const float spare = std::max(width - natural, 0.0F) / static_cast<float>(labels.size());
    int picked = -1;
    float x = at.x + 4.0F;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const float tab_width = widths[index] + spare;
        ImGui::SetCursorScreenPos({x, at.y + 4.0F});
        ImGui::PushID(static_cast<int>(index));
        const PaddockPress press = PaddockBeginPress("##tab", {tab_width, 44.0F});
        ImGui::PopID();
        if (press.pressed) picked = static_cast<int>(index);
        const bool selected = static_cast<int>(index) == current;
        const PaddockRadii radii{6.0F, 6.0F, 0.0F, 0.0F};
        if (selected) {
            PaddockFill(draw, press.min, press.max, radii, PaddockCol(0x123E50));
        } else if (press.hover > 0.0F) {
            PaddockFill(draw, press.min, press.max, radii, PaddockApply(PaddockMix(
                PaddockRgb(0x174C60, 0U), PaddockRgb(0x174C60), press.hover)));
        }
        if (selected) {
            draw->AddRectFilled({press.min.x, press.max.y - 3.0F}, press.max, PaddockCol(kSetWarm));
        }
        const unsigned ink = selected ? 0xFFF0C2U : press.hover > 0.5F ? kSetText : kSetMuted;
        const float text_width = PaddockMeasure(type, labels[index]);
        PaddockDrawRun(draw, type, {std::round(press.min.x + (tab_width - text_width) * 0.5F),
                                    std::round(press.min.y + (44.0F - 3.0F - type.line) * 0.5F)},
                       PaddockCol(ink), labels[index], labels[index] + std::strlen(labels[index]));
        PaddockEndPress(press, 6.0F, 1.0F, false);
        if (press.focused) {
            draw->AddRect({press.min.x + 1.5F, press.min.y + 1.5F}, {press.max.x - 1.5F, press.max.y - 1.5F},
                          PaddockCol(kSetFocus), 6.0F, 0, 3.0F);
        }
        x += tab_width + 4.0F;
    }
    const float bottom = at.y + 4.0F + 44.0F;
    draw->AddRectFilled({at.x, bottom}, {at.x + width, bottom + 1.0F}, PaddockCol(kSetBorder));
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, bottom + 1.0F - at.y});
    return picked;
}

// A grid of settings (.saves-setting-grid): three columns, two below 780 px of
// panel and one below 540 px.
struct SavesGrid {
    int count = 0;
    ImVec2 origin{};
    float column = 0.0F;
    int per_row = 3;
    float row_top = 0.0F;
    float row_bottom = 0.0F;

    SavesGrid(float width, float panel) : origin(ImGui::GetCursorScreenPos()) {
        per_row = panel <= 540.0F ? 1 : panel <= 780.0F ? 2 : 3;
        column = std::floor((width - 16.0F * static_cast<float>(per_row - 1)) / static_cast<float>(per_row));
        row_top = row_bottom = origin.y;
    }
    void Begin() {
        const int slot = count % per_row;
        if (slot == 0 && count > 0) row_top = row_bottom + 16.0F;
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

void DrawSavesSlotEditor(SavesPageState& state, float width, float panel, int slot_index) {
    using namespace dkr::runtime::saves::codec;
    AdventureSlot& slot = g_save_builder_image->slots[static_cast<std::size_t>(slot_index)];
    ImGui::PushID(slot_index);
    PaddockText(PaddockSign(19.0F, 1.45F, 0.0F), PaddockRgb(kSetText), "Racer initials", width);
    PaddockGap(13.0F);
    {
        // Three letter pickers, 280 px wide together (.saves-initials).
        static const std::vector<SettingsChoice> kLetters = [] {
            std::vector<SettingsChoice> letters;
            for (char letter = 'A'; letter <= 'Z'; ++letter) letters.push_back({std::string(1, letter), {}, false});
            letters.push_back({".", {}, false});
            letters.push_back({"?", {}, false});
            letters.push_back({"SPACE", {}, false});
            return letters;
        }();
        std::string padded = sanitise_name(slot.name);
        padded.resize(3U, ' ');
        const float cell = std::floor((std::min(width, 280.0F) - 16.0F) / 3.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        for (int character = 0; character < 3; ++character) {
            ImGui::SetCursorScreenPos({at.x + (cell + 8.0F) * static_cast<float>(character), at.y});
            ImGui::BeginGroup();
            ImGui::PushID(character);
            const char current = padded[static_cast<std::size_t>(character)];
            int selected = current >= 'A' && current <= 'Z' ? current - 'A'
                         : current == '.' ? 26 : current == '?' ? 27 : 28;
            if (SettingsDropdown("letter", {}, &selected, kLetters, cell)) {
                padded[static_cast<std::size_t>(character)] =
                    selected < 26 ? static_cast<char>('A' + selected)
                    : selected == 26 ? '.' : selected == 27 ? '?' : ' ';
                slot.name = sanitise_name(padded);
            }
            ImGui::PopID();
            ImGui::EndGroup();
        }
        ImGui::SetCursorScreenPos({at.x, at.y + 52.0F});
        ImGui::Dummy({width, 0.0F});
        PaddockGap(20.0F);
    }

    SavesSeparator("Golden Balloons", width, false);
    SavesNote("Up to 7 on the central island and 8 in each world. The total updates automatically.", width);
    unsigned worlds = 0U;
    for (std::size_t world = 1U; world < kWorldCount; ++world) worlds += slot.balloons[world];
    int hub = slot.balloons[0] > worlds
        ? static_cast<int>(std::min<unsigned>(slot.balloons[0] - worlds, kMaximumHubBalloons)) : 0;
    {
        // The total (.saves-progress): a dark bar that fills amber.
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PaddockFill(draw, at, {at.x + width, at.y + 32.0F}, PaddockRound(6.0F), PaddockCol(0x153E50));
        const float t = static_cast<float>(slot.balloons[0]) / static_cast<float>(kMaximumTotalBalloons);
        if (t > 0.0F) {
            draw->PushClipRect(at, {at.x + width * t, at.y + 32.0F}, true);
            PaddockFill(draw, at, {at.x + width, at.y + 32.0F}, PaddockRound(6.0F), PaddockCol(0x806000));
            draw->PopClipRect();
        }
        const std::string total = std::to_string(slot.balloons[0]) + " / " + std::to_string(kMaximumTotalBalloons);
        const PaddockType type = PaddockReading(16.0F, true, 1.0F);
        const float text_width = PaddockMeasure(type, total);
        PaddockDrawRun(draw, type, {std::round(at.x + (width - text_width) * 0.5F),
                                    std::round(at.y + (32.0F - type.line) * 0.5F)},
                       PaddockCol(kSetText), total.data(), total.data() + total.size());
        ImGui::Dummy({width, 32.0F});
        PaddockGap(18.0F);
    }
    constexpr std::array<const char*, kWorldCount> kAreas{{
        "DKR-R", "Dino Domain", "Sherbet Island", "Snowflake Mountain", "Dragon Forest", "Future Fun Land"}};
    const auto count = [](int value) { return SettingsRangeValue{std::to_string(value), ""}; };
    SavesGrid balloons(width, panel);
    for (std::size_t area = 0; area < kWorldCount; ++area) {
        balloons.Begin();
        ImGui::PushID(static_cast<int>(area));
        int value = area == 0U ? hub : slot.balloons[area];
        if (SettingsRange("##balloons", kAreas[area], &value, 0,
                          area == 0U ? kMaximumHubBalloons : kMaximumWorldBalloons, balloons.column, count)) {
            if (area == 0U) hub = value;
            else slot.balloons[area] = static_cast<std::uint8_t>(value);
            unsigned total = static_cast<unsigned>(hub);
            for (std::size_t world = 1U; world < kWorldCount; ++world) total += slot.balloons[world];
            slot.balloons[0] = static_cast<std::uint8_t>(std::min<unsigned>(total, kMaximumTotalBalloons));
        }
        ImGui::PopID();
        balloons.End();
    }
    balloons.Finish(width);

    SavesSeparator("Amulets and keys", width, false);
    SavesGrid keys(width, panel);
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
    constexpr std::array<const char*, 4> kKeys{{
        "Dino Domain key", "Snowflake Mountain key", "Sherbet Island key", "Dragon Forest key"}};
    for (std::size_t key = 0; key < kKeys.size(); ++key) {
        keys.Begin();
        const auto mask = static_cast<std::uint8_t>(1U << (key + 1U));
        bool value = (slot.keys & mask) != 0U;
        ImGui::PushID(static_cast<int>(key));
        if (SettingsCheck("##key", kKeys[key], &value, keys.column)) {
            if (value) slot.keys |= mask;
            else slot.keys &= static_cast<std::uint8_t>(~mask);
        }
        ImGui::PopID();
        keys.End();
    }
    keys.Finish(width);

    bool& courses_open = state.courses_open[static_cast<std::size_t>(slot_index)];
    if (SavesRaceButton(courses_open ? "HIDE COURSE PROGRESS (34)" : "SHOW COURSE PROGRESS (34)")) {
        courses_open = !courses_open;
    }
    PaddockGap(16.0F);
    if (courses_open) {
        const auto& names = course_names();
        SavesGrid courses(width, panel);
        for (std::size_t course = 0; course < kCourseCount; ++course) {
            courses.Begin();
            ImGui::PushID(static_cast<int>(course));
            int status = slot.course_status[course];
            if (SettingsDropdown("course", names[course], &status,
                                 {"Not started", "Race won", "Silver Coins won", "Complete"}, courses.column)) {
                slot.course_status[course] = static_cast<std::uint8_t>(status);
            }
            ImGui::PopID();
            courses.End();
        }
        courses.Finish(width);
    }
    if (SavesRaceButton("MAX OUT THIS ADVENTURE")) {
        state.ask = SavesPageState::Ask::MaxOut;
        state.slot = slot_index;
    }
    ImGui::PopID();
}

void DrawSavesUnlocks(SavesPageState& state, float width, float panel) {
    auto& settings = g_save_builder_image->settings;
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
    if (SettingsDropdown("language", "Language", &language, {"English", "German", "French", "Japanese"},
                         grid.column)) {
        settings.language = static_cast<std::uint8_t>(language);
    }
    grid.End();
    grid.Finish(width);

    SavesSeparator("T.T. time-trial victories", width, false);
    constexpr std::array<const char*, 20> kTrials{{
        "Ancient Lake", "Fossil Canyon", "Jungle Falls", "Hot Top Volcano", "Whale Bay", "Crescent Island",
        "Pirate Lagoon", "Treasure Caves", "Everfrost Peak", "Walrus Cove", "Snowball Valley", "Frosty Village",
        "Boulder Canyon", "Greenwood Village", "Windmill Plains", "Haunted Woods", "Spacedust Alley",
        "Darkmoon Caverns", "Star City", "Spaceport Alpha"}};
    SavesGrid trials(width, panel);
    for (std::size_t trial = 0; trial < kTrials.size(); ++trial) {
        trials.Begin();
        ImGui::PushID(static_cast<int>(trial));
        SettingsCheck("##trial", kTrials[trial], &settings.tt_trials[trial], trials.column);
        ImGui::PopID();
        trials.End();
    }
    trials.Finish(width);
    if (SavesRaceButton("UNLOCK ALL RACERS AND MODES")) state.ask = SavesPageState::Ask::UnlockAll;
}

void DrawSavesRecords(SavesPageState& state, float width) {
    using namespace dkr::runtime::saves::codec;
    PaddockText(PaddockSign(19.0F, 1.45F, 0.0F), PaddockRgb(kSetText),
                "Personal records are view-only. Race in Time Trial mode to set or improve them.", width);
    PaddockGap(13.0F);
    const std::array<std::pair<const char*, const std::array<Record, kRecordCount>*>, 2> kLists{{
        {"Fastest laps", &g_save_builder_image->fastest_laps},
        {"Course times", &g_save_builder_image->course_times},
    }};
    for (std::size_t list = 0; list < kLists.size(); ++list) {
        ImGui::PushID(static_cast<int>(list));
        // The summary (.saves-records summary): a quiet bar with a marker.
        const PaddockPress press = PaddockBeginPress("##records", {width, 44.0F});
        bool& open = state.records_open[list];
        if (press.pressed) open = !open;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PaddockFill(draw, press.min, press.max, PaddockRound(8.0F),
                    PaddockApply(PaddockMix(PaddockRgb(0x123E50), PaddockRgb(0x174C60), press.hover)));
        const PaddockType type = PaddockSign(19.0F, 1.2F, 0.0F);
        const ImVec2 marker{press.min.x + 16.0F + 4.0F, (press.min.y + press.max.y) * 0.5F};
        if (open) {
            draw->AddTriangleFilled({marker.x - 4.5F, marker.y - 3.0F}, {marker.x + 4.5F, marker.y - 3.0F},
                                    {marker.x, marker.y + 4.0F}, PaddockCol(kSetText));
        } else {
            draw->AddTriangleFilled({marker.x - 3.0F, marker.y - 4.5F}, {marker.x + 4.0F, marker.y},
                                    {marker.x - 3.0F, marker.y + 4.5F}, PaddockCol(kSetText));
        }
        PaddockDrawRun(draw, type, {press.min.x + 36.0F, std::round(press.min.y + (44.0F - type.line) * 0.5F)},
                       PaddockCol(kSetText), kLists[list].first,
                       kLists[list].first + std::strlen(kLists[list].first));
        PaddockEndPress(press, 8.0F, 1.0F);
        if (open) {
            const auto& records = *kLists[list].second;
            for (std::size_t index = 0; index < records.size(); ++index) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                PaddockGap(12.0F);
                PaddockText(PaddockReading(16.0F, false, 1.4F), PaddockRgb(kSetText), record_names()[index], width);
                PaddockGap(4.0F);
                std::string detail = "Time: " + (records[index].time != 0U ? FormatRecordTime(records[index].time)
                                                                             : std::string("No personal record yet"));
                if (records[index].time != 0U) {
                    detail += "    Racer: " + (records[index].initials.empty() ? std::string("---")
                                                                               : records[index].initials);
                }
                PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted), detail, width);
                PaddockGap(12.0F);
                const float bottom = ImGui::GetCursorScreenPos().y;
                draw->AddRectFilled({at.x, bottom - 1.0F}, {at.x + width, bottom}, PaddockCol(kSetBorder));
            }
        }
        PaddockGap(12.0F);
        ImGui::PopID();
    }
}

void DrawSavesEditor(SavesPageState& state, float width, float panel, bool editable, bool exists) {
    using namespace dkr::runtime::saves;
    if (!editable) {
        SavesSeparator("A valid save is needed", width, false);
        SavesNote(exists ? "Restore a valid backup or import a working save from Overview before editing."
                         : "Start an adventure in the game, import a save, or create a fresh adventure from Overview.",
                  width);
        if (SavesRaceButton("GO TO OVERVIEW")) state.section = 0;
        return;
    }
    if (!g_save_builder_image) {
        codec::SaveImage image{};
        std::string error;
        if (load_adventure(image, error)) {
            // Opening the editor must not silently normalise existing progress.
            g_save_builder_image = image;
            state.original = image;
            g_shared_draft_revision=dkr::runtime::saves::shared::revision();
        } else {
            g_save_manager_status = error;
            return;
        }
    }
    if (!state.original) state.original = *g_save_builder_image;
    SavesNote("Manual edits use Apply Changes. Completion and unlock buttons save when confirmed.", width);
    const int picked = SavesEditorTabs({"SLOT 1", "SLOT 2", "SLOT 3", "UNLOCKS", "RECORDS"}, state.tab, width);
    if (picked >= 0) state.tab = picked;
    PaddockGap(20.0F);
    if (state.tab < 3) DrawSavesSlotEditor(state, width, panel, state.tab);
    else if (state.tab == 3) DrawSavesUnlocks(state, width, panel);
    else DrawSavesRecords(state, width);

    // The draft bar (.saves-draft-bar): it rides the bottom of the panel
    // while the editor scrolls under it.
    PaddockGap(20.0F);
    const bool dirty = SavesDraftDirty();
    const ImVec2 natural = ImGui::GetCursorScreenPos();
    const float discard_width = PaddockRaceButtonWidth("DISCARD CHANGES");
    const float apply_width = PaddockRaceButtonWidth("APPLY CHANGES");
    const bool wraps = width - discard_width - apply_width - 24.0F < 180.0F;
    const float bar_height = 1.0F + 16.0F + (wraps ? 49.0F + 12.0F + 44.0F : std::max(49.0F, 44.0F)) + 16.0F;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float visible_bottom = window->InnerRect.Max.y;
    const float top = std::min(natural.y, visible_bottom - bar_height);
    // Its own child window, so it stays above the editor it covers for both
    // drawing and the pointer.
    ImGui::SetCursorScreenPos({natural.x, top});
    ImGui::BeginChild("##draft-bar", {width, bar_height}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoBackground);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({natural.x, top}, {natural.x + width, top + bar_height}, PaddockCol(0x0A202C));
    draw->AddRectFilled({natural.x, top}, {natural.x + width, top + 1.0F}, PaddockCol(kSetBorder));
    ImGui::SetCursorScreenPos({natural.x, top + 17.0F});
    ImGui::BeginGroup();
    const float text_width = wraps ? width : width - discard_width - apply_width - 24.0F;
    PaddockText(PaddockReading(17.0F, false, 1.5F), PaddockRgb(kSetText),
                dirty ? "Unapplied changes" : "No pending changes", text_width);
    PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetMuted), "Applying creates a backup first.",
                text_width);
    ImGui::EndGroup();
    const float buttons_y = wraps ? top + 17.0F + 49.0F + 12.0F : top + 17.0F + (49.0F - 44.0F) * 0.5F;
    ImGui::SetCursorScreenPos({wraps ? natural.x : natural.x + width - apply_width - 12.0F - discard_width,
                               buttons_y});
    if (SavesRaceButton("DISCARD CHANGES", discard_width, SavesTone::Plain, !dirty,
                        "There are no changes to discard.")) {
        state.ask = SavesPageState::Ask::Discard;
    }
    ImGui::SameLine(0.0F, 12.0F);
    if (SavesRaceButton("APPLY CHANGES", apply_width, SavesTone::Primary, !dirty,
                        "There are no changes to apply.")) {
        state.ask = SavesPageState::Ask::Apply;
    }
    ImGui::EndChild();
    ImGui::SetCursorScreenPos({natural.x, natural.y + bar_height});
    ImGui::Dummy({width, 0.0F});
}

// ------------------------------------------------------------ overview

void DrawSavesOverview(SavesPageState& state,float width,float panel,const SaveManagerViewCache& view,bool editable) {
    using namespace dkr::runtime::saves;
    SavesNote("One personal save for normal play, with or without mods. Three Adventure slots, shared unlocks and records.",width);
    const bool busy=SharedSaveBusy();
    const auto button=[&](const char* label,bool disabled=false,SavesTone tone=SavesTone::Plain){
        return SavesRaceButton(label,0,tone,busy||disabled);
    };
    if(button("BACK UP SAVE",!editable,SavesTone::Primary))BeginSharedSaveJob([](std::string& error){std::filesystem::path created;return backup_adventure(created,error);},"Personal save backed up.");
    PaddockGap(8);
    if(button("EXPORT SAVE",!editable))ExportAdventureWithDialog();
    PaddockGap(8);
    if(button("IMPORT SAVE"))ImportAdventureWithDialog();
    PaddockGap(14);
    if(!view.adventure.exists) {
        if(button("CREATE PERSONAL SAVE"))state.ask=SavesPageState::Ask::Reset;
        return;
    }
    if(editable) {
        if(!state.summary&&view.image)state.summary=view.image;
        if(state.summary) {
            SavesSeparator("Adventure slots",width);
            for(unsigned i=0;i<3;++i) {
                const auto& slot=state.summary->slots[i];
                SavesNote("Slot "+std::to_string(i+1)+" - "+(slot.name.empty()?"---":slot.name)+" - "+std::to_string(slot.balloons[0])+" / 47 balloons",width,18,kSetText);
            }
            const auto& settings=state.summary->settings;
            SavesNote(std::string("Drumstick: ")+(settings.drumstick?"unlocked":"locked")+"   |   T.T.: "+
                (std::all_of(settings.tt_trials.begin(),settings.tt_trials.end(),[](bool x){return x;})?"unlocked":"locked")+
                "   |   Adventure Two: "+(settings.adventure_two?"unlocked":"locked"),width);
        }
        if(button("MAX OUT ENTIRE SAVE",false,SavesTone::Primary)){state.slot=-1;state.ask=SavesPageState::Ask::MaxOut;}
        PaddockGap(8);
        if(button("UNLOCK ALL RACERS AND MODES"))state.ask=SavesPageState::Ask::UnlockAll;
        SavesSeparator("Development tools",width);
        SavesNote("Dev Menu uses a separate copy, not this personal save. New development copies start from your saved progress. You can refresh an existing copy below; its previous progress is kept.",width);
        if(button("COPY PERSONAL SAVE TO DEVELOPMENT"))state.ask=SavesPageState::Ask::DevelopmentCopy;
    }
    SavesSeparator("Older progress and backups",width);
    SavesNote("Older mod-set saves are kept. Choose one from Backups & Recovery if it contains the progress you want to keep. Nothing is merged or deleted automatically.",width);
    if(button("OPEN BACKUPS & RECOVERY")){state.scope=3;state.ask=SavesPageState::Ask::None;}
    PaddockGap(20);
    if(button("RESET PERSONAL SAVE",false,SavesTone::Danger))state.ask=SavesPageState::Ask::Reset;
    (void)panel;
}


// ------------------------------------------------------------ transfer

// Complete manager actions and stored-save recovery. This replaces the old
// split-profile UI, but keeps the proven slot/record editor above unchanged.
void SharedPakDialog(int port,bool importing) {
    if(SharedSaveBusy())return;
    if(NFD_Init()!=NFD_OKAY){g_save_manager_status="The system file picker could not be opened.";return;}
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"DKR-R Controller Pak","mpk"}};
    const auto dialog=importing?NFD_OpenDialogU8(&result,filters,1,nullptr):NFD_SaveDialogU8(&result,filters,1,nullptr,"dkr-controller-pak.mpk");
    if(dialog==NFD_OKAY) {
        auto path=std::filesystem::u8path(result);NFD_FreePathU8(result);
        if(importing)g_shared_save_import={path,2,port};
        else BeginSharedSaveJob([path,port](std::string& error){return dkr::runtime::saves::export_controller_pak(port,path,error);},"Controller Pak exported.");
    } else if(dialog==NFD_ERROR)g_save_manager_status=NFD_GetError();
    NFD_Quit();
}
void SharedRawExportDialog(const std::filesystem::path& source) {
    if(NFD_Init()!=NFD_OKAY)return;
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"Original recovery image","bin"}};
    if(NFD_SaveDialogU8(&result,filters,1,nullptr,"original-save-image.bin")==NFD_OKAY) {
        auto dest=std::filesystem::u8path(result);NFD_FreePathU8(result);
        BeginSharedSaveJob([source,dest](std::string& error){return dkr::runtime::saves::shared::export_raw(source,dest,error);},"Original image exported unchanged. It has not been repaired.");
    }
    NFD_Quit();
}
void DrawSavesTransfer(float width,float panel,const SaveManagerViewCache& view) {
    using namespace dkr::runtime::saves;
    SavesNote("Move your personal Adventure, custom-course records and Controller Paks together. Older save bundles can still be imported.",width);
    if(SavesRaceButton("EXPORT SAVE BUNDLE",0,SavesTone::Plain,SharedSaveBusy()))ExportSaveBundleWithDialog();
    PaddockGap(8);
    if(SavesRaceButton("IMPORT SAVE BUNDLE",0,SavesTone::Plain,SharedSaveBusy()))ImportSaveBundleWithDialog();
    SavesSeparator("Controller Paks",width);
    SavesNote("DKR-R virtual memory cards. Use DKR-R-format .mpk files, not an unrelated emulator format.",width);
    for(int port=0;port<4;++port) {
        ImGui::PushID(port);const auto& pak=view.controller_paks[port];
        SavesNote("Controller "+std::to_string(port+1)+": "+(!pak.exists?"not created":pak.valid?"ready":"needs recovery"),width,18,kSetText);
        if(SavesRaceButton("BACK UP PAK",0,SavesTone::Plain,SharedSaveBusy()||!pak.valid))
            BeginSharedSaveJob([port](std::string& error){std::filesystem::path created;return backup_controller_pak(port,created,error);},"Controller Pak backed up.");
        PaddockGap(8);
        if(SavesRaceButton("EXPORT PAK",0,SavesTone::Plain,SharedSaveBusy()||!pak.valid))SharedPakDialog(port,false);
        PaddockGap(8);
        if(SavesRaceButton("IMPORT PAK",0,SavesTone::Plain,SharedSaveBusy()))SharedPakDialog(port,true);
        PaddockGap(18);ImGui::PopID();
    }
    (void)panel;
}
void DrawSavesConfirmations(SavesPageState& state) {
    using namespace dkr::runtime::saves;
    using Ask=SavesPageState::Ask;
    static Ask shown=Ask::None;
    if(state.ask==Ask::None)return;
    if(shown!=state.ask){shown=state.ask;ImGui::OpenPopup("##saves-confirm");}
    std::string heading="Confirm save change",detail,confirm="CONFIRM";
    bool replace=true;
    static std::filesystem::path preview_source;static std::string preview_text;
    if(preview_source!=state.restore){preview_source=state.restore;preview_text=state.restore.empty()?std::string{}:shared::describe(state.restore);}
    switch(state.ask) {
        case Ask::Repair: heading="Repair personal save?";detail="Only checksums can be repaired. Damaged progress cannot be reconstructed. An unchanged original is preserved.";confirm="REPAIR & KEEP ORIGINAL";break;
        case Ask::Reset: heading="Reset personal save?";detail="Reset all three slots, unlocks and records. Your current Adventure is backed up first.";confirm="BACK UP & RESET";break;
        case Ask::Restore: heading="Use this as your personal save?";detail=preview_text;confirm="BACK UP & USE THIS SAVE";break;
        case Ask::MaxOut: heading=state.slot<0?"Complete the entire save?":"Complete this Adventure?";detail=state.slot<0?"Complete all three slots and unlock Drumstick, T.T. and Adventure Two. Existing initials and personal records are kept.":"Complete this slot and unlock Drumstick, T.T. and Adventure Two. Other slots and personal records are kept.";confirm="BACK UP & SAVE COMPLETION";break;
        case Ask::UnlockAll: heading="Unlock racers and modes?";detail="Unlock Drumstick, T.T. and Adventure Two. Your Adventure progress and personal best times are kept.";confirm="BACK UP & SAVE UNLOCKS";break;
        case Ask::Discard: heading="Discard editor changes?";detail="Reload your current personal save. Nothing on disk will be replaced.";confirm="DISCARD DRAFT";replace=false;break;
        case Ask::Apply: heading="Apply editor changes?";detail="Save the draft to your personal Adventure. A backup is created first.";confirm="BACK UP & APPLY";break;
        case Ask::Import: heading="Import personal save?";detail=preview_text;confirm="BACK UP & IMPORT";break;
        case Ask::Bundle: heading="Import save bundle?";detail="Replace the personal Adventure and Controller Paks included in this bundle. Originals are preserved in a recoverable transaction.";confirm="BACK UP & IMPORT BUNDLE";break;
        case Ask::Pak: heading="Import Controller Pak?";detail="Replace Controller "+std::to_string(state.slot+1)+"'s Pak. An existing valid image is backed up first.";confirm="BACK UP & IMPORT PAK";break;
        case Ask::RepairUse: heading="Repair and use a copy?";detail="Attempt checksum repair, then replace your personal save with that copy. Corrupt progress cannot be reconstructed. Both originals are retained.";confirm="REPAIR COPY & USE";break;
        case Ask::Host: heading="Use this online Adventure?";detail=preview_text+". The current host Adventure is backed up. Personal saves are unchanged.";confirm="USE FOR HOSTING";break;
        case Ask::DevelopmentCopy: heading="Refresh the next development save?";detail="Copy your saved personal progress into the next offline development session. The existing development save will be preserved before replacement. Your personal save will not be changed. Unapplied editor changes are not included.";confirm="QUEUE DEVELOPMENT COPY";replace=false;break;
        default:break;
    }
    if(!BeginSettingsDialog("##saves-confirm",heading)) {
        if(!ImGui::IsPopupOpen("##saves-confirm")){state.ask=shown=Ask::None;}
        return;
    }
    const auto inner=SettingsDialogInner();
    SavesNote(detail,inner);
    if(!state.restore.empty()&&(state.ask==Ask::Import||state.ask==Ask::Restore||state.ask==Ask::RepairUse||state.ask==Ask::Host))SavesNote(PathUtf8(state.restore),inner,14);
    if(replace&&state.ask!=Ask::Host)SavesNote("Normal play and mods use this personal save. Online and development saves stay separate.",inner);
    if(SavesDraftDirty()&&state.ask!=Ask::Apply&&state.ask!=Ask::MaxOut&&state.ask!=Ask::UnlockAll)SavesNote("Your unapplied editor draft will be discarded after this succeeds.",inner);
    const float height=44;const bool stack=PaddockRaceButtonWidth(confirm.c_str())+PaddockRaceButtonWidth("CANCEL")+12>inner;
    const float cell=stack?inner:(inner-12)*0.5f;
    bool close=false;
    if(SavesRaceButton("CANCEL",cell))close=true;
    if(!stack)ImGui::SameLine(0,12);else PaddockGap(10);
    if(SavesRaceButton(confirm.c_str(),cell,SavesTone::Primary,SharedSaveBusy())) {
        const auto source=state.restore;const auto port=state.slot;
        if(state.ask==Ask::Discard) {SavesForgetDraft();g_shared_draft_revision.clear();}
        else if(state.ask==Ask::MaxOut||state.ask==Ask::UnlockAll||state.ask==Ask::Apply) {
            codec::SaveImage image;std::string error;std::string expected=g_shared_draft_revision;
            bool ready=false;
            if(g_save_builder_image){image=*g_save_builder_image;ready=true;}
            else {expected=shared::revision();ready=load_adventure(image,error);}
            if(ready) {
                if(state.ask==Ask::MaxOut)shared::complete(image,state.slot);
                if(state.ask==Ask::UnlockAll)shared::unlock(image);
                BeginSharedSaveJob([image=std::move(image),expected](std::string& error){return shared::apply(image,expected,error);},"Personal save updated and read-back verified. Existing Dev Menu copies are separate; use Copy Personal Save to Development to refresh one.");
            }else g_save_manager_status=error;
        } else if(state.ask==Ask::Reset)BeginSharedSaveJob([](std::string& error){return shared::reset(error);},"Personal save reset. Previous progress and custom records are kept in Recovery.");
        else if(state.ask==Ask::Bundle)BeginSharedSaveJob([source](std::string& error){return import_bundle(source,error);},"Save bundle imported. Original images are kept in the transaction archive.");
        else if(state.ask==Ask::Pak)BeginSharedSaveJob([source,port](std::string& error){return shared::prepare(g_config_directory,error)&&import_controller_pak(port,source,error);},"Controller Pak imported.");
        else if(state.ask==Ask::Host)BeginSharedSaveJob([source](std::string& error){return shared::import_host(source,error);},"Online Adventure selected. Personal progress was not changed.");
        else if(state.ask==Ask::DevelopmentCopy)BeginSharedSaveJob([](std::string& error){return shared::queue_development_copy(error);},"Personal save snapshot queued for the next offline development launch. Existing development progress will be preserved.");
        else if(state.ask==Ask::Repair)BeginSharedSaveJob([](std::string& error){bool changed;std::filesystem::path original;return shared::prepare(g_config_directory,error)&&repair_adventure_checksums(changed,original,error);},"Checksum repair complete. Check your progress; the original was retained.");
        else {const bool repair=state.ask==Ask::RepairUse;BeginSharedSaveJob([source,repair](std::string& error){return shared::adopt(source,repair,error);},"Personal save replaced. Both the selected original and previous progress were retained.");}
        close=true;
    }
    if(close){ImGui::CloseCurrentPopup();state.ask=shown=Ask::None;state.restore.clear();}
    EndSettingsDialog();(void)height;
}

struct SharedSaveInventory {
    std::vector<dkr::runtime::saves::StoredAdventure> records;
    bool truncated=false;
};
void DrawStoredSaves(SavesPageState& state,float width,bool locked) {
    using namespace dkr::runtime::saves;
    static std::future<SharedSaveInventory> pending;
    static SharedSaveInventory inventory;
    static std::size_t limit=256;
    static std::uint64_t generation=0;
    static auto refresh=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    const bool reload=generation!=g_shared_save_generation||now>=refresh;
    if(!pending.valid()&&(reload||SavesRaceButton("REFRESH SAVES"))) {
        generation=g_shared_save_generation;refresh=now+std::chrono::seconds(10);
        pending=std::async(std::launch::async,[budget=limit]{SharedSaveInventory result;result.records=stored_adventures(budget,&result.truncated);return result;});
    }
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try{
            inventory=pending.get();
            std::map<std::tuple<int,int,std::string>,std::size_t> groups;
            std::vector<StoredAdventure> compact;
            for(auto& record:inventory.records) {
                const auto bucket=record.scope==0?0:record.scope==1?1:3;
                const auto key=std::tuple{bucket,int(record.kind),record.content_hash};
                const auto [it,added]=groups.emplace(key,compact.size());
                if(added)compact.push_back(std::move(record));else ++compact[it->second].copies;
            }
            inventory.records=std::move(compact);
        }catch(const std::exception& e){g_save_manager_status=e.what();}
    }
    if(pending.valid())SavesNote("Reading saved progress...",width);
    SavesNote(state.scope==1?"Online Adventures are separate. The host can continue with different mods without switching saves.":
        "Choose an older save to use as your personal Adventure. Original mod-set copies and backups are never removed automatically.",width);
    bool found=false;
    for(const auto& record:inventory.records) {
        if(state.scope==1?(record.scope!=1):(record.scope==0||record.scope==1))continue;
        found=true;ImGui::PushID(PathUtf8(record.info.path).c_str());
        PaddockBox box(width,{19,19});const auto inner=box.Inner();
        const auto label=record.kind==StoredSaveKind::CustomRecords?"Custom-course records":
            record.kind==StoredSaveKind::ControllerPak?"Controller Pak backup":record.kind==StoredSaveKind::ExperimentalPaks?"Online Controller Paks":
            state.scope==1?"Online Adventure":record.scope==2?"Older mod-set Adventure":"Adventure backup";
        SavesNote(label,inner,17,kSetText);
        SavesNote(record.preview,inner);SavesNote(SavesBackupTime(record.info.path),inner,14);
        if(record.copies>1)SavesNote(std::to_string(record.copies)+" identical copies grouped here. All originals are kept.",inner,14);
        const bool adventure=record.kind==StoredSaveKind::Adventure;
        const bool development=record.info.path.generic_string().find("/development/")!=std::string::npos;
        if(development)SavesNote("Development image - contains tool-session progress, not ordinary play.",inner,14,kSetWarm);
        if(adventure&&!locked) {
            if(SavesRaceButton(record.info.valid?(state.scope==1?"USE FOR HOSTING":"USE AS PERSONAL SAVE"):"REPAIR & USE COPY",0,SavesTone::Primary,SharedSaveBusy()||(!record.info.valid&&record.info.size!=512))) {
                state.restore=record.info.path;state.ask=!record.info.valid?SavesPageState::Ask::RepairUse:state.scope==1?SavesPageState::Ask::Host:SavesPageState::Ask::Restore;
            }
            PaddockGap(8);
        }
        if(SavesRaceButton(record.info.valid?"EXPORT COPY":"EXPORT ORIGINAL IMAGE",0,SavesTone::Plain,SharedSaveBusy())) {
            if(record.info.valid)ExportAdventureWithDialog(record.info.path,record.kind);else SharedRawExportDialog(record.info.path);
        }
        PaddockGap(8);
        if(SavesRaceButton("COPY LOCATION"))SDL_SetClipboardText(PathUtf8(record.info.path).c_str());
        box.End([](ImDrawList* draw,ImVec2 a,ImVec2 b){PaddockPanel(draw,a,b,{PaddockRound(16),PaddockRgb(0x0B2E40,245),PaddockRgb(kSetBorder),1});});
        PaddockGap(14);ImGui::PopID();
    }
    if(!found&&!pending.valid())SavesNote("No saved images found here.",width);
    if(inventory.truncated) {
        SavesNote("More saved images are available. The list is limited to keep scanning responsive.",width);
        if(limit<50000&&SavesRaceButton("LOAD MORE SAVES")){limit=std::min<std::size_t>(limit*4,50000);refresh={};}
    }
}

void DrawSaveManagerPage(float available_width,bool live) {
    using namespace dkr::runtime::saves;
    auto& state=g_saves_page;const auto width=available_width;
    static std::uint64_t generation=0;
    const auto frame=ImGui::GetFrameCount();const bool entered=state.context!=ImGui::GetCurrentContext()||state.last_frame!=frame-1;
    if(entered||generation!=g_shared_save_generation) {
        state.summary.reset();InvalidateSaveManagerViewCache();++g_shared_save_generation;generation=g_shared_save_generation;
        // Preserve a dirty draft, but compare its revision at commit time.
        // A clean draft can be safely reloaded after gameplay/return.
        if(!SavesDraftDirty())SavesForgetDraft();
    }
    state.context=ImGui::GetCurrentContext();state.last_frame=frame;
    PumpSharedSaveJob();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    DrawPageHeading("SAVE MANAGER");PaddockGap(14);
    if(!g_save_manager_status.empty())DrawFriendlyStatus(g_save_manager_status,width,dkr::runtime::copy::Area::Saves);
    const bool lobby=dkr::runtime::netplay::session().active();const bool locked=live||lobby||SharedSaveBusy();
    const int current=state.scope==0?0:state.scope==1?2:1;
    const int picked=SettingsSectionSigns({"YOUR SAVE","BACKUPS & RECOVERY","ONLINE SAVES"},current,width);
    if(picked>=0){state.scope=picked==0?0:picked==2?1:3;state.ask=SavesPageState::Ask::None;state.restore.clear();}
    PaddockGap(20);
    if(live) {
        SavesNote("Save editing is unavailable while the game is running. Return to the launcher first.",width);
        SavesNote("Active save: "+PathUtf8(runtime_save_path()),width,14);
    } else if(state.scope!=0)DrawStoredSaves(state,width,locked);
    else {
        const auto& view=CachedSaveManagerView();const auto& info=view.adventure;
        if(!view.valid){SavesNote("Reading your personal save...",width);ImGui::PopStyleVar();return;}
        const bool valid=info.exists&&info.valid;
        SavesNote(valid?"YOUR PERSONAL ADVENTURE":info.exists?"YOUR SAVE NEEDS RECOVERY":"NO PERSONAL SAVE YET",width,19,valid?kSavesAccent:kSetWarm);
        if(lobby)SavesNote("Leave the lobby before changing saves. You can export your personal save.",width);
        if(SharedSaveBusy())SavesNote("Finishing the save operation. Launch is temporarily held until it is safe.",width);
        if(!locked) {
            if(!info.valid&&info.exists&&info.size==512) {
                if(SavesRaceButton("BACK UP & REPAIR CHECKSUMS",0,SavesTone::Primary))state.ask=SavesPageState::Ask::Repair;
                PaddockGap(12);
            }
            const auto section=SettingsSectionSigns({"OVERVIEW","EDIT PROGRESS","TRANSFER & PAKS"},state.section,width);
            if(section>=0)state.section=section;
            PaddockGap(18);
            if(state.section==1)DrawSavesEditor(state,width,width,valid,info.exists);
            else if(state.section==2)DrawSavesTransfer(width,width,view);
            else DrawSavesOverview(state,width,width,view,valid);
        } else if(!SharedSaveBusy()&&valid&&SavesRaceButton("EXPORT PERSONAL COPY"))ExportAdventureWithDialog();
    }
    if(!g_shared_save_import.path.empty()&&!locked) {
        state.restore=std::move(g_shared_save_import.path);state.slot=g_shared_save_import.port;
        state.ask=g_shared_save_import.kind==1?SavesPageState::Ask::Bundle:g_shared_save_import.kind==2?SavesPageState::Ask::Pak:SavesPageState::Ask::Import;
        g_shared_save_import={};
    }
    ImGui::PopStyleVar();
    if(!locked)DrawSavesConfirmations(state);
}
