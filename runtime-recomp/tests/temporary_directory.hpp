#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

// Each process atomically claims its own parent directory. Tests may reset
// fixtures below it without ever removing another invocation's fixtures.
class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(const std::string& prefix) {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                             (prefix + "-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Cannot create isolated test directory: " + prefix);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};
