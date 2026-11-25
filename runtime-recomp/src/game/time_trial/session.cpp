#include "session.hpp"
#include "ghost_file.hpp"
#include "options.hpp"

#include "achievements.hpp"
#include "atomic_snapshot.hpp"
#include "capture_symbols.hpp"
#include "custom_tracks.hpp"
#include "game_payload.hpp"
#include "guest_call_context.hpp"
#include "mods/magic_code_package.hpp"
#include "netplay/check_autopilot.hpp"
#include "revision_addresses.hpp"
#include "runtime_netplay.hpp"
#include "time_trial_symbols.hpp"
#include "ultramodern/ultramodern.hpp"
#include "updates/update_process.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <numbers>
#include <string_view>
#include <thread>
#include <utility>

#if defined(__linux__) && !defined(__ANDROID__)
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dkr::runtime::time_trial {
namespace {
using mods::Bytes;
using mods::code::Json;
using netplay::PackedInput;
using netplay::experimental::AutopilotState;
namespace addresses = revision_addresses;
using speedrunning::Option;
using speedrunning::options;

constexpr unsigned TailUpdates = 210; // Seven seconds of neutral input at 30 Hz.
constexpr unsigned BootSeed = 0x444B5252;
constexpr unsigned LogicRate = 2; // NTSC clock units per 30 Hz simulation update.
constexpr unsigned CheckpointInterval = 600;
constexpr unsigned MaxUpdates = 54000;
constexpr std::string_view Signature = "DKRREPL2";
constexpr std::size_t HeaderBytes = 12;
constexpr std::size_t MaxManifest = 16384;
constexpr std::size_t MaxAdventureManifest = 65536;
constexpr std::size_t MaxReplayBytes = HeaderBytes + MaxAdventureManifest + kAdventureMaxUpdates * 4;

std::atomic<Mode> mode{Mode::None};
std::filesystem::path replay_path;
std::atomic<int> exit_status{1};
std::atomic<int> capture_socket{-1};
std::atomic<bool> launcher_return_enabled{false};
std::atomic<std::int64_t> launcher_return_started{0};
std::mutex progress_mutex;
Progress current_progress;
Theatre transport;
Progress visible_progress;
std::chrono::steady_clock::time_point next_present;
bool flushed_last_graphics = false;
bool archive_error = false;
std::chrono::steady_clock::time_point podium_exit_at;
unsigned prior_finish_personal_best = 0;
bool replay_audio_reset = false;
struct AudioBlock {
    unsigned frame;
    std::vector<std::int16_t> samples;
};
std::vector<AudioBlock> audio_blocks;
std::vector<Progress> timeline_progress;
std::deque<Progress> movie_progress;
std::size_t captured_audio_samples = 0;
unsigned mixer_update = 0;
unsigned audio_ready = 0;
std::vector<std::int16_t> present_audio;
std::deque<std::pair<std::uint32_t, FrameStamp>> graphics_stamps;
struct GhostPose {
    std::array<std::uint32_t, 3> position;
    std::array<std::uint16_t, 3> rotation;
    std::uint16_t segment;
};
struct GhostPreparation {
    std::filesystem::path recording;
    std::filesystem::path track_package;
    std::filesystem::path cache_file;
    unsigned player;
    Mode practice_mode;
    std::optional<Attempt> attempt;
    std::shared_ptr<const std::string> credited_name;
};
struct GhostObjectState {
    std::uint32_t seed;
    std::uint16_t map, vehicle, character, time;
};
std::optional<GhostPreparation> ghost_preparation;
std::optional<GhostObjectState> ghost_object_state;
AtomicSnapshot<const std::string> ghost_credited_name;
std::atomic<bool> ghost_race_active{false};
std::vector<GhostPose> ghost_poses;
std::string ghost_replay_digest;
std::filesystem::path ghost_export;
unsigned ghost_racer = kDiddy;
unsigned ghost_clock = 0;
std::array<unsigned, kMaximumLapCount> ghost_splits{};
std::atomic<bool> restart_confirmation{false};
std::optional<Attempt> ranked_attempt;
Result result;
bool launched = false;
bool sampled = false;
std::uint32_t update = 0;
PackedInput previous{};
AutopilotState autopilot;
Bytes inputs, observations;
Json checkpoints = Json::array();
Json expected;
Json finished_record;
unsigned racer_choice = kDiddy;
const Course* selected_course = &kCourses.front();
Vehicle selected_vehicle = Vehicle::Car;
Adventure adventure_goal = Adventure::None;
bool legacy_adventure = false;
bool adventure_isolates_effects = false;
bool fresh_adventure_save = false;
bool adventure_intro_seen = false;
bool attest_adventure_splits = false;
bool use_adventure_route_splits = false;
bool use_completed_save_rules = false;
AdventureSplits reached_splits;
AdventureProgress previous_adventure_progress;
unsigned previous_taj_flags = 0;
std::string custom_track_id;
std::string custom_track_hash;
std::string custom_track_name;
unsigned selected_level = 0;
unsigned selected_laps = kRetailLapCount;
unsigned adventure_start = 0;
int adventure_slot = -1;

bool replay_hash(std::string_view hash) {
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == hash.npos;
}

bool replay_id(std::string_view id) {
    return !id.empty() && id.size() <= 96 &&
           id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == id.npos;
}

bool valid_attempt(std::string_view board, std::string_view engine, std::string_view ticket) {
    return replay_id(board) && replay_hash(engine) && ticket.size() >= 40 && ticket.size() <= 160 &&
           ticket.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") == ticket.npos;
}

bool legacy_course() {
    return custom_track_id.empty() && selected_course == &kCourses.front() && selected_vehicle == Vehicle::Car;
}
unsigned race_finish = 0;

gpr guest(std::uint32_t address) {
    return static_cast<std::int32_t>(address);
}

bool in_rdram(std::uint32_t address, std::uint32_t bytes) {
    return address >= 0x80000000U && address <= 0x80800000U - bytes;
}

void append_word(Bytes& bytes, std::uint32_t value) {
    for (unsigned bit = 0; bit < 32; bit += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> bit));
}

std::uint32_t word_at(mods::View bytes, std::size_t index) {
    return std::uint32_t(bytes[index]) | (std::uint32_t(bytes[index + 1]) << 8) |
           (std::uint32_t(bytes[index + 2]) << 16) | (std::uint32_t(bytes[index + 3]) << 24);
}

constexpr std::string_view GhostCacheSignature = "DKRGHOST1";
constexpr std::size_t DigestLength = 64;
constexpr std::size_t GhostPoseBytes = kGhostPoseBytes;

std::string ghost_cache_prefix() {
    // Playtest builds can share a version label while running different code.
    static const auto runtime_hash = updates::hash_file(updates::application_executable());
    return std::string(GhostCacheSignature) + ghost_replay_digest + runtime_hash;
}

std::vector<GhostPose> load_ghost_file(const std::filesystem::path& path, unsigned frames, std::string_view prefix) {
    try {
        if (!std::filesystem::exists(path))
            return {};
        updates::require_private_tree(path.parent_path());
        const auto header = prefix.size() + DigestLength;
        const auto size = header + std::size_t(frames) * GhostPoseBytes;
        const auto cache = updates::read_text(path, size);
        if (cache.size() != size || !cache.starts_with(prefix))
            return {};
        const mods::View data(reinterpret_cast<const std::uint8_t*>(cache.data() + header), cache.size() - header);
        if (std::string_view(cache).substr(prefix.size(), DigestLength) != mods::sha256(data))
            return {};

        std::vector<GhostPose> poses;
        poses.reserve(frames);
        for (std::size_t offset = 0; offset < data.size(); offset += GhostPoseBytes) {
            GhostPose pose{
                {word_at(data, offset), word_at(data, offset + 4), word_at(data, offset + 8)},
                {std::uint16_t(word_at(data, offset + 12)), std::uint16_t(word_at(data, offset + 12) >> 16),
                 std::uint16_t(word_at(data, offset + 16))},
                std::uint16_t(word_at(data, offset + 16) >> 16),
            };
            if (std::ranges::any_of(pose.position,
                                    [](auto bits) { return !std::isfinite(std::bit_cast<float>(bits)); }))
                return {};
            poses.push_back(pose);
        }
        return poses;
    } catch (const std::exception&) {
        // Unavailable or damaged cache files never prevent normal preparation.
        return {};
    }
}

std::string encode_ghost(std::string prefix, std::span<const GhostPose> poses) {
    Bytes payload;
    payload.reserve(poses.size() * GhostPoseBytes);
    for (const auto& pose : poses) {
        for (auto coordinate : pose.position)
            append_word(payload, coordinate);
        append_word(payload, std::uint32_t(pose.rotation[0]) | (std::uint32_t(pose.rotation[1]) << 16));
        append_word(payload, std::uint32_t(pose.rotation[2]) | (std::uint32_t(pose.segment) << 16));
    }
    prefix += mods::sha256(payload);
    prefix.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    return prefix;
}

void export_verified_ghost() {
    if (ghost_export.empty())
        return;
    try {
        const auto file = encode_ghost(portable_ghost_prefix(ghost_replay_digest), ghost_poses);
        mods::write_new_file(ghost_export, mods::View(reinterpret_cast<const std::uint8_t*>(file.data()), file.size()));
        std::fprintf(stderr, "[ghost] exported %zu verified updates\n", ghost_poses.size());
    } catch (const std::exception& failure) {
        // The caller treats a missing export as a retryable verifier failure.
        std::fprintf(stderr, "[ghost] could not export verified ghost: %s\n", failure.what());
    }
}

void save_ghost_cache(const std::filesystem::path& path, std::span<const GhostPose> poses) {
    try {
        const auto cache = encode_ghost(ghost_cache_prefix(), poses);
        const auto folder = path.parent_path();
        std::filesystem::create_directories(folder.parent_path());
        updates::require_private_tree(folder.parent_path());
        std::filesystem::create_directory(folder);
        updates::require_private_tree(folder);
        const updates::InstallLock lock(folder);
        updates::durable_write(path, cache);
        std::fprintf(stderr, "[ghost] cached %zu updates\n", poses.size());
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "[ghost] could not save cache: %s\n", failure.what());
    }
}

std::uint32_t player_address(std::uint8_t* rdram) {
    const auto ports = std::uint32_t(MEM_W(0, guest(addresses::current().RacersByPort)));
    if (!in_rdram(ports, 4))
        return 0;
    const auto player = std::uint32_t(MEM_W(0, guest(ports)));
    return in_rdram(player, 0x68) ? player : 0;
}

bool playback() {
    const auto action = mode.load();
    return action == Mode::Verify || action == Mode::Watch || action == Mode::PrepareGhost;
}

unsigned active_replay_rom() {
    switch (addresses::gSelectedRevision) {
    case rom::Revision::UsV77:
        return 77;
    case rom::Revision::UsV80:
        return 80;
    default:
        return 0;
    }
}

std::int64_t return_clock() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void exit_run() {
    if (launcher_return_enabled && (mode == Mode::Record || mode == Mode::Watch)) {
        std::int64_t not_started = 0;
        launcher_return_started.compare_exchange_strong(not_started, return_clock());
        return;
    }
    ultramodern::quit();
}

void stop(bool passed, const char* detail, bool request_quit = true) {
    {
        std::scoped_lock lock(progress_mutex);
        result = {passed ? replay_path : std::filesystem::path{},
                  detail,
                  current_progress.clock,
                  current_progress.best_lap,
                  passed,
                  ranked_attempt.has_value(),
                  playback()};
    }
    exit_status.store(passed ? 0 : 6);
    const unsigned proof_update = passed && exporting_video() && race_finish ? race_finish : update;
    std::fprintf(stderr, "[time-trial] %s update=%u %s\n", passed ? "PASS" : "FAIL", proof_update, detail);
    std::fflush(stderr);
    if (request_quit && (!watching() || !passed))
        exit_run();
}

