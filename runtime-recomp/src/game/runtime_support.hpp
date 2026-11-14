#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime::support {

struct SystemSummary {
    std::string operating_system;
    std::string cpu;
    std::string memory;
    std::string gpu;
    std::string boot_drive;
    std::string application_drive;
};

void configure(const std::filesystem::path& config_directory);

bool diagnostic_logging_enabled();
bool crash_dumps_enabled();
void set_diagnostic_logging_enabled(bool enabled);
void set_crash_dumps_enabled(bool enabled);

const std::filesystem::path& log_directory();
const std::filesystem::path& crash_dump_directory();
const std::filesystem::path& support_report_directory();

bool open_directory(const std::filesystem::path& directory, std::string& error);
bool export_report(const std::string& report, std::filesystem::path& output, std::string& error);

SystemSummary collect_system_summary();

} // namespace dkr::runtime::support
