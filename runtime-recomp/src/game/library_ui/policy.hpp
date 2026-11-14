#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dkr::library_ui {
// Existing page identities stay stable. Only visual order changes.
inline constexpr int LibraryPage = 10, PageCount = 11;
inline constexpr std::array<int, PageCount> SidebarOrder{0, 5, LibraryPage, 6, 1, 2, 3, 7, 13, 11, 12};
inline constexpr int sidebar_parent(int page) {
    return page == 4 || page == 9 ? 0 : page;
}
inline constexpr int TexturesSection = 4;
// Preserve existing section identities used by links and import callbacks.
inline constexpr std::array<int, 5> ModsSectionOrder{0, 1, TexturesSection, 2, 3};
inline constexpr std::array<const char*, 5> ModsSections{
    "My mods##section", "Magic Codes##section", "Textures##section", "Track Lab##section", "Help & imports##section"};
inline constexpr std::array<const char*, 5> ControlsSections{"Device", "Button mapping", "Stick response", "Motion",
                                                             "Shortcuts"};
inline int grid_columns(float width) {
    // Give titles room to breathe, with at most six cards on wide screens.
    return std::clamp(int((std::max(0.F, width) + 20.F) / 320.F), 1, 6);
}
inline constexpr std::string_view AnalogueId = "dkr_analogue_driving";
inline constexpr std::string_view AnalogueHelp =
    "Independent analogue accelerator and brake for cars, hovercraft and planes. "
    "Set up each player's controls here. Changes are saved on this device.";
inline bool configurable(std::string_view id, bool selected, bool approved, bool compatible, bool active) {
    return id == AnalogueId && ((selected && approved && compatible) || active);
}
inline bool writes_locked(bool game, bool lobby, bool preparing, bool admission, bool vehicles) {
    return game || lobby || preparing || admission || vehicles;
}
// This conversion is for displayed status copy, never IDs, URLs or credentials.
inline std::string visible_status(std::string value) {
    for (const auto& words : {std::pair{"Mod Store", "Mod Library"}, std::pair{"MOD STORE", "MOD LIBRARY"}}) {
        std::size_t at = 0;
        while ((at = value.find(words.first, at)) != value.npos) {
            value.replace(at, std::char_traits<char>::length(words.first), words.second);
            at += std::char_traits<char>::length(words.second);
        }
    }
    return value;
}
struct CaptureTarget {
    std::optional<unsigned> player;
    bool begin(unsigned target) {
        if (target >= 4 || player)
            return false;
        player = target;
        return true;
    }
    std::size_t resolve(std::size_t normal) const {
        return player ? *player : normal;
    }
    void clear() {
        player.reset();
    }
};
} // namespace dkr::library_ui