bool replay_rom_matches(unsigned revision) {
    if (!playback() || expected.at("rom").get<unsigned>() == revision)
        return true;
    stop(false, "This replay needs the US ROM revision used to record it.");
    return false;
}

void bind_custom_track(std::string id, const std::filesystem::path& package, std::string_view expected_hash) {
    const auto source = package.empty() ? custom_tracks::installed_replay_source(id) : package;
    if (source.empty() || !source.is_absolute())
        throw mods::Error("Install the recorded .dkrmap package or provide its absolute package directory.");
    custom_tracks::Track track;
    std::string error;
    if (!custom_tracks::inspect_package(source, track, error))
        throw mods::Error("Cannot verify the .dkrmap package. " + error);
    if (track.id != id || !track.level_links.empty())
        throw mods::Error("This replay needs an exact standalone .dkrmap package.");
    if (!(custom_tracks::replay_vehicles(track) & (1U << static_cast<unsigned>(selected_vehicle))))
        throw mods::Error("Choose a one-to-five-lap racing track and a vehicle it supports.");
    selected_laps = custom_tracks::replay_laps(track);
    Json entries = Json::array();
    for (const auto& entry : track.entries)
        entries.push_back(Json::array({static_cast<unsigned>(entry.section), static_cast<unsigned>(entry.slot),
                                       entry.ordinal, mods::sha256(entry.bytes)}));
    const auto canonical = Json::array({kCustomRules, id, entries}).dump();
    const auto hash =
        mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()));
    if (!expected_hash.empty() && hash != expected_hash)
        throw mods::Error("The installed track does not match the replay package.");
    custom_track_name = track.name;
    custom_track_id = std::move(id);
    custom_track_hash = hash;
    track.enabled = true;
    custom_tracks::begin_replay_track(std::move(track));
}

void read_replay(const std::filesystem::path& track_package, bool prepare_ghost) {
    const auto file = mods::read_file(replay_path, MaxReplayBytes);
    const auto error = [] { return mods::Error("The replay has an unsupported format or rules."); };
    if (file.size() < HeaderBytes || !std::equal(Signature.begin(), Signature.end(), file.begin()))
        throw error();
    const auto size = word_at(file, Signature.size());
    if (size > MaxAdventureManifest || size > file.size() - HeaderBytes)
        throw error();
    expected = mods::code::parse_document(mods::slice(file, HeaderBytes, size), MaxAdventureManifest);
    if (!supported_replay_rom(expected.at("rom").get<unsigned>()) || expected.at("startSeed") != BootSeed)
        throw error();
    const auto rules = expected.at("ruleset").get<std::string>();
    if (rules == kAdventureRules || rules == kAdventureOriginalRouteRules || rules == kAdventureMilestoneRules ||
        rules == kAdventureColdSaveRules || rules == kAdventureVisualRngRules || rules == kAdventureSharedRngRules ||
        rules == kLegacyAdventureRules) {
        legacy_adventure = rules == kLegacyAdventureRules;
        use_completed_save_rules = rules == kAdventureRules;
        use_adventure_route_splits = use_completed_save_rules || rules == kAdventureOriginalRouteRules;
        attest_adventure_splits = use_adventure_route_splits || rules == kAdventureMilestoneRules;
        adventure_goal = adventure_for(expected.at("category").get<std::string>());
        fresh_adventure_save =
            (attest_adventure_splits || rules == kAdventureColdSaveRules) && !uses_completed_adventure_save();
        adventure_isolates_effects =
            fresh_adventure_save || uses_completed_adventure_save() || rules == kAdventureVisualRngRules;
        const auto count = expected.at("ticks").get<unsigned>();
        const auto clock = expected.at("clockTicks").get<unsigned>();
        const auto& checks = expected.at("checkpoints");
        const bool practice = expected.at("practice").get<bool>();
        if ((!practice && (!adventure_isolates_effects || !expected.at("releaseHash").is_null() ||
                           !parse_adventure_attempt(expected.at("board").get<std::string>(),
                                                    expected.at("engine").get<std::string>(),
                                                    expected.at("ticket").get<std::string>(), adventure_goal))) ||
            adventure_goal == Adventure::None ||
            (!use_adventure_route_splits && adventure_goal > Adventure::TTAmulet) || !track_package.empty() ||
            count == 0 || count > kAdventureMaxUpdates || clock == 0 || clock > count * LogicRate ||
            expected.at("finishTick") != count || file.size() != HeaderBytes + size + count * 4 || !checks.is_array() ||
            checks.size() != (count + kAdventureInterval - 1) / kAdventureInterval)
            throw error();
        inputs.assign(file.begin() + HeaderBytes + size, file.end());
        current_progress.finish_update = count;
        return;
    }
    if (size > MaxManifest)
        throw error();
    if (rules == kLegacyRules) {
        selected_course = &kCourses.front();
        selected_vehicle = Vehicle::Car;
        if (expected.contains("course") || expected.contains("vehicle"))
            throw error();
    } else if (rules == kVanillaRules) {
        selected_course = course_for(expected.at("course").get<std::string>());
        const auto id = expected.at("vehicle").get<std::string>();
        const auto vehicle = std::find(kVehicleIds.begin(), kVehicleIds.end(), id);
        if (!selected_course || vehicle == kVehicleIds.end())
            throw error();
        selected_vehicle = static_cast<Vehicle>(vehicle - kVehicleIds.begin());
        if (!selected_course->supports(selected_vehicle) || legacy_course())
            throw error();
    } else if (rules == kCustomRules || rules == kCustomVariableLapsRules) {
        const auto id = expected.at("vehicle").get<std::string>();
        const auto vehicle = std::find(kVehicleIds.begin(), kVehicleIds.end(), id);
        const auto hash = expected.at("trackHash").get<std::string>();
        if (vehicle == kVehicleIds.end() || !replay_hash(hash) || expected.contains("course") ||
            (!expected.at("practice").get<bool>() &&
             (!valid_attempt(expected.at("board").get<std::string>(), expected.at("engine").get<std::string>(),
                             expected.at("ticket").get<std::string>()) ||
              !replay_hash(expected.at("releaseHash").get<std::string>()))))
            throw error();
        selected_vehicle = static_cast<Vehicle>(vehicle - kVehicleIds.begin());
        bind_custom_track(expected.at("trackId").get<std::string>(), track_package, hash);
        if (rules == kCustomVariableLapsRules
                ? (selected_laps == kRetailLapCount || expected.at("laps") != selected_laps)
                : (selected_laps != kRetailLapCount || expected.contains("laps")))
            throw error();
    } else {
        throw error();
    }
    if (!track_package.empty() && custom_track_id.empty())
        throw error();
    const auto updates = expected.at("ticks").get<unsigned>();
    if (!updates || updates > MaxUpdates + TailUpdates || file.size() != HeaderBytes + size + updates * 4)
        throw error();
    const auto finish = expected.value("finishTick", updates);
    racer_choice = expected.value("racer", kDiddy);
    if (!finish || finish > MaxUpdates || finish > updates || updates - finish > TailUpdates ||
        racer_choice >= kRacers.size() ||
        std::any_of(file.begin() + HeaderBytes + size + finish * 4, file.end(), [](auto value) { return value != 0; }))
        throw error();
    if (custom_track_id.empty() && !expected.value("practice", true) &&
        expected.at("board") != board_for(*selected_course, selected_vehicle, racer_choice))
        throw error();
    const auto& splits = expected.at("splits");
    const auto& checks = expected.at("checkpoints");
    if (!splits.is_array() || splits.size() != selected_laps || splits.back() != expected.at("clockTicks") ||
        !checks.is_array() || checks.size() != (finish + CheckpointInterval - 1) / CheckpointInterval)
        throw error();
    inputs.assign(file.begin() + HeaderBytes + size, file.end());
    current_progress.finish_update = finish;
    if (prepare_ghost)
        ghost_replay_digest = mods::sha256(file);
}

// The caller holds progress_mutex.
void set_seek_target(unsigned target) {
    transport.seek_to = std::clamp(target, 1U, visible_progress.total_updates);
    transport.seeking = transport.seek_to != visible_progress.updates;
    transport.finished = transport.seek_to == visible_progress.total_updates && !transport.seeking;
    replay_audio_reset = true;
    present_audio.clear();
}

void write_replay(const Json& result) {
    const auto manifest = result.dump();
    Bytes file(Signature.begin(), Signature.end());
    append_word(file, static_cast<std::uint32_t>(manifest.size()));
    file.insert(file.end(), manifest.begin(), manifest.end());
    file.insert(file.end(), inputs.begin(), inputs.end());
    mods::write_new_file(replay_path, file);
}

void add_ranked_fields(Json& manifest) {
    if (ranked_attempt) {
#if defined(_WIN32)
        constexpr auto platform = "windows";
#elif defined(__linux__) && !defined(__ANDROID__)
        constexpr auto platform = "linux";
#else
        constexpr auto platform = "unsupported";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
        constexpr auto arch = "aarch64";
#else
        constexpr auto arch = "x86_64";
#endif
        manifest.update(
            {{"board", ranked_attempt->board},
             {"engine", ranked_attempt->engine},
             {"ticket", ranked_attempt->ticket},
             {"releaseHash", ranked_attempt->release_hash.empty() ? Json(nullptr) : Json(ranked_attempt->release_hash)},
             {"platform", platform},
             {"arch", arch}});
    }
}

void finish_recording(bool request_quit = true) {
    try {
        finished_record["ticks"] = update;
        write_replay(finished_record);
        if (ghost_clock) {
            try {
                auto metadata = replay_path;
                metadata += ".json";
                updates::durable_write(metadata, Json{{"ghostTicks", ghost_clock}}.dump());
            } catch (const std::exception& failure) {
                std::fprintf(stderr, "[ghost] could not save race comparison: %s\n", failure.what());
            }
        }
        finished_record = nullptr;
        stop(true,
             ghost_clock && current_progress.clock < ghost_clock ? "Ghost beaten. Run recorded." : "Run recorded.",
             request_quit);
    } catch (const std::exception& failure) {
        finished_record = nullptr;
        stop(false, failure.what(), request_quit);
    }
}

} // namespace

std::string board_for(const Course& course, Vehicle vehicle, unsigned racer) {
    if (!course.supports(vehicle) || racer >= kRacers.size())
        return {};
    constexpr std::string_view prefix = "ancient-lake-car";
    return std::string(course.id) + '-' + std::string(kVehicleIds[static_cast<unsigned>(vehicle)]) +
           std::string(kRacers[racer].board.substr(prefix.size()));
}

std::optional<Attempt> parse_attempt(const std::string& board, const std::string& engine, const std::string& ticket,
                                     const Course& course, Vehicle vehicle, unsigned racer) {
    if (board_for(course, vehicle, racer) != board || !valid_attempt(board, engine, ticket))
        return std::nullopt;
    return Attempt{board, engine, ticket, {}, {}, {}};
}

std::optional<Attempt> parse_custom_attempt(const std::string& board, const std::string& engine,
                                            const std::string& ticket, std::string_view release,
                                            std::string_view release_hash, std::string_view track,
                                            std::string_view track_hash, Vehicle vehicle, unsigned racer) {
    constexpr std::string_view old_prefix = "ancient-lake-car";
    const auto vehicle_index = static_cast<unsigned>(vehicle);
    if (!valid_attempt(board, engine, ticket) || !replay_id(release) || !replay_id(track) ||
        !replay_hash(release_hash) || !replay_hash(track_hash) || racer >= kRacers.size() ||
        vehicle_index >= kVehicleIds.size())
        return std::nullopt;
    const auto expected_board = "track-" + std::string(release) + "-" + std::string(kVehicleIds[vehicle_index]) +
                                std::string(kRacers[racer].board.substr(old_prefix.size()));
    if (board != expected_board)
        return std::nullopt;
    return Attempt{board, engine, ticket, std::string(track), std::string(track_hash), std::string(release_hash)};
}

