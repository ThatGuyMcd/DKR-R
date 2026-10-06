#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime::portable {

// Resolve once before selecting a profile. AppImages use their outer, writable
// location, not the temporary read-only mount containing the ELF executable.
void configure(const char* executable_argument);
const std::filesystem::path& application_directory();
const std::filesystem::path& marker_path();
bool supported();
bool enabled();
bool enabled_at_startup();
bool set_enabled(bool value, std::string& error);

} // namespace dkr::runtime::portable
