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
    enum class Ask { None, Repair, Reset, Restore, MaxOut, UnlockAll, Discard, Apply } ask = Ask::None;
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
            codec::normalise_editable_fields(image);
            g_save_builder_image = image;
            state.original = image;
        } else {
            g_save_manager_status = error;
            return;
        }
    }
    if (!state.original) state.original = *g_save_builder_image;
    SavesNote("Edit a slot or shared unlocks. Changes stay in your draft until you apply them.", width);
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

void DrawSavesOverview(SavesPageState& state, float width, float panel, const SaveManagerViewCache& view,
                       bool editable) {
    using namespace dkr::runtime::saves;
    const auto& info = view.adventure;
    const char* reason = !info.exists ? "Create or import a save first." : "Restore or repair the save first.";
    PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), "Save file", width);
    PaddockGap(13.0F);
    SavesNote("These actions include all three adventure slots, shared unlocks and personal records.", width);
    {
        const float backup = PaddockRaceButtonWidth("BACK UP SAVE");
        const float export_width = PaddockRaceButtonWidth("EXPORT SAVE");
        const float import_width = PaddockRaceButtonWidth("IMPORT SAVE");
        if (SavesRaceButton("BACK UP SAVE", backup, SavesTone::Primary, !editable, reason)) {
            std::filesystem::path created;
            std::string error;
            if (backup_adventure(created, error)) {
                InvalidateSaveManagerViewCache();
                g_save_manager_status = "Backup created. You can restore it below.";
            } else {
                g_save_manager_status = error;
            }
        }
        const bool one_line = backup + export_width + import_width + 20.0F <= width;
        if (one_line) ImGui::SameLine(0.0F, 10.0F); else PaddockGap(10.0F);
        if (SavesRaceButton("EXPORT SAVE", export_width, SavesTone::Plain, !editable, reason)) {
            ExportAdventureWithDialog();
        }
        if (one_line) ImGui::SameLine(0.0F, 10.0F); else PaddockGap(10.0F);
        if (SavesRaceButton("IMPORT SAVE", import_width)) ImportAdventureWithDialog();
        PaddockGap(18.0F);
    }
    if (!editable) SavesNote(std::string(reason) + " Backup and export require a valid save.", width);
    if (!info.exists) {
        if (SettingsButton("Create fresh save")) state.ask = SavesPageState::Ask::Reset;
    } else if (editable) {
        // Adventure slots: who is racing in each and how far they got.
        const auto now = std::chrono::steady_clock::now();
        if (!state.summary || now >= state.summary_at) {
            codec::SaveImage image{};
            std::string error;
            if (load_adventure(image, error)) state.summary = image;
            state.summary_at = now + std::chrono::seconds(1);
        }
        if (state.summary) {
            SavesSeparator("Adventure slots", width);
            PaddockGap(10.0F);
            const bool stacked = panel <= 540.0F;
            const float column = stacked ? width : std::floor((width - 32.0F) / 3.0F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            float bottom = at.y;
            for (std::size_t index = 0; index < codec::kAdventureSlotCount; ++index) {
                const auto& slot = state.summary->slots[index];
                const ImVec2 cell{stacked ? at.x : at.x + (column + 16.0F) * static_cast<float>(index),
                                  stacked ? bottom + (index > 0U ? 16.0F : 0.0F) : at.y};
                ImGui::SetCursorScreenPos({cell.x + 16.0F, cell.y});
                ImGui::BeginGroup();
                const float inner = column - 16.0F;
                PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText),
                            "Slot " + std::to_string(index + 1U), inner);
                PaddockGap(6.0F);
                const std::string name = codec::sanitise_name(slot.name);
                const bool blank = name.find_first_not_of(' ') == std::string::npos;
                PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetMuted),
                            blank ? std::string("No racer initials") : name, inner);
                PaddockGap(8.0F);
                PaddockText(PaddockReading(16.0F, true, 1.5F), PaddockRgb(kSetText),
                            std::to_string(slot.balloons[0]) + " / 47 balloons", inner);
                PaddockGap(8.0F);
                ImGui::PushID(static_cast<int>(index));
                const std::string label = "Edit slot " + std::to_string(index + 1U);
                if (SettingsButton(label.c_str(), inner)) {
                    state.section = 1;
                    state.tab = static_cast<int>(index);
                }
                ImGui::PopID();
                ImGui::EndGroup();
                const float cell_bottom = ImGui::GetItemRectMax().y;
                ImGui::GetWindowDrawList()->AddRectFilled({cell.x, cell.y}, {cell.x + 2.0F, cell_bottom},
                                                          PaddockCol(kSetBorder));
                bottom = std::max(bottom, cell_bottom);
            }
            ImGui::SetCursorScreenPos({at.x, bottom});
            ImGui::Dummy({width, 0.0F});
        }
    }

    SavesSeparator("Backups", width);
    SavesNote("Restore earlier progress. A safety backup is also created before applying edits or resetting a save.",
              width);
    const auto& backups = view.adventure_backups;
    if (backups.empty()) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({at.x + 20.0F, at.y + 16.0F});
        ImGui::BeginGroup();
        PaddockText(PaddockReading(16.0F, true, 1.5F), PaddockRgb(kSetText), "No backups yet", width - 38.0F);
        PaddockGap(13.0F);
        PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetMuted),
                    "Use Back up save to keep a copy of your current progress here.", width - 38.0F);
        ImGui::EndGroup();
        const float bottom = ImGui::GetItemRectMax().y + 16.0F;
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + 2.0F, bottom}, PaddockCol(kSetBorder));
        ImGui::SetCursorScreenPos({at.x, bottom});
        ImGui::Dummy({width, 0.0F});
    } else {
        const std::size_t shown = state.all_backups ? backups.size() : std::min<std::size_t>(backups.size(), 6U);
        for (std::size_t index = 0; index < shown; ++index) {
            ImGui::PushID(static_cast<int>(index));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float restore_width = SettingsButtonWidth("Restore");
            ImGui::SetCursorScreenPos({at.x, at.y + 14.0F});
            ImGui::BeginGroup();
            PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetText), SavesBackupTime(backups[index]),
                        width - restore_width - 18.0F);
            PaddockText(PaddockReading(16.0F, false, 1.5F), PaddockRgb(kSetMuted),
                        index == 0U ? "Latest backup" : "Earlier backup", width - restore_width - 18.0F);
            ImGui::EndGroup();
            const float text_bottom = ImGui::GetItemRectMax().y;
            const float row_height = std::max(text_bottom - (at.y + 14.0F), 44.0F);
            ImGui::SetCursorScreenPos({at.x + width - restore_width, at.y + 14.0F + (row_height - 44.0F) * 0.5F});
            if (SettingsButton("Restore", restore_width)) {
                state.ask = SavesPageState::Ask::Restore;
                state.restore = backups[index];
            }
            const float bottom = at.y + 14.0F + row_height + 14.0F;
            ImGui::GetWindowDrawList()->AddRectFilled({at.x, bottom - 1.0F}, {at.x + width, bottom},
                                                      PaddockCol(kSetBorder));
            ImGui::SetCursorScreenPos({at.x, bottom});
            ImGui::Dummy({width, 0.0F});
            ImGui::PopID();
        }
        if (backups.size() > 6U) {
            PaddockGap(12.0F);
            const std::string label = state.all_backups ? std::string("Show recent backups")
                                                        : "Show all " + std::to_string(backups.size()) + " backups";
            if (SavesRaceButton(label.c_str())) state.all_backups = !state.all_backups;
        }
    }

    if (info.exists) {
        // Start over (.saves-reset).
        PaddockGap(28.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
        PaddockGap(1.0F + 20.0F);
        const float reset_width = SettingsButtonWidth("Reset save");
        const ImVec2 row = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), "Start over", width - reset_width - 20.0F);
        PaddockGap(6.0F);
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted),
                    "Reset all three slots, unlocks and records. A backup is kept.", width - reset_width - 20.0F);
        ImGui::EndGroup();
        const float text_bottom = ImGui::GetItemRectMax().y;
        ImGui::SetCursorScreenPos({row.x + width - reset_width, row.y + (text_bottom - row.y - 44.0F) * 0.5F});
        if (SettingsButton("Reset save", reset_width)) state.ask = SavesPageState::Ask::Reset;
        ImGui::SetCursorScreenPos({row.x, std::max(text_bottom, row.y + 44.0F)});
        ImGui::Dummy({width, 0.0F});
    }
}

