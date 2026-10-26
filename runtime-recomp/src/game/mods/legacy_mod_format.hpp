#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "legacy_behaviour.hpp"

namespace dkr::mods {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
inline constexpr std::size_t MiB = 1024 * 1024;
inline constexpr std::size_t MaxArchive = 128 * MiB;
inline constexpr std::size_t MaxPatch = 64 * MiB;
inline constexpr std::size_t MaxImage = 64 * MiB;
inline constexpr std::size_t MaxStaged = 256 * MiB;
inline constexpr unsigned MaxEntries = 1024;
inline constexpr unsigned Schema = 1;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline std::filesystem::path utf8_path(std::string_view value) {
    if (value.find('\0') != std::string_view::npos)
        throw Error("A file path contains a NUL byte.");
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::string sha256(View bytes);
// Canonical little-endian PCM hash without allocating a second song buffer.
std::string sha256_pcm_le(std::span<const std::int16_t> samples);
// Absolute private-storage path; Windows hash-addressed asset trees routinely
// exceed MAX_PATH. This does not change process-wide filesystem behaviour.
std::filesystem::path private_storage_path(const std::filesystem::path& path);
Bytes read_file(const std::filesystem::path& path, std::size_t limit);
void write_new_file(const std::filesystem::path& path, View bytes);
void canonicalize_rom(Bytes& bytes);
std::string verified_revision(View bytes);
std::string revision_fingerprint(std::string_view revision);
std::uint16_t be16(View bytes, std::size_t pos);
std::uint32_t be32(View bytes, std::size_t pos);
View slice(View bytes, std::size_t offset, std::size_t size);
Bytes inflate_asset(View bytes, std::size_t maximum);

// Views borrow the immutable ROM image. No offsets from untrusted content
// are used without a subtraction-based bounds check.
struct AssetImage {
    std::array<View, 50> sections{};
    std::uint32_t lut_start = 0;
    explicit AssetImage(View rom, const std::string& source_revision);
    std::vector<View> records(unsigned section) const;
    View record(unsigned section, unsigned id) const;
};

struct Root {
    unsigned carrier = 0;
    unsigned geometry = 0;
    unsigned object_map = 0;
    unsigned collectables = 0;
    unsigned race_type = 0;
    unsigned vehicles = 0;
    std::string name;
    std::string content_id;
    std::vector<std::string> blockers;
    BehaviourContract behaviour;
};
struct CharacterRoot {
    std::string name, content_id;
    unsigned base_character = 0, portrait = 0;
    std::array<unsigned, 3> headers{};
    std::vector<std::string> blockers;
    BehaviourContract behaviour;
};
struct Analysis {
    std::string source_revision;
    std::string patch_digest;
    std::string target_digest;
    std::string asset_digest;
    std::string profile;
    BehaviourContract behaviour;
    std::vector<Root> tracks;
    std::vector<std::string> characters;
    std::vector<CharacterRoot> character_roots;
    std::vector<std::string> notes;
    std::vector<std::string> blockers;
    // Changed section/record IDs are evidence, not permission to overlay a
    // complete bank. Dummy deletions must never replace stock content.
    std::vector<std::pair<unsigned, unsigned>> changed_records;
};
// Asset-only mode is an explicit frozen compatibility policy for artifacts
// prepared before the behaviour ABI. It is never an automatic failure fallback.
Analysis analyze(View base, View target, const std::string& patch_digest, bool behaviour_enabled = true);
std::string analysis_json(const Analysis& analysis);

struct PatchInput {
    std::string name;
    Bytes data;
};
// ZIP member names are validated but never used as output paths. Only xdelta
// bytes are read; bundled scripts, executables and links are never executed.
std::vector<PatchInput> read_patch_inputs(const std::filesystem::path& source);
Bytes decode_patch(View source, View patch);
} // namespace dkr::mods
