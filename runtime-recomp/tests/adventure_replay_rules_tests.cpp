#include "../src/game/time_trial/adventure_rules.hpp"
#include "../src/game/time_trial/adventure_splits.hpp"
#include "../src/game/time_trial/session.hpp"
#include <cassert>

using namespace dkr::runtime::time_trial;

static_assert(adventure_for("any-percent") == Adventure::AnyPercent);
static_assert(adventure_for("100-percent") == Adventure::HundredPercent);
static_assert(adventure_for("all-trophy-races") == Adventure::AllTrophyRaces);
static_assert(adventure_for("unknown") == Adventure::None);
static_assert(!adventure_complete(Adventure::AnyPercent, {.bosses = 1}));
static_assert(adventure_complete(Adventure::AnyPercent, {.bosses = 0x20}));
static_assert(!adventure_complete(Adventure::AllTrophyRaces, {.trophies = 0x2ff & ~1U}));
static_assert(adventure_complete(Adventure::AllTrophyRaces, {.trophies = 0xff}));
static_assert(!adventure_complete(Adventure::HundredPercent, {47, 0x1e, 0x20, 0xff, 3, 4}));
static_assert(!adventure_complete(Adventure::HundredPercent, {47, 0x0e, 0x20, 0xff, 4, 4}));
static_assert(!adventure_complete(Adventure::HundredPercent, {46, 0x1e, 0x20, 0xff, 4, 4}));
static_assert(!adventure_complete(Adventure::HundredPercent, {47, 0x1e, 1, 0xff, 4, 4}));
static_assert(adventure_complete(Adventure::HundredPercent, {47, 0x1e, 0x20, 0xff, 4, 4}));
static_assert(!adventure_complete(Adventure::AllTrophyRaces, {.trophies = 0xff}, true));
static_assert(adventure_complete(Adventure::AllTrophyRaces, {.trophies = 0x3ff}, true));
static_assert(!adventure_complete(Adventure::AllBosses, {.bosses = 0x3e}));
static_assert(adventure_complete(Adventure::AllBosses, {.bosses = 0x3f}));
static_assert(!adventure_complete(Adventure::AllMinigames, {.keys = 0x1e, .tt_amulet = 3}));
static_assert(adventure_complete(Adventure::TTAmulet, {.keys = 0x1e, .tt_amulet = 4}));
static_assert(tracks_route(Adventure::AllMinigames) && tracks_route(Adventure::AllTrophyRaces));
static_assert(!tracks_route(Adventure::AllMinigames, false) && !tracks_route(Adventure::AllTrophyRaces, false));
static_assert(completed_save_route(Adventure::AllBosses) && !tracks_route(Adventure::AllBosses));
static_assert(!completed_save_route(Adventure::AnyPercent) && !completed_save_route(Adventure::HundredPercent));
static_assert(earned_character_flags(0) == 0);
static_assert(earned_character_flags(0x0ffff2) == 2);
static_assert(earned_character_flags(0xfffff2) == 3);

