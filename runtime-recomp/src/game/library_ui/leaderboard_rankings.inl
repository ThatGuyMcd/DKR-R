std::string LeaderboardPoints(std::int64_t milli) {
    char formatted[32];
    std::snprintf(formatted, sizeof(formatted), "%.3f", static_cast<double>(milli) / 1000.0);
    std::string result(formatted);
    while (result.ends_with('0'))
        result.pop_back();
    if (result.ends_with('.'))
        result.pop_back();
    return result;
}

std::string RecordDate(std::int64_t seconds) {
    const auto stamp = static_cast<std::time_t>(seconds);
    std::tm date{};
#ifdef _WIN32
    if (gmtime_s(&date, &stamp))
        return {};
#else
    if (!gmtime_r(&stamp, &date))
        return {};
#endif
    char label[28]{};
    std::strftime(label, sizeof(label), "%d %b %Y", &date);
    return label;
}

std::int64_t RecordUnix(const dkr::store::Json& data, const char* key) {
    const auto value = data.find(key);
    if (value == data.end() || !value->is_number_integer())
        return 0;
    return std::clamp<std::int64_t>(value->get<std::int64_t>(), 0, 253402300799);
}

std::string RecordDuration(std::int64_t seconds) {
    if (seconds >= 86400) {
        const auto days = seconds / 86400;
        return std::to_string(days) + (days == 1 ? " day" : " days");
    }
    if (seconds >= 3600) {
        const auto hours = seconds / 3600;
        return std::to_string(hours) + (hours == 1 ? " hour" : " hours");
    }
    if (seconds >= 60) {
        const auto minutes = seconds / 60;
        return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    }
    return std::to_string(std::max<std::int64_t>(seconds, 0)) + " sec";
}

