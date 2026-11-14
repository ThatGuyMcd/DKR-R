#pragma once
#include "magic_code_library.hpp"
#include "magic_code_trophy_catalog.hpp"
#include "../atomic_snapshot.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace dkr::mods::code {
struct LibraryView {
    std::vector<LibraryItem> items;
    std::string operation, error;
    bool busy = false;
    std::uint64_t generation = 0;
};
struct CupFileEntry {
    std::filesystem::path path;
    bool directory = false;
};
struct CupDirectory {
    std::filesystem::path path;
    std::vector<CupFileEntry> entries;
    bool limited = false;
};
// One bounded storage job at a time. Readers take an immutable snapshot; no
// filesystem, archive parsing or joins occur in a launcher/overlay draw call.
class LibraryJobs {
  public:
    LibraryJobs();
    ~LibraryJobs();
    LibraryJobs(const LibraryJobs&) = delete;
    LibraryJobs& operator=(const LibraryJobs&) = delete;
    void configure(std::filesystem::path root, std::string version);
    std::shared_ptr<const LibraryView> view() const;
    bool refresh();
    bool import_file(std::filesystem::path);
    bool update_trophies(std::filesystem::path);
    bool select(std::string digest);
    bool approve(std::string digest, bool);
    bool enable(std::string digest, bool);
    bool settings(std::string digest, Json);
    bool data(std::string digest, Json);
    bool remove(std::string digest);
    bool load_courses(std::filesystem::path rom, std::filesystem::path legacy, std::vector<std::filesystem::path> labs);
    std::shared_ptr<const std::vector<trophy::Course>> courses() const {
        return courses_.load();
    }
    bool import_cups(std::filesystem::path);
    bool export_cups(std::filesystem::path, std::vector<trophy::Definition>);
    std::shared_ptr<const trophy::SharedCups> imported_cups() const {
        return imported_cups_.load();
    }
    bool browse_cups(std::filesystem::path);
    std::shared_ptr<const CupDirectory> cup_directory() const {
        return cup_directory_.load();
    }

  private:
    using Job = std::function<void(Library&, std::stop_token)>;
    bool submit(std::string operation, Job, bool rescan = true);
    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::string version_;
    dkr::runtime::AtomicSnapshot<const LibraryView> view_;
    std::jthread worker_;
    bool running_ = false;
    dkr::runtime::AtomicSnapshot<const std::vector<trophy::Course>> courses_;
    dkr::runtime::AtomicSnapshot<const trophy::SharedCups> imported_cups_;
    dkr::runtime::AtomicSnapshot<const CupDirectory> cup_directory_;
};
} // namespace dkr::mods::code
