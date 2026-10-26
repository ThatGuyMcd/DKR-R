#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::updates {
inline constexpr unsigned kSchema = 1;
inline constexpr std::uint64_t kMaxPackageBytes = 512ULL * 1024 * 1024;
inline constexpr std::uint64_t kMaxExtractedBytes = 1024ULL * 1024 * 1024;
inline constexpr std::size_t kMaxMetadataBytes = 2 * 1024 * 1024;
struct Version {
    std::uint32_t major{}, minor{}, patch{};
    std::vector<std::string> pre;
    static Version parse(std::string_view);
    int compare(const Version&) const;
};
struct File {
    std::string path, sha256, link;
    std::uint64_t bytes{};
    unsigned mode = 0644;
};
struct Artifact {
    std::string platform, name, kind, sha256, root, minimum_os, minimum_glibc;
    std::uint64_t bytes{};
    std::vector<File> files;
};
struct Manifest {
    std::string version, tag, channel, notes, commit;
    unsigned minimum_updater = 1;
    std::vector<Artifact> artifacts;
    const Artifact* find(std::string_view platform) const;
};
std::string platform();
bool safe_relative(std::string_view);
bool protected_path(std::string_view);
bool allowed_https_url(std::string_view);
std::string artifact_url(const Manifest&, const Artifact&);
std::vector<std::uint8_t> unhex(std::string_view, std::size_t bytes);
std::string hex(std::span<const std::uint8_t>);
std::string hash(std::span<const std::uint8_t>);
std::string hash_file(const std::filesystem::path&, std::uint64_t limit = kMaxExtractedBytes);
Manifest verify_manifest(std::string_view json, std::string_view signature_hex, std::string_view public_key_hex);
std::string read_text(const std::filesystem::path&, std::size_t limit = kMaxMetadataBytes);
void durable_write(const std::filesystem::path&, std::string_view);
void replace_file(const std::filesystem::path& from, const std::filesystem::path& to);
void copy_file_durable(const std::filesystem::path& from, const std::filesystem::path& to);
void require_private_tree(const std::filesystem::path&);
void extract_verified(const std::filesystem::path& package, const Artifact&,
                      const std::filesystem::path& fresh_directory);
// This receipt lists package-owned paths, never user data. It is also included
// in the signed package's file table. Old receipt paths are independently bounded.
std::vector<std::string> installed_files(const std::filesystem::path& install);
// Transaction directories are beneath the canonical install directory. Files
// are backed up before changes, with a durable journal for idempotent recovery.
void install_files(const std::filesystem::path& install, const std::filesystem::path& stage, const Artifact&);
void recover_files(const std::filesystem::path& install);
void commit_files(const std::filesystem::path& install);
bool pending_files(const std::filesystem::path& install);
std::string transaction_state(const std::filesystem::path& install);
void archive_transaction(const std::filesystem::path& install);
void authorize_updated_launch(const std::filesystem::path&, std::string_view token, const std::filesystem::path& data,
                              std::string_view version);
bool updated_launch_authorized(const std::filesystem::path&, std::string_view token);
bool finalize_healthy_update(const std::filesystem::path&);
} // namespace dkr::updates
