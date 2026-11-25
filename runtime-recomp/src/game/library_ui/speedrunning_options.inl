namespace speed = dkr::runtime::speedrunning;
using RunOption = speed::Option;

int RunPreference(RunOption key) {
    return speed::options.get(key);
}

std::string RunClock(std::int64_t ticks) {
    auto time = LeaderboardTime(ticks);
    if (RunPreference(RunOption::Precision) == 1)
        time.pop_back();
    return time;
}

struct RunCorner {
    ImVec2 position, pivot;
};

RunCorner RunOverlayCorner(const ImGuiViewport& view, int corner) {
    const auto inset = std::array{12.F, 22.F, 36.F}[RunPreference(RunOption::Inset)];
    const bool right = corner % 2, bottom = corner >= 2;
    return {{view.WorkPos.x + (right ? view.WorkSize.x - inset : inset),
             view.WorkPos.y + (bottom ? view.WorkSize.y - inset : inset + 24.F)},
            {float(right), float(bottom)}};
}

void SetRunPreference(RunOption key, int value) {
    speed::options.set(key, value);
    SaveSettings();
}

ImTextureID RunPreviewTexture() {
#if DKR_RUNTIME_HAS_RT64
    struct PreviewPixels {
        int width = 0, height = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> rgba{nullptr, stbi_image_free};
    };
    static const auto pixels = [] {
        PreviewPixels image;
        image.rgba.reset(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(SpeedrunningHudPreview),
                                               static_cast<int>(SpeedrunningHudPreview_size), &image.width,
                                               &image.height, nullptr, STBI_rgb_alpha));
        return image;
    }();
    if (!pixels.rgba)
        return {};
    if (!g_mod_store_renderer)
        return reinterpret_cast<ImTextureID>(
            dkr::store::overlay::image(
                "speedrunning-hud-preview", pixels.width, pixels.height,
                std::span(pixels.rgba.get(), std::size_t(pixels.width) * pixels.height * STBI_rgb_alpha))
                .id);
    if (!g_speedrunning_preview_texture) {
        auto* texture = SDL_CreateTexture(g_mod_store_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                          pixels.width, pixels.height);
        if (texture && SDL_UpdateTexture(texture, nullptr, pixels.rgba.get(), pixels.width * STBI_rgb_alpha) != 0) {
            SDL_DestroyTexture(texture);
            return {};
        }
        if (texture) {
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        g_speedrunning_preview_texture = texture;
    }
    return reinterpret_cast<ImTextureID>(g_speedrunning_preview_texture);
#else
    return {};
#endif
}

void PaintReplayController(ImDrawList* draw, ImVec2 base, const trial::Progress& state, float scale);

