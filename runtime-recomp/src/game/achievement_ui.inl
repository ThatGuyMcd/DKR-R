namespace ach = dkr::runtime::achievements;
int g_achievement_filter = 0, g_achievement_sort = 0;
std::string g_achievement_category, g_achievement_detail;
std::array<char, 161> g_achievement_search{};
bool g_achievement_reset_scroll = false;
bool g_achievement_settings = false, g_achievement_reset = false, g_achievement_recognise = false;
std::string g_achievement_focus_row;
ach::Toast g_achievement_toast;
double g_achievement_toast_started = 0;
SDL_AudioDeviceID g_achievement_audio = 0;

void AchievementChime(float gain = 1.F) {
    if (gain <= 0.F)
        return;
    if (!g_achievement_audio) {
        SDL_AudioSpec wanted{}, actual{};
        wanted.freq = 48000;
        wanted.format = AUDIO_F32SYS;
        wanted.channels = 2;
        wanted.samples = 512;
        g_achievement_audio = SDL_OpenAudioDevice(nullptr, 0, &wanted, &actual, 0);
        if (!g_achievement_audio)
            return;
        SDL_PauseAudioDevice(g_achievement_audio, 0);
    }
    std::vector<float> samples(48000 * 2 * 3 / 5);
    const float volume = gain * .10F * dkr::runtime::audio::sound_effects_volume();
    for (unsigned i = 0; i < samples.size() / 2; ++i) {
        const float t = float(i) / 48000.F;
        const float envelope = std::min(t * 90.F, 1.F) * std::exp(-6.F * t);
        float value = 0;
        for (const auto tone : std::array<float, 3>{523.25F, 659.25F, 783.99F})
            value += std::sin(t * 6.2831853F * tone);
        samples[2 * i] = samples[2 * i + 1] = value * envelope * volume / 3.F;
    }
    SDL_ClearQueuedAudio(g_achievement_audio);
    SDL_QueueAudio(g_achievement_audio, samples.data(), Uint32(samples.size() * sizeof(float)));
}
void ReleaseAchievementAudio() {
    if (g_achievement_audio) {
        SDL_CloseAudioDevice(g_achievement_audio);
        g_achievement_audio = 0;
    }
}
const char* AchievementBadge(const ach::Definition& d) {
    switch (d.kind) {
    case ach::Kind::TimeTrial:
        return "TT";
    case ach::Kind::Developer:
        return "DEV";
    case ach::Kind::Silver:
        return "SC";
    case ach::Kind::Driver:
        return "5";
    case ach::Kind::Key:
        return "K";
    case ach::Kind::Trophy:
        return "1";
    case ach::Kind::Boss:
        return "W";
    case ach::Kind::Amulet:
        return "4";
    case ach::Kind::Power:
        return "+";
    default:
        return "B";
    }
}
void DrawAchievementBadge(ImVec2 at, float size, const char* text, bool unlocked) {
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 center{at.x + size * .5F, at.y + size * .5F};
    draw->AddCircleFilled(center, size * .48F, PaddockRgb(unlocked ? 0x0CA68F : 0x263E4C));
    draw->AddCircle(center, size * .38F, PaddockRgb(unlocked ? 0xFFCF68 : 0x718990), 0, 1.5F);
    const auto type = PaddockReading(size * .27F, true, 1.F);
    const auto width = PaddockMeasure(type, text);
    draw->AddText(type.font, type.size, {center.x - width * .5F, center.y - type.size * .5F},
                  PaddockRgb(unlocked ? 0xFFF6DA : 0xB0C9CC), text);
}
bool AchievementToastVisible() {
    return !g_achievement_toast.id.empty() || ach::notification_active() || !ach::view()->pinned.empty();
}
void DrawPinnedAchievement() {
    const auto state = ach::view();
    if (state->pinned.empty())
        return;
    const auto& definitions = ach::catalogue();
    const auto found =
        std::find_if(definitions.begin(), definitions.end(), [&](auto& d) { return d.id == state->pinned; });
    if (found == definitions.end())
        return;
    const auto& entry = state->entries[std::size_t(found - definitions.begin())];
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(330.F, viewport->WorkSize.x - 32.F);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 16.F, viewport->WorkPos.y + 16.F});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.F, 12.F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PaddockRgb(0x0A202D, 238));
    ImGui::Begin("Pinned achievement##dkr", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    PaddockText(PaddockReading(15.F, true), PaddockRgb(0xFFCB66), found->title, width - 28.F);
    if (found->target > 1 && found->kind != ach::Kind::TimeTrial && found->kind != ach::Kind::Developer) {
        const auto progress = std::to_string(entry.progress) + " / " + std::to_string(found->target);
        PaddockText(PaddockReading(14.F), PaddockRgb(0xFFF6DA), progress, width - 28.F);
    } else
        PaddockText(PaddockReading(13.F), PaddockRgb(0xB0C9CC), found->description, width - 28.F);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
void DrawAchievementToast() {
    const auto state = ach::view();
    const auto& p = state->preferences;
    if (!p.notifications) {
        g_achievement_toast = {};
        ach::Toast discard;
        while (ach::next_toast(discard)) {
        }
        return;
    }
    // Launcher and in-game overlays use separate ImGui contexts. A shared
    // monotonic clock keeps a toast's lifetime correct across that handoff.
    const auto time = double(SDL_GetTicks64()) * .001;
    if (!g_achievement_toast.id.empty() && time - g_achievement_toast_started >= p.duration)
        g_achievement_toast = {};
    if (g_achievement_toast.id.empty()) {
        if (!ach::next_toast(g_achievement_toast))
            return;
        g_achievement_toast_started = time;
        if (p.sound)
            AchievementChime();
    }
    const auto* viewport = ImGui::GetMainViewport();
    const auto origin = viewport->WorkPos;
    const auto extent = viewport->WorkSize;
    const float scale = p.scale, width = std::min(420.F * scale, extent.x - 32.F), padding = 15.F * scale;
    const auto title = PaddockReading(17.F * scale, true, 1.25F);
    const auto label = PaddockReading(13.F * scale, false, 1.1F);
    const float text_width = std::max(50.F, width - 88.F * scale);
    const auto title_size =
        title.font->CalcTextSizeA(title.size, FLT_MAX, text_width, g_achievement_toast.title.c_str());
    const float height = std::max(80.F * scale, title_size.y + label.size + padding * 2 + 5.F);
    float x = p.position == 0 ? origin.x + (extent.x - width) * .5F : origin.x + extent.x - width - 20.F;
    float y = p.position == 2 ? origin.y + 20.F : origin.y + extent.y - height - 20.F;
    const float elapsed = float(time - g_achievement_toast_started);
    if (!p.reduced_motion)
        y += (p.position == 2 ? -1.F : 1.F) * (1.F - PaddockEase(std::min(elapsed / .22F, 1.F))) * 16.F;
    ImGui::SetNextWindowPos({x, y});
    ImGui::SetNextWindowSize({width, height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, height * .5F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PaddockRgb(0x0A202D, 244));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x2C6577));
    ImGui::Begin("Achievement unlocked##dkr-toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    DrawAchievementBadge({x + padding, y + (height - 50.F * scale) * .5F}, 50.F * scale, "!", true);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 text{x + padding + 64.F * scale, y + padding};
    draw->AddText(label.font, label.size, text, PaddockRgb(0xFFCB66), "ACHIEVEMENT UNLOCKED");
    draw->AddText(title.font, title.size, {text.x, text.y + label.size + 6.F}, PaddockRgb(0xFFF6DA),
                  g_achievement_toast.title.c_str(), nullptr, text_width);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
void AchievementFileDialog(bool importing) {
    StartDialogJob([importing]() -> std::function<void()> {
        // The established native folder picker keeps Android/Linux/Windows
        // path handling outside the ImGui frame. Backups have one fixed name.
        std::filesystem::path folder;
        std::string error;
        const bool chosen = PickFolder(folder, error);
        return [folder, chosen, error, importing] {
            if (chosen) {
                const auto path = folder / "DKR-R-Achievements.json";
                if (importing)
                    ach::import_progress(path);
                else
                    ach::export_progress(path);
            } else if (!error.empty())
                g_support_action_status = error;
        };
    });
}
void DrawAchievementPreferences(float width, const ach::View& state) {
    if (SettingsButton("Back to achievements")) {
        g_achievement_settings = false;
        return;
    }
    PaddockGap(14.F);
    constexpr float gap = 12.F;
    const bool side_by_side = width >= 640.F;
    const float column = side_by_side ? (width - gap) * .5F : width;
    const auto top = ImGui::GetCursorScreenPos();
    const auto paint = [](ImDrawList* draw, ImVec2 min, ImVec2 max) {
        PaddockPanel(draw, min, max, {PaddockRound(10.F), PaddockRgb(0x0C293A), PaddockRgb(kSetBorder), 1.F});
    };

    PaddockBox notifications(column, {14.F, 14.F});
    float inner = notifications.Inner();
    DetailHeading("Notifications", inner);
    auto p = state.preferences;
    bool changed = false;
    changed |= SettingsCheck("##achievement-popups", "Show unlock notifications", &p.notifications, inner);
    changed |= SettingsCheck("##achievement-chime", "Play unlock sound", &p.sound, inner);
    changed |= SettingsCheck("##achievement-motion", "Reduce notification motion", &p.reduced_motion, inner);
    changed |= SettingsDropdown("##achievement-position", "Position", &p.position,
                                {"Bottom centre", "Bottom right", "Top right"}, inner);
    PaddockGap(8.F);
    int scale = static_cast<int>(std::lround(p.scale * 10.F));
    if (SettingsRange("##achievement-size", "Size", &scale, 7, 16, inner, [](int value) {
            return SettingsRangeValue{std::to_string(value / 10) + "." + std::to_string(value % 10), "x"};
        })) {
        p.scale = float(scale) / 10.F;
        changed = true;
    }
    PaddockGap(8.F);
    int duration = static_cast<int>(std::lround(p.duration));
    if (SettingsRange("##achievement-duration", "Duration", &duration, 2, 10, inner,
                      [](int value) { return SettingsRangeValue{std::to_string(value), " seconds"}; })) {
        p.duration = float(duration);
        changed = true;
    }
    if (changed)
        ach::preferences(p);
    PaddockGap(8.F);
    if (SettingsButton("Preview notification"))
        ach::preview();
    const float left_height = notifications.End(paint);

    ImGui::SetCursorScreenPos(side_by_side ? ImVec2{top.x + column + gap, top.y}
                                           : ImVec2{top.x, top.y + left_height + gap});
    PaddockBox collection(column, {14.F, 14.F});
    inner = collection.Inner();
    DetailHeading("Your collection", inner);
    DetailText("Achievements stay separate from your game saves. Starting another adventure will not erase them.",
               inner);
    DumpActionRow files(inner);
    if (files.Button("Export backup"))
        AchievementFileDialog(false);
    if (files.Button("Import backup"))
        AchievementFileDialog(true);
    if (files.Button("Recognise existing progress"))
        g_achievement_recognise = true;
    PaddockGap(16.F);
    if (SettingsButton("Reset achievement collection"))
        g_achievement_reset = true;
    collection.End(paint);
    ImGui::SetCursorScreenPos({top.x, std::max(top.y + left_height, ImGui::GetItemRectMax().y)});
    ImGui::Dummy({width, 0.F});
    PaddockGap(18);
    DetailText(
        "Challenge objectives inspired by Delmaru's Diddy Kong Racing achievement set. Local DKR-R implementation, wording and badge designs. Not connected to RetroAchievements.",
        width, kSetMuted);
}
void AchievementProgressBar(float width, float progress) {
    const auto at = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const auto end = ImVec2{at.x + width, at.y + 6.F};
    PaddockFill(draw, at, end, PaddockRound(3.F), PaddockCol(0x071B28));
    const float filled = width * std::clamp(progress, 0.F, 1.F);
    if (filled > 0.F)
        PaddockFill(draw, at, {at.x + filled, end.y}, PaddockRound(3.F), PaddockCol(kSetWarm));
    ImGui::Dummy({width, 6.F});
}

bool AchievementHasProgress(const ach::Definition& badge) {
    return badge.target > 1 && badge.kind != ach::Kind::TimeTrial && badge.kind != ach::Kind::Developer;
}

bool DrawAchievementGalleryCard(const ach::Definition& badge, const ach::Entry& entry, float width, float height,
                                bool pinned) {
    ImGui::PushID(badge.id.c_str());
    if (g_achievement_focus_row == badge.id) {
        ImGui::SetKeyboardFocusHere();
        g_achievement_focus_row.clear();
    }
    const auto press = PaddockBeginPress("##badge", {width, height});
    auto* draw = ImGui::GetWindowDrawList();
    const bool active = ImGui::IsItemFocused() || press.hovered;
    PaddockPanel(draw, press.min, press.max,
                 {PaddockRound(10.F), PaddockRgb(active ? 0x123C4E : 0x0C293A),
                  PaddockRgb(active ? kSetWarm : kSetBorder), 1.F});
    constexpr float pad = 11.F;
    DrawAchievementBadge({press.min.x + pad, press.min.y + 8.F}, 36.F, AchievementBadge(badge), entry.unlocked);
    const auto title = PaddockReading(16.F, true, 1.17F);
    const auto meta = PaddockReading(13.F, false, 1.24F);
    const float category_width = std::min(PaddockMeasure(meta, badge.category), width - 64.F);
    PaddockTextAt(draw, meta, {press.max.x - pad - category_width, press.min.y + 14.F}, category_width + 0.5F,
                  badge.category, {PaddockRgb(kSetMuted)});
    PaddockTextAt(draw, title, {press.min.x + pad, press.min.y + 50.F}, width - 2.F * pad, badge.title,
                  {PaddockRgb(kSetText)});
    const bool progress = AchievementHasProgress(badge) && !entry.unlocked;
    const std::string status =
        entry.unlocked ? (entry.restored ? "Recognised" : "Unlocked")
        : pinned       ? "Pinned"
        : progress ? std::to_string(std::clamp(entry.progress, 0, badge.target)) + " / " + std::to_string(badge.target)
                   : "Locked";
    PaddockTextAt(draw, meta, {press.min.x + 12.F, press.max.y - (progress ? 34.F : 25.F)}, width - 24.F, status,
                  {PaddockRgb(entry.unlocked ? 0x42D6BC
                              : pinned       ? kSetWarm
                                             : kSetMuted)});
    if (progress) {
        const float y = press.max.y - 12.F;
        const float fraction = std::clamp(float(entry.progress) / float(badge.target), 0.F, 1.F);
        PaddockFill(draw, {press.min.x + 12.F, y}, {press.max.x - 12.F, y + 4.F}, PaddockRound(2.F),
                    PaddockCol(0x071B28));
        if (fraction > 0.F)
            PaddockFill(draw, {press.min.x + 12.F, y}, {press.min.x + 12.F + (width - 24.F) * fraction, y + 4.F},
                        PaddockRound(2.F), PaddockCol(kSetWarm));
    }
    ImGui::PopID();
    return press.pressed;
}

void DrawAchievementDetail(const ach::View& state, const std::vector<ach::Definition>& definitions) {
    const auto found = std::ranges::find(definitions, g_achievement_detail, &ach::Definition::id);
    if (found == definitions.end())
        return;
    if (BeginSettingsDialog("##achievement-detail", found->title, 570.F, 0x0C293A)) {
        const auto index = std::size_t(found - definitions.begin());
        const auto& entry = state.entries[index];
        const float width = SettingsDialogInner();
        const auto summary = ImGui::GetCursorScreenPos();
        DrawAchievementBadge(summary, 44.F, AchievementBadge(*found), entry.unlocked);
        ImGui::SetCursorScreenPos({summary.x + 56.F, summary.y + 10.F});
        DetailText(found->category, width - 56.F, kSetWarm);
        ImGui::SetCursorScreenPos({summary.x, summary.y + 51.F});
        DetailText(found->description, width);
        DetailText(entry.unlocked ? (entry.restored ? "Recognised from existing progress"
                                     : entry.modded ? "Unlocked during modded play"
                                                    : "Unlocked")
                                  : "Not unlocked yet",
                   width, entry.unlocked ? 0x42D6BC : kSetMuted);
        if (entry.when) {
            const auto time = std::time_t(entry.when);
            std::tm stamp{};
#ifdef _WIN32
            gmtime_s(&stamp, &time);
#else
            gmtime_r(&time, &stamp);
#endif
            char when[80]{};
            std::strftime(when, sizeof(when), "%d %b %Y, %H:%M UTC", &stamp);
            DetailText(when, width, kSetMuted);
        }
        if (found->kind == ach::Kind::TimeTrial || found->kind == ach::Kind::Developer) {
            const auto c = std::find_if(ach::courses.begin(), ach::courses.end(),
                                        [&](auto& c) { return c.id == found->argument; });
            if (c != ach::courses.end()) {
                const auto hundredths =
                    found->kind == ach::Kind::TimeTrial ? c->tt_ticks * 100 / 60 : c->developer_centiseconds;
                char target[48];
                std::snprintf(target, sizeof(target), "Target: %d:%02d.%02d", hundredths / 6000,
                              (hundredths / 100) % 60, hundredths % 100);
                constexpr const char* vehicles[]{"Car", "Hovercraft", "Plane"};
                DetailText(std::string(target) + "  |  " + vehicles[c->vehicle], width);
            }
        } else if (found->target > 1) {
            DetailText(std::to_string(entry.progress) + " / " + std::to_string(found->target), width);
            AchievementProgressBar(width, float(entry.progress) / float(found->target));
        }
        if (!entry.unlocked && SettingsButton(state.pinned == found->id ? "Unpin goal" : "Pin goal"))
            ach::pin(state.pinned == found->id ? "" : found->id);
        if (!entry.unlocked && found->restorable && entry.progress >= found->target)
            DetailText(
                "This milestone is already present in your save. Settings lets you recognise existing progress explicitly.",
                width, kSetMuted);
        if (found->kind == ach::Kind::Driver)
            DetailText("New balloons only. Leaving Adventure or closing the game restarts this challenge.", width,
                       kSetMuted);
        if (found->argument < 65 && (found->kind == ach::Kind::Silver || found->kind == ach::Kind::Developer ||
                                     found->kind == ach::Kind::TimeTrial))
            DetailText("Original course only. Custom courses and custom cups do not count for this goal.", width,
                       kSetMuted);
        PaddockGap(14.F);
        if (SettingsButton("Back to gallery") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            g_achievement_focus_row = g_achievement_detail;
            ImGui::CloseCurrentPopup();
        }
        EndSettingsDialog();
    }
}

void DrawAchievementGallery(float width, const ach::View& state, const std::vector<ach::Definition>& definitions) {
    const auto pinned = std::ranges::find(definitions, state.pinned, &ach::Definition::id);
    const std::string footer =
        pinned == definitions.end() ? "Select a badge to see how to unlock it." : "Pinned goal: " + pinned->title;
    const auto footer_type = PaddockReading(14.F, false, 1.35F);
    const float footer_height = PaddockTextHeight(footer_type, footer, width);
    const float height = std::max(112.F, PanelFooterBounds(ImGui::GetCurrentWindow()).Max.y -
                                             ImGui::GetCursorScreenPos().y - footer_height - 24.F);
    PaddockGap(10.F);
    bool opened = false;
    if (std::exchange(g_achievement_reset_scroll, false))
        ImGui::SetNextWindowScroll({-1.F, 0.F});
    if (ImGui::BeginChild("##badge-gallery", {width, height}, false,
                          ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoBackground)) {
        LibraryNavigationScrollWindow(ImGui::GetCurrentWindow());
        std::vector<unsigned> entries;
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        const auto search = lower(g_achievement_search.data());
        for (unsigned i = 0; i < definitions.size(); ++i) {
            const auto& badge = definitions[i];
            const auto& entry = state.entries[i];
            if ((g_achievement_filter == 1 && entry.unlocked) || (g_achievement_filter == 2 && !entry.unlocked) ||
                (!g_achievement_category.empty() && badge.category != g_achievement_category))
                continue;
            if (search.empty() ||
                lower(badge.title + " " + badge.description + " " + badge.category).find(search) != std::string::npos)
                entries.push_back(i);
        }
        if (g_achievement_sort == 1) {
            std::stable_sort(entries.begin(), entries.end(),
                             [&](unsigned a, unsigned b) { return state.entries[a].when > state.entries[b].when; });
        } else if (g_achievement_sort == 2) {
            const auto completion = [&](unsigned i) {
                return state.entries[i].unlocked ? 1.F
                       : AchievementHasProgress(definitions[i])
                           ? std::clamp(float(state.entries[i].progress) / float(definitions[i].target), 0.F, 1.F)
                           : 0.F;
            };
            std::stable_sort(entries.begin(), entries.end(),
                             [&](unsigned a, unsigned b) { return completion(a) > completion(b); });
        }
        constexpr float gap = 9.F;
        const float available = ImGui::GetContentRegionAvail().x - 4.F;
        const std::size_t columns = available >= 660.F ? 3U : available >= 420.F ? 2U : 1U;
        const float card_width = std::floor((available - gap * float(columns - 1U)) / float(columns));
        const auto at = ImGui::GetCursorScreenPos();
        const auto title = PaddockReading(16.F, true, 1.17F);
        float y = at.y;
        for (std::size_t row = 0; row < entries.size(); row += columns) {
            const auto end = std::min(row + columns, entries.size());
            float card_height = 109.F;
            for (std::size_t index = row; index < end; ++index) {
                const auto& badge = definitions[entries[index]];
                card_height = std::max(card_height, 87.F + PaddockTextHeight(title, badge.title, card_width - 22.F));
            }
            for (std::size_t index = row; index < end; ++index) {
                const unsigned id = entries[index];
                const auto& badge = definitions[id];
                ImGui::SetCursorScreenPos({at.x + float(index - row) * (card_width + gap), y});
                if (DrawAchievementGalleryCard(badge, state.entries[id], card_width, card_height,
                                               state.pinned == badge.id)) {
                    g_achievement_detail = badge.id;
                    opened = true;
                }
            }
            y += card_height + gap;
        }
        if (entries.empty())
            DetailText("No achievements match those filters.", available, kSetMuted);
    }
    ImGui::EndChild();
    PaddockGap(6.F);
    PaddockText(footer_type, PaddockRgb(kSetMuted), footer, width);
    if (opened)
        ImGui::OpenPopup("##achievement-detail");
    DrawAchievementDetail(state, definitions);
}

void DrawAchievementsPage(float width) {
    const auto state = ach::view();
    const auto& definitions = ach::catalogue();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    const auto header = ImGui::GetCursorScreenPos();
    DrawPageHeading("ACHIEVEMENTS");
    const auto below = ImGui::GetCursorScreenPos();
    const float back_width = SettingsButtonWidth("Back to Play");
    if (width >= 440.F)
        ImGui::SetCursorScreenPos({header.x + width - back_width, header.y + 11.F});
    if (PaddockTextLink("Back to Play"))
        g_page_navigation_request = kPagePlay;
    ImGui::SetCursorScreenPos({header.x, std::max(below.y, ImGui::GetItemRectMax().y) + 10.F});
    if (g_achievement_settings) {
        DrawAchievementPreferences(width, *state);
    } else {
        {
            PaddockBox collection(width, {12.F, 10.F});
            const float inner = collection.Inner();
            const auto at = ImGui::GetCursorScreenPos();
            const auto text =
                std::to_string(state->unlocked) + " / " + std::to_string(definitions.size()) + " unlocked";
            const auto type = PaddockReading(15.F, true, 1.22F);
            const float text_width = PaddockMeasure(type, text) + 1.F;
            PaddockText(type, PaddockRgb(kSetText), text, inner);
            if (inner >= text_width + 112.F) {
                ImGui::SetCursorScreenPos({at.x + text_width + 12.F, at.y + (type.line - 6.F) * .5F});
                AchievementProgressBar(inner - text_width - 12.F, float(state->unlocked) / float(definitions.size()));
                ImGui::SetCursorScreenPos({at.x, at.y + type.line});
                ImGui::Dummy({inner, 0.F});
            } else {
                PaddockGap(4.F);
                AchievementProgressBar(inner, float(state->unlocked) / float(definitions.size()));
            }
            collection.End([](ImDrawList* draw, ImVec2 min, ImVec2 max) {
                PaddockPanel(draw, min, max, {PaddockRound(10.F), PaddockRgb(0x0C293A), PaddockRgb(kSetBorder), 1.F});
            });
        }
        if (!state->error.empty())
            DetailText(state->error, width, 0xFFAB72);
        else if (state->status != "Saved on this device")
            DetailText(state->status, width, kSetMuted);
        PaddockGap(10.F);
        const int previous_filter = g_achievement_filter;
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
                g_achievement_filter = (g_achievement_filter + 2) % 3;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
                g_achievement_filter = (g_achievement_filter + 1) % 3;
        }
        const auto tabs = ImGui::GetCursorScreenPos();
        const float settings_width = SettingsButtonWidth("Settings");
        const bool inline_settings = width >= 440.F;
        const int filter = SettingsSectionSigns({"ALL", "LOCKED", "UNLOCKED"}, g_achievement_filter,
                                                width - (inline_settings ? settings_width + 9.F : 0.F), true);
        if (filter >= 0)
            g_achievement_filter = filter;
        g_achievement_reset_scroll |= previous_filter != g_achievement_filter;
        const float after_tabs = ImGui::GetCursorScreenPos().y;
        if (inline_settings)
            ImGui::SetCursorScreenPos({tabs.x + width - settings_width, tabs.y});
        if (SettingsButton("Settings", settings_width))
            g_achievement_settings = true;
        ImGui::SetCursorScreenPos({tabs.x, std::max(after_tabs, ImGui::GetItemRectMax().y) + 9.F});
        const bool wide = width >= 620.F;
        constexpr float gap = 9.F;
        const float field = wide ? std::clamp(width * .25F, 210.F, 240.F) : (width - gap) * .5F;
        const float search_width = wide ? width - 2.F * (field + gap) : width;
        const auto toolbar = ImGui::GetCursorScreenPos();
        SettingsLabel("Search achievements", search_width);
        const auto search = PaddockSearch("achievements", g_achievement_search.data(), g_achievement_search.size(),
                                          "Find an achievement...", search_width);
        g_achievement_reset_scroll |= search == PaddockFieldResult::Edited;
        if (search == PaddockFieldResult::GamepadActivated) {
            RequestCustomTextKeyboard("Find an achievement", g_achievement_search.data(), g_achievement_search.size(),
                                      [](const std::string& value) {
                                          std::snprintf(g_achievement_search.data(), g_achievement_search.size(), "%s",
                                                        value.c_str());
                                          g_achievement_reset_scroll = true;
                                      });
        }
        const float search_bottom = ImGui::GetItemRectMax().y;
        const float category_x = wide ? toolbar.x + search_width + gap : toolbar.x;
        const float filters_y = wide ? toolbar.y : search_bottom + gap;
        std::vector<SettingsChoice> categories{{"All categories", {}, false}};
        int selected = 0;
        for (const auto& badge : definitions) {
            if (std::ranges::find(categories, badge.category, &SettingsChoice::label) == categories.end()) {
                if (g_achievement_category == badge.category)
                    selected = static_cast<int>(categories.size());
                categories.push_back({badge.category, {}, false});
            }
        }
        ImGui::SetCursorScreenPos({category_x, filters_y});
        ImGui::BeginGroup();
        if (SettingsDropdown("##achievement-category", "Category", &selected, categories, field, false, {}, 44.F)) {
            g_achievement_category = selected ? categories[selected].label : "";
            g_achievement_reset_scroll = true;
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos({category_x + field + gap, filters_y});
        ImGui::BeginGroup();
        g_achievement_reset_scroll |=
            SettingsDropdown("##achievement-sort", "Sort by", &g_achievement_sort,
                             {"Collection order", "Recently unlocked", "Most progress"}, field, false, {}, 44.F);
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos({toolbar.x, std::max(search_bottom, ImGui::GetItemRectMax().y)});
        ImGui::Dummy({width, 0.F});
        DrawAchievementGallery(width, *state, definitions);
    }
    ImGui::PopStyleVar();
    if (g_achievement_reset) {
        ImGui::OpenPopup("Reset achievements##confirm");
        g_achievement_reset = false;
    }
    if (g_achievement_recognise) {
        ImGui::OpenPopup("Recognise progress##confirm");
        g_achievement_recognise = false;
    }
    const float confirmation_width = std::max(140.F, std::min(520.F, ImGui::GetMainViewport()->WorkSize.x - 80.F));
    if (BeginPaddockModalWindow("Reset achievements##confirm", ImGuiWindowFlags_AlwaysAutoResize)) {
        DetailText("Reset this achievement collection? Your game saves will not change. A backup is kept.",
                   confirmation_width);
        DumpActionRow actions(confirmation_width);
        if (actions.Button("Reset collection")) {
            ach::reset_collection();
            ImGui::CloseCurrentPopup();
        }
        if (actions.Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (BeginPaddockModalWindow("Recognise progress##confirm", ImGuiWindowFlags_AlwaysAutoResize)) {
        DetailText(
            "Recognise milestones shown by the current adventure? Time trials and session challenges still need to be played. Recognised entries are labelled, not presented as new gameplay unlocks.",
            confirmation_width);
        DumpActionRow actions(confirmation_width);
        if (actions.Button("Recognise progress")) {
            ach::restore_progress();
            ImGui::CloseCurrentPopup();
        }
        if (actions.Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
