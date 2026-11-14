#include "rom_revision.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> swap32(std::vector<std::uint8_t> bytes) {
    for (std::size_t i = 0; i + 3U < bytes.size(); i += 4U) {
        std::swap(bytes[i], bytes[i + 3U]);
        std::swap(bytes[i + 1U], bytes[i + 2U]);
    }
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 4);
    const std::filesystem::path v80_z64 = std::filesystem::u8path(argv[1]);
    const std::filesystem::path v80_swapped = std::filesystem::u8path(argv[2]);
    const std::filesystem::path v77_z64 = std::filesystem::u8path(argv[3]);

    const auto v80_a = dkr::runtime::rom::inspect(v80_z64);
    const auto v80_b = dkr::runtime::rom::inspect(v80_swapped);
    const auto v77 = dkr::runtime::rom::inspect(v77_z64);
    assert(v80_a.supported() && v80_a.revision == dkr::runtime::rom::Revision::UsV80);
    assert(v80_b.supported() && v80_b.revision == dkr::runtime::rom::Revision::UsV80);
    assert(v80_b.byte_order == dkr::runtime::rom::ByteOrder::ByteSwapped16);
    assert(v80_a.canonical_xxh3 == v80_b.canonical_xxh3);
    assert(v77.supported() && v77.revision == dkr::runtime::rom::Revision::UsV77);

    const auto test_identity =
        std::to_string(std::hash<std::string>{}(std::filesystem::absolute(std::filesystem::u8path(argv[0])).string()));
    const auto temporary =
        std::filesystem::temp_directory_path() / ("dkr-r-rom-revision-test-" + test_identity + ".n64");
    write_file(temporary, swap32(read_file(v77_z64)));
    const auto v77_little = dkr::runtime::rom::inspect(temporary);
    std::filesystem::remove(temporary);
    assert(v77_little.supported());
    assert(v77_little.revision == dkr::runtime::rom::Revision::UsV77);
    assert(v77_little.byte_order == dkr::runtime::rom::ByteOrder::LittleEndian32);

    const auto canonical_cache =
        std::filesystem::temp_directory_path() / ("dkr-r-rom-revision-canonical-cache-" + test_identity);
    std::filesystem::remove_all(canonical_cache);
    std::filesystem::path canonical_path;
    std::string canonical_error;
    assert(
        dkr::runtime::rom::materialize_canonical(v80_swapped, v80_b, canonical_cache, canonical_path, canonical_error));
    assert(read_file(canonical_path) == read_file(v80_z64));
    const auto canonical_identity = dkr::runtime::rom::inspect(canonical_path);
    assert(canonical_identity.supported());
    assert(canonical_identity.byte_order == dkr::runtime::rom::ByteOrder::BigEndian);
    std::filesystem::remove_all(canonical_cache);

    const auto identity_cache =
        std::filesystem::temp_directory_path() / ("dkr-r-rom-identity-cache-test-" + test_identity);
    std::filesystem::remove_all(identity_cache);
    std::filesystem::create_directories(identity_cache);
    const auto cached_rom = identity_cache / "cached.z64";
    write_file(cached_rom, read_file(v80_z64));
    dkr::runtime::rom::configure_identity_cache(identity_cache);
    const auto first_cached_identity = dkr::runtime::rom::inspect(cached_rom);
    assert(first_cached_identity.supported());
    // Reload from disk so this verifies the persistent parser as well as the
    // in-process lookup populated by inspect().
    dkr::runtime::rom::configure_identity_cache(identity_cache);
    const auto cache_hit = dkr::runtime::rom::cached_identity(cached_rom);
    assert(cache_hit.has_value());
    assert(cache_hit->canonical_xxh3 == first_cached_identity.canonical_xxh3);

    auto changed_bytes = read_file(cached_rom);
    changed_bytes.front() ^= 0xFFU;
    write_file(cached_rom, changed_bytes);
    std::filesystem::last_write_time(cached_rom,
                                     std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
    assert(!dkr::runtime::rom::cached_identity(cached_rom).has_value());
    assert(!dkr::runtime::rom::inspect(cached_rom).supported());
    std::filesystem::remove_all(identity_cache);

    std::cout << "[test][rom-revision] PASS: v77/v80 and all supplied byte orders\n";
    return 0;
}
