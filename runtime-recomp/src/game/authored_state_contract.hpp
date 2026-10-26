#pragma once

#include "revision_addresses.hpp"

#include <array>
#include <cstdint>

namespace dkr::runtime::netplay::authored_contract {

// This is the single portable simulation contract used by both deterministic
// hashing and Player 1 checkpoints. Keep renderer scale, camera distance,
// animation frames, sound handles and every host/runtime pointer out of it.
inline std::array<std::uint32_t, 12> global_words() {
    return {
        revision_addresses::IsInRace,
        revision_addresses::TracksMode,
        revision_addresses::NumberOfActivePlayers,
        revision_addresses::CurrentRngSeed,
        revision_addresses::PreviousRngSeed,
        revision_addresses::RaceStartTimer,
        revision_addresses::RaceStartProgress,
        revision_addresses::LogicUpdateRate,
        revision_addresses::ActiveMagicCodes,
        revision_addresses::PathUpdateOff,
        revision_addresses::RaceFinishTriggered,
        revision_addresses::NumberOfFinishedRacers,
    };
}

inline std::array<std::uint32_t, 21> roster_bytes() {
    return {
        revision_addresses::NumberOfGameplayPlayers,    revision_addresses::PlayerIdMap + 0U,
        revision_addresses::PlayerIdMap + 1U,           revision_addresses::PlayerIdMap + 2U,
        revision_addresses::PlayerIdMap + 3U,           revision_addresses::PlayersCharacterArray + 0U,
        revision_addresses::PlayersCharacterArray + 1U, revision_addresses::PlayersCharacterArray + 2U,
        revision_addresses::PlayersCharacterArray + 3U, revision_addresses::PlayersCharacterArray + 4U,
        revision_addresses::PlayersCharacterArray + 5U, revision_addresses::PlayersCharacterArray + 6U,
        revision_addresses::PlayersCharacterArray + 7U, revision_addresses::CharacterIdSlots + 0U,
        revision_addresses::CharacterIdSlots + 1U,      revision_addresses::CharacterIdSlots + 2U,
        revision_addresses::CharacterIdSlots + 3U,      revision_addresses::CharacterIdSlots + 4U,
        revision_addresses::CharacterIdSlots + 5U,      revision_addresses::CharacterIdSlots + 6U,
        revision_addresses::CharacterIdSlots + 7U,
    };
}

inline constexpr std::array<std::uint16_t, 10> kObjectWords{
    0x0000U, 0x0004U, 0x000CU, 0x0010U, 0x0014U, 0x001CU, 0x0020U, 0x0024U, 0x0028U, 0x002CU,
};

// Pointer-free state shared by moving/scripted non-racer actors that originate
// in the level object map. The authoritative-state codec keys them by immutable
// map-bank and level-entry offset; their local emulated allocation is never
// transmitted. Camera distance, renderer pointers, transient projectiles,
// collision allocations, particles and sound handles are deliberately
// excluded. Scale is included because several gameplay actors use it for
// collection/collision state rather than presentation only.
inline constexpr std::array<std::uint16_t, 13> kActorObjectWords{
    0x0000U, 0x0004U, 0x0008U, 0x000CU, 0x0010U, 0x0014U, 0x001CU, 0x0020U, 0x0024U, 0x0028U, 0x002CU, 0x0034U, 0x0038U,
};

inline constexpr std::array<std::uint16_t, 1> kActorObjectHalves{
    0x0018U, // animFrame only; numActiveEmitters at 0x1A stays local.
};

inline constexpr std::array<std::uint16_t, 4> kLogBehaviorWords{
    0x0000U,
    0x0004U,
    0x0008U,
    0x000CU,
};

inline constexpr std::array<std::uint16_t, 3> kRacerIdentityWords{
    0x0000U,
    0x0004U,
    0x0008U,
};

inline constexpr std::array<std::uint16_t, 46> kRacerPhysicsWords{
    0x002CU, 0x0030U, 0x0034U, 0x0038U, 0x003CU, 0x0040U, 0x0044U, 0x0048U, 0x004CU, 0x0050U, 0x0054U, 0x0058U,
    0x005CU, 0x0060U, 0x0064U, 0x0068U, 0x006CU, 0x0070U, 0x009CU, 0x00A0U, 0x00A4U, 0x00A8U, 0x00ACU, 0x00B0U,
    0x00B4U, 0x00B8U, 0x00BCU, 0x00C0U, 0x00C4U, 0x00C8U, 0x00CCU, 0x00D0U, 0x00D4U, 0x00D8U, 0x00DCU, 0x00E0U,
    0x00E4U, 0x00E8U, 0x00ECU, 0x00F0U, 0x00F4U, 0x00F8U, 0x00FCU, 0x0100U, 0x0104U,
};

inline constexpr std::array<std::uint16_t, 3> kRacerPostPointerWords{
    0x010CU,
    0x0110U,
    0x0114U,
};

inline constexpr std::array<std::uint16_t, 9> kRacerLapWords{
    0x011CU, 0x0120U, 0x0124U, 0x0128U, 0x012CU, 0x0130U, 0x0134U, 0x0138U, 0x013CU,
};

inline constexpr std::array<std::uint16_t, 6> kRacerRotationWords{
    0x0160U, 0x0164U, 0x0168U, 0x016CU, 0x0170U, 0x0174U,
};

// Object_Racer becomes densely packed after 0x184. Do not hash/copy that
// whole tail in four-byte buckets: several buckets mix authored race state
// with camera, animation, transparency, HUD, lighting and delayed-audio
// bookkeeping. Those presentation fields legitimately differ while a peer is
// replaying rollback frames and must never terminate a deterministic race.
inline constexpr std::array<std::uint16_t, 21> kRacerRaceWords{
    0x0188U, 0x018CU, 0x0190U, 0x0198U, 0x019CU, 0x01A4U, 0x01A8U, 0x01ACU, 0x01B0U, 0x01B4U, 0x01B8U,
    0x01BCU, 0x01C0U, 0x01C4U, 0x01C8U, 0x01D4U, 0x01D8U, 0x01DCU, 0x01E0U, 0x0200U, 0x0204U,
};

inline constexpr std::array<std::uint16_t, 1> kRacerRaceHalves{
    0x01A2U, // y_rotation_vel; 0x1A0 is visual steering rotation.
};

inline constexpr std::array<std::uint16_t, 42> kRacerRaceBytes{
    0x0185U, 0x0186U, 0x0187U,                                     // bananas and attack state
    0x0194U, 0x0195U,                                              // lap/magnet state; 0x196 is camera yaw
    0x01CCU, 0x01CDU, 0x01CEU,                                     // AI state; 0x1CF is an HUD counter
    0x01D1U, 0x01D2U, 0x01D3U,                                     // boost state; 0x1D0 is spectator camera
    0x01E4U, 0x01E5U, 0x01E6U,                                     // contact/water/drift state
    0x01E8U, 0x01E9U, 0x01EAU, 0x01EBU, 0x01ECU, 0x01EDU, 0x01EEU, // input/squish; 0x1EF is boost audio
    0x01F0U, 0x01F1U, 0x01F2U, 0x01F3U, 0x01F4U, 0x01F5U, 0x01F6U, // start/zipper; 0x1F7 is transparency
    0x01FAU, 0x01FBU,                                              // drift state; 0x1F8/0x1F9 are HUD
    0x01FCU, 0x01FEU, 0x01FFU,                                     // wrong-way and adjacent authored state
    0x0208U, 0x0209U, 0x020BU,                                     // 0x20A is a presentation light flag
    0x020CU, 0x020DU,                                              // throttle release; 0x20E is delayed audio
    0x0214U, 0x0215U, 0x0216U, 0x0217U,
};

} // namespace dkr::runtime::netplay::authored_contract
