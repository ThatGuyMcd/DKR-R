#include "save_manager.hpp"
#include "netplay/experimental_pak.hpp"
#include "dkr_save_codec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

constexpr std::size_t kAdventureSaveSize = 0x200U;
constexpr std::size_t kAdventureSlotSize = 0x28U;
constexpr std::size_t kAdventureConfigOffset = 0x78U;
constexpr std::size_t kFastestLapsOffset = 0x80U;
constexpr std::size_t kCourseTimesOffset = 0x140U;
constexpr std::size_t kControllerPakSize = 32U * 1024U;
constexpr std::size_t kPakDirectoryOffset = 256U;
constexpr std::size_t kPakDirectoryEntrySize = 64U;
constexpr std::size_t kPakMaximumFiles = 16U;
constexpr std::size_t kPakDataStart = 5U * 256U;
constexpr std::array<std::uint8_t, 8> kPakMagic{
    'D', 'K', 'R', 'M', 'P', 'K', '1', 0};
constexpr std::array<std::uint8_t, 16> kBundleMagic{
    'D', 'K', 'R', 'P', 'O', 'R', 'T', 'S', 'A', 'V', 'E', '1', 0, 0, 0, 0};
constexpr std::uint32_t kBundleVersion = 1U;
constexpr std::uint32_t kBundleAdventureKind = 1U;
constexpr std::uint32_t kBundlePakKind = 2U;
constexpr std::size_t kMaximumBundleSize =
    64U + kAdventureSaveSize +
    dkr::runtime::saves::kControllerPakCount * (32U + kControllerPakSize);
std::filesystem::path g_config_directory;
std::mutex g_save_manager_mutex;
std::vector<std::uint8_t> g_staged_host_online_save;
std::uint64_t g_staged_host_online_match_id=0;

std::filesystem::path AdventurePath() {
    return g_config_directory / "saves" / "dkr.us.v77.bin";
}

std::string MatchIdText(std::uint64_t match_id) {
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << match_id;
    return output.str();
}

std::filesystem::path OnlineAdventureSubfolder(bool host,
                                                std::uint64_t match_id) {
    if (host) return std::filesystem::path("online") / "host";
    return std::filesystem::path("online") / "sessions" /
           MatchIdText(match_id);
}

std::filesystem::path OnlineAdventurePath(bool host,
                                          std::uint64_t match_id) {
    return g_config_directory / "saves" /
           OnlineAdventureSubfolder(host, match_id) / "dkr.us.v77.bin";
}

std::filesystem::path ControllerPakPath(int channel) {
    return g_config_directory /
        ("controller-pak-" + std::to_string(channel + 1) + ".mpk");
}

bool ValidChannel(int channel) {
    return channel >= 0 && channel < dkr::runtime::saves::kControllerPakCount;
}

std::uint32_t ReadLE32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset + 0U]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void AppendLE32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::uint32_t ImageChecksum(const std::vector<std::uint8_t>& bytes,
                            std::size_t clear_offset = SIZE_MAX) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t value = index >= clear_offset &&
                index < clear_offset + 4U
            ? 0U : bytes[index];
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

bool ReadFileBounded(const std::filesystem::path& path,
                     std::size_t maximum_size,
                     std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_size) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return !input.bad() && bytes.size() == size;
}

bool ReadAdventure(const std::filesystem::path& path,
                   std::vector<std::uint8_t>& bytes) {
    return ReadFileBounded(path, kAdventureSaveSize, bytes) &&
           dkr::runtime::saves::codec::validate(bytes);
}

bool ValidControllerPak(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kControllerPakSize ||
        !std::equal(kPakMagic.begin(), kPakMagic.end(), bytes.begin()) ||
        ReadLE32(bytes, 8U) != 1U ||
        ReadLE32(bytes, 16U) != ImageChecksum(bytes, 16U)) {
        return false;
    }
    std::size_t total_reserved = 0U;
    for (std::size_t index = 0; index < kPakMaximumFiles; ++index) {
        const std::size_t entry =
            kPakDirectoryOffset + index * kPakDirectoryEntrySize;
        if (ReadLE32(bytes, entry) == 0U) {
            continue;
        }
        const std::size_t size = ReadLE32(bytes, entry + 12U);
        const std::size_t offset = ReadLE32(bytes, entry + 16U);
        const std::size_t reserved = (size + 255U) & ~255U;
        if (size == 0U || offset < kPakDataStart ||
            offset > bytes.size() || size > bytes.size() - offset ||
            reserved > bytes.size() - kPakDataStart - total_reserved) {
            return false;
        }
        total_reserved += reserved;
    }
    return true;
}

