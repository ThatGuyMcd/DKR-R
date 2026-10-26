#pragma once

#include <filesystem>
#include <string>

#include "rom_revision.hpp"

namespace dkr::runtime {

// Keep the runtime identity stable across compatible retail revisions. The
// EEPROM layout is unchanged, so both engines intentionally use the existing
// save, mod and configuration namespace instead of silently creating a second
// adventure file when the player selects Rev A.
inline constexpr char8_t kGameId[] = u8"dkr.us.v77";

bool RegisterGame(const std::filesystem::path& config_directory, rom::Revision revision, std::string& error);
bool SelectRom(const std::filesystem::path& rom_path, std::string& error);
bool ValidateRomForLauncher(const std::filesystem::path& rom_path, rom::Identity& identity, std::string& error);
} // namespace dkr::runtime
