#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
namespace dkr::runtime::netplay::experimental {
// Application-supplied executables, exact argv, no shell. The blocking form
// belongs on a preparation worker and kills/reaps ONLY its own child on cancel.
bool run_child(const std::filesystem::path& executable, std::span<const std::string> arguments,
    const std::function<bool()>& keep_running, std::chrono::seconds timeout, std::string& error);
bool launch_child(const std::filesystem::path& executable, std::span<const std::string> arguments,
    std::string& error);
}
