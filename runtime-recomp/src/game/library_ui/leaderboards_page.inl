constexpr const char* kLeaderboardWebsite = "https://positron-portal.com/dkr-r/#leaderboards";

std::int64_t LeaderboardNumber(const dkr::store::Json& data, const char* key) {
    const auto value = data.find(key);
    if (value == data.end() || !value->is_number_integer())
        return 0;
    return std::clamp(value->get<std::int64_t>(), std::int64_t(0),
                      std::int64_t(dkr::runtime::time_trial::kAdventureMaxUpdates * 2));
}

std::string LeaderboardTime(std::int64_t ticks) {
    const auto millis = ticks * 1000 / 60;
    char clock[16]{};
    std::snprintf(clock, sizeof(clock), "%d:%02d.%03d", int(millis / 60000), int(millis / 1000 % 60),
                  int(millis % 1000));
    return clock;
}

namespace trial = dkr::runtime::time_trial;
#include "speedrunning_options.inl"

struct LapReference {
    std::array<unsigned, trial::kMaximumLapCount> personal{}, best_segments{};
    unsigned world_best_lap = 0;
};

using AdventureReference = std::map<std::string, std::int64_t, std::less<>>;
using AdventureSegments = std::map<std::pair<std::string, std::string>, std::int64_t>;

struct TimeTrialRequest {
    trial::Mode mode;
    std::filesystem::path replay;
    std::optional<trial::Attempt> attempt = std::nullopt;
    unsigned racer = trial::kDiddy;
    const trial::Course* course = &trial::kCourses.front();
    trial::Vehicle vehicle = trial::Vehicle::Car;
    trial::Adventure adventure = trial::Adventure::None;
    std::string track;
    std::filesystem::path ghost;
    std::string ghost_name;
    LapReference lap_reference;
    unsigned prior_best = 0;
    AdventureReference adventure_pb, adventure_record;
    AdventureSegments adventure_segments;
};
std::optional<TimeTrialRequest> g_time_trial_request;
std::optional<TimeTrialRequest> g_previous_time_trial_request;
std::optional<TimeTrialRequest> g_local_run_confirmation;
std::filesystem::path g_ranked_playback_file;
unsigned g_run_prior_best = 0;
bool g_open_speedrunning_result = false;
LapReference g_run_lap_reference;
AdventureReference g_adventure_pb_splits, g_adventure_record_splits;
AdventureSegments g_adventure_segments;

void DeleteRankedPlayback(const std::filesystem::path& file) {
    if (file.empty())
        return;
    std::error_code error;
    std::filesystem::remove(file, error);
    std::filesystem::remove(trial::portable_ghost_file(file), error);
}

void QueueTimeTrialRequest(TimeTrialRequest request, bool signed_in) {
    if (signed_in && request.mode == trial::Mode::Record && !request.attempt)
        g_local_run_confirmation = std::move(request);
    else
        g_time_trial_request = std::move(request);
}

