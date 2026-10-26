#pragma once
#include "magic_code_package.hpp"

namespace dkr::mods::code::trophy {
inline constexpr unsigned DefinitionSchema = 1, MaxRounds = 128, MaxLaps = 99, AutomaticVehicle = 255;
struct Round {
    std::string course;
    unsigned laps = 3, vehicle = AutomaticVehicle;
    bool operator==(const Round&) const = default;
};
struct Definition {
    std::string id, name;
    bool shuffle = false;
    std::vector<Round> rounds;
    bool operator==(const Definition&) const = default;
};
struct Course {
    std::string id, name, content;
    unsigned carrier = 0, vehicles = 0, default_vehicle = 0;
    bool race = false;
    bool operator==(const Course&) const = default;
};
struct ResolvedRound {
    Course course;
    unsigned laps = 3, vehicle = 0, source = 0;
};
struct Frozen {
    Definition definition;
    std::uint64_t seed = 0;
    std::vector<ResolvedRound> rounds;
    std::string identity;
};
Definition decode(View);
Bytes encode(const Definition&);
// Supply ONLY the admitted game's immutable catalogue. Filenames, display
// names and current Track Select row indices are never course identities.
Frozen freeze(const Definition&, const std::vector<Course>&, std::uint64_t seed);
Bytes encode(const Frozen&);
Frozen verify(View, const std::vector<Course>&);
} // namespace dkr::mods::code::trophy
