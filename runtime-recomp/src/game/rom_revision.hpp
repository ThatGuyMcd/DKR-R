#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>

namespace dkr::runtime::rom {

enum class Revision {
    Unsupported,
    UsV77,
    UsV80,
};

enum class ByteOrder {
    Unknown,
    BigEndian,
    ByteSwapped16,
    LittleEndian32,
};

enum class InspectionError {
    None,
    FailedToOpen,
    TooSmall,
    InvalidHeader,
    UnsupportedRevision,
};

struct Identity {
    Revision revision = Revision::Unsupported;
    ByteOrder byte_order = ByteOrder::Unknown;
    InspectionError error = InspectionError::None;
    std::uint64_t canonical_xxh3 = 0;
    std::uint32_t header_crc1 = 0;
    std::uint32_t header_crc2 = 0;
    std::uint8_t header_revision = 0;
    std::size_t file_size = 0;

    [[nodiscard]] bool supported() const {
        return error == InspectionError::None && revision != Revision::Unsupported;
    }
};

inline constexpr std::size_t kRetailRomSize = 12U * 1024U * 1024U;
inline constexpr std::uint64_t kUsV77Xxh3 = 0x68512C37A6FDA951ULL;
inline constexpr std::uint64_t kUsV80Xxh3 = 0xB55D4348B9AB07F5ULL;

[[nodiscard]] Identity inspect(const std::filesystem::path& path);
// Loads and maintains a metadata-keyed cache of identities that DKR-R has
// already computed. A cache hit is advisory only: librecomp remains the final
// authoritative ROM validator immediately before runtime start.
void configure_identity_cache(const std::filesystem::path& config_directory);
[[nodiscard]] std::optional<Identity> cached_identity(const std::filesystem::path& path);
// N64ModernRuntime accepts every standard ROM byte order, but revision
// dispatch must not make the selected container format part of the runtime
// path. Non-big-endian inputs are normalised once into DKR-R's local cache and
// verified against the identity discovered during inspection. The user's ROM
// is never modified.
[[nodiscard]] bool materialize_canonical(const std::filesystem::path& source, const Identity& identity,
                                         const std::filesystem::path& cache_directory,
                                         std::filesystem::path& canonical_path, std::string& error);
[[nodiscard]] std::string describe(const Identity& identity);
[[nodiscard]] std::string_view revision_id(Revision revision);
[[nodiscard]] std::string_view revision_name(Revision revision);
[[nodiscard]] std::string_view byte_order_name(ByteOrder byte_order);

} // namespace dkr::runtime::rom