void DrawLocalRunConfirmation() {
    if (!g_local_run_confirmation)
        return;
    constexpr const char* title = "Start a local run?";
    if (!ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    if (!BeginPaddedModal(title, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto catalogue = dkr::store::service().view()->speedrun_catalogue;
    OlParagraph(catalogue && catalogue->value("canClaimLocal", false)
                    ? "This run will be saved locally. You can link an eligible run to your Discord account later."
                    : "This run will be saved locally. Sign in before starting a run to post it to the leaderboard.",
                15.F, kOlInk, 350.F);
    if (OlButton("Cancel")) {
        if (!g_ranked_playback_file.empty() && g_local_run_confirmation->ghost == g_ranked_playback_file)
            DeleteRankedPlayback(std::exchange(g_ranked_playback_file, {}));
        g_local_run_confirmation.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (OlButton("Start local run", OlTone::Go)) {
        g_time_trial_request = std::exchange(g_local_run_confirmation, std::nullopt);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string g_time_trial_message;
dkr::store::Json g_ranked_runs = nullptr;
std::string g_ranked_runs_owner;
bool g_ranked_runs_review = false;
std::chrono::steady_clock::time_point g_ranked_runs_received;

bool LaunchTimeTrial(TimeTrialRequest& request) {
    const auto start = [&] {
        if (!request.ghost.empty())
            return trial::start_ghost(request.ghost, request.replay, g_time_trial_message, request.racer, request.mode,
                                      {}, request.attempt ? &*request.attempt : nullptr, request.ghost_name);
        return trial::start(request.mode, request.replay, g_time_trial_message,
                            request.attempt ? &*request.attempt : nullptr, request.racer, *request.course,
                            request.vehicle, request.adventure, request.track);
    };
    if (!start()) {
        if (!request.ghost.empty() || request.mode != trial::Mode::Record || request.track.empty() || !request.attempt)
            return false;
        // An installed custom track can differ from the version listed online.
        request.attempt.reset();
        if (!start())
            return false;
    }
    g_run_lap_reference = request.lap_reference;
    g_run_prior_best = request.prior_best;
    g_adventure_pb_splits = request.adventure_pb;
    g_adventure_record_splits = request.adventure_record;
    g_adventure_segments = request.adventure_segments;
    trial::set_finish_personal_best(request.prior_best);
    return true;
}

std::string RankedRunsOwner() {
    const auto file = g_config_directory / "ranked-local.json";
    static std::mutex local_owner_mutex;
    std::scoped_lock lock(local_owner_mutex);
    static std::filesystem::path prior_file;
    static std::string local_owner;
    if (file != prior_file) {
        prior_file = file;
        local_owner.clear();
        try {
            const auto local = dkr::store::Json::parse(dkr::updates::read_text(file, 4096));
            const auto port = LeaderboardNumber(local, "port");
            if (port > 1023 && port <= 65535 && StoreText(local, "token", 43).size() == 43)
                local_owner = "local-ranked-" + std::to_string(port);
        } catch (const std::exception&) {
            // Ordinary profiles use the signed-in Mod Library account.
        }
    }
    return local_owner.empty() ? StoreText(dkr::store::service().view()->user, "id", 96) : local_owner;
}

std::filesystem::path NewTimeTrialFile(const std::filesystem::path& config, const std::string& prefix) {
    auto directory = config / "speedruns";
    std::filesystem::create_directories(directory);
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    return directory / (prefix + "-" + std::to_string(stamp) + ".dkrreplay");
}

void RankedJob(std::function<std::function<void()>()> job) {
    g_time_trial_message.clear();
    StartDialogJob([job = std::move(job)]() -> std::function<void()> {
        try {
            return job();
        } catch (const std::exception& failure) {
            return [message = std::string(failure.what())] { g_time_trial_message = message; };
        }
    });
}

#include "replay_library.inl"

void ApplyReplayRetention(const trial::Result& result) {
    const auto library = g_config_directory / "speedruns";
    if (!RunPreference(RunOption::Retention) || !result.complete || result.replay ||
        result.file.parent_path() != library)
        return;
    try {
        const auto replays = ReadLocalReplayLibrary(library);
        const auto saved = std::ranges::find(replays, result.file, &LocalReplay::file);
        if (saved != replays.end() && saved->local_capture && !saved->personal_best && !saved->beat_ghost &&
            saved->posted_owner.empty() &&
            !dkr::library_ui::can_post_replay_attempt(saved->has_attempt, saved->practice, false))
            RemoveLocalReplay(saved->file);
    } catch (const std::exception&) {
        // Keep the recording when the library cannot classify it.
    }
}

std::filesystem::path TimeTrialRom(const TimeTrialRequest& request, const std::filesystem::path& selected,
                                   const std::vector<RomCatalogEntry>& catalogue) {
    namespace rom = dkr::runtime::rom;
    const auto& evidence = request.ghost.empty() ? request.replay : request.ghost;
    if (request.mode == trial::Mode::Record && request.ghost.empty())
        return selected;
    try {
        const unsigned revision = ReadReplaySummary(evidence).at("rom").get<unsigned>();
        const auto required = revision == 77 ? rom::Revision::UsV77 : rom::Revision::UsV80;
        if (rom::inspect(selected).revision == required)
            return selected;
        const auto found = std::ranges::find_if(
            catalogue, [&](const auto& entry) { return rom::inspect(entry.path).revision == required; });
        if (found != catalogue.end())
            return found->path;
        g_time_trial_message = revision == 77 ? "Add your US v1.0 ROM in Play to watch this replay."
                                              : "Add your US v1.1 ROM in Play to watch this replay.";
    } catch (const std::exception& error) {
        g_time_trial_message = error.what();
    }
    return {};
}

bool CanLaunchLeaderboardReplay();

double g_ranked_podium_started = -1;

void DrawRankedPodium() {
    const auto state = trial::progress();
    if (!state.finish_update || state.replay || !state.clock || !g_previous_time_trial_request ||
        g_previous_time_trial_request->mode != trial::Mode::Record)
        return;
    const auto& attempt = g_previous_time_trial_request->attempt;
    const bool pb = !g_run_prior_best || state.clock < g_run_prior_best;
    const bool celebration = speed::celebrates(state.podium_place, pb);
    if (!celebration && !RunPreference(RunOption::FinishSummary))
        return;
    const double now = ImGui::GetTime();
    if (g_ranked_podium_started < 0) {
        g_ranked_podium_started = now;
        if (celebration)
            AchievementChime(RunPreference(RunOption::CueVolume) * .01F);
    }
    if (now - g_ranked_podium_started >= (celebration ? 4.5 : 2.7))
        return;

    const auto* view = ImGui::GetMainViewport();
    const bool record = state.podium_place == 1;
    const std::string heading = !celebration         ? "FINISH"
                                : record             ? "WORLD RECORD RUN!"
                                : state.podium_place ? "PODIUM FINISH  #" + std::to_string(state.podium_place)
                                                     : "PERSONAL BEST!";
    const auto art = record || !state.podium_place ? 0xFFCE6C : state.podium_place == 2 ? 0xCED9E9 : 0xF0A477;
    const auto ink = PaddockCol(art);
    const std::string time = RunClock(state.clock);
    const std::string detail =
        attempt && attempt->finish_targets ? attempt->finish_targets->title + "  |  " + time : time;
    constexpr const char* basis = "Compared with the leaderboard at the start of this run";
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!celebration || RunPreference(RunOption::Celebration) == 1) {
        const float width = std::min(view->WorkSize.x - 32.F, 350.F);
        ImGui::SetNextWindowPos({view->WorkPos.x + view->WorkSize.x * .5F, view->WorkPos.y + 24.F}, 0, {.5F, 0.F});
        ImGui::SetNextWindowSize({width, 0.F});
        ImGui::SetNextWindowBgAlpha(.89F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.F, 10.F});
        if (ImGui::Begin("##speedrunning-finish", nullptr, flags | ImGuiWindowFlags_AlwaysAutoResize)) {
            OlHeading(heading, 22.F, art, width - 28.F);
            OlParagraph(detail, 16.F, kOlInk, width - 28.F);
            if (celebration && state.podium_place)
                OlParagraph(basis, 13.F, kOlSoft, width - 28.F);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::SetNextWindowPos(view->Pos);
    ImGui::SetNextWindowSize(view->Size);
    ImGui::SetNextWindowBgAlpha(.93F);
    ImGui::Begin("##ranked-podium", nullptr, flags);
    const auto size = ImGui::GetWindowSize();
    const auto offset = ImGui::GetWindowPos();
    auto* draw = ImGui::GetWindowDrawList();
    const bool motion =
        !RunPreference(RunOption::ReducedMotion) && !dkr::runtime::achievements::view()->preferences.reduced_motion;
    for (unsigned i = 0; i < (record ? 65U : 32U); ++i) {
        const auto x = float((i * 37U) % 97U) / 97.F;
        const auto phase = float((i * 23U) % 113U) / 113.F;
        const float y = motion ? std::fmod(phase + float(now) * .18F, 1.F) : phase;
        draw->AddCircleFilled({offset.x + x * size.x, offset.y + y * size.y}, 1.6F + float(i % 4), ink, 4);
    }
    const auto title = PaddockSign(std::min(62.F, size.x * .074F));
    const auto ext = title.font->CalcTextSizeA(title.size, FLT_MAX, 0.F, heading.c_str());
    draw->AddText(title.font, title.size, {offset.x + (size.x - ext.x) * .5F, offset.y + size.y * .28F}, ink,
                  heading.c_str());
    ImGui::SetCursorPos({std::max(14.F, (size.x - ImGui::CalcTextSize(detail.c_str()).x) * .5F), size.y * .46F});
    ImGui::TextUnformatted(detail.c_str());
    if (state.podium_place) {
        ImGui::SetCursorPos({std::max(14.F, (size.x - ImGui::CalcTextSize(basis).x) * .5F), size.y * .62F});
        ImGui::TextDisabled("%s", basis);
    }
    ImGui::End();
}

std::optional<trial::FinishTargets> ReadFinishTargets(const dkr::store::Json& reply) {
    const auto found = reply.find("finishTargets");
    if (found == reply.end() || !found->is_object())
        return {};
    trial::FinishTargets targets{StoreText(*found, "title", 120), unsigned(LeaderboardNumber(*found, "personal"))};
    const auto times = found->find("rivals");
    if (targets.title.empty() || times == found->end() || !times->is_array() || times->size() > targets.rivals.size())
        return {};
    for (std::size_t index = 0; index < times->size(); ++index) {
        const auto& time = (*times)[index];
        if (!time.is_number_integer())
            return {};
        const auto ticks = time.get<std::int64_t>();
        if (ticks <= 0 || ticks > trial::kAdventureMaxUpdates * 2)
            return {};
        targets.rivals[index] = unsigned(ticks);
    }
    return targets;
}

std::string TrackAttemptBoard(const dkr::store::Json* catalogue, std::string_view id, const trial::Course* course,
                              trial::Vehicle vehicle, unsigned racer) {
    if (course)
        return trial::board_for(*course, vehicle, racer);
    if (!catalogue || !catalogue->contains("boards") || !catalogue->value("canSubmitCustom", false))
        return {};
    for (const auto& entry : catalogue->at("boards")) {
        if (!entry.value("ranked", false))
            continue;
        if (StoreText(entry, "trackId", 96) == id &&
            StoreText(entry, "vehicle", 24) == trial::kVehicleIds[static_cast<unsigned>(vehicle)] &&
            StoreText(entry, "racer", 32) == trial::kRacers[racer].name)
            return StoreText(entry, "id", 96);
    }
    return {};
}

std::optional<trial::Attempt> RequestTrackAttempt(const std::string& board, std::string_view id,
                                                  const trial::Course* course, trial::Vehicle vehicle, unsigned racer) {
    if (board.empty())
        return {};
    try {
        const auto reply = dkr::store::service().ranked_attempt(board);
        const auto board_id = StoreText(reply, "board", 96);
        const auto engine = StoreText(reply, "engine", 64);
        const auto ticket = StoreText(reply, "ticket", 160);
        if (board_id != board)
            return {};
        auto attempt =
            course ? trial::parse_attempt(board_id, engine, ticket, *course, vehicle, racer)
                   : trial::parse_custom_attempt(board_id, engine, ticket, StoreText(reply, "releaseId", 96),
                                                 StoreText(reply, "releaseHash", 64), StoreText(reply, "trackId", 96),
                                                 StoreText(reply, "trackHash", 64), vehicle, racer);
        if (!attempt || (!course && attempt->track_id != id))
            return {};
        attempt->finish_targets = ReadFinishTargets(reply);
        return attempt;
    } catch (const std::exception&) {
        // The player can still race locally when the service is unavailable.
        return {};
    }
}

std::pair<LapReference, unsigned> ReadRunReference(const std::filesystem::path& directory, const std::string& id,
                                                   const trial::Course* course, trial::Vehicle vehicle, unsigned racer,
                                                   std::string_view track_hash);

TimeTrialRequest PrepareGhostRun(const std::filesystem::path& file, const std::filesystem::path& config,
                                 std::optional<unsigned> local_racer, bool signed_in) {
    const auto manifest = ReadReplaySummary(file);
    const auto racer = local_racer ? *local_racer : manifest.value("racer", trial::kDiddy);
    if (racer >= trial::kRacers.size())
        throw std::runtime_error("The replay racer is unsupported.");
    TimeTrialRequest request{trial::Mode::Record, NewTimeTrialFile(config, "Ghost-Practice")};
    request.racer = racer;
    request.ghost = file;
    const auto rules = StoreText(manifest, "ruleset", 48);
    const bool custom = rules == trial::kCustomRules || rules == trial::kCustomVariableLapsRules;
    const auto id = StoreText(manifest, custom ? "trackId" : "course", 96);
    const auto* course = custom                         ? nullptr
                         : rules == trial::kLegacyRules ? &trial::kCourses.front()
                                                        : trial::course_for(id);
    if (!custom && !course)
        return request;
    const auto vehicle_id = rules == trial::kLegacyRules ? "car" : StoreText(manifest, "vehicle", 16);
    const auto vehicle = std::ranges::find(trial::kVehicleIds, vehicle_id);
    if (vehicle == trial::kVehicleIds.end())
        return request;
    const auto chosen = static_cast<trial::Vehicle>(vehicle - trial::kVehicleIds.begin());
    const auto snapshot = dkr::store::service().view();
    const auto board =
        signed_in ? TrackAttemptBoard(snapshot->speedrun_catalogue.get(), id, course, chosen, racer) : std::string{};
    request.attempt = RequestTrackAttempt(board, id, course, chosen, racer);
    if (custom && request.attempt && request.attempt->track_hash != StoreText(manifest, "trackHash", 64))
        request.attempt.reset();
    std::tie(request.lap_reference, request.prior_best) =
        ReadRunReference(config / "speedruns", course ? std::string(course->id) : id, course, chosen, racer,
                         StoreText(manifest, "trackHash", 64));
    return request;
}

void RaceReplayGhost(const std::filesystem::path& file) {
    RankedJob([file, config = g_config_directory, signed_in = !RankedRunsOwner().empty()] {
        auto request = PrepareGhostRun(file, config, std::nullopt, signed_in);
        return [request = std::move(request), signed_in]() mutable {
            QueueTimeTrialRequest(std::move(request), signed_in);
        };
    });
}

struct PublishedReplayTimes {
    std::filesystem::path profile;
    std::string owner;
    std::optional<std::vector<dkr::store::PublishedReplayTime>> scores;
    std::chrono::steady_clock::time_point refresh_at{};
    bool unavailable = false;
};
PublishedReplayTimes g_published_replay_times;

void SavePublishedReplayTimes(const std::filesystem::path& profile, const std::string& owner,
                              std::optional<std::vector<dkr::store::PublishedReplayTime>> scores) {
    if (profile == g_config_directory && owner == RankedRunsOwner()) {
        const bool unavailable = !scores;
        g_published_replay_times = {profile, owner, std::move(scores),
                                    std::chrono::steady_clock::now() + std::chrono::seconds(30), unavailable};
    }
}

void RefreshPublishedReplayTimes(const std::string& owner) {
    auto& status = g_published_replay_times;
    if (status.profile != g_config_directory || status.owner != owner)
        status = {g_config_directory, owner};
    const auto now = std::chrono::steady_clock::now();
    if (now < status.refresh_at || DialogJobRunning())
        return;
    status.refresh_at = now + std::chrono::seconds(30);
    StartDialogJob([profile = status.profile, owner]() -> std::function<void()> {
        std::optional<std::vector<dkr::store::PublishedReplayTime>> scores;
        try {
            scores = dkr::store::service().published_replay_times();
        } catch (const std::exception&) {
            // Keep the posting decision pending until the signed-in player's times are known.
        }
        return [profile, owner, scores = std::move(scores)]() mutable {
            SavePublishedReplayTimes(profile, owner, std::move(scores));
        };
    });
}

std::function<void()> SubmitRankedReplay(const std::filesystem::path& file, const std::filesystem::path& profile,
                                         bool personal_best, bool link_local = false, std::string expected_owner = {}) {
    const auto owner = RankedRunsOwner();
    if (link_local && owner != expected_owner)
        throw std::runtime_error("Your signed-in account changed. Confirm the Discord account again.");
    auto metadata = ReadReplaySidecar(file);
    if (owner.empty() || file.parent_path() != profile / "speedruns" || !OwnReplayFile(file, metadata))
        throw std::runtime_error("Sign in and select one of your eligible recordings to post it.");
    const auto manifest = ReadReplaySummary(file);
    if (link_local ? !IsLocalReplayCapture(manifest) : !ReplayHasRankedAttempt(manifest))
        throw std::runtime_error("Sign in and select one of your eligible recordings to post it.");
    if (personal_best && !link_local) {
        auto scores = dkr::store::service().published_replay_times();
        if (std::ranges::binary_search(scores,
                                       dkr::store::PublishedReplayTime{StoreText(manifest, "board", 96),
                                                                       LeaderboardNumber(manifest, "clockTicks")})) {
            return [profile, owner, scores = std::move(scores)]() mutable {
                SavePublishedReplayTimes(profile, owner, std::move(scores));
            };
        }
    }
    const auto reply = dkr::store::service().submit_ranked_replay(file, link_local);
    const auto state = StoreText(reply.at("run"), "state", 32);
    try {
        metadata["postedOwner"] = owner;
        WriteReplaySidecar(file, metadata);
    } catch (const std::exception&) {
        // The service has accepted the replay even if the local receipt could not be written.
    }
    return [file, profile, state, owner] {
        if (profile == g_config_directory && owner == RankedRunsOwner()) {
            g_time_trial_message = "Run submitted | " + state;
            g_ranked_runs = nullptr;
            for (auto& replay : g_local_replays)
                if (replay.file == file)
                    replay.posted_owner = owner;
        }
    };
}

void DrawReplayResult(const LocalReplay& replay) {
    if (replay.personal_best)
        DrawRaceBadge("PERSONAL BEST", {.36F, .245F, .09F, 1.F});
    if (replay.beat_ghost)
        DrawRaceBadge("GHOST BEATEN", {.11F, .29F, .24F, 1.F});
}

std::string LocalClaimBoard(const LocalReplay& replay, const dkr::store::Json& catalogue) {
    if (replay.track_id.empty())
        return replay.board;
    const auto vehicle = std::ranges::find(trial::kVehicleNames, replay.vehicle);
    if (vehicle == trial::kVehicleNames.end() || !catalogue.contains("boards"))
        return {};
    std::string board;
    for (const auto& entry : catalogue.at("boards")) {
        if (entry.value("ranked", false) && StoreText(entry, "trackId", 96) == replay.track_id &&
            StoreText(entry, "trackHash", 64) == replay.track_hash &&
            StoreText(entry, "vehicle", 24) == trial::kVehicleIds[vehicle - trial::kVehicleNames.begin()] &&
            StoreText(entry, "racer", 32) == replay.racer) {
            if (!board.empty())
                return {};
            board = StoreText(entry, "id", 96);
        }
    }
    return board;
}

void DrawLocalReplayPosting(const LocalReplay& replay, float width, const dkr::store::Json* catalogue) {
    const auto snapshot = dkr::store::service().view();
    const auto owner = RankedRunsOwner();
    if (!replay.posted_owner.empty()) {
        OlParagraph(owner == replay.posted_owner ? "Submitted · View status in My Submitted Runs"
                                                 : "This run has already been linked to a Discord account.",
                    14.F, kOlSoft, width);
        return;
    }
    constexpr const char* popup = "Link this local run?";
    const bool confirming = ImGui::IsPopupOpen(popup);
    if (!confirming && catalogue) {
        if (const auto board = LocalClaimBoard(replay, *catalogue); !board.empty() && !owner.empty()) {
            RefreshPublishedReplayTimes(owner);
            const auto& scores = g_published_replay_times.scores;
            if (scores && std::ranges::binary_search(*scores, dkr::store::PublishedReplayTime{board, replay.time})) {
                OlParagraph("Already on the leaderboard", 14.F, kOlSoft, width);
                return;
            }
        }
    }

    const OlDisabled busy(DialogJobRunning());
    if (owner.empty() && !confirming) {
        if (OlButton("Sign in with Discord to link", OlTone::Go))
            dkr::store::service().login();
        return;
    }

    static std::string confirmed_owner;
    if (OlButton("Link & Post to Leaderboard", OlTone::Go)) {
        confirmed_owner = owner;
        ImGui::OpenPopup(popup);
    }
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    if (!BeginPaddedModal(popup, flags))
        return;
    const auto name = StoreText(snapshot->user, "displayName", 100);
    const auto credit = name.empty() ? std::string("your signed-in Discord account") : name;
    const auto text_width = std::min(435.F, ImGui::GetMainViewport()->WorkSize.x * .7F);
    OlParagraph("Link " + replay.title + " to " + credit +
                    " and send it for verification? The recording can only be linked once.",
                16.F, kOlInk, text_width);
    PaddockGap(10.F);
    const bool cancel = OlButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || confirmed_owner != owner;
    ImGui::SameLine();
    if (OlButton("Link & Post", OlTone::Go) && !cancel) {
        RankedJob([file = replay.file, profile = g_config_directory, owner] {
            return SubmitRankedReplay(file, profile, false, true, owner);
        });
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawReplayPosting(const LocalReplay& replay, float width, bool explain = false) {
    const bool merit = replay.personal_best || replay.beat_ghost;
    if (replay.local_capture && (merit || !replay.posted_owner.empty())) {
        const auto catalogue = dkr::store::service().view()->speedrun_catalogue;
        if (!replay.posted_owner.empty() || (catalogue && catalogue->value("canClaimLocal", false))) {
            DrawLocalReplayPosting(replay, width, catalogue.get());
            return;
        }
    }
    if (!dkr::library_ui::can_post_replay_attempt(replay.has_attempt, replay.practice, merit)) {
        if (!replay.has_attempt && (merit || explain))
            OlParagraph("Linking this local run is unavailable.", 14.F, kOlSoft, width);
        else if (explain)
            OlParagraph("You can post this run if it's your personal best.", 14.F, kOlSoft, width);
        return;
    }
    const auto owner = RankedRunsOwner();
    if (owner.empty()) {
        OlParagraph("Sign in to post this run.", 14.F, kOlSoft, width);
    } else if (owner == replay.posted_owner) {
        OlParagraph("Submitted · View status in My Submitted Runs", 14.F, kOlSoft, width);
    } else {
        if (merit) {
            RefreshPublishedReplayTimes(owner);
            const auto& status = g_published_replay_times;
            if (!status.scores) {
                if (explain || status.unavailable)
                    OlParagraph(status.unavailable ? "Could not check your published times."
                                                   : "Checking your published times...",
                                14.F, kOlSoft, width);
                return;
            }
            if (std::ranges::binary_search(*status.scores,
                                           dkr::store::PublishedReplayTime{replay.board, replay.time})) {
                OlParagraph("Already on the leaderboard", 14.F, kOlSoft, width);
                return;
            }
        }
        const OlDisabled busy(DialogJobRunning());
        if (OlButton("Post to Leaderboard", OlTone::Go))
            RankedJob([file = replay.file, profile = g_config_directory, best = merit] {
                return SubmitRankedReplay(file, profile, best);
            });
    }
}

std::optional<ImVec4> g_split_bounds;

std::array<unsigned, trial::kMaximumLapCount> ReplayLaps(const dkr::store::Json& manifest) {
    std::array<unsigned, trial::kMaximumLapCount> laps{};
    const auto splits = manifest.find("splits");
    if (splits == manifest.end() || !splits->is_array() || splits->size() > laps.size())
        return laps;
    unsigned previous = 0;
    for (std::size_t i = 0; i < splits->size(); ++i) {
        if (!(*splits)[i].is_number_unsigned())
            return {};
        const auto time = (*splits)[i].get<unsigned>();
        if (time <= previous)
            return {};
        laps[i] = time - previous;
        previous = time;
    }
    return laps;
}

std::pair<LapReference, unsigned> ReadRunReference(const std::filesystem::path& directory, const std::string& id,
                                                   const trial::Course* course, trial::Vehicle vehicle, unsigned racer,
                                                   std::string_view track_hash) {
    LapReference result;
    unsigned best = 0;
    try {
        for (const auto& replay : ReadLocalReplayLibrary(directory)) {
            if (!replay.valid || replay.racer != trial::kRacers[racer].name ||
                replay.vehicle != trial::kVehicleNames[static_cast<unsigned>(vehicle)] ||
                (course ? replay.course != course->name || !replay.track_id.empty() : replay.track_id != id) ||
                (!track_hash.empty() && !course && replay.track_hash != track_hash))
                continue;
            const auto laps = ReplayLaps(ReadReplaySummary(replay.file));
            for (std::size_t i = 0; i < laps.size(); ++i)
                if (laps[i] && (!result.best_segments[i] || laps[i] < result.best_segments[i]))
                    result.best_segments[i] = laps[i];
            if (replay.time > 0 && (!best || replay.time < best)) {
                best = unsigned(replay.time);
                result.personal = laps;
            }
        }
    } catch (const std::exception&) {
        // An unavailable reference should leave a run playable.
    }
    const auto catalogue = dkr::store::service().view()->speedrun_catalogue;
    if (catalogue) {
        const auto rows = catalogue->find(course ? "vanillaTracks" : "customTracks");
        if (rows != catalogue->end() && rows->is_array()) {
            const auto row =
                std::ranges::find_if(*rows, [&](const auto& item) { return StoreText(item, "id", 96) == id; });
            if (row != rows->end()) {
                const auto leaders = row->find("leaders");
                if (leaders != row->end() && leaders->is_object()) {
                    const auto standings = leaders->find(trial::kVehicleIds[static_cast<unsigned>(vehicle)]);
                    if (standings != leaders->end() && standings->is_object()) {
                        const auto lap = standings->find("lap");
                        if (lap != standings->end())
                            result.world_best_lap = unsigned(LeaderboardNumber(*lap, "ticks"));
                    }
                }
            }
        }
    }
    return {result, best};
}

AdventureReference ReadAdventureReference(const dkr::store::Json& run) {
    AdventureReference reference;
    const auto splits = run.find("splits");
    if (splits == run.end() || !splits->is_array() || splits->size() > 64)
        return reference;
    for (const auto& split : *splits) {
        if (split.is_array() && split.size() == 2 && split[0].is_string() && split[1].is_number_integer()) {
            const auto label = split[0].get<std::string>();
            const auto ticks = split[1].get<std::int64_t>();
            if (label.size() <= 64 && ticks > 0 && ticks <= trial::kAdventureMaxUpdates * 2)
                reference.try_emplace(label, ticks);
        }
    }
    return reference;
}

void DrawRunDelta(const char* label, std::int64_t actual, std::int64_t reference) {
    if (!reference)
        return;
    const auto delta = actual - reference;
    ImGui::SameLine();
    const auto colour = delta < 0 ? kOlGoText : delta > 0 ? kOlAmber : kOlSoft;
    const bool words = RunPreference(RunOption::DeltaWords);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PaddockCol(colour)), "%s %s%s%s", label,
                       words       ? ""
                       : delta < 0 ? "-"
                       : delta > 0 ? "+"
                                   : "",
                       RunClock(std::abs(delta)).c_str(),
                       words ? delta < 0   ? " ahead"
                               : delta > 0 ? " behind"
                                           : " even"
                             : "");
}

void DrawAdventureDelta(std::span<const trial::AdventureSplit> splits) {
    if (splits.empty())
        return;
    const auto& split = splits.back();
    const auto comparison = RunPreference(RunOption::Comparison);
    if (comparison < 2) {
        const auto& reference = comparison == 0 ? g_adventure_pb_splits : g_adventure_record_splits;
        if (const auto match = reference.find(split.label); match != reference.end())
            DrawRunDelta(comparison == 0 ? "PB" : "WR", split.clock, match->second);
    } else if (comparison == 2) {
        std::int64_t total = 0;
        std::string previous;
        for (const auto& stage : splits) {
            const auto segment = g_adventure_segments.find({previous, std::string(stage.label)});
            if (segment == g_adventure_segments.end())
                return;
            total += segment->second;
            previous = stage.label;
        }
        DrawRunDelta("Best segments", split.clock, total);
    }
}

void DrawTimeTrialHud(bool video = false) {
    g_split_bounds.reset();
    const auto state = trial::progress();
    const bool preparing = trial::preparing_ghost();
    if (!state.lap && !preparing && state.adventure == trial::Adventure::None)
        return;
    const bool splits =
        video || (RunPreference(RunOption::Splits) && (!state.replay || RunPreference(RunOption::ReplaySplits)));
    const bool gap = !video && state.ghost && RunPreference(RunOption::GhostGap) && state.ghost_gap.has_value();
    if (!preparing && !splits && !gap)
        return;
    const auto* viewport = ImGui::GetMainViewport();
    if (preparing) {
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddRectFilled(viewport->Pos, {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y},
                            IM_COL32(6, 18, 28, 240));
    }
    const auto corner = RunOverlayCorner(*viewport, RunPreference(RunOption::SplitPosition));
    ImGui::SetNextWindowPos(video ? ImVec2{viewport->WorkPos.x + 16.F, viewport->WorkPos.y + 16.F} : corner.position,
                            ImGuiCond_Always, video ? ImVec2{} : corner.pivot);
    if (video)
        ImGui::SetNextWindowSizeConstraints({0.F, 0.F}, {viewport->WorkSize.x * .47F, viewport->WorkSize.y * .48F});
    const bool contrast = !video && RunPreference(RunOption::Contrast);
    ImGui::SetNextWindowBgAlpha(video ? .78F : contrast ? 1.F : RunPreference(RunOption::Background) * .01F);
    if (contrast)
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.025F, .055F, .085F, 1.F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.F, 9.F));
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Time Trial HUD", nullptr, flags)) {
        ImGui::SetWindowFontScale(video ? 1.F : std::array{.85F, 1.F, 1.24F}[RunPreference(RunOption::HudSize)]);
        if (preparing) {
            ImGui::TextUnformatted("Preparing ghost");
            if (state.finish_update)
                ImGui::ProgressBar(float(state.updates) / float(state.finish_update), {205.F, 8.F}, "");
            ImGui::End();
            ImGui::PopStyleVar(2);
            if (contrast)
                ImGui::PopStyleColor();
            return;
        }
        if (gap) {
            const auto delta = *state.ghost_gap;
            const bool words = RunPreference(RunOption::DeltaWords);
            ImGui::Text("Ghost  %s%s%s",
                        words       ? ""
                        : delta < 0 ? "-"
                        : delta > 0 ? "+"
                                    : "",
                        RunClock(std::abs(delta)).c_str(),
                        words ? delta < 0   ? " ahead"
                                : delta > 0 ? " behind"
                                            : " even"
                              : "");
        }
        if (splits) {
            const int detail = video ? 0 : RunPreference(RunOption::SplitDetail);
            if (state.adventure != trial::Adventure::None) {
                for (const auto& category : trial::kAdventureCategories)
                    if (category.goal == state.adventure)
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PaddockCol(kOlAmber)), "%s",
                                           category.title.data());
            } else {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PaddockCol(kOlAmber)), "%s | Lap %u / %u",
                                   state.replay  ? "Replay"
                                   : state.ghost ? "Ghost Race"
                                                 : "Time Trial",
                                   state.lap, state.lap_count);
            }
            const auto elapsed =
                state.started ? (video ? LeaderboardTime(state.clock) : RunClock(state.clock)) : std::string("Ready");
            ImGui::TextUnformatted(elapsed.c_str());
            if (detail != 2 && state.adventure != trial::Adventure::None) {
                const auto splits = trial::adventure_splits();
                if (!splits.empty())
                    ImGui::Separator();
                if (video) {
                    for (const auto& split : std::span(splits).last(std::min<std::size_t>(4, splits.size()))) {
                        const auto time = LeaderboardTime(split.clock);
                        ImGui::Text("%.*s  %s", static_cast<int>(split.label.size()), split.label.data(), time.c_str());
                    }
                } else {
                    const auto upcoming = trial::AdventureSplits::upcoming(state.adventure, splits);
                    const auto capacity = static_cast<std::size_t>(std::clamp(
                        (viewport->WorkSize.y * .40F - 74.F) / ImGui::GetTextLineHeightWithSpacing(), 3.F, 30.F));
                    const auto reserve = std::min({upcoming.size(), capacity - std::min(splits.size(), std::size_t{2}),
                                                   std::size_t(RunPreference(RunOption::Upcoming))});
                    const auto recent = std::min(splits.size(), detail == 1 ? std::size_t{1} : capacity - reserve);
                    for (const auto& split : std::span(splits).last(recent)) {
                        const auto time = RunClock(split.clock);
                        ImGui::Text("%.*s  %s", static_cast<int>(split.label.size()), split.label.data(), time.c_str());
                        if (!state.replay && &split == &splits.back())
                            DrawAdventureDelta(splits);
                    }
                    if (reserve) {
                        ImGui::Separator();
                        for (std::size_t row = 0; row < reserve; ++row) {
                            const auto stage = upcoming[row];
                            if (row == 0)
                                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(PaddockCol(kOlAmber)), "Next  %.*s",
                                                   int(stage.size()), stage.data());
                            else
                                ImGui::TextDisabled("%.*s", int(stage.size()), stage.data());
                        }
                    }
                }
            }
            if (detail != 2 && state.recent_split && (video || RunPreference(RunOption::LapDelta))) {
                ImGui::Text("Last lap  %s",
                            (video ? LeaderboardTime(state.recent_split) : RunClock(state.recent_split)).c_str());
                if (!state.replay && state.completed_laps) {
                    const auto lap = state.completed_laps - 1;
                    switch (RunPreference(RunOption::Comparison)) {
                    case 0:
                        DrawRunDelta("PB", state.recent_split, g_run_lap_reference.personal[lap]);
                        break;
                    case 1:
                        DrawRunDelta("WR lap", state.recent_split, g_run_lap_reference.world_best_lap);
                        break;
                    case 2:
                        DrawRunDelta("Best lap", state.recent_split, g_run_lap_reference.best_segments[lap]);
                        break;
                    }
                }
            }
            if (!video && detail != 2 && !state.finish_update && state.adventure == trial::Adventure::None &&
                state.lap <= state.lap_count) {
                const auto next = std::max(1U, state.lap);
                const auto rows = std::min(unsigned(RunPreference(RunOption::Upcoming)), state.lap_count + 1 - next);
                for (unsigned step = 0; step < rows; ++step)
                    ImGui::TextDisabled(
                        "%s  %s", step ? "Then" : "Next",
                        next + step == state.lap_count ? "Finish" : ("Lap " + std::to_string(next + step + 1)).c_str());
            }
            if (state.finish_update && !state.replay && state.adventure == trial::Adventure::None)
                ImGui::TextUnformatted("Recording the finish");
            if (!video && state.total_updates)
                ImGui::ProgressBar(float(state.updates) / float(state.total_updates), {152.F, 8.F}, "");
        }
        const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        g_split_bounds = ImVec4{pos.x, pos.y, pos.x + size.x, pos.y + size.y};
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (contrast)
        ImGui::PopStyleColor();
}