void DrawTrackRecordHistory(float width, const dkr::store::Json& history, const std::string& board) {
    if (!history.is_object())
        return;
    const auto changes = history.find("changes");
    if (changes == history.end() || !changes->is_array())
        return;
    const bool complete = history.value("complete", false);
    OlHeading("World record history", 21.F, kOlCream, width);
    if (changes->empty()) {
        OlParagraph(complete ? "Record history appears after the first published run."
                             : "Publication dates are unavailable for these results.",
                    15.F, kOlSoft, width);
        return;
    }
    static std::string selected_board;
    static std::int64_t selected_at = 0;
    if (selected_board != board) {
        selected_board = board;
        selected_at = 0;
    }
    const auto at = [&](std::size_t index) { return RecordUnix((*changes)[index], "at"); };
    const auto ticks = [&](std::size_t index) { return LeaderboardNumber((*changes)[index], "ticks"); };
    const auto last = changes->size() - 1;
    const auto latest = std::max(at(last), RecordUnix(history, "asOf"));
    std::size_t selected = last;
    for (std::size_t index = 0; index < changes->size(); ++index)
        if (selected_at == at(index))
            selected = index;

    const auto chart_origin = ImGui::GetCursorScreenPos();
    constexpr float height = 150.F;
    const float left = std::min(100.F, width * .40F);
    const float right = std::max(left + 1.F, width - 11.F);
    const auto font = OlRead(14.F);
    const auto end = complete ? latest : at(last);
    const double time_span = static_cast<double>(std::max<std::int64_t>(end - at(0), 1));
    const double pace_span = static_cast<double>(std::max<std::int64_t>(ticks(0) - ticks(last), 1));
    const auto chart_x = [&](std::int64_t stamp) {
        return chart_origin.x + left + (right - left) * static_cast<float>((stamp - at(0)) / time_span);
    };
    const auto chart_y = [&](std::int64_t pace) {
        return chart_origin.y + 18.F + 96.F * static_cast<float>((ticks(0) - pace) / pace_span);
    };

    ImGui::Dummy({width, height});
    auto* draw = ImGui::GetWindowDrawList();
    const auto x0 = chart_origin.x + left;
    const auto x1 = chart_origin.x + right;
    const auto caption = [&](const std::string& label, ImVec2 pos) {
        draw->AddText(font.font, font.size, pos, PaddockCol(kOlSoft), label.c_str());
    };
    for (const auto tick : std::array{ticks(0), ticks(last)}) {
        const auto y = chart_y(tick);
        draw->AddLine({x0, y}, {x1, y}, PaddockCol(kOlRing));
        caption(LeaderboardTime(tick), {chart_origin.x, y - 8.F});
        if (ticks(0) == ticks(last))
            break;
    }
    const float date_y = chart_origin.y + 129.F;
    caption(RecordDate(at(0)), {x0, date_y});
    if (end != at(0) && x1 - x0 > 212.F) {
        const auto label = RecordDate(end);
        const auto span = font.font->CalcTextSizeA(font.size, FLT_MAX, 0.F, label.c_str());
        caption(label, {x1 - span.x, date_y});
    }

    float nearest = 16.F;
    const auto pointer = ImGui::GetMousePos();
    const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    for (std::size_t index = 0; index < changes->size(); ++index) {
        const float x = chart_x(at(index)), y = chart_y(ticks(index));
        const float next_x = index != last ? chart_x(at(index + 1)) : chart_x(end);
        draw->AddLine({x, y}, {next_x, y}, PaddockCol(kOlAmber), 2.F);
        if (index != last)
            draw->AddLine({next_x, y}, {next_x, chart_y(ticks(index + 1))}, PaddockCol(kOlAmber), 2.F);
        draw->AddCircleFilled({x, y}, index == selected ? 5.5F : 3.F, PaddockCol(kOlAmber));
        if (clicked) {
            const float distance = std::abs(pointer.x - x) + std::abs(pointer.y - y);
            if (distance < nearest) {
                nearest = distance;
                selected = index;
                selected_at = at(index);
            }
        }
    }
    {
        const OlDisabled unavailable(selected == 0);
        if (OlButton("Earlier record")) {
            --selected;
            selected_at = at(selected);
        }
    }
    ImGui::SameLine();
    {
        const OlDisabled unavailable(selected == last);
        if (OlButton("Later record")) {
            ++selected;
            selected_at = at(selected);
        }
    }
    const auto& event = (*changes)[selected];
    const auto expired = RecordUnix(event, "until");
    std::string detail = LeaderboardTime(ticks(selected)) + "  |  " + RecordDate(at(selected));
    if (expired || complete)
        detail += "  |  " + std::string(expired ? "Stood for " : "Unbeaten for ") +
                  RecordDuration((expired ? expired : latest) - at(selected));
    OlParagraph(detail, 16.F, kOlCream, width, true);
    if (const auto holders = event.find("holders"); holders != event.end() && holders->is_array()) {
        for (const auto& holder : *holders) {
            auto line = StoreText(holder, "name", 96);
            const auto until = RecordUnix(holder, "until");
            if (until || complete)
                line += "  |  " + std::string(until ? "Held for " : "Holding for ") +
                        RecordDuration((until ? until : latest) - RecordUnix(holder, "from"));
            OlParagraph(line, 15.F, kOlSoft, width);
        }
    }
    if (!complete || LeaderboardNumber(history, "earlierChanges"))
        OlParagraph(complete ? "Showing the latest record changes." : "Some publication dates are unavailable.", 14.F,
                    kOlSoft, width);
}

enum class OverallPlayerTab { Score, Runs };

dkr::store::Json g_overall_standings, g_overall_profile;
std::string g_overall_player, g_overall_focus;
OverallPlayerTab g_overall_tab = OverallPlayerTab::Score;
bool g_overall_runs_requested = false;
bool g_overall_show_custom = false;

void SelectOverallPlayer(std::string id) {
    g_overall_player = std::move(id);
    g_overall_profile = nullptr;
    g_overall_runs_requested = false;
    g_overall_show_custom = false;
}

void RefreshOverallRankings() {
    RankedJob([] {
        auto data = dkr::store::service().overall_rankings();
        return [data = std::move(data)]() mutable {
            g_overall_standings = std::move(data);
            g_overall_profile = nullptr;
            g_overall_runs_requested = false;
        };
    });
}

