#pragma once

#include <array>
#include <string_view>

namespace dkr::runtime::time_trial {
enum class Vehicle : unsigned { Car, Hovercraft, Plane };

inline constexpr std::array<std::string_view, 3> kVehicleIds{"car", "hovercraft", "plane"};
inline constexpr std::array<std::string_view, 3> kVehicleNames{"Car", "Hovercraft", "Plane"};
inline constexpr std::string_view kLegacyRules = "ancient-lake-us80-practice-2";
inline constexpr std::string_view kVanillaRules = "vanilla-time-trial-us80-1";
inline constexpr std::string_view kCustomRules = "custom-time-trial-us80-1";
inline constexpr std::string_view kCustomVariableLapsRules = "custom-time-trial-us80-2";
inline constexpr unsigned kRetailLapCount = 3;
inline constexpr unsigned kMaximumLapCount = 5;

struct Course {
    std::string_view id, name;
    unsigned level, vehicles;

    constexpr bool supports(Vehicle vehicle) const {
        const auto index = static_cast<unsigned>(vehicle);
        return index < kVehicleIds.size() && (vehicles & (1U << index)) != 0;
    }
};

// Retail Time Trial level IDs and the vehicles with save-record slots.
inline constexpr std::array kCourses{
    Course{"ancient-lake", "Ancient Lake", 5, 7},
    Course{"fossil-canyon", "Fossil Canyon", 3, 7},
    Course{"jungle-falls", "Jungle Falls", 29, 7},
    Course{"hot-top-volcano", "Hot Top Volcano", 7, 6},
    Course{"everfrost-peak", "Everfrost Peak", 13, 7},
    Course{"walrus-cove", "Walrus Cove", 6, 3},
    Course{"snowball-valley", "Snowball Valley", 9, 3},
    Course{"frosty-village", "Frosty Village", 28, 7},
    Course{"whale-bay", "Whale Bay", 8, 2},
    Course{"crescent-island", "Crescent Island", 10, 3},
    Course{"pirate-lagoon", "Pirate Lagoon", 4, 2},
    Course{"treasure-caves", "Treasure Caves", 30, 7},
    Course{"windmill-plains", "Windmill Plains", 20, 7},
    Course{"greenwood-village", "Greenwood Village", 18, 3},
    Course{"boulder-canyon", "Boulder Canyon", 19, 2},
    Course{"haunted-woods", "Haunted Woods", 31, 3},
    Course{"spacedust-alley", "Spacedust Alley", 17, 7},
    Course{"darkmoon-caverns", "Darkmoon Caverns", 32, 3},
    Course{"spaceport-alpha", "Spaceport Alpha", 15, 7},
    Course{"star-city", "Star City", 33, 7},
};

constexpr const Course* course_for(std::string_view id) {
    for (const auto& course : kCourses)
        if (course.id == id)
            return &course;
    return nullptr;
}
} // namespace dkr::runtime::time_trial
