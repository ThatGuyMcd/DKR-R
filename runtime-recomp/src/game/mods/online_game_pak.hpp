#pragma once
#include "../rom_revision.hpp"
#include "legacy_mod_format.hpp"
#include <stop_token>

namespace dkr::mods::online {
// ROM cache/network IDs (dkr.us.v*) and native asset IDs (us.v*) are
// deliberately different contracts. Convert the verified enum, not arbitrary
// text, at the launcher boundary; never change existing ROM/cache identities.
constexpr std::string_view asset_revision(dkr::runtime::rom::Revision revision) {
    using dkr::runtime::rom::Revision;
    switch(revision) {
    case Revision::UsV77: return "us.v77";
    case Revision::UsV80: return "us.v80";
    default: return {};
    }
}
constexpr bool matches_game_pak(const dkr::runtime::rom::Identity& identity,
    std::string_view revision) {
    return identity.error==dkr::runtime::rom::InspectionError::None &&
        !asset_revision(identity.revision).empty() && asset_revision(identity.revision)==revision;
}
// Shared by the launcher callback and headless admission checks. Selection
// must use the verified game revision, not a cache ID or whichever ROM is
// encountered first. Cancellation never constructs a runtime namespace.
template<class Inspect, class Prepare>
auto with_matching_game_pak(const std::vector<std::filesystem::path>& roms,
    std::string_view revision,std::stop_token stop,Inspect inspect,Prepare prepare) {
    for(const auto& rom:roms) {
        if(stop.stop_requested())throw Error("Online mod preparation cancelled.");
        if(matches_game_pak(inspect(rom),revision))return prepare(rom);
    }
    throw Error("Import the host's original Game Pak revision first. DKR-R never downloads ROMs.");
}
}