bool ReadControllerPak(const std::filesystem::path& path,
                       std::vector<std::uint8_t>& bytes) {
    return ReadFileBounded(path, kControllerPakSize, bytes) &&
           ValidControllerPak(bytes);
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S")
           << '-' << std::setw(3) << std::setfill('0') << milliseconds.count();
    return output.str();
}

using ImageValidator = bool (*)(const std::filesystem::path&,
                                std::vector<std::uint8_t>&);

bool ReplaceFileAtomic(const std::filesystem::path& temporary,
                       const std::filesystem::path& destination,
                       std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool WriteAtomic(const std::filesystem::path& destination,
                 const std::vector<std::uint8_t>& bytes,
                 ImageValidator validator, std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the save folder: " + filesystem_error.message();
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".importing";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create the temporary save file.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "The temporary save file could not be written completely.";
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    std::vector<std::uint8_t> check;
    if (!validator(temporary, check)) {
        error = "The temporary save failed validation.";
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    const std::filesystem::path rollback = destination.string() + ".rollback";
    const bool had_destination = std::filesystem::exists(destination, filesystem_error);
    filesystem_error.clear();
    if (had_destination) {
        std::filesystem::copy_file(destination, rollback,
            std::filesystem::copy_options::overwrite_existing, filesystem_error);
        if (filesystem_error) {
            error = "Could not create the import rollback copy: " +
                    filesystem_error.message();
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    filesystem_error.clear();
    if (!ReplaceFileAtomic(temporary, destination, filesystem_error)) {
        error = "Could not activate the imported save: " + filesystem_error.message();
        if (had_destination) {
            std::error_code recovery_error;
            std::filesystem::copy_file(rollback, destination,
                std::filesystem::copy_options::overwrite_existing, recovery_error);
        }
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    filesystem_error.clear();
    std::filesystem::remove(rollback, filesystem_error);
    return true;
}

bool BackupUnlocked(std::filesystem::path& created, std::string& error) {
    const auto source = AdventurePath();
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(source, bytes)) {
        error = "No valid 512-byte Adventure save is available to back up.";
        return false;
    }
    std::error_code filesystem_error;
    const auto directory = g_config_directory / "save-backups";
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup garage: " + filesystem_error.message();
        return false;
    }
    created = directory / ("adventure-" + Timestamp() + ".bin");
    std::filesystem::copy_file(source, created,
                               std::filesystem::copy_options::overwrite_existing,
                               filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup: " + filesystem_error.message();
        return false;
    }
    return true;
}

bool BackupRawAdventureUnlocked(std::filesystem::path& created,
                                std::string& error) {
    const auto source = AdventurePath();
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBounded(source, kAdventureSaveSize, bytes) ||
        bytes.size() != kAdventureSaveSize) {
        error = "No 512-byte Adventure EEPROM is available to preserve.";
        return false;
    }
    std::error_code filesystem_error;
    const auto directory = g_config_directory / "save-backups";
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup garage: " +
                filesystem_error.message();
        return false;
    }
    created = directory /
        ("adventure-before-checksum-repair-" + Timestamp() + ".bin");
    std::filesystem::copy_file(source, created,
        std::filesystem::copy_options::overwrite_existing, filesystem_error);
    if (filesystem_error) {
        error = "Could not preserve the original EEPROM: " +
                filesystem_error.message();
        return false;
    }
    return true;
}

bool BackupPakUnlocked(int channel, std::filesystem::path& created,
                       std::string& error) {
    if (!ValidChannel(channel)) {
        error = "That Controller Pak channel is outside the supported range.";
        return false;
    }
    const auto source = ControllerPakPath(channel);
    std::vector<std::uint8_t> bytes;
    if (!ReadControllerPak(source, bytes)) {
        error = "No valid Controller Pak image is available to back up.";
        return false;
    }
    std::error_code filesystem_error;
    const auto directory = g_config_directory / "save-backups";
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the backup garage: " + filesystem_error.message();
        return false;
    }
    created = directory /
        ("controller-pak-" + std::to_string(channel + 1) + "-" +
         Timestamp() + ".mpk");
    std::filesystem::copy_file(source, created,
        std::filesystem::copy_options::overwrite_existing, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the Controller Pak backup: " +
                filesystem_error.message();
        return false;
    }
    return true;
}