std::optional<Attempt> parse_adventure_attempt(const std::string& board, const std::string& engine,
                                               const std::string& ticket, Adventure goal) {
    if (goal == Adventure::None || board != "adventure-" + std::string(adventure_id(goal)) ||
        !valid_attempt(board, engine, ticket))
        return std::nullopt;
    return Attempt{board, engine, ticket, {}, {}, {}};
}

bool start(Mode requested, const std::filesystem::path& replay, std::string& error, const Attempt* attempt,
           unsigned racer, const Course& course, Vehicle vehicle, Adventure adventure, std::string_view track,
           const std::filesystem::path& track_package, const std::filesystem::path& export_path) {
    try {
        if (requested == Mode::None || active() || !replay.is_absolute())
            throw mods::Error("Time Trial needs an absolute replay file path.");
        if (adventure != Adventure::None &&
            (requested != Mode::Record || !track.empty() || !track_package.empty() ||
             (attempt && (!attempt->track_id.empty() || !attempt->release_hash.empty() ||
                          !parse_adventure_attempt(attempt->board, attempt->engine, attempt->ticket, adventure)))))
            throw mods::Error("Select a category when recording a new Adventure.");
        if (!export_path.empty() && (requested != Mode::Verify || !export_path.is_absolute()))
            throw mods::Error("Use an absolute ghost export path when verifying a replay.");
        adventure_goal = adventure;
        adventure_isolates_effects = adventure != Adventure::None;
        attest_adventure_splits = adventure_isolates_effects;
        use_adventure_route_splits = adventure_isolates_effects;
        use_completed_save_rules = adventure_isolates_effects;
        fresh_adventure_save = adventure_isolates_effects && !uses_completed_adventure_save();
        replay_path = replay;
        const bool reading = requested == Mode::Verify || requested == Mode::Watch || requested == Mode::PrepareGhost;
        if (reading) {
            if (!track.empty())
                throw mods::Error("Replay playback uses the recorded track.");
            const bool capture = requested == Mode::PrepareGhost || !export_path.empty();
            read_replay(track_package, capture);
            if (capture) {
                if (adventure_goal != Adventure::None)
                    throw mods::Error("Only Time Trial replays can be raced as ghosts.");
                ghost_poses.reserve(current_progress.finish_update);
                ghost_export = export_path;
            }
        } else {
            const auto* requested_course = course_for(course.id);
            if (!requested_course || (track.empty() && !requested_course->supports(vehicle)) ||
                racer >= kRacers.size() || static_cast<unsigned>(vehicle) >= kVehicleIds.size() ||
                (attempt && adventure == Adventure::None &&
                 ((track.empty() &&
                   (!attempt->track_id.empty() || attempt->board != board_for(*requested_course, vehicle, racer))) ||
                  (!track.empty() && (attempt->track_id != track || !replay_hash(attempt->track_hash) ||
                                      !replay_hash(attempt->release_hash))))))
                throw mods::Error("The selected racer does not match the Time Trial attempt.");
            if (std::filesystem::exists(replay))
                throw mods::Error("Time Trial recording needs a new replay file.");
            racer_choice = racer;
            selected_course = requested_course;
            selected_vehicle = vehicle;
            if (!track.empty())
                bind_custom_track(std::string(track), track_package,
                                  attempt ? attempt->track_hash : std::string_view{});
            else if (!track_package.empty())
                throw mods::Error("Choose the track ID when recording a custom Time Trial.");
        }
        {
            std::scoped_lock lock(progress_mutex);
            ranked_attempt = attempt ? std::optional(*attempt) : std::nullopt;
            current_progress.ranked = ranked_attempt.has_value();
            current_progress.replay = reading;
            current_progress.adventure = adventure_goal;
            current_progress.lap_count = selected_laps;
            if (current_progress.replay && adventure_goal == Adventure::None) {
                const unsigned count = static_cast<unsigned>(inputs.size() / 4);
                const unsigned race_updates = expected.at("clockTicks").get<unsigned>() / LogicRate;
                const unsigned finish = current_progress.finish_update;
                const unsigned countdown = finish - std::min(finish, race_updates);
                current_progress.lap_updates[0] = std::max(1U, countdown);
                for (unsigned lap = 1; lap < selected_laps; ++lap)
                    current_progress.lap_updates[lap] =
                        std::min(count, countdown + expected.at("splits").at(lap - 1).get<unsigned>() / LogicRate);
            }
            current_progress.total_updates = static_cast<unsigned>(inputs.size() / 4);
            current_progress.racer = racer_choice;
            visible_progress = current_progress;
            result = {};
        }
#if defined(__linux__) && !defined(__ANDROID__)
        if (requested == Mode::Watch) {
            if (const char* input = std::getenv("DKR_REPLAY_CAPTURE_SOCKET")) {
                const std::string_view value(input);
                int descriptor = -1;
                const auto parsed = std::from_chars(value.begin(), value.end(), descriptor);
                struct stat status {};
                if (parsed.ec != std::errc{} || parsed.ptr != value.end() || descriptor < 3 ||
                    fstat(descriptor, &status) != 0 || !S_ISSOCK(status.st_mode))
                    throw mods::Error("Replay video needs a private capture socket.");
                capture_socket.store(descriptor);
            }
        }
#endif
        if (requested == Mode::Watch && !exporting_video()) {
            transport.speed = speedrunning::playback_speeds[options.get(Option::ReplaySpeed)];
            transport.pause_on_open = options.get(Option::StartPaused) != 0;
        }
        mode.store(requested);
        return true;
    } catch (const std::exception& failure) {
        if (!active())
            retire();
        error = failure.what();
        return false;
    }
}

namespace {
bool start_prepared_ghost(const GhostPreparation& load, std::vector<GhostPose> poses, unsigned clock,
                          std::string& error) {
    const auto& course = *selected_course;
    const auto vehicle = selected_vehicle;
    const auto driver = racer_choice;
    const auto track = custom_track_id;
    const auto hash = custom_track_hash;
    const auto recorded_splits = expected.at("splits").get<std::vector<unsigned>>();
    const bool return_to_launcher = launcher_return_enabled;
    retire();
    const auto* attempt =
        load.attempt && (track.empty() || (load.attempt->track_id == track && load.attempt->track_hash == hash))
            ? &*load.attempt
            : nullptr;
    if (!start(load.practice_mode, load.recording, error, attempt, load.player, course, vehicle, Adventure::None, track,
               load.track_package))
        return false;
    if (custom_track_hash != hash) {
        retire();
        error = "The installed track changed while the ghost was being prepared.";
        return false;
    }
    ghost_poses = std::move(poses);
    launcher_return_enabled = return_to_launcher;
    ghost_racer = driver;
    ghost_clock = clock;
    std::copy_n(recorded_splits.begin(), std::min(recorded_splits.size(), ghost_splits.size()), ghost_splits.begin());
    ghost_credited_name.store(load.credited_name);
    ghost_race_active = true;
    {
        std::scoped_lock lock(progress_mutex);
        current_progress.ghost = true;
    }
    std::fprintf(stderr, "[ghost] ready updates=%zu finish=%u ghost=%s player=%s\n", ghost_poses.size(), clock,
                 kRacers[driver].name.data(), kRacers[load.player].name.data());
    return true;
}
} // namespace

bool start_ghost(const std::filesystem::path& source, const std::filesystem::path& recording, std::string& error,
                 unsigned racer, Mode practice_mode, const std::filesystem::path& track_package, const Attempt* attempt,
                 std::string_view credited_name) {
    if ((practice_mode != Mode::Record && practice_mode != Mode::SelfTest) || racer >= kRacers.size() ||
        !recording.is_absolute()) {
        error = "Select a racer and a new practice replay file.";
        return false;
    }
    if (!start(Mode::PrepareGhost, source, error, nullptr, kDiddy, kCourses.front(), Vehicle::Car, Adventure::None, {},
               track_package))
        return false;
    const auto path = recording.parent_path() / "ghost-cache" / (ghost_replay_digest + ".dkrghost");
    GhostPreparation load{recording,
                          track_package,
                          path,
                          racer,
                          practice_mode,
                          attempt ? std::optional(*attempt) : std::nullopt,
                          credited_name.empty() ? nullptr : std::make_shared<const std::string>(credited_name)};
    auto poses = load_ghost_file(portable_ghost_file(source), current_progress.finish_update,
                                 portable_ghost_prefix(ghost_replay_digest));
    if (!poses.empty()) {
        std::fprintf(stderr, "[ghost] loaded %zu downloaded updates\n", poses.size());
        const auto clock = expected.at("clockTicks").get<unsigned>();
        return start_prepared_ghost(load, std::move(poses), clock, error);
    }
    try {
        if (std::filesystem::exists(path))
            poses = load_ghost_file(path, current_progress.finish_update, ghost_cache_prefix());
    } catch (const std::exception&) {
        // Local preparation remains available if the runtime identity cannot be read.
    }
    if (!poses.empty()) {
        std::fprintf(stderr, "[ghost] loaded %zu cached updates\n", poses.size());
        const auto clock = expected.at("clockTicks").get<unsigned>();
        return start_prepared_ghost(load, std::move(poses), clock, error);
    }
    ghost_preparation = std::move(load);
    return true;
}

bool preparing_ghost() {
    return mode.load() == Mode::PrepareGhost;
}

bool ghost_nameplate_active() {
    return ghost_race_active.load(std::memory_order_relaxed) && options.get(Option::GhostOpacity) &&
           (options.get(Option::GhostStyle) || (options.get(Option::GhostNames) && bool(ghost_credited_name.load())));
}

std::optional<GhostNameplate> ghost_nameplate(const std::uint8_t* snapshot) {
    if (!ghost_race_active.load(std::memory_order_relaxed))
        return std::nullopt;
    auto name = options.get(Option::GhostNames) ? ghost_credited_name.load() : nullptr;
    const bool outline = options.get(Option::GhostStyle);
    const auto base_opacity = options.get(Option::GhostOpacity);
    if ((!name && !outline) || !base_opacity)
        return std::nullopt;
    const auto revision = active_replay_rom();
    if (!supported_replay_rom(revision))
        return std::nullopt;
    const auto read = [snapshot](std::uint32_t address) {
        std::uint32_t word;
        std::memcpy(&word, snapshot + (address & 0x007FFFFFU), sizeof(word));
        return word;
    };
    const auto& slots = symbols::for_revision(revision);
    const auto object = read(slots.player_ghost);
    constexpr unsigned invisible = 1U << 14; // OBJ_FLAGS_INVISIBLE
    if (!in_rdram(object, 0x68) || (read(object + 4) & invisible))
        return std::nullopt;
    const auto racer = read(object + 0x64);
    if (!in_rdram(racer, 0x1F8))
        return std::nullopt;
    const auto opacity = snapshot[((racer + 0x1F7) & 0x007FFFFFU) ^ 3U];
    if (!opacity)
        return std::nullopt;
    const auto framebuffer = read(slots.framebuffer);
    if (!in_rdram(framebuffer, 4))
        return std::nullopt;
    return GhostNameplate{std::move(name),
                          {std::bit_cast<float>(read(object + 0x0C)), std::bit_cast<float>(read(object + 0x10)),
                           std::bit_cast<float>(read(object + 0x14))},
                          std::bit_cast<float>(read(object + 8)),
                          std::min(opacity * (outline ? 4.F : 1.F) / (2.55F * base_opacity), 1.F),
                          framebuffer & 0x00FFFFFFU,
                          outline};
}

