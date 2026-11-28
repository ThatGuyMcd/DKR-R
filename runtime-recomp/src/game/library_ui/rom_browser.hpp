#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace dkr::library_ui {

inline std::u8string folded_filename(const std::filesystem::path& path) {
    auto name = path.u8string();
    for (auto& letter : name)
        if (letter >= u8'A' && letter <= u8'Z')
            letter += u8'a' - u8'A';
    return name;
}

inline bool is_rom_file(const std::filesystem::path& path) {
    const auto ext = folded_filename(path.extension());
    return ext == u8".z64" || ext == u8".v64" || ext == u8".n64";
}

class RomBrowser {
  public:
    struct Entry {
        std::filesystem::path path;
        bool directory;
    };

    void open(std::filesystem::path directory, std::vector<Entry> locations) {
        locations_ = std::move(locations);
        history_.clear();
        directory_ = std::move(directory);
        refresh({});
    }

    void visit(std::filesystem::path directory) {
        if (directory == directory_)
            return;
        history_.push_back({directory_, directory});
        auto focus = directory.parent_path() == directory_ ? std::filesystem::path{} : directory_;
        directory_ = std::move(directory);
        refresh(focus);
    }

    void up() {
        if (directory_.empty())
            return;
        auto parent = directory_.parent_path();
        visit(parent == directory_ ? std::filesystem::path{} : std::move(parent));
    }

    bool back() {
        if (history_.empty())
            return false;
        auto previous = std::move(history_.back());
        history_.pop_back();
        directory_ = std::move(previous.directory);
        refresh(previous.focus);
        return true;
    }

    const std::filesystem::path& directory() const {
        return directory_;
    }
    const std::vector<Entry>& entries() const {
        return entries_;
    }
    const std::error_code& error() const {
        return error_;
    }
    std::optional<int> take_focus_row() {
        return std::exchange(focus_row_, std::nullopt);
    }

  private:
    struct Visit {
        std::filesystem::path directory;
        std::filesystem::path focus;
    };

    void refresh(const std::filesystem::path& focus) {
        entries_.clear();
        error_.clear();
        if (directory_.empty()) {
            entries_ = locations_;
        } else {
            for (std::filesystem::directory_iterator it(directory_, error_), end; it != end && !error_;
                 it.increment(error_)) {
                std::error_code entry_error;
                if (it->is_directory(entry_error))
                    entries_.push_back({it->path(), true});
                else if (is_rom_file(it->path()) && it->is_regular_file(entry_error))
                    entries_.push_back({it->path(), false});
            }
            std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
                if (a.directory != b.directory)
                    return a.directory;
                const auto left = folded_filename(a.path.filename()), right = folded_filename(b.path.filename());
                return left == right ? a.path < b.path : left < right;
            });
        }
        const auto row =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.path == focus; });
        focus_row_ = row == entries_.end() ? 0 : static_cast<int>(row - entries_.begin());
    }

    std::filesystem::path directory_;
    std::vector<Entry> locations_;
    std::vector<Entry> entries_;
    std::vector<Visit> history_;
    std::error_code error_;
    std::optional<int> focus_row_;
};

} // namespace dkr::library_ui
