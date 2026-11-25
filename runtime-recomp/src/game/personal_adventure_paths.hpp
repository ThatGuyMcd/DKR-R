#pragma once

#include <filesystem>
#include <string_view>

namespace dkr::runtime::saves {
inline constexpr std::string_view kModdedAdventureFolder = "modded";
inline constexpr std::string_view kModdedAdventureDirectory = "saves/modded/";

inline std::filesystem::path personal_adventure_subfolder(bool modded) {
    return modded ? std::filesystem::path(kModdedAdventureFolder) : std::filesystem::path{};
}

inline std::filesystem::path personal_adventure_path(const std::filesystem::path& root, bool modded) {
    return root / "saves" / personal_adventure_subfolder(modded) / "dkr.us.v77.bin";
}
} // namespace dkr::runtime::saves