// Called between guest sessions. Failed verification never supplies a ghost to a race.
bool advance_ghost() {
    if (!preparing_ghost() || status() != 0 || !ghost_preparation || ghost_poses.size() != update)
        return false;
    auto load = std::move(*ghost_preparation);
    save_ghost_cache(load.cache_file, ghost_poses);
    std::string error;
    if (start_prepared_ghost(load, std::move(ghost_poses), current_progress.clock, error))
        return true;

    std::scoped_lock lock(progress_mutex);
    result = {{}, std::move(error)};
    exit_status.store(6);
    return false;
}

bool active() {
    return mode.load() != Mode::None;
}

bool recording() {
    return mode.load() == Mode::Record;
}

bool replaying() {
    return playback();
}

void finish_interrupted_recording() {
    if (!finished_record.is_null())
        finish_recording(false);
}

bool watching() {
    return mode.load() == Mode::Watch && (adventure_goal == Adventure::None || exporting_video());
}

std::filesystem::path watched_replay() {
    std::scoped_lock lock(progress_mutex);
    return watching() ? replay_path : std::filesystem::path{};
}

bool exporting_video() {
    return capture_socket.load() >= 0;
}

bool exporting_full_game_video() {
    return exporting_video() && adventure_goal != Adventure::None;
}