struct BundleEntry {
    std::uint32_t kind = 0;
    std::uint32_t channel = 0;
    std::vector<std::uint8_t> bytes;
};

bool DecodeBundle(const std::filesystem::path& path,
                  std::vector<BundleEntry>& entries) {
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBounded(path, kMaximumBundleSize, bytes) || bytes.size() < 24U ||
        !std::equal(kBundleMagic.begin(), kBundleMagic.end(), bytes.begin()) ||
        ReadLE32(bytes, 16U) != kBundleVersion) {
        return false;
    }
    const std::uint32_t count = ReadLE32(bytes, 20U);
    if (count == 0U || count >
            1U + static_cast<std::uint32_t>(dkr::runtime::saves::kControllerPakCount)) {
        return false;
    }
    std::array<bool, 1U + dkr::runtime::saves::kControllerPakCount> seen{};
    std::size_t cursor = 24U;
    entries.clear();
    for (std::uint32_t index = 0; index < count; ++index) {
        if (cursor > bytes.size() || bytes.size() - cursor < 16U) {
            return false;
        }
        BundleEntry entry{};
        entry.kind = ReadLE32(bytes, cursor + 0U);
        entry.channel = ReadLE32(bytes, cursor + 4U);
        const std::uint32_t size = ReadLE32(bytes, cursor + 8U);
        const std::uint32_t checksum = ReadLE32(bytes, cursor + 12U);
        cursor += 16U;
        if (size > bytes.size() - cursor) {
            return false;
        }
        entry.bytes.assign(bytes.begin() + cursor, bytes.begin() + cursor + size);
        cursor += size;
        std::size_t seen_index = 0U;
        if (entry.kind == kBundleAdventureKind) {
            if (entry.channel != 0U || size != kAdventureSaveSize) {
                return false;
            }
        } else if (entry.kind == kBundlePakKind) {
            if (entry.channel >= dkr::runtime::saves::kControllerPakCount ||
                size != kControllerPakSize || !ValidControllerPak(entry.bytes)) {
                return false;
            }
            seen_index = 1U + entry.channel;
        } else {
            return false;
        }
        if (seen[seen_index] || checksum != ImageChecksum(entry.bytes)) {
            return false;
        }
        seen[seen_index] = true;
        entries.push_back(std::move(entry));
    }
    return cursor == bytes.size();
}

bool ReadBundle(const std::filesystem::path& path,
                std::vector<std::uint8_t>& bytes) {
    std::vector<BundleEntry> entries;
    if (!DecodeBundle(path, entries)) {
        return false;
    }
    return ReadFileBounded(path, kMaximumBundleSize, bytes);
}

} // namespace

void dkr::runtime::saves::configure(
    const std::filesystem::path& config_directory) {
    std::scoped_lock lock(g_save_manager_mutex);
    g_config_directory = config_directory;
    g_staged_host_online_save.clear();
    g_staged_host_online_match_id=0;
}

dkr::runtime::saves::SaveInfo dkr::runtime::saves::adventure_info() {
    std::scoped_lock lock(g_save_manager_mutex);
    SaveInfo info{};
    info.path = AdventurePath();
    std::error_code error;
    info.exists = std::filesystem::exists(info.path, error);
    if (info.exists && !error) {
        info.size = std::filesystem::file_size(info.path, error);
        std::vector<std::uint8_t> bytes;
        info.valid = !error && ReadAdventure(info.path, bytes);
    }
    return info;
}

