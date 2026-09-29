#include "rom_revision.hpp"
#include "startup_performance.hpp"

#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <vector>

namespace dkr::runtime::rom {
namespace {

constexpr std::array<std::uint8_t, 4> kBigEndianMagic{0x80, 0x37, 0x12, 0x40};
constexpr std::array<std::uint8_t, 4> kByteSwapped16Magic{0x37, 0x80, 0x40, 0x12};
constexpr std::array<std::uint8_t, 4> kLittleEndian32Magic{0x40, 0x12, 0x37, 0x80};

struct FileStamp {
    std::uintmax_t size = 0;
#if defined(__ANDROID__)
    // Bionic libc++ uses a 128-bit duration rep with no iostream operators.
    // Android's timestamps fit signed 64-bit nanoseconds; reject outliers
    // rather than truncate and potentially accept an invalid cache entry.
    std::int64_t modified = 0;
#else
    std::filesystem::file_time_type::rep modified = 0;
#endif
    bool valid = false;
};

struct CachedIdentity {
    FileStamp stamp;
    Identity identity;
};

std::mutex g_identity_cache_mutex;
std::map<std::string, CachedIdentity> g_identity_cache;
std::filesystem::path g_identity_cache_path;

std::string PathUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string CacheKey(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(path, error).lexically_normal();
    return PathUtf8(error ? path.lexically_normal() : absolute);
}

FileStamp ReadStamp(const std::filesystem::path& path) {
    FileStamp stamp{};
    std::error_code error;
    stamp.size = std::filesystem::file_size(path, error);
    if (error) return stamp;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return stamp;
    const auto ticks = modified.time_since_epoch().count();
#if defined(__ANDROID__)
    if (ticks < INT64_MIN || ticks > INT64_MAX) return stamp;
#endif
    stamp.modified = ticks;
    stamp.valid = true;
    return stamp;
}

bool SameStamp(const FileStamp& left, const FileStamp& right) {
    return left.valid && right.valid && left.size == right.size &&
           left.modified == right.modified;
}

void SaveIdentityCacheLocked() {
    if (g_identity_cache_path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(g_identity_cache_path.parent_path(),
                                        error);
    if (error) return;
    const std::filesystem::path temporary =
        std::filesystem::path(g_identity_cache_path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return;
    output << "# DKR-R ROM identity cache v1\n";
    for (const auto& [key, entry] : g_identity_cache) {
        if (!entry.stamp.valid || !entry.identity.supported()) continue;
        output << std::quoted(key) << '\t' << entry.stamp.size << '\t'
               << entry.stamp.modified << '\t'
               << static_cast<unsigned>(entry.identity.revision) << '\t'
               << static_cast<unsigned>(entry.identity.byte_order) << '\t'
               << entry.identity.canonical_xxh3 << '\t'
               << entry.identity.header_crc1 << '\t'
               << entry.identity.header_crc2 << '\t'
               << static_cast<unsigned>(entry.identity.header_revision) << '\n';
    }
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        return;
    }
    std::filesystem::rename(temporary, g_identity_cache_path, error);
    if (!error) return;
    error.clear();
    std::filesystem::remove(g_identity_cache_path, error);
    error.clear();
    std::filesystem::rename(temporary, g_identity_cache_path, error);
    if (error) std::filesystem::remove(temporary, error);
}

std::optional<Identity> FindCachedIdentity(const std::filesystem::path& path,
                                           const FileStamp& stamp) {
    if (!stamp.valid) return std::nullopt;
    const std::string key = CacheKey(path);
    std::scoped_lock lock(g_identity_cache_mutex);
    const auto match = g_identity_cache.find(key);
    if (match == g_identity_cache.end() ||
        !SameStamp(match->second.stamp, stamp) ||
        !match->second.identity.supported()) {
        return std::nullopt;
    }
    return match->second.identity;
}

void RememberIdentity(const std::filesystem::path& path,
                      const FileStamp& stamp, const Identity& identity) {
    if (!stamp.valid || !identity.supported()) return;
    std::scoped_lock lock(g_identity_cache_mutex);
    g_identity_cache[CacheKey(path)] = {stamp, identity};
    SaveIdentityCacheLocked();
}

bool ReadExact(std::ifstream& input, std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return true;
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return input.gcount() == static_cast<std::streamsize>(bytes.size());
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

ByteOrder detect_byte_order(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kBigEndianMagic.size()) {
        return ByteOrder::Unknown;
    }

    const std::array<std::uint8_t, 4> magic{
        bytes[0], bytes[1], bytes[2], bytes[3]};
    if (magic == kBigEndianMagic) {
        return ByteOrder::BigEndian;
    }
    if (magic == kByteSwapped16Magic) {
        return ByteOrder::ByteSwapped16;
    }
    if (magic == kLittleEndian32Magic) {
        return ByteOrder::LittleEndian32;
    }
    return ByteOrder::Unknown;
}

void canonicalize(std::vector<std::uint8_t>& bytes, ByteOrder byte_order) {
    // librecomp pads to a complete N64 word before normalising. Match that
    // behaviour so inspection and final ROM selection always agree.
    bytes.resize((bytes.size() + 3U) & ~std::size_t{3U});
    if (byte_order == ByteOrder::ByteSwapped16) {
        for (std::size_t i = 0; i < bytes.size(); i += 2U) {
            std::swap(bytes[i], bytes[i + 1U]);
        }
    } else if (byte_order == ByteOrder::LittleEndian32) {
        for (std::size_t i = 0; i < bytes.size(); i += 4U) {
            std::swap(bytes[i], bytes[i + 3U]);
            std::swap(bytes[i + 1U], bytes[i + 2U]);
        }
    }
}

} // namespace

Identity inspect(const std::filesystem::path& path) {
    const auto inspection_started_at =
        dkr::runtime::startup_performance::Clock::now();
    Identity identity{};
    const FileStamp stamp = ReadStamp(path);
    if (const auto cached = FindCachedIdentity(path, stamp)) {
        dkr::runtime::startup_performance::report(
            "rom-inspect-cache-hit", inspection_started_at);
        return *cached;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        identity.error = InspectionError::FailedToOpen;
        dkr::runtime::startup_performance::report(
            "rom-inspect-failed-open", inspection_started_at);
        return identity;
    }
    identity.file_size = stamp.valid
        ? static_cast<std::size_t>(stamp.size) : 0U;
    if (!stamp.valid || stamp.size < 0x40U) {
        identity.error = InspectionError::TooSmall;
        dkr::runtime::startup_performance::report(
            "rom-inspect-too-small", inspection_started_at);
        return identity;
    }

    const std::size_t bytes_to_read = stamp.size == kRetailRomSize
        ? kRetailRomSize : 0x40U;
    std::vector<std::uint8_t> bytes(bytes_to_read);
    if (!ReadExact(input, bytes)) {
        identity.error = InspectionError::FailedToOpen;
        dkr::runtime::startup_performance::report(
            "rom-inspect-short-read", inspection_started_at);
        return identity;
    }

    identity.byte_order = detect_byte_order(bytes);
    if (identity.byte_order == ByteOrder::Unknown) {
        identity.error = InspectionError::InvalidHeader;
        dkr::runtime::startup_performance::report(
            "rom-inspect-invalid-header", inspection_started_at);
        return identity;
    }

    canonicalize(bytes, identity.byte_order);
    identity.header_crc1 = read_be32(bytes, 0x10U);
    identity.header_crc2 = read_be32(bytes, 0x14U);
    identity.header_revision = bytes[0x3FU];

    if (identity.file_size != kRetailRomSize) {
        identity.error = InspectionError::UnsupportedRevision;
        dkr::runtime::startup_performance::report(
            "rom-inspect-wrong-size", inspection_started_at);
        return identity;
    }

    identity.canonical_xxh3 = XXH3_64bits(bytes.data(), bytes.size());

    if (identity.canonical_xxh3 == kUsV77Xxh3) {
        identity.revision = Revision::UsV77;
    } else if (identity.canonical_xxh3 == kUsV80Xxh3) {
        identity.revision = Revision::UsV80;
    } else {
        identity.error = InspectionError::UnsupportedRevision;
    }
    RememberIdentity(path, stamp, identity);
    dkr::runtime::startup_performance::report(
        identity.supported() ? "rom-inspect-cache-miss"
                             : "rom-inspect-unsupported",
        inspection_started_at);
    return identity;
}

void configure_identity_cache(const std::filesystem::path& config_directory) {
    std::scoped_lock lock(g_identity_cache_mutex);
    g_identity_cache.clear();
    g_identity_cache_path = config_directory / "rom-identities-v1.txt";
    std::ifstream input(g_identity_cache_path);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream record(line);
        std::string key;
        CachedIdentity entry{};
        unsigned revision = 0;
        unsigned byte_order = 0;
        unsigned header_revision = 0;
        if (!(record >> std::quoted(key) >> entry.stamp.size >>
              entry.stamp.modified >> revision >> byte_order >>
              entry.identity.canonical_xxh3 >> entry.identity.header_crc1 >>
              entry.identity.header_crc2 >> header_revision)) {
            continue;
        }
        if (revision < static_cast<unsigned>(Revision::UsV77) ||
            revision > static_cast<unsigned>(Revision::UsV80) ||
            byte_order < static_cast<unsigned>(ByteOrder::BigEndian) ||
            byte_order > static_cast<unsigned>(ByteOrder::LittleEndian32) ||
            entry.stamp.size != kRetailRomSize || header_revision > 0xFFU) {
            continue;
        }
        entry.stamp.valid = true;
        entry.identity.revision = static_cast<Revision>(revision);
        entry.identity.byte_order = static_cast<ByteOrder>(byte_order);
        entry.identity.error = InspectionError::None;
        entry.identity.file_size = static_cast<std::size_t>(entry.stamp.size);
        entry.identity.header_revision =
            static_cast<std::uint8_t>(header_revision);
        if (entry.identity.canonical_xxh3 != kUsV77Xxh3 &&
            entry.identity.canonical_xxh3 != kUsV80Xxh3) {
            continue;
        }
        g_identity_cache.emplace(std::move(key), entry);
    }
}

std::optional<Identity> cached_identity(const std::filesystem::path& path) {
    return FindCachedIdentity(path, ReadStamp(path));
}

bool materialize_canonical(const std::filesystem::path& source,
                           const Identity& identity,
                           const std::filesystem::path& cache_directory,
                           std::filesystem::path& canonical_path,
                           std::string& error) {
    if (!identity.supported()) {
        error = "An unsupported ROM cannot be normalised.";
        return false;
    }
    if (identity.byte_order == ByteOrder::BigEndian) {
        canonical_path = source;
        error.clear();
        return true;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(cache_directory, filesystem_error);
    if (filesystem_error) {
        error = "The local canonical ROM cache could not be created: " +
            filesystem_error.message();
        return false;
    }

    const std::filesystem::path destination = cache_directory /
        (std::string(revision_id(identity.revision)) + ".z64");
    const auto cached_identity = inspect(destination);
    if (cached_identity.supported() &&
        cached_identity.byte_order == ByteOrder::BigEndian &&
        cached_identity.canonical_xxh3 == identity.canonical_xxh3) {
        canonical_path = destination;
        error.clear();
        return true;
    }

    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error = "The selected ROM could not be reopened for normalisation.";
        return false;
    }
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() != identity.file_size || bytes.size() != kRetailRomSize) {
        error = "The selected ROM changed while it was being prepared.";
        return false;
    }
    canonicalize(bytes, identity.byte_order);
    if (XXH3_64bits(bytes.data(), bytes.size()) != identity.canonical_xxh3) {
        error = "The selected ROM failed canonical verification.";
        return false;
    }