#if defined(__linux__) && !defined(__ANDROID__)
namespace {
bool send_video_packet(unsigned code, std::span<const std::int16_t> samples = {}) {
    const int socket = capture_socket.load();
    if (socket < 0)
        return false;
    const auto send_all = [socket](const void* bytes, std::size_t size) {
        const auto* input = static_cast<const char*>(bytes);
        while (size) {
            pollfd writable{socket, POLLOUT, 0};
            if (poll(&writable, 1, 15000) != 1 || !(writable.revents & POLLOUT))
                return false;
            const auto count = send(socket, input, size, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (count <= 0)
                return false;
            input += count;
            size -= static_cast<std::size_t>(count);
        }
        return true;
    };
    const std::array<std::uint8_t, 4> prefix{std::uint8_t(code), std::uint8_t(code >> 8), std::uint8_t(code >> 16),
                                             std::uint8_t(code >> 24)};
    pollfd readable{socket, POLLIN, 0};
    char ack = 0;
    return (code != 5 || send_all("DKR3", 4)) && send_all(prefix.data(), prefix.size()) &&
           (samples.empty() || send_all(samples.data(), samples.size_bytes())) && poll(&readable, 1, 15000) == 1 &&
           recv(socket, &ack, 1, MSG_DONTWAIT) == 1 && ack == 'K';
}
} // namespace
#endif

bool export_video_picture(unsigned frame, unsigned index) {
#if defined(__linux__) && !defined(__ANDROID__)
    // Slots 1 and 2 identify a completed renderer present; slot 0 carries the tick's PCM.
    return index < 2 && send_video_packet(frame * 4 + index + 1);
#else
    (void)frame;
    (void)index;
    return false;
#endif
}

bool export_video_audio(unsigned frame, std::span<const std::int16_t> audio) {
#if defined(__linux__) && !defined(__ANDROID__)
    constexpr unsigned samples_per_update = 48000 / 30;
    const auto source_frames = audio.size() / 2;
    std::array<std::int16_t, samples_per_update * 2> pcm{};
    if (source_frames) {
        for (unsigned i = 0; i < samples_per_update; ++i) {
            const auto position = i * source_frames;
            const auto a = position / samples_per_update;
            const auto b = std::min(a + 1, source_frames - 1);
            const float fraction = static_cast<float>(position % samples_per_update) / samples_per_update;
            for (unsigned channel = 0; channel < 2; ++channel)
                pcm[2 * i + channel] =
                    static_cast<std::int16_t>(std::lerp(audio[2 * a + channel], audio[2 * b + channel], fraction));
        }
    }
    if (!send_video_packet(frame * 4, pcm))
        return false;
    if (frame == progress().total_updates)
        ultramodern::quit();
    return true;
#else
    (void)frame;
    (void)audio;
    return false;
#endif
}

bool theatre_holding_frame() {
    if (!watching())
        return false;
    const auto state = theatre();
    return state.paused || state.scrubbing || state.seeking || state.finished;
}

Theatre theatre() {
    std::scoped_lock lock(progress_mutex);
    return transport;
}

void theatre_play_pause() {
    std::scoped_lock lock(progress_mutex);
    if (!watching())
        return;
    if (transport.finished) {
        transport.paused = false;
        set_seek_target(1);
    } else {
        transport.paused = !transport.paused;
        replay_audio_reset = true;
        present_audio.clear();
        next_present = {};
    }
}

void theatre_complete_intro() {
    std::scoped_lock lock(progress_mutex);
    // Submit the first few pictures so RT64 can prime its present queue.
    constexpr unsigned warmup_frames = 8;
    const auto ready = std::min(warmup_frames, visible_progress.total_updates);
    if (transport.pause_on_open && ready && visible_progress.updates >= ready && !transport.buffering) {
        transport.pause_on_open = false;
        transport.paused = true;
        set_seek_target(1);
    }
}

void theatre_seek(unsigned target) {
    std::scoped_lock lock(progress_mutex);
    if (watching()) {
        transport.scrubbing = false;
        set_seek_target(target);
    }
}

void theatre_scrub(unsigned target) {
    std::scoped_lock lock(progress_mutex);
    if (watching()) {
        transport.scrubbing = true;
        set_seek_target(target);
    }
}

void theatre_cancel_seek() {
    std::scoped_lock lock(progress_mutex);
    if (watching() && transport.seeking) {
        transport.paused = true;
        transport.scrubbing = false;
        set_seek_target(visible_progress.updates);
    }
}

void theatre_exit() {
    exit_run();
}

void theatre_step(int delta) {
    std::scoped_lock lock(progress_mutex);
    if (!watching() || !visible_progress.updates)
        return;
    const auto from = transport.seeking ? transport.seek_to : visible_progress.updates;
    transport.paused = true;
    set_seek_target(static_cast<unsigned>(std::max(1, int(from) + delta)));
}

void theatre_speed(unsigned percent) {
    std::scoped_lock lock(progress_mutex);
    if (watching() &&
        std::ranges::find(speedrunning::playback_speeds, percent) != speedrunning::playback_speeds.end()) {
        transport.speed = percent;
        replay_audio_reset = true;
        present_audio.clear();
        next_present = {};
        std::fprintf(stderr, "[replay] playback speed=%u%% frame=%u\n", percent, visible_progress.updates);
    }
}

void theatre_graphics_submitted(std::uint8_t* rdram, std::uint32_t display_list,
                                std::shared_ptr<const presentation::ReplayPresentation> presentation) {
    if (!watching() || !update)
        return;
    const auto address = symbols::for_revision(active_replay_rom()).framebuffer;
    const auto framebuffer = std::uint32_t(MEM_W(0, guest(address)));
    if (!in_rdram(framebuffer, 4))
        return;
    std::scoped_lock lock(progress_mutex);
    graphics_stamps.emplace_back(display_list & 0x007fffffU,
                                 FrameStamp{current_progress, framebuffer, std::move(presentation)});
}

std::optional<FrameStamp> theatre_graphics_stamp(std::uint32_t display_list) {
    std::scoped_lock lock(progress_mutex);
    if (graphics_stamps.empty() || graphics_stamps.front().first != (display_list & 0x007fffffU))
        return std::nullopt;
    const auto stamp = graphics_stamps.front().second;
    graphics_stamps.pop_front();
    return stamp;
}

void theatre_buffered(unsigned frame) {
    std::scoped_lock lock(progress_mutex);
    if (frame <= transport.buffered)
        return;
    transport.buffered = frame;
    if (frame == visible_progress.total_updates)
        std::fprintf(stderr, "[replay] indexed all %u frames; %zu audio samples\n", frame, captured_audio_samples);
}

std::optional<unsigned> theatre_next_frame(std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock(progress_mutex);
    if (!transport.buffered)
        return std::nullopt;
    const unsigned shown = visible_progress.updates;
    auto picture_ready = transport.buffered;
    if (exporting_full_game_video()) {
        // menus and scene loads can hold one picture for several updates.
        // Wait for submitted graphics, but let the clock and inputs advance
        // over gaps without requiring the guest to produce a new picture.
        constexpr unsigned graphics_slack = 3;
        auto hold_through = exit_status.load() == 0
                                ? current_progress.total_updates
                                : current_progress.updates - std::min(current_progress.updates, graphics_slack);
        if (!graphics_stamps.empty() && graphics_stamps.front().second.progress.updates)
            hold_through = std::min(hold_through, graphics_stamps.front().second.progress.updates - 1);
        picture_ready = std::max(picture_ready, hold_through);
    }
    const auto ready = std::min(picture_ready, audio_ready);
    if (exporting_video()) {
        // A movie uses the authored update number as PTS. Wait for each
        // picture and its mix without advancing to catch up to wall time.
        if (!transport.finished && shown < ready)
            return shown + 1;
        return std::nullopt;
    }
    if (!shown) {
        if (!next_present.time_since_epoch().count())
            next_present = now + std::chrono::seconds(3);
        const unsigned starting_frames = std::min(visible_progress.total_updates, 90U);
        if (now < next_present || ready < starting_frames)
            return std::nullopt;
        transport.buffering = false;
    }
    // A request can arrive before the producer has indexed it. Show the
    // current picture until the exact target is available.
    if (transport.seeking && transport.seek_to <= transport.buffered) {
        next_present = now + std::chrono::microseconds(3333333U / transport.speed);
        return transport.seek_to;
    }
    if (!shown) {
        next_present = now + std::chrono::microseconds(3333333U / transport.speed);
        return 1;
    }
    if (transport.seeking || transport.scrubbing || transport.paused || transport.finished || now < next_present)
        return std::nullopt;

    if (shown >= ready) {
        transport.buffering = shown < visible_progress.total_updates;
        next_present = {};
        return std::nullopt;
    }
    if (transport.buffering && ready < std::min(visible_progress.total_updates, shown + 15U))
        return std::nullopt;
    transport.buffering = false;
    const auto period = std::chrono::microseconds(3333333U / transport.speed);
    const auto overdue = next_present.time_since_epoch().count() ? (now - next_present) / period : 0;
    const auto elapsed = 1U + static_cast<unsigned>(std::clamp<std::int64_t>(overdue, 0, 4));
    next_present =
        now + period -
        (next_present.time_since_epoch().count() ? (now - next_present) % period : std::chrono::microseconds(0));
    return std::min(ready, shown + elapsed);
}

bool theatre_presented(Progress source, unsigned frame) {
    std::scoped_lock lock(progress_mutex);
    const auto prior = visible_progress.updates;
    const bool seek = transport.seeking && frame == transport.seek_to;
    if (transport.seeking && !seek && prior)
        return false; // The user chose a newer target while a frame decoded.
    if (exporting_full_game_video()) {
        // The game may reuse one picture across several updates. Keep the
        // input, clock and capture tag on the exact update being exported.
        if (movie_progress.empty() || movie_progress.front().updates != frame) {
            archive_error = true;
            return false;
        }
        source = movie_progress.front();
        movie_progress.pop_front();
    } else if (frame <= timeline_progress.size())
        source = timeline_progress[frame - 1];
    visible_progress = source;
    if (seek) {
        transport.seeking = false;
        std::fprintf(stderr, "[replay] seek completed at frame=%u racer=%s\n", frame,
                     kRacers[source.racer].name.data());
    } else if ((prior || exporting_video()) && !transport.pause_on_open && !transport.paused && !transport.scrubbing &&
               frame > prior) {
        const auto first = std::upper_bound(audio_blocks.begin(), audio_blocks.end(), prior,
                                            [](unsigned n, const AudioBlock& block) { return n < block.frame; });
        const auto last = std::upper_bound(first, audio_blocks.end(), frame,
                                           [](unsigned n, const AudioBlock& block) { return n < block.frame; });
        for (auto it = first; it != last; ++it)
            present_audio.insert(present_audio.end(), it->samples.begin(), it->samples.end());
        if (exporting_full_game_video())
            audio_blocks.erase(audio_blocks.begin(), last);
    }
    if (frame == source.total_updates) {
        transport.finished = true;
        transport.paused = true;
    }
    return true;
}

void theatre_archive_error() {
    std::scoped_lock lock(progress_mutex);
    archive_error = true;
}

bool theatre_capture_audio(std::span<const std::int16_t> samples) {
    if (!watching())
        return false;
    std::scoped_lock lock(progress_mutex);
    // __amHandleFrameMsg submits the previous mix to AI before mixing the
    // current update. Discard the boot mix and stamp the rest on that clock.
    if (mixer_update > 1 && samples.size() <= 8192) {
        audio_blocks.push_back({mixer_update - 1, {samples.begin(), samples.end()}});
        captured_audio_samples += samples.size();
    }
    return true;
}

std::vector<std::int16_t> theatre_take_audio() {
    std::scoped_lock lock(progress_mutex);
    return std::exchange(present_audio, {});
}

bool theatre_take_audio_reset() {
    std::scoped_lock lock(progress_mutex);
    return std::exchange(replay_audio_reset, false);
}

int status() {
    return exit_status.load();
}

Progress progress() {
    std::scoped_lock lock(progress_mutex);
    return watching() ? visible_progress : current_progress;
}

std::vector<AdventureSplit> adventure_splits() {
    std::scoped_lock lock(progress_mutex);
    if (watching()) {
        const auto end =
            std::ranges::upper_bound(reached_splits.entries, visible_progress.clock, {}, &AdventureSplit::clock);
        return {reached_splits.entries.begin(), end};
    }
    return reached_splits.entries;
}

std::vector<unsigned> adventure_timeline_markers() {
    std::scoped_lock lock(progress_mutex);
    const auto stages = expected.find("splits");
    if (!watching() || adventure_goal == Adventure::None || stages == expected.end() || !stages->is_array())
        return {};
    const auto finish = current_progress.finish_update;
    const auto elapsed = expected.value("clockTicks", 0U) / LogicRate;
    const auto lead_in = finish - std::min(finish, elapsed);
    std::vector<unsigned> frames;
    for (const auto& stage : *stages) {
        if (stage.is_array() && stage.size() == 2 && stage[1].is_number_integer()) {
            const auto ticks = stage[1].get<std::int64_t>();
            if (ticks > 0 && ticks / LogicRate <= elapsed)
                frames.push_back(lead_in + unsigned(ticks) / LogicRate);
        }
    }
    return frames;
}

Result last_result() {
    std::scoped_lock lock(progress_mutex);
    return result;
}

void set_finish_personal_best(unsigned ticks) {
    prior_finish_personal_best = ticks;
}

void enable_launcher_return() {
    launcher_return_enabled = true;
}

float launcher_return_opacity() {
    const auto started = launcher_return_started.load();
    return started ? std::clamp(float(return_clock() - started + 1) / 240.F, 0.F, 1.F) : 0.F;
}

// Only after the guest and renderer have joined, or a launch was refused.
void retire() {
    std::scoped_lock lock(progress_mutex);
    if (exit_status.load() == 1 && active())
        result = {{}, "Run ended before the finish.", 0, 0, false, false, playback()};
    mode.store(Mode::None);
    launcher_return_enabled = false;
    launcher_return_started = 0;
    capture_socket.store(-1);
    exit_status.store(1);
    current_progress = {};
    podium_exit_at = {};
    prior_finish_personal_best = 0;
    transport = {};
    visible_progress = {};
    next_present = {};
    flushed_last_graphics = archive_error = replay_audio_reset = false;
    graphics_stamps.clear();
    audio_blocks.clear();
    timeline_progress.clear();
    movie_progress.clear();
    captured_audio_samples = 0;
    mixer_update = audio_ready = 0;
    present_audio.clear();
    ranked_attempt.reset();
    ghost_preparation.reset();
    ghost_object_state.reset();
    ghost_credited_name.store(nullptr);
    ghost_race_active = false;
    ghost_splits = {};
    restart_confirmation = false;
    ghost_poses.clear();
    ghost_replay_digest.clear();
    ghost_export.clear();
    ghost_racer = kDiddy;
    ghost_clock = 0;
    launched = sampled = false;
    update = 0;
    previous = {};
    autopilot = {};
    inputs.clear();
    observations.clear();
    checkpoints.clear();
    expected = nullptr;
    finished_record = nullptr;
    racer_choice = kDiddy;
    adventure_goal = Adventure::None;
    legacy_adventure = false;
    adventure_isolates_effects = false;
    fresh_adventure_save = false;
    adventure_intro_seen = false;
    attest_adventure_splits = false;
    use_adventure_route_splits = false;
    use_completed_save_rules = false;
    reached_splits = {};
    previous_adventure_progress = {};
    previous_taj_flags = 0;
    if (!custom_track_id.empty())
        custom_tracks::retire_replay_track();
    custom_track_id.clear();
    custom_track_hash.clear();
    custom_track_name.clear();
    selected_level = 0;
    selected_laps = kRetailLapCount;
    adventure_start = 0;
    adventure_slot = -1;
    race_finish = 0;
    replay_path.clear();
}

bool uses_uninitialised_adventure_save() {
    return fresh_adventure_save;
}

bool uses_completed_adventure_save() {
    return use_completed_save_rules && completed_save_route(adventure_goal);
}

extern "C" void dkr_time_trial_launch(std::uint8_t* rdram, recomp_context* context) {
    if (!active() || launched || adventure_goal != Adventure::None)
        return;
    const auto* payload = active_payload();
    const unsigned revision = active_replay_rom();
    if (!payload || !supported_replay_rom(revision)) {
        stop(false, "Select a US v1.0 or US v1.1 ROM.");
        return;
    }
    if (!replay_rom_matches(revision))
        return;
    selected_level = selected_course->level;
    if (!custom_track_id.empty()) {
        const auto level = custom_tracks::resolved_level_id(custom_track_id);
        if (level < 0 || level > custom_tracks::kMaxLevelId) {
            stop(false, "The replay's track did not load in the isolated session.");
            return;
        }
        selected_level = static_cast<unsigned>(level);
    }
    const auto& a = addresses::current();
    auto call = [&](RecompiledEntrypoint entry, gpr argument = 0) {
        GuestCallContext arguments(*context);
        arguments->r4 = argument;
        entry(rdram, arguments.get());
        return arguments->r2;
    };
    call(payload->titlescreen_controller_assign);
    call(payload->input_assign_players);
    for (int port = 0; port < 4; ++port) {
        MEM_B(port, guest(a.CharacterSelectStatus)) = port == 0 ? 2 : 0;
        MEM_B(port, guest(a.ActivePlayersArray)) = port == 0 ? 1 : 0;
    }
    MEM_W(0, guest(a.NumberOfReadyPlayers)) = 1;
    MEM_W(0, guest(a.NumberOfActivePlayers)) = 1;
    MEM_W(0, guest(a.TracksMode)) = 1;
    call(payload->set_time_trial_enabled, 1);
    const unsigned table =
        unsigned(call(payload->unlock_drumstick) != 0) | (unsigned(call(payload->unlock_tt) != 0) << 1);
    const auto& selection = capture_symbols::for_revision(revision == 80);
    const auto roster = table | (racer_choice == 6 ? 1U : racer_choice == 8 ? 2U : 0U);
    MEM_W(0, guest(selection.select_data)) = selection.tables[roster];
    MEM_B(0, guest(a.CharacterIdSlots)) = racer_choice;
    call(payload->charselect_assign_ai, 1);
    call(payload->init_racer_headers);
    MEM_W(0, guest(selection.game_players)) = 0;
    MEM_W(0, guest(a.CurrentRngSeed)) = BootSeed;
    context->r2 = (1U << 9) | selected_level;
    launched = true;
    if (!custom_track_id.empty())
        std::fprintf(stderr, "[time-trial] track=%s hash=%s vehicle=%s\n", custom_track_id.c_str(),
                     custom_track_hash.c_str(), kVehicleIds[static_cast<unsigned>(selected_vehicle)].data());
    else if (!legacy_course())
        std::fprintf(stderr, "[time-trial] course=%s vehicle=%s\n", selected_course->id.data(),
                     kVehicleIds[static_cast<unsigned>(selected_vehicle)].data());
    std::fprintf(stderr, "[time-trial] rom=%u\n", revision);
    std::fprintf(stderr, "[time-trial] %s / %s / %s / %s\n",
                 custom_track_id.empty() ? selected_course->name.data() : custom_track_name.c_str(),
                 kRacers[racer_choice].name.data(), kVehicleNames[static_cast<unsigned>(selected_vehicle)].data(),
                 revision == 80 ? "US v1.1" : "US v1.0");
}

extern "C" void dkr_time_trial_load_course(std::uint8_t* rdram, recomp_context* context) {
    if (adventure_goal != Adventure::None || !launched || legacy_course() || context->r4 != selected_level)
        return;
    const auto vehicle = static_cast<unsigned>(selected_vehicle);
    GuestCallContext arguments(*context);
    arguments->r4 = vehicle;
    active_payload()->set_level_default_vehicle(rdram, arguments.get());
    context->r7 = vehicle;
}

// These scopes extend the existing presentation RNG only for new Adventure
// recordings and their playback. Earlier recordings retain their original calls.
extern "C" int dkr_practice_ghost_mount(std::uint8_t* rdram, unsigned revision) {
    if (!supported_replay_rom(revision) || playback() || !active() || ghost_poses.empty())
        return 0;
    const auto& slots = symbols::for_revision(revision);
    const auto seed = guest(addresses::current().CurrentRngSeed);
    ghost_object_state = GhostObjectState{
        std::uint32_t(MEM_W(0, seed)),         MEM_HU(0, guest(slots.ghost_map)),
        MEM_HU(0, guest(slots.ghost_vehicle)), MEM_HU(0, guest(slots.ghost_character)),
        MEM_HU(0, guest(slots.ghost_time)),
    };
    MEM_H(0, guest(slots.ghost_map)) = selected_level;
    MEM_H(0, guest(slots.ghost_vehicle)) = static_cast<unsigned>(selected_vehicle);
    MEM_H(0, guest(slots.ghost_character)) = ghost_racer;
    MEM_H(0, guest(slots.ghost_time)) = std::min(ghost_clock, 32767U);
    return 1;
}

extern "C" void dkr_practice_ghost_spawned(std::uint8_t* rdram, unsigned revision) {
    if (!supported_replay_rom(revision) || !ghost_object_state)
        return;
    const auto& slots = symbols::for_revision(revision);
    const auto state = *ghost_object_state;
    ghost_object_state.reset();
    // The stock spawn is useful for its model and disabled collision. Keep
    // the controller-pak ghost state and gameplay RNG from this practice run.
    MEM_H(0, guest(slots.ghost_map)) = state.map;
    MEM_H(0, guest(slots.ghost_vehicle)) = state.vehicle;
    MEM_H(0, guest(slots.ghost_character)) = state.character;
    MEM_H(0, guest(slots.ghost_time)) = state.time;
    MEM_W(0, guest(addresses::current().CurrentRngSeed)) = state.seed;
}

extern "C" int dkr_practice_ghost_pose(std::uint8_t* rdram, recomp_context* context, unsigned revision) {
    if (!supported_replay_rom(revision) || playback() || !active() || ghost_poses.empty())
        return 0;
    const auto& slots = symbols::for_revision(revision);
    const auto object = std::uint32_t(context->r4);
    if (!in_rdram(object, 0x68))
        return 0;
    const auto address = guest(object);
    if (object != std::uint32_t(MEM_W(0, guest(slots.player_ghost)))) {
        // Keep the staff ghost's native update but show only the chosen opponent.
        constexpr unsigned invisible = 1U << 14; // OBJ_FLAGS_INVISIBLE
        MEM_H(0x06, address) = MEM_HU(0x06, address) | invisible;
        return 0;
    }

    // Both runs use one authored step per update, including the countdown.
    // Only this stock ghost object's transform is updated.
    const auto index = std::min<std::size_t>(update, ghost_poses.size() - 1);
    const auto& pose = ghost_poses[index];
    for (unsigned axis = 0; axis < pose.position.size(); ++axis) {
        MEM_W(0x0C + axis * 4, address) = pose.position[axis];
        MEM_H(axis * 2, address) = pose.rotation[axis];
    }
    const auto segments = MEM_W(0, guest(addresses::current().NumberOfLevelSegments));
    MEM_H(0x2E, address) = pose.segment < segments ? pose.segment : -1;
    const auto racer = std::uint32_t(MEM_W(0x64, address));
    if (in_rdram(racer, 0x1F8)) {
        const auto fade = std::min<std::size_t>(30, update - index);
        float alpha = float(options.get(Option::GhostOpacity)) * 2.55F * float(30 - fade) / 30.F;
        if (options.get(Option::GhostStyle))
            alpha *= .25F;
        if (options.get(Option::GhostProximity)) {
            if (const auto player = player_address(rdram)) {
                float distance2 = 0;
                for (unsigned axis = 0; axis < pose.position.size(); ++axis) {
                    const auto own = std::bit_cast<float>(std::uint32_t(MEM_W(0x0C + axis * 4, guest(player))));
                    const auto delta = own - std::bit_cast<float>(pose.position[axis]);
                    distance2 += delta * delta;
                }
                if (std::isfinite(distance2))
                    alpha *= std::clamp((std::sqrt(distance2) - 28.F) / 100.F, .1F, 1.F);
            }
        }
        MEM_B(0x1F7, guest(racer)) = std::clamp(int(alpha + .5F), 0, 255);
    }
    if (update == 90 || update == 600 || update == ghost_poses.size())
        std::fprintf(stderr, "[ghost] update=%u sample=%zu object=%08x player=%08x\n", update, index + 1, object,
                     player_address(rdram));
    context->r2 = update == index ? 1 : 0;
    return 1;
}

extern "C" int dkr_practice_ghost_marker(int staff) {
    return !ghost_race_active.load(std::memory_order_relaxed) || (!staff && options.get(Option::GhostMarker));
}

extern "C" void dkr_adventure_presentation_begin() {
    if (launched && adventure_isolates_effects)
        netplay::begin_presentation_random_scope();
}

extern "C" void dkr_adventure_presentation_end() {
    if (launched && adventure_isolates_effects)
        netplay::end_presentation_random_scope();
}

extern "C" void dkr_full_game_tracks_menu(std::uint8_t* rdram, unsigned revision) {
    if (!supported_replay_rom(revision) || !active() || !use_adventure_route_splits || use_completed_save_rules ||
        !tracks_route(adventure_goal, false))
        return;
    const auto sample = achievements::capture(rdram, 8U * mods::MiB, revision);
    if (!sample.valid)
        return;
    const std::uint32_t flags = MEM_W(4, guest(sample.settings));
    if (!in_rdram(flags, sample.course_flags.size() * 4))
        return;
    // Make the retail Tracks menu fully available in the isolated session.
    // No Adventure is pre-created, and these flags cannot count as race results.
    for (const auto& course : kCourses)
        MEM_W(course.level * 4, guest(flags)) |= 7;
    MEM_H(12, guest(sample.settings)) |= 0x79e;
    MEM_H(0, guest(symbols::for_revision(revision).future_fun_land)) = 0;
}

extern "C" int dkr_time_trial_frame_begin(std::uint8_t* rdram, unsigned revision) {
    if (podium_exit_at.time_since_epoch().count() && std::chrono::steady_clock::now() >= podium_exit_at) {
        podium_exit_at = {};
        exit_run();
    }
    const auto returning = launcher_return_started.load();
    if (returning && return_clock() - returning >= 360)
        ultramodern::quit();
    if (restart_confirmation.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 1;
    }
    if (active() && adventure_goal != Adventure::None && !launched) {
        if (!supported_replay_rom(revision)) {
            stop(false, "Adventure requires a US v1.0 or US v1.1 ROM.");
            return 1;
        }
        if (!replay_rom_matches(revision))
            return 1;
        MEM_W(0, guest(addresses::current().CurrentRngSeed)) = BootSeed;
        launched = true;
        std::fprintf(stderr, "[time-trial] rom=%u\n", revision);
        std::fprintf(stderr, "[adventure] category=%s\n", adventure_id(adventure_goal).data());
    }
    if (!launched || !supported_replay_rom(revision))
        return 0;
    if (watching() && update != 0) {
        bool failed;
        bool waiting;
        {
            std::scoped_lock lock(progress_mutex);
            failed = archive_error;
            // An export can only advance once the encoder consumes frames.
            // Keep the full-session picture and audio buffers bounded.
            waiting =
                update > mixer_update + 2 || (exporting_full_game_video() && update > visible_progress.updates + 6);
        }
        if (failed) {
            stop(false, "Replay Theatre could not write its temporary frame index.");
            return 1;
        }
        if (exit_status.load() != 1) {
            if (std::exchange(flushed_last_graphics, true)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return 1;
            }
        }
        if (waiting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return 1;
        }
        // Produce ahead of the viewing clock with one retrace wait. The
        // authored simulation still uses the recording's original step.
        if (!exporting_full_game_video())
            MEM_B(0, guest(symbols::for_revision(revision).video_delta)) = 1;
    }
    if (preparing_ghost())
        MEM_B(0, guest(symbols::for_revision(revision).video_delta)) = 1;
    const auto& a = addresses::current();
    if (adventure_goal != Adventure::None || MEM_W(0, guest(a.GameMode)) == 0)
        MEM_W(0, guest(a.LogicUpdateRate)) = LogicRate;
    return 0;
}

extern "C" int dkr_theatre_audio_begin() {
    if (!watching())
        return 1;
    std::scoped_lock lock(progress_mutex);
    if (!current_progress.updates)
        return 1;
    // Permit one extra mixer call at the end to submit the last AI block.
    const auto available = current_progress.updates + (exit_status.load() != 1 ? 1U : 0U);
    if (mixer_update >= available)
        return 0;
    ++mixer_update;
    return 1;
}

extern "C" void dkr_theatre_audio_done() {
    if (!watching())
        return;
    std::scoped_lock lock(progress_mutex);
    if (mixer_update) {
        audio_ready = mixer_update - 1;
        if (audio_ready && audio_ready == current_progress.total_updates)
            std::fprintf(stderr, "[replay][audio] indexed %u updates; %zu stereo frames\n", audio_ready,
                         captured_audio_samples / 2);
    }
}

namespace {
std::atomic<bool> restart_intent{false};
std::uint32_t pause_resume_text = 0;
bool pause_resume_requested = false;

bool recording_pause_available() {
    return mode == Mode::Record && launched && exit_status.load() == 1 && !race_finish;
}
} // namespace

bool request_restart() {
    if (!recording_pause_available())
        return false;
    if (options.get(Option::ConfirmRestart))
        restart_confirmation = true;
    else
        confirm_restart(true);
    return true;
}

bool restart_confirmation_pending() {
    return restart_confirmation.load();
}

void confirm_restart(bool restart) {
    restart_confirmation = false;
    if (restart && recording_pause_available()) {
        restart_intent = true;
        stop(false, "Restarting the attempt.");
    }
}

bool take_restart_request() {
    return restart_intent.exchange(false);
}

extern "C" void dkr_time_trial_pause_options(std::uint8_t* rdram, unsigned revision) {
    if (!supported_replay_rom(revision) || !recording_pause_available() || adventure_goal != Adventure::None)
        return;
    const auto& menu = symbols::for_revision(revision);
    pause_resume_text = 0;
    pause_resume_requested = false;
    const auto count = MEM_W(0, guest(menu.menu_count));
    if (count < 3 || count > 8)
        return;
    pause_resume_text = MEM_W(0, guest(menu.menu_text));
    const auto quit = MEM_W((count - 1) * 4, guest(menu.menu_text));
    MEM_W(0, guest(menu.menu_text)) = MEM_W(4, guest(menu.menu_text));
    MEM_W(4, guest(menu.menu_text)) = quit;
    MEM_W(0, guest(menu.menu_count)) = 2;
}

extern "C" void dkr_time_trial_resume(std::uint8_t* rdram, unsigned revision) {
    if (!supported_replay_rom(revision) || !recording_pause_available() || adventure_goal != Adventure::None)
        return;
    const auto& menu = symbols::for_revision(revision);
    if (MEM_W(0, guest(menu.menu_count)) != 2 || !pause_resume_text || MEM_W(0, guest(menu.menu_suboption)) ||
        MEM_W(0, guest(menu.menu_delay)) || MEM_W(0, guest(menu.menu_ignore_input)))
        return;
    constexpr unsigned start_or_back = 0x5000;
    if (MEM_HU(0, guest(menu.pressed)) & start_or_back) {
        pause_resume_requested = true;
        MEM_H(0, guest(menu.pressed)) &= ~start_or_back;
    }
    if (pause_resume_requested && !(MEM_HU(0, guest(menu.current)) & start_or_back)) {
        // Let the native delay branch close its dialog and restore audio as usual.
        pause_resume_requested = false;
        MEM_H(0, guest(menu.pressed)) &= ~start_or_back;
        MEM_W(0, guest(menu.menu_option)) = 0;
        MEM_W(0, guest(menu.menu_text)) = pause_resume_text;
        MEM_W(0, guest(menu.menu_delay)) = 3;
    }
}

extern "C" void dkr_time_trial_pause_action(recomp_context* context, unsigned revision) {
    if (!supported_replay_rom(revision) || !recording_pause_available())
        return;
    constexpr unsigned restart = 2;
    constexpr unsigned quit_game = 4;
    if (context->r2 == restart) {
        if (request_restart())
            context->r2 = 1;
    } else if (context->r2 == quit_game) {
        context->r2 = 1;
        stop(false, "Run quit before the finish.");
    }
}

extern "C" void dkr_time_trial_input(std::uint8_t* rdram, unsigned revision) {
    if (!launched || sampled || !supported_replay_rom(revision) || exit_status.load() != 1)
        return;
    const auto& a = addresses::current();
    const bool adventure = adventure_goal != Adventure::None;
    if (!adventure && !race_finish && (MEM_W(0, guest(a.GameMode)) != 0 || !player_address(rdram)))
        return;
    const unsigned limit = adventure ? kAdventureMaxUpdates : MaxUpdates;
    if ((!race_finish && update >= limit) || (playback() && update * 4 == inputs.size())) {
        stop(false, "Playback ended before the finish.");
        return;
    }

    const auto& pad = symbols::for_revision(revision);
    if (!adventure && recording_pause_available() &&
        (MEM_BU(0, guest(a.IsPaused)) || (MEM_HU(0, guest(pad.pressed)) & 0x1000U)))
        return; // Solo pause input belongs to the native menu, outside race sampling.
    PackedInput input{};
    if (race_finish) {
        if (!playback())
            append_word(inputs, 0);
    } else if (playback()) {
        input = netplay::input_from_bits(word_at(inputs, update * 4));
    } else {
        if (mode == Mode::SelfTest) {
            const auto drivers = netplay::experimental::capture_autopilot_observations(rdram, 8U * mods::MiB, revision);
            input = netplay::experimental::check_autopilot_steer(drivers[0], update, autopilot);
            // Qualification runs can steer closer to the checkpoints than
            // the regular network self-test. The resulting file still
            // records only ordinary controller inputs and must re-simulate.
            if (const auto* style = std::getenv("DKR_SOLO_QUALIFY_STEER"); style && drivers[0].valid) {
                const auto& car = drivers[0];
                const auto& target = car.nodes[2];
                const float dx = target.x - car.position.x, dz = target.z - car.position.z;
                const auto turn = int(std::lround(std::atan2(dx, dz) * (32768.0 / std::numbers::pi))) - 32768;
                const auto angle = ((turn - int(car.heading) + 32768) & 65535) - 32768;
                if (car.vehicle != 4) {
                    int stick = -angle / (car.vehicle == 2 ? 165 : 215);
                    if (std::string_view(style) == "blend")
                        stick = (stick + input.stick_x) / 2;
                    input.stick_x = std::int8_t(std::clamp(stick, -75, 75));
                    if (car.vehicle == 2) {
                        const auto object = player_address(rdram);
                        const auto pitch = object ? MEM_H(2, guest(object)) : 0;
                        const double dy = target.y - car.position.y;
                        const auto target_pitch = int(std::lround(std::atan2(dy, std::max(180.F, std::hypot(dx, dz))) *
                                                                  (32768.0 / std::numbers::pi)));
                        input.stick_y = std::int8_t(std::clamp((pitch - target_pitch) / 80, -57, 57));
                    } else if (std::abs(angle) > 7600 && std::hypot(dx, dz) < 280.F && update % 8 < 4) {
                        input.buttons |= 0x4000; // Brake before tight bends.
                    }
                }
                if (update % 300 == 0)
                    std::fprintf(stderr, "[time-trial][qualify] frame=%u checkpoint=%d heading-error=%d stick=%d,%d\n",
                                 update, int(car.next_checkpoint), angle, int(input.stick_x), int(input.stick_y));
            }
        } else {
            input = {static_cast<std::uint16_t>(MEM_HU(0, guest(pad.current))),
                     static_cast<std::int8_t>(MEM_B(2, guest(pad.current))),
                     static_cast<std::int8_t>(MEM_B(3, guest(pad.current)))};
        }
        // Solo races sample gameplay only. Adventure also records its native menu input.
        if (!adventure)
            input.buttons &= ~0x1000U;
        append_word(inputs, static_cast<std::uint32_t>(netplay::packed_input_bits(input)));
    }
    if (!adventure)
        input.buttons &= ~0x1000U;
    else
        for (int port = 1; port < 4; ++port) {
            MEM_W(port * 4, guest(pad.current)) = 0;
            MEM_W(port * 4, guest(pad.previous)) = 0;
            MEM_H(port * 2, guest(pad.pressed)) = 0;
            MEM_H(port * 2, guest(pad.released)) = 0;
        }
    const auto changed = previous.buttons ^ input.buttons;
    const auto button_mask = MEM_HU(0, guest(pad.button_mask));
    MEM_H(0, guest(pad.previous)) = previous.buttons;
    MEM_B(2, guest(pad.previous)) = previous.stick_x;
    MEM_B(3, guest(pad.previous)) = previous.stick_y;
    MEM_H(0, guest(pad.current)) = input.buttons;
    MEM_B(2, guest(pad.current)) = input.stick_x;
    MEM_B(3, guest(pad.current)) = input.stick_y;
    MEM_H(0, guest(pad.pressed)) = changed & input.buttons & button_mask;
    MEM_H(0, guest(pad.released)) = changed & previous.buttons & button_mask;
    previous = input;
    sampled = true;
}

namespace {
void adventure_frame(std::uint8_t* rdram, unsigned revision) {
    const auto& settings = addresses::current();
    const unsigned earned =
        use_completed_save_rules
            ? earned_character_flags(MEM_W(4, guest(symbols::for_revision(revision).eeprom_settings)))
            : 0U;
    const unsigned enabled = MEM_W(0, guest(settings.ActiveMagicCodes)) | MEM_W(0, guest(settings.UnlockedMagicCodes));
    if (enabled & ~earned) {
        stop(false, "Adventure verification requires the original rules with Magic Codes disabled.");
        return;
    }
    const auto sample = achievements::capture(rdram, 8U * mods::MiB, revision);
    const auto object = sample.mode == 0 ? player_address(rdram) : 0;
    constexpr int new_game_cinematic = 23;
    if (fresh_adventure_save && !adventure_intro_seen && sample.mode == 1 && sample.menu == new_game_cinematic) {
        adventure_intro_seen = true;
        std::fprintf(stderr, "[adventure] new-save cinematic update=%u\n", update);
    }
    const AdventureProgress progress{
        unsigned(sample.balloons), unsigned(sample.keys),      unsigned(sample.bosses),
        unsigned(sample.trophies), unsigned(sample.tt_amulet), unsigned(sample.wizpig_amulet),
    };
    constexpr int hub_world_race_type = 5;
    constexpr int boss_race_type = 8;
    const bool tracks = use_adventure_route_splits && tracks_route(adventure_goal, use_completed_save_rules);
    const bool minigames = use_completed_save_rules && adventure_goal == Adventure::AllMinigames;
    const bool bosses = use_completed_save_rules && adventure_goal == Adventure::AllBosses;
    const bool minigame_race = sample.race_type >= 64 && sample.race_type <= 66;
    if (!adventure_start && tracks && sample.valid && object && !sample.tracks &&
        sample.race_type == hub_world_race_type) {
        stop(false, "Choose Tracks on the game's main menu for this category.");
        return;
    }
    if (!adventure_start && tracks && sample.valid && object && sample.tracks &&
        (sample.race_type == 0 || minigame_race)) {
        if (sample.mirrored || sample.trial || minigames != minigame_race ||
            (sample.trophy_world != 0) != trophy_route(adventure_goal)) {
            stop(false, "Select the original race mode for this Tracks category.");
            return;
        }
        adventure_start = update;
        std::fprintf(stderr, "[adventure] started update=%u\n", update);
    } else if (!adventure_start && !tracks && sample.valid && object && sample.race_type == hub_world_race_type &&
               !sample.tracks && !sample.mirrored && sample.slot >= 0 && sample.slot < 3 &&
               MEM_BU(0x48, guest(sample.settings)) == 0) {
        if (fresh_adventure_save && !adventure_intro_seen) {
            stop(false, "The new Adventure intro did not play.");
            return;
        }
        const bool empty_save = !progress.balloons && !progress.keys && !progress.bosses && !progress.trophies &&
                                !progress.tt_amulet && !progress.wizpig_amulet;
        const bool completed_save = sample.slot == 0 && adventure_complete(Adventure::HundredPercent, progress) &&
                                    adventure_complete(Adventure::AllTrophyRaces, progress, true);
        if (bosses ? !completed_save : !empty_save) {
            stop(false, bosses ? "Select the completed Adventure slot for All Bosses."
                               : "Adventure must begin from a fresh save.");
            return;
        }
        // The title/menu presentation may consume random values before the run
        // starts. Give a fresh Adventure the same gameplay seed on every host.
        MEM_W(0, guest(settings.CurrentRngSeed)) = BootSeed;
        adventure_start = update;
        adventure_slot = sample.slot;
        std::fprintf(stderr, "[adventure] started update=%u\n", update);
    }
    const bool wrong_mode = tracks ? object && (!sample.tracks || sample.mirrored || sample.trial)
                                   : sample.slot != adventure_slot || sample.tracks || sample.mirrored || sample.trial;
    if (adventure_start && sample.valid &&
        (wrong_mode ||
         std::any_of(sample.racers.begin(), sample.racers.end(), [](const auto& racer) { return racer.player > 0; }))) {
        stop(false, "The simulation left the original single-player rules for this category.");
        return;
    }
    const unsigned clock = adventure_start ? (update - adventure_start + 1) * LogicRate : 0;
    bool finished = clock && sample.valid && adventure_complete(adventure_goal, progress, attest_adventure_splits);
    {
        std::scoped_lock lock(progress_mutex);
        current_progress.started = adventure_start != 0;
        current_progress.clock = clock;
        current_progress.updates = update;
        current_progress.buttons = previous.buttons;
        current_progress.stick_x = previous.stick_x;
        current_progress.stick_y = previous.stick_y;
        if (exporting_full_game_video())
            movie_progress.push_back(current_progress);
        if (attest_adventure_splits && clock && sample.valid) {
            const unsigned taj_flags = MEM_HU(0x14, guest(sample.settings));
            const unsigned world = MEM_BU(0x48, guest(sample.settings));
            if (update == adventure_start)
                reached_splits.mark(tracks ? "Start" : "Overworld", clock);
            if (use_adventure_route_splits) {
                const auto player = std::ranges::find(sample.racers, 0, &achievements::Racer::player);
                if (object && player != sample.racers.end() && player->finished && player->position > 0) {
                    if (sample.race_type == 0 && player->lap == sample.laps) {
                        reached_splits.observe_race(adventure_goal,
                                                    {unsigned(sample.level), unsigned(player->vehicle),
                                                     unsigned(sample.trophy_world), unsigned(sample.trophy_round),
                                                     player->position == 1},
                                                    clock);
                    } else if (player->position == 1 &&
                               ((minigames && minigame_race) || (bosses && sample.race_type == boss_race_type))) {
                        reached_splits.observe_event_win(adventure_goal, unsigned(sample.level), clock);
                    }
                }
                if (!tracks && !bosses && world == 5 && object)
                    reached_splits.observe_future_fun_land(sample.course_flags, sample.race_type, clock);
                finished = reached_splits.route_complete(adventure_goal, progress, use_completed_save_rules);
            }
            if (!tracks && !bosses)
                reached_splits.observe(previous_adventure_progress, progress, object ? sample.race_type : -1, world,
                                       previous_taj_flags, taj_flags, clock,
                                       use_adventure_route_splits ? adventure_goal : Adventure::None);
            if (finished)
                reached_splits.mark("Finish", clock);
            previous_adventure_progress = progress;
            previous_taj_flags = taj_flags;
        }
    }
    if (use_adventure_route_splits && reached_splits.wrong_first_trophy(adventure_goal)) {
        stop(false, "The first Trophy set did not match the selected category.");
        return;
    }
    append_word(observations, static_cast<unsigned>(netplay::packed_input_bits(previous)));
    if (adventure_start) {
        const unsigned menu = legacy_adventure || sample.mode == 1 ? unsigned(sample.menu) : 0U;
        for (unsigned word :
             {std::uint32_t(MEM_W(0, guest(settings.CurrentRngSeed))), unsigned(sample.mode), menu,
              unsigned(sample.level), unsigned(sample.slot), unsigned(sample.paused), unsigned(sample.trophy_world),
              unsigned(sample.trophy_round), progress.balloons, progress.keys, progress.bosses, progress.trophies,
              progress.tt_amulet, progress.wizpig_amulet})
            append_word(observations, word);
        for (auto flags : sample.course_flags)
            append_word(observations, flags);
        // 0x18 packs the render animation frame and active emitters, not
        // a coordinate or velocity. New recordings attest all six motion axes.
        const unsigned motion_slot = legacy_adventure ? 0x18 : 0x24;
        for (unsigned offset : {0x0CU, 0x10U, 0x14U, motion_slot, 0x1CU, 0x20U})
            append_word(observations, object ? std::uint32_t(MEM_W(offset, guest(object))) : 0);
    }
    if (adventure_start && sample.valid && adventure_goal == Adventure::HundredPercent &&
        adventure_complete(Adventure::AnyPercent, progress) && !finished) {
        stop(false, "Complete the 100% requirements before beating the final Wizpig.");
        return;
    }
    if (update % kAdventureInterval == 0 || finished) {
        const auto hash = mods::sha256(observations);
        const auto checkpoint = Json::array({update, hash});
        std::fprintf(stderr, "[adventure] tick=%u clock=%u hash=%s\n", update, clock, hash.c_str());
        if (playback() && (checkpoints.size() >= expected["checkpoints"].size() ||
                           expected["checkpoints"][checkpoints.size()] != checkpoint)) {
            stop(false, "Adventure input, progress or RNG diverged.");
            return;
        }
        checkpoints.push_back(checkpoint);
        observations.clear();
    }
    if (!finished)
        return;
    auto automatic_splits = Json::array();
    if (attest_adventure_splits) {
        {
            // Evidence includes all splits, even when the viewer is still
            // displaying an earlier update.
            std::scoped_lock lock(progress_mutex);
            for (const auto& split : reached_splits.entries)
                automatic_splits.push_back({split.label, split.clock});
        }
        std::fprintf(stderr, "[adventure] splits=%s\n", automatic_splits.dump().c_str());
    }
    if (playback()) {
        const bool matches = expected["clockTicks"] == clock && expected["finishTick"] == update &&
                             expected["checkpoints"] == checkpoints &&
                             (!attest_adventure_splits || expected["splits"] == automatic_splits);
        stop(matches, matches ? "Adventure objective and gameplay checkpoints verified."
                              : "The recorded Adventure finish did not match.");
        return;
    }
    try {
        Json manifest{{"ruleset", kAdventureRules},  {"rom", revision},
                      {"startSeed", BootSeed},       {"category", adventure_id(adventure_goal)},
                      {"practice", !ranked_attempt}, {"ticks", update},
                      {"finishTick", update},        {"clockTicks", clock},
                      {"checkpoints", checkpoints},  {"splits", automatic_splits}};
        add_ranked_fields(manifest);
        write_replay(manifest);
        const unsigned place =
            ranked_attempt && ranked_attempt->finish_targets ? ranked_attempt->finish_targets->place(clock) : 0;
        const bool personal_best = !prior_finish_personal_best || clock < prior_finish_personal_best;
        const bool celebration = dkr::runtime::speedrunning::celebrates(place, personal_best);
        const int finish_seconds = celebration ? 5 : options.get(Option::FinishSummary) ? 3 : 0;
        {
            std::scoped_lock lock(progress_mutex);
            current_progress.podium_place = place;
            current_progress.finish_update = update;
            if (finish_seconds)
                podium_exit_at = std::chrono::steady_clock::now() + std::chrono::seconds(finish_seconds);
        }
        stop(true, "Full Game run recorded.", !finish_seconds);
    } catch (const std::exception& failure) {
        stop(false, failure.what());
    }
}
} // namespace

extern "C" void dkr_time_trial_frame_end(std::uint8_t* rdram, unsigned revision) {
    if (!sampled || !supported_replay_rom(revision))
        return;
    sampled = false;
    ++update;
    if (adventure_goal != Adventure::None) {
        adventure_frame(rdram, revision);
        return;
    }
    if (race_finish) {
        {
            std::scoped_lock lock(progress_mutex);
            current_progress.updates = update;
            if (watching()) {
                current_progress.buttons = previous.buttons;
                current_progress.stick_x = previous.stick_x;
                current_progress.stick_y = previous.stick_y;
                timeline_progress.push_back(current_progress);
            }
        }
        if (!playback() && update - race_finish == TailUpdates)
            finish_recording();
        else if (watching() && update == current_progress.total_updates)
            stop(true, "Finish, laps and gameplay checkpoints verified.");
        return;
    }
    const auto& a = addresses::current();
    const auto& race = symbols::for_revision(revision);
    const auto object = player_address(rdram);
    const auto racer = object ? std::uint32_t(MEM_W(0x64, guest(object))) : 0;
    // Object_Racer fields are the retail guest ABI, independent of the host's struct layout.
    if (watching() && MEM_B(0, guest(a.IsPaused))) {
        // A game-side pause (for example, from a controller transition) must
        // never abort the Theatre. The visible transport owns the pause.
        MEM_B(0, guest(a.IsPaused)) = 0;
    }
    if (!in_rdram(racer, 0x1D9) || MEM_W(0, guest(race.map)) != selected_level || !MEM_B(0, guest(race.enabled)) ||
        MEM_B(0, guest(a.IsPaused)) || MEM_H(0, guest(racer)) != 0 || MEM_BU(3, guest(racer)) != racer_choice) {
        stop(false, "The simulation left the permitted solo Time Trial.");
        return;
    }
    constexpr unsigned loop_vehicle = 4; // VEHICLE_LOOPDELOOP
    const unsigned vehicle = MEM_BU(0x1D6, guest(racer));
    const auto expected_vehicle = static_cast<unsigned>(selected_vehicle);
    // The retail track temporarily changes vehicleID in a loop. vehicleIDPrev
    // retains the selected vehicle and is restored when the racer exits it.
    const bool retail_loop = vehicle == loop_vehicle && MEM_BU(0x1D7, guest(racer)) == expected_vehicle &&
                             selected_course &&
                             (selected_course->id == "walrus-cove" || selected_course->id == "darkmoon-caverns");
    if (vehicle != expected_vehicle && !retail_loop) {
        stop(false, "The simulation left the permitted solo Time Trial.");
        return;
    }
    if (preparing_ghost() || !ghost_export.empty()) {
        GhostPose pose{};
        for (unsigned axis = 0; axis < pose.position.size(); ++axis) {
            pose.position[axis] = std::uint32_t(MEM_W(0x0C + axis * 4, guest(object)));
            pose.rotation[axis] = MEM_HU(axis * 2, guest(object)) + MEM_HU(0x160 + axis * 2, guest(racer));
        }
        pose.segment = MEM_HU(0x2E, guest(object));
        ghost_poses.push_back(pose);
    }
    const unsigned lap = MEM_BU(0x194, guest(racer));
    const bool finished = MEM_B(0x1D8, guest(racer)) != 0 && lap == selected_laps;
    const unsigned countdown = MEM_W(0, guest(a.RaceStartTimer));
    append_word(observations, std::uint32_t(MEM_W(0, guest(a.CurrentRngSeed))));
    append_word(observations, countdown);
    for (unsigned offset : {0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20})
        append_word(observations, std::uint32_t(MEM_W(offset, guest(object))));
    append_word(observations, std::uint32_t(MEM_W(0xA8, guest(racer))));
    append_word(observations, MEM_BU(0x192, guest(racer)));
    append_word(observations, lap);
    std::array<unsigned, kMaximumLapCount> splits{};
    unsigned clock = 0;
    unsigned best = 0, recent = 0;
    for (unsigned index = 0; index < selected_laps; ++index) {
        const unsigned lap_time = MEM_W(0x128 + 4 * index, guest(racer));
        append_word(observations, lap_time);
        clock += lap_time;
        splits[index] = clock;
        if (index < lap) {
            recent = lap_time;
            best = best == 0 ? lap_time : std::min(best, lap_time);
        }
    }
    {
        std::scoped_lock lock(progress_mutex);
        current_progress.clock = clock;
        current_progress.lap = std::min(lap + 1, selected_laps);
        current_progress.best_lap = best;
        current_progress.recent_split = recent;
        current_progress.completed_laps = std::min(lap, selected_laps);
        if (ghost_clock && lap > 0 && lap <= selected_laps && ghost_splits[lap - 1])
            current_progress.ghost_gap = int(splits[lap - 1]) - int(ghost_splits[lap - 1]);
        current_progress.updates = update;
        current_progress.started = countdown == 0;
        current_progress.buttons = previous.buttons;
        current_progress.stick_x = previous.stick_x;
        current_progress.stick_y = previous.stick_y;
        if (watching())
            timeline_progress.push_back(current_progress);
    }
    if (update % CheckpointInterval == 0 || finished) {
        const auto hash = mods::sha256(observations);
        if (playback()) {
            const auto index = checkpoints.size();
            if (index >= expected["checkpoints"].size() ||
                expected["checkpoints"][index] != Json::array({update, hash})) {
                std::fprintf(stderr, "[time-trial] checkpoint %u actual=%s expected=%s\n", update, hash.c_str(),
                             index < expected["checkpoints"].size() ? expected["checkpoints"][index].dump().c_str()
                                                                    : "missing");
                stop(false, "Gameplay state or RNG diverged.");
                return;
            }
        }
        checkpoints.push_back(Json::array({update, hash}));
        observations.clear();
        std::fprintf(stderr, "[time-trial] tick=%u clock=%u lap=%u checkpoint=%u hash=%s\n", update, clock, lap,
                     MEM_BU(0x192, guest(racer)), hash.c_str());
    }
    if (!finished)
        return;
    const auto lap_splits = Json(std::vector<unsigned>(splits.begin(), splits.begin() + selected_laps));
    if (playback()) {
        const bool matches = expected["clockTicks"] == clock && expected["splits"] == lap_splits &&
                             expected.value("finishTick", expected.at("ticks").get<unsigned>()) == update &&
                             expected["checkpoints"] == checkpoints;
        if (matches) {
            export_verified_ghost();
            std::fprintf(stderr, "[time-trial] clock=%u splits=%s\n", clock, lap_splits.dump().c_str());
            if (watching() && update < current_progress.total_updates) {
                race_finish = update;
                return;
            }
        }
        stop(matches,
             matches ? "Finish, laps and gameplay checkpoints verified." : "The recorded finish did not match.");
        return;
    }
    race_finish = update;
    {
        std::scoped_lock lock(progress_mutex);
        current_progress.finish_update = update;
        if (ranked_attempt && ranked_attempt->finish_targets)
            current_progress.podium_place = ranked_attempt->finish_targets->place(clock);
    }
    finished_record = {{"ruleset", legacy_course() ? kLegacyRules : kVanillaRules},
                       {"rom", revision},
                       {"startSeed", BootSeed},
                       {"finishTick", update},
                       {"racer", racer_choice},
                       {"clockTicks", clock},
                       {"splits", lap_splits},
                       {"checkpoints", checkpoints},
                       {"practice", !ranked_attempt}};
    if (!legacy_course()) {
        finished_record["vehicle"] = kVehicleIds[static_cast<unsigned>(selected_vehicle)];
        if (custom_track_id.empty()) {
            finished_record["course"] = selected_course->id;
        } else {
            finished_record["ruleset"] = selected_laps == kRetailLapCount ? kCustomRules : kCustomVariableLapsRules;
            if (selected_laps != kRetailLapCount)
                finished_record["laps"] = selected_laps;
            finished_record["trackId"] = custom_track_id;
            finished_record["trackHash"] = custom_track_hash;
        }
    }
    add_ranked_fields(finished_record);
    std::fprintf(stderr, "[time-trial] clock=%u splits=%s\n", clock, lap_splits.dump().c_str());
}
} // namespace dkr::runtime::time_trial