std::filesystem::path dkr::runtime::saves::backup_directory() {
    std::scoped_lock lock(g_save_manager_mutex);
    return g_config_directory / "save-backups";
}

std::vector<std::filesystem::path> dkr::runtime::saves::adventure_backups() {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::filesystem::path> result;
    std::error_code error;
    const auto directory = g_config_directory / "save-backups";
    if (!std::filesystem::is_directory(directory, error)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied,
             error)) {
        if (entry.is_regular_file(error) &&
            entry.path().filename().string().rfind("adventure-", 0) == 0 &&
            entry.path().extension() == ".bin") {
            std::vector<std::uint8_t> bytes;
            if (ReadAdventure(entry.path(), bytes)) {
                result.push_back(entry.path());
            }
        }
        error.clear();
    }
    std::sort(result.begin(), result.end(), std::greater<>());
    return result;
}

bool dkr::runtime::saves::backup_adventure(std::filesystem::path& created,
                                            std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    return BackupUnlocked(created, error);
}

bool dkr::runtime::saves::export_adventure(
    const std::filesystem::path& destination, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(AdventurePath(), bytes)) {
        error = "No valid Adventure save is available to export.";
        return false;
    }
    return WriteAtomic(destination, bytes, ReadAdventure, error);
}

bool dkr::runtime::saves::import_adventure(
    const std::filesystem::path& source, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(source, bytes)) {
        std::vector<std::uint8_t> unverified;
        if (!ReadFileBounded(source, kAdventureSaveSize, unverified) ||
            unverified.size() != kAdventureSaveSize) {
            error = "That file is not a 512-byte DKR Adventure save.";
            return false;
        }
        if (!codec::repair_checksums(unverified, bytes, &error)) {
            error = "That Adventure save could not be checksum-repaired: " +
                    error;
            return false;
        }
    }
    std::error_code equivalent_error;
    if (std::filesystem::exists(AdventurePath(), equivalent_error)) {
        std::filesystem::path backup;
        std::vector<std::uint8_t> current;
        const bool current_valid = ReadAdventure(AdventurePath(), current);
        if (current_valid && !BackupUnlocked(backup, error)) {
            return false;
        }
        if (!current_valid &&
            !BackupRawAdventureUnlocked(backup, error)) {
            return false;
        }
    }
    return WriteAtomic(AdventurePath(), bytes, ReadAdventure, error);
}

bool dkr::runtime::saves::reset_adventure(std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::error_code exists_error;
    if (std::filesystem::exists(AdventurePath(), exists_error)) {
        std::filesystem::path backup;
        if (!BackupUnlocked(backup, error)) {
            return false;
        }
    }
    return WriteAtomic(AdventurePath(), codec::blank_bytes(),
                       ReadAdventure, error);
}

bool dkr::runtime::saves::load_adventure(codec::SaveImage& image,
                                         std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> bytes;
    if (!ReadAdventure(AdventurePath(), bytes)) {
        error = "No checksum-valid Adventure EEPROM is available to edit.";
        return false;
    }
    return codec::decode(bytes, image, &error);
}

bool dkr::runtime::saves::commit_adventure(const codec::SaveImage& image,
                                           std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    if (!codec::validate_editable_ranges(image, &error)) {
        error = "The edited Adventure save is outside DKR's retail limits: " +
            error;
        return false;
    }
    const std::vector<std::uint8_t> bytes = codec::encode(image);
    if (!codec::validate(bytes, &error)) {
        error = "The edited Adventure save could not be encoded safely: " + error;
        return false;
    }
    std::error_code exists_error;
    if (std::filesystem::exists(AdventurePath(), exists_error)) {
        std::filesystem::path backup;
        if (!BackupUnlocked(backup, error)) {
            return false;
        }
    }
    return WriteAtomic(AdventurePath(), bytes, ReadAdventure, error);
}

