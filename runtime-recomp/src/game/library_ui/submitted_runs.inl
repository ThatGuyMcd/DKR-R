struct SubmissionQuota {
    std::int64_t remaining;
    std::int64_t limit;
    std::int64_t next_slot;
};

std::optional<SubmissionQuota> ReadSubmissionQuota(const dkr::store::Json& listing) {
    const auto quota = listing.find("submissionQuota");
    if (quota == listing.end() || !quota->is_object())
        return {};
    const auto limit = LeaderboardNumber(*quota, "maxRuns");
    const auto observed = RecordUnix(*quota, "observedAt");
    const auto releases = quota->find("releasesAt");
    if (limit <= 0 || observed <= 0 || releases == quota->end() || !releases->is_array() ||
        releases->size() > static_cast<std::size_t>(limit))
        return {};
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_ranked_runs_received)
            .count();
    const auto now = observed + elapsed;
    SubmissionQuota slots{limit, limit, 0};
    for (const auto& release : *releases) {
        if (!release.is_number_integer())
            continue;
        const auto stamp = release.get<std::int64_t>();
        if (stamp <= now)
            continue;
        --slots.remaining;
        if (!slots.next_slot || stamp < slots.next_slot)
            slots.next_slot = stamp;
    }
    return slots;
}

void DrawSubmissionQuota(float width, const SubmissionQuota& quota) {
    const auto time = quota.next_slot > 0
                          ? LocalReplayDate(static_cast<std::time_t>(quota.next_slot), "%d %b, %H:%M:%S")
                          : std::string{};
    const bool has_next_slot = !time.empty();
    constexpr float gap = 20.F;
    const float meter_width = std::min(width, 294.F);
    const bool inline_next = has_next_slot && width >= meter_width + gap + 215.F;
    ImGui::BeginGroup();
    OlHeading(std::to_string(quota.remaining) + " / " + std::to_string(quota.limit), 27.F, kOlCream, meter_width, 1.2F);
    OlParagraph("Submissions left", 14.F, kOlSoft, meter_width, true, 1.25F);
    const auto meter = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float fraction = static_cast<float>(quota.remaining) / static_cast<float>(quota.limit);
    PaddockFill(draw, meter, {meter.x + meter_width, meter.y + 6.F}, PaddockRound(3.F), PaddockCol(0x254658U));
    if (quota.remaining > 0)
        PaddockFill(draw, meter, {meter.x + meter_width * fraction, meter.y + 6.F}, PaddockRound(3.F),
                    PaddockCol(kOlGo));
    ImGui::Dummy({meter_width, 6.F});
    ImGui::EndGroup();
    if (has_next_slot) {
        if (inline_next)
            ImGui::SameLine(0.F, gap);
        const float next_width = inline_next ? width - meter_width - gap : width;
        ImGui::BeginGroup();
        OlParagraph("Next slot opens", 14.F, kOlSoft, next_width, true, 1.2F);
        OlParagraph(time + " (local time)", 16.F, kOlInk, next_width, true, 1.2F);
        ImGui::EndGroup();
    }
}

struct SubmittedRunStatus {
    std::string_view id;
    const char* label;
    PaddockTone tone;
};

constexpr std::array kSubmittedRunStatuses{
    SubmittedRunStatus{"uploading", "Upload incomplete", PaddockTone::Warning},
    SubmittedRunStatus{"queued", "Waiting for verification", PaddockTone::Neutral},
    SubmittedRunStatus{"verified", "Awaiting review", PaddockTone::Warning},
    SubmittedRunStatus{"published", "Published", PaddockTone::On},
    SubmittedRunStatus{"rejected", "Rejected", PaddockTone::Warning},
};

