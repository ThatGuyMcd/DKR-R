#pragma once
#include "legacy_mod_stage.hpp"

namespace dkr::mods {
// Converts an immutable import review into independently verified course
// artifacts. This runs in ModWorker, never in a UI/network callback. It does
// not enable a course, alter a ROM, or write a save/catalogue.
void prepare_imported_tracks(const std::filesystem::path& review,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& fresh_destination,
    const ProgressCallback& progress={},const ImportPreparation* preparation=nullptr);
void prepare_imported_characters(const std::filesystem::path& review,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& fresh_destination,
    const ProgressCallback& progress={},const ImportPreparation* preparation=nullptr);
}