bool dkr::runtime::saves::repair_adventure_checksums(
    bool& changed, std::filesystem::path& original_backup,
    std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    changed = false;
    original_backup.clear();

    std::vector<std::uint8_t> original;
    if (!ReadFileBounded(AdventurePath(), kAdventureSaveSize, original) ||
        original.size() != kAdventureSaveSize) {
        error = "No 512-byte Adventure EEPROM is available to repair.";
        return false;
    }
    if (codec::validate(original)) {
        error.clear();
        return true;
    }

    std::vector<std::uint8_t> repaired;
    if (!codec::repair_checksums(original, repaired, &error)) {
        return false;
    }
    if (repaired == original) {
        error = "The EEPROM remains invalid even though no checksum byte changed.";
        return false;
    }

    // Repair is intentionally permitted to alter only checksum bytes:
    // two bytes at each populated adventure slot, the config checksum byte,
    // and two bytes at each T.T. record block. This check is independent of
    // the codec implementation and prevents an accidental payload rewrite.
    const auto checksum_byte = [](std::size_t index) {
        if (index == kAdventureConfigOffset) return true;
        for (std::size_t slot = 0; slot < codec::kAdventureSlotCount; ++slot) {
            const std::size_t offset = slot * kAdventureSlotSize;
            if (index == offset || index == offset + 1U) return true;
        }
        return index == kFastestLapsOffset ||
               index == kFastestLapsOffset + 1U ||
               index == kCourseTimesOffset ||
               index == kCourseTimesOffset + 1U;
    };
    for (std::size_t index = 0; index < original.size(); ++index) {
        if (original[index] != repaired[index] && !checksum_byte(index)) {
            error = "Checksum repair attempted to alter Adventure progress data.";
            return false;
        }
    }

    if (!BackupRawAdventureUnlocked(original_backup, error)) {
        return false;
    }
    if (!WriteAtomic(AdventurePath(), repaired, ReadAdventure, error)) {
        return false;
    }
    changed = true;
    error.clear();
    return true;
}

bool dkr::runtime::saves::canonical_adventure_bytes(
    std::vector<std::uint8_t>& bytes, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::uint8_t> stored;
    if (!ReadFileBounded(AdventurePath(), kAdventureSaveSize, stored) ||
        stored.size() != kAdventureSaveSize) {
        error = "No 512-byte Adventure EEPROM is available.";
        bytes.clear();
        return false;
    }
    return codec::canonical_bytes(stored, bytes, &error);
}

dkr::runtime::saves::SaveInfo
dkr::runtime::saves::previous_online_adventure_info() {
    std::scoped_lock lock(g_save_manager_mutex);
    SaveInfo info{};
    info.path = OnlineAdventurePath(true, 0U);
    std::error_code filesystem_error;
    info.exists = std::filesystem::exists(info.path, filesystem_error);
    if (info.exists && !filesystem_error) {
        info.size = std::filesystem::file_size(info.path, filesystem_error);
        std::vector<std::uint8_t> bytes;
        info.valid = !filesystem_error && ReadAdventure(info.path, bytes);
    }
    return info;
}

bool dkr::runtime::saves::prepare_host_online_adventure(
    OnlineSaveSeedMode mode, std::vector<std::uint8_t>& bytes,
    std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    bytes.clear();
    const std::filesystem::path destination = OnlineAdventurePath(true, 0U);

    if (mode == OnlineSaveSeedMode::Fresh) {
        bytes = codec::blank_bytes();
    } else {
        const std::filesystem::path source =
            mode == OnlineSaveSeedMode::CopySinglePlayer
                ? AdventurePath() : destination;
        std::vector<std::uint8_t> stored;
        if (!ReadAdventure(source, stored)) {
            error = mode == OnlineSaveSeedMode::CopySinglePlayer
                ? "No checksum-valid single-player Adventure save is available to copy."
                : "No checksum-valid previous online session save is available.";
            return false;
        }
        // The host explicitly chose an existing save. Preserve the complete
        // checksum-valid EEPROM image byte-for-byte, including opaque retail
        // data that the save editor does not interpret.
        bytes = std::move(stored);
    }

    // Creating a lobby is not a commit point: registration/approval may fail.
    // Keep the validated seed in memory until actual host game-save activation.
    g_staged_host_online_save = bytes;
    g_staged_host_online_match_id=0;
    error.clear();
    return true;
}

