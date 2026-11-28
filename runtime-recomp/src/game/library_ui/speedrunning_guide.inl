namespace speedrunning_guide {
enum Category { TopPlayers, Vanilla, Custom, FullGame, Runs };
enum RunTab { NewRun, Replays, MySubmittedRuns, ReviewRuns, Options };

struct Stop {
    Category category;
    RunTab tab;
    const char* title;
    const char* description;
};

constexpr std::array kStops{
    Stop{TopPlayers, NewRun, "Top Players",
         "See the overall standings. Select a player to see their points, personal bests and world records. "
         "How points work explains how runs count toward the standings."},
    Stop{Vanilla, NewRun, "Vanilla Tracks",
         "Browse the original tracks. Open View Times to see full-race and best-lap times by vehicle, "
         "watch a run or race a player's ghost."},
    Stop{Custom, NewRun, "Custom Tracks",
         "Browse community-made tracks. Open View Times for records and replays. "
         "Install the matching track from the Mod Library to race it."},
    Stop{FullGame, NewRun, "Full Game",
         "Browse categories like Any%, 100% and All Trophy Races. Open View Times to see the standings "
         "and record history for a category."},
    Stop{Runs, NewRun, "Runs / Run",
         "Pick a course, vehicle and racer under Vanilla Tracks or Custom Tracks, or pick a category under "
         "Challenge Runs. Select Start Run. When you finish, look for your result in Last Run."},
    Stop{Runs, Replays, "Runs / Replays",
         "Watch your saved runs, race a saved ghost or post an eligible run. You can sign in with Discord "
         "to link and post an eligible run you recorded while signed out."},
    Stop{Runs, MySubmittedRuns, "Runs / My Submitted Runs",
         "Check your posted runs, their review status and how many submissions you have left."},
    Stop{Runs, Options, "Runs / Options",
         "Adjust live splits, the HUD, ghosts, replay playback, finish notifications and how runs are saved."},
    Stop{Runs, ReviewRuns, "Runs / Review Runs",
         "Moderators and admins can watch and review submissions before they appear on the leaderboard."},
};

struct AccountStanding {
    enum class Status { Pending, Loading, Ready, Unavailable };
    std::string id;
    dkr::store::Json player;
    Status status = Status::Pending;
};

AccountStanding g_account_standing;

const dkr::store::Json& AccountRank(const std::string& id) {
    if (g_account_standing.id != id)
        g_account_standing = {id};
    if (const auto players = g_overall_standings.find("players");
        players != g_overall_standings.end() && players->is_array()) {
        const auto player =
            std::ranges::find_if(*players, [&](const auto& record) { return StoreText(record, "id", 96) == id; });
        if (player != players->end())
            return *player;
        const auto total_players = g_overall_standings.find("totalPlayers");
        if (total_players != g_overall_standings.end() && total_players->is_number_unsigned() &&
            total_players->get<std::size_t>() <= players->size()) {
            g_account_standing.player = nullptr;
            g_account_standing.status = AccountStanding::Status::Ready;
        }
    }
    if (g_account_standing.status == AccountStanding::Status::Pending && !DialogJobRunning()) {
        if (StartDialogJob([id]() -> std::function<void()> {
                std::optional<dkr::store::Json> summary;
                try {
                    summary = dkr::store::service().overall_player_summary(id);
                } catch (const std::exception&) {
                    // Keep this card usable when the leaderboard cannot supply a rank.
                }
                return [id, summary = std::move(summary)]() mutable {
                    if (g_account_standing.id == id && g_account_standing.status == AccountStanding::Status::Loading) {
                        g_account_standing.status =
                            summary ? AccountStanding::Status::Ready : AccountStanding::Status::Unavailable;
                        g_account_standing.player = summary ? std::move(*summary) : dkr::store::Json{};
                    }
                };
            }))
            g_account_standing.status = AccountStanding::Status::Loading;
    }
    return g_account_standing.player;
}

void DrawAccount(float width, const dkr::store::Snapshot& snapshot) {
    const auto id = StoreText(snapshot.user, "id", 96);
    OlPanelLook look;
    look.fill = 0x123648;
    DrawOlCard(
        width, look,
        [&](float inner) {
            if (id.empty()) {
                g_account_standing = {};
                if (snapshot.signing_in) {
                    OlParagraph(snapshot.status.empty() ? "Waiting for Discord sign-in..." : snapshot.status, 14.F,
                                kOlSoft, inner, false, 1.25F);
                    if (OlButton("Cancel sign-in", OlTone::Plain, std::min(inner, OlButtonWidth("Cancel sign-in"))))
                        dkr::store::service().cancel_login();
                } else {
                    OlParagraph("Sign in to post your runs.", 15.F, kOlInk, inner, true, 1.25F);
                    if (OlButton("Sign in with Discord", OlTone::Go,
                                 std::min(inner, OlButtonWidth("Sign in with Discord", OlTone::Go))))
                        dkr::store::service().login();
                }
                StoreControlFocus();
                return;
            }

            auto name = StoreText(snapshot.user, "displayName", 96);
            const auto discord = snapshot.user.find("discordProfile");
            if (name.empty() && discord != snapshot.user.end())
                name = StoreText(*discord, "displayName", 96);
            if (name.empty())
                name = "Player";
            const auto profile = snapshot.user.find("launcherProfile");
            const auto avatar = profile != snapshot.user.end() && profile->contains("avatarUrl")
                                    ? StoreText(*profile, "avatarUrl", 180)
                                : discord != snapshot.user.end() ? StoreText(*discord, "avatarUrl", 180)
                                                                 : std::string{};
            const auto& standing = AccountRank(id);
            std::string rank;
            if (standing.is_object())
                rank = "#" + std::to_string(LeaderboardNumber(standing, "place")) + " overall";
            else if (g_account_standing.status == AccountStanding::Status::Ready)
                rank = "No published runs yet";
            else if (g_account_standing.status == AccountStanding::Status::Unavailable)
                rank = "Leaderboard rank unavailable";
            else
                rank = "Loading leaderboard rank...";

            constexpr float avatar_size = 42.F, gap = 10.F;
            const auto at = ImGui::GetCursorScreenPos();
            DrawOlProfileAvatar(ImGui::GetWindowDrawList(), at, avatar_size, name, avatar, false);
            ImGui::Dummy({avatar_size, avatar_size});
            ImGui::SameLine(0.F, gap);
            ImGui::BeginGroup();
            OlParagraph(name, 16.F, kOlCream, inner - avatar_size - gap, true, 1.25F);
            OlParagraph(rank, 14.F, standing.is_object() ? kOlAmber : kOlSoft, inner - avatar_size - gap, false, 1.25F);
            ImGui::EndGroup();
            if (standing.is_object()) {
                OlParagraph(LeaderboardPoints(LeaderboardNumber(standing, "total")) + " pts  ·  " +
                                OverallPersonalBests(standing) + " PBs  ·  " +
                                std::to_string(LeaderboardNumber(standing, "records")) + " WRs",
                            14.F, kOlSoft, inner, false, 1.25F);
            }
        },
        12.F);
}

bool DrawPanel(float width, bool can_review, const dkr::store::Snapshot& snapshot) {
    static bool expanded = false;
    static bool show_points = false;
    bool start = false;
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.F, 8.F});
        constexpr float account_gap = 16.F;
        const bool inline_account = inner >= 580.F;
        const float account_width = inline_account ? std::clamp(inner * .32F, 230.F, 325.F) : inner;
        const float guide_width = inline_account ? inner - account_width - account_gap : inner;
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        OlHeading("Getting started", 24.F, kOlCream, guide_width, 1.25F);
        OlParagraph("Open Runs > Run, choose what to race and select Start Run. After you finish, eligible "
                    "personal bests can be posted from Last Run or Replays.",
                    15.F, kOlInk, guide_width);
        const char* details = expanded ? "Hide tab guide" : "Show tab guide";
        const char* points = show_points ? "Hide points explanation" : "How points work";
        constexpr float gap = 8.F;
        const float tour_width = OlButtonWidth("Start guided tour", OlTone::Go);
        const float detail_width = OlButtonWidth(details);
        const float points_width = OlButtonWidth(points);
        start = OlButton("Start guided tour", OlTone::Go, std::min(guide_width, tour_width));
        StoreControlFocus();
        const bool inline_details = tour_width + gap + detail_width <= guide_width;
        if (inline_details)
            ImGui::SameLine(0.F, gap);
        if (OlButton(details, OlTone::Plain, std::min(guide_width, detail_width)))
            expanded = !expanded;
        StoreControlFocus();
        if (detail_width + gap + points_width + (inline_details ? tour_width + gap : 0.F) <= guide_width)
            ImGui::SameLine(0.F, gap);
        if (OlButton(points, OlTone::Plain, std::min(guide_width, points_width)))
            show_points = !show_points;
        StoreControlFocus();
        ImGui::EndGroup();
        const float guide_bottom = ImGui::GetItemRectMax().y;
        if (inline_account)
            ImGui::SetCursorScreenPos({origin.x + guide_width + account_gap, origin.y});
        DrawAccount(account_width, snapshot);
        const float row_bottom = std::max(guide_bottom, ImGui::GetItemRectMax().y);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({inner, row_bottom - origin.y});
        if (show_points) {
            PaddockGap(8.F);
            DrawOverallPointsExplanation(inner);
        }
        ImGui::PopStyleVar();
        if (!expanded)
            return;

        PaddockGap(8.F);
        constexpr float column_gap = 22.F;
        const bool columns = inner >= 670.F;
        const float column = columns ? (inner - column_gap) / 2.F : inner;
        const auto describe = [&](const Stop& stop) {
            OlParagraph(stop.title, 15.F, kOlAmber, column, true);
            OlParagraph(stop.description, 15.F, kOlSoft, column);
            PaddockGap(6.F);
        };
        ImGui::BeginGroup();
        for (const auto& stop : kStops)
            if (stop.category != Runs)
                describe(stop);
        ImGui::EndGroup();
        if (columns)
            ImGui::SameLine(0.F, column_gap);
        ImGui::BeginGroup();
        for (const auto& stop : kStops)
            if (stop.category == Runs && (stop.tab != ReviewRuns || can_review))
                describe(stop);
        ImGui::EndGroup();
    });
    PaddockGap(8.F);
    return start;
}

