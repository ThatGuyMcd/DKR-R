#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkr::runtime::saves {

struct RuntimeOnlineSaveStatus {
    bool active = false;
    bool verified = false;
    std::uint64_t hash = 0U;
    std::filesystem::path path;
};

void reset_runtime_online_save_status();
bool activate_online_save_for_runtime(bool host, std::uint64_t match_id,
                                      std::uint64_t expected_hash,
                                      std::string& error);
RuntimeOnlineSaveStatus runtime_online_save_status();
// Freeze ownership before N64ModernRuntime creates its save buffer/worker.
bool prepare_runtime_save_context(const std::filesystem::path& root,
    const std::filesystem::path& offline_subfolder,
    const std::filesystem::path& offline_paks, bool online, bool host,
    std::uint64_t match_id, std::uint64_t expected_hash, std::string& error);
void retire_runtime_save_context();
bool runtime_save_context_ready();
bool runtime_save_writes_allowed();
std::filesystem::path runtime_save_path();
std::filesystem::path runtime_pak_directory();
bool load_runtime_save(const std::filesystem::path&, std::vector<char>&);
bool commit_runtime_save(const std::filesystem::path&, const std::vector<char>&);
std::string runtime_save_failure();
struct RuntimeSaveSelection { bool online = false, host = false; std::uint64_t match = 0, hash = 0; };
RuntimeSaveSelection runtime_save_selection();

} // namespace dkr::runtime::saves
