constexpr const char* kLeaderboardWebsite = "https://positron-portal.com/dkr-r/#leaderboards";
constexpr const char* kLeaderboardPost = "https://positron-portal.com/dkr-r/#post-run";

std::int64_t LeaderboardNumber(const dkr::store::Json& data, const char* key) {
    const auto value = data.find(key);
    if (value == data.end() || !value->is_number_integer())
        return 0;
    return std::clamp(value->get<std::int64_t>(), std::int64_t(0), std::int64_t(216000));
}

std::string LeaderboardTime(std::int64_t ticks) {
    const auto millis = ticks * 1000 / 60;
    char clock[16]{};
    std::snprintf(clock, sizeof(clock), "%d:%02d.%03d", int(millis / 60000), int(millis / 1000 % 60),
                  int(millis % 1000));
    return clock;
}

void DrawLeaderboardRanking(float width, const dkr::store::Json& board) {
    const auto& event = board["board"];
    DrawOlFeature(width, StoreText(event, "title", 120).c_str(), nullptr, [&](float inner) {
        PaddockGap(6.F);
        OlParagraph(StoreText(event, "category", 80) + " · " + StoreText(event, "vehicle", 24), 15.F, kOlSoft, inner);
        PaddockGap(12.F);
        const auto& entries = board["entries"];
        if (entries.empty()) {
            OlParagraph("No verified runs have been published yet.", 15.F, kOlSoft, inner);
            return;
        }
        if (!ImGui::BeginTable("ranked-runs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, {inner, 0.F}))
            return;
        ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 70.F);
        ImGui::TableSetupColumn("Racer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 82.F);
        ImGui::TableSetupColumn("Replay", ImGuiTableColumnFlags_WidthFixed, 86.F);
        ImGui::TableHeadersRow();
        for (const auto& entry : entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::to_string(LeaderboardNumber(entry, "place")).c_str());
            ImGui::TableNextColumn();
            OlParagraph(StoreText(entry, "name", 60), 15.F, kOlInk, ImGui::GetContentRegionAvail().x);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(LeaderboardTime(LeaderboardNumber(entry, "ticks")).c_str());
            ImGui::TableNextColumn();
            const auto id = StoreText(entry, "id", 96);
            if (!id.empty() &&
                id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") ==
                    std::string::npos) {
                ImGui::PushID(id.c_str());
                if (OlButton("Download", OlTone::Plain, ImGui::GetContentRegionAvail().x, nullptr, 32.F))
                    SDL_OpenURL((std::string(dkr::online::Api) + "/v1/speedruns/runs/" + id + "/replay").c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    });
}

const dkr::store::Snapshot& LeaderboardLayoutFixture() {
    using Json = dkr::store::Json;
    static const auto fixture = [] {
        dkr::store::Snapshot value;
        value.speedrun_catalogue = std::make_shared<const Json>(Json::parse(R"json({
            "boards": [
                {"id":"ancient-lake-car","kind":"vanilla","title":"Ancient Lake","category":"Time Trial","vehicle":"car"},
                {"id":"crescent-island-car","kind":"vanilla","title":"Crescent Island","category":"Time Trial","vehicle":"car"},
                {"id":"frosty-village-car","kind":"vanilla","title":"Frosty Village","category":"Time Trial","vehicle":"car"}
            ],
            "customTracks": [
                {"id":"splashdown","title":"Splashdown Pass","version":"1.0","qualified":false},
                {"id":"sherbet-land","title":"Sherbet Land","version":"1.0","qualified":false}
            ], "nextCursor":""
        })json"));
        value.speedrun_board = std::make_shared<const Json>(Json::parse(R"json({
            "board":{"id":"ancient-lake-car","title":"Ancient Lake","category":"Time Trial","vehicle":"car"},
            "entries":[{"place":1,"name":"Example Racer","ticks":5300,"id":"fixture-one"},
                       {"place":2,"name":"Deck Racer","ticks":5350,"id":"fixture-two"}]
        })json"));
        return value;
    }();
    return fixture;
}

void DrawLeaderboardsPage(float width, bool entered) {
    static const std::vector<std::string> labels{"Vanilla Tracks", "Custom Tracks", "Full Game"};
    constexpr std::array kinds{"vanilla", "custom", "full-game"};
    static int category = 0;
    static std::string selected_board;
    auto& service = dkr::store::service();
    const auto published = service.view();
    const auto* snapshot = published.get();
    if (g_leaderboard_layout_check) {
        snapshot = &LeaderboardLayoutFixture();
        category = g_launcher_layout_step == 3 ? 1 : 0;
        selected_board = g_launcher_layout_step == 2 ? "ancient-lake-car" : "";
    } else if (entered && !snapshot->speedrun_catalogue) {
        service.load_leaderboards();
    }
    DrawPageHeading("LEADERBOARDS");
    PaddockGap(8.F);
    OlParagraph("Race for a personal best and see how other players set theirs.", 16.F, kOlSoft, width);
    PaddockGap(12.F);
    if (OlButton("Post a run", OlTone::Go))
        SDL_OpenURL(kLeaderboardPost);
    ImGui::SameLine();
    if (OlButton("Website"))
        SDL_OpenURL(kLeaderboardWebsite);
    ImGui::SameLine();
    {
        const OlDisabled busy(snapshot->speedrun_loading);
        if (OlButton("Refresh"))
            service.load_leaderboards(selected_board);
    }
    ImGui::SameLine();
    if (OlButton("Back to Play"))
        g_page_navigation_request = kPagePlay;
    PaddockGap(12.F);
    const int next = StoreTabLine(labels, category, width, "speedrun-category");
    if (next != category) {
        category = next;
        selected_board.clear();
    }
    PaddockGap(12.F);
    if (!snapshot->speedrun_error.empty())
        OlParagraph(snapshot->speedrun_error, 15.F, kOlError, width);
    if (!selected_board.empty()) {
        if (OlButton("All tracks") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            selected_board.clear();
        else if (snapshot->speedrun_board &&
                 StoreText((*snapshot->speedrun_board)["board"], "id", 96) == selected_board) {
            DrawLeaderboardRanking(width, *snapshot->speedrun_board);
            return;
        } else {
            if (snapshot->speedrun_loading)
                OlParagraph("Loading times…", 15.F, kOlSoft, width);
            return;
        }
    }
    if (!snapshot->speedrun_catalogue) {
        if (snapshot->speedrun_loading)
            OlParagraph("Loading leaderboards…", 15.F, kOlSoft, width);
        return;
    }

    const auto& catalogue = *snapshot->speedrun_catalogue;
    std::vector<const dkr::store::Json*> events;
    for (const auto& event : catalogue["boards"])
        if (StoreText(event, "kind", 16) == kinds[category])
            events.push_back(&event);
    if (events.empty()) {
        DrawOlCard(width, OlPanelLook{}, [&](float inner) {
            OlParagraph(category == 2 ? "Planned categories include Any%, 100% and All Trophy Races."
                                      : "Times will appear here once the first tracks pass replay qualification.",
                        15.F, kOlSoft, inner);
        });
        PaddockGap(12.F);
    } else {
        const int columns = std::clamp(int(width / 320.F), 1, 4);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {5.F, 5.F});
        if (ImGui::BeginTable("leaderboard-events", columns, ImGuiTableFlags_SizingStretchSame, {width, 0.F})) {
            for (const auto* event : events) {
                ImGui::TableNextColumn();
                const auto id = StoreText(*event, "id", 96);
                ImGui::PushID(id.c_str());
                DrawOlFeature(ImGui::GetContentRegionAvail().x, StoreText(*event, "title", 120).c_str(), nullptr,
                              [&](float inner) {
                                  PaddockGap(6.F);
                                  OlParagraph(StoreText(*event, "category", 80) + " · " +
                                                  StoreText(*event, "vehicle", 24),
                                              15.F, kOlSoft, inner);
                                  PaddockGap(8.F);
                                  const OlDisabled busy(snapshot->speedrun_loading);
                                  if (OlButton("View times")) {
                                      service.load_leaderboards(id);
                                      selected_board = id;
                                  }
                              });
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        PaddockGap(12.F);
    }
    if (category != 1)
        return;

    DrawOlFeature(width, "Tracks in the Mod Library", nullptr, [&](float inner) {
        for (const auto& track : catalogue["customTracks"]) {
            PaddockGap(8.F);
            OlParagraph(StoreText(track, "title", 120), 16.F, kOlInk, inner, true);
            const bool qualified = track.contains("qualified") && track["qualified"] == true;
            OlParagraph(StoreText(track, "version", 80) + (qualified ? " · Ranked" : " · Awaiting qualification"), 14.F,
                        kOlSoft, inner);
        }
        if (catalogue["customTracks"].empty())
            OlParagraph("No custom tracks are listed yet.", 15.F, kOlSoft, inner);
        if (const auto cursor = StoreText(catalogue, "nextCursor", 96); !cursor.empty()) {
            PaddockGap(8.F);
            const OlDisabled busy(snapshot->speedrun_loading);
            if (OlButton("More tracks"))
                service.load_leaderboards({}, cursor);
        }
    });
}