int DrawTour(float width, int step, int steps, bool focus_next) {
    int next = step;
    const auto& stop = kStops[static_cast<std::size_t>(step)];
    DrawOlFeature(width, stop.title, nullptr, [&](float inner) {
        OlParagraph("Guided tour " + std::to_string(step + 1) + " of " + std::to_string(steps), 14.F, kOlAmber, inner);
        PaddockGap(4.F);
        OlParagraph(stop.description, 15.F, kOlInk, inner);
        if (stop.tab == NewRun && stop.category == Runs) {
            PaddockGap(4.F);
            OlParagraph("Sign in with Discord to post. If your run qualifies, choose Post to Leaderboard under "
                        "Last Run or find the recording in Replays.",
                        15.F, kOlSoft, inner);
        }
        PaddockGap(8.F);
        if (step > 0) {
            if (OlButton("Back###speedrun-tour-back"))
                next = step - 1;
            StoreControlFocus();
            ImGui::SameLine(0.F, 8.F);
        }
        const bool last = step + 1 == steps;
        if (focus_next)
            ImGui::SetKeyboardFocusHere();
        if (OlButton(last ? "Done###speedrun-tour-next" : "Next###speedrun-tour-next", OlTone::Go))
            next = last ? -1 : step + 1;
        StoreControlFocus();
        if (!last) {
            ImGui::SameLine(0.F, 8.F);
            if (OlButton("End tour"))
                next = -1;
            StoreControlFocus();
        }
    });
    PaddockGap(8.F);
    return next;
}
} // namespace speedrunning_guide