void DrawSubmittedRun(float width, const dkr::store::Json& run, float status_width, float watch_width) {
    const auto id = StoreText(run, "id", 96);
    if (id.empty())
        return;

    const auto state = StoreText(run, "state", 24);
    const auto badge = std::ranges::find(kSubmittedRunStatuses, state, &SubmittedRunStatus::id);
    const auto label =
        dkr::localisation::source(badge == kSubmittedRunStatuses.end() ? "Status unavailable" : badge->label);
    const auto tone = badge == kSubmittedRunStatuses.end() ? PaddockTone::Neutral : badge->tone;
    const auto verified_ticks = LeaderboardNumber(run, "ticks");
    const auto ticks = verified_ticks ? verified_ticks : LeaderboardNumber(run, "claimedTicks");
    const auto time = ticks > 0 ? LeaderboardTime(ticks) : std::string("—");
    const auto vehicle = StoreText(run, "vehicle", 24);
    std::string metadata =
        vehicle == "mixed" ? std::string(dkr::localisation::source("Full Game")) : LeaderboardVehicle(vehicle);
    const auto submitted_at = RecordUnix(run, "createdAt");
    const auto date = submitted_at > 0 ? LocalReplayDate(static_cast<std::time_t>(submitted_at), "%d %b %Y") : "";
    for (const auto& part :
         {date, run.value("linkedLocal", false) ? std::string(dkr::localisation::source("Linked local run"))
                                                : std::string{}}) {
        if (!part.empty()) {
            if (!metadata.empty())
                metadata += "  ·  ";
            metadata += part;
        }
    }
    const auto title = StoreText(run, "title", 120);
    OlPanelLook look;
    look.ring_width = 1.F;
    ImGui::PushID(id.c_str());
    DrawOlCard(
        width, look,
        [&](float inner) {
            constexpr float gap = 10.F;
            const float time_width =
                std::min(inner, std::max(104.F, PaddockMeasure(OlSignType(19.F, 1.25F), time) + 4.F));
            const float action_width = time_width + status_width + watch_width + 2.F * gap;
            const bool inline_actions = inner >= action_width + gap + 216.F;
            const float title_width = inline_actions ? inner - action_width - gap : inner;
            ImGui::BeginGroup();
            OlHeading(title, 20.F, kOlCream, title_width, 1.25F);
            if (!metadata.empty())
                OlParagraph(metadata, 14.F, kOlSoft, title_width, false, 1.2F);
            ImGui::EndGroup();
            if (inline_actions)
                ImGui::SameLine(0.F, gap);
            ImGui::BeginGroup();
            OlHeading(time, 19.F, kOlInk, time_width, 1.25F);
            if (!verified_ticks && ticks > 0)
                OlParagraph("Recorded time", 13.F, kOlSoft, time_width, false, 1.2F);
            ImGui::EndGroup();

            const auto badge_label =
                PaddockEllipsize(PaddockReading(11.F, true, 1.5F), label, std::max(1.F, inner - 27.F));
            const auto size = PaddockBadgeSize(badge_label);
            const float badge_width = size.x;
            const bool inline_badge = time_width + gap + (inline_actions ? status_width : badge_width) <= inner;
            if (inline_badge)
                ImGui::SameLine(0.F, gap);
            const auto origin = ImGui::GetCursorScreenPos();
            PaddockBadge(ImGui::GetWindowDrawList(), {origin.x, origin.y + (34.F - size.y) * .5F}, badge_label, tone);
            ImGui::Dummy({inline_actions ? status_width : std::min(badge_width, inner), 34.F});

            if (state != "uploading") {
                const float used =
                    (inline_badge ? time_width + gap : 0.F) + (inline_actions ? status_width : badge_width);
                if (used + gap + watch_width <= inner)
                    ImGui::SameLine(0.F, gap);
                const OlDisabled launch(DialogJobRunning() || !CanLaunchLeaderboardReplay());
                if (OlButton("Watch replay", OlTone::Plain, std::min(watch_width, inner), nullptr, 34.F))
                    WatchLeaderboardReplay(id, true);
                StoreControlFocus();
            }
            for (const auto* field : {"notes", "reason"})
                if (const auto value = StoreText(run, field, 600); !value.empty())
                    OlParagraph(value, 14.F, kOlSoft, inner, false, 1.25F);
        },
        12.F);
    ImGui::PopID();
}

void DrawSubmittedRuns(float width) {
    const auto owner = RankedRunsOwner();
    const auto rows = g_ranked_runs.find("runs");
    const bool loaded = !owner.empty() && g_ranked_runs_owner == owner && !g_ranked_runs_review &&
                        rows != g_ranked_runs.end() && rows->is_array();
    const auto quota = loaded ? ReadSubmissionQuota(g_ranked_runs) : std::nullopt;

    const auto spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing.x, 7.F});
    DrawOlCard(
        width, OlPanelLook{},
        [&](float inner) {
            OlHeading("My Submitted Runs", 24.F, kOlCream, inner);
            if (owner.empty()) {
                OlParagraph("Sign in with Discord to see your submitted runs.", 15.F, kOlSoft, inner);
                const OlDisabled busy(DialogJobRunning());
                if (OlButton("Sign in with Discord", OlTone::Go))
                    dkr::store::service().login();
                StoreControlFocus();
                return;
            }
            if (quota)
                DrawSubmissionQuota(inner, *quota);
            if (loaded) {
                OlParagraph(
                    "The leaderboard host prepares videos for review. You can close DKR-R once your upload finishes.",
                    14.F, kOlSoft, inner, false, 1.25F);
                if (rows->empty())
                    OlParagraph("You haven't submitted a run yet.", 15.F, kOlInk, inner);
            } else {
                OlParagraph(DialogJobRunning() ? "Loading submitted runs..." : "Use Refresh to check for runs.", 15.F,
                            kOlSoft, inner);
            }
        },
        14.F);
    if (loaded && !rows->empty()) {
        float status_width = PaddockBadgeSize(dkr::localisation::source("Status unavailable")).x;
        for (const auto& status : kSubmittedRunStatuses)
            status_width = std::max(status_width, PaddockBadgeSize(dkr::localisation::source(status.label)).x);
        const float watch_width = OlButtonWidth("Watch replay");
        for (const auto& run : *rows)
            DrawSubmittedRun(width, run, status_width, watch_width);
    }
    ImGui::PopStyleVar();
}
