struct LocalReplay {
    std::filesystem::path file;
    std::filesystem::file_time_type saved;
    std::string title, course, racer, vehicle, board, posted_owner, adventure_category, ruleset, track_id, track_hash;
    std::int64_t time = 0, best_lap = 0;
    bool practice = true;
    bool valid = false;
    bool can_ghost = false;
    bool personal_best = false;
    bool beat_ghost = false;
    bool has_attempt = false;
    bool local_capture = false;
};

std::vector<LocalReplay> g_local_replays;
std::filesystem::path g_replay_library_profile;
bool g_refresh_replay_library = true;

constexpr std::size_t kReplayLibraryBytes = 3 * 1024 * 1024;

dkr::store::Json ReadReplaySummary(const std::filesystem::path& file) {
    const auto bytes = std::filesystem::file_size(file);
    std::ifstream input(file, std::ios::binary);
    std::array<unsigned char, 12> header{};
    if (bytes > kReplayLibraryBytes || !input.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        std::string_view(reinterpret_cast<const char*>(header.data()), 8) != "DKRREPL2")
        throw std::runtime_error("This file is not a supported DKR-R replay.");
    const auto length = std::uint32_t(header[8]) | (std::uint32_t(header[9]) << 8) | (std::uint32_t(header[10]) << 16) |
                        (std::uint32_t(header[11]) << 24);
    if (!length || length > 65536 || length > bytes - header.size())
        throw std::runtime_error("The replay metadata is incomplete.");
    std::string metadata(length, '\0');
    if (!input.read(metadata.data(), metadata.size()))
        throw std::runtime_error("The replay metadata could not be read.");
    auto manifest = dkr::store::Json::parse(metadata);
    const auto rules = StoreText(manifest, "ruleset", 48);
    const bool adventure = rules == trial::kAdventureRules || rules == trial::kAdventureOriginalRouteRules ||
                           rules == trial::kAdventureMilestoneRules || rules == trial::kAdventureColdSaveRules ||
                           rules == trial::kAdventureVisualRngRules || rules == trial::kAdventureSharedRngRules ||
                           rules == trial::kLegacyAdventureRules;
    const auto ticks = LeaderboardNumber(manifest, "ticks");
    const auto clock = LeaderboardNumber(manifest, "clockTicks");
    if ((!adventure && rules != trial::kLegacyRules && rules != trial::kVanillaRules && rules != trial::kCustomRules &&
         rules != trial::kCustomVariableLapsRules) ||
        !ticks || !clock || clock > ticks * 2 || bytes != header.size() + length + ticks * 4 ||
        !trial::supported_replay_rom(manifest.value("rom", 0U)))
        throw std::runtime_error("The replay format or recording length is unsupported.");
    return manifest;
}

std::filesystem::path ReplaySidecar(std::filesystem::path file) {
    return file += ".json";
}

bool ReplayLibraryFile(const std::filesystem::path& file) {
    std::error_code ec;
    return std::filesystem::symlink_status(file, ec).type() == std::filesystem::file_type::regular;
}

dkr::store::Json ReadReplaySidecar(const std::filesystem::path& file) {
    const auto path = ReplaySidecar(file);
    if (ReplayLibraryFile(path)) {
        try {
            const auto text = dkr::updates::read_text(path, 4096);
            auto metadata = dkr::store::Json::parse(text, nullptr, false);
            if (metadata.is_object())
                return metadata;
        } catch (const std::exception&) {
            // Sidecar errors must not hide a playable replay.
        }
    }
    return dkr::store::Json::object();
}

void WriteReplaySidecar(const std::filesystem::path& file, const dkr::store::Json& metadata) {
    dkr::updates::durable_write(ReplaySidecar(file), metadata.dump());
}

void CheckManagedReplayDirectory(const std::filesystem::path& directory) {
    std::error_code ec;
    const auto state = std::filesystem::symlink_status(directory, ec).type();
    if (state != std::filesystem::file_type::directory && state != std::filesystem::file_type::not_found)
        throw std::runtime_error("The replay folder is unavailable.");
}

bool OwnReplayFile(const std::filesystem::path& file, const dkr::store::Json& metadata) {
    const auto name = PathUtf8(file.filename());
    const auto origin = StoreText(metadata, "origin", 20);
    return file.extension() == ".dkrreplay" && !name.starts_with("Replay-") && !name.starts_with("Imported-") &&
           origin != "Downloaded" && origin != "Imported" && ReplayLibraryFile(file);
}

bool ReplayHasRankedAttempt(const dkr::store::Json& manifest) {
    return !manifest.value("practice", true) && !StoreText(manifest, "board", 96).empty() &&
           !StoreText(manifest, "ticket", 160).empty();
}