bool g_theatre_controls = true;
bool g_theatre_inputs = false;
int g_theatre_controller_corner = -1;
std::atomic<std::uint64_t> g_theatre_activity{};

void SeekReplaySeconds(int direction) {
    const auto state = trial::progress();
    const auto transport = trial::theatre();
    const auto frame = transport.seeking ? transport.seek_to : state.updates;
    const int step = speed::seek_seconds[RunPreference(RunOption::SeekStep)] * 30;
    if (state.total_updates)
        trial::theatre_seek(unsigned(std::clamp(int(frame) + direction * step, 1, int(state.total_updates))));
}

bool HandleTheatreInput(const SDL_Event& event) {
    g_theatre_activity = SDL_GetTicks64();
    if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        trial::theatre_play_pause();
        return true;
    }
    if (event.type != SDL_KEYDOWN || event.key.repeat)
        return false;
    switch (event.key.keysym.scancode) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_SPACE:
        trial::theatre_play_pause();
        return true;
    case SDL_SCANCODE_F8:
        g_theatre_controls = !g_theatre_controls;
        return true;
    case SDL_SCANCODE_COMMA:
        trial::theatre_step(-1);
        return true;
    case SDL_SCANCODE_PERIOD:
        trial::theatre_step(1);
        return true;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:
        if (event.key.keysym.mod & KMOD_CTRL) {
            SeekReplaySeconds(event.key.keysym.scancode == SDL_SCANCODE_LEFT ? -1 : 1);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void PaintReplayController(ImDrawList* draw, ImVec2 base, const trial::Progress& state, float scale) {
    const auto point = [&](float x, float y) { return ImVec2{base.x + x * scale, base.y + y * scale}; };
    const auto shell = PaddockCol(0xA2B0C7);
    draw->AddRectFilled(point(9, 15), point(223, 76), shell, 29.F * scale);
    draw->AddQuadFilled(point(12, 43), point(63, 57), point(48, 121), point(19, 116), shell);
    draw->AddQuadFilled(point(91, 57), point(141, 57), point(134, 136), point(98, 136), shell);
    draw->AddQuadFilled(point(172, 57), point(221, 43), point(213, 117), point(185, 120), shell);
    const auto button = [&](const char* label, float x, float y, float radius, unsigned bit, unsigned color) {
        const bool pressed = (state.buttons & bit) != 0;
        draw->AddCircleFilled(point(x, y), radius * scale, PaddockCol(color));
        if (pressed)
            draw->AddCircle(point(x, y), (radius + 2.F) * scale, PaddockCol(0xFFF0B0), 20, 2.6F * scale);
        const auto size = ImGui::CalcTextSize(label);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, point(x - size.x * .5F, y - size.y * .5F),
                      PaddockCol(0x102033), label);
    };
    button("L", 40, 17, 10, 0x0020, 0xC4CFDD);
    button("R", 187, 17, 10, 0x0010, 0xC4CFDD);
    button("Z", 202, 101, 10, 0x2000, 0xC4CFDD);
    button("A", 155, 60, 12, 0x8000, 0x5191F4);
    button("B", 142, 41, 10, 0x4000, 0x5CD693);
    button("", 115, 31, 6, 0x1000, 0xED6462);
    button("", 184, 33, 6, 0x0008, 0xFFCD57);
    button("", 173, 44, 6, 0x0002, 0xFFCD57);
    button("", 195, 44, 6, 0x0001, 0xFFCD57);
    button("", 184, 55, 6, 0x0004, 0xFFCD57);
    const auto dpad = PaddockCol(0x344459);
    draw->AddRectFilled(point(32, 53), point(63, 63), dpad);
    draw->AddRectFilled(point(42, 43), point(53, 74), dpad);
    const auto active = PaddockCol(0xFFF0B0);
    if (state.buttons & 0x0800)
        draw->AddRectFilled(point(42, 43), point(53, 53), active);
    if (state.buttons & 0x0400)
        draw->AddRectFilled(point(42, 63), point(53, 74), active);
    if (state.buttons & 0x0200)
        draw->AddRectFilled(point(32, 53), point(42, 63), active);
    if (state.buttons & 0x0100)
        draw->AddRectFilled(point(53, 53), point(63, 63), active);
    draw->AddCircleFilled(point(115, 68), 20.F * scale, PaddockCol(0x64758E), 8);
    const float x = 115.F + 12.F * std::clamp(float(state.stick_x) / 80.F, -1.F, 1.F);
    const float y = 68.F - 12.F * std::clamp(float(state.stick_y) / 80.F, -1.F, 1.F);
    draw->AddCircleFilled(point(x, y), 8.F * scale, PaddockCol(0xCED9E9));
    draw->AddCircle(point(x, y), 4.F * scale, PaddockCol(0x8999AF));
}