// ------------------------------------------------------------ transfer

void DrawSavesTransfer(float width, float panel, const SaveManagerViewCache& view) {
    SavesSeparator("Move all your saves", width, false);
    SavesNote("Transfer Adventure progress and all available Controller Paks together between Windows and Steam Deck.",
              width);
    const bool stacked = panel <= 540.0F;
    const float half = stacked ? width : std::floor((width - 10.0F) * 0.5F);
    if (SavesRaceButton("EXPORT COMPLETE GARAGE", half)) ExportSaveBundleWithDialog();
    if (!stacked) ImGui::SameLine(0.0F, 10.0F); else PaddockGap(10.0F);
    if (SavesRaceButton("IMPORT COMPLETE GARAGE", half)) ImportSaveBundleWithDialog();
    PaddockGap(18.0F);
    SavesSeparator("Virtual Controller Paks", width, false);
    SavesNote("Four virtual memory cards. Each card is included in a complete garage export when available.", width);
    const float column = stacked ? width : std::floor((width - 12.0F) * 0.5F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    float row_top = at.y;
    float row_bottom = at.y;
    for (int channel = 0; channel < dkr::runtime::saves::kControllerPakCount; ++channel) {
        const auto& pak = view.controller_paks[static_cast<std::size_t>(channel)];
        const int slot = stacked ? 0 : channel % 2;
        if (slot == 0 && channel > 0) row_top = row_bottom + 12.0F;
        ImGui::SetCursorScreenPos({at.x + (column + 12.0F) * static_cast<float>(slot), row_top});
        PaddockBox box(column, {19.0F, 19.0F});
        const float inner = box.Inner();
        PaddockText(PaddockSign(19.0F, 1.45F, 0.0F), PaddockRgb(kSetText),
                    "CONTROLLER " + std::to_string(channel + 1), inner);
        PaddockGap(13.0F);
        if (!pak.exists) {
            PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted), "Not created yet", inner);
        } else {
            PaddockText(PaddockSign(19.0F, 1.45F, 0.0F), PaddockRgb(pak.valid ? kSavesAccent : kSavesError),
                        pak.valid ? "PAK READY" : "RECOVERY NEEDED", inner);
        }
        PaddockGap(13.0F);
        PaddockText(PaddockReading(14.0F, false, 1.4F), PaddockRgb(kSetMuted), PathUtf8(pak.path), inner);
        const float height = box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(16.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 1.0F});
        });
        row_bottom = slot == 0 ? row_top + height : std::max(row_bottom, row_top + height);
    }
    ImGui::SetCursorScreenPos({at.x, row_bottom});
    ImGui::Dummy({width, 0.0F});
}