bool IsLocalReplayCapture(const dkr::store::Json& manifest) {
    return manifest.value("practice", false) && !manifest.contains("ticket") && !manifest.contains("board") &&
           !manifest.contains("engine") && !manifest.contains("releaseHash") && !manifest.at("checkpoints").empty();
}

std::vector<LocalReplay> ReadLocalReplayLibrary(const std::filesystem::path& directory) {
    CheckManagedReplayDirectory(directory);
    std::vector<LocalReplay> replays;
    if (!std::filesystem::exists(directory))
        return replays;
    const auto installed = dkr::runtime::custom_tracks::installed_replay_tracks();
    std::map<std::string, std::size_t> records;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto& file = entry.path();
        if (file.extension() != ".dkrreplay" || !ReplayLibraryFile(file))
            continue;
        const auto metadata = ReadReplaySidecar(file);
        if (!OwnReplayFile(file, metadata))
            continue;
        LocalReplay item;
        item.file = file;
        std::error_code ec;
        item.saved = entry.last_write_time(ec);
        item.title = StoreText(metadata, "title", 120);
        item.posted_owner = StoreText(metadata, "postedOwner", 96);
        std::string record;
        try {
            const auto manifest = ReadReplaySummary(file);
            const auto rules = StoreText(manifest, "ruleset", 48);
            item.ruleset = rules;
            if (rules.starts_with("adventure-")) {
                const auto id = StoreText(manifest, "category", 40);
                item.adventure_category = id;
                const auto category = trial::adventure_for(id);
                item.board = "adventure-" + id;
                record = rules + ":" + id;
                for (const auto& choice : trial::kAdventureCategories)
                    if (choice.goal == category)
                        item.course = choice.title;
            } else {
                const auto racer = manifest.value("racer", trial::kDiddy);
                if (racer >= trial::kRacers.size())
                    throw std::runtime_error("The replay racer is unsupported.");
                item.racer = trial::kRacers[racer].name;
                const auto vehicle = rules == trial::kLegacyRules ? "car" : StoreText(manifest, "vehicle", 16);
                trial::Vehicle route_vehicle = trial::Vehicle::Car;
                for (unsigned i = 0; i < trial::kVehicleIds.size(); ++i) {
                    if (trial::kVehicleIds[i] == vehicle) {
                        item.vehicle = trial::kVehicleNames[i];
                        route_vehicle = static_cast<trial::Vehicle>(i);
                    }
                }
                if (item.vehicle.empty())
                    throw std::runtime_error("The replay vehicle is unsupported.");
                if (rules == trial::kLegacyRules) {
                    item.course = trial::kCourses.front().name;
                    item.board = trial::board_for(trial::kCourses.front(), route_vehicle, racer);
                    record = trial::kCourses.front().id;
                } else if (rules == trial::kVanillaRules) {
                    if (const auto* course = trial::course_for(StoreText(manifest, "course", 64))) {
                        item.course = course->name;
                        item.board = trial::board_for(*course, route_vehicle, racer);
                        record = course->id;
                    }
                } else {
                    item.track_id = StoreText(manifest, "trackId", 96);
                    item.track_hash = StoreText(manifest, "trackHash", 64);
                    item.course = item.track_id;
                    record = "custom:" + item.track_id + ":" + item.track_hash;
                    const auto found =
                        std::ranges::find(installed, item.course, &dkr::runtime::custom_tracks::ReplayTrack::id);
                    if (found != installed.end())
                        item.course = found->name;
                    else
                        std::replace(item.course.begin(), item.course.end(), '-', ' ');
                }
                record += ":" + vehicle + ":" + std::to_string(racer);
                const auto splits = manifest.value("splits", dkr::store::Json::array());
                const auto laps =
                    rules == trial::kCustomVariableLapsRules ? manifest.value("laps", 0U) : trial::kRetailLapCount;
                if (laps > 0 && laps <= trial::kMaximumLapCount && splits.is_array() && splits.size() == laps) {
                    std::int64_t previous = 0;
                    for (const auto& value : splits) {
                        const auto end = value.get<std::int64_t>();
                        if (end <= previous || end > LeaderboardNumber(manifest, "clockTicks")) {
                            item.best_lap = 0;
                            break;
                        }
                        const auto lap = end - previous;
                        item.best_lap = item.best_lap ? std::min(item.best_lap, lap) : lap;
                        previous = end;
                    }
                }
            }
            item.time = LeaderboardNumber(manifest, "clockTicks");
            item.beat_ghost = item.time > 0 && item.time < LeaderboardNumber(metadata, "ghostTicks");
            item.has_attempt = ReplayHasRankedAttempt(manifest);
            if (item.has_attempt)
                item.board = StoreText(manifest, "board", 96);
            item.practice =
                manifest.value("practice", true) || PathUtf8(file.filename()).find("-Practice-") != std::string::npos;
            item.valid = !item.course.empty();
            item.local_capture = item.valid && IsLocalReplayCapture(manifest) &&
                                 (!rules.starts_with("adventure-") || rules == trial::kAdventureRules);
            item.can_ghost = item.valid && !rules.starts_with("adventure-");
        } catch (const std::exception&) {
            // Keep malformed files visible so players can remove them.
        }
        if (item.title.empty())
            item.title = item.valid ? item.course : PathUtf8(file.filename());
        if (item.valid) {
            const auto [best, inserted] = records.try_emplace(record, replays.size());
            if (inserted) {
                item.personal_best = true;
            } else {
                auto& prior = replays[best->second];
                if (std::tie(item.time, item.saved, item.file) < std::tie(prior.time, prior.saved, prior.file)) {
                    prior.personal_best = false;
                    item.personal_best = true;
                    best->second = replays.size();
                }
            }
        }
        replays.push_back(std::move(item));
    }
    std::ranges::sort(
        replays, [](const auto& a, const auto& b) { return a.saved != b.saved ? a.saved > b.saved : a.file > b.file; });
    return replays;
}