    std::random_device random;
    std::ostringstream temporary_name;
    temporary_name << destination.filename().string() << ".tmp."
                   << std::hex << random() << random();
    const std::filesystem::path temporary =
        destination.parent_path() / temporary_name.str();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "The canonical ROM cache could not be written.";
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, filesystem_error);
            error = "The canonical ROM cache write did not complete.";
            return false;
        }
    }

    // A second launcher may have prepared the same immutable revision while
    // this process was writing. Prefer its verified result if the rename loses
    // that race.
    filesystem_error.clear();
    std::filesystem::remove(destination, filesystem_error);
    filesystem_error.clear();
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
        const auto raced_identity = inspect(destination);
        std::filesystem::remove(temporary, filesystem_error);
        if (!raced_identity.supported() ||
            raced_identity.byte_order != ByteOrder::BigEndian ||
            raced_identity.canonical_xxh3 != identity.canonical_xxh3) {
            error = "The canonical ROM cache could not be committed.";
            return false;
        }
    }

    const auto written_identity = inspect(destination);
    if (!written_identity.supported() ||
        written_identity.byte_order != ByteOrder::BigEndian ||
        written_identity.canonical_xxh3 != identity.canonical_xxh3) {
        error = "The canonical ROM cache failed its final verification.";
        return false;
    }
    canonical_path = destination;
    error.clear();
    return true;
}