void DrawRunHudPreview(float width, bool replay) {
    const bool splits = RunPreference(RunOption::Splits) && (!replay || RunPreference(RunOption::ReplaySplits));
    const bool inputs = replay && RunPreference(RunOption::ReplayInputs);
    const bool contrast = RunPreference(RunOption::Contrast);
    const int detail = RunPreference(RunOption::SplitDetail);
    const int upcoming = detail == 2 ? 0 : RunPreference(RunOption::Upcoming);
    const int comparison = RunPreference(RunOption::Comparison);
    OlHeading("HUD Preview", 18.F, kOlCream, width);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float height = width * 9.F / 16.F;
    const ImVec2 end{origin.x + width, origin.y + height};
    const float scale = std::clamp(width / 530.F, .54F, 1.4F);
    auto* draw = ImGui::GetWindowDrawList();
    const auto type = OlRead(14.F * scale);
    if (const auto texture = RunPreviewTexture())
        draw->AddImageRounded(texture, origin, end, {0.F, 0.F}, {1.F, 1.F}, IM_COL32_WHITE, 9.F);
    else
        draw->AddRectFilled(origin, end, IM_COL32(15, 45, 62, 255), 9.F);
    draw->AddRect(origin, end, PaddockCol(kOlSoft, 87), 9.F);
    draw->PushClipRect(origin, end, true);
    const float inset = scale * std::array{6.F, 11.F, 17.F}[RunPreference(RunOption::Inset)];
    const auto card_origin = [&](int corner, float w, float h) {
        return ImVec2{corner % 2 ? end.x - inset - w : origin.x + inset,
                      corner >= 2 ? end.y - inset - h : origin.y + inset};
    };
    if (splits) {
        const auto clock = RunClock(replay ? 1036 : 8009);
        const std::array<std::string_view, 4> comparisons{"PB", "WR", "Best segments", ""};
        const auto pace =
            std::string(comparisons[comparison]) + (RunPreference(RunOption::DeltaWords) ? " 0.38 ahead" : " -0.38");
        std::array<std::pair<std::string_view, unsigned>, 6> lines;
        std::size_t rows = 0;
        const auto add_line = [&](std::string_view text, unsigned color) { lines[rows++] = {text, color}; };
        add_line(replay ? "REPLAY · LAP 1 / 3" : "LIVE SPLITS", kOlAmber);
        add_line(clock, kOlCream);
        if (detail != 2) {
            add_line(detail ? "Ancient Lake" : "Dino Domain · 2:13", kOlInk);
            if (comparison != 3)
                add_line(pace, kOlGoText);
            if (upcoming)
                add_line("Next · Fossil Canyon", kOlSoft);
            if (upcoming == 2)
                add_line("Then · Jungle Falls", kOlSoft);
        }
        const auto visible_lines = std::span(lines).first(rows);
        const float padding = 7.F * scale;
        float desired_width = scale * std::array{155.F, 177.F, 195.F}[RunPreference(RunOption::HudSize)];
        for (const auto& [value, color] : visible_lines) {
            const auto measured =
                type.font->CalcTextSizeA(type.size, FLT_MAX, 0.F, value.data(), value.data() + value.size());
            desired_width = std::max(desired_width, measured.x + 2.F * padding);
        }
        const float x = std::min(width - 2.F * inset, desired_width);
        const float lh = type.size + 4.F * scale;
        const float h = rows * lh + 12.F;
        const auto pos = card_origin(RunPreference(RunOption::SplitPosition), x, h);
        const int alpha = contrast ? 255 : RunPreference(RunOption::Background) * 255 / 100;
        draw->AddRectFilled(pos, {pos.x + x, pos.y + h}, IM_COL32(6, 18, 31, alpha), 7.F);
        auto line = ImVec2{pos.x + padding, pos.y + 6.F};
        draw->PushClipRect(pos, {pos.x + x, pos.y + h}, true);
        for (const auto& [value, color] : visible_lines) {
            draw->AddText(type.font, type.size, line, PaddockCol(color), value.data(), value.data() + value.size());
            line.y += lh;
        }
        draw->PopClipRect();
    }
    if (inputs) {
        int corner = RunPreference(RunOption::InputPosition);
        if (splits && corner == RunPreference(RunOption::SplitPosition))
            corner ^= 1;
        const float input_scale = scale * std::array{.40F, .50F, .60F}[RunPreference(RunOption::InputSize)];
        const ImVec2 size{232.F * input_scale, 140.F * input_scale};
        const auto point = card_origin(corner, size.x, size.y);
        const int alpha = contrast ? 255 : 212;
        draw->AddRectFilled(point, {point.x + size.x, point.y + size.y}, IM_COL32(6, 18, 31, alpha), 7.F);
        const trial::Progress sample{.buttons = 0x8000, .stick_x = 25, .stick_y = 35};
        PaintReplayController(draw, point, sample, input_scale);
    }
    draw->PopClipRect();
    ImGui::Dummy({width, height});
}

