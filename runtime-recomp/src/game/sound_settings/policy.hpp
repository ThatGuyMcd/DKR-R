#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

// The Sound page's slice of the settings file. This header only parses and
// formats; the runtime owns the live volumes and applies a `Loaded` once the
// whole file has been read (muted channels go silent last, so each keeps the
// level it was saved with).
namespace dkr::sound {
// Master, then the four-part island mix: music, effects, vehicles, nature.
inline constexpr std::size_t ChannelCount = 5;
using Curve = std::array<int, 3>; // bass, mid, treble in whole dB

// What the runtime writes. A muted channel reports the level it keeps.
struct Settings {
    std::array<float, ChannelCount> level{1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
    unsigned muted = 0U; // one bit per channel
    std::optional<Curve> custom_curve;
    bool multiplayer_race_music = false;
    float bass = 0.0F, mid = 0.0F, treble = 0.0F;
};

// What a file asked for; unset fields leave the runtime's value alone.
struct Loaded {
    std::array<std::optional<float>, ChannelCount> level;
    unsigned muted = 0U;
    std::optional<Curve> custom_curve;
    std::optional<bool> multiplayer_race_music;
    std::optional<float> bass, mid, treble;
};

inline constexpr std::array<std::string_view, ChannelCount> LevelKeys{
    "master_volume", "music_volume", "sound_effects_volume", "vehicle_volume", "nature_volume"};

// Returns true when `key` belongs to the sound group. A malformed number
// throws, exactly as the settings parser always did for these keys, so the
// caller can log it and skip the line.
inline bool load(std::string_view key, const std::string& value, Loaded& loaded) {
    if (key == "sound_muted") {
        loaded.muted = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        return true;
    }
    if (key == "eq_custom") {
        Curve curve{};
        char comma_a = 0;
        char comma_b = 0;
        std::istringstream fields(value);
        if (fields >> curve[0] >> comma_a >> curve[1] >> comma_b >> curve[2] && comma_a == ',' && comma_b == ',') {
            for (int& band : curve)
                band = std::clamp(band, -12, 12);
            loaded.custom_curve = curve;
        }
        return true;
    }
    for (std::size_t channel = 0; channel < ChannelCount; ++channel) {
        if (key == LevelKeys[channel]) {
            (void)std::stoi(value); // Every other numeric key rejects what stoi rejects, e.g. ".5".
            loaded.level[channel] = std::stof(value);
            return true;
        }
    }
    if (key == "modern_multiplayer_race_music") {
        loaded.multiplayer_race_music = std::stoi(value) != 0;
        return true;
    }
    const bool bass = key == "eq_bass";
    const bool mid = key == "eq_mid";
    const bool treble = key == "eq_treble";
    if (!bass && !mid && !treble)
        return false;
    (void)std::stoi(value);
    (bass ? loaded.bass : mid ? loaded.mid : loaded.treble) = std::stof(value);
    return true;
}

template <class Stream> void save(Stream& out, const Settings& settings) {
    for (std::size_t channel = 0; channel < ChannelCount; ++channel)
        out << LevelKeys[channel] << '=' << settings.level[channel] << '\n';
    out << "sound_muted=" << settings.muted << '\n';
    if (settings.custom_curve) {
        const Curve& curve = *settings.custom_curve;
        out << "eq_custom=" << curve[0] << ',' << curve[1] << ',' << curve[2] << '\n';
    }
    out << "modern_multiplayer_race_music=" << (settings.multiplayer_race_music ? 1 : 0) << '\n';
    out << "eq_bass=" << settings.bass << '\n';
    out << "eq_mid=" << settings.mid << '\n';
    out << "eq_treble=" << settings.treble << '\n';
}
} // namespace dkr::sound
