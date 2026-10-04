#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "dkr_save_codec.hpp"

namespace dkr::runtime::saves {

enum class OnlineSaveSeedMode : std::uint8_t {
    CopySinglePlayer,
    Fresh,
    ContinuePreviousSession,
};

struct SaveInfo {
    std::filesystem::path path;
    bool exists = false;
    bool valid = false;
    std::uintmax_t size = 0;
};

constexpr int kControllerPakCount = 4;

void configure(const std::filesystem::path& config_directory);
SaveInfo adventure_info();
std::filesystem::path backup_directory();
std::vector<std::filesystem::path> adventure_backups();
bool backup_adventure(std::filesystem::path& created, std::string& error);
bool export_adventure(const std::filesystem::path& destination, std::string& error);
bool import_adventure(const std::filesystem::path& source, std::string& error);
bool reset_adventure(std::string& error);
bool load_adventure(codec::SaveImage& image, std::string& error);
bool commit_adventure(const codec::SaveImage& image, std::string& error);
bool repair_adventure_checksums(bool& changed,
                                std::filesystem::path& original_backup,
                                std::string& error);
bool canonical_adventure_bytes(std::vector<std::uint8_t>& bytes,
                               std::string& error);
SaveInfo previous_online_adventure_info();
// Stages validated bytes without touching previous online progress. The host's
// first read_online_adventure during game activation commits this seed.
bool prepare_host_online_adventure(
    OnlineSaveSeedMode mode, std::vector<std::uint8_t>& bytes,
    std::string& error);
bool install_synchronized_online_adventure(
    std::uint64_t match_id,
    std::span<const std::uint8_t> bytes,
    std::filesystem::path& installed_path,
    std::string& error);
bool bind_staged_host_online_adventure(std::uint64_t match_id,std::string& error);
void discard_staged_host_online_adventure();
bool read_online_adventure(bool host, std::uint64_t match_id,
                           std::vector<std::uint8_t>& bytes,
                           std::filesystem::path& path,
                           std::string& error);
std::filesystem::path online_adventure_subfolder(bool host,
                                                  std::uint64_t match_id);
// Confirmed-only owned backend persistence. Never resolves a SP path.
bool commit_online_adventure(bool host,std::uint64_t match_id,
                             std::span<const std::uint8_t> bytes,std::string& error);
bool read_experimental_online_paks(bool host,std::uint64_t match_id,std::vector<std::uint8_t>& images,std::string& error);
bool commit_experimental_online_paks(bool host,std::uint64_t match_id,std::span<const std::uint8_t> images,std::string& error);

SaveInfo controller_pak_info(int channel);
std::vector<std::filesystem::path> controller_pak_backups(int channel);
bool backup_controller_pak(int channel, std::filesystem::path& created,
                           std::string& error);
bool export_controller_pak(int channel,
                           const std::filesystem::path& destination,
                           std::string& error);
bool import_controller_pak(int channel, const std::filesystem::path& source,
                           std::string& error);

// A DKR-R bundle contains only fixed, typed save-image records. It has no
// filenames or extraction paths, so importing cannot traverse outside the
// configured save directory. Adventure EEPROM and all present Controller Paks
// are validated before any live file is replaced.
bool export_bundle(const std::filesystem::path& destination, std::string& error);
bool import_bundle(const std::filesystem::path& source, std::string& error);

} // namespace dkr::runtime::saves