void DrawRunOptionPanel(int group, int panel, float width) {
    static constexpr std::array headings{
        std::array{"Timer display", "Split comparisons"}, std::array{"HUD layout", "Accessibility"},
        std::array{"Appearance", "Race information"},     std::array{"Replay overlays", "Playback"},
        std::array{"Finish details", "Celebrations"},     std::array{"Run setup", "Replay library"},
    };
    DrawOlCard(
        width, OlPanelLook{},
        [&](float inner) {
            OlHeading(headings[group][panel], 20.F, kOlCream, inner);
            PaddockGap(4.F);
            const auto id = [](RunOption key) { return speed::settings[static_cast<std::size_t>(key)].key.data(); };
            const auto choice = [&](RunOption key, const char* title, std::initializer_list<const char*> labels,
                                    bool disabled = false) {
                int value = RunPreference(key);
                if (SettingsDropdown(id(key), title, &value, labels, inner, disabled))
                    SetRunPreference(key, value);
            };
            const auto toggle = [&](RunOption key, const char* title, const char* help) {
                bool value = RunPreference(key) != 0;
                if (OlSwitchRow(id(key), title, help, &value, inner, false))
                    SetRunPreference(key, value);
            };
            const auto range = [&](RunOption key, const char* title, int minimum) {
                int value = RunPreference(key);
                if (SettingsRange(id(key), title, &value, minimum, 100, inner,
                                  [](int percent) { return SettingsRangeValue{std::to_string(percent), "%"}; }))
                    SetRunPreference(key, value);
            };
            const std::initializer_list<const char*> corners{"Top left", "Top right", "Bottom left", "Bottom right"};
            const std::initializer_list<const char*> sizes{"Small", "Standard", "Large"};
            switch (group) {
            case 0:
                if (panel == 0) {
                    toggle(RunOption::Splits, "Show Live Splits", "Show the speedrunning timer and splits.");
                    const bool hidden = !RunPreference(RunOption::Splits);
                    choice(RunOption::SplitPosition, "Position", corners, hidden);
                    choice(RunOption::SplitDetail, "Detail", {"Detailed", "Compact", "Timer only"}, hidden);
                    choice(RunOption::Precision, "Time precision", {"Milliseconds", "Hundredths"}, hidden);
                } else {
                    const bool timer = !RunPreference(RunOption::Splits) || RunPreference(RunOption::SplitDetail) == 2;
                    choice(RunOption::Comparison, "Compare against",
                           {"Personal Best", "World Record", "Best Segments", "Off"}, timer);
                    choice(RunOption::Upcoming, "Upcoming splits", {"Hide", "Next split", "Next two splits"}, timer);
                }
                break;
            case 1:
                if (panel == 0) {
                    choice(RunOption::HudSize, "Split HUD size", sizes);
                    choice(RunOption::Inset, "Screen-edge padding", {"Narrow", "Normal", "Wide"});
                    range(RunOption::Background, "Background opacity", 55);
                } else {
                    toggle(RunOption::Contrast, "High contrast HUD", "Use an opaque background and brighter text.");
                    toggle(RunOption::DeltaWords, "Label deltas ahead / behind",
                           "Use words as well as colour to show pace.");
                    toggle(RunOption::ReducedMotion, "Reduce overlay animations",
                           "Show static celebrations and loading indicators.");
                }
                break;
            case 2:
                if (panel == 0) {
                    choice(RunOption::GhostStyle, "Ghost appearance", {"Translucent racer", "Outline marker"});
                    range(RunOption::GhostOpacity, "Ghost opacity", 0);
                    toggle(RunOption::GhostNames, "Show ghost racer names",
                           "Display the credited Discord name above leaderboard ghosts.");
                } else {
                    choice(RunOption::GhostProximity, "When the ghost is close", {"Keep visible", "Fade out"});
                    toggle(RunOption::GhostGap, "Show time gap to ghost",
                           "Compare your lap crossing with the ghost's.");
                    toggle(RunOption::GhostMarker, "Ghost marker on minimap",
                           "Show the ghost's position on the course map.");
                }
                break;
            case 3:
                if (panel == 0) {
                    toggle(RunOption::ReplayInputs, "Show controller inputs",
                           "Display the recorded N64 controller inputs.");
                    toggle(RunOption::ReplaySplits, "Show Live Splits in replays",
                           "Use your Live Splits layout during playback.");
                    const bool no_controller = !RunPreference(RunOption::ReplayInputs);
                    choice(RunOption::InputPosition, "Controller position", corners, no_controller);
                    choice(RunOption::InputSize, "Controller size", sizes, no_controller);
                } else {
                    choice(RunOption::ReplaySpeed, "Starting speed", {"0.5x", "1x", "1.5x", "2x"});
                    choice(RunOption::PlaybackControls, "Playback controls", {"Always show", "Auto-hide"});
                    choice(RunOption::SeekStep, "Seek step", {"5 seconds", "10 seconds", "30 seconds"});
                    toggle(RunOption::StartPaused, "Open replays paused", "Wait for Play after the replay loads.");
                    toggle(RunOption::LapMarkers, "Show timeline markers",
                           "Show lap or split markers on the timeline.");
                }
                break;
            case 4:
                if (panel == 0) {
                    toggle(RunOption::LapDelta, "Show lap time comparisons",
                           "Show your lap time and gap to the selected reference.");
                    toggle(RunOption::FinishSummary, "Show finish time card", "Show your time when the run ends.");
                } else {
                    choice(RunOption::Celebration, "Podium celebrations", {"Full screen", "Compact", "Off"});
                    choice(RunOption::CelebrationScope, "Celebrate",
                           {"Top-three pace", "Record pace only", "Personal bests and podiums"},
                           RunPreference(RunOption::Celebration) == 2);
                    range(RunOption::CueVolume, "Speedrunning cue volume", 0);
                }
                break;
            case 5:
                if (panel == 0) {
                    choice(RunOption::ReturnTo, "After a run, open", {"Last Run", "Replays"});
                    toggle(RunOption::RememberSetup, "Remember last run setup",
                           "Keep your last course, vehicle, racer, and challenge.");
                    toggle(RunOption::ConfirmRestart, "Confirm before restarting",
                           "Ask before replacing the current attempt.");
                } else {
                    choice(RunOption::Retention, "Keep new local replays",
                           {"All completed runs", "Personal bests and pending posts"});
                    choice(RunOption::ReplaySort, "Default replay order",
                           {"Newest first", "Personal bests first", "Track name"});
                    toggle(RunOption::PostPrompt, "Offer to post eligible personal bests",
                           "Show the posting action in the Last Run card.");
                }
                break;
            }
        },
        16.F);
}

