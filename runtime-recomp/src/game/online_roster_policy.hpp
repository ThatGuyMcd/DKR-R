#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

inline constexpr std::size_t kOnlineControllerPorts = 4U;
inline constexpr std::size_t kDkrCharacterSlots = 8U;
inline constexpr std::size_t kDkrPlayerIdSlots = 16U;
inline constexpr std::uint32_t kN64BButton = 0x4000U;

struct CharacterSelectSeed {
    std::array<std::int8_t, kOnlineControllerPorts> active_players{};
    std::array<std::int8_t, kDkrCharacterSlots> characters{};
    std::array<std::uint8_t, kDkrPlayerIdSlots> player_ids{};
    std::uint32_t active_player_count = 0U;
};

constexpr bool contiguous_online_roster(const std::array<bool, kOnlineControllerPorts>& occupied) {
    bool found_empty = false;
    std::size_t count = 0U;
    for (const bool slot : occupied) {
        if (!slot) {
            found_empty = true;
        } else {
            if (found_empty)
                return false;
            ++count;
        }
    }
    return count >= 2U && occupied[0];
}

constexpr CharacterSelectSeed
make_character_select_seed(const std::array<bool, kOnlineControllerPorts>& occupied,
                           const std::array<std::int8_t, kDkrCharacterSlots>& existing_characters) {
    CharacterSelectSeed seed{};
    seed.characters.fill(-1);
    for (std::size_t index = 0; index < seed.player_ids.size(); ++index) {
        seed.player_ids[index] = static_cast<std::uint8_t>(index);
    }

    std::array<bool, 8U> used_default_characters{};
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
        if (!occupied[slot])
            continue;
        seed.active_players[slot] = 1;
        ++seed.active_player_count;
        const std::int8_t existing = existing_characters[slot];
        if (existing >= 0 && existing < 8 && !used_default_characters[static_cast<std::size_t>(existing)]) {
            seed.characters[slot] = existing;
            used_default_characters[static_cast<std::size_t>(existing)] = true;
        }
    }

    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
        if (!occupied[slot] || seed.characters[slot] >= 0)
            continue;
        for (std::size_t character = 0; character < used_default_characters.size(); ++character) {
            if (used_default_characters[character])
                continue;
            seed.characters[slot] = static_cast<std::int8_t>(character);
            used_default_characters[character] = true;
            break;
        }
    }
    return seed;
}

constexpr std::uint32_t locked_character_select_buttons(bool occupied, std::int8_t character_status,
                                                        std::uint32_t buttons) {
    if (!occupied)
        return 0U;
    // DKR normally treats B on an unconfirmed racer as "remove this player".
    // Online membership is frozen at launch, so B remains available to undo a
    // confirmed choice but cannot remove an assigned network port.
    if (character_status == 0)
        return buttons & ~kN64BButton;
    return buttons;
}

} // namespace dkr::runtime::netplay
