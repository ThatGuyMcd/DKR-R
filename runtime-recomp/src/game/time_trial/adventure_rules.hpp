#pragma once

#include "courses.hpp"

#include <array>
#include <string_view>

namespace dkr::runtime::time_trial {
enum class Adventure {
    None,
    AnyPercent,
    HundredPercent,
    AllTrophyRaces,
    AllBosses,
    AllMinigames,
    TTAmulet,
    AnyPercentGlitchless,
    TrophyDinoFirst,
    TrophyFutureFirst,
    CarPercent,
    HoverPercent,
    PlanePercent,
};

struct AdventureCategory {
    Adventure goal;
    std::string_view id;
    std::string_view title;
};

inline constexpr std::array kAdventureCategories{
    AdventureCategory{Adventure::AnyPercent, "any-percent", "Any%"},
    AdventureCategory{Adventure::HundredPercent, "100-percent", "100%"},
    AdventureCategory{Adventure::AllTrophyRaces, "all-trophy-races", "All Trophy Races"},
    AdventureCategory{Adventure::AllBosses, "all-bosses", "All Bosses"},
    AdventureCategory{Adventure::AllMinigames, "all-minigames", "All Minigames"},
    AdventureCategory{Adventure::TTAmulet, "tt-amulet-no-wrong-warp", "T.T. Amulet – No Wrong Warp"},
    AdventureCategory{Adventure::AnyPercentGlitchless, "any-percent-glitchless", "Any% Glitchless"},
    AdventureCategory{Adventure::TrophyDinoFirst, "all-trophy-races-dino-first",
                      "All Trophy Races - Dino Domain First"},
    AdventureCategory{Adventure::TrophyFutureFirst, "all-trophy-races-ffl-first",
                      "All Trophy Races - Future Fun Land First"},
    AdventureCategory{Adventure::CarPercent, "car-percent", "Car%"},
    AdventureCategory{Adventure::HoverPercent, "hover-percent", "Hover%"},
    AdventureCategory{Adventure::PlanePercent, "plane-percent", "Plane%"},
};

inline constexpr std::string_view kAdventureRules = "adventure-us80-7";
inline constexpr std::string_view kAdventureOriginalRouteRules = "adventure-us80-6";
inline constexpr std::string_view kAdventureMilestoneRules = "adventure-us80-5";
inline constexpr std::string_view kAdventureColdSaveRules = "adventure-us80-4";
inline constexpr std::string_view kAdventureVisualRngRules = "adventure-us80-3";
inline constexpr std::string_view kAdventureSharedRngRules = "adventure-us80-2";
inline constexpr std::string_view kLegacyAdventureRules = "adventure-us80-1";
inline constexpr unsigned kAdventureInterval = 1800;
inline constexpr unsigned kAdventureMaxUpdates = 30 * 60 * 60 * 6;

constexpr Adventure adventure_for(std::string_view id) {
    for (const auto& category : kAdventureCategories)
        if (category.id == id)
            return category.goal;
    return Adventure::None;
}

constexpr std::string_view adventure_id(Adventure goal) {
    for (const auto& category : kAdventureCategories)
        if (category.goal == goal)
            return category.id;
    return {};
}

constexpr bool tracks_route(Adventure goal, bool completed_save_rules = true) {
    return (goal >= Adventure::TrophyDinoFirst && goal <= Adventure::PlanePercent) ||
           (completed_save_rules && (goal == Adventure::AllTrophyRaces || goal == Adventure::AllMinigames));
}

constexpr bool completed_save_route(Adventure goal) {
    return tracks_route(goal) || goal == Adventure::AllBosses;
}

// The original game exposes earned characters through the same two bits it
// uses for cheat state. Match init_title_screen_variables' EEPROM conditions.
constexpr unsigned earned_character_flags(unsigned eeprom) {
    constexpr unsigned tt_trial_bits = 0xfffff0;
    return (eeprom & 2U) | unsigned((eeprom & tt_trial_bits) == tt_trial_bits);
}

constexpr bool trophy_route(Adventure goal) {
    return goal == Adventure::AllTrophyRaces || goal == Adventure::TrophyDinoFirst ||
           goal == Adventure::TrophyFutureFirst;
}

struct AdventureProgress {
    unsigned balloons = 0;
    unsigned keys = 0;
    unsigned bosses = 0;
    unsigned trophies = 0;
    unsigned tt_amulet = 0;
    unsigned wizpig_amulet = 0;
};

// The retail Adventure settings store the central-island and Future Fun
// Land Wizpig victories as separate bits. Gold is 3 in each world's two-bit
// trophy field. The four world keys occupy bits 1 through 4.
constexpr bool adventure_complete(Adventure goal, AdventureProgress progress, bool all_five_trophies = false) {
    constexpr unsigned final_wizpig = 1U << 5;
    constexpr unsigned four_gold_cups = 0xff;
    constexpr unsigned four_world_keys = 0x1e;
    const bool final_victory = (progress.bosses & final_wizpig) != 0;
    const bool gold_cups = (progress.trophies & four_gold_cups) == four_gold_cups;
    switch (goal) {
    case Adventure::AnyPercent:
        return final_victory;
    case Adventure::AnyPercentGlitchless:
        return final_victory && (progress.bosses & 0x3f) == 0x3f;
    case Adventure::HundredPercent:
        return final_victory && gold_cups && (progress.keys & four_world_keys) == four_world_keys &&
               progress.balloons == 47 && progress.tt_amulet == 4 && progress.wizpig_amulet == 4;
    case Adventure::AllTrophyRaces:
        return gold_cups && (!all_five_trophies || (progress.trophies & 0x300) == 0x300);
    case Adventure::AllBosses:
        return (progress.bosses & 0x3f) == 0x3f;
    case Adventure::AllMinigames:
    case Adventure::TTAmulet:
        return progress.tt_amulet == 4 && (progress.keys & four_world_keys) == four_world_keys;
    case Adventure::TrophyDinoFirst:
    case Adventure::TrophyFutureFirst:
    case Adventure::CarPercent:
    case Adventure::HoverPercent:
    case Adventure::PlanePercent:
    case Adventure::None:
        return false;
    }
    return false;
}
} // namespace dkr::runtime::time_trial