void RefreshOverallPlayer(const std::string& id, unsigned offset = 0) {
    g_overall_runs_requested = true;
    RankedJob([id, offset] {
        auto data = dkr::store::service().overall_rankings(id, offset);
        return [id, offset, data = std::move(data)]() mutable {
            if (id != g_overall_player)
                return;
            if (offset && g_overall_profile.is_object()) {
                for (auto& entry : data["events"])
                    g_overall_profile["events"].push_back(std::move(entry));
                g_overall_profile["nextOffset"] = data["nextOffset"];
            } else {
                g_overall_profile = std::move(data);
            }
        };
    });
}

std::string OverallPersonalBests(const dkr::store::Json& player) {
    const auto count = player.find("personalBests");
    return count != player.end() && count->is_number_integer()
               ? std::to_string(LeaderboardNumber(player, "personalBests"))
               : "—";
}

void DrawOverallStandings(float width, const dkr::store::Json& players) {
    DrawOlCard(
        width, OlPanelLook{},
        [&](float inner) {
            OlHeading("Top Players", 24.F, kOlCream, inner);
            PaddockGap(4.F);
            const bool compact = inner < 300.F;
            constexpr auto flags =
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {compact ? 3.F : 5.F, 4.F});
            if (ImGui::BeginTable("top-player-standings", 5, flags, {inner, 0.F})) {
                ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, compact ? 34.F : 46.F);
                ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("PBs", ImGuiTableColumnFlags_WidthFixed, compact ? 29.F : 34.F);
                ImGui::TableSetupColumn("WRs", ImGuiTableColumnFlags_WidthFixed, compact ? 29.F : 38.F);
                ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, compact ? 58.F : 67.F);

                const auto cell = [](std::string_view text, float height, unsigned colour, bool right = false) {
                    const float width = ImGui::GetContentRegionAvail().x;
                    const auto type = OlRead(14.F, true, 1.2F);
                    const std::string label = PaddockEllipsize(type, text, width);
                    auto at = ImGui::GetCursorScreenPos();
                    if (right)
                        at.x += std::max(0.F, width - PaddockMeasure(type, label));
                    at.y += (height - type.line) * .5F;
                    PaddockDrawRun(ImGui::GetWindowDrawList(), type, at, PaddockCol(colour), label.data(),
                                   label.data() + label.size());
                    ImGui::Dummy({width, height});
                };

                ImGui::TableNextRow();
                int column = 0;
                for (const auto* header : {compact ? "#" : "Rank", "Player", "PBs", "WRs", "Points"}) {
                    ImGui::TableNextColumn();
                    cell(dkr::localisation::source(header), 22.F, kOlSoft, column >= 2);
                    ++column;
                }
                constexpr float row_height = 34.F;
                for (const auto& player : players) {
                    const auto id = StoreText(player, "id", 96);
                    if (id.empty())
                        continue;
                    const auto name = StoreText(player, "name", 96);
                    const auto place = LeaderboardNumber(player, "place");
                    const bool selected = g_overall_player == id;
                    ImGui::PushID(id.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, PaddockCol(selected ? 0x194650U : 0x123345U));
                    ImGui::TableNextColumn();
                    const auto at = ImGui::GetCursorScreenPos();
                    const unsigned badge = place == 1   ? 0x644C26U
                                           : place == 2 ? 0x40576BU
                                           : place == 3 ? 0x67482FU
                                                        : 0x23495CU;
                    PaddockFill(ImGui::GetWindowDrawList(), {at.x + 2.F, at.y + 4.F}, {at.x + 30.F, at.y + 30.F},
                                PaddockRound(5.F), PaddockCol(badge));
                    const auto rank = std::to_string(place);
                    const auto digit = OlRead(14.F, true, 1.2F);
                    PaddockDrawRun(
                        ImGui::GetWindowDrawList(), digit,
                        {at.x + 16.F - PaddockMeasure(digit, rank) * .5F, at.y + (row_height - digit.line) * .5F},
                        PaddockCol(place == 1 ? kOlCream : kOlInk), rank.data(), rank.data() + rank.size());
                    ImGui::Dummy({32.F, row_height});

                    ImGui::TableNextColumn();
                    const float name_width = ImGui::GetContentRegionAvail().x;
                    const auto caption =
                        PaddockEllipsize(OlRead(14.F, true, 1.2F), name, std::max(1.F, name_width - 14.F));
                    if (g_overall_focus == id) {
                        ImGui::SetKeyboardFocusHere();
                        g_overall_focus.clear();
                    }
                    if (OlButton((caption + "###player").c_str(), OlTone::Plain, name_width,
                                 caption != name ? name.c_str() : nullptr, row_height) &&
                        !selected)
                        SelectOverallPlayer(id);
                    StoreControlFocus();

                    ImGui::TableNextColumn();
                    cell(OverallPersonalBests(player), row_height, kOlInk, true);
                    ImGui::TableNextColumn();
                    cell(std::to_string(LeaderboardNumber(player, "records")), row_height, kOlInk, true);
                    ImGui::TableNextColumn();
                    cell(LeaderboardPoints(LeaderboardNumber(player, "total")), row_height, kOlCream, true);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
            if (g_overall_standings.value("tiedCutoff", false))
                OlParagraph("More players share the final rank shown.", 14.F, kOlSoft, inner);
        },
        13.F);
}