std::string_view revision_id(Revision revision) {
    switch (revision) {
    case Revision::UsV77: return "dkr.us.v77";
    case Revision::UsV80: return "dkr.us.v80";
    default: return "unsupported";
    }
}

std::string_view revision_name(Revision revision) {
    switch (revision) {
    case Revision::UsV77: return "Diddy Kong Racing - US v 1.0";
    case Revision::UsV80: return "Diddy Kong Racing - US Rev A / v 1.1";
    default: return "Unsupported Diddy Kong Racing revision";
    }
}

std::string_view byte_order_name(ByteOrder byte_order) {
    switch (byte_order) {
    case ByteOrder::BigEndian: return "z64 / big-endian";
    case ByteOrder::ByteSwapped16: return "v64 / 16-bit byte-swapped";
    case ByteOrder::LittleEndian32: return "n64 / 32-bit little-endian";
    default: return "unknown";
    }
}

std::string describe(const Identity& identity) {
    if (identity.supported()) {
        std::ostringstream output;
        output << revision_name(identity.revision) << " ("
               << byte_order_name(identity.byte_order) << ')';
        return output.str();
    }
    switch (identity.error) {
    case InspectionError::FailedToOpen:
        return "The ROM could not be opened.";
    case InspectionError::TooSmall:
        return "The selected file is too small to contain an N64 ROM header.";
    case InspectionError::InvalidHeader:
        return "The selected file is not a recognized N64 ROM.";
    case InspectionError::UnsupportedRevision:
        return "This Diddy Kong Racing revision or modified ROM is not supported.";
    default:
        return "The selected ROM could not be identified.";
    }
}

} // namespace dkr::runtime::rom