void DrawSpeedrunningOptions(float width) {
    static int group = 0;
    static const std::vector<std::string> groups{"Live Splits",    "HUD & Accessibility", "Ghosts",
                                                 "Replay Theatre", "Finish & Cues",       "Run & Library"};
    group = StoreTabLine(groups, group, width, "speedrunning-options", true);
    PaddockGap(8.F);

    constexpr float gap = 16.F;
    constexpr float minimum_pair = 640.F;
    const auto draw_row = [&](float row_width, std::span<const float> columns, auto&& draw_column) {
        const auto at = ImGui::GetCursorScreenPos();
        float left = at.x, bottom = at.y;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            ImGui::SetCursorScreenPos({left, at.y});
            ImGui::BeginGroup();
            draw_column(i, columns[i]);
            ImGui::EndGroup();
            bottom = std::max(bottom, ImGui::GetItemRectMax().y);
            left += columns[i] + gap;
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({row_width, bottom - at.y});
    };
    const auto draw_settings = [&](float settings_width) {
        if (settings_width >= minimum_pair) {
            const float column = (settings_width - gap) * .5F;
            draw_row(settings_width, std::array{column, column},
                     [&](std::size_t panel, float size) { DrawRunOptionPanel(group, static_cast<int>(panel), size); });
        } else {
            DrawRunOptionPanel(group, 0, settings_width);
            PaddockGap(gap);
            DrawRunOptionPanel(group, 1, settings_width);
        }
    };
    const auto draw_preview = [&](float preview_width) {
        DrawOlCard(preview_width, OlPanelLook{}, [&](float inner) { DrawRunHudPreview(inner, group == 3); }, 16.F);
    };

    const bool preview = group == 0 || group == 1 || group == 3;
    if (preview && width >= 740.F) {
        const float preview_width = (width - gap) * .46F;
        draw_row(width, std::array{width - gap - preview_width, preview_width}, [&](std::size_t column, float size) {
            if (column == 0)
                draw_settings(size);
            else
                draw_preview(size);
        });
    } else {
        draw_settings(width);
        if (preview) {
            PaddockGap(gap);
            draw_preview(width);
        }
    }
}
