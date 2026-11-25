#pragma once

#include "adventure_rules.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace dkr::runtime::time_trial {
struct AdventureSplit {
    std::string_view label;
    unsigned clock;
};

struct AdventureRace {
    unsigned level, vehicle, trophy_world, trophy_round;
    bool won;
};

class AdventureSplits {
    unsigned challenge_world_ = 0;
    unsigned course_wins_ = 0;
    unsigned first_trophy_world_ = 0;
    unsigned event_wins_ = 0;
    std::array<unsigned, 5> trophy_rounds_{};

    static constexpr std::array<unsigned, 4> minigame_maps{11, 25, 26, 27};
    static constexpr std::array<unsigned, 6> boss_maps{37, 46, 52, 53, 54, 55};
    static constexpr std::array<std::string_view, 4> minigame_names{"Fire Mountain", "Smokey Castle", "Darkwater Beach",
                                                                    "Icicle Pyramid"};
    static constexpr std::array<std::string_view, 6> boss_names{"Wizpig 1", "Tricky", "Bluey",
                                                                "Bubbler",  "Smokey", "Wizpig 2"};
    static constexpr unsigned trophy_courses[5][4] = {
        {5, 3, 29, 7}, {8, 4, 10, 30}, {13, 6, 9, 28}, {19, 18, 20, 31}, {17, 32, 33, 15},
    };

    static constexpr Vehicle route_vehicle(Adventure goal) {
        return goal == Adventure::HoverPercent   ? Vehicle::Hovercraft
               : goal == Adventure::PlanePercent ? Vehicle::Plane
                                                 : Vehicle::Car;
    }

  public:
    std::vector<AdventureSplit> entries;

    static std::vector<std::string_view> upcoming(Adventure goal, std::span<const AdventureSplit> completed) {
        std::vector<std::string_view> route{tracks_route(goal) ? "Start" : "Overworld"};
        const auto append = [&](std::initializer_list<std::string_view> stages) { route.insert(route.end(), stages); };
        if (trophy_route(goal)) {
            for (unsigned set = 0; set < 5; ++set) {
                const unsigned world = goal == Adventure::TrophyFutureFirst ? 4 - set : set;
                for (unsigned level : trophy_courses[world])
                    if (const auto course = std::ranges::find(kCourses, level, &Course::level);
                        course != kCourses.end())
                        route.push_back(course->name);
            }
        } else if (goal >= Adventure::CarPercent) {
            for (const auto& course : kCourses)
                if (course.supports(route_vehicle(goal)))
                    route.push_back(course.name);
        } else if (goal == Adventure::AllMinigames) {
            route.insert(route.end(), minigame_names.begin(), minigame_names.end());
        } else if (goal == Adventure::AllBosses) {
            append({"Tricky", "Bluey", "Bubbler", "Smokey", "Wizpig 1", "Wizpig 2"});
        } else if (goal == Adventure::TTAmulet) {
            append({"Fire Mountain", "Car Challenge", "Icicle Pyramid", "Hover Challenge", "Darkwater Beach",
                    "Plane Challenge", "Smokey Castle"});
        } else if (goal == Adventure::AnyPercent) {
            append({"Enter Wizpig 1", "Wizpig 1", "Dino 2", "Enter FFL", "Enter Wizpig 2", "Wizpig 2"});
        } else {
            append({"Dino 1",          "Fire Mountain",     "Dino 2",
                    "Dino Trophy",     "Car Challenge",     "Snowflake 1",
                    "Icicle Pyramid",  "Snowflake 2",       "Snow Trophy",
                    "Hover Challenge", "Sherbet 1",         "Darkwater Beach",
                    "Sherbet 2",       "Sherbet Trophy",    "Plane Challenge",
                    "Dragon 1",        "Smokey Castle",     "Dragon 2",
                    "Dragon Trophy",   "Enter Wizpig 1",    "Wizpig 1",
                    "Enter FFL",       "Future Fun Land 1", "Future Fun Land 2"});
            if (goal == Adventure::HundredPercent)
                route.push_back("Future Fun Land Trophy");
            append({"Enter Wizpig 2", "Wizpig 2"});
        }
        route.push_back("Finish");
        std::erase_if(route, [&](auto stage) {
            return std::ranges::find(completed, stage, &AdventureSplit::label) != completed.end();
        });
        return route;
    }

    void mark(std::string_view label, unsigned clock) {
        if (std::ranges::find(entries, label, &AdventureSplit::label) == entries.end())
            entries.push_back({label, clock});
    }

    void observe_race(Adventure goal, const AdventureRace& race, unsigned clock) {
        const auto course = std::ranges::find(kCourses, race.level, &Course::level);
        if (course == kCourses.end())
            return;
        if (trophy_route(goal)) {
            if (!race.trophy_world || race.trophy_world > trophy_rounds_.size() || race.trophy_round >= 4)
                return;
            if (trophy_courses[race.trophy_world - 1][race.trophy_round] != race.level)
                return;
            auto& rounds = trophy_rounds_[race.trophy_world - 1];
            if ((rounds & (1U << race.trophy_round)) == 0) {
                if (!first_trophy_world_)
                    first_trophy_world_ = race.trophy_world;
                rounds |= 1U << race.trophy_round;
                mark(course->name, clock);
            }
        } else if (goal >= Adventure::CarPercent && !race.trophy_world && race.won &&
                   race.vehicle == static_cast<unsigned>(route_vehicle(goal)) &&
                   course->supports(route_vehicle(goal))) {
            course_wins_ |= 1U << (course - kCourses.begin());
            mark(course->name, clock);
        }
    }

