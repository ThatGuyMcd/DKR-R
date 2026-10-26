#include "legacy_import_library.hpp"
#include "legacy_mod_process.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <set>

namespace dkr::mods {
namespace {
using nlohmann::json;
constexpr unsigned MaxReviews = 128, MaxItems = 512;
bool digest(std::string_view s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string text_field(const json& j, const char* key, std::size_t max = 256) {
    const auto value = j.at(key).get<std::string>();
    if (value.size() > max || value.find('\0') != std::string::npos)
        throw Error("Import review contains an invalid text field.");
    return value;
}
std::string path_text(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {s.begin(), s.end()};
}
void ordinary_directory(const std::filesystem::path& p) {
    if (std::filesystem::is_symlink(p) || !std::filesystem::is_directory(p))
        throw Error("The private mod directory must be a normal directory.");
}
void child_directory(const std::filesystem::path& p) {
    std::filesystem::create_directory(p);
    ordinary_directory(p);
}
json read_json(const std::filesystem::path& p, std::size_t max) {
    if (std::filesystem::is_symlink(p) || !std::filesystem::is_regular_file(p))
        throw Error("The import review is missing or is not a regular file.");
    auto bytes = read_file(p, max);
    return json::parse(bytes.begin(), bytes.end(), [](int depth, json::parse_event_t, json&) {
        if (depth > 24)
            throw Error("Import review nesting exceeds its metadata limit.");
        return true;
    });
}
std::vector<std::string> warnings(const json& j) {
    std::vector<std::string> out;
    const auto& values = j.at("blockers");
    if (!values.is_array() || values.size() > 128)
        throw Error("Import review has too many compatibility notes.");
    for (const auto& value : values) {
        const auto s = value.get<std::string>();
        if (s.size() > 1024 || s.find('\0') != std::string::npos)
            throw Error("Invalid compatibility note.");
        out.push_back(s);
    }
    return out;
}
std::vector<ImportReviewItem> review(const std::filesystem::path& directory) {
    ordinary_directory(directory);
    const auto receipt = read_json(directory / "review.json", 8 * MiB);
    if (receipt.at("schema") != Schema || receipt.at("state") != "awaiting-review" ||
        receipt.at("runtime_activated") != false)
        throw Error("Unsupported import review state.");
    const auto& packages = receipt.at("packages");
    if (!packages.is_array() || packages.empty() || packages.size() > 32)
        throw Error("Invalid import review package count.");
    std::vector<ImportReviewItem> out;
    for (const auto& package : packages) {
        if (package.at("enabled") != false || package.at("online_certified") != false)
            throw Error("A review cannot enable game or online content.");
        const auto rev = text_field(package, "source_revision"), asset = text_field(package, "asset_sha256");
        if ((rev != "us.v77" && rev != "us.v80") || !digest(asset))
            throw Error("Invalid import review identity.");
        auto notes = warnings(package);
        const auto& tracks = package.at("tracks");
        const auto& characters = package.at("characters");
        if (!tracks.is_array() || tracks.size() > 64 || !characters.is_array() || characters.size() > 64)
            throw Error("Import review has too many content entries.");
        for (const auto& track : tracks) {
            const auto id = text_field(track, "id"), name = text_field(track, "name");
            if (!digest(id))
                throw Error("Invalid track identity.");
            auto local = warnings(track);
            local.insert(local.end(), notes.begin(), notes.end());
            out.push_back({id, name, rev, "Track", std::move(local)});
        }
        unsigned i = 0;
        for (const auto& character : characters) {
            auto name = character.get<std::string>();
            if (name.size() > 256 || name.find('\0') != std::string::npos)
                throw Error("Invalid character name.");
            out.push_back({asset + "-character-" + std::to_string(i++), std::move(name), rev, "Character", notes});
        }
        if (out.size() > MaxItems)
            throw Error("Import review exceeds the library item limit.");
    }
    return out;
}
std::vector<ImportReviewItem> scan_library(const std::filesystem::path& root, std::stop_token stop) {
    std::vector<ImportReviewItem> out;
    std::set<std::string> seen;
    unsigned count = 0, examined = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root / "reviews")) {
        if (stop.stop_requested())
            throw Error("Import cancelled.");
        if (++examined > 1024)
            throw Error("The private review directory has too many entries.");
        if (!digest(path_text(entry.path().filename())))
            continue;
        if (++count > MaxReviews)
            throw Error("The import review library is full (128 reviews).");
        ordinary_directory(entry.path());
        if (std::filesystem::is_symlink(entry.path() / "review.json"))
            throw Error("A saved review must not be a link.");
        const auto receipt = read_file(entry.path() / "review.json", 8 * MiB);
        if (sha256(receipt) != path_text(entry.path().filename()))
            throw Error("A saved import review has changed; it was not loaded.");
        for (auto& item : review(entry.path())) {
            item.review = path_text(entry.path().filename());
            if (seen.insert(item.id).second)
                out.push_back(std::move(item));
            if (out.size() > MaxItems)
                throw Error("The review library has exceeded 512 content entries.");
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}
// Only an exclusively-created child of jobs can be cleaned up. Do not follow
// a substituted directory link or use a patch/member name as a filesystem path.
void remove_job(const std::filesystem::path& root, const std::filesystem::path& job) noexcept {
    try {
        if (job.empty() || job.parent_path() != root / "jobs" || !digest(path_text(job.filename())))
            return;
        ordinary_directory(root);
        ordinary_directory(root / "jobs");
        ordinary_directory(job);
        if (std::filesystem::weakly_canonical(job).parent_path() != std::filesystem::weakly_canonical(root / "jobs"))
            return;
        std::filesystem::remove_all(job);
    } catch (...) {
    } // An incomplete job is never enumerated as an installed review.
}
} // namespace
ImportLibrary::~ImportLibrary() {
    cancel();
    if (thread_.joinable())
        thread_.join();
}
void ImportLibrary::publish() {
    published_ = std::make_shared<ImportLibraryView>(view_);
}
std::shared_ptr<const ImportLibraryView> ImportLibrary::snapshot() const {
    std::scoped_lock lock(mutex_);
    return published_;
}
void ImportLibrary::configure(std::filesystem::path root, std::filesystem::path worker) {
    cancel();
    if (thread_.joinable())
        thread_.join();
    root_ = private_storage_path(root);
    worker_ = std::move(worker);
    {
        std::scoped_lock lock(mutex_);
        view_ = {};
        publish();
    }
    refresh();
}
void ImportLibrary::cancel() {
    if (thread_.joinable())
        thread_.request_stop();
}
void ImportLibrary::dismiss() {
    std::scoped_lock lock(mutex_);
    if (!view_.busy) {
        view_.modal = false;
        publish();
    }
}
bool ImportLibrary::refresh() {
    return start({}, {}, true);
}
bool ImportLibrary::import_file(std::filesystem::path source, std::vector<std::filesystem::path> roms) {
    return start(std::move(source), std::move(roms), false);
}
bool ImportLibrary::start(std::filesystem::path source, std::vector<std::filesystem::path> roms, bool scan) {
    {
        std::scoped_lock lock(mutex_);
        if (view_.busy)
            return false;
    }
    if (thread_.joinable())
        thread_.join();
    {
        std::scoped_lock lock(mutex_);
        view_.busy = true;
        view_.modal = !scan;
        view_.succeeded = false;
        view_.patch = 0;
        view_.count = 0;
        view_.imported_review.clear();
        view_.imported_tracks = false;
        view_.imported_characters = false;
        view_.stage = scan ? "Reading imported content" : "Preparing legacy import";
        view_.result.clear();
        publish();
    }
    try {
        thread_ = std::jthread([this, source = std::move(source), roms = std::move(roms), scan](std::stop_token stop) {
            std::filesystem::path job;
            try {
                std::filesystem::create_directories(root_);
                ordinary_directory(root_);
                child_directory(root_ / "jobs");
                child_directory(root_ / "reviews");
                auto items = scan_library(root_, stop);
                bool duplicate = false;
                std::string imported_review;
                bool imported_tracks = false, imported_characters = false;
                if (!scan) {
                    if (roms.empty() || roms.size() > 8)
                        throw Error("Import an original Game Pak in the Play tab first (maximum eight source files).");
                    if (!worker_.is_absolute() || !std::filesystem::is_regular_file(worker_))
                        throw Error("The packaged mod importer is missing. Re-extract the complete DKR-R package.");
                    unsigned reviews = 0;
                    for (const auto& entry : std::filesystem::directory_iterator(root_ / "reviews"))
                        if (digest(path_text(entry.path().filename())))
                            ++reviews;
                    if (reviews >= MaxReviews)
                        throw Error("The import review library is full (128 reviews).");
                    std::random_device random;
                    Bytes nonce(32);
                    for (auto& byte : nonce)
                        byte = static_cast<std::uint8_t>(random());
                    job = root_ / "jobs" / sha256(nonce);
                    if (!std::filesystem::create_directory(job))
                        throw Error("Could not reserve a new private import job.");
                    json request = {{"schema", Schema},
                                    {"source", path_text(std::filesystem::absolute(source))},
                                    {"roms", json::array()}};
                    for (const auto& rom : roms)
                        request["roms"].push_back(path_text(std::filesystem::absolute(rom)));
                    auto encoded = request.dump();
                    if (encoded.size() > 64 * 1024)
                        throw Error("The importer request exceeds its size limit.");
                    write_new_file(job / "request.json",
                                   View(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size()));
                    auto next_poll = std::chrono::steady_clock::now();
                    const auto result = run_worker(worker_, job / "request.json", [&] {
                        if (stop.stop_requested())
                            return false;
                        const auto now = std::chrono::steady_clock::now();
                        if (now < next_poll)
                            return true;
                        next_poll = now + std::chrono::milliseconds(200);
                        const auto path = job / "progress.jsonl";
                        if (!std::filesystem::exists(path))
                            return true;
                        const auto bytes = read_file(path, 128 * 1024);
                        // Ignore the final partial line while the worker appends.
                        const std::string data(bytes.begin(), bytes.end());
                        const auto end = data.find_last_of('\n');
                        if (end == std::string::npos)
                            return true;
                        const auto before = end ? data.find_last_of('\n', end - 1) : std::string::npos;
                        const auto begin = before == std::string::npos ? 0 : before + 1;
                        const auto event = json::parse(data.substr(begin, end - begin));
                        const auto stage = text_field(event, "stage");
                        const unsigned patch = event.at("patch"), count = event.at("count");
                        if (count > 32 || patch > count)
                            throw Error("Invalid import progress event.");
                        std::scoped_lock lock(mutex_);
                        view_.stage = stage;
                        view_.patch = patch;
                        view_.count = count;
                        publish();
                        return true;
                    });
                    if (result.outcome == WorkerOutcome::Cancelled || stop.stop_requested())
                        throw Error("Import cancelled. Stock tracks, ROMs and saves were not changed.");
                    if (result.outcome == WorkerOutcome::TimedOut)
                        throw Error("Import exceeded its time budget and was stopped safely.");
                    if (result.outcome != WorkerOutcome::Completed) {
                        if (std::filesystem::exists(job / "failure.json"))
                            throw Error(text_field(read_json(job / "failure.json", 8192), "error", 1024));
                        throw Error(
                            "The isolated importer stopped unexpectedly. The game and existing content were not changed.");
                    }
                    auto incoming = review(job / "content");
                    std::set<std::string> ids;
                    for (const auto& item : items)
                        ids.insert(item.id);
                    for (const auto& item : incoming)
                        ids.insert(item.id);
                    if (ids.size() > MaxItems)
                        throw Error("This import would exceed the 512-entry review library limit.");
                    const auto bytes = read_file(job / "content" / "review.json", 8 * MiB);
                    const auto destination = root_ / "reviews" / sha256(bytes);
                    imported_review = sha256(bytes);
                    for (const auto& item : incoming) {
                        imported_tracks |= item.kind == "Track";
                        imported_characters |= item.kind == "Character";
                    }
                    if (stop.stop_requested())
                        throw Error("Import cancelled before the review was saved.");
                    if (std::filesystem::exists(destination)) {
                        ordinary_directory(destination);
                        if (read_file(destination / "review.json", 8 * MiB) != bytes)
                            throw Error("An existing review failed its identity check.");
                        duplicate = true;
                    } else {
                        // Atomic same-filesystem directory publication; never overwrites.
                        std::filesystem::rename(job / "content", destination);
                    }
                    // Cancellation after publication cannot turn a saved review
                    // into a reported cancelled/half-installed transaction.
                    items = scan_library(root_, {});
                }
                remove_job(root_, job);
                std::scoped_lock lock(mutex_);
                view_.items = std::move(items);
                view_.busy = false;
                view_.succeeded = true;
                view_.imported_review = std::move(imported_review);
                view_.imported_tracks = imported_tracks;
                view_.imported_characters = imported_characters;
                view_.stage = "Import review ready";
                view_.result =
                    scan ? "Library refreshed."
                    : duplicate
                        ? "This content is already in the review library. No duplicate was added."
                        : "Content identified and saved. Compatible assets can now be prepared without modifying original Game Paks or saves.";
                publish();
            } catch (const std::exception& e) {
                remove_job(root_, job);
                std::scoped_lock lock(mutex_);
                view_.busy = false;
                view_.succeeded = false;
                view_.result = std::string(e.what()).substr(0, 1024);
                view_.stage = "Import stopped safely";
                publish();
            } catch (...) {
                remove_job(root_, job);
                std::scoped_lock lock(mutex_);
                view_.busy = false;
                view_.succeeded = false;
                view_.result = "Import stopped safely after an unexpected error.";
                publish();
            }
        });
    } catch (const std::exception& e) {
        std::scoped_lock lock(mutex_);
        view_.busy = false;
        view_.result = e.what();
        publish();
        return false;
    }
    return true;
}
} // namespace dkr::mods
