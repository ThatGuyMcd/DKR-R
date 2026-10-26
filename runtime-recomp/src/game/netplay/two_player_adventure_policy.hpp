#pragma once

#include "netplay_pacing_policy.hpp"

#include <cstdint>
#include <span>

namespace dkr::runtime::netplay {

inline constexpr int kDefaultRaceType = 0;
inline constexpr int kHubWorldRaceType = 5;
inline constexpr int kCutsceneRaceType1 = 6;
inline constexpr int kCutsceneRaceType2 = 7;
inline constexpr int kBossRaceType = 8;
inline constexpr int kChallengeRaceTypeMask = 0x40;
inline constexpr std::uint32_t kTwoPlayerAdventureRaceRacerCount = 6U;
inline constexpr std::uint32_t kTwoPlayerAdventureChallengeRacerCount = 4U;

// Retail ASSET_MISC_67 contains ten boss -> introduction pairs. Read the
// active ROM's table, not guessed map IDs shared across ROM revisions.
inline constexpr bool boss_introduction_redirect_matches(std::span<const std::uint8_t> pairs, std::uint32_t requested,
                                                         std::uint32_t resolved) {
    if (pairs.size() != 20U || requested == resolved)
        return false;
    for (std::size_t i = 0; i < pairs.size(); i += 2U) {
        if (pairs[i] == requested)
            return pairs[i + 1U] == resolved;
    }
    return false;
}

// level_load redirects hub requests into key-door cinematics before returning
// to that same hub. These legitimate scenes have zero racer objects. They
// still require a load/epoch handshake, but must resume on the frontend input
// timeline, not enable racer validation or masquerade as a playable race.
inline constexpr bool two_player_adventure_empty_cutscene_ready(bool online_running, bool assigned_ports_released,
                                                                bool adventure_selected, int requested_race_type,
                                                                int resolved_race_type, std::uint32_t active_players,
                                                                std::uint32_t racer_count,
                                                                std::uint8_t expected_players) {
    const bool cinematic = resolved_race_type == kCutsceneRaceType1 || resolved_race_type == kCutsceneRaceType2;
    const bool scripted_destination =
        requested_race_type == kHubWorldRaceType || requested_race_type == kBossRaceType ||
        requested_race_type == kCutsceneRaceType1 || requested_race_type == kCutsceneRaceType2;
    return online_running && assigned_ports_released && adventure_selected && expected_players == 2U &&
           active_players == 1U && racer_count == 0U && cinematic && scripted_destination;
}

// DKR's JOINTVENTURE mode uses one visible overworld racer and swaps the
// retail controller-ID table when Player 2 becomes the lead. Online play must
// keep its authored Player 1/Player 2 slots stable and let the retail mapping
// consume the appropriate slot; applying a second netplay remap would invert
// the ownership twice and make the shared hub uncontrollable.
enum class TwoPlayerAdventurePhase : std::uint8_t {
    GuidedMenus,
    SharedHubPlayer1,
    SharedHubPlayer2,
    TransitionBarrier,
    DualGameplay,
};

inline constexpr TwoPlayerAdventurePhase two_player_adventure_phase(bool online_running, bool assigned_ports_released,
                                                                    bool two_player_adventure, bool player_two_is_lead,
                                                                    FrameDebtPhase frame_debt_phase) {
    if (!online_running || frame_debt_phase == FrameDebtPhase::Inactive || !assigned_ports_released) {
        return TwoPlayerAdventurePhase::GuidedMenus;
    }
    if (frame_debt_phase_is_barrier(frame_debt_phase)) {
        return TwoPlayerAdventurePhase::TransitionBarrier;
    }
    if (frame_debt_phase == FrameDebtPhase::Gameplay) {
        return TwoPlayerAdventurePhase::DualGameplay;
    }
    if (two_player_adventure) {
        return player_two_is_lead ? TwoPlayerAdventurePhase::SharedHubPlayer2
                                  : TwoPlayerAdventurePhase::SharedHubPlayer1;
    }
    return TwoPlayerAdventurePhase::GuidedMenus;
}

inline constexpr bool two_player_adventure_accepts_assigned_input(TwoPlayerAdventurePhase phase) {
    return phase == TwoPlayerAdventurePhase::SharedHubPlayer1 || phase == TwoPlayerAdventurePhase::SharedHubPlayer2 ||
           phase == TwoPlayerAdventurePhase::DualGameplay;
}

// JOINTVENTURE deliberately exposes one overworld racer even though two
// network players remain assigned. Limit this exception to a resolved hub
// scene so it cannot conceal an incomplete race, boss, challenge or minigame.
inline constexpr bool two_player_adventure_shared_hub_topology_ready(
    bool online_running, bool assigned_ports_released, bool two_player_adventure, int requested_race_type,
    int resolved_race_type, std::uint32_t active_players, std::uint32_t racer_count, std::uint8_t expected_players) {
    const bool hub_scene = resolved_race_type == kHubWorldRaceType || resolved_race_type == kCutsceneRaceType1 ||
                           resolved_race_type == kCutsceneRaceType2;
    return online_running && assigned_ports_released && two_player_adventure &&
           requested_race_type == kHubWorldRaceType && hub_scene && expected_players == 2U && active_players == 1U &&
           racer_count >= 1U && racer_count <= 10U;
}

// JOINTVENTURE keeps the frontend's gNumberOfActivePlayers at one after the
// file-select handoff. Retail promotes that value internally when loading a
// normal race: get_active_player_count() exposes two humans and
// track_setup_racers() creates six racers (or four for a challenge). This is a
// distinct dual-control topology, not the shared-hub exception above. Admit
// only an exact requested/resolved race-type match and the retail racer count,
// so a partial load, boss introduction or unrelated cutscene cannot release
// the two gameplay input lanes.
inline constexpr bool two_player_adventure_dual_gameplay_topology_ready(
    bool online_running, bool assigned_ports_released, bool two_player_adventure, int requested_race_type,
    int resolved_race_type, std::uint32_t active_players, std::uint32_t racer_count, std::uint8_t expected_players) {
    if (!online_running || !assigned_ports_released || !two_player_adventure || expected_players != 2U ||
        active_players != 1U || requested_race_type < 0 || requested_race_type != resolved_race_type) {
        return false;
    }
    if (resolved_race_type == kDefaultRaceType) {
        return racer_count == kTwoPlayerAdventureRaceRacerCount;
    }
    return (resolved_race_type & kChallengeRaceTypeMask) != 0 && racer_count == kTwoPlayerAdventureChallengeRacerCount;
}

// Boss races intentionally remain a one-viewport scene in two-player
// Adventure: retail creates one human racer plus the boss, then uses its own
// controller-ID table to select which network player's stable input slot owns
// the human. A genuine boss request may first resolve to a retail introduction
// cutscene, whose temporary roster is scene-authored; the actual boss arena
// must still expose exactly the one-human/two-racer topology. The requested
// type prevents either exception from admitting Tracks mode or an unrelated
// Adventure cutscene.
inline constexpr bool two_player_adventure_boss_topology_ready(bool online_running, bool assigned_ports_released,
                                                               bool two_player_adventure, int requested_race_type,
                                                               int resolved_race_type, std::uint32_t active_players,
                                                               std::uint32_t racer_count, std::uint8_t expected_players,
                                                               bool verified_boss_introduction = false) {
    if (!online_running || !assigned_ports_released || !two_player_adventure || requested_race_type != kBossRaceType ||
        expected_players != 2U || active_players != 1U) {
        return false;
    }
    if (resolved_race_type == kBossRaceType) {
        return racer_count == 2U;
    }
    // Boss fly-ins are authored as HUBWORLD in retail. Admit only the exact
    // redirect from the ROM's boss table and its one-racer shared topology.
    if (resolved_race_type == kHubWorldRaceType) {
        return verified_boss_introduction && racer_count == 1U;
    }
    const bool boss_introduction = resolved_race_type == kCutsceneRaceType1 || resolved_race_type == kCutsceneRaceType2;
    return boss_introduction && racer_count >= 1U && racer_count <= 10U;
}

// On a brand-new save the first shared-hub load can finish before retail has
// latched is_in_two_player_adventure(). The synchronized JOINTVENTURE setting
// is already immutable at that point, so admit only the exact pre-latch hub
// topology. Requiring the hub race type prevents this bootstrap exception from
// masking an incomplete race, boss, challenge, or minigame roster.
inline constexpr bool two_player_adventure_bootstrap_hub_topology_ready(
    bool online_running, bool assigned_ports_released, bool jointventure_selected, int requested_race_type,
    int resolved_race_type, std::uint32_t active_players, std::uint32_t racer_count, std::uint8_t expected_players) {
    const bool hub_scene = resolved_race_type == kHubWorldRaceType || resolved_race_type == kCutsceneRaceType1 ||
                           resolved_race_type == kCutsceneRaceType2;
    return online_running && assigned_ports_released && jointventure_selected &&
           requested_race_type == kHubWorldRaceType && hub_scene && expected_players == 2U && active_players == 1U &&
           racer_count >= 1U && racer_count <= 10U;
}

} // namespace dkr::runtime::netplay
