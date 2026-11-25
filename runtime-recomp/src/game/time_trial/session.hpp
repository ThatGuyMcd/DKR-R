#pragma once

#include "adventure_rules.hpp"
#include "adventure_splits.hpp"
#include "courses.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <vector>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dkr::runtime::presentation {
struct ReplayPresentation;
}

namespace dkr::runtime::time_trial {
enum class Mode { None, Record, SelfTest, Verify, Watch, PrepareGhost };

inline constexpr bool supported_replay_rom(unsigned revision) {
    return revision == 77 || revision == 80;
}

inline constexpr unsigned kDiddy = 9;
struct Racer {
    std::string_view name;
    std::string_view board;
};
inline constexpr std::array kRacers{
    Racer{"Krunch", "ancient-lake-car-krunch"},
    Racer{"Bumper", "ancient-lake-car-bumper"},
    Racer{"Tiptup", "ancient-lake-car-tiptup"},
    Racer{"Conker", "ancient-lake-car-conker"},
    Racer{"Timber", "ancient-lake-car-timber"},
    Racer{"Banjo", "ancient-lake-car-banjo"},
    Racer{"Drumstick", "ancient-lake-car-drumstick"},
    Racer{"Pipsy", "ancient-lake-car-pipsy"},
    Racer{"T.T.", "ancient-lake-car-tt"},
    Racer{"Diddy", "ancient-lake-car"},
};

struct Progress {
    unsigned clock = 0;
    unsigned lap = 0;
    unsigned lap_count = kRetailLapCount;
    unsigned recent_split = 0;
    unsigned best_lap = 0;
    unsigned updates = 0;
    unsigned total_updates = 0;
    unsigned finish_update = 0;
    unsigned racer = kDiddy;
    std::array<unsigned, kMaximumLapCount> lap_updates{};
    unsigned buttons = 0;
    int stick_x = 0, stick_y = 0;
    Adventure adventure = Adventure::None;
    bool started = false;
    bool ranked = false;
    bool replay = false;
    bool ghost = false;
    unsigned completed_laps = 0;
    std::optional<int> ghost_gap;
    unsigned podium_place = 0;
};

struct Result {
    std::filesystem::path file;
    std::string message;
    unsigned clock = 0;
    unsigned best_lap = 0;
    bool complete = false;
    bool ranked = false;
    bool replay = false;
};

struct FinishTargets {
    std::string title;
    unsigned personal = 0;
    std::array<unsigned, 3> rivals{};

    constexpr unsigned place(unsigned clock) const {
        if (!clock || (personal && clock >= personal))
            return 0;
        unsigned place = 1;
        for (auto rival : rivals) {
            if (rival == clock)
                return 0;
            if (rival && rival < clock)
                ++place;
        }
        return place <= 3 ? place : 0;
    }
};

struct Attempt {
    std::string board;
    std::string engine;
    std::string ticket;
    std::string track_id, track_hash, release_hash;
    std::optional<FinishTargets> finish_targets;
};

struct FrameStamp {
    Progress progress;
    std::uint32_t framebuffer = 0;
    std::shared_ptr<const presentation::ReplayPresentation> sidecar;
};

// Presentation only. The label is supplied by the leaderboard, outside the replay data.
struct GhostNameplate {
    std::shared_ptr<const std::string> name;
    std::array<float, 3> position;
    float scale;
    float opacity;
    std::uint32_t framebuffer;
    bool outline;
};
bool ghost_nameplate_active();
std::optional<GhostNameplate> ghost_nameplate(const std::uint8_t* snapshot);

struct Theatre {
    unsigned buffered = 0;
    unsigned seek_to = 0;
    unsigned speed = 100;
    bool paused = false;
    bool seeking = false;
    bool scrubbing = false;
    bool finished = false;
    bool buffering = true;
    bool pause_on_open = false;
};

std::string board_for(const Course& course, Vehicle vehicle, unsigned racer);
std::optional<Attempt> parse_attempt(const std::string& board, const std::string& engine, const std::string& ticket,
                                     const Course& course = kCourses.front(), Vehicle vehicle = Vehicle::Car,
                                     unsigned racer = kDiddy);
std::optional<Attempt> parse_custom_attempt(const std::string& board, const std::string& engine,
                                            const std::string& ticket, std::string_view release,
                                            std::string_view release_hash, std::string_view track,
                                            std::string_view track_hash, Vehicle vehicle, unsigned racer);
std::optional<Attempt> parse_adventure_attempt(const std::string& board, const std::string& engine,
                                               const std::string& ticket, Adventure goal);
bool start(Mode mode, const std::filesystem::path& replay, std::string& error, const Attempt* attempt = nullptr,
           unsigned racer = kDiddy, const Course& course = kCourses.front(), Vehicle vehicle = Vehicle::Car,
           Adventure adventure = Adventure::None, std::string_view track = {},
           const std::filesystem::path& track_package = {}, const std::filesystem::path& ghost_export = {});
bool start_ghost(const std::filesystem::path& source, const std::filesystem::path& recording, std::string& error,
                 unsigned racer, Mode practice_mode = Mode::Record, const std::filesystem::path& track_package = {},
                 const Attempt* attempt = nullptr, std::string_view credited_name = {});
bool advance_ghost();
bool preparing_ghost();
bool replaying();
bool uses_uninitialised_adventure_save();
bool uses_completed_adventure_save();
void finish_interrupted_recording();
bool request_restart();
bool take_restart_request();
bool restart_confirmation_pending();
void confirm_restart(bool restart);
bool watching();
std::filesystem::path watched_replay();
bool exporting_video();
bool exporting_full_game_video();
bool export_video_picture(unsigned frame, unsigned index);
bool export_video_audio(unsigned frame, std::span<const std::int16_t> audio);
bool theatre_holding_frame();
Theatre theatre();
void theatre_play_pause();
void theatre_complete_intro();
void theatre_seek(unsigned update);
void theatre_scrub(unsigned update);
void theatre_graphics_submitted(std::uint8_t* rdram, std::uint32_t display_list,
                                std::shared_ptr<const presentation::ReplayPresentation> presentation);
std::optional<FrameStamp> theatre_graphics_stamp(std::uint32_t display_list);
void theatre_buffered(unsigned frame);
std::optional<unsigned> theatre_next_frame(std::chrono::steady_clock::time_point now);
bool theatre_presented(Progress source, unsigned frame);
void theatre_archive_error();
bool theatre_capture_audio(std::span<const std::int16_t> samples);
std::vector<std::int16_t> theatre_take_audio();
bool theatre_take_audio_reset();
void theatre_cancel_seek();
void theatre_exit();
void theatre_step(int delta);
void theatre_speed(unsigned percent);
bool active();
bool recording();
int status();
Progress progress();
std::vector<AdventureSplit> adventure_splits();
std::vector<unsigned> adventure_timeline_markers();
Result last_result();
void set_finish_personal_best(unsigned ticks);
void enable_launcher_return();
float launcher_return_opacity();
void retire();
} // namespace dkr::runtime::time_trial