void DrawOverallScoreBreakdown(float width, const dkr::store::Json& player) {
    const auto points = player.find("points");
    const auto draw_category = [&](const char* label, const char* key, int cap) {
        const auto earned = points == player.end() ? 0 : LeaderboardNumber(*points, key);
        const std::string score = LeaderboardPoints(earned) + " / " + std::to_string(cap) + " pts";
        const auto caption = LocalisedUi::copy(label);
        const auto value_type = OlRead(14.F, false, 1.25F);
        const float value_width = PaddockMeasure(value_type, score) + 2.F;
        const bool side_by_side = width >= PaddockMeasure(OlRead(15.F, true, 1.25F), caption) + value_width + 10.F;
        OlParagraph(caption, 15.F, kOlInk, side_by_side ? width - value_width - 8.F : width, true, 1.25F);
        if (side_by_side)
            ImGui::SameLine(0.F, 8.F);
        OlParagraph(score, 14.F, kOlSoft, side_by_side ? value_width : width, false, 1.25F);
        const auto at = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        const float fraction = std::clamp(static_cast<float>(earned) / (1000.F * cap), 0.F, 1.F);
        PaddockFill(draw, at, {at.x + width, at.y + 6.F}, PaddockRound(3.F), PaddockCol(0x254658U));
        if (fraction > 0.F)
            PaddockFill(draw, at, {at.x + width * fraction, at.y + 6.F}, PaddockRound(3.F), PaddockCol(kOlGo));
        ImGui::Dummy({width, 6.F});
    };
    draw_category("Vanilla Tracks", "vanilla", 500);
    draw_category("Custom Tracks", "custom", 250);
    draw_category("Full Game", "fullGame", 250);
    OlParagraph(std::to_string(LeaderboardNumber(player, "podiums")) + " podiums  ·  " +
                    std::to_string(LeaderboardNumber(player, "coverage")) + " ranked events",
                14.F, kOlSoft, width);
}

