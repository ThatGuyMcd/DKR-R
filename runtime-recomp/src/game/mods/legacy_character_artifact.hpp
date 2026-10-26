#pragma once
#include "legacy_character_materialize.hpp"

namespace dkr::mods {
inline constexpr unsigned CharacterAdapterVersion = 2;
std::string write_character_artifact(const std::filesystem::path&, const PreparedCharacter&, const AssetBank&);
PreparedCharacter read_character_artifact(const std::filesystem::path&, const std::string& expected, const AssetBank&);
} // namespace dkr::mods
