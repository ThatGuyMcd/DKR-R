#include "achievements.hpp"
#include <stdexcept>
#include <set>
namespace dkr::runtime::achievements {
const std::vector<Definition>& catalogue() {
    static const auto result = [] {
        std::vector<Definition> out;
        auto add = [&](std::string id, std::string title, std::string description, std::string group, Kind kind,
                       int arg = 0, int target = 1, int vehicle = -1, bool mirrored = false, bool restorable = false) {
            out.push_back({std::move(id), std::move(title), std::move(description), std::move(group), kind, arg, target,
                           vehicle, mirrored, restorable});
        };
        add("first-balloon", "First Balloon", "Earn your first adventure balloon.", "Adventure", Kind::Balloons, 0, 1,
            -1, false, true);
        add("banana-ten", "Banana Bunch", "Hold 10 bananas at once during a race.", "Racing", Kind::Power, 5, 10);
        add("tt-amulet", "Four Challenges", "Complete all four adventure battle challenges.", "Adventure", Kind::Amulet,
            0, 4, -1, false, true);
        add("wizpig-amulet", "Ready for Wizpig", "Collect all four Wizpig amulet pieces.", "Adventure", Kind::Amulet, 1,
            4, -1, false, true);
        const char* key_names[]{"Ancient Lake", "Crescent Island", "Snowball Valley", "Boulder Canyon"};
        for (int i = 0; i < 4; ++i)
            add("key-" + std::to_string(i), std::string(key_names[i]) + " Key",
                "Find the hidden key in " + std::string(key_names[i]) + ".", "Adventure", Kind::Key, i, 1, -1, false,
                true);
        add("balloons-39", "Beyond the Island", "Collect 39 balloons in one adventure.", "Adventure", Kind::Balloons, 0,
            39, -1, false, true);
        add("balloons-47", "Across the Galaxy", "Collect all 47 balloons in one adventure.", "Adventure",
            Kind::Balloons, 0, 47, -1, false, true);
        const char* worlds[]{"Dino Domain", "Sherbet Island", "Snowflake Mountain", "Dragon Forest", "Future Fun Land"};
        for (int i = 0; i < 5; ++i)
            add("trophy-" + std::to_string(i), std::string(worlds[i]) + " Champion",
                "Win the original " + std::string(worlds[i]) + " trophy competition.", "Trophies", Kind::Trophy, i, 3,
                -1, false, true);
        add("wizpig-one", "Wizpig Defeated", "Win the first Wizpig race.", "Adventure", Kind::Boss, 1, 1, -1, false,
            true);
        add("wizpig-two", "One More for the Road", "Win the second Wizpig race.", "Adventure", Kind::Boss, 32, 1, -1,
            false, true);
        add("mirrored-wizpig", "Wizpig Strikes Twice", "Win the second Wizpig race in Adventure Two.", "Adventure Two",
            Kind::Boss, 32, 1, -1, true, true);
        add("mirrored-47", "Mirror Master", "Collect all 47 balloons in Adventure Two.", "Adventure Two",
            Kind::Balloons, 0, 47, -1, true, true);
        const char* vehicles[]{"car", "hovercraft", "plane"};
        for (const auto& c : courses) {
            const auto key = std::to_string(c.id);
            const std::string name = c.name;
            add("tt-" + key, name + ": T.T. Challenge",
                "Beat T.T.'s full-race time on " + name + " using the " + vehicles[c.vehicle] + " in Time Trial mode.",
                "T.T. Times", Kind::TimeTrial, c.id, c.tt_ticks, c.vehicle);
            add("rare-" + key, name + ": Developer Challenge",
                "Beat the developer's full-race time on " + name + " using the " + vehicles[c.vehicle] +
                    " in Time Trial mode.",
                "Developer Times", Kind::Developer, c.id, c.developer_centiseconds, c.vehicle);
            add("silver-" + key, name + ": Mirror Coins",
                "Collect all eight silver coins and finish first in the same Adventure Two race on " + name + ".",
                "Adventure Two", Kind::Silver, c.id, 8, -1, true, true);
        }
        const char* powers[]{"Boost", "Rocket", "Trap", "Shield", "Magnet"};
        for (int i = 0; i < 5; ++i)
            add("power-" + std::to_string(i), std::string(powers[i]) + " Expert",
                "Obtain the highest-level " + std::string(powers[i]) + " power-up.", "Racing", Kind::Power, i, 2);
        const char* drivers[]{"Krunch", "Bumper",    "Tiptup", "Conker", "Timber",
                              "Banjo",  "Drumstick", "Pipsy",  "T.T.",   "Diddy Kong"};
        for (int i = 0; i < 10; ++i)
            add("driver-" + std::to_string(i), std::string(drivers[i]) + ": Five More",
                "Earn five new balloons as " + std::string(drivers[i]) +
                    " without leaving Adventure mode, in one session.",
                "Characters", Kind::Driver, i, 5);
        std::set<std::string> ids;
        for (const auto& d : out)
            if (!ids.insert(d.id).second)
                throw std::logic_error("Duplicate achievement ID");
        if (out.size() != 94)
            throw std::logic_error("The built-in collection must contain all 94 achievements");
        return out;
    }();
    return result;
}
} // namespace dkr::runtime::achievements