const LocalReplay* FindLocalReplay(const std::filesystem::path& file) {
    if (g_replay_library_profile != g_config_directory)
        return nullptr;
    const auto replay = std::ranges::find(g_local_replays, file, &LocalReplay::file);
    return replay == g_local_replays.end() ? nullptr : &*replay;
}

std::string LocalReplayDate(std::time_t time, const char* format) {
    std::tm calendar{};
#ifdef _WIN32
    if (localtime_s(&calendar, &time) != 0)
        return {};
#else
    if (!localtime_r(&time, &calendar))
        return {};
#endif
    char date[40]{};
    std::strftime(date, sizeof(date), format, &calendar);
    return date;
}

std::string ReplayDate(std::filesystem::file_time_type saved) {
    if (saved == std::filesystem::file_time_type::min())
        return {};
#ifdef _MSC_VER
    const auto stamp = std::chrono::clock_cast<std::chrono::system_clock>(saved);
#else
    const auto stamp = std::chrono::file_clock::to_sys(saved);
#endif
    const auto system_stamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(stamp);
    return LocalReplayDate(std::chrono::system_clock::to_time_t(system_stamp), "%Y-%m-%d");
}

void RenameLocalReplay(const std::filesystem::path& file, const std::string& title) {
    CheckManagedReplayDirectory(file.parent_path());
    if (!ReplayLibraryFile(file))
        throw std::runtime_error("This replay is no longer in your library.");
    auto metadata = ReadReplaySidecar(file);
    metadata["title"] = title;
    WriteReplaySidecar(file, metadata);
}

void RemoveLocalReplay(const std::filesystem::path& file) {
    CheckManagedReplayDirectory(file.parent_path());
    if (!ReplayLibraryFile(file) || !std::filesystem::remove(file))
        throw std::runtime_error("This replay could not be removed.");
    for (const auto& sidecar : {ReplaySidecar(file), trial::portable_ghost_file(file)}) {
        if (ReplayLibraryFile(sidecar)) {
            std::error_code ec;
            std::filesystem::remove(sidecar, ec);
        }
    }
}

using ReplayPurgeSelection = std::map<std::filesystem::path, std::filesystem::file_time_type>;

ReplayPurgeSelection SelectReplayPurge(const std::vector<LocalReplay>& replays, bool include_bests,
                                       bool include_unsubmitted) {
    ReplayPurgeSelection selected;
    for (const auto& replay : replays) {
        if (dkr::library_ui::can_purge_replay(replay.personal_best, !replay.posted_owner.empty(), include_bests,
                                              include_unsubmitted))
            selected.emplace(replay.file, replay.saved);
    }
    return selected;
}

std::pair<std::size_t, std::size_t> PurgeLocalReplays(const std::filesystem::path& directory,
                                                      const ReplayPurgeSelection& confirmed, bool include_bests,
                                                      bool include_unsubmitted) {
    const auto current = SelectReplayPurge(ReadLocalReplayLibrary(directory), include_bests, include_unsubmitted);
    std::size_t removed = 0;
    for (const auto& [file, saved] : confirmed) {
        const auto it = current.find(file);
        if (it == current.end() || it->second != saved)
            continue;
        try {
            RemoveLocalReplay(file);
            ++removed;
        } catch (const std::exception&) {
            // Continue with other confirmed recordings when one is in use.
        }
    }
    return {removed, confirmed.size() - removed};
}

void RefreshReplayLibrary() {
    const auto profile = g_config_directory;
    g_refresh_replay_library = false;
    RankedJob([profile] {
        auto replays = ReadLocalReplayLibrary(profile / "speedruns");
        return [profile, replays = std::move(replays)]() mutable {
            if (profile == g_config_directory) {
                g_replay_library_profile = profile;
                g_local_replays = std::move(replays);
            }
        };
    });
}
