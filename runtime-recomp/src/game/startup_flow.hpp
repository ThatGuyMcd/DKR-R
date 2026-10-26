#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace dkr::runtime::startup {

// Launcher-owned, one-shot preference. Never part of a checkpoint or lobby
// settings: online startup follows the normal shared retail sequence.
inline std::atomic<bool> skip_intro_pending{false};
enum class Destination : int { StartMenu = 0, CharacterSelect = 1 };
inline std::atomic<Destination> launch_destination{Destination::StartMenu};
inline std::atomic<bool> character_select_pending{false};

inline Destination parse_destination(std::string_view value) {
    if (!value.empty() && value.back() == '\r')
        value.remove_suffix(1);
    return value == "1" ? Destination::CharacterSelect : Destination::StartMenu;
}

inline bool parse_skip_intro(std::string_view value) {
    // Settings copied from Windows retain CRLF when read on Linux/Android.
    if (!value.empty() && value.back() == '\r')
        value.remove_suffix(1);
    return value == "1";
}

inline void prepare_launch(bool requested, bool online, bool direct_track_boot,
                           Destination destination = Destination::StartMenu) {
    character_select_pending.store(false, std::memory_order_release);
    launch_destination.store(destination, std::memory_order_release);
    skip_intro_pending.store(requested && !online && !direct_track_boot, std::memory_order_release);
}

inline bool skip_logos() {
    return skip_intro_pending.load(std::memory_order_acquire);
}

inline bool consume_title_skip() {
    return skip_intro_pending.exchange(false, std::memory_order_acq_rel);
}

inline bool consume_character_select() {
    return character_select_pending.exchange(false, std::memory_order_acq_rel);
}

} // namespace dkr::runtime::startup
