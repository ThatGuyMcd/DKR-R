#pragma once

#include <array>
#include <cstdint>

namespace dkr::runtime::enhancements {

inline constexpr std::uint8_t kChooseYourRacerSequence = 0x1A;
inline constexpr std::uint8_t kInvalidCharacterChannel = 0x64;

inline constexpr std::array<std::array<std::uint8_t, 2>, 10> kCharacterMusicChannels{{
    {{0x0F, kInvalidCharacterChannel}},
    {{0x0C, 0x07}},
    {{0x09, kInvalidCharacterChannel}},
    {{0x0A, kInvalidCharacterChannel}},
    {{0x08, kInvalidCharacterChannel}},
    {{0x0B, kInvalidCharacterChannel}},
    {{0x0D, kInvalidCharacterChannel}},
    {{0x0E, kInvalidCharacterChannel}},
    {{0x05, kInvalidCharacterChannel}},
    {{0x04, kInvalidCharacterChannel}},
}};

constexpr std::uint16_t character_music_channel_mask(int selected_character) {
    std::uint16_t mask = 0xFFFFU;

    // Channel 6 and every character-specific channel start muted. Channels
    // 0-3 are the shared backing arrangement and remain enabled.
    mask &= static_cast<std::uint16_t>(~(1U << 6U));
    for (const auto& channels : kCharacterMusicChannels) {
        for (const std::uint8_t channel : channels) {
            if (channel < 16U) {
                mask &= static_cast<std::uint16_t>(~(1U << channel));
            }
        }
    }

    if (selected_character >= 0 && selected_character < static_cast<int>(kCharacterMusicChannels.size())) {
        for (const std::uint8_t channel : kCharacterMusicChannels[static_cast<std::size_t>(selected_character)]) {
            if (channel < 16U) {
                mask |= static_cast<std::uint16_t>(1U << channel);
            }
        }
    }

    return mask;
}

} // namespace dkr::runtime::enhancements
