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
// Native worker-only, single-patch scratch state. This is built from verified
// locally owned ROM bytes, never deserialized or supplied by the host.
// Review/course/character stages can share one reconstruction and analysis.
struct ImportPreparation {
    std::map<std::string,Bytes> bases;
    Bytes target;
    Analysis analysis;
};
ImportPreparation load_import_sources(const std::vector<std::filesystem::path>& roms);
void reconstruct_import(ImportPreparation& preparation,View patch);
// Produces a private, reviewable transaction. It never enables tracks, boots
// a patched ROM, writes a catalog/save, or replaces an existing directory.
// The caller owns the staging parent; only a fresh generated leaf is accepted.
void stage_import(const std::filesystem::path& source,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& fresh_destination,
    const ProgressCallback& progress={},
    const std::map<std::string,std::string>& source_labels={},
    const ImportPreparation* preparation=nullptr);
} // namespace dkr::mods
