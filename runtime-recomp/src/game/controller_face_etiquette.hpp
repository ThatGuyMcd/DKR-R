#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dkr::runtime::controllers {

// Face-button reporting for Nintendo diamond pads. Xbox/PlayStation keep SDL
// position numbering. Switch Pro / Joy-Con are normalised to printed letters
// (physical A reports as SDL A) so stock bindings match Nintendo etiquette.

enum class ControllerFamily : std::uint8_t {
    Xbox = 0,
    PlayStation = 1,
    Switch = 2,
    N64 = 3,
    GameCube = 4,
};

inline constexpr int kSdlControllerTypeUnknown = 0;
inline constexpr int kSdlControllerTypeXbox360 = 1;
inline constexpr int kSdlControllerTypeXboxOne = 2;
inline constexpr int kSdlControllerTypePs3 = 3;
inline constexpr int kSdlControllerTypePs4 = 4;
inline constexpr int kSdlControllerTypeNintendoSwitchPro = 5;
inline constexpr int kSdlControllerTypeVirtual = 6;
inline constexpr int kSdlControllerTypePs5 = 7;
inline constexpr int kSdlControllerTypeAmazonLuna = 8;
inline constexpr int kSdlControllerTypeGoogleStadia = 9;
inline constexpr int kSdlControllerTypeNvidiaShield = 10;
inline constexpr int kSdlControllerTypeJoyConLeft = 11;
inline constexpr int kSdlControllerTypeJoyConRight = 12;
inline constexpr int kSdlControllerTypeJoyConPair = 13;

inline bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const auto match_at = [&](std::size_t offset) {
        for (std::size_t index = 0; index < needle.size(); ++index) {
            const auto left = static_cast<unsigned char>(haystack[offset + index]);
            const auto right = static_cast<unsigned char>(needle[index]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t offset = 0; offset + needle.size() <= haystack.size();
         ++offset) {
        if (match_at(offset)) {
            return true;
        }
    }
    return false;
}

inline bool name_looks_like_n64(std::string_view name) {
    return contains_ci(name, "n64") || contains_ci(name, "nintendo 64");
}

inline bool name_looks_like_gamecube(std::string_view name) {
    return contains_ci(name, "gamecube") || contains_ci(name, "game cube") ||
           contains_ci(name, "nso gc") || contains_ci(name, "raphnet gc") ||
           contains_ci(name, "gc adapter") ||
           contains_ci(name, "gc and n64") ||
           contains_ci(name, "gamecube controller");
}

inline bool name_looks_like_switch(std::string_view name) {
    if (name_looks_like_n64(name) || name_looks_like_gamecube(name)) {
        return false;
    }
    return contains_ci(name, "switch pro") || contains_ci(name, "joy-con") ||
           contains_ci(name, "joycon") ||
           contains_ci(name, "nintendo switch") ||
           contains_ci(name, "nso snes");
}

inline ControllerFamily family_from_sdl_type(int sdl_type) {
    switch (sdl_type) {
    case kSdlControllerTypePs3:
    case kSdlControllerTypePs4:
    case kSdlControllerTypePs5:
        return ControllerFamily::PlayStation;
    case kSdlControllerTypeNintendoSwitchPro:
    case kSdlControllerTypeJoyConLeft:
    case kSdlControllerTypeJoyConRight:
    case kSdlControllerTypeJoyConPair:
        return ControllerFamily::Switch;
    default:
        return ControllerFamily::Xbox;
    }
}

// Prefer N64/GameCube name hits over SDL's Switch-Pro classification: NSO and
// third-party retro pads often report as a generic Nintendo device.
inline ControllerFamily detect_controller_family(std::string_view name,
                                                 int sdl_type =
                                                     kSdlControllerTypeUnknown) {
    if (name_looks_like_n64(name)) {
        return ControllerFamily::N64;
    }
    if (name_looks_like_gamecube(name)) {
        return ControllerFamily::GameCube;
    }
    if (sdl_type != kSdlControllerTypeUnknown) {
        if (sdl_type == kSdlControllerTypeVirtual &&
            name_looks_like_switch(name)) {
            return ControllerFamily::Switch;
        }
        return family_from_sdl_type(sdl_type);
    }
    if (name_looks_like_switch(name)) {
        return ControllerFamily::Switch;
    }
    return ControllerFamily::Xbox;
}

constexpr bool uses_nintendo_face_etiquette(ControllerFamily family) {
    return family == ControllerFamily::Switch;
}

constexpr std::size_t nintendo_label_face_button(std::size_t button) {
    switch (button) {
    case 0U:
        return 1U;
    case 1U:
        return 0U;
    case 2U:
        return 3U;
    case 3U:
        return 2U;
    default:
        return button;
    }
}

template <typename ButtonArray>
inline void apply_nintendo_face_etiquette(ButtonArray& buttons) {
    const auto south = buttons[0];
    const auto east = buttons[1];
    const auto west = buttons[2];
    const auto north = buttons[3];
    buttons[0] = east;
    buttons[1] = south;
    buttons[2] = north;
    buttons[3] = west;
}

} // namespace dkr::runtime::controllers
