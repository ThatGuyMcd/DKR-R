#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace dkr::runtime::time_trial {

// Owns the temporary backing file for one replay. The caller serializes access.
class TheatreFile {
  public:
    TheatreFile() = default;
    TheatreFile(const TheatreFile&) = delete;
    TheatreFile& operator=(const TheatreFile&) = delete;

    ~TheatreFile() {
        close();
    }

    bool open() {
        if (stream.is_open())
            return bool(stream);
        const auto root = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int i = 0; i < 8; ++i) {
            const auto path = root / ("dkr-r-theatre-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path)) {
                directory_ = path;
#ifndef _WIN32
                std::filesystem::permissions(directory_, std::filesystem::perms::owner_all);
#endif
                stream.open(directory_ / "index.tmp",
                            std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
                return bool(stream);
            }
        }
        return false;
    }

    void close() {
        if (!directory_.empty()) {
            stream.close();
            stream.clear();
            std::error_code error;
            std::filesystem::remove(directory_ / "index.tmp", error);
            std::filesystem::remove(directory_, error);
            directory_.clear();
        }
    }

    std::fstream stream;

  private:
    std::filesystem::path directory_;
};

} // namespace dkr::runtime::time_trial
