#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// The on-screen overlay preferences (FPS, network, controller input) in the
// settings file. This header only parses and formats; the runtime snapshots
// its live values into a `Settings`, lets `load` overwrite whatever the file
// names, then applies the result. No key depends on another, so applying once
// after the whole file is read matches assigning each key as it is parsed.
namespace dkr::overlay {
struct Colour {
    float r = 0.0F, g = 0.0F, b = 0.0F, a = 1.0F;
};

// Order is the order of the saved keys: fps_custom_<name>.
inline constexpr std::size_t FpsFieldCount = 8;
inline constexpr std::array<std::string_view, FpsFieldCount> FpsFieldNames{
    "frame_time", "simulation", "graphics", "vi", "interpolation", "audio", "target", "resolution"};

struct Settings {
    bool fps_enabled = false;
    int fps_position = 1;
    int fps_detail = 1;
    bool fps_single_row = false;
    std::array<bool, FpsFieldCount> fps_custom{true, true, false, false, false, false, true, false};
    int fps_font_size = 24;
    Colour fps_fill{0.96F, 0.18F, 0.10F, 1.0F};
    Colour fps_outline{1.0F, 0.58F, 0.04F, 1.0F};
    bool network_enabled = false;
    int network_position = 0;
    int network_detail = 1;
    bool network_single_row = false;
    bool controller_input_enabled = false;
    int controller_input_position = 2;
};

namespace detail {
inline bool channel(std::string_view key, std::string_view prefix, Colour& colour, int number) {
    if (!key.starts_with(prefix) || key.size() != prefix.size() + 1U)
        return false;
    const float value = std::clamp(number, 0, 255) / 255.0F;
    switch (key.back()) {
    case 'r':
        colour.r = value;
        return true;
    case 'g':
        colour.g = value;
        return true;
    case 'b':
        colour.b = value;
        return true;
    case 'a':
        colour.a = value;
        return true;
    default:
        return false;
    }
}

template <class Stream> void channels(Stream& out, std::string_view prefix, const Colour& colour) {
    out << prefix << "r=" << static_cast<int>(colour.r * 255.0F) << '\n';
    out << prefix << "g=" << static_cast<int>(colour.g * 255.0F) << '\n';
    out << prefix << "b=" << static_cast<int>(colour.b * 255.0F) << '\n';
    out << prefix << "a=" << static_cast<int>(colour.a * 255.0F) << '\n';
}
} // namespace detail

// Returns true when `key` belongs to the overlay group. `number` is the
// already-parsed value, so a malformed line never gets here.
inline bool load(std::string_view key, int number, Settings& settings) {
    if (key == "fps_overlay_enabled")
        settings.fps_enabled = number != 0;
    else if (key == "fps_overlay_position")
        settings.fps_position = std::clamp(number, 0, 3);
    else if (key == "fps_overlay_detail")
        settings.fps_detail = std::clamp(number, 0, 3);
    else if (key == "fps_overlay_single_row")
        settings.fps_single_row = number != 0;
    else if (key == "fps_font_size")
        settings.fps_font_size = std::clamp(number, 16, 64);
    else if (detail::channel(key, "fps_fill_", settings.fps_fill, number) ||
             detail::channel(key, "fps_outline_", settings.fps_outline, number)) {
        // Handled by `channel`.
    } else if (key == "network_overlay_enabled")
        settings.network_enabled = number != 0;
    else if (key == "network_overlay_position")
        settings.network_position = std::clamp(number, 0, 3);
    else if (key == "network_overlay_detail")
        settings.network_detail = std::clamp(number, 0, 2);
    else if (key == "network_overlay_single_row")
        settings.network_single_row = number != 0;
    else if (key == "controller_input_overlay_enabled")
        settings.controller_input_enabled = number != 0;
    else if (key == "controller_input_overlay_position")
        settings.controller_input_position = std::clamp(number, 0, 3);
    else {
        constexpr std::string_view custom = "fps_custom_";
        if (!key.starts_with(custom))
            return false;
        const auto name = std::find(FpsFieldNames.begin(), FpsFieldNames.end(), key.substr(custom.size()));
        if (name == FpsFieldNames.end())
            return false;
        settings.fps_custom[static_cast<std::size_t>(name - FpsFieldNames.begin())] = number != 0;
    }
    return true;
}

template <class Stream> void save(Stream& out, const Settings& settings) {
    out << "fps_overlay_enabled=" << (settings.fps_enabled ? 1 : 0) << '\n';
    out << "fps_overlay_position=" << settings.fps_position << '\n';
    out << "fps_overlay_detail=" << settings.fps_detail << '\n';
    out << "fps_overlay_single_row=" << (settings.fps_single_row ? 1 : 0) << '\n';
    for (std::size_t field = 0; field < FpsFieldCount; ++field)
        out << "fps_custom_" << FpsFieldNames[field] << '=' << (settings.fps_custom[field] ? 1 : 0) << '\n';
    out << "fps_font_size=" << settings.fps_font_size << '\n';
    detail::channels(out, "fps_fill_", settings.fps_fill);
    detail::channels(out, "fps_outline_", settings.fps_outline);
    out << "network_overlay_enabled=" << (settings.network_enabled ? 1 : 0) << '\n';
    out << "network_overlay_position=" << settings.network_position << '\n';
    out << "network_overlay_detail=" << settings.network_detail << '\n';
    out << "network_overlay_single_row=" << (settings.network_single_row ? 1 : 0) << '\n';
    out << "controller_input_overlay_enabled=" << (settings.controller_input_enabled ? 1 : 0) << '\n';
    out << "controller_input_overlay_position=" << settings.controller_input_position << '\n';
}
} // namespace dkr::overlay