void DrawReplayController(const trial::Progress& state, float scale) {
    PaintReplayController(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), state, scale);
    ImGui::Dummy({232.F * scale, 140.F * scale});
}

void DrawReplayInputViewer(const trial::Progress& state, bool video = false) {
    if (!video && !g_theatre_inputs)
        return;
    const auto* view = ImGui::GetMainViewport();
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (video) {
        if (state.adventure != trial::Adventure::None) {
            // Full Game splits own the upper-left of the picture. Anchor the
            // controls in the opposite corner with a bounded footprint.
            ImGui::SetNextWindowPos(
                {view->WorkPos.x + view->WorkSize.x - 16.F, view->WorkPos.y + view->WorkSize.y - 16.F},
                ImGuiCond_Always, {1.F, 1.F});
            ImGui::SetNextWindowSizeConstraints({0.F, 0.F}, {view->WorkSize.x * .47F, view->WorkSize.y * .43F});
        } else {
            ImGui::SetNextWindowPos({view->WorkPos.x + 16.F, view->WorkPos.y + view->WorkSize.y * .43F},
                                    ImGuiCond_Always);
        }
        flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
    } else {
        const auto corner = RunPreference(RunOption::InputPosition);
        auto anchor = RunOverlayCorner(*view, corner);
        const auto scale = std::array{.78F, 1.F, 1.18F}[RunPreference(RunOption::InputSize)];
        const float height = 140.F * scale + 54.F;
        const auto* existing = ImGui::FindWindowByName("Replay Controller");
        const auto intersects = [&](ImVec2 point, ImVec2 size) {
            return g_split_bounds && point.x < g_split_bounds->z && point.x + size.x > g_split_bounds->x &&
                   point.y < g_split_bounds->w && point.y + size.y > g_split_bounds->y;
        };
        const auto space = ImVec2{232.F * scale + 28.F, height};
        const auto proposed =
            ImVec2{anchor.position.x - anchor.pivot.x * space.x, anchor.position.y - anchor.pivot.y * space.y};
        const bool conflict = intersects(proposed, space);
        if (conflict) {
            anchor.position.y = anchor.pivot.y == 0.F ? g_split_bounds->w + 8.F : g_split_bounds->y - 8.F;
            if (anchor.position.y - anchor.pivot.y * height < view->WorkPos.y ||
                anchor.position.y + (1.F - anchor.pivot.y) * height > view->WorkPos.y + view->WorkSize.y)
                anchor = RunOverlayCorner(*view, corner ^ 1);
        }
        const bool reposition =
            corner != g_theatre_controller_corner || (existing && intersects(existing->Pos, existing->Size));
        ImGui::SetNextWindowPos(anchor.position, reposition ? ImGuiCond_Always : ImGuiCond_FirstUseEver, anchor.pivot);
        g_theatre_controller_corner = corner;
    }
    ImGui::SetNextWindowBgAlpha(!video && RunPreference(RunOption::Contrast) ? 1.F : .84F);
    if (ImGui::Begin("Replay Controller", video ? nullptr : &g_theatre_inputs, flags))
        DrawReplayController(state, video ? .78F : std::array{.78F, 1.F, 1.18F}[RunPreference(RunOption::InputSize)]);
    ImGui::End();
}

