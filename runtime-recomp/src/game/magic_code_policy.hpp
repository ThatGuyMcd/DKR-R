#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::magic_codes {

enum class MagicCodeBehaviour : std::uint8_t {
    Persistent,
    DeferredAction,
    Diagnostic,
};

struct MagicCodeDefinition {
    std::uint8_t internal_index;
    const char* phrase;
    const char* effect;
    const char* availability;
    MagicCodeBehaviour behaviour;
};

inline constexpr std::array<MagicCodeDefinition, 24> kMagicCodeDefinitions{{
    {4, "ARNOLD", "Large racers", "ALL RACE MODES", MagicCodeBehaviour::Persistent},
    {5, "TEENYWEENIES", "Small racers", "ALL RACE MODES", MagicCodeBehaviour::Persistent},
    {6, "JUKEBOX", "Unlock the Music Test", "OPTIONS MENU", MagicCodeBehaviour::Persistent},
    {7, "FREEFRUIT", "Start races with ten bananas", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {8, "BLABBERMOUTH", "Character voices replace vehicle horns", "ALL RACE MODES", MagicCodeBehaviour::Persistent},
    {10, "WHODIDTHIS", "Open Options > Magic Codes, then go Back to show the credits",
     "ONE SHOT - OFFLINE ONLY; KEPT QUEUED ONLINE", MagicCodeBehaviour::DeferredAction},
    {11, "BYEBYEBALLOONS", "Disable Weapon Balloons", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {12, "NOYELLOWSTUFF", "Disable bananas", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {13, "BOGUSBANANAS", "Bananas reduce speed", "TRACKS MODE - NOT TIME TRIAL", MagicCodeBehaviour::Persistent},
    {14, "VITAMINB", "Remove the banana limit", "TRACKS MODE - NOT TIME TRIAL", MagicCodeBehaviour::Persistent},
    {15, "BOMBSAWAY", "All Weapon Balloons are red", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {16, "TOXICOFFENDER", "All Weapon Balloons are green", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {17, "ROCKETFUEL", "All Weapon Balloons are blue", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {18, "BODYARMOR", "All Weapon Balloons are yellow", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {19, "OPPOSITESATTRACT", "All Weapon Balloons are rainbow", "TRACKS MODE - NOT CHALLENGES OR TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {20, "FREEFORALL", "Weapon Balloons begin fully powered", "TRACKS MODE - NOT TIME TRIAL",
     MagicCodeBehaviour::Persistent},
    {21, "ZAPTHEZIPPERS", "Disable zippers", "TRACKS MODE - NOT TIME TRIAL", MagicCodeBehaviour::Persistent},
    {22, "DOUBLEVISION", "Allow duplicate racers", "CHARACTER SELECT", MagicCodeBehaviour::Persistent},
    {23, "OFFROAD", "Enable four-wheel drive", "TRACKS MODE - NOT TIME TRIAL", MagicCodeBehaviour::Persistent},
    {24, "JOINTVENTURE", "Enable two-player Adventure", "CHARACTER SELECT AND ADVENTURE",
     MagicCodeBehaviour::Persistent},
    {25, "TIMETOLOSE", "Enable Ultimate AI", "ALL RACE MODES", MagicCodeBehaviour::Persistent},
    {26, "EOLAOBFENRLONE", "Grant one Golden Balloon to the selected Adventure save",
     "ONE SHOT - ADVENTURE FILE SELECT", MagicCodeBehaviour::DeferredAction},
    {27, "EPC", "Enable the EPC lock-up diagnostic", "DIAGNOSTIC - ONLY VISIBLE DURING A FAULT",
     MagicCodeBehaviour::Diagnostic},
    {28, "DODGYROMMER", "Display the ROM checksum", "DIAGNOSTIC - IN-GAME MAGIC CODES SCREEN",
     MagicCodeBehaviour::Diagnostic},
}};

constexpr bool magic_code_is_one_shot(const MagicCodeDefinition& definition) {
    return definition.behaviour == MagicCodeBehaviour::DeferredAction;
}

constexpr bool magic_code_is_diagnostic(const MagicCodeDefinition& definition) {
    return definition.behaviour == MagicCodeBehaviour::Diagnostic;
}

constexpr std::uint32_t magic_code_bit(std::uint8_t internal_index) {
    return internal_index < 32U ? (1U << internal_index) : 0U;
}

constexpr std::uint32_t selectable_magic_code_mask() {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        result |= magic_code_bit(definition.internal_index);
    }
    return result;
}

constexpr std::uint32_t one_shot_magic_code_mask() {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        if (magic_code_is_one_shot(definition)) {
            result |= magic_code_bit(definition.internal_index);
        }
    }
    return result;
}

inline constexpr std::uint32_t kSelectableMagicCodeMask = selectable_magic_code_mask();
inline constexpr std::uint32_t kOneShotMagicCodeMask = one_shot_magic_code_mask();
inline constexpr std::uint32_t kPersistentMagicCodeMask = kSelectableMagicCodeMask & ~kOneShotMagicCodeMask;

constexpr std::uint32_t enable_magic_code(std::uint32_t current, std::uint8_t internal_index) {
    const std::uint32_t selected = magic_code_bit(internal_index);
    if ((selected & kSelectableMagicCodeMask) == 0U) {
        return current & kSelectableMagicCodeMask;
    }

    current = (current | selected) & kSelectableMagicCodeMask;
    const std::uint32_t big = magic_code_bit(4);
    const std::uint32_t small = magic_code_bit(5);
    if (selected == big)
        current &= ~small;
    if (selected == small)
        current &= ~big;

    const std::uint32_t disable_bananas = magic_code_bit(12);
    const std::uint32_t banana_modifiers = magic_code_bit(7) | magic_code_bit(13) | magic_code_bit(14);
    if (selected == disable_bananas)
        current &= ~banana_modifiers;
    if ((selected & banana_modifiers) != 0U)
        current &= ~disable_bananas;

    const std::uint32_t disable_weapons = magic_code_bit(11);
    const std::uint32_t weapon_modifiers = magic_code_bit(15) | magic_code_bit(16) | magic_code_bit(17) |
                                           magic_code_bit(18) | magic_code_bit(19) | magic_code_bit(20);
    if (selected == disable_weapons)
        current &= ~weapon_modifiers;
    if ((selected & weapon_modifiers) != 0U)
        current &= ~disable_weapons;

    const std::uint32_t balloon_colours =
        magic_code_bit(15) | magic_code_bit(16) | magic_code_bit(17) | magic_code_bit(18) | magic_code_bit(19);
    if ((selected & balloon_colours) != 0U) {
        current &= ~(balloon_colours & ~selected);
    }
    return current;
}

constexpr std::uint32_t disable_magic_code(std::uint32_t current, std::uint8_t internal_index) {
    return (current & ~magic_code_bit(internal_index)) & kSelectableMagicCodeMask;
}

constexpr std::uint32_t normalise_magic_code_mask(std::uint32_t mask) {
    std::uint32_t result = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        const std::uint32_t bit = magic_code_bit(definition.internal_index);
        if ((mask & bit) != 0U) {
            result = enable_magic_code(result, definition.internal_index);
        }
    }
    return result;
}

constexpr bool magic_code_enabled(std::uint32_t mask, std::uint8_t internal_index) {
    return (mask & magic_code_bit(internal_index)) != 0U;
}

constexpr bool magic_code_manifest_needs_refresh(bool lobby_active, std::uint64_t cached_mask, std::uint32_t selected) {
    return !lobby_active && cached_mask != selected;
}

} // namespace dkr::runtime::magic_codes
