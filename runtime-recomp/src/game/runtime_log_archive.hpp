#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace dkr::runtime::support::history {
inline constexpr std::size_t MaxLogs = 20;
inline constexpr std::uintmax_t MaxBytes = 128ULL * 1024 * 1024;
inline bool owned_name(const std::string& name) {
    if (!name.starts_with("runtime-") || !name.ends_with(".log"))
        return false;
    const auto id = name.substr(8, name.size() - 12);
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
}
inline std::vector<std::filesystem::path> logs(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (owned_name(it->path().filename().string()) &&
            it->symlink_status(error).type() == std::filesystem::file_type::regular)
            result.push_back(it->path());
    }
    std::sort(result.begin(), result.end());
    return result;
}
inline void prune(const std::filesystem::path& directory) {
    auto files = logs(directory);
    std::error_code error;
    std::uintmax_t total = 0;
    for (const auto& file : files) {
        const auto size = std::filesystem::file_size(file, error);
        if (!error)
            total += size;
        error.clear();
    }
    for (std::size_t i = 0; i < files.size() && (files.size() - i > MaxLogs || total > MaxBytes); ++i) {
        const auto size = std::filesystem::file_size(files[i], error);
        if (!error && std::filesystem::remove(files[i], error))
            total -= size;
        error.clear();
    }
}
// Never truncate a locked/unmovable current log on a second process launch.
inline bool rotate(const std::filesystem::path& directory) {
    std::error_code error;
    const auto archive = directory / "history";
    std::filesystem::create_directories(archive, error);
    if (error)
        return false;
    const auto previous = directory / "runtime-previous.log";
    const auto current = directory / "runtime.log";
    if (std::filesystem::exists(previous, error)) {
        static std::atomic<unsigned> sequence{0};
        const auto stamp =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        std::filesystem::path target;
        do {
            target =
                archive / ("runtime-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)) + ".log");
        } while (std::filesystem::exists(target, error) && !error);
        if (error)
            return false;
        std::filesystem::rename(previous, target, error);
        if (error)
            return false;
    }
    error.clear();
    if (std::filesystem::exists(current, error)) {
        std::filesystem::rename(current, previous, error);
        if (error)
            return false;
    }
    if (error)
        return false;
    prune(archive);
    return true;
}
// A support export is bounded and read-only with respect to the original log.
inline void snapshot(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::ifstream input(source, std::ios::binary | std::ios::ate);
    if (!input)
        return;
    const auto length = input.tellg();
    if (length < 0)
        return;
    std::ofstream output(target, std::ios::binary);
    if (!output)
        return;
    constexpr std::streamoff half = 1024 * 1024;
    const auto copy = [&](std::streamoff start, std::streamoff count) {
        input.seekg(start);
        char buffer[16384];
        while (count > 0 && input) {
            input.read(buffer, std::min<std::streamoff>(sizeof(buffer), count));
            output.write(buffer, input.gcount());
            count -= input.gcount();
        }
    };
    if (length <= 2 * half)
        copy(0, length);
    else {
        copy(0, half);
        output << "\n[diagnostics: middle omitted; original retained in logs]\n";
        copy(length - half, half);
    }
}
} // namespace dkr::runtime::support::history
