#pragma once
#include <filesystem>
#include <cstdint>
struct recomp_context;
namespace dkr::runtime::legacy {
// Development build only; never enabled by a release package or mod archive.
void configure_qualification(const std::filesystem::path& rom, const std::filesystem::path& recipe);
void qualify_native_menu(std::uint8_t* rdram, recomp_context* context, unsigned event, const std::uint32_t* fields,
                         bool after);
} // namespace dkr::runtime::legacy