// ------------------------------------------------------------ confirmations

void DrawSavesConfirmations(SavesPageState& state) {
    using namespace dkr::runtime::saves;
    using Ask = SavesPageState::Ask;
    constexpr const char* kPopup = "##saves-confirm";
    static Ask shown = Ask::None;
    if (state.ask != Ask::None && shown != state.ask) {
        shown = state.ask;
        ImGui::OpenPopup(kPopup);
    }
    const bool exists = CachedSaveManagerView().adventure.exists;
    const bool dirty = SavesDraftDirty();
    std::string heading;
    std::vector<std::string> paragraphs;
    const char* confirm = "OK";
    bool destructive = false;
    switch (state.ask) {
        case Ask::Repair:
            heading = "Repair this save?";
            paragraphs = {"Back up the save, then rebuild its checksums without changing save data?",
                          "The exact original is kept in Backups."};
            confirm = "BACK UP & REPAIR";
            break;
        case Ask::Reset:
            heading = exists ? "Start a fresh adventure?" : "Create an Adventure save?";
            paragraphs = {exists ? "Reset all three slots, unlocks and records? Your current save will be backed up first."
                                 : "Create an empty save with three adventure slots?",
                          dirty ? "Your unapplied editor changes will be discarded."
                                : "You can edit the new save from Edit progress."};
            confirm = exists ? "BACK UP & RESET" : "CREATE SAVE";
            destructive = exists;
            break;
        case Ask::Restore:
            heading = "Restore this backup?";
            paragraphs = {PathUtf8(state.restore.filename()),
                          "Your current save will be backed up before it is replaced.",
                          dirty ? "Your unapplied editor changes will be discarded."
                                : "You can restore the replaced save from this list."};
            confirm = "RESTORE BACKUP";
            destructive = true;
            break;
        case Ask::MaxOut:
            heading = "Max out this adventure?";
            paragraphs = {"Set all balloons, amulets, keys and course progress in this slot to complete?",
                          "This changes the draft. Use Apply changes to save it."};
            confirm = "MAX OUT";
            break;
        case Ask::UnlockAll:
            heading = "Unlock all racers and modes?";
            paragraphs = {"Enable Adventure Two, Drumstick and all T.T. victories in this draft?",
                          "Use Apply changes to save these unlocks."};
            confirm = "UNLOCK ALL";
            break;
        case Ask::Discard:
            heading = "Discard unapplied changes?";
            paragraphs = {"Reload the current save and discard edits to all three slots and shared unlocks?"};
            confirm = "DISCARD";
            destructive = true;
            break;
        case Ask::Apply:
            heading = "Apply changes?";
            paragraphs = {"Replace the current save with this draft? A backup of the current save will be created first.",
                          "Checksums are rebuilt and verified before the save is swapped in."};
            confirm = "BACK UP & APPLY";
            break;
        case Ask::None:
        default:
            break;
    }
    if (!BeginSettingsDialog(kPopup, heading)) {
        if (shown != Ask::None && !ImGui::IsPopupOpen(kPopup)) {
            shown = Ask::None;
            state.ask = Ask::None;
        }
        return;
    }
    const float inner = SettingsDialogInner();
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (index > 0U) PaddockGap(13.0F);
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(index == 0U ? kSetText : kSetMuted),
                    paragraphs[index], inner);
    }
    PaddockGap(24.0F);
    const float half = std::floor((inner - 12.0F) * 0.5F);
    bool close = false;
    if (SavesRaceButton("CANCEL", half)) close = true;
    ImGui::SameLine(0.0F, 12.0F);
    if (SavesRaceButton(confirm, half, destructive ? SavesTone::Danger : SavesTone::Primary)) {
        std::string error;
        switch (state.ask) {
            case Ask::Repair: {
                bool changed = false;
                std::filesystem::path backup;
                if (repair_adventure_checksums(changed, backup, error)) {
                    InvalidateSaveManagerViewCache();
                    SavesForgetDraft();
                    g_save_manager_status = changed
                        ? "Checksums repaired without changing save data. The original is available in Backups."
                        : "The Adventure save checksums are already valid.";
                } else {
                    g_save_manager_status = error;
                }
                break;
            }
            case Ask::Reset:
                if (reset_adventure(error)) {
                    InvalidateSaveManagerViewCache();
                    SavesForgetDraft();
                    g_save_manager_status = exists
                        ? "Fresh save created. Previous progress is available in Backups."
                        : "Fresh save created. Open Edit progress to customize it.";
                } else {
                    g_save_manager_status = error;
                }
                break;
            case Ask::Restore:
                if (import_adventure(state.restore, error)) {
                    InvalidateSaveManagerViewCache();
                    SavesForgetDraft();
                    g_save_manager_status = "Backup restored. Any replaced save was backed up too.";
                } else {
                    g_save_manager_status = error;
                }
                break;
            case Ask::MaxOut:
                if (g_save_builder_image) {
                    auto& slot = g_save_builder_image->slots[static_cast<std::size_t>(state.slot)];
                    std::fill(slot.course_status.begin(), slot.course_status.end(), 3U);
                    slot.taj_flags = 0x3F;
                    slot.trophies = 0x3FF;
                    slot.bosses = 0xFFF;
                    slot.balloons = {47, 8, 8, 8, 8, 8};
                    slot.tt_amulet = 4;
                    slot.wizpig_amulet = 4;
                    slot.world_flags.fill(0xFFFF);
                    slot.keys = 0x1E;
                }
                break;
            case Ask::UnlockAll:
                if (g_save_builder_image) {
                    g_save_builder_image->settings.adventure_two = true;
                    g_save_builder_image->settings.drumstick = true;
                    g_save_builder_image->settings.tt_trials.fill(true);
                }
                break;
            case Ask::Discard:
                if (state.original) g_save_builder_image = state.original;
                break;
            case Ask::Apply:
                if (g_save_builder_image && commit_adventure(*g_save_builder_image, error)) {
                    InvalidateSaveManagerViewCache();
                    state.original = g_save_builder_image;
                    state.summary.reset();
                    g_save_manager_status = "Changes applied. Checksums verified; previous progress is available in Backups.";
                } else if (!error.empty()) {
                    g_save_manager_status = error;
                }
                break;
            case Ask::None:
            default:
                break;
        }
        close = true;
    }
    if (close) {
        ImGui::CloseCurrentPopup();
        state.ask = Ask::None;
        shown = Ask::None;
    }
    EndSettingsDialog();
}