bool dkr::runtime::saves::bind_staged_host_online_adventure(std::uint64_t match_id,std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    if(!match_id||g_staged_host_online_save.empty()||
       (g_staged_host_online_match_id&&g_staged_host_online_match_id!=match_id)) {
        error="The staged host save does not belong to this lobby.";return false;
    }
    g_staged_host_online_match_id=match_id;error.clear();return true;
}
void dkr::runtime::saves::discard_staged_host_online_adventure() {
    std::scoped_lock lock(g_save_manager_mutex);
    g_staged_host_online_save.clear();g_staged_host_online_match_id=0;
}

bool dkr::runtime::saves::install_synchronized_online_adventure(
    std::uint64_t match_id, std::span<const std::uint8_t> bytes,
    std::filesystem::path& installed_path, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    installed_path.clear();
    if (match_id == 0U) {
        error = "The online session has no authenticated match identity.";
        return false;
    }
    if (bytes.size() != kAdventureSaveSize ||
        !codec::validate(bytes, &error)) {
        error = "The host supplied an invalid Adventure EEPROM: " + error;
        return false;
    }

    const std::vector<std::uint8_t> synchronized(bytes.begin(), bytes.end());
    installed_path = OnlineAdventurePath(false, match_id);
    if (!WriteAtomic(installed_path, synchronized, ReadAdventure, error)) {
        installed_path.clear();
        return false;
    }
    std::vector<std::uint8_t> verified;
    if (!ReadAdventure(installed_path, verified) || verified != synchronized) {
        error = "The synchronized online save did not pass its read-back verification.";
        installed_path.clear();
        return false;
    }
    error.clear();
    return true;
}

bool dkr::runtime::saves::read_online_adventure(
    bool host, std::uint64_t match_id, std::vector<std::uint8_t>& bytes,
    std::filesystem::path& path, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    bytes.clear();
    path = OnlineAdventurePath(host, match_id);
    if (host && !g_staged_host_online_save.empty()) {
        if(g_staged_host_online_match_id&&g_staged_host_online_match_id!=match_id) {
            error="The staged online save belongs to a different lobby.";return false;
        }
        std::vector<std::uint8_t> previous;
        const bool had_previous = ReadAdventure(path, previous);
        if (had_previous && previous != g_staged_host_online_save &&
            !WriteAtomic(path.string() + ".previous-session", previous, ReadAdventure, error)) {
            error = "The previous online session could not be preserved: " + error;
            return false;
        }
        if ((!had_previous || previous != g_staged_host_online_save) &&
            !WriteAtomic(path, g_staged_host_online_save, ReadAdventure, error)) return false;
        std::vector<std::uint8_t> verified;
        if (!ReadAdventure(path, verified) || verified != g_staged_host_online_save) {
            error = "The staged host save did not pass activation read-back verification.";
            return false;
        }
        g_staged_host_online_save.clear();
        g_staged_host_online_match_id=0;
    }
    if ((!host && match_id == 0U) || !ReadAdventure(path, bytes)) {
        error = "The expected checksum-valid online Adventure save is unavailable.";
        bytes.clear();
        return false;
    }
    error.clear();
    return true;
}

