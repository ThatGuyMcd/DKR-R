#pragma once
#include "online_mod_manifest.hpp"
#include "legacy_mod_stage.hpp"

namespace dkr::mods::online {
// Child-worker entry point. It reconstructs artifacts from patches against
// locally owned ROMs; it never trusts host-prepared executable/artifact bytes.
// All writes target a fresh private session tree, never the offline library.
void prepare_library(const Manifest& manifest,const std::filesystem::path& payload_directory,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& fresh_destination,const ProgressCallback& progress={});
}
