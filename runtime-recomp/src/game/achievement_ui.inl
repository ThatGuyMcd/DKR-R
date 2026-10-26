namespace ach = dkr::runtime::achievements;
int g_achievement_filter = 0, g_achievement_sort = 0;
std::string g_achievement_category, g_achievement_detail;
std::array<char, 161> g_achievement_search{};
bool g_achievement_settings = false, g_achievement_reset = false, g_achievement_recognise = false;
bool g_achievement_focus_detail = false;
std::string g_achievement_focus_row;
float g_achievement_return_scroll = 0;
ach::Toast g_achievement_toast;
double g_achievement_toast_started = 0;
SDL_AudioDeviceID g_achievement_audio = 0;

void AchievementChime() {
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
    const float volume = .10F * dkr::runtime::audio::sound_effects_volume();
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
    PaddockGap(18);
    DetailHeading("Notifications", width);
    auto p = state.preferences;
    bool changed = false;
    changed |= SettingsCheck("##achievement-popups", "Show unlock notifications", &p.notifications, width);
    changed |= SettingsCheck("##achievement-chime", "Play unlock sound", &p.sound, width);
    changed |= SettingsCheck("##achievement-motion", "Reduce notification motion", &p.reduced_motion, width);
    SettingsLabel("Position", width);
    const char* positions[]{"Bottom centre", "Bottom right", "Top right"};
    ImGui::SetNextItemWidth(std::min(width, 300.F));
    changed |= ImGui::Combo("##achievement-position", &p.position, positions, 3);
    SettingsLabel("Size", width);
    ImGui::SetNextItemWidth(std::min(width, 360.F));
    changed |= ImGui::SliderFloat("##achievement-size", &p.scale, .7F, 1.6F, "%.1fx");
    SettingsLabel("Duration", width);
    ImGui::SetNextItemWidth(std::min(width, 360.F));
    changed |= ImGui::SliderFloat("##achievement-duration", &p.duration, 2.F, 10.F, "%.0f seconds");
    if (changed)
        ach::preferences(p);
    if (SettingsButton("Preview notification"))
        ach::preview();
    PaddockGap(22);
    DetailHeading("Your collection", width);
    DetailText("Achievements stay separate from your game saves. Starting another adventure will not erase them.",
               width);
    DumpActionRow files(width);
    if (files.Button("Export backup"))
        AchievementFileDialog(false);
    if (files.Button("Import backup"))
        AchievementFileDialog(true);
    if (files.Button("Recognise existing progress"))
        g_achievement_recognise = true;
    PaddockGap(16);
    if (SettingsButton("Reset achievement collection"))
        g_achievement_reset = true;
    PaddockGap(18);
    DetailText(
        "Challenge objectives inspired by Delmaru's Diddy Kong Racing achievement set. Local DKR-R implementation, wording and badge designs. Not connected to RetroAchievements.",
        width, kSetMuted);
}
void DrawAchievementsPage(float width) {
    const auto state = ach::view();
    const auto& definitions = ach::catalogue();
    DrawPageHeading("ACHIEVEMENTS");
    PaddockGap(8);
    DetailText("Your challenges. Your collection.", width, kSetMuted, 12);
    DetailHeading(std::to_string(state->unlocked) + " / 94 unlocked", width);
    ImGui::ProgressBar(float(state->unlocked) / 94.F, {width, 7.F}, "");
    PaddockGap(14);
    if (!state->error.empty())
        DetailText(state->error, width, 0xFFAB72);
    else if (state->status != "Saved on this device")
        DetailText(state->status, width, kSetMuted);
    if (g_achievement_settings) {
        DrawAchievementPreferences(width, *state);
    } else if (!g_achievement_detail.empty()) {
        const auto found = std::find_if(definitions.begin(), definitions.end(),
                                        [](const auto& d) { return d.id == g_achievement_detail; });
        if (found == definitions.end()) {
            g_achievement_detail.clear();
            return;
        }
        const auto index = std::size_t(found - definitions.begin());
        const auto& entry = state->entries[index];
        if (g_achievement_focus_detail) {
            ImGui::SetKeyboardFocusHere();
            g_achievement_focus_detail = false;
        }
        if (SettingsButton("Back to achievements") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            g_achievement_focus_row = g_achievement_detail;
            g_achievement_detail.clear();
            ImGui::SetScrollY(g_achievement_return_scroll);
        }
        PaddockGap(18);
        DetailHeading(found->title, width);
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
                DetailText(target, width);
            }
        } else if (found->target > 1) {
            DetailText(std::to_string(entry.progress) + " / " + std::to_string(found->target), width);
            ImGui::ProgressBar(float(entry.progress) / float(found->target), {width, 7.F}, "");
        }
        if (!entry.unlocked && SettingsButton(state->pinned == found->id ? "Unpin goal" : "Pin goal"))
            ach::pin(state->pinned == found->id ? "" : found->id);
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
    } else {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
            g_achievement_filter = (g_achievement_filter + 2) % 3;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
            g_achievement_filter = (g_achievement_filter + 1) % 3;
        DumpActionRow actions(width);
        for (int i = 0; i < 3; ++i) {
            const char* labels[]{"All", "Locked", "Unlocked"};
            if (actions.Button(labels[i], false,
                               i == g_achievement_filter ? SettingsButtonTone::Selected : SettingsButtonTone::Plain))
                g_achievement_filter = i;
        }
        if (actions.Button("Settings"))
            g_achievement_settings = true;
        PaddockGap(12);
        ImGui::SetNextItemWidth(width >= 240.F ? width - 115.F : width);
        ImGui::InputTextWithHint("##achievement-search", "Find an achievement...", g_achievement_search.data(),
                                 g_achievement_search.size());
        if (width >= 240.F)
            ImGui::SameLine();
        if (SettingsButton("Keyboard", 100.F))
            RequestCustomTextKeyboard("Find an achievement", g_achievement_search.data(), g_achievement_search.size(),
                                      [](const std::string& value) {
                                          std::snprintf(g_achievement_search.data(), g_achievement_search.size(), "%s",
                                                        value.c_str());
                                      });
        PaddockGap(10);
        std::vector<std::string> groups{"All categories"};
        for (const auto& d : definitions)
            if (std::find(groups.begin(), groups.end(), d.category) == groups.end())
                groups.push_back(d.category);
        ImGui::SetNextItemWidth(std::min(width, 260.F));
        if (ImGui::BeginCombo("##achievement-category",
                              g_achievement_category.empty() ? "All categories" : g_achievement_category.c_str())) {
            for (unsigned i = 0; i < groups.size(); ++i)
                if (ImGui::Selectable(groups[i].c_str(), g_achievement_category == groups[i]))
                    g_achievement_category = i ? groups[i] : "";
            ImGui::EndCombo();
        }
        const char* sorts[]{"Collection order", "Recently unlocked", "Most progress"};
        ImGui::SetNextItemWidth(std::min(width, 260.F));
        ImGui::Combo("##achievement-sort", &g_achievement_sort, sorts, 3);
        std::vector<unsigned> entries;
        std::string search = g_achievement_search.data();
        std::transform(search.begin(), search.end(), search.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        for (unsigned i = 0; i < definitions.size(); ++i) {
            const auto& d = definitions[i];
            const auto& e = state->entries[i];
            if ((g_achievement_filter == 1 && e.unlocked) || (g_achievement_filter == 2 && !e.unlocked) ||
                (!g_achievement_category.empty() && d.category != g_achievement_category))
                continue;
            auto text = d.title + " " + d.description;
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (!search.empty() && text.find(search) == std::string::npos)
                continue;
            entries.push_back(i);
        }
        if (g_achievement_sort)
            std::stable_sort(entries.begin(), entries.end(), [&](unsigned a, unsigned b) {
                return g_achievement_sort == 1 ? state->entries[a].when > state->entries[b].when
                                               : double(state->entries[a].progress) / definitions[a].target >
                                                     double(state->entries[b].progress) / definitions[b].target;
            });
        PaddockGap(12);
        if (entries.empty())
            DetailText("No achievements match those filters.", width, kSetMuted);
        const auto heading = PaddockReading(17.F, true, 1.3F), body = PaddockReading(14.F, false, 1.3F);
        for (const auto index : entries) {
            const auto& d = definitions[index];
            const auto& e = state->entries[index];
            ImGui::PushID(d.id.c_str());
            const float badge = 48.F, text_width = std::max(50.F, width - 88.F);
            const auto wrapped = body.font->CalcTextSizeA(body.size, FLT_MAX, text_width, d.description.c_str());
            const auto title_size = heading.font->CalcTextSizeA(heading.size, FLT_MAX, text_width, d.title.c_str());
            const float height = std::max(90.F, title_size.y + wrapped.y + 43.F);
            if (g_achievement_focus_row == d.id) {
                ImGui::SetKeyboardFocusHere();
                g_achievement_focus_row.clear();
            }
            const auto at = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##open", {width, height})) {
                g_achievement_detail = d.id;
                g_achievement_return_scroll = ImGui::GetScrollY();
                g_achievement_focus_detail = true;
                ImGui::SetScrollY(0);
            }
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at, {at.x + width, at.y + height}, PaddockRgb(0x10364A), 12);
            draw->AddRect(at, {at.x + width, at.y + height},
                          PaddockRgb(ImGui::IsItemFocused() || ImGui::IsItemHovered() ? 0xFFCB66 : 0x296B70), 12);
            DrawAchievementBadge({at.x + 12, at.y + 14}, badge, AchievementBadge(d), e.unlocked);
            const ImVec2 text{at.x + 74, at.y + 12};
            draw->AddText(heading.font, heading.size, text, PaddockRgb(0xFFF6DA), d.title.c_str(), nullptr, text_width);
            draw->AddText(body.font, body.size, {text.x, text.y + title_size.y + 5}, PaddockRgb(0xB0C9CC),
                          d.description.c_str(), nullptr, text_width);
            const std::string status = e.unlocked       ? (e.restored ? "Recognised" : "Unlocked")
                                       : e.progress > 0 ? std::to_string(e.progress) + " / " + std::to_string(d.target)
                                                        : "Locked";
            draw->AddText(body.font, body.size, {text.x, at.y + height - body.line - 8},
                          PaddockRgb(e.unlocked ? 0x42D6BC : 0xB0C9CC), status.c_str());
            ImGui::PopID();
            PaddockGap(8);
        }
        DetailText("A: Open   B: Back   LB / RB: Filter", width, kSetMuted);
    }
    if (!state->pinned.empty()) {
        const auto found =
            std::find_if(definitions.begin(), definitions.end(), [&](auto& d) { return d.id == state->pinned; });
        if (found != definitions.end())
            DetailText("Pinned: " + found->title, width, kSetMuted);
    }
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