// ------------------------------------------------------------ page

void DrawSaveManagerPage(float available_width, bool live) {
    SavesPageState& state = g_saves_page;
    const int frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() || state.last_frame != frame - 1;
    if (entered) {
        state.context = ImGui::GetCurrentContext();
        state.summary.reset();
    }
    state.last_frame = frame;
    const float width = available_width;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    DrawPageHeading("SAVE MANAGER");
    PaddockGap(13.0F);
    PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted),
                "Keep your progress safe, edit adventures and move saves between devices.", width);
    PaddockGap(22.0F);

    if (live) {
        PaddockBox box(width, {19.0F, 19.0F});
        const float inner = box.Inner();
        PaddockText(PaddockSign(19.0F, 1.45F, 0.0F), PaddockRgb(kSetText), "PIT LANE SAFETY LOCK", inner);
        PaddockGap(12.0F);
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted),
                    "Save import, restore and reset are available before the game starts. Close the game and use "
                    "Save Manager so DKR cannot write to the same save or Controller Pak during a transfer.",
                    inner);
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(16.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 1.0F});
        });
        ImGui::PopStyleVar();
        return;
    }

    if (!g_save_manager_status.empty()) {
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetWarm), g_save_manager_status, width);
        PaddockGap(13.0F);
    }
    const SaveManagerViewCache& view = CachedSaveManagerView();
    const auto& info = view.adventure;
    const bool editable = info.exists && info.valid && info.size == dkr::runtime::saves::codec::kImageSize;
    if (!editable && g_save_builder_image) SavesForgetDraft();

    // The current save (.saves-card.saves-adventure).
    {
        std::string status = "Ready to use";
        std::string description = "Three adventure slots, shared unlocks and personal records.";
        unsigned status_colour = kSavesAccent;
        if (!info.exists) {
            status = "No Adventure save yet";
            description = "The game creates a save after your first adventure. You can also import a save or "
                          "create a fresh one below.";
            status_colour = kSetText;
        } else if (info.size != dkr::runtime::saves::codec::kImageSize) {
            status = "Save size not recognized";
            description = "This save is " + std::to_string(info.size) +
                          " bytes; 512 bytes are required. Import a working save or restore a backup below.";
            status_colour = kSavesError;
        } else if (!info.valid) {
            status = "Save needs repair";
            description = "The save failed its integrity check. Back up and repair it, or restore a working backup.";
            status_colour = kSavesError;
        }
        PaddockBox box(width, {19.0F, 19.0F});
        const float inner = box.Inner();
        const PaddockType badge = PaddockReading(15.0F, true, 1.4F);
        const float badge_width = PaddockMeasure(badge, status);
        const ImVec2 head = ImGui::GetCursorScreenPos();
        PaddockText(PaddockSign(19.0F, 1.2F, 0.0F), PaddockRgb(kSetText), "ADVENTURE SAVE",
                    inner - badge_width - 10.0F);
        PaddockDrawRun(ImGui::GetWindowDrawList(), badge, {head.x + inner - badge_width, head.y},
                       PaddockCol(status_colour), status.data(), status.data() + status.size());
        PaddockGap(12.0F);
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetMuted), description, inner);
        PaddockGap(13.0F);
        PaddockText(PaddockReading(14.0F, false, 1.4F), PaddockRgb(kSetMuted), PathUtf8(info.path), inner);
        if (info.exists && info.size == dkr::runtime::saves::codec::kImageSize && !info.valid) {
            PaddockGap(13.0F);
            if (SavesRaceButton("BACK UP & REPAIR SAVE", 0.0F, SavesTone::Primary)) {
                state.ask = SavesPageState::Ask::Repair;
            }
        }
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(16.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 1.0F});
        });
    }
    PaddockGap(24.0F);
    const std::string edit_label = std::string("EDIT PROGRESS") + (SavesDraftDirty() ? " *" : "") + "##edit";
    const int picked = SettingsSectionSigns({"OVERVIEW##overview", edit_label.c_str(), "TRANSFER & PAKS##transfer"},
                                            state.section, width);
    if (picked >= 0) state.section = picked;
    PaddockGap(24.0F);
    if (state.section == 1) {
        DrawSavesEditor(state, width, available_width, editable, info.exists);
    } else if (state.section == 2) {
        DrawSavesTransfer(width, available_width, view);
    } else {
        DrawSavesOverview(state, width, available_width, view, editable);
    }
    ImGui::PopStyleVar();
    DrawSavesConfirmations(state);
}