void DrawReplayTheatre() {
    const auto state = trial::progress();
    if (!state.total_updates)
        return;
    auto transport = trial::theatre();
    // Let RT64 present through the launcher handoff before holding frame one.
    if (transport.pause_on_open && !transport.buffering && state.updates) {
        trial::theatre_complete_intro();
        transport = trial::theatre();
    }
    const auto file = trial::watched_replay();
    const bool local = file.parent_path() == g_config_directory / "speedruns";
    if (local && g_refresh_replay_library && !DialogJobRunning())
        RefreshReplayLibrary();
    const auto* view = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 13.F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.F, 13.F});
    if (transport.buffering) {
        ImGui::SetNextWindowPos(view->GetCenter(), ImGuiCond_Always, {.5F, .5F});
        ImGui::SetNextWindowBgAlpha(.94F);
        if (ImGui::Begin("Preparing replay", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto at = ImGui::GetCursorScreenPos();
            auto* draw = ImGui::GetWindowDrawList();
            const float phase = RunPreference(RunOption::ReducedMotion) ? 0.F : float(ImGui::GetTime() * 5.5);
            draw->PathArcTo({at.x + 12.F, at.y + 12.F}, 9.F, phase, phase + 4.7F, 18);
            draw->PathStroke(PaddockCol(kOlAmber), 0, 2.4F);
            ImGui::Dummy({27.F, 24.F});
            ImGui::SameLine();
            ImGui::TextUnformatted("Preparing replay...");
            if (OlButton("Cancel", OlTone::Plain, 0.F, nullptr, 28.F))
                trial::theatre_exit();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    DrawReplayInputViewer(state);
    constexpr const char* theatre_window = "Replay Theatre · F8 to show or hide";
    if (const auto* previous = ImGui::FindWindowByName(theatre_window)) {
        const auto far_corner = ImVec2{previous->Pos.x + previous->Size.x, previous->Pos.y + previous->Size.y};
        if (ImGui::IsMouseHoveringRect(previous->Pos, far_corner))
            g_theatre_activity = SDL_GetTicks64();
    }
    const bool auto_hidden = RunPreference(RunOption::PlaybackControls) && !transport.paused && !transport.seeking &&
                             !transport.finished && SDL_GetTicks64() - g_theatre_activity.load() > 3000;
    if (!g_theatre_controls || auto_hidden) {
        ImGui::PopStyleVar(2);
        return;
    }
    const float width = std::min(730.F, view->WorkSize.x - 24.F);
    ImGui::SetNextWindowPos({view->WorkPos.x + view->WorkSize.x / 2.F, view->WorkPos.y + view->WorkSize.y - 14.F},
                            ImGuiCond_FirstUseEver, {0.5F, 1.F});
    ImGui::SetNextWindowSize({width, 0.F});
    ImGui::SetNextWindowBgAlpha(.86F);
    constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin(theatre_window, &g_theatre_controls, flags)) {
        static std::optional<unsigned> drag_target;
        if (ImGui::IsWindowAppearing()) {
            drag_target.reset();
            ImGui::SetWindowFocus();
        }
        const auto time = RunClock(state.clock);
        ImGui::Text("REPLAY THEATRE | %s", trial::kRacers[state.racer].name.data());
        ImGui::SameLine();
        ImGui::Text("%s   |   Frame %u / %u%s", time.c_str(), state.updates, state.total_updates,
                    transport.finished && !transport.seeking ? "   |   Finished" : "");
        if (local) {
            const auto* replay = FindLocalReplay(file);
            if (replay) {
                DrawReplayResult(*replay);
                DrawReplayPosting(*replay, ImGui::GetContentRegionAvail().x, true);
            }
            const bool posted = replay && !replay->posted_owner.empty() && replay->posted_owner == RankedRunsOwner();
            if (!posted && !g_time_trial_message.empty())
                OlParagraph(g_time_trial_message, 14.F, kOlSoft, ImGui::GetContentRegionAvail().x);
        }
        if (transport.seeking) {
            ImGui::Text("Preparing frame %u   |   %s on arrival", transport.seek_to,
                        transport.paused ? "Pause" : "Play");
            ImGui::SameLine();
            if (OlButton("Cancel seek", OlTone::Plain, 0.F, nullptr, 28.F))
                trial::theatre_cancel_seek();
        } else if (transport.buffered < state.total_updates) {
            ImGui::Text("Preparing replay %u / %u", transport.buffered, state.total_updates);
        } else {
            ImGui::TextUnformatted("Ready to seek");
        }
        const unsigned first = 1, last = state.total_updates;
        const unsigned destination = transport.seeking ? transport.seek_to : std::max(first, state.updates);
        unsigned cursor = drag_target.value_or(destination);
        ImGui::SetNextItemWidth(-1.F);
        const bool changed = ImGui::SliderScalar("##replay-timeline", ImGuiDataType_U32, &cursor, &first, &last, "",
                                                 ImGuiSliderFlags_NoInput);
        if (changed) {
            drag_target = cursor;
            trial::theatre_scrub(cursor);
        }
        if (!ImGui::IsItemActive() && drag_target) {
            trial::theatre_seek(*drag_target);
            drag_target.reset();
        }
        const auto left = ImGui::GetItemRectMin();
        const auto right = ImGui::GetItemRectMax();
        if (transport.buffered > first && last > first) {
            const auto x = left.x + (right.x - left.x) * float(transport.buffered - first) / float(last - first);
            ImGui::GetWindowDrawList()->AddLine({left.x, right.y - 2.F}, {x, right.y - 2.F}, PaddockCol(kOlSoft), 2.F);
        }
        if (RunPreference(RunOption::LapMarkers) && last > 1) {
            const auto marker = [&](unsigned frame) {
                if (!frame || frame > last)
                    return;
                const auto x = left.x + (right.x - left.x) * float(frame - 1) / float(last - 1);
                ImGui::GetWindowDrawList()->AddLine({x, right.y - 3.F}, {x, right.y}, PaddockCol(kOlAmber), 2.F);
            };
            if (state.adventure == trial::Adventure::None) {
                for (const auto lap : std::span(state.lap_updates).first(state.lap_count))
                    marker(lap);
            } else {
                for (const auto frame : trial::adventure_timeline_markers())
                    marker(frame);
            }
        }
        if (ImGui::IsItemActive() && drag_target)
            ImGui::SetTooltip("Seek to frame %u", *drag_target);
        {
            if (OlButton("Start", OlTone::Plain, 0.F, nullptr, 32.F))
                trial::theatre_seek(1);
            for (unsigned lap = 0; lap < state.lap_count && state.lap_updates[lap]; ++lap) {
                ImGui::SameLine();
                const auto label = "Lap " + std::to_string(lap + 1);
                if (OlButton(label.c_str(), OlTone::Plain, 0.F, nullptr, 32.F))
                    trial::theatre_seek(state.lap_updates[lap]);
            }
            ImGui::SameLine();
            if (OlButton("Finish", OlTone::Plain, 0.F, nullptr, 32.F))
                trial::theatre_seek(state.finish_update);
            const auto seconds = std::to_string(speed::seek_seconds[RunPreference(RunOption::SeekStep)]) + "s";
            ImGui::SameLine();
            if (OlButton(("-" + seconds).c_str(), OlTone::Plain, 0.F, nullptr, 32.F))
                SeekReplaySeconds(-1);
            ImGui::SameLine();
            if (OlButton(("+" + seconds).c_str(), OlTone::Plain, 0.F, nullptr, 32.F))
                SeekReplaySeconds(1);
        }
        {
            const char* action = transport.finished && !transport.seeking ? "Replay"
                                 : transport.paused                       ? "Play"
                                                                          : "Pause";
            if (OlButton(action, OlTone::Go, 0.F, nullptr, 32.F))
                trial::theatre_play_pause();
            const auto frame = transport.seeking ? transport.seek_to : state.updates;
            ImGui::SameLine();
            {
                const OlDisabled at_start(frame <= 1);
                if (OlButton("-1 frame", OlTone::Plain, 0.F, nullptr, 32.F))
                    trial::theatre_step(-1);
            }
            ImGui::SameLine();
            {
                const OlDisabled at_end(!frame || frame == state.total_updates);
                if (OlButton("+1 frame", OlTone::Plain, 0.F, nullptr, 32.F))
                    trial::theatre_step(1);
            }
        }
        if (width >= 630.F)
            ImGui::SameLine();
        static const std::vector<SettingsChoice> speeds{"0.5x", "1x", "1.5x", "2x"};
        const auto current = std::ranges::find(speed::playback_speeds, transport.speed);
        int chosen = current == speed::playback_speeds.end() ? 1 : int(current - speed::playback_speeds.begin());
        if (SettingsDropdown("theatre-speed", {}, &chosen, speeds, 100.F, false, {}, 32.F))
            trial::theatre_speed(speed::playback_speeds[chosen]);
        ImGui::SameLine();
        ImGui::Checkbox("Controller", &g_theatre_inputs);
        ImGui::SameLine();
        if (OlButton("Exit", OlTone::Plain, 0.F, nullptr, 32.F))
            trial::theatre_exit();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void DrawSpeedrunningRestartConfirmation() {
    constexpr const char* title = "Restart this run?";
    if (!trial::restart_confirmation_pending() && !ImGui::IsPopupOpen(title))
        return;
    if (trial::restart_confirmation_pending() && !ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    if (!BeginPaddedModal(title, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!trial::restart_confirmation_pending()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    OlParagraph("The current attempt will be replaced.", 15.F, kOlInk, 330.F);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    if (OlButton("Keep racing")) {
        trial::confirm_restart(false);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (OlButton("Restart Run", OlTone::Go)) {
        trial::confirm_restart(true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

enum class LeaderboardReplayAction { Watch, RaceGhost };

void WatchLeaderboardReplay(const std::string& id, bool own_run,
                            LeaderboardReplayAction action = LeaderboardReplayAction::Watch,
                            std::string ghost_name = {}) {
    RankedJob([id, own_run, action, ghost_name = std::move(ghost_name), config = g_config_directory,
               signed_in = !RankedRunsOwner().empty()] {
        const auto directory = config / "speedrun-playback";
        CheckManagedReplayDirectory(directory);
        std::filesystem::create_directories(directory);
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto file = directory / (std::to_string(stamp) + ".dkrreplay");
        try {
            dkr::store::service().save_ranked_replay(id, file, own_run);
            auto request = action == LeaderboardReplayAction::RaceGhost
                               ? PrepareGhostRun(file, config, std::nullopt, signed_in)
                               : TimeTrialRequest{trial::Mode::Watch, file};
            request.ghost_name = ghost_name;
            return [file, request = std::move(request), signed_in]() mutable {
                DeleteRankedPlayback(std::exchange(g_ranked_playback_file, file));
                QueueTimeTrialRequest(std::move(request), signed_in);
            };
        } catch (...) {
            DeleteRankedPlayback(file);
            throw;
        }
    });
}

void DrawSubmissionLimit(float width, const dkr::store::Json* catalogue) {
    if (!catalogue)
        return;
    const auto limit = catalogue->find("submissionLimit");
    if (limit == catalogue->end() || !limit->is_object())
        return;
    const auto runs = LeaderboardNumber(*limit, "maxRuns");
    const auto hours = LeaderboardNumber(*limit, "windowHours");
    if (runs > 0 && hours > 0)
        OlParagraph("You can post up to " + std::to_string(runs) + " runs. Each submission counts for " +
                        std::to_string(hours) + " hours, including after approval.",
                    15.F, kOlSoft, width);
}

void DrawLocalReplays(float width, const dkr::store::Json* catalogue) {
    static char search[121]{};
    static char title[121]{};
    static int filter = 0;
    static std::filesystem::path selected;
    static std::string selected_title;
    static bool purge_bests = false;
    static bool purge_unsubmitted = false;
    const bool can_watch = CanLaunchLeaderboardReplay();
    if (g_replay_library_profile != g_config_directory)
        g_local_replays.clear();
    if (g_refresh_replay_library && !DialogJobRunning())
        RefreshReplayLibrary();

    DrawOlFeature(width, "My Replays", nullptr, [&](float inner) {
        DrawSubmissionLimit(inner, catalogue);
        if (OlButton("Refresh replays")) {
            g_refresh_replay_library = true;
            g_published_replay_times = {};
        }
        ImGui::SameLine();
        {
            const OlDisabled busy(g_local_replays.empty() || DialogJobRunning());
            if (OlButton("Purge replays")) {
                purge_bests = false;
                purge_unsubmitted = false;
                ImGui::OpenPopup("Purge stored replays?");
            }
        }
        if (ImGui::BeginPopupModal("Purge stored replays?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            OlParagraph("Permanently remove local copies from this device. Leaderboard submissions stay online.", 15.F,
                        kOlInk, 340.F);
            ImGui::Checkbox("Include personal bests", &purge_bests);
            ImGui::Checkbox("Include runs I haven't posted", &purge_unsubmitted);
            const auto count = std::ranges::count_if(g_local_replays, [&](const auto& replay) {
                return dkr::library_ui::can_purge_replay(replay.personal_best, !replay.posted_owner.empty(),
                                                         purge_bests, purge_unsubmitted);
            });
            OlParagraph(count ? std::to_string(count) +
                                    (count == 1 ? " replay will be removed." : " replays will be removed.")
                              : "No replays match these choices.",
                        15.F, kOlSoft, 340.F);
            if (OlButton("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            {
                const OlDisabled unavailable(count == 0 || DialogJobRunning());
                if (OlButton("Purge selected replays", OlTone::Danger)) {
                    RankedJob([profile = g_config_directory,
                               chosen = SelectReplayPurge(g_local_replays, purge_bests, purge_unsubmitted),
                               bests = purge_bests, unsubmitted = purge_unsubmitted] {
                        const auto [removed, skipped] =
                            PurgeLocalReplays(profile / "speedruns", chosen, bests, unsubmitted);
                        return [profile, removed, skipped] {
                            if (profile != g_config_directory)
                                return;
                            g_refresh_replay_library = true;
                            g_time_trial_message =
                                "Removed " + std::to_string(removed) + (removed == 1 ? " replay." : " replays.");
                            if (skipped)
                                g_time_trial_message +=
                                    " Skipped " + std::to_string(skipped) + " changed or unavailable.";
                        };
                    });
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        PaddockGap(8.F);
        ImGui::BeginGroup();
        const float search_width = std::min(300.F, inner);
        SettingsLabel("Search replays", search_width);
        OlInputOptions options;
        options.hint = "Track, racer or title";
        options.height = kSettingsDropdownHeight;
        if (OlInput("##find-replay", nullptr, search, sizeof(search), search_width, options) ==
            OlInputResult::Gamepad) {
            RequestCustomTextKeyboard(
                "Search replays", search, sizeof(search),
                [](const std::string& text) { std::snprintf(search, sizeof(search), "%s", text.c_str()); }, true);
        }
        ImGui::EndGroup();
        if (inner >= 540.F)
            ImGui::SameLine();
        ImGui::BeginGroup();
        SettingsDropdown("replay-type", "Replay type", &filter, {"All replays", "Local runs", "Leaderboard runs"},
                         std::min(226.F, inner));
        ImGui::EndGroup();

        const auto needle = dkr::runtime::texture_browser::lower_ascii(search);
        bool rename = false, remove = false;
        unsigned matches = 0;
        std::vector<const LocalReplay*> ordered;
        ordered.reserve(g_local_replays.size());
        for (const auto& replay : g_local_replays)
            ordered.push_back(&replay);
        switch (RunPreference(RunOption::ReplaySort)) {
        case 1:
            std::ranges::stable_sort(ordered, std::greater{}, &LocalReplay::personal_best);
            break;
        case 2:
            std::ranges::stable_sort(ordered, {}, &LocalReplay::course);
            break;
        }
        for (const auto* record : ordered) {
            const auto& replay = *record;
            const std::string kind = replay.has_attempt ? "Leaderboard run" : "Local run";
            if ((filter == 1 && replay.has_attempt) || (filter == 2 && !replay.has_attempt))
                continue;
            const auto haystack = replay.title + " " + replay.course + " " + replay.racer + " " + replay.vehicle + " " +
                                  PathUtf8(replay.file.filename());
            if (dkr::runtime::texture_browser::lower_ascii(haystack).find(needle) == std::string::npos)
                continue;
            ++matches;
            const auto id = PathUtf8(replay.file.filename());
            ImGui::PushID(id.c_str());
            const ImVec2 divider = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(divider, {divider.x + inner, divider.y + 1.F},
                                                      ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::Dummy({inner, 1.F});
            const bool side_by_side = inner >= 755.F;
            ImGui::BeginGroup();
            const float details_width = side_by_side ? inner - 415.F : inner;
            OlParagraph(replay.title, 19.F, kOlInk, details_width);
            if (replay.valid) {
                std::string details;
                const auto append = [&](const std::string& part) {
                    if (!part.empty()) {
                        if (!details.empty())
                            details += " | ";
                        details += part;
                    }
                };
                if (replay.title != replay.course)
                    append(replay.course);
                append(replay.vehicle);
                append(replay.racer);
                append(kind);
                const auto saved = ReplayDate(replay.saved);
                if (!saved.empty())
                    append("Saved " + saved);
                OlParagraph(details, 15.F, kOlSoft, details_width);
                OlParagraph("Finish " + LeaderboardTime(replay.time) +
                                (replay.best_lap ? " | Best lap " + LeaderboardTime(replay.best_lap) : ""),
                            15.F, kOlInk, details_width);
                DrawReplayResult(replay);
                DrawReplayPosting(replay, details_width);
            } else {
                OlParagraph("Unsupported or incomplete replay", 15.F, kOlSoft, details_width);
            }
            ImGui::EndGroup();
            if (side_by_side)
                ImGui::SameLine(inner - 392.F);
            {
                const OlDisabled busy(!replay.valid || !can_watch || DialogJobRunning());
                if (OlButton("Watch"))
                    g_time_trial_request = TimeTrialRequest{trial::Mode::Watch, replay.file};
            }
            if (replay.can_ghost) {
                ImGui::SameLine();
                const OlDisabled busy(!can_watch || DialogJobRunning());
                if (OlButton("Race Ghost"))
                    RaceReplayGhost(replay.file);
            }
            ImGui::SameLine();
            const OlDisabled busy(DialogJobRunning());
            if (OlButton("Rename")) {
                selected = replay.file;
                selected_title = replay.title;
                std::snprintf(title, sizeof(title), "%s", replay.title.c_str());
                rename = true;
            }
            ImGui::SameLine();
            if (OlButton("Remove")) {
                selected = replay.file;
                selected_title = replay.title;
                remove = true;
            }
            ImGui::PopID();
        }
        if (!matches)
            OlParagraph(g_local_replays.empty() ? "Recorded runs and downloaded replays will appear here."
                                                : "No replays match your filters.",
                        15.F, kOlSoft, inner);
        if (rename)
            ImGui::OpenPopup("Rename replay");
        if (remove)
            ImGui::OpenPopup("Remove local copy?");
        if (ImGui::BeginPopupModal("Rename replay", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (OlInput("##replay-name", "Display title", title, sizeof(title), 310.F, {}) == OlInputResult::Gamepad)
                RequestCustomTextKeyboard(
                    "Display title", title, sizeof(title),
                    [](const std::string& text) { std::snprintf(title, sizeof(title), "%s", text.c_str()); }, true);
            const std::string name = title;
            {
                const OlDisabled disabled(name.find_first_not_of(" \t\r\n") == std::string::npos);
                if (OlButton("Save name", OlTone::Go)) {
                    RankedJob([file = selected, name] {
                        RenameLocalReplay(file, name);
                        return [] { g_refresh_replay_library = true; };
                    });
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (OlButton("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Remove local copy?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            OlParagraph(selected_title, 17.F, kOlInk, 320.F);
            if (OlButton("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (OlButton("Remove local copy")) {
                RankedJob([file = selected] {
                    RemoveLocalReplay(file);
                    return [] { g_refresh_replay_library = true; };
                });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    });
}

using TrackQuery = dkr::store::LeaderboardTrackQuery;
std::string g_track_return_focus;

void DrawAdventureBoards(float width, const dkr::store::Json* catalogue, TrackQuery& query);
std::function<void()> DrawPracticeAdventure(float width, const dkr::store::Json* catalogue, bool signed_in);

void DrawLatestRecording(float width) {
    const auto result = trial::last_result();
    if (result.replay || result.message.empty())
        return;
    DrawOlFeature(width, "Last run", nullptr, [&](float inner) {
        OlParagraph(result.message, 15.F, kOlSoft, inner);
        if (!result.complete)
            return;
        if (RunPreference(RunOption::FinishSummary))
            OlParagraph("Finish " + LeaderboardTime(result.clock) +
                            (result.best_lap ? " | Best lap " + LeaderboardTime(result.best_lap) : ""),
                        16.F, kOlInk, inner);
        if (const auto* replay = FindLocalReplay(result.file)) {
            DrawReplayResult(*replay);
            if (RunPreference(RunOption::PostPrompt))
                DrawReplayPosting(*replay, inner, true);
            const OlDisabled busy(!CanLaunchLeaderboardReplay() || DialogJobRunning());
            if (OlButton("Watch Replay"))
                g_time_trial_request = TimeTrialRequest{trial::Mode::Watch, result.file};
        }
    });
    PaddockGap(8.F);
}

void StartTrackRun(std::string id, const trial::Course* course, trial::Vehicle vehicle, unsigned racer,
                   std::string board, bool signed_in) {
    RankedJob(
        [id = std::move(id), course, vehicle, racer, board = std::move(board), signed_in, config = g_config_directory] {
            auto request = TimeTrialRequest{trial::Mode::Record, NewTimeTrialFile(config, id + "-Practice")};
            request.attempt = RequestTrackAttempt(board, id, course, vehicle, racer);
            request.racer = racer;
            request.vehicle = vehicle;
            if (course)
                request.course = course;
            else
                request.track = id;
            std::tie(request.lap_reference, request.prior_best) =
                ReadRunReference(config / "speedruns", id, course, vehicle, racer,
                                 request.attempt ? std::string_view(request.attempt->track_hash) : std::string_view{});
            return [request = std::move(request), signed_in]() mutable {
                QueueTimeTrialRequest(std::move(request), signed_in);
            };
        });
}

struct SpeedrunningSetup {
    std::array<std::string, 2> courses;
    std::array<unsigned, 2> vehicles{};
    unsigned racer = trial::kDiddy;
    int challenge = 0;
};
SpeedrunningSetup g_run_setup;
std::filesystem::path g_run_setup_profile;

void LoadSpeedrunningSetup() {
    if (g_run_setup_profile == g_config_directory)
        return;
    g_run_setup_profile = g_config_directory;
    g_run_setup = {};
    if (!RunPreference(RunOption::RememberSetup))
        return;
    try {
        const auto file = g_config_directory / "speedrunning-setup.json";
        const auto saved = dkr::store::Json::parse(dkr::updates::read_text(file, 4096));
        for (unsigned i = 0; i < 2; ++i) {
            g_run_setup.courses[i] = StoreText(saved, i ? "custom" : "vanilla", 128);
            g_run_setup.vehicles[i] =
                std::min(2U, unsigned(LeaderboardNumber(saved, i ? "customVehicle" : "vanillaVehicle")));
        }
        if (saved.contains("racer"))
            g_run_setup.racer =
                std::min(unsigned(trial::kRacers.size() - 1), unsigned(LeaderboardNumber(saved, "racer")));
        g_run_setup.challenge = int(LeaderboardNumber(saved, "challenge"));
    } catch (const std::exception&) {
        // Older profiles start with the existing default selections.
    }
}

void SaveSpeedrunningSetup() {
    if (!RunPreference(RunOption::RememberSetup))
        return;
    const dkr::store::Json setup{{"vanilla", g_run_setup.courses[0]},
                                 {"custom", g_run_setup.courses[1]},
                                 {"vanillaVehicle", g_run_setup.vehicles[0]},
                                 {"customVehicle", g_run_setup.vehicles[1]},
                                 {"racer", g_run_setup.racer},
                                 {"challenge", g_run_setup.challenge}};
    try {
        dkr::updates::durable_write(g_config_directory / "speedrunning-setup.json", setup.dump());
    } catch (const std::exception&) {
        std::fprintf(stderr, "[speedrunning] Could not save run setup.\n");
    }
}

std::function<void()> DrawPracticeTracks(float width, const dkr::store::Json* catalogue, bool custom, bool signed_in) {
    using dkr::runtime::custom_tracks::ReplayTrack;
    static const auto vanilla = [] {
        std::vector<ReplayTrack> choices;
        for (const auto& course : trial::kCourses)
            choices.push_back({std::string(course.id), std::string(course.name), course.vehicles});
        return choices;
    }();
    OlHeading(custom ? "Custom Tracks" : "Vanilla Tracks", width < 275.F ? 19.F : 24.F, kOlCream, width);
    const auto installed = custom ? dkr::runtime::custom_tracks::installed_replay_tracks() : std::vector<ReplayTrack>{};
    const auto& tracks = custom ? installed : vanilla;
    if (tracks.empty()) {
        OlParagraph("Install a compatible .dkrmap track to practise here.", 15.F, kOlSoft, width);
        return {};
    }
    auto& selected = g_run_setup.courses;
    auto& vehicle = g_run_setup.vehicles[custom];
    auto& racer = g_run_setup.racer;
    const auto current = std::ranges::find(tracks, selected[custom], &ReplayTrack::id);
    int course_index = current == tracks.end() ? 0 : static_cast<int>(current - tracks.begin());
    std::vector<SettingsChoice> courses;
    courses.reserve(tracks.size());
    for (const auto& track : tracks)
        courses.emplace_back(track.name);
    SettingsDropdown("run-course", "Course", &course_index, courses, std::min(360.F, width));
    const auto* track = &tracks[static_cast<std::size_t>(course_index)];
    selected[custom] = track->id;

    std::vector<SettingsChoice> vehicle_names;
    std::vector<unsigned> available_vehicles;
    int vehicle_index = 0;
    for (unsigned i = 0; i < trial::kVehicleNames.size(); ++i) {
        if (!(track->vehicles & (1U << i)))
            continue;
        if (i == vehicle)
            vehicle_index = static_cast<int>(available_vehicles.size());
        available_vehicles.push_back(i);
        vehicle_names.emplace_back(std::string(dkr::localisation::source(trial::kVehicleNames[i])));
    }
    SettingsDropdown("run-vehicle", "Vehicle", &vehicle_index, vehicle_names, std::min(360.F, width));
    vehicle = available_vehicles[static_cast<std::size_t>(vehicle_index)];

    static const auto racers = [] {
        std::vector<SettingsChoice> choices;
        choices.reserve(trial::kRacers.size());
        for (const auto& entry : trial::kRacers)
            choices.emplace_back(std::string(entry.name));
        return choices;
    }();
    int racer_index = static_cast<int>(racer);
    SettingsDropdown("run-racer", "Racer", &racer_index, racers, std::min(360.F, width));
    racer = static_cast<unsigned>(racer_index);
    const auto choice = static_cast<trial::Vehicle>(vehicle);
    const auto* course = custom ? nullptr : trial::course_for(track->id);
    auto board = signed_in ? TrackAttemptBoard(catalogue, track->id, course, choice, racer) : std::string{};
    return [id = track->id, course, choice, racer, board = std::move(board), signed_in] {
        StartTrackRun(id, course, choice, racer, board, signed_in);
    };
}

void DrawLeaderboardPractice(float width, const dkr::store::Json* catalogue) {
    LoadSpeedrunningSetup();
    if (g_refresh_replay_library && !DialogJobRunning())
        RefreshReplayLibrary();
    DrawLatestRecording(width);
    const bool ready = CanLaunchLeaderboardReplay();
    const bool can_launch = ready && !g_leaderboard_layout_check;
    const bool signed_in = !RankedRunsOwner().empty();
    if (!ready)
        OlParagraph("Select a US v1.0 or US v1.1 ROM in Play and leave any active lobby.", 15.F, kOlSoft, width);
    constexpr float gap = 12.F;
    constexpr float minimum_card = 242.F;
    const bool row = width >= 3.F * minimum_card + 2.F * gap;
    const float card = row ? std::floor((width - 2.F * gap) / 3.F) : width;
    const auto draw_card = [&](int section) {
        ImGui::PushID(section);
        DrawOlCard(
            card, OlPanelLook{},
            [&](float inner) {
                const auto start = section == 2 ? DrawPracticeAdventure(inner, catalogue, signed_in)
                                                : DrawPracticeTracks(inner, catalogue, section == 1, signed_in);
                if (start) {
                    PaddockGap(6.F);
                    const OlDisabled busy(!can_launch || DialogJobRunning());
                    if (OlButton("Start Run", OlTone::Go)) {
                        SaveSpeedrunningSetup();
                        start();
                    }
                }
            },
            14.F);
        ImGui::PopID();
    };
    if (row) {
        const auto origin = ImGui::GetCursorScreenPos();
        float bottom = origin.y;
        {
            OlEqualProfileRow equal;
            for (int section = 0; section < 3; ++section) {
                ImGui::SetCursorScreenPos({origin.x + section * (card + gap), origin.y});
                equal.capture_next = true;
                draw_card(section);
                bottom = std::max(bottom, ImGui::GetItemRectMax().y);
            }
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({width, bottom - origin.y});
    } else {
        for (int section = 0; section < 3; ++section) {
            if (section)
                PaddockGap(gap);
            draw_card(section);
        }
    }
}

using LeaderboardChoices = std::vector<std::pair<std::string, std::string>>;

std::string LeaderboardVehicle(std::string_view id) {
    const auto vehicle = std::find(trial::kVehicleIds.begin(), trial::kVehicleIds.end(), id);
    return vehicle == trial::kVehicleIds.end()
               ? std::string(id)
               : std::string(trial::kVehicleNames[vehicle - trial::kVehicleIds.begin()]);
}

bool LeaderboardFilter(const char* label, const LeaderboardChoices& choices, std::string& value, float width) {
    const auto selected =
        std::find_if(choices.begin(), choices.end(), [&](const auto& entry) { return entry.first == value; });
    if (choices.empty())
        return false;
    std::vector<SettingsChoice> names;
    names.reserve(choices.size());
    for (const auto& entry : choices)
        names.emplace_back(std::string(dkr::localisation::source(entry.second)));
    int index = selected == choices.end() ? 0 : static_cast<int>(selected - choices.begin());
    ImGui::BeginGroup();
    const bool changed = SettingsDropdown(label, label, &index, names, width);
    ImGui::EndGroup();
    if (changed)
        value = choices[static_cast<std::size_t>(index)].first;
    return changed;
}

bool CanLaunchLeaderboardReplay() {
    return g_launcher_rom && !dkr::runtime::netplay::session().active() && !g_time_trial_request;
}

void DrawLeaderboardRankRow(float width, const dkr::store::Json& entry, bool full_game) {
    const auto place = LeaderboardNumber(entry, "place");
    const auto id = StoreText(entry, "id", 96);
    const auto time = LeaderboardTime(LeaderboardNumber(entry, "ticks"));
    const auto name = StoreText(entry, "name", 60);
    const auto racer = StoreText(entry, "racer", 50);
    constexpr float gap = 8.F;
    constexpr float rank_width = 56.F;
    constexpr float time_width = 97.F;
    const float watch_width = OlButtonWidth("Watch");
    const float ghost_width = full_game ? 0.F : OlButtonWidth("Race Ghost", OlTone::Go);
    const float actions_width = watch_width + (full_game ? 0.F : ghost_width + gap);
    OlPanelLook look;
    look.fill = place == 1 ? 0x143446 : 0x102D3E;
    look.ring = place == 1 ? 0x947445 : kOlRing;
    look.ring_width = 1.F;
    look.radii = PaddockRound(9.F);

    ImGui::PushID(id.c_str());
    DrawOlCard(
        width, look,
        [&](float inner) {
            const bool inline_actions = !id.empty() && inner >= rank_width + time_width + actions_width + 168.F;
            const float time_right = inline_actions ? inner - actions_width - 20.F : inner;
            const float time_left = time_right - time_width;
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float badge_width = rank_width - gap;
            PaddockFill(ImGui::GetWindowDrawList(), {start.x, start.y + 5.F}, {start.x + badge_width, start.y + 33.F},
                        PaddockRound(6.F), PaddockCol(place == 1 ? 0x45371D : 0x1A4054));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7.F);
            OlParagraph("#" + std::to_string(place), 15.F, place == 1 ? kOlAmber : kOlInk, badge_width, true, 1.4F,
                        true);

            ImGui::SameLine(0.F, gap);
            ImGui::SetCursorScreenPos({start.x + rank_width, start.y});
            ImGui::BeginGroup();
            const float name_width = std::max(1.F, time_left - rank_width - gap);
            OlParagraph(name, 16.F, kOlInk, name_width, true, 1.3F);
            if (!racer.empty())
                OlParagraph(racer, 14.F, kOlSoft, name_width, false, 1.25F);
            ImGui::EndGroup();

            ImGui::SameLine();
            ImGui::SetCursorScreenPos({start.x + time_left, start.y + 7.F});
            OlParagraph(time, 18.F, kOlCream, time_width, true, 1.3F);
            if (id.empty())
                return;

            if (inline_actions) {
                ImGui::SameLine();
                ImGui::SetCursorScreenPos({start.x + inner - actions_width, start.y + 7.F});
            } else {
                PaddockGap(3.F);
            }
            const OlDisabled launch(DialogJobRunning() || !CanLaunchLeaderboardReplay());
            if (!full_game) {
                if (OlButton("Race Ghost", OlTone::Go))
                    WatchLeaderboardReplay(id, false, LeaderboardReplayAction::RaceGhost, name);
                if (inner >= actions_width)
                    ImGui::SameLine(0.F, gap);
            }
            if (OlButton("Watch"))
                WatchLeaderboardReplay(id, false);
        },
        11.F);
    ImGui::PopID();
}

#include "leaderboard_rankings.inl"

void DrawLeaderboardRanking(float width, const dkr::store::Json& ranking, TrackQuery& query) {
    const auto& event = ranking["track"];
    DrawOlFeature(width, StoreText(event, "title", 120).c_str(), nullptr, [&](float inner) {
        const auto detail = StoreText(event, query.custom ? "version" : "world", 80);
        if (!detail.empty())
            OlParagraph(detail, 15.F, kOlSoft, inner);
        PaddockGap(8.F);
        auto& service = dkr::store::service();
        const OlDisabled loading(service.view()->speedrun_loading);
        auto next = query;
        next.offset = 0;
        next.vehicle = StoreText(ranking, "vehicle", 24);
        next.racer = StoreText(ranking, "racer", 96);
        next.best_lap = StoreText(ranking, "score", 6) == "lap";
        LeaderboardChoices vehicles, racers{{"", "All racers"}};
        for (const auto& vehicle : ranking["vehicles"]) {
            const auto id = vehicle.get<std::string>();
            vehicles.emplace_back(id, LeaderboardVehicle(id));
        }
        for (const auto& racer : ranking["racers"])
            racers.emplace_back(StoreText(racer, "id", 96), StoreText(racer, "name", 40));
        bool filters_changed = false;
        if (!query.full_game) {
            const float filter_width = std::min(220.F, inner);
            filters_changed = LeaderboardFilter("Racer", racers, next.racer, filter_width);
            if (!vehicles.empty()) {
                if (2 * filter_width + ImGui::GetStyle().ItemSpacing.x <= inner)
                    ImGui::SameLine();
                if (LeaderboardFilter("Vehicle", vehicles, next.vehicle, filter_width)) {
                    next.racer.clear();
                    filters_changed = true;
                }
            }
            PaddockGap(8.F);
            for (const bool lap : {false, true}) {
                if (lap)
                    ImGui::SameLine();
                if (OlButton(lap ? "Best lap" : "Full race", next.best_lap == lap ? OlTone::Go : OlTone::Plain) &&
                    next.best_lap != lap) {
                    next.best_lap = lap;
                    filters_changed = true;
                }
            }
        }
        if (filters_changed) {
            query = next;
            service.load_leaderboards({}, query);
            return;
        }
        PaddockGap(8.F);
        const auto& entries = ranking["entries"];
        if (entries.empty()) {
            OlParagraph(ranking.value("ranked", false) ? "No verified runs have been published for this selection."
                                                       : "This track and vehicle are awaiting replay qualification.",
                        15.F, kOlSoft, inner);
        } else {
            for (const auto& entry : entries) {
                DrawLeaderboardRankRow(inner, entry, query.full_game);
                PaddockGap(6.F);
            }
        }
        if (const auto offset = LeaderboardNumber(ranking, "nextOffset"); offset && OlButton("More times")) {
            next.offset = static_cast<unsigned>(offset);
            service.load_leaderboards({}, next);
        }
        if (const auto history = ranking.find("history"); history != ranking.end() && history->is_object()) {
            PaddockGap(10.F);
            DrawTrackRecordHistory(
                inner, *history, query.id + "/" + next.vehicle + "/" + next.racer + (next.best_lap ? "/lap" : "/race"));
        }
    });
    PaddockGap(12.F);
}

void RefreshLeaderboardRuns(bool review) {
    RankedJob([review, owner = RankedRunsOwner()] {
        auto reply = dkr::store::service().ranked_runs(review);
        const auto received = std::chrono::steady_clock::now();
        return [reply = std::move(reply), owner, received, review] {
            if (owner == RankedRunsOwner()) {
                g_ranked_runs_owner = owner;
                g_ranked_runs_review = review;
                g_ranked_runs = std::move(reply);
                g_ranked_runs_received = received;
            }
        };
    });
}

#include "submitted_runs.inl"

void DrawLeaderboardRuns(float width, bool review) {
    if (!review) {
        DrawSubmittedRuns(width);
        return;
    }
    static std::string selected;
    static char reason[601]{};
    DrawOlFeature(width, "Review Runs", nullptr, [&](float inner) {
        const auto owner = RankedRunsOwner();
        const OlDisabled busy(DialogJobRunning());
        if (owner.empty()) {
            OlParagraph("Sign in with Discord to see your submitted runs.", 15.F, kOlSoft, inner);
            if (OlButton("Sign in with Discord", OlTone::Go))
                dkr::store::service().login();
            return;
        }
        const bool loaded = g_ranked_runs_owner == owner && g_ranked_runs_review && g_ranked_runs.contains("runs");
        if (!loaded) {
            OlParagraph("Use Refresh to check for runs.", 15.F, kOlSoft, inner);
            return;
        }
        if (g_ranked_runs["runs"].empty())
            OlParagraph("No verified runs are awaiting review.", 15.F, kOlSoft, inner);
        for (const auto& run : g_ranked_runs["runs"]) {
            const auto id = StoreText(run, "id", 96);
            const auto state = StoreText(run, "state", 24);
            const auto ticks = LeaderboardNumber(run, "ticks");
            if (id.empty())
                continue;
            ImGui::PushID(id.c_str());
            PaddockGap(8.F);
            OlParagraph(StoreText(run, "title", 100) + " | " + StoreText(run, "name", 60) + " | " + state +
                            (ticks ? " | " + LeaderboardTime(ticks) : ""),
                        16.F, kOlInk, inner);
            if (run.value("linkedLocal", false))
                OlParagraph("Linked local run", 14.F, kOlSoft, inner);
            for (const char* field : {"notes", "reason"})
                if (const auto value = StoreText(run, field, 600); !value.empty())
                    OlParagraph(value, 14.F, kOlSoft, inner);
            if (state != "uploading") {
                {
                    const OlDisabled launch(!CanLaunchLeaderboardReplay());
                    if (OlButton("Watch replay"))
                        WatchLeaderboardReplay(id, true);
                }
            }
            if (state == "verified") {
                ImGui::SameLine();
                if (OlButton("Review") && selected != id) {
                    selected = id;
                    reason[0] = '\0';
                }
                if (selected == id) {
                    if (run.value("videoRequired", false) && StoreText(run, "videoState", 24) != "ready")
                        OlParagraph("You can review the replay here while the website video is being prepared.", 14.F,
                                    kOlSoft, inner);
                    OlInputOptions options;
                    options.hint = "Review note";
                    if (OlInput("##review-note", nullptr, reason, sizeof(reason), std::min(430.F, inner), options) ==
                        OlInputResult::Gamepad) {
                        RequestCustomTextKeyboard(
                            "Review note", reason, sizeof(reason),
                            [id](const std::string& text) {
                                if (selected == id)
                                    std::snprintf(reason, sizeof(reason), "%s", text.c_str());
                            },
                            true);
                    }
                    const auto submit = [&](bool publish) {
                        RankedJob([id, publish, note = std::string(reason), owner = RankedRunsOwner()] {
                            dkr::store::service().review_ranked_run(id, publish, note);
                            auto rows = dkr::store::service().ranked_runs(true);
                            return [owner, rows = std::move(rows)] {
                                if (owner == RankedRunsOwner()) {
                                    g_ranked_runs_review = true;
                                    g_ranked_runs = std::move(rows);
                                    g_time_trial_message = "Review saved.";
                                }
                            };
                        });
                        selected.clear();
                    };
                    if (OlButton("Publish", OlTone::Go))
                        submit(true);
                    ImGui::SameLine();
                    const OlDisabled no_note(reason[0] == '\0');
                    if (OlButton("Reject"))
                        submit(false);
                }
            }
            ImGui::PopID();
        }
    });
}

const dkr::store::Snapshot& LeaderboardLayoutFixture() {
    using Json = dkr::store::Json;
    static const auto fixture = [] {
        dkr::store::Snapshot value;
        value.speedrun_catalogue = std::make_shared<const Json>(Json::parse(R"json({
            "vanillaTracks":[
                {"id":"ancient-lake","title":"Ancient Lake","world":"Dino Domain","vehicles":["car","hovercraft","plane"]},
                {"id":"crescent-island","title":"Crescent Island","world":"Sherbet Island","vehicles":["car","hovercraft"]},
                {"id":"frosty-village","title":"Frosty Village","world":"Snowflake Mountain","vehicles":["car","plane"]}
            ],
            "boards":[{"id":"ancient-lake-car","kind":"vanilla","title":"Ancient Lake","vehicle":"car","ranked":true}],
            "customTracks":[
                {"id":"splashdown","title":"Splashdown Pass","version":"1.0","qualified":false},
                {"id":"sherbet-land","title":"Sherbet Land","version":"1.0","qualified":false}
            ], "nextCursor":""
        })json"));
        value.speedrun_board = std::make_shared<const Json>(Json::parse(R"json({
            "track":{"id":"ancient-lake","title":"Ancient Lake","world":"Dino Domain"},
            "vehicle":"car", "vehicles":["car","hovercraft","plane"], "racers":[{"id":"ancient-lake-car","name":"Diddy"}],
            "score":"race", "ranked":true,
            "entries":[{"place":1,"name":"Example Racer","racer":"Diddy","ticks":5300,"id":"fixture-one"},
                       {"place":2,"name":"Deck Racer","racer":"Diddy","ticks":5350,"id":"fixture-two"}]
        })json"));
        return value;
    }();
    return fixture;
}

void DrawCatalogueLeaders(const dkr::store::Json& track, float width, bool full_game) {
    const auto data = track.find("leaders");
    if (data == track.end() || !data->is_object())
        return;
    const auto draw_score = [&](const char* label, const dkr::store::Json& vehicle, const char* kind, float available) {
        const auto score = vehicle.find(kind);
        if (score == vehicle.end() || !score->is_object() || LeaderboardNumber(*score, "ticks") <= 0) {
            OlParagraph(std::string(label) + "  —", 14.F, kOlSoft, available);
            return;
        }
        const auto line = std::string(label) + "  " + StoreText(*score, "name", 80) + "  ·  " +
                          LeaderboardTime(LeaderboardNumber(*score, "ticks"));
        const auto type = OlRead(14.F);
        const auto badge_type = OlRead(12.F, true);
        const auto place = LocalisedUi::copy("1st");
        const float spacing = 7.F;
        const float icon_width = 14.F;
        const float badge_width = spacing + icon_width + 4.F + PaddockMeasure(badge_type, place);
        const auto lines = OlWrap(type, line, std::max(1.F, available - badge_width));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        PaddockTextStyle style;
        style.colour = PaddockCol(kOlSoft);
        PaddockDrawLines(draw, type, start, available, lines, style);
        const float x = start.x + lines.back().width + spacing;
        const float y = start.y + type.line * static_cast<float>(lines.size() - 1);
        const float top = y + (type.line - 11.F) * .5F;
        const std::array<ImVec2, 7> crown{{
            {x, top + 2.F},
            {x + 4.F, top + 5.F},
            {x + 7.F, top},
            {x + 10.F, top + 5.F},
            {x + icon_width, top + 2.F},
            {x + 12.F, top + 10.F},
            {x + 2.F, top + 10.F},
        }};
        const auto gold = PaddockCol(kOlAmber);
        draw->AddPolyline(crown.data(), static_cast<int>(crown.size()), gold, ImDrawFlags_Closed, 1.4F);
        PaddockDrawRun(draw, badge_type, {x + icon_width + 4.F, y + (type.line - badge_type.line) * .5F}, gold,
                       place.data(), place.data() + place.size());
        ImGui::Dummy({available, type.line * static_cast<float>(lines.size())});
    };
    if (full_game) {
        draw_score("Finish time", data->value("mixed", dkr::store::Json::object()), "race", width);
        return;
    }
    const auto vehicles = track.value("vehicles", dkr::store::Json::array());
    if (vehicles.empty())
        return;
    const auto columns =
        std::min<unsigned>(static_cast<unsigned>(vehicles.size()), std::max(1U, static_cast<unsigned>(width / 185.F)));
    if (!ImGui::BeginTable("TrackRecords", columns, ImGuiTableFlags_SizingStretchSame, {width, 0.F}))
        return;
    for (const auto& vehicle : vehicles) {
        const auto& id = vehicle.get_ref<const std::string&>();
        ImGui::TableNextColumn();
        const float available = ImGui::GetContentRegionAvail().x;
        OlParagraph(LeaderboardVehicle(id), 14.F, kOlAmber, available, true);
        const auto scores = data->value(id, dkr::store::Json::object());
        draw_score("Full race", scores, "race", available);
        draw_score("Best lap", scores, "lap", available);
    }
    ImGui::EndTable();
}

template <typename DrawActions>
void DrawLeaderboardTrackRow(float width, const std::string& title, const std::string& detail, DrawActions draw_actions,
                             const dkr::store::Json* records = nullptr, bool full_game = false) {
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        const auto draw_details = [&](float available) {
            OlParagraph(title, 18.F, kOlInk, available, true);
            if (!detail.empty())
                OlParagraph(detail, 14.F, kOlSoft, available);
            PaddockGap(4.F);
            draw_actions(available);
        };
        if (!records || inner < 440.F) {
            draw_details(inner);
            if (records) {
                PaddockGap(8.F);
                DrawCatalogueLeaders(*records, inner, full_game);
            }
            return;
        }
        if (!ImGui::BeginTable("TrackRow", 2, ImGuiTableFlags_SizingStretchProp, {inner, 0.F}))
            return;
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, std::clamp(inner * .22F, 160.F, 300.F));
        ImGui::TableSetupColumn("Leaders", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        draw_details(ImGui::GetContentRegionAvail().x);
        ImGui::TableNextColumn();
        DrawCatalogueLeaders(*records, ImGui::GetContentRegionAvail().x, full_game);
        ImGui::EndTable();
    });
    PaddockGap(6.F);
}

void StartAdventureRun(trial::Adventure goal, std::string board, bool signed_in, bool qualified) {
    RankedJob([goal, board = std::move(board), signed_in, qualified, config = g_config_directory] {
        std::optional<trial::Attempt> attempt;
        if (signed_in) {
            try {
                const auto reply = dkr::store::service().ranked_attempt(board);
                attempt = trial::parse_adventure_attempt(StoreText(reply, "board", 96), StoreText(reply, "engine", 64),
                                                         StoreText(reply, "ticket", 160), goal);
                if (attempt)
                    attempt->finish_targets = ReadFinishTargets(reply);
            } catch (const std::exception&) {
                attempt.reset();
            }
        }
        auto request = TimeTrialRequest{trial::Mode::Record, NewTimeTrialFile(config, board + "-Practice")};
        request.adventure = goal;
        const bool load_record = attempt.has_value() || (qualified && !signed_in);
        request.attempt = std::move(attempt);
        try {
            for (const auto& replay : ReadLocalReplayLibrary(request.replay.parent_path())) {
                if (!replay.valid || replay.time <= 0 || replay.adventure_category != trial::adventure_id(goal) ||
                    replay.ruleset != trial::kAdventureRules)
                    continue;
                const auto manifest = ReadReplaySummary(replay.file);
                const auto reference = ReadAdventureReference(manifest);
                if (!request.prior_best || replay.time < request.prior_best) {
                    request.prior_best = unsigned(replay.time);
                    request.adventure_pb = reference;
                }
                const auto splits = manifest.find("splits");
                if (splits == manifest.end() || !splits->is_array() || reference.size() != splits->size())
                    continue;
                std::string previous;
                std::int64_t previous_ticks = 0;
                for (const auto& entry : *splits) {
                    const auto label = entry[0].get<std::string>();
                    const auto ticks = reference.at(label);
                    if (ticks <= previous_ticks)
                        break;
                    const auto segment = ticks - previous_ticks;
                    auto [slot, inserted] = request.adventure_segments.try_emplace({previous, label}, segment);
                    if (!inserted)
                        slot->second = std::min(slot->second, segment);
                    previous = label;
                    previous_ticks = ticks;
                }
            }
        } catch (const std::exception&) {
            // The comparison is optional when the local replay folder is unavailable.
        }
        if (load_record) {
            try {
                const auto standings = dkr::store::service().adventure_record(std::string(trial::adventure_id(goal)));
                const auto& entries = standings.at("entries");
                if (entries.is_array() && !entries.empty())
                    request.adventure_record = ReadAdventureReference(entries.front());
            } catch (const std::exception&) {
                // A network outage should never prevent practising a route.
            }
        }
        return [request = std::move(request), signed_in]() mutable {
            QueueTimeTrialRequest(std::move(request), signed_in);
        };
    });
}

struct AdventureBoardRow {
    trial::Adventure goal;
    std::string_view id, title, description;
};
constexpr std::array kAdventureBoardRows{
    AdventureBoardRow{trial::Adventure::AnyPercent, "any-percent", "Any%", "Beat Wizpig in Future Fun Land."},
    AdventureBoardRow{trial::Adventure::HundredPercent, "100-percent", "100%",
                      "Complete the Adventure checklist and beat Wizpig."},
    AdventureBoardRow{trial::Adventure::AnyPercentGlitchless, "any-percent-glitchless", "Any% Glitchless",
                      "Defeat each boss without banned glitches."},
    AdventureBoardRow{trial::Adventure::AllTrophyRaces, "all-trophy-races", "All Trophy Races",
                      "Finish all five Trophy sets in Tracks Mode."},
    AdventureBoardRow{trial::Adventure::AllBosses, "all-bosses", "All Bosses",
                      "Win all six boss rematches from the completed Adventure slot."},
    AdventureBoardRow{trial::Adventure::AllMinigames, "all-minigames", "All Minigames",
                      "Win all four minigames in Tracks Mode."},
    AdventureBoardRow{trial::Adventure::TrophyDinoFirst, "all-trophy-races-dino-first",
                      "All Trophy Races - Dino Domain First",
                      "In Tracks Mode, race all five Trophy sets beginning in Dino Domain."},
    AdventureBoardRow{trial::Adventure::TrophyFutureFirst, "all-trophy-races-ffl-first",
                      "All Trophy Races - Future Fun Land First",
                      "In Tracks Mode, race all five Trophy sets beginning in Future Fun Land."},
    AdventureBoardRow{trial::Adventure::CarPercent, "car-percent", "Car%",
                      "Win each available car course in Tracks Mode."},
    AdventureBoardRow{trial::Adventure::HoverPercent, "hover-percent", "Hover%",
                      "Win each available hovercraft course in Tracks Mode."},
    AdventureBoardRow{trial::Adventure::PlanePercent, "plane-percent", "Plane%",
                      "Win each available plane course in Tracks Mode."},
    AdventureBoardRow{trial::Adventure::TTAmulet, "tt-amulet-no-wrong-warp", "T.T. Amulet - No Wrong Warp",
                      "Earn the four T.T. Amulet pieces through normal progression."},
};

bool QualifiedAdventureBoard(const dkr::store::Json* catalogue, const std::string& board) {
    return catalogue && catalogue->contains("boards") &&
           std::ranges::any_of(catalogue->at("boards"), [&](const auto& entry) {
               return StoreText(entry, "id", 96) == board && entry.value("ranked", false);
           });
}

std::function<void()> DrawPracticeAdventure(float width, const dkr::store::Json* catalogue, bool signed_in) {
    OlHeading("Challenge Runs", width < 275.F ? 19.F : 24.F, kOlCream, width);
    auto& selected = g_run_setup.challenge;
    static const auto categories = [] {
        std::vector<SettingsChoice> choices;
        choices.reserve(kAdventureBoardRows.size());
        for (const auto& row : kAdventureBoardRows)
            choices.emplace_back(std::string(row.title));
        return choices;
    }();
    selected = std::clamp(selected, 0, int(categories.size()) - 1);
    SettingsDropdown("run-category", "Category", &selected, categories, std::min(360.F, width));
    const auto& category = kAdventureBoardRows[static_cast<std::size_t>(selected)];
    PaddockGap(8.F);
    OlParagraph(category.description, 15.F, kOlSoft, width);
    const auto board = "adventure-" + std::string(category.id);
    const bool qualified = QualifiedAdventureBoard(catalogue, board);
    return
        [goal = category.goal, board, signed_in, qualified] { StartAdventureRun(goal, board, signed_in, qualified); };
}

void DrawAdventureBoards(float width, const dkr::store::Json* catalogue, TrackQuery& query) {
    const auto* catalogue_categories =
        catalogue && catalogue->contains("adventureCatalogue") ? &catalogue->at("adventureCatalogue") : nullptr;
    for (const auto& category : kAdventureBoardRows) {
        const std::string board = "adventure-" + std::string(category.id);
        const dkr::store::Json* records = nullptr;
        if (catalogue_categories) {
            const auto entry = std::ranges::find_if(
                *catalogue_categories, [&](const auto& row) { return StoreText(row, "id", 96) == category.id; });
            if (entry != catalogue_categories->end())
                records = &*entry;
        }
        const bool qualified = QualifiedAdventureBoard(catalogue, board);
        ImGui::PushID(category.id.data());
        DrawLeaderboardTrackRow(
            width, std::string(category.title), std::string(category.description),
            [&](float inner) {
                if (!qualified) {
                    OlParagraph("Awaiting qualification", 14.F, kOlAmber, inner);
                    return;
                }
                if (g_track_return_focus == category.id) {
                    ImGui::SetKeyboardFocusHere();
                    g_track_return_focus.clear();
                }
                if (OlButton("View Times")) {
                    query = {};
                    query.id = category.id;
                    query.full_game = true;
                    dkr::store::service().load_leaderboards({}, query);
                }
            },
            records, true);
        ImGui::PopID();
    }
}

#include "speedrunning_welcome.inl"

void DrawLeaderboardsPage(float width, bool entered, bool live) {
    enum Category { TopPlayers, Vanilla, Custom, FullGame, Runs };
    enum RunTab { NewRun, Replays, MySubmittedRuns, ReviewRuns, Options };
    static const std::vector<std::string> labels{"Top Players", "Vanilla Tracks", "Custom Tracks", "Full Game", "Runs"};
    static int category = TopPlayers;
    static RunTab run_tab = NewRun;
    static std::string last_requested_owner;
    static RunTab last_requested_tab = NewRun;
    static std::string world, vehicle;
    static char search[121]{};
    static TrackQuery query;
    static bool back_to_profile = false;
    auto& service = dkr::store::service();
    const auto published = service.view();
    const auto* snapshot = published.get();
    if (g_leaderboard_layout_check) {
        snapshot = &LeaderboardLayoutFixture();
        category = g_launcher_layout_step == 3   ? Custom
                   : g_launcher_layout_step == 4 ? FullGame
                   : g_launcher_layout_step == 2 ? Vanilla
                                                 : Runs;
        query = {g_launcher_layout_step == 2 ? "ancient-lake" : ""};
        run_tab = NewRun;
    } else if (entered) {
        const bool after_run = std::exchange(g_open_speedrunning_result, false);
        category = after_run ? Runs : TopPlayers;
        query = {};
        back_to_profile = false;
        g_track_return_focus.clear();
        run_tab = after_run && RunPreference(RunOption::ReturnTo) ? Replays : NewRun;
        last_requested_tab = NewRun;
        SelectOverallPlayer({});
        g_overall_focus.clear();
        g_overall_tab = OverallPlayerTab::Score;
        if (!DialogJobRunning())
            RefreshOverallRankings();
        g_refresh_replay_library = true;
        g_published_replay_times = {};
        if (!snapshot->speedrun_catalogue)
            service.load_leaderboards();
    }
    const auto role = StoreText(snapshot->user, "role", 20);
    const bool staff = role == "moderator" || role == "admin";
    const auto owner = RankedRunsOwner();
    const bool can_review = staff && !owner.empty();
    if (run_tab == ReviewRuns && !can_review)
        run_tab = NewRun;
    const bool showing_submissions = category == Runs && (run_tab == MySubmittedRuns || run_tab == ReviewRuns);
    const float refresh = PaddockImportButtonWidth("Refreshing...");
    const float website = PaddockImportButtonWidth("Website");
    const float actions = refresh + website + 12.F;
    DrawSharedPageHeader(
        width, live ? "LEADERBOARDS" : "SPEEDRUNNING",
        "Race for a personal best and watch how other players set theirs.", actions, [&] {
            {
                const OlDisabled loading(snapshot->speedrun_loading || DialogJobRunning() ||
                                         (showing_submissions && owner.empty()));
                if (PaddockImportButton(snapshot->speedrun_loading ? "Refreshing...###speedrun-refresh"
                                                                   : "Refresh###speedrun-refresh",
                                        std::min(width, refresh), true)) {
                    if (showing_submissions) {
                        RefreshLeaderboardRuns(run_tab == ReviewRuns);
                    } else if (category == TopPlayers) {
                        RefreshOverallRankings();
                    } else if (category == Runs && run_tab == Replays) {
                        g_refresh_replay_library = true;
                        service.load_leaderboards();
                    } else {
                        service.load_leaderboards({}, query.id.empty() ? std::nullopt : std::optional(query));
                    }
                }
            }
            if (width >= actions)
                ImGui::SameLine(0.F, 12.F);
            else
                PaddockGap(12.F);
            if (PaddockImportButton("Website###speedrun-website", std::min(width, website), false, true))
                SDL_OpenURL(kLeaderboardWebsite);
        });
    DrawLocalRunConfirmation();
    if (!live && !g_leaderboard_layout_check)
        DrawSpeedrunningWelcome(entered, !owner.empty(), staff, snapshot->speedrun_catalogue.get());
    const int next = StoreTabLine(labels, category, width, "speedrun-category", true);
    if (next != category) {
        category = next;
        query = {};
        back_to_profile = false;
        g_track_return_focus.clear();
        run_tab = NewRun;
        last_requested_tab = NewRun;
        if (category == TopPlayers && g_overall_standings.is_null() && !DialogJobRunning())
            RefreshOverallRankings();
        world.clear();
        vehicle.clear();
        search[0] = '\0';
    }
    if ((category != Runs || (run_tab != NewRun && run_tab != Options)) && !snapshot->speedrun_error.empty())
        OlParagraph(snapshot->speedrun_error, 15.F, kOlError, width);
    if (!g_time_trial_message.empty())
        OlParagraph(g_time_trial_message, 15.F, kOlSoft, width);
    PaddockGap(8.F);
    if (category == TopPlayers) {
        if (const auto selected = DrawOverallRankings(width)) {
            query = *selected;
            category = query.full_game ? FullGame : query.custom ? Custom : Vanilla;
            back_to_profile = true;
            service.load_leaderboards({}, query);
        }
        return;
    }
    if (category == Runs) {
        std::vector<std::string> run_tabs{"Run", "Replays", "My Submitted Runs"};
        if (can_review)
            run_tabs.emplace_back("Review Runs");
        const int options_index = static_cast<int>(run_tabs.size());
        run_tabs.emplace_back("Options");
        const int selected =
            StoreTabLine(run_tabs, run_tab == Options ? options_index : run_tab, width, "speedrun-runs", true);
        const auto next_tab = selected == options_index ? Options : static_cast<RunTab>(selected);
        if (next_tab == Replays && run_tab != Replays)
            g_refresh_replay_library = true;
        run_tab = next_tab;
        if (owner.empty() || (run_tab != MySubmittedRuns && run_tab != ReviewRuns)) {
            last_requested_owner.clear();
            last_requested_tab = NewRun;
        } else if (!DialogJobRunning() && (last_requested_tab != run_tab || last_requested_owner != owner)) {
            last_requested_tab = run_tab;
            last_requested_owner = owner;
            RefreshLeaderboardRuns(run_tab == ReviewRuns);
        }
        PaddockGap(8.F);
        if (run_tab == NewRun)
            DrawLeaderboardPractice(width, snapshot->speedrun_catalogue.get());
        else if (run_tab == Replays)
            DrawLocalReplays(width, snapshot->speedrun_catalogue.get());
        else if (run_tab == Options)
            DrawSpeedrunningOptions(width);
        else
            DrawLeaderboardRuns(width, run_tab == ReviewRuns);
        return;
    }
    if (!query.id.empty()) {
        if (OlButton(back_to_profile        ? "Back to player stats"
                     : category == FullGame ? "All categories"
                                            : "All tracks") ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            if (!back_to_profile)
                g_track_return_focus = query.id;
            query = {};
            if (std::exchange(back_to_profile, false)) {
                g_overall_focus = g_overall_player;
                category = TopPlayers;
                return;
            }
        } else {
            if (snapshot->speedrun_loading)
                OlParagraph("Loading times...", 15.F, kOlSoft, width);
            else if (snapshot->speedrun_board && StoreText(snapshot->speedrun_board->at("track"), "id", 96) == query.id)
                DrawLeaderboardRanking(width, *snapshot->speedrun_board, query);
            return;
        }
    }
    if (category == FullGame) {
        DrawAdventureBoards(width, snapshot->speedrun_catalogue.get(), query);
        return;
    }
    if (!snapshot->speedrun_catalogue) {
        if (snapshot->speedrun_loading)
            OlParagraph("Loading leaderboards...", 15.F, kOlSoft, width);
        return;
    }
    const auto& catalogue = *snapshot->speedrun_catalogue;
    const auto tracks =
        catalogue.value(category == Vanilla ? "vanillaTracks" : "customTracks", dkr::store::Json::array());
    constexpr float world_width = 224.F, vehicle_width = 160.F;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float filters_width = category == Vanilla ? world_width + vehicle_width + 2.F * gap : 0.F;
    const float search_width =
        std::min(category == Vanilla ? 300.F : 360.F, width >= filters_width + 176.F ? width - filters_width : width);
    OlInputOptions options;
    options.hint = "Track name";
    options.height = kSettingsDropdownHeight;
    ImGui::BeginGroup();
    SettingsLabel("Search tracks", search_width);
    if (OlInput("##find-track", nullptr, search, sizeof(search), search_width, options) == OlInputResult::Gamepad) {
        RequestCustomTextKeyboard(
            "Search tracks", search, sizeof(search),
            [](const std::string& text) { std::snprintf(search, sizeof(search), "%s", text.c_str()); }, true);
    }
    ImGui::EndGroup();
    float row_width = search_width;
    const auto draw_filter = [&](const char* label, const LeaderboardChoices& choices, std::string& value,
                                 float preferred) {
        const float field_width = std::min(preferred, width);
        if (row_width > 0.F && row_width + gap + field_width <= width + 0.1F) {
            ImGui::SameLine();
            row_width += gap;
        } else {
            row_width = 0.F;
        }
        LeaderboardFilter(label, choices, value, field_width);
        row_width += field_width;
    };
    if (category == Vanilla) {
        LeaderboardChoices worlds{{"", "All worlds"}};
        for (const auto& track : tracks) {
            auto name = StoreText(track, "world", 80);
            if (!name.empty() &&
                std::none_of(worlds.begin(), worlds.end(), [&](const auto& entry) { return entry.first == name; }))
                worlds.emplace_back(name, name);
        }
        draw_filter("World", worlds, world, world_width);
        static const LeaderboardChoices vehicles{
            {"", "All vehicles"}, {"car", "Car"}, {"hovercraft", "Hovercraft"}, {"plane", "Plane"}};
        draw_filter("Vehicle", vehicles, vehicle, vehicle_width);
    }
    PaddockGap(8.F);
    const auto needle = dkr::runtime::texture_browser::lower_ascii(search);
    std::vector<std::pair<const dkr::store::Json*, bool>> matching;
    for (const auto& track : tracks) {
        const auto title = StoreText(track, "title", 120);
        if (dkr::runtime::texture_browser::lower_ascii(title).find(needle) == std::string::npos)
            continue;
        const auto track_world = StoreText(track, "world", 80);
        const auto supported = track.value("vehicles", dkr::store::Json::array());
        if (category == Vanilla &&
            ((!world.empty() && track_world != world) ||
             (!vehicle.empty() && std::find(supported.begin(), supported.end(), vehicle) == supported.end())))
            continue;
        const auto& boards = catalogue["boards"];
        const bool qualified =
            category == Custom ? track.value("qualified", false)
                               : std::any_of(boards.begin(), boards.end(), [&](const auto& board) {
                                     return StoreText(board, "kind", 20) == "vanilla" &&
                                            StoreText(board, "title", 120) == title && board.value("ranked", false) &&
                                            (vehicle.empty() || StoreText(board, "vehicle", 24) == vehicle);
                                 });
        matching.emplace_back(&track, qualified);
    }
    OlParagraph(matching.empty() ? "No tracks match these filters."
                                 : std::to_string(matching.size()) + " of " + std::to_string(tracks.size()) + " tracks",
                14.F, kOlSoft, width);
    for (const auto& [track, qualified] : matching) {
        const auto id = StoreText(*track, "id", 96);
        const auto title = StoreText(*track, "title", 120);
        std::string detail =
            category == Custom ? "Version " + StoreText(*track, "version", 40) : StoreText(*track, "world", 80);
        for (const auto& choice : track->value("vehicles", dkr::store::Json::array()))
            detail += " | " + LeaderboardVehicle(choice.get<std::string>());
        ImGui::PushID(id.c_str());
        DrawLeaderboardTrackRow(
            width, title, detail,
            [&](float inner) {
                if (!qualified) {
                    OlParagraph("Awaiting qualification", 14.F, kOlAmber, inner);
                    return;
                }
                const OlDisabled loading(snapshot->speedrun_loading);
                if (g_track_return_focus == id) {
                    ImGui::SetKeyboardFocusHere();
                    g_track_return_focus.clear();
                }
                if (OlButton("View Times")) {
                    query = {id, category == Vanilla ? vehicle : std::string{}, {}, category == Custom};
                    service.load_leaderboards({}, query);
                }
            },
            track->contains("leaders") ? track : nullptr);
        ImGui::PopID();
    }
    if (category == Custom) {
        if (const auto cursor = StoreText(catalogue, "nextCursor", 96); !cursor.empty()) {
            const OlDisabled busy(snapshot->speedrun_loading);
            if (OlButton("More tracks"))
                service.load_leaderboards(cursor);
        }
    }
}
