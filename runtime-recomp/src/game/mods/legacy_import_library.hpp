#pragma once
#include "legacy_mod_format.hpp"
#include <memory>
#include <mutex>
#include <thread>

namespace dkr::mods {
struct ImportReviewItem {
    std::string id, name, revision, kind;
    std::vector<std::string> warnings;
    std::string review;
};
struct ImportLibraryView {
    bool busy = false, modal = false, succeeded = false;
    unsigned patch = 0, count = 0;
    std::string stage, result;
    std::string imported_review;
    bool imported_tracks = false, imported_characters = false;
    std::vector<ImportReviewItem> items;
};
// UI-independent controller. The decoder only runs in the private executable;
// snapshots never perform disk I/O. Completed reviews are not playable mods.
class ImportLibrary {
  public:
    ImportLibrary() = default;
    ~ImportLibrary();
    ImportLibrary(const ImportLibrary&) = delete;
    ImportLibrary& operator=(const ImportLibrary&) = delete;
    void configure(std::filesystem::path root, std::filesystem::path worker);
    bool import_file(std::filesystem::path source, std::vector<std::filesystem::path> roms);
    bool refresh();
    void cancel();
    void dismiss();
    std::shared_ptr<const ImportLibraryView> snapshot() const;

  private:
    // configure/start/refresh/dismiss are called by the owning UI thread.
    mutable std::mutex mutex_;
    ImportLibraryView view_;
    std::shared_ptr<const ImportLibraryView> published_ = std::make_shared<ImportLibraryView>();
    std::filesystem::path root_, worker_;
    std::jthread thread_;
    void publish(); // mutex held
    bool start(std::filesystem::path source, std::vector<std::filesystem::path> roms, bool scan);
};
} // namespace dkr::mods