    bool wrong_first_trophy(Adventure goal) const {
        return first_trophy_world_ && ((goal == Adventure::TrophyDinoFirst && first_trophy_world_ != 1) ||
                                       (goal == Adventure::TrophyFutureFirst && first_trophy_world_ != 5));
    }

    void observe_event_win(Adventure goal, unsigned level, unsigned clock) {
        const std::span<const unsigned> maps = goal == Adventure::AllMinigames
                                                   ? std::span<const unsigned>{minigame_maps}
                                                   : std::span<const unsigned>{boss_maps};
        const std::span<const std::string_view> names = goal == Adventure::AllMinigames
                                                            ? std::span<const std::string_view>{minigame_names}
                                                            : std::span<const std::string_view>{boss_names};
        const auto found = std::ranges::find(maps, level);
        if (found != maps.end()) {
            const auto index = found - maps.begin();
            event_wins_ |= 1U << index;
            mark(names[index], clock);
        }
    }

    bool route_complete(Adventure goal, AdventureProgress progress, bool completed_save_rules = false) const {
        if (trophy_route(goal)) {
            const bool raced_every_round =
                std::ranges::all_of(trophy_rounds_, [](unsigned rounds) { return rounds == 0xf; });
            return raced_every_round && !wrong_first_trophy(goal) &&
                   (completed_save_rules || goal != Adventure::AllTrophyRaces ||
                    adventure_complete(goal, progress, true));
        }
        if (completed_save_rules && goal == Adventure::AllMinigames)
            return event_wins_ == (1U << minigame_maps.size()) - 1;
        if (completed_save_rules && goal == Adventure::AllBosses)
            return event_wins_ == (1U << boss_maps.size()) - 1;
        if (completed_save_rules && goal == Adventure::HundredPercent &&
            !adventure_complete(Adventure::AllTrophyRaces, progress, true))
            return false;
        if (goal >= Adventure::CarPercent) {
            for (unsigned index = 0; index < kCourses.size(); ++index) {
                if (kCourses[index].supports(route_vehicle(goal)) && !(course_wins_ & (1U << index)))
                    return false;
            }
            return true;
        }
        return adventure_complete(goal, progress, true);
    }

    void observe_future_fun_land(std::span<const std::uint32_t> flags, int race_type, unsigned clock) {
        constexpr std::array future_courses{15U, 17U, 32U, 33U};
        const auto cleared = [&](unsigned flag) {
            return std::ranges::all_of(future_courses, [&](unsigned course) { return flags[course] & flag; });
        };
        if (flags.size() > future_courses.back()) {
            if (cleared(2))
                mark("Future Fun Land 1", clock);
            if (cleared(4))
                mark("Future Fun Land 2", clock);
        }
        if (race_type == 8)
            mark("Enter Wizpig 2", clock);
    }

    // Use the cartridge's persistent objective bits for clears and its live
    // gameplay mode for entries. Menu transitions and deaths cannot split.
    void observe(AdventureProgress before, AdventureProgress now, int race_type, unsigned world, unsigned previous_taj,
                 unsigned taj, unsigned clock, Adventure goal = Adventure::None) {
        static constexpr std::array bosses{
            "Wizpig 1", "Dino 1", "Sherbet 1", "Snowflake 1", "Dragon 1", "Wizpig 2",
            "",         "Dino 2", "Sherbet 2", "Snowflake 2", "Dragon 2",
        };
        static constexpr std::array cups{
            "Dino Trophy", "Sherbet Trophy", "Snow Trophy", "Dragon Trophy", "Future Fun Land Trophy",
        };
        static constexpr std::array boss_names{
            "Wizpig 1", "Tricky", "Bubbler", "Bluey", "Smokey", "Wizpig 2",
        };
        static constexpr std::array challenges{
            "Fire Mountain",
            "Darkwater Beach",
            "Icicle Pyramid",
            "Smokey Castle",
        };
        static constexpr std::array taj_challenges{"Car Challenge", "Hover Challenge", "Plane Challenge"};
        if (race_type == 8 && world == 0)
            mark("Enter Wizpig 1", clock);
        if (race_type == 5 && world == 5)
            mark("Enter FFL", clock);
        if (race_type >= 64 && race_type <= 66 && world >= 1 && world <= challenges.size())
            challenge_world_ = world;
        if (now.tt_amulet > before.tt_amulet && challenge_world_)
            mark(challenges[challenge_world_ - 1], clock);
        for (unsigned i = 0; i < bosses.size(); ++i) {
            if (*bosses[i] && (now.bosses & ~before.bosses & (1U << i))) {
                if (goal == Adventure::AllBosses && i < boss_names.size())
                    mark(boss_names[i], clock);
                else
                    mark(bosses[i], clock);
            }
        }
        for (unsigned i = 0; i < cups.size(); ++i) {
            const unsigned shift = 2 * i;
            if (((now.trophies >> shift) & 3) == 3 && ((before.trophies >> shift) & 3) != 3)
                mark(cups[i], clock);
        }
        for (unsigned i = 0; i < taj_challenges.size(); ++i)
            if (taj & ~previous_taj & (8U << i))
                mark(taj_challenges[i], clock);
    }
};
} // namespace dkr::runtime::time_trial