std::filesystem::path dkr::runtime::saves::online_adventure_subfolder(
    bool host, std::uint64_t match_id) {
    return OnlineAdventureSubfolder(host, match_id);
}
namespace {
bool ReadExperimentalOnlinePaks(const std::filesystem::path& path,std::vector<std::uint8_t>& bytes) {
    using dkr::runtime::netplay::experimental::Paks;
    if(!ReadFileBounded(path,Paks::kImagesBytes,bytes)||bytes.size()!=Paks::kImagesBytes)return false;
    Paks owner;return owner.start(1,15,bytes);
}
}
bool dkr::runtime::saves::read_experimental_online_paks(bool host,std::uint64_t match_id,std::vector<std::uint8_t>& images,std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    using netplay::experimental::Paks;
    if(!match_id){error="Online MemPak storage requires an authenticated match.";return false;}
    const auto path=OnlineAdventureSubfolder(host,match_id)/"experimental-controller-paks.bin";
    std::error_code ec;const bool exists=std::filesystem::exists(path,ec);
    if(ec){error="Cannot inspect online MemPak storage: "+ec.message();return false;}
    if(!exists){images=Paks::blank_images();error.clear();return true;}
    if(!ReadExperimentalOnlinePaks(path,images)){error="The online MemPak image is invalid; it has been left untouched.";return false;}
    error.clear();return true;
}
bool dkr::runtime::saves::commit_experimental_online_paks(bool host,std::uint64_t match_id,std::span<const std::uint8_t> images,std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    using netplay::experimental::Paks;
    Paks owner;if(!match_id||!owner.start(1,15,images)){error="Invalid confirmed online MemPak image; no file was changed.";return false;}
    return WriteAtomic(OnlineAdventureSubfolder(host,match_id)/"experimental-controller-paks.bin",{images.begin(),images.end()},ReadExperimentalOnlinePaks,error);
}
bool dkr::runtime::saves::commit_online_adventure(bool host,std::uint64_t match_id,
    std::span<const std::uint8_t> bytes,std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    if(!match_id||bytes.size()!=kAdventureSaveSize||!codec::validate(bytes,&error)) {
        error="The confirmed online EEPROM cannot be persisted: "+error;return false;
    }
    return WriteAtomic(OnlineAdventurePath(host,match_id),{bytes.begin(),bytes.end()},ReadAdventure,error);
}

dkr::runtime::saves::SaveInfo dkr::runtime::saves::controller_pak_info(
    int channel) {
    std::scoped_lock lock(g_save_manager_mutex);
    SaveInfo info{};
    if (!ValidChannel(channel)) {
        return info;
    }
    info.path = ControllerPakPath(channel);
    std::error_code filesystem_error;
    info.exists = std::filesystem::exists(info.path, filesystem_error);
    if (info.exists && !filesystem_error) {
        info.size = std::filesystem::file_size(info.path, filesystem_error);
        std::vector<std::uint8_t> bytes;
        info.valid = !filesystem_error && ReadControllerPak(info.path, bytes);
    }
    return info;
}

std::vector<std::filesystem::path>
dkr::runtime::saves::controller_pak_backups(int channel) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<std::filesystem::path> result;
    if (!ValidChannel(channel)) {
        return result;
    }
    std::error_code error;
    const auto directory = g_config_directory / "save-backups";
    if (!std::filesystem::is_directory(directory, error)) {
        return result;
    }
    const std::string prefix =
        "controller-pak-" + std::to_string(channel + 1) + "-";
    for (const auto& entry : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied,
             error)) {
        if (entry.is_regular_file(error) &&
            entry.path().filename().string().rfind(prefix, 0) == 0 &&
            entry.path().extension() == ".mpk") {
            std::vector<std::uint8_t> bytes;
            if (ReadControllerPak(entry.path(), bytes)) {
                result.push_back(entry.path());
            }
        }
        error.clear();
    }
    std::sort(result.begin(), result.end(), std::greater<>());
    return result;
}

bool dkr::runtime::saves::backup_controller_pak(
    int channel, std::filesystem::path& created, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    return BackupPakUnlocked(channel, created, error);
}

bool dkr::runtime::saves::export_controller_pak(
    int channel, const std::filesystem::path& destination, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    if (!ValidChannel(channel)) {
        error = "That Controller Pak channel is outside the supported range.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadControllerPak(ControllerPakPath(channel), bytes)) {
        error = "No valid Controller Pak image is available to export.";
        return false;
    }
    return WriteAtomic(destination, bytes, ReadControllerPak, error);
}

bool dkr::runtime::saves::import_controller_pak(
    int channel, const std::filesystem::path& source, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    if (!ValidChannel(channel)) {
        error = "That Controller Pak channel is outside the supported range.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadControllerPak(source, bytes)) {
        error = "That file is not a valid DKR-R Controller Pak image.";
        return false;
    }
    std::error_code exists_error;
    if (std::filesystem::exists(ControllerPakPath(channel), exists_error)) {
        std::filesystem::path backup;
        if (!BackupPakUnlocked(channel, backup, error)) {
            return false;
        }
    }
    return WriteAtomic(ControllerPakPath(channel), bytes, ReadControllerPak, error);
}

