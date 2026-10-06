#pragma once
#include "legacy_mod_format.hpp"
#include <functional>
#include <map>

namespace dkr::mods {
struct Progress {
    unsigned patch=0;
    unsigned count=0;
    std::string stage;
};
using ProgressCallback=std::function<bool(const Progress&)>;
// Produces a private, reviewable transaction. It never enables tracks, boots
// a patched ROM, writes a catalog/save, or replaces an existing directory.
// The caller owns the staging parent; only a fresh generated leaf is accepted.
void stage_import(const std::filesystem::path& source,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& fresh_destination,
    const ProgressCallback& progress={},
    const std::map<std::string,std::string>& source_labels={});
} // namespace dkr::mods
