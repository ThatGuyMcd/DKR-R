#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <string_view>

namespace dkr::runtime::speedrunning {

enum class Option {
    Splits,
    SplitPosition,
    SplitDetail,
    Comparison,
    Upcoming,
    Precision,
    HudSize,
    Inset,
    Background,
    Contrast,
    DeltaWords,
    ReducedMotion,
    GhostNames,
    GhostOpacity,
    GhostGap,
    GhostStyle,
    GhostProximity,
    GhostMarker,
    ReplayInputs,
    ReplaySplits,
    InputPosition,
    ReplaySpeed,
    PlaybackControls,
    InputSize,
    SeekStep,
    StartPaused,
    LapMarkers,
    LapDelta,
    Celebration,
    CelebrationScope,
    FinishSummary,
    CueVolume,
    Retention,
    ReturnTo,
    PostPrompt,
    RememberSetup,
    ConfirmRestart,
    ReplaySort,
    Count
};

struct Setting {
    std::string_view key;
    int initial, maximum;
};

inline constexpr std::array<Setting, static_cast<std::size_t>(Option::Count)> settings{{
    {"splits", 1, 1},          {"split_position", 0, 3},    {"split_detail", 0, 2},      {"comparison", 0, 3},
    {"upcoming", 1, 2},        {"precision", 0, 1},         {"hud_size", 1, 2},          {"inset", 1, 2},
    {"background", 85, 100},   {"contrast", 0, 1},          {"delta_words", 0, 1},       {"reduced_motion", 0, 1},
    {"ghost_names", 1, 1},     {"ghost_opacity", 50, 100},  {"ghost_gap", 1, 1},         {"ghost_style", 0, 1},
    {"ghost_proximity", 0, 1}, {"ghost_marker", 1, 1},      {"replay_inputs", 1, 1},     {"replay_splits", 1, 1},
    {"input_position", 2, 3},  {"replay_speed", 1, 3},      {"playback_controls", 0, 1}, {"input_size", 1, 2},
    {"seek_step", 1, 2},       {"start_paused", 0, 1},      {"lap_markers", 1, 1},       {"lap_delta", 1, 1},
    {"celebration", 0, 2},     {"celebration_scope", 0, 2}, {"finish_summary", 1, 1},    {"cue_volume", 35, 100},
    {"retention", 0, 1},       {"return_to", 0, 1},         {"post_prompt", 1, 1},       {"remember_setup", 1, 1},
    {"confirm_restart", 0, 1}, {"replay_sort", 0, 2},
}};

inline constexpr std::string_view prefix = "speedrunning_";
inline constexpr std::array<unsigned, 4> playback_speeds{50, 100, 150, 200};
inline constexpr std::array<int, 3> seek_seconds{5, 10, 30};

class Preferences {
    std::array<std::atomic<int>, settings.size()> values_{};

  public:
    Preferences() {
        reset();
    }

    void reset() {
        for (std::size_t i = 0; i < settings.size(); ++i)
            values_[i].store(settings[i].initial, std::memory_order_relaxed);
    }

    int get(Option option) const {
        return values_[static_cast<std::size_t>(option)].load(std::memory_order_relaxed);
    }

    void set(Option option, int value) {
        const auto index = static_cast<std::size_t>(option);
        const int minimum = option == Option::Background ? 55 : 0;
        values_[index].store(std::clamp(value, minimum, settings[index].maximum), std::memory_order_relaxed);
    }

    bool parse(std::string_view key, std::string_view value) {
        if (!key.starts_with(prefix))
            return false;
        key.remove_prefix(prefix.size());
        const auto entry = std::ranges::find(settings, key, &Setting::key);
        if (entry == settings.end())
            return false;
        int number = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
            set(static_cast<Option>(entry - settings.begin()), number);
        return true;
    }
};

inline Preferences options;

inline bool celebrates(unsigned place, bool personal_best) {
    const auto scope = options.get(Option::CelebrationScope);
    return options.get(Option::Celebration) != 2 &&
           ((place && (scope != 1 || place == 1)) || (scope == 2 && personal_best));
}

} // namespace dkr::runtime::speedrunning