bool dkr::runtime::saves::export_bundle(
    const std::filesystem::path& destination, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<BundleEntry> entries;
    std::vector<std::uint8_t> image;
    if (ReadAdventure(AdventurePath(), image)) {
        entries.push_back({kBundleAdventureKind, 0U, image});
    }
    for (int channel = 0; channel < kControllerPakCount; ++channel) {
        image.clear();
        if (ReadControllerPak(ControllerPakPath(channel), image)) {
            entries.push_back({kBundlePakKind,
                static_cast<std::uint32_t>(channel), image});
        }
    }
    if (entries.empty()) {
        error = "No valid Adventure save or Controller Pak is available to export.";
        return false;
    }
    std::vector<std::uint8_t> bundle(kBundleMagic.begin(), kBundleMagic.end());
    AppendLE32(bundle, kBundleVersion);
    AppendLE32(bundle, static_cast<std::uint32_t>(entries.size()));
    for (const BundleEntry& entry : entries) {
        AppendLE32(bundle, entry.kind);
        AppendLE32(bundle, entry.channel);
        AppendLE32(bundle, static_cast<std::uint32_t>(entry.bytes.size()));
        AppendLE32(bundle, ImageChecksum(entry.bytes));
        bundle.insert(bundle.end(), entry.bytes.begin(), entry.bytes.end());
    }
    return WriteAtomic(destination, bundle, ReadBundle, error);
}

bool dkr::runtime::saves::import_bundle(
    const std::filesystem::path& source, std::string& error) {
    std::scoped_lock lock(g_save_manager_mutex);
    std::vector<BundleEntry> entries;
    if (!DecodeBundle(source, entries)) {
        error = "That file is not a valid DKR-R save bundle.";
        return false;
    }

    struct OriginalImage {
        std::filesystem::path destination;
        std::vector<std::uint8_t> bytes;
        ImageValidator validator = nullptr;
        bool existed = false;
    };
    std::vector<OriginalImage> originals;
    originals.reserve(entries.size());
    for (const BundleEntry& entry : entries) {
        OriginalImage original{};
        original.destination = entry.kind == kBundleAdventureKind
            ? AdventurePath()
            : ControllerPakPath(static_cast<int>(entry.channel));
        original.validator = entry.kind == kBundleAdventureKind
            ? ReadAdventure : ReadControllerPak;
        original.existed = original.validator(original.destination, original.bytes);
        if (std::filesystem::exists(original.destination) && !original.existed) {
            error = "A destination save exists but is corrupt; move it aside before importing.";
            return false;
        }
        originals.push_back(std::move(original));
    }

    std::size_t committed = 0U;
    for (; committed < entries.size(); ++committed) {
        const BundleEntry& entry = entries[committed];
        const OriginalImage& original = originals[committed];
        if (original.existed) {
            std::filesystem::path backup;
            const bool backed_up = entry.kind == kBundleAdventureKind
                ? BackupUnlocked(backup, error)
                : BackupPakUnlocked(static_cast<int>(entry.channel), backup, error);
            if (!backed_up) {
                break;
            }
        }
        if (!WriteAtomic(original.destination, entry.bytes, original.validator, error)) {
            break;
        }
    }
    if (committed == entries.size()) {
        return true;
    }

    // Roll back every earlier replacement from its in-memory original. A file
    // that did not exist before the transaction is removed instead.
    for (std::size_t index = 0; index < committed; ++index) {
        const OriginalImage& original = originals[index];
        if (original.existed) {
            std::string ignored;
            WriteAtomic(original.destination, original.bytes,
                        original.validator, ignored);
        } else {
            std::error_code remove_error;
            std::filesystem::remove(original.destination, remove_error);
        }
    }
    if (error.empty()) {
        error = "The save bundle transaction was rolled back safely.";
    } else {
        error += " The transaction was rolled back safely.";
    }
    return false;
}
