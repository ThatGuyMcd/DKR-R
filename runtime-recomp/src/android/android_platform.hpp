#pragma once
#include <string>

namespace dkr::runtime::android {
void export_diagnostics();
void performance_tools();
const std::string& build_identity();
void report_graphics_failure(const char* message);
void report_task_stall(const char* message);
}
