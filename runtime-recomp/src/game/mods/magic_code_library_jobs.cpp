#include "magic_code_library_jobs.hpp"
#include "online_mod_bundle.hpp"
#include <utility>
#include <algorithm>
#include <cctype>

namespace dkr::mods::code {
LibraryJobs::LibraryJobs() {
    view_.store(std::make_shared<const LibraryView>());
    courses_.store(std::make_shared<const std::vector<trophy::Course>>());
    imported_cups_.store(std::make_shared<const trophy::SharedCups>());
    cup_directory_.store(std::make_shared<const CupDirectory>());
}
LibraryJobs::~LibraryJobs() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}
void LibraryJobs::configure(std::filesystem::path root, std::string version) {
    // Configuration is a launch/return lifecycle boundary, never a draw call.
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    {
        std::lock_guard lock(mutex_);
        root_ = std::move(root);
        version_ = std::move(version);
        running_ = false;
    }
    view_.store(std::make_shared<const LibraryView>());
    refresh();
}
std::shared_ptr<const LibraryView> LibraryJobs::view() const {
    return view_.load();
}
bool LibraryJobs::submit(std::string operation, Job job, bool rescan) {
    std::lock_guard lock(mutex_);
    if (running_ || root_.empty())
        return false;
    // A completed worker has published its final snapshot and relinquished
    // running_ under this mutex. Joining it cannot wait for storage work.
    if (worker_.joinable())
        worker_.join();
    auto next = std::make_shared<LibraryView>(*view_.load());
    next->busy = true;
    next->operation = std::move(operation);
    next->error.clear();
    view_.store(next);
    running_ = true;
    const auto root = root_;
    const auto version = version_;
    worker_ = std::jthread([this, root, version, job = std::move(job), rescan](std::stop_token stop) {
        auto result = std::make_shared<LibraryView>(*view_.load());
        try {
            Library library(root, version);
            job(library, stop);
            if (rescan && !stop.stop_requested())
                result->items = library.scan(stop);
        } catch (const std::exception& e) {
            result->error = e.what();
        } catch (...) {
            result->error = "Custom Magic Codes could not complete the storage operation.";
        }
        result->busy = false;
        result->operation.clear();
        ++result->generation;
        // Publish before releasing running_. submit() never joins a worker
        // that is still waiting for this mutex.
        std::lock_guard done(mutex_);
        view_.store(std::move(result));
        running_ = false;
    });
    return true;
}
bool LibraryJobs::refresh() {
    return submit("Reading Custom Magic Codes", [](Library&, std::stop_token) {});
}
bool LibraryJobs::import_file(std::filesystem::path path) {
    return submit("Importing Custom Magic Code", [path = std::move(path)](Library& l, std::stop_token stop) {
        if (stop.stop_requested())
            return;
        online::check_storage(path, false);
        const auto bytes = read_file(path, MaxPackage);
        if (!stop.stop_requested())
            l.import(bytes);
    });
}
bool LibraryJobs::select(std::string id) {
    return submit("Selecting edition", [id = std::move(id)](Library& l, std::stop_token) { l.select(id); });
}
bool LibraryJobs::update_trophies(std::filesystem::path path) {
    return submit("Updating Custom Trophy Races", [path = std::move(path)](Library& library, std::stop_token stop) {
        if (stop.stop_requested())
            return;
        online::check_storage(path, false);
        const auto bytes = read_file(path, MaxPackage);
        if (sha256(bytes) != TrophyUpdateDigest)
            throw Error(
                "The bundled trophy update differs from this build. Import the correct 1.0.2 package manually.");
        if (stop.stop_requested())
            return;
        const auto digest = library.import(bytes);
        library.select(digest); // Existing cups migrate; new code remains OFF and unapproved.
    });
}
bool LibraryJobs::approve(std::string id, bool value) {
    return submit("Updating code permission",
                  [id = std::move(id), value](Library& l, std::stop_token) { l.approve(id, value); });
}
bool LibraryJobs::enable(std::string id, bool value) {
    return submit("Updating code selection",
                  [id = std::move(id), value](Library& l, std::stop_token) { l.enable(id, value); });
}
bool LibraryJobs::settings(std::string id, Json values) {
    return submit("Saving code settings", [id = std::move(id), values = std::move(values)](
                                              Library& l, std::stop_token) { l.settings(id, values); });
}
bool LibraryJobs::data(std::string id, Json values) {
    return submit("Saving code configuration", [id = std::move(id), values = std::move(values)](
                                                   Library& l, std::stop_token) { l.data(id, values); });
}
bool LibraryJobs::remove(std::string id) {
    return submit("Removing code from library", [id = std::move(id)](Library& l, std::stop_token) { l.remove(id); });
}
bool LibraryJobs::load_courses(std::filesystem::path rom, std::filesystem::path legacy,
                               std::vector<std::filesystem::path> labs) {
    return submit("Reading enabled cup courses", [this, rom = std::move(rom), legacy = std::move(legacy),
                                                  labs = std::move(labs)](Library&, std::stop_token stop) {
        courses_.store(
            std::make_shared<const std::vector<trophy::Course>>(trophy::installed_catalogue(rom, legacy, labs, stop)));
    });
}
bool LibraryJobs::import_cups(std::filesystem::path path) {
    return submit("Reading trophy cups", [this, path = std::move(path)](Library&, std::stop_token stop) {
        if (stop.stop_requested())
            return;
        online::check_storage(path, false);
        auto shared = trophy::read_share(read_file(path, trophy::MaxSharedCups));
        if (!stop.stop_requested())
            imported_cups_.store(std::make_shared<const trophy::SharedCups>(std::move(shared)));
    });
}
bool LibraryJobs::export_cups(std::filesystem::path path, std::vector<trophy::Definition> cups) {
    const auto courses = courses_.load();
    return submit("Exporting trophy cups",
                  [path = std::move(path), cups = std::move(cups), courses](Library&, std::stop_token stop) {
                      const auto bytes = trophy::share(cups, *courses);
                      if (!stop.stop_requested())
                          Library::export_cups(path, bytes);
                  });
}
bool LibraryJobs::browse_cups(std::filesystem::path path) {
    return submit(
        "Reading cup folder",
        [this, path = std::move(path)](Library&, std::stop_token stop) {
            auto result = std::make_shared<CupDirectory>();
            result->path = std::filesystem::canonical(path);
            // No recursion or archive inspection. Bound work even for huge folders.
            std::size_t visited = 0;
            for (const auto& entry : std::filesystem::directory_iterator(result->path)) {
                if (stop.stop_requested())
                    return;
                if (++visited > 8192) {
                    result->limited = true;
                    break;
                }
                std::error_code error;
                if (entry.is_symlink(error) || error)
                    continue;
                const bool directory = entry.is_directory(error);
                if (error)
                    continue;
                auto extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                if (!directory && (!entry.is_regular_file(error) || error || extension != ".dkrcup"))
                    continue;
                if (result->entries.size() >= 2048) {
                    result->limited = true;
                    break;
                }
                result->entries.push_back({entry.path(), directory});
            }
            std::sort(result->entries.begin(), result->entries.end(), [](const auto& a, const auto& b) {
                return a.directory != b.directory ? a.directory > b.directory : a.path.filename() < b.path.filename();
            });
            if (!stop.stop_requested())
                cup_directory_.store(std::move(result));
        },
        false);
}
} // namespace dkr::mods::code