std::optional<TrackQuery> DrawOverallPlayerRuns(float width) {
    if (!g_overall_profile.is_object()) {
        if (!g_overall_runs_requested && !DialogJobRunning())
            RefreshOverallPlayer(g_overall_player);
        OlParagraph(DialogJobRunning() ? "Loading ranked runs..." : "Ranked runs are unavailable.", 15.F, kOlSoft,
                    width);
        if (!DialogJobRunning() && OlButton("Try again"))
            RefreshOverallPlayer(g_overall_player);
        return {};
    }

    std::optional<TrackQuery> next;
    const auto events = g_overall_profile.find("events");
    if (events == g_overall_profile.end() || !events->is_array() || events->empty()) {
        OlParagraph("No ranked runs to show.", 15.F, kOlSoft, width);
        return {};
    }
    for (const auto& event : *events) {
        const auto kind = StoreText(event, "kind", 16);
        const auto id = StoreText(event, "id", 96);
        const auto vehicle = StoreText(event, "vehicle", 24);
        const bool full = kind == "full-game";
        const auto& race = event["race"];
        ImGui::PushID((kind + "/" + id + "/" + vehicle).c_str());
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(at, {at.x + width, at.y}, PaddockCol(kOlRing));
        ImGui::Dummy({width, 1.F});
        auto title = StoreText(event, "title", 120);
        if (!full)
            title += "  ·  " + LeaderboardVehicle(vehicle);
        OlParagraph(title, 16.F, kOlCream, width, true, 1.2F);
        auto times = LeaderboardTime(LeaderboardNumber(race, "ticks")) + "  |  #" +
                     std::to_string(LeaderboardNumber(race, "place"));
        if (const auto lap = event.find("lap"); lap != event.end())
            times += "  |  Best lap " + LeaderboardTime(LeaderboardNumber(*lap, "ticks")) + "  |  #" +
                     std::to_string(LeaderboardNumber(*lap, "place"));
        OlParagraph(times, 14.F, kOlSoft, width, false, 1.2F);
        if (OlButton("View Times", OlTone::Plain, 0.F, nullptr, 32.F))
            next = TrackQuery{id, full ? "" : vehicle, "", kind == "custom", false, 0, full};
        StoreControlFocus();
        if (const auto run = StoreText(race, "runId", 96); !run.empty()) {
            const OlDisabled launch(DialogJobRunning() || !CanLaunchLeaderboardReplay());
            float used_width = OlButtonWidth("View Times");
            const auto action = [&](const char* label, LeaderboardReplayAction action_type) {
                const float button_width = OlButtonWidth(label);
                if (used_width + 6.F + button_width <= width) {
                    ImGui::SameLine(0.F, 6.F);
                    used_width += 6.F + button_width;
                } else {
                    used_width = button_width;
                }
                if (OlButton(label, OlTone::Plain, std::min(width, button_width), nullptr, 32.F))
                    WatchLeaderboardReplay(run, false, action_type,
                                           action_type == LeaderboardReplayAction::RaceGhost
                                               ? StoreText(g_overall_profile["player"], "name", 60)
                                               : std::string{});
                StoreControlFocus();
            };
            action("Watch", LeaderboardReplayAction::Watch);
            if (!full)
                action("Race Ghost", LeaderboardReplayAction::RaceGhost);
        }
        ImGui::PopID();
    }
    {
        const OlDisabled busy(DialogJobRunning());
        if (const auto offset = LeaderboardNumber(g_overall_profile, "nextOffset"); offset && OlButton("More results"))
            RefreshOverallPlayer(g_overall_player, static_cast<unsigned>(offset));
    }
    if (const auto custom = g_overall_profile.find("customTracks");
        custom != g_overall_profile.end() && custom->is_array() && !custom->empty()) {
        if (OlButton(g_overall_show_custom ? "Hide scoring tracks" : "Custom scoring tracks"))
            g_overall_show_custom = !g_overall_show_custom;
        if (g_overall_show_custom) {
            for (const auto& track : *custom) {
                const auto title = StoreText(track, "title", 120);
                OlParagraph((title.empty() ? StoreText(track, "id", 96) : title) + "  |  " +
                                LeaderboardPoints(LeaderboardNumber(track, "points")) + " pts",
                            14.F, kOlSoft, width);
            }
        }
    }
    return next;
}

