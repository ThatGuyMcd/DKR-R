#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dkr::online_stability {
inline constexpr unsigned MaximumPrepareBatch = 4;
std::uint64_t available_memory();
inline unsigned preparation_batch(std::uint64_t disk, std::uint64_t memory) {
    return disk >= 4352ULL * 1024 * 1024 && memory >= 3ULL * 1024 * 1024 * 1024 ? MaximumPrepareBatch : 2;
}
inline std::filesystem::path archived_course(const std::filesystem::path& root, std::string_view digest) {
    if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string_view::npos)
        throw std::invalid_argument("Invalid offline course digest.");
    return root / "online-variants" / (std::string(digest) + ".dkrmap");
}
} // namespace dkr::online_stability
