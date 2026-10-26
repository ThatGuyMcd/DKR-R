#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace RT64 {
struct FileSystem;
}

namespace dkr::runtime::rice_texture {

struct ImportResult {
    std::size_t source_images = 0;
    std::size_t identities = 0;
    std::size_t merged_pairs = 0;
    std::size_t opaque_rgb = 0;
    std::size_t all_images = 0;
    std::string detail;
};

using ProgressCallback = std::function<bool(std::size_t completed, std::size_t total, const char* stage)>;

bool convert_archive(RT64::FileSystem& archive, const std::filesystem::path& destination,
                     const std::string& source_name, ImportResult& result, std::string& error,
                     const ProgressCallback& progress = {});

} // namespace dkr::runtime::rice_texture