std::optional<TrackQuery> DrawOverallPlayerPanel(float width, const dkr::store::Json& player) {
    std::optional<TrackQuery> next;
    DrawOlCard(
        width, OlPanelLook{},
        [&](float inner) {
            const auto total = LeaderboardPoints(LeaderboardNumber(player, "total")) + " pts";
            const float total_width = std::ceil(PaddockMeasure(OlSignType(23.F, 1.3F), total)) + 4.F;
            const bool inline_total = inner >= total_width + 148.F;
            ImGui::BeginGroup();
            const float name_width = inline_total ? inner - total_width - 10.F : inner;
            OlParagraph("#" + std::to_string(LeaderboardNumber(player, "place")) + " overall", 14.F, kOlSoft,
                        name_width, false, 1.2F);
            OlHeading(StoreText(player, "name", 96), 23.F, kOlCream, name_width, 1.3F);
            ImGui::EndGroup();
            if (inline_total)
                ImGui::SameLine(0.F, 10.F);
            OlHeading(total, 23.F, kOlAmber, inline_total ? total_width : inner, 1.3F);

            const auto tab = [&](const char* label, OverallPlayerTab target) {
                if (OlButton(label, target == g_overall_tab ? OlTone::PlateStrong : OlTone::Plain,
                             std::min(inner, OlButtonWidth(label)), nullptr, 34.F))
                    g_overall_tab = target;
                StoreControlFocus();
            };
            tab("Score breakdown", OverallPlayerTab::Score);
            if (OlButtonWidth("Score breakdown") + OlButtonWidth("Ranked runs") + 6.F <= inner)
                ImGui::SameLine(0.F, 6.F);
            tab("Ranked runs", OverallPlayerTab::Runs);

            OlPanelLook count_look;
            count_look.fill = 0x123648;
            const auto stat = [&](float available, const char* label, const std::string& value) {
                DrawOlCard(
                    available, count_look,
                    [&](float inside) {
                        OlParagraph(label, 14.F, kOlSoft, inside, false, 1.2F);
                        OlHeading(value, 24.F, kOlCream, inside, 1.2F);
                    },
                    8.F);
            };
            const bool pair_stats = inner >= 230.F;
            const float stat_width = pair_stats ? (inner - 8.F) * .5F : inner;
            stat(stat_width, "Posted PBs", OverallPersonalBests(player));
            if (pair_stats)
                ImGui::SameLine(0.F, 8.F);
            stat(stat_width, "World Records", std::to_string(LeaderboardNumber(player, "records")));
            PaddockGap(3.F);

            if (g_overall_tab == OverallPlayerTab::Score)
                DrawOverallScoreBreakdown(inner, player);
            else
                next = DrawOverallPlayerRuns(inner);
        },
        13.F);
    return next;
}

std::optional<TrackQuery> DrawOverallRankings(float width) {
    static bool show_points = false;
    if (OlButton(show_points ? "Hide points explanation" : "How points work"))
        show_points = !show_points;
    StoreControlFocus();
    if (show_points) {
        DrawOlFeature(width, "How points work", nullptr, [&](float inner) {
            OlParagraph("Verified finishes earn points for placement (70%) and pace against the record (30%). "
                        "Solo boards count for 35%. Board weight grows to 100% at eight competitors.",
                        15.F, kOlSoft, inner);
            OlParagraph("Vanilla Tracks 500  |  Custom Tracks 250  |  Full Game 250", 16.F, kOlAmber, inner, true);
            OlParagraph("Track scores use 75% full race and 25% best lap. Your best 10 custom tracks earn up to "
                        "200 points, and the next 20 earn up to 50. Adding a track keeps your existing points.",
                        15.F, kOlSoft, inner);
        });
    }
    const auto players = g_overall_standings.find("players");
    if (players == g_overall_standings.end() || !players->is_array() || players->empty()) {
        OlParagraph(DialogJobRunning()                     ? "Loading top players..."
                    : players == g_overall_standings.end() ? "Refresh to load the overall rankings."
                                                           : "Top players appear after the first published run.",
                    15.F, kOlSoft, width);
        return {};
    }

    const auto selected = [&] {
        return std::ranges::find_if(*players,
                                    [](const auto& player) { return StoreText(player, "id", 96) == g_overall_player; });
    };
    if (selected() == players->end())
        SelectOverallPlayer(StoreText(players->front(), "id", 96));

    constexpr float gap = 12.F;
    const bool columns = width >= 732.F;
    const float standings_width = columns ? std::floor((width - gap) * .48F) : width;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 6.F});
    ImGui::BeginGroup();
    DrawOverallStandings(standings_width, *players);
    ImGui::EndGroup();
    if (columns)
        ImGui::SameLine(0.F, gap);
    else
        PaddockGap(8.F);

    std::optional<TrackQuery> next;
    if (const auto player = selected(); player != players->end()) {
        ImGui::BeginGroup();
        next = DrawOverallPlayerPanel(columns ? width - standings_width - gap : width, *player);
        ImGui::EndGroup();
    }
    ImGui::PopStyleVar();
    return next;
}