int main() {
    const FinishTargets podium{"Ancient Lake", 140, {100, 120, 160}};
    assert(podium.place(90) == 1 && podium.place(110) == 2 && podium.place(130) == 3);
    assert(podium.place(100) == 0 && podium.place(145) == 0);
    assert(FinishTargets{}.place(4000) == 1);
    assert((FinishTargets{"Ancient Lake", 0, {100, 120, 160}}.place(200) == 0));
    const std::array trophy_done{AdventureSplit{"Start", 2}, AdventureSplit{"Spacedust Alley", 600}};
    const auto future_first = AdventureSplits::upcoming(Adventure::TrophyFutureFirst, trophy_done);
    assert(future_first.front() == "Darkmoon Caverns" && future_first.back() == "Finish");
    const std::array mins_done{AdventureSplit{"Start", 2}, AdventureSplit{"Smokey Castle", 400}};
    const auto minigames = AdventureSplits::upcoming(Adventure::AllMinigames, mins_done);
    assert(minigames.size() == 4);
    assert(std::ranges::find(minigames, "Smokey Castle") == minigames.end());
    AdventureSplits timeline;
    timeline.mark("Overworld", 2);
    timeline.observe({}, {}, 64, 3, 0, 0, 800);
    timeline.observe({}, {}, -1, 0, 0, 0, 900);
    timeline.observe({}, {.tt_amulet = 1}, -1, 0, 0, 0, 1000);
    timeline.observe({.tt_amulet = 1}, {.trophies = 3, .tt_amulet = 1}, -1, 1, 0, 0, 1200);
    timeline.observe({.trophies = 3, .tt_amulet = 1}, {.trophies = 3, .tt_amulet = 1}, 5, 5, 0, 0, 1500);
    timeline.observe({.trophies = 3, .tt_amulet = 1}, {.trophies = 3, .tt_amulet = 1}, 5, 5, 0, 0, 1600);
    assert(timeline.entries.size() == 4);
    assert(timeline.entries[1].label == "Icicle Pyramid" && timeline.entries[1].clock == 1000);
    assert(timeline.entries[2].label == "Dino Trophy" && timeline.entries[2].clock == 1200);
    assert(timeline.entries[3].label == "Enter FFL" && timeline.entries[3].clock == 1500);

    AdventureSplits trophy;
    trophy.observe_race(Adventure::TrophyFutureFirst, {5, 0, 5, 0, true}, 2);
    assert(trophy.entries.empty());
    trophy.observe_race(Adventure::TrophyFutureFirst, {17, 2, 5, 0, true}, 4);
    assert(!trophy.wrong_first_trophy(Adventure::TrophyFutureFirst));
    assert(trophy.wrong_first_trophy(Adventure::TrophyDinoFirst));
    assert(trophy.entries[0].label == "Spacedust Alley" && trophy.entries[0].clock == 4);
    constexpr unsigned rounds[5][4] = {
        {5, 3, 29, 7}, {8, 4, 10, 30}, {13, 6, 9, 28}, {19, 18, 20, 31}, {17, 32, 33, 15},
    };
    for (unsigned world = 0; world < 5; ++world) {
        for (unsigned round = 0; round < 4; ++round)
            trophy.observe_race(Adventure::TrophyFutureFirst, {rounds[world][round], 0, world + 1, round, false}, 6);
    }
    assert(trophy.entries.size() == 20);
    assert(trophy.route_complete(Adventure::TrophyFutureFirst, {}));
    assert(!trophy.route_complete(Adventure::TrophyDinoFirst, {}));
    assert(!trophy.route_complete(Adventure::AllTrophyRaces, {.trophies = 0xff}));
    assert(trophy.route_complete(Adventure::AllTrophyRaces, {.trophies = 0x3ff}));
    assert(trophy.route_complete(Adventure::AllTrophyRaces, {}, true));
    const AdventureProgress completed{47, 0x1e, 0xfff, 0x3ff, 4, 4};
    assert(!AdventureSplits{}.route_complete(Adventure::AllTrophyRaces, completed, true));
    assert(!AdventureSplits{}.route_complete(Adventure::AllBosses, completed, true));
    assert(!AdventureSplits{}.route_complete(Adventure::AllMinigames, completed, true));

    AdventureSplits battles;
    for (unsigned level : {25U, 26U, 11U, 11U, 5U})
        battles.observe_event_win(Adventure::AllMinigames, level, 8);
    assert(!battles.route_complete(Adventure::AllMinigames, completed, true));
    battles.observe_event_win(Adventure::AllMinigames, 27, 10);
    assert(battles.route_complete(Adventure::AllMinigames, {}, true) && battles.entries.size() == 4);

    AdventureSplits boss_rematches;
    for (unsigned level : {37U, 46U, 52U, 53U, 54U, 38U})
        boss_rematches.observe_event_win(Adventure::AllBosses, level, 8);
    assert(!boss_rematches.route_complete(Adventure::AllBosses, completed, true));
    boss_rematches.observe_event_win(Adventure::AllBosses, 55, 10);
    assert(boss_rematches.route_complete(Adventure::AllBosses, completed, true) && boss_rematches.entries.size() == 6);
    assert(AdventureSplits{}.route_complete(Adventure::HundredPercent, completed, true));
    assert(!AdventureSplits{}.route_complete(Adventure::HundredPercent, {47, 0x1e, 0xfff, 0xff, 4, 4}, true));

    AdventureSplits vehicle;
    for (const auto& course : kCourses) {
        if (course.id != "ancient-lake")
            vehicle.observe_race(Adventure::PlanePercent, {course.level, 2, 0, 0, true}, 10);
    }
    vehicle.observe_race(Adventure::PlanePercent, {5, 0, 0, 0, true}, 12);
    vehicle.observe_race(Adventure::PlanePercent, {5, 2, 0, 0, false}, 14);
    assert(!vehicle.route_complete(Adventure::PlanePercent, {}));
    vehicle.observe_race(Adventure::PlanePercent, {5, 2, 0, 0, true}, 16);
    assert(vehicle.route_complete(Adventure::PlanePercent, {}));
    assert(vehicle.entries.size() == 11);
}
