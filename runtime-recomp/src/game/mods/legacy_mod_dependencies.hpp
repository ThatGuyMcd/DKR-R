#pragma once
#include "legacy_mod_format.hpp"
#include <map>
#include <set>

namespace dkr::mods {
struct DependencyRecord {
    unsigned section = 0, id = 0;
    std::string sha256;
    bool changed = false;
};
struct DependencyReport {
    std::vector<DependencyRecord> records;
    std::vector<std::string> blockers;
    std::set<unsigned> behaviours;
    unsigned placed_objects = 0;
    // Static references are evidence for the runtime adapter, not proof that
    // behaviour-driven spawns/audio/particle dependencies are all covered.
    bool runtime_certified = false;
};
// Reads immutable source/target images. Never changes a game table or cache.
DependencyReport inspect_track_dependencies(const AssetImage& base, const AssetImage& target, const Root& root);
std::vector<unsigned> inspect_sprite_textures(View record);
std::vector<unsigned> inspect_model_textures(View compressed_record);
void validate_texture_record(View record);
} // namespace dkr::mods
