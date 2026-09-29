#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace dkr::runtime::android_pipeline_cache {
// Set once before renderer workers start. Only device creation/destruction does
// file I/O; compilation threads share Vulkan's internally synchronized cache.
inline std::filesystem::path directory;
constexpr std::size_t maximum_bytes = 32U * 1024U * 1024U;
struct Identity {
    std::uint32_t vendor, device, driver;
    std::array<std::uint8_t, 16> uuid;
};
inline std::uint32_t word(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
        (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
}
inline void append_word(std::vector<std::uint8_t>& data, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.push_back(std::uint8_t(value >> (i * 8)));
}
inline std::uint64_t checksum(const std::vector<std::uint8_t>& data, std::size_t begin = 0) {
    std::uint64_t result = 14695981039346656037ULL;
    for (std::size_t i = begin; i < data.size(); ++i) { result ^= data[i]; result *= 1099511628211ULL; }
    return result; // Corruption detection, not an authentication mechanism.
}
inline bool compatible(const std::vector<std::uint8_t>& data, const Identity& id) {
    if (data.size() < 32 || data.size() > maximum_bytes || word(data, 0) != 32 ||
        word(data, 4) != 1 || word(data, 8) != id.vendor || word(data, 12) != id.device) return false;
    return std::equal(id.uuid.begin(), id.uuid.end(), data.begin() + 16);
}
inline std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& payload, const Identity& id) {
    if (!compatible(payload, id)) return {};
    std::vector<std::uint8_t> result{'D','K','R','V','K','P','C',1};
    append_word(result, id.driver);
    append_word(result, static_cast<std::uint32_t>(payload.size()));
    const auto sum = checksum(payload);
    append_word(result, std::uint32_t(sum)); append_word(result, std::uint32_t(sum >> 32));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}
inline std::vector<std::uint8_t> decode(const std::vector<std::uint8_t>& file, const Identity& id) {
    const std::array<std::uint8_t, 8> magic{'D','K','R','V','K','P','C',1};
    if (file.size() < 56 || file.size() > maximum_bytes + 24 ||
        !std::equal(magic.begin(), magic.end(), file.begin()) || word(file, 8) != id.driver ||
        word(file, 12) != file.size() - 24) return {};
    const auto sum = std::uint64_t(word(file, 16)) | (std::uint64_t(word(file, 20)) << 32);
    if (sum != checksum(file, 24)) return {};
    std::vector<std::uint8_t> payload(file.begin() + 24, file.end());
    return compatible(payload, id) ? payload : std::vector<std::uint8_t>{};
}
inline std::filesystem::path path_for(const Identity& id) {
    char prefix[80];
    std::snprintf(prefix, sizeof(prefix), "pipeline-v1-%08x-%08x-%08x-", id.vendor, id.device, id.driver);
    std::string name(prefix);
    constexpr char hex[] = "0123456789abcdef";
    for (auto value : id.uuid) { name += hex[value >> 4]; name += hex[value & 15]; }
    return directory / (name + ".bin");
}
inline std::vector<std::uint8_t> load(const Identity& id) noexcept {
    try {
        if (directory.empty()) return {};
        std::ifstream input(path_for(id), std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto length = input.tellg();
        if (length < 56 || length > static_cast<std::streamoff>(maximum_bytes + 24)) return {};
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return {};
        return decode(bytes, id);
    } catch (...) { return {}; } // A disposable cache must never prevent startup.
}
inline bool save(const Identity& id, const std::vector<std::uint8_t>& payload) noexcept {
    try {
        if (directory.empty()) return false;
        auto file = encode(payload, id);
        if (file.empty()) return false;
        std::filesystem::create_directories(directory);
        const auto destination = path_for(id);
        auto temporary = destination; temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output.write(reinterpret_cast<const char*>(file.data()), file.size())) return false;
            output.flush();
            if (!output) return false;
        }
        // Android rename replaces atomically; a failed write leaves the old
        // cache intact. No recursive cleanup or directory-wide eviction.
        std::filesystem::rename(temporary, destination);
        return true;
    } catch (...) { return false; }
}
}
