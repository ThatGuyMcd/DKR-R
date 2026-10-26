#include "store_vehicle_adapter.hpp"
#include "shared_snapshot.hpp"
#include "mod_store_install.hpp"
#include "custom_tracks.hpp"
#include "mods/legacy_mod_library.hpp"
#include "mods/magic_code_library.hpp"
#include "runtime_texture_packs.hpp"
#include "updates/update_core.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dkr::store {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t MaxPackage = 2ULL * 1024 * 1024 * 1024, MaxTree = 8ULL * 1024 * 1024 * 1024;
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct StoreLease {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    explicit StoreLease(const fs::path& path) {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw Failure("Another DKR-R copy is changing this mod library. Finish there, then retry.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            info.nNumberOfLinks != 1) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw Failure("The mod operation lock is unsafe.");
        }
#else
        handle = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0 || flock(handle, LOCK_EX | LOCK_NB)) {
            if (handle >= 0)
                close(handle);
            handle = -1;
            throw Failure("Another DKR-R copy is changing this mod library. Finish there, then retry.");
        }
        struct stat info{};
        if (fstat(handle, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != geteuid()) {
            close(handle);
            handle = -1;
            throw Failure("The mod operation lock is unsafe.");
        }
#endif
    }
    ~StoreLease() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (handle >= 0)
            close(handle);
#endif
    }
};
void sync_directory(const fs::path& path) {
#ifndef _WIN32
    const int handle = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const bool ok = handle >= 0 && fsync(handle) == 0;
    if (handle >= 0)
        close(handle);
    if (!ok)
        throw Failure("Could not safely flush mod storage. The installation was not committed.");
#else
    (void)path; // File flushes and write-through renames provide the Windows barrier.
#endif
}
void sync_file(const fs::path& path) {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool ok = handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle);
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
#else
    const int handle = open(path.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    const bool ok = handle >= 0 && fsync(handle) == 0;
    if (handle >= 0)
        close(handle);
#endif
    if (!ok)
        throw Failure("Could not safely flush the mod copy. Check the storage device.");
}
std::string path_text(const fs::path& path) {
    const auto s = path.generic_u8string();
    return {s.begin(), s.end()};
}
std::string uuid() {
    std::random_device random;
    std::array<std::uint8_t, 32> bytes{};
    for (auto& b : bytes)
        b = std::uint8_t(random());
    return dkr::updates::hash(bytes).substr(0, 32);
}
bool hash_string(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == s.npos;
}
bool instance_id(const std::string& s) {
    return s.size() == 32 && s.find_first_not_of("0123456789abcdef") == s.npos;
}
bool path_exists(const fs::path& p) {
    std::error_code e;
    return fs::symlink_status(store_io_path(p), e).type() != fs::file_type::not_found && !e;
}
void ordinary(const fs::path& input, bool directory) {
    const auto path = store_io_path(input);
    const auto s = fs::symlink_status(path);
    if (fs::is_symlink(s) || (directory ? !fs::is_directory(s) : !fs::is_regular_file(s)))
        throw Failure("The mod storage contains an unsafe or missing path.");
#ifdef _WIN32
    const auto flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || flags & FILE_ATTRIBUTE_REPARSE_POINT)
        throw Failure("Mod storage must not contain junctions or reparse points.");
#endif
}
fs::path child(const fs::path& root, const std::string& relative) {
    auto p = fs::u8path(relative);
    if (relative.empty() || p.is_absolute() || p.has_root_name())
        throw Failure("Invalid managed mod path.");
    for (const auto& part : p)
        if (part == "." || part == ".." || path_text(part).find(':') != std::string::npos)
            throw Failure("Invalid managed mod path component.");
    auto at = root;
    ordinary(root, true);
    for (auto it = p.begin(); it != p.end(); ++it) {
        at /= *it;
        if (path_exists(at))
            ordinary(at, fs::is_directory(store_io_path(at)));
    }
    return at;
}
void directories(const fs::path& path) {
    if (path_exists(path)) {
        ordinary(path, true);
        return;
    }
    directories(path.parent_path());
    fs::create_directory(store_io_path(path));
    sync_directory(store_io_path(path.parent_path()));
    ordinary(path, true);
}
Json read(const fs::path& path, std::size_t max = 16 * 1024 * 1024) {
    if (!path_exists(path))
        return Json::object();
    ordinary(path, false);
    return Json::parse(dkr::updates::read_text(store_io_path(path), max));
}
void write(const fs::path& path, const Json& j) {
    directories(path.parent_path());
    dkr::updates::durable_write(store_io_path(path), j.dump());
}
std::uint64_t tree_size(const fs::path& input) {
    const auto path = store_io_path(input);
    ordinary(path, fs::is_directory(path));
    if (fs::is_regular_file(path))
        return fs::file_size(path);
    std::uint64_t total = 0;
    std::size_t count = 0;
    for (const auto& e : fs::recursive_directory_iterator(path)) {
        ordinary(e.path(), e.is_directory());
        if (++count > 250000)
            throw Failure("This mod has too many files.");
        if (e.is_regular_file()) {
            const auto n = e.file_size();
            if (n > MaxTree - total)
                throw Failure("This mod exceeds its storage limit.");
            total += n;
        }
    }
    return total;
}
void copy_plain(const fs::path& source_path, const fs::path& target_path, std::stop_token stop = {},
                bool durable = true) {
    const auto source = store_io_path(source_path), target = store_io_path(target_path);
    ordinary(source, fs::is_directory(source));
    if (stop.stop_requested())
        throw Failure("Mod operation cancelled.");
    if (path_exists(target))
        throw Failure("A private staging destination is already in use.");
    directories(target.parent_path());
    if (fs::is_directory(source)) {
        fs::create_directory(target);
        for (const auto& e : fs::directory_iterator(source))
            copy_plain(e.path(), target / e.path().filename(), stop, durable);
        if (durable)
            sync_directory(target);
    } else {
        std::ifstream in(source, std::ios::binary);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!in || !out)
            throw Failure("Could not open mod storage for copying.");
        const auto capacity = std::min<std::uint64_t>(1024 * 1024, std::max<std::uint64_t>(1, fs::file_size(source)));
        std::vector<char> buffer(static_cast<std::size_t>(capacity));
        std::uint64_t total = 0;
        while (in) {
            if (stop.stop_requested())
                throw Failure("Mod operation cancelled.");
            in.read(buffer.data(), std::streamsize(buffer.size()));
            const auto n = in.gcount();
            if (n < 0 || std::uint64_t(n) > MaxTree - total)
                throw Failure("A mod file exceeds its storage limit.");
            total += std::uint64_t(n);
            out.write(buffer.data(), n);
            if (!out)
                throw Failure("Could not copy the mod. Check free disk space.");
        }
        if (!in.eof())
            throw Failure("The source mod could not be read completely.");
        out.close();
        if (!out || total != fs::file_size(source))
            throw Failure("The mod copy was incomplete.");
        if (durable) {
            sync_file(target);
            sync_directory(target.parent_path());
        }
    }
}
std::string tree_hash(const fs::path& input, std::stop_token stop = {}) {
    const auto path = store_io_path(input);
    if (fs::is_regular_file(path))
        return dkr::updates::hash_file(path, MaxTree);
    std::map<std::string, std::string> files;
    tree_size(path);
    for (const auto& e : fs::recursive_directory_iterator(path)) {
        if (stop.stop_requested())
            throw Failure("Mod operation cancelled.");
        if (e.is_regular_file())
            files[path_text(e.path().lexically_relative(path))] = dkr::updates::hash_file(e.path(), MaxTree);
    }
    const auto bytes = Json(files).dump();
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
std::string track_hash(const fs::path& input, std::stop_token stop = {}) {
    const auto path = store_io_path(input);
    std::map<std::string, std::string> files;
    tree_size(path);
    for (const auto& e : fs::recursive_directory_iterator(path)) {
        if (stop.stop_requested())
            throw Failure("Mod operation cancelled.");
        const auto name = path_text(e.path().lexically_relative(path));
        // This first-party activation marker is not authored track content.
        if (name == ".online-keep-state")
            continue;
        if (e.is_regular_file())
            files[name] = dkr::updates::hash_file(e.path(), MaxTree);
    }
    const auto bytes = Json(files).dump();
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
std::string name_stamp(const fs::path& input) {
    const auto path = store_io_path(input);
    std::map<std::string, std::string> entries;
    ordinary(path, fs::is_directory(path));
    if (fs::is_regular_file(path))
        entries["file"] = std::to_string(fs::file_size(path)) + ":" +
                          std::to_string(static_cast<std::int64_t>(fs::last_write_time(path).time_since_epoch().count()));
    else
        for (const auto& e : fs::directory_iterator(path)) {
            ordinary(e.path(), false);
            if (entries.size() >= 200005)
                return {};
            entries[path_text(e.path().filename())] =
                std::to_string(e.file_size()) + ":" + std::to_string(static_cast<std::int64_t>(e.last_write_time().time_since_epoch().count()));
        }
    const auto bytes = Json(entries).dump();
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
std::string track_stamp(const fs::path& input) {
    const auto path = store_io_path(input);
    std::map<std::string, std::string> entries;
    tree_size(path);
    for (const auto& e : fs::recursive_directory_iterator(path))
        if (e.is_regular_file())
            entries[path_text(e.path().lexically_relative(path))] =
                std::to_string(e.file_size()) + ":" + std::to_string(static_cast<std::int64_t>(e.last_write_time().time_since_epoch().count()));
    const auto bytes = Json(entries).dump();
    return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
void discard(const fs::path& root, const fs::path& path) {
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative == "." || *relative.begin() == ".." || relative.is_absolute())
        throw Failure("Refused to remove a path outside the mod transaction.");
    if (path_exists(path)) {
        tree_size(path);
        fs::remove_all(store_io_path(path));
    }
}
std::string name_key(std::string s) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 128 || std::isalnum(c))
            out += char(std::tolower(c));
    return out;
}
bool managed_target(const std::string& s) {
    // Explicit ownership roots only. Never saves, ROMs, authoring folders or
    // arbitrary publisher-supplied paths.
    if (s.empty() || s.back() == '/' || s.find("..") != s.npos || s.find_first_of("\\:") != s.npos)
        return false;
    const auto leaf_after = [&](std::string_view prefix) {
        if (!s.starts_with(prefix))
            return std::string{};
        auto leaf = s.substr(prefix.size());
        if (leaf.empty() || leaf[0] == '.' || leaf.find('/') != leaf.npos)
            return std::string{};
        return leaf;
    };
    if (!leaf_after("texture-packs/").empty() || !leaf_after("custom-tracks/").empty())
        return true;
    for (const auto* prefix : {"mods/legacy/prepared/", "mods/legacy/prepared-characters/", "mods/legacy/reviews/"})
        if (hash_string(leaf_after(prefix)))
            return true;
    const auto car = leaf_after("mods/vehicles/packages/");
    for(const auto* suffix : {".dkrkart", ".dkrhover", ".dkrplane"})
        if(car.ends_with(suffix) && hash_string(car.substr(0, car.size()-std::char_traits<char>::length(suffix)))) return true;
    const auto portrait = leaf_after("mods/vehicles/portraits/");
    if(portrait.ends_with(".rgba") && hash_string(portrait.substr(0,portrait.size()-5))) return true;
    if(s=="mods/vehicles/library.json") return true;
    const auto code = leaf_after("mods/custom-magic-codes/packages/");
    if (code.ends_with(".dkrcode") && hash_string(code.substr(0, code.size() - 8)))
        return true;
    return s == "mods/custom-magic-codes/library.json" || s == "texture-packs.ini" ||
           s == "texture-packs-index-v1.json" || s == "custom-tracks-enabled.json" || s == "custom-tracks-state.txt" ||
           s == "mods/legacy/track-catalog.json" || s == "mods/legacy/character-catalog.json" ||
           s == "mods/legacy/track-library.json" || s == "mods/legacy/character-library.json";
}
std::string canonical_member_path(const Json& member) {
    if (!member.is_object() || !member.contains("path") || !member["path"].is_string())
        return {};
    auto path = member["path"].get<std::string>();
    if (path.size() > 4096 || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; }))
        return {};
    // Native Windows inventory used backslashes in beta 8. Receipts use one
    // portable representation; do not resolve dot components or absolute paths.
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
bool managed_member(const Json& member, const std::string& path) {
    if (!managed_target(path))
        return false;
    const auto kind = text(member, "kind", 40);
    if(kind=="vehicle") {
        const auto digest=text(member,"digest",64);
        if(!hash_string(digest)||text(member,"id",256)!="vehicle:"+digest||text(member,"key",512)!="vehicle:"+digest) return false;
        for(const auto* suffix:{".dkrkart",".dkrhover",".dkrplane"})
            if(path=="mods/vehicles/packages/"+digest+suffix) return true;
        return false;
    }
    return (kind == "texture" && path.starts_with("texture-packs/")) ||
           (kind == "track" && path.starts_with("custom-tracks/")) ||
           (kind == "code" && path.starts_with("mods/custom-magic-codes/packages/")) ||
           (kind == "legacy-track" && path.starts_with("mods/legacy/prepared/")) ||
           (kind == "legacy-character" && path.starts_with("mods/legacy/prepared-characters/"));
}
std::string member_path_identity(const Json& member) {
    auto path = canonical_member_path(member);
#ifdef _WIN32
    for (auto& c : path)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
#endif
    return path;
}
bool same_native_member(const Json& member, const Json& local) {
    if (text(member, "key", 512) != text(local, "key", 512))
        return false;
    // Track IDs are game identities, not folder ownership. Two imported
    // directories can contain the same ID; never silently adopt/delete one
    // merely because the native scanner selected the other.
    return text(member, "kind", 40) != "track" || member_path_identity(member) == member_path_identity(local);
}
void validate_members(Json& record) {
    if (!record.contains("members") || !record["members"].is_array() || record["members"].size() > 1024)
        throw Failure("An installation receipt is invalid; files were left untouched.");
    for (auto& member : record["members"]) {
        const auto path = canonical_member_path(member);
        if (!managed_member(member, path))
            throw Failure("An installation receipt has an unsafe path (" + text(member, "kind", 40) +
                          "): " + text(member, "path", 240) + ". Files were left untouched.");
        if (text(member, "key", 512).empty())
            throw Failure("An installation receipt has an invalid member.");
        member["path"] = path;
    }
}
void validate_registry(Json& value) {
    if (!value.is_object() || value.value("schema", 0) != 1 || !value.contains("records") ||
        !value["records"].is_array() || value["records"].size() > 4096)
        throw Failure("The mod installation registry needs recovery; it was not replaced.");
    std::set<std::string> ids;
    for (auto& record : value["records"]) {
        const auto id = text(record, "instance", 32);
        if (!instance_id(id) || !ids.insert(id).second)
            throw Failure("An installation receipt is invalid; files were left untouched.");
        validate_members(record);
        if (record.contains("previous"))
            validate_members(record["previous"]);
    }
    if (value.contains("ignored") && (!value["ignored"].is_array() || value["ignored"].size() > 8192))
        throw Failure("The installation registry has an invalid unlink list.");
}
} // namespace

Json current_release(const Json& item) {
    if (!item.contains("releases") || !item["releases"].is_array())
        return Json::object();
    const auto explicit_id = text(item, "currentReleaseId", 160);
    for (const auto& r : item["releases"])
        if ((!explicit_id.empty() && text(r, "id", 160) == explicit_id) ||
            (explicit_id.empty() && text(r, "fileUrl", 2048) == text(item, "fileUrl", 2048) &&
             text(r, "version", 80) == text(item, "version", 80)))
            return r;
    return Json::object(); // Never silently choose an arbitrary older release.
}

bool recover_cached_package(const fs::path& folder, const fs::path& target, const std::string& hash,
                            std::uint64_t bytes, std::stop_token stop) {
    ordinary(folder, true);
    if (target.parent_path() != folder || !hash_string(hash))
        throw Failure("Invalid package cache request.");
    if (path_exists(target) && fs::hard_link_count(target) == 1 && fs::file_size(target) == bytes &&
        dkr::updates::hash_file(target, MaxPackage) == hash)
        return true;
    std::vector<fs::path> candidates;
    for (const auto& e : fs::directory_iterator(folder)) {
        if (candidates.size() > 64)
            throw Failure("The package cache has too many aliases.");
        if (e.is_regular_file() && e.path().extension() == target.extension() && e.file_size() == bytes)
            candidates.push_back(e.path());
    }
    for (const auto& candidate : candidates) {
        ordinary(candidate, false);
        if (fs::space(folder).available < bytes + 32ULL * 1024 * 1024)
            throw Failure("Not enough free space to repair the package cache safely.");
        const auto incoming = folder / ("verified-" + uuid() + ".tmp");
        try {
            copy_plain(candidate, incoming, stop);
            if (dkr::updates::hash_file(incoming, MaxPackage) != hash) {
                discard(folder, incoming);
                continue;
            }
            dkr::updates::replace_file(incoming, target);
            return true;
        } catch (...) {
            if (path_exists(incoming))
                discard(folder, incoming);
            throw;
        }
    }
    return false;
}

struct InstallManager::Impl {
    mutable std::mutex mutex;
    std::condition_variable_any cv;
    dkr::runtime::SharedSnapshot<const InstallView> published{std::make_shared<const InstallView>()};
    InstallView state;
    fs::path config, root, worker_path;
    std::string version;
    Json catalogue = Json::array(), inventory = Json::array(), registry = {{"schema", 1}, {"records", Json::array()}};
    Json journal = Json::object();
    StorePrepared prepared;
    Download proposed;
    fs::path transaction;
    bool configured = false, safe = false, refreshing = false;
    std::unique_ptr<StoreLease> lease;
    Json warnings = Json::array();
    std::map<std::string, std::pair<std::string, std::string>> track_verified;
    std::deque<std::function<void(std::stop_token)>> jobs;
    std::jthread worker;
    void publish() {
        ++state.generation;
        published.store(std::make_shared<const InstallView>(state));
    }
    void update(const std::function<void(InstallView&)>& fn) {
        std::lock_guard lock(mutex);
        fn(state);
        publish();
    }
    bool queue(std::function<void(std::stop_token)> job) {
        std::lock_guard lock(mutex);
        if (jobs.size() >= 16)
            return false;
        jobs.push_back(std::move(job));
        cv.notify_all();
        return true;
    }
    void progress(float value, std::string stage) {
        update([&](InstallView& s) {
            s.progress = value;
            s.stage = std::move(stage);
        });
    }
    void run(std::stop_token stop);
    void refresh(std::stop_token stop);
    void prepare(std::vector<fs::path>, std::stop_token);
    void commit(std::string mode, std::string instance, std::stop_token);
    void recover();
    void rollback();
    void save();
    void begin_journal();
    void protect(const std::string& relative);
    void retire_group(const std::string& relative, std::stop_token);
    std::pair<bool, bool> protect_metadata(const Json& members, const Json& outputs);
    void put(const fs::path& source, const std::string& relative, std::stop_token);
    void restore_native(bool tracks = true, bool textures = true);
    Json candidates() const;
    void retire_legacy(const Json& old, const Json& members, std::stop_token stop);
    void remove_instance(std::string instance, bool remove_files, std::stop_token);
    void load_registry() {
        auto saved = read(root / "installations.json");
        if (saved.empty())
            saved = {{"schema", 1}, {"records", Json::array()}};
        validate_registry(saved);
        // A failed reconciliation must not leave unsaved receipts in memory,
        // especially when there is no registry file yet on a first install.
        registry = std::move(saved);
    }
    void prune();
};
InstallManager::InstallManager() : impl_(std::make_unique<Impl>()) {}
InstallManager::~InstallManager() {
    impl_->worker.request_stop();
    impl_->cv.notify_all();
}
InstallManager& installations() {
    static InstallManager value;
    return value;
}
std::shared_ptr<const InstallView> InstallManager::view() const {
    return impl_->published.load();
}
void InstallManager::Impl::run(std::stop_token stop) {
    for (;;) {
        std::function<void(std::stop_token)> job;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, stop, [&] { return !jobs.empty(); });
            if (stop.stop_requested())
                return;
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        try {
            job(stop);
        } catch (const std::exception& e) {
            std::string message = e.what();
            bool recovery_failed = false;
            try {
                if (!journal.empty())
                    rollback();
                else if (!transaction.empty() && path_exists(transaction) &&
                         !path_exists(transaction / "journal.json") && path_exists(transaction / "preparing.json"))
                    discard(root / "transactions", transaction);
            } catch (const std::exception& r) {
                recovery_failed = true;
                message += " Recovery required: " + std::string(r.what());
            }
            lease.reset();
            update([&](InstallView& s) {
                s.error = message;
                s.busy = false;
                s.choosing = false;
                s.completed_key = proposed.key;
                s.succeeded = false;
                if (recovery_failed)
                    s.ready = false;
                ++s.completion;
            });
        }
    }
}
void InstallManager::configure(fs::path config, std::string version, fs::path worker) {
    auto& p = *impl_;
    if (p.configured)
        return;
    p.configured = true;
    p.config = fs::absolute(config).lexically_normal();
    p.root = p.config / "mod-store";
    p.version = std::move(version);
    p.worker_path = std::move(worker);
    p.update([](InstallView& s) {
        s.busy = true;
        s.stage = "Checking mod installations";
    });
    p.worker = std::jthread([&p](std::stop_token stop) { p.run(stop); });
    p.queue([&p](std::stop_token) {
        directories(p.root);
        directories(p.root / "transactions");
        directories(p.root / "history");
        p.lease = std::make_unique<StoreLease>(p.root / "install.lock");
        p.recover();
        p.load_registry();
        p.prune();
        p.lease.reset();
        p.update([&](InstallView& s) {
            s.records = p.registry["records"];
            s.ready = true;
            s.busy = false;
            s.completed_key.clear();
            ++s.completion;
        });
    });
}
void InstallManager::safe_to_write(bool value) {
    std::lock_guard lock(impl_->mutex);
    impl_->safe = value;
}
void InstallManager::retry_recovery() {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (p.state.busy)
            return;
        p.state.busy = true;
        p.state.error.clear();
        p.publish();
    }
    p.queue([&p](std::stop_token) {
        p.proposed = {};
        p.lease = std::make_unique<StoreLease>(p.root / "install.lock");
        p.recover();
        p.load_registry();
        p.prune();
        p.lease.reset();
        p.update([&](InstallView& s) {
            s.ready = true;
            s.busy = false;
            s.records = p.registry["records"];
            s.completed_key.clear();
            ++s.completion;
        });
    });
}
void InstallManager::refresh(Json items, Json inventory) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.configured || !p.state.ready || p.refreshing || p.state.busy)
            return;
        p.refreshing = true;
    }
    if (!p.queue([&p, items = std::move(items), inventory = std::move(inventory)](std::stop_token stop) {
            struct Done {
                Impl& p;
                ~Done() {
                    std::lock_guard lock(p.mutex);
                    p.refreshing = false;
                }
            } done{p};
            p.catalogue = items;
            p.inventory = Json::array();
            for (auto member : inventory) {
                const auto path = canonical_member_path(member);
                // External/native-only resources remain owned by their own
                // importer, never adopted, replaced or removed by the Store.
                if (!managed_member(member, path))
                    continue;
                member["path"] = path;
                p.inventory.push_back(std::move(member));
            }
            try {
                p.refresh(stop);
            } catch (const std::exception& e) {
                try {
                    p.load_registry();
                } catch (...) { /* Preserve the original diagnostic. */
                }
                p.update([&](InstallView& s) { s.error = e.what(); });
            }
        })) {
        std::lock_guard lock(p.mutex);
        p.refreshing = false;
    }
}
bool InstallManager::propose(Download job, std::vector<fs::path> roms) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.configured || !p.safe || p.state.busy || !p.state.ready)
            return false;
        p.state.busy = true;
        p.state.choosing = false;
        p.state.error.clear();
        p.state.result.clear();
        p.state.progress = 0;
        p.publish();
    }
    if (!p.queue([&p, job = std::move(job), roms = std::move(roms)](std::stop_token stop) {
            p.proposed = job;
            p.prepare(roms, stop);
        })) {
        p.update([](InstallView& s) { s.busy = false; });
        return false;
    }
    return true;
}
void InstallManager::decide(std::string mode, std::string instance) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.state.choosing || (mode != "cancel" && !p.safe))
            return;
        p.state.choosing = false;
        p.publish();
    }
    p.queue([&p, mode = std::move(mode), instance = std::move(instance)](std::stop_token stop) {
        p.commit(mode, instance, stop);
    });
}

void InstallManager::Impl::save() {
    validate_registry(registry);
    write(root / "installations.json", registry);
}
void InstallManager::Impl::begin_journal() {
    // Validate the rollback snapshot BEFORE publishing anything. An unrelated
    // receipt problem must never make an otherwise recoverable install unsafe.
    validate_registry(registry);
    // Version 2 adds write-ahead directory retirement. Older builds stop on an
    // unfinished v2 journal rather than incorrectly skipping a pending move.
    journal = {{"schema", 2},
               {"id", path_text(transaction.filename())},
               {"phase", "preparing"},
               {"registry", registry},
               {"actions", Json::array()}};
    write(transaction / "journal.json", journal);
}
void InstallManager::Impl::protect(const std::string& relative) {
    if (!managed_target(relative))
        throw Failure("This mod does not own its requested destination.");
    for (const auto& action : journal["actions"])
        if (action["target"] == relative)
            return;
    const auto target = child(config, relative);
    const auto backup = "backup/" + std::to_string(journal["actions"].size());
    Json action = {{"target", relative}, {"backup", backup}, {"existed", path_exists(target)}, {"ready", false}};
    // There is nothing to copy for a new destination. One durable write is
    // sufficient to record its removal on rollback, before any mutation.
    if (!action["existed"].get<bool>()) {
        action["ready"] = true;
        journal["actions"].push_back(action);
        write(transaction / "journal.json", journal);
        return;
    }
    journal["actions"].push_back(action);
    write(transaction / "journal.json", journal);
    if (path_exists(target)) {
        const auto size = tree_size(target);
        if (fs::space(transaction).available < size + 32ULL * 1024 * 1024)
            throw Failure("Not enough free space to protect the current mod.");
        copy_plain(target, child(transaction, backup));
        const auto proof = tree_hash(target);
        if (proof != tree_hash(child(transaction, backup)))
            throw Failure("The mod backup did not pass verification.");
        journal["actions"].back()["content"] = proof;
    }
    journal["actions"].back()["ready"] = true;
    write(transaction / "journal.json", journal);
}
void InstallManager::Impl::retire_group(const std::string& relative, std::stop_token stop) {
    if (!managed_target(relative) ||
        !(relative.starts_with("mods/legacy/prepared/") || relative.starts_with("mods/legacy/prepared-characters/")))
        throw Failure("Invalid legacy retirement destination.");
    const auto target = child(config, relative);
    for (const auto& action : journal["actions"])
        if (text(action, "target", 4096) == relative) {
            // A caller may already have independently protected this target.
            discard(config, target);
            return;
        }
    const auto backup = "backup/" + std::to_string(journal["actions"].size());
    const auto proof = tree_hash(target, stop);
    directories(transaction / "backup");
    journal["actions"].push_back({{"target", relative},
                                  {"backup", backup},
                                  {"existed", true},
                                  {"ready", false},
                                  {"capture", "move"},
                                  {"content", proof}});
    // Write ahead: recovery also understands a move that completed just before
    // the ready flag was flushed. The old immutable group stays recoverable.
    write(transaction / "journal.json", journal);
    if (stop.stop_requested())
        throw Failure("Mod replacement cancelled before retirement.");
    dkr::updates::replace_file(store_io_path(target), store_io_path(child(transaction, backup)));
    sync_directory(store_io_path(target.parent_path()));
    journal["actions"].back()["ready"] = true;
    write(transaction / "journal.json", journal);
}
std::pair<bool, bool> InstallManager::Impl::protect_metadata(const Json& members, const Json& outputs) {
    bool tracks = false, textures = false, legacy_tracks = false, characters = false, code = false, vehicles = false;
    const auto consider = [&](const Json& entry) {
        const auto kind = text(entry, "kind", 40);
        tracks |= kind == "track";
        textures |= kind == "texture";
        legacy_tracks |= kind == "legacy-track";
        characters |= kind == "legacy-character";
        code |= kind == "code";
        vehicles |= kind == "vehicle" || kind == "vehicle-portrait";
    };
    for (const auto& member : members)
        consider(member);
    for (const auto& output : outputs)
        consider(output);
    if (textures) {
        // Join an earlier pack-index scan only if this operation uses textures.
        dkr::runtime::texture_packs::configure(config);
        protect("texture-packs.ini");
        protect("texture-packs-index-v1.json");
    }
    if (tracks) {
        protect("custom-tracks-enabled.json");
        protect("custom-tracks-state.txt");
    }
    if (legacy_tracks) {
        protect("mods/legacy/track-catalog.json");
        protect("mods/legacy/track-library.json");
    }
    if (characters) {
        protect("mods/legacy/character-catalog.json");
        protect("mods/legacy/character-library.json");
    }
    if (vehicles) protect("mods/vehicles/library.json");
    if (code)
        protect("mods/custom-magic-codes/library.json");
    return {tracks, textures};
}
void InstallManager::Impl::put(const fs::path& source, const std::string& relative, std::stop_token stop) {
    if (!managed_target(relative))
        throw Failure("Unsafe mod output destination.");
    const auto target = child(config, relative);
    if (path_exists(target) && tree_hash(source, stop) == tree_hash(target, stop))
        return;
    const auto size = tree_size(source);
    if (fs::space(transaction).available < size * 2 + 32ULL * 1024 * 1024)
        throw Failure("Not enough free space to replace this mod safely.");
    protect(relative);
    const auto incoming = target.parent_path() / (".store-incoming-" + path_text(transaction.filename()));
    if (path_exists(incoming))
        throw Failure("An earlier private import needs recovery.");
    journal["incoming"] = path_text(incoming.lexically_relative(config));
    write(transaction / "journal.json", journal);
    copy_plain(source, incoming, stop);
    if (tree_hash(source, stop) != tree_hash(incoming, stop))
        throw Failure("The prepared mod copy did not pass verification.");
    if (path_exists(target))
        discard(config, target);
    dkr::updates::replace_file(store_io_path(incoming), store_io_path(target));
    journal.erase("incoming");
    write(transaction / "journal.json", journal);
}
void InstallManager::Impl::restore_native(bool tracks, bool textures) {
    if (tracks)
        dkr::runtime::custom_tracks::reload();
    if (textures)
        dkr::runtime::texture_packs::configure(config);
}
void InstallManager::Impl::rollback() {
    if (journal.empty())
        return;
    if (journal.value("phase", std::string{}) == "committed") {
        journal = Json::object();
        return;
    }
    for (auto it = journal["actions"].rbegin(); it != journal["actions"].rend(); ++it) {
        const auto relative = text(*it, "target", 4096);
        if (!managed_target(relative))
            throw Failure("Unsafe recovery target; files were left untouched.");
        const bool moving = it->value("capture", std::string{}) == "move";
        if (!it->value("ready", false) && !moving)
            continue;
        const auto target = child(config, relative), backup = child(transaction, text(*it, "backup", 256));
        if (moving && !it->value("ready", false) && !path_exists(backup)) {
            if (!path_exists(target))
                throw Failure("An interrupted retirement is missing its original and recovery group.");
            continue; // The durable intent was written, but the move did not run.
        }
        if (it->value("existed", false)) {
            if (!path_exists(backup))
                throw Failure("A protected mod backup is missing.");
            if (!hash_string(text(*it, "content", 64)) || tree_hash(backup) != text(*it, "content", 64))
                throw Failure("A protected mod backup has changed. Files were left in place for recovery.");
            // Copy, do not consume the backup: recovery remains repeatable if
            // power is lost while restoring the next member of the bundle.
            const auto incoming = target.parent_path() / (".store-restore-" + path_text(transaction.filename()));
            if (path_exists(incoming))
                discard(config, incoming);
            copy_plain(backup, incoming);
            if (tree_hash(backup) != tree_hash(incoming))
                throw Failure("Restored mod backup did not match.");
            if (path_exists(target))
                discard(config, target);
            dkr::updates::replace_file(store_io_path(incoming), store_io_path(target));
        } else if (path_exists(target))
            discard(config, target);
    }
    if (journal.contains("incoming")) {
        const auto incoming = child(config, text(journal, "incoming", 4096));
        if (path_text(incoming.filename()) == ".store-incoming-" + path_text(transaction.filename()))
            discard(config, incoming);
    }
    registry = journal.at("registry");
    validate_registry(registry);
    save();
    journal["phase"] = "rolled-back";
    write(transaction / "journal.json", journal);
    restore_native();
    journal = Json::object();
}
void InstallManager::Impl::recover() {
    for (const auto& entry : fs::directory_iterator(root / "transactions")) {
        if (!entry.is_directory() || !instance_id(path_text(entry.path().filename())))
            continue;
        ordinary(entry.path(), true);
        auto saved = read(entry.path() / "journal.json");
        if (saved.empty() || saved.value("phase", std::string{}) == "committed" ||
            saved.value("phase", std::string{}) == "rolled-back")
            continue;
        const auto schema = saved.value("schema", 0);
        if ((schema != 1 && schema != 2) || text(saved, "id", 32) != path_text(entry.path().filename()) ||
            !saved.contains("actions") || !saved["actions"].is_array() || saved["actions"].size() > 2048)
            throw Failure("An unfinished mod transaction needs manual recovery.");
        validate_registry(saved.at("registry"));
        std::set<std::string> targets;
        for (std::size_t i = 0; i < saved["actions"].size(); ++i) {
            const auto& action = saved["actions"][i];
            const auto target = text(action, "target", 4096);
            if (!managed_target(target) || !targets.insert(target).second ||
                text(action, "backup", 256) != "backup/" + std::to_string(i) || !action.contains("existed") ||
                !action["existed"].is_boolean() || !action.contains("ready") || !action["ready"].is_boolean())
                throw Failure("An unfinished transaction has invalid recovery paths; files were left untouched.");
            if (action.value("ready", false) && action.value("existed", false) &&
                !hash_string(text(action, "content", 64)))
                throw Failure("An unfinished transaction has no valid backup proof. Files were left untouched.");
            const auto capture = text(action, "capture", 16);
            if (!capture.empty() && (schema != 2 || capture != "move" || !action.value("existed", false) ||
                                     !hash_string(text(action, "content", 64)) ||
                                     !(target.starts_with("mods/legacy/prepared/") ||
                                       target.starts_with("mods/legacy/prepared-characters/"))))
                throw Failure("An unfinished transaction has an invalid retirement intent.");
        }
        if (saved.contains("incoming")) {
            const auto incoming = fs::u8path(text(saved, "incoming", 4096));
            bool allowed = false;
            for (const auto& action : saved["actions"])
                if (incoming == fs::u8path(text(action, "target", 4096)).parent_path() /
                                    (".store-incoming-" + path_text(entry.path().filename())))
                    allowed = true;
            if (!allowed)
                throw Failure("Invalid interrupted mod staging path; files were left untouched.");
        }
        transaction = entry.path();
        journal = std::move(saved);
        rollback();
    }
}
void InstallManager::Impl::prune() {
    // Never delete an unknown directory. Keep every current/previous edition's
    // transaction, plus the twenty newest recoverable completed operations.
    std::set<std::string> keep;
    for (const auto& r : registry["records"]) {
        keep.insert(text(r, "transaction", 32));
        if (r.contains("previous"))
            keep.insert(text(r["previous"], "transaction", 32));
    }
    std::vector<std::pair<fs::file_time_type, fs::path>> old;
    for (const auto& e : fs::directory_iterator(root / "transactions")) {
        const auto id = path_text(e.path().filename());
        if (!instance_id(id) || !e.is_directory() || keep.contains(id))
            continue;
        ordinary(e.path(), true);
        const auto j = read(e.path() / "journal.json");
        if (j.empty()) {
            const auto preparation = read(e.path() / "preparing.json");
            if (preparation.value("id", std::string{}) == id)
                discard(root / "transactions", e.path());
        } else if ((j.value("schema", 0) == 1 || j.value("schema", 0) == 2) && text(j, "id", 32) == id &&
                   (j.value("phase", std::string{}) == "committed" || j.value("phase", std::string{}) == "rolled-back"))
            old.emplace_back(e.last_write_time(), e.path());
    }
    std::sort(old.begin(), old.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 20; i < old.size(); ++i)
        discard(root / "transactions", old[i].second);
}
void InstallManager::Impl::refresh(std::stop_token stop) {
    if (!inventory.is_array() || !catalogue.is_array())
        return;
    // Serialise receipt changes between launcher copies as well as threads.
    std::unique_ptr<StoreLease> metadata_lease;
    if (!lease) {
        metadata_lease = std::make_unique<StoreLease>(root / "install.lock");
        load_registry();
    }
    bool changed = false;
    const auto names = read(root / "installed-names.json");
    for (auto& local : inventory) {
        const auto relative = text(local, "path", 4096);
        if (text(local, "kind", 40) == "texture" && !relative.empty() && names.contains("textures")) {
            const auto key = path_text(config / fs::u8path(relative));
            if (names["textures"].contains(key)) {
                const auto& proof = names["textures"][key];
                try {
                    if (text(proof, "stamp", 64) == name_stamp(child(config, relative)))
                        local["digest"] = text(proof, "sha256", 64);
                } catch (...) { /* Leave unproven content unlinked. */
                }
            }
        }
    }
    // Older versions did not write track receipts. Reconstruct a proof from
    // the verified cached package, never from a similar folder or display name.
    for (const auto& item : catalogue) {
        if (text(item, "category", 32) != "tracks")
            continue;
        for (const auto& release : item.value("releases", Json::array())) {
            const auto digest = text(release, "sha256", 64);
            if (!hash_string(digest) || !text(release, "format", 32).empty() && text(release, "format", 32) != ".zip" &&
                                            text(release, "format", 32) != ".dkrmap")
                continue;
            bool need = false;
            for (const auto& local : inventory)
                if (text(local, "kind", 40) == "track" && text(local, "digest", 64).empty())
                    need = true;
            if (!need)
                break;
            auto proof = registry.value("trackProofs", Json::object()).value(digest, Json::object());
            if (proof.empty()) {
                const auto folder = root / "downloads" / digest;
                if (!path_exists(folder))
                    continue;
                const auto file =
                    folder / fs::u8path(package_filename(text(item, "title", 200), text(release, "fileName", 180)));
                if (!recover_cached_package(folder, file, digest, release.value("size", 0ULL), stop))
                    continue;
                const auto scratch = root / "transactions" / uuid();
                directories(scratch);
                write(scratch / "preparing.json", {{"id", path_text(scratch.filename())}});
                try {
                    const auto track = prepare_store_track(file, scratch / "payload", text(item, "title", 200), stop);
                    proof = {{"id", text(track.native, "trackId", 256)},
                             {"content", track_hash(fs::u8path(text(track.outputs.front(), "source", 4096)), stop)}};
                    registry["trackProofs"][digest] = proof;
                    changed = true;
                    discard(root / "transactions", scratch);
                } catch (...) {
                    discard(root / "transactions", scratch);
                    continue;
                }
            }
            for (auto& local : inventory)
                if (text(local, "kind", 40) == "track" && text(local, "id", 256) == text(proof, "id", 256)) {
                    const auto path = child(config, text(local, "path", 4096));
                    if (path_exists(path)) {
                        const auto stamp = track_stamp(path), key = path_text(path);
                        if (!track_verified.contains(key) || track_verified[key].first != stamp)
                            track_verified[key] = {stamp, track_hash(path, stop)};
                        if (track_verified[key].second == text(proof, "content", 64))
                            local["digest"] = digest;
                    }
                }
        }
    }
    // Adopt only exact proofs. Similar names remain unlinked duplicate hints.
    for (const auto& item : catalogue) {
        const auto item_id = text(item, "id", 160), category = text(item, "category", 32);
        if (!item.contains("releases") || !item["releases"].is_array())
            continue;
        for (const auto& release : item["releases"]) {
            const auto digest = text(release, "sha256", 64);
            if (!hash_string(digest))
                continue;
            Json members = Json::array();
            for (const auto& local : inventory) {
                if (text(local, "digest", 64) != digest)
                    continue;
                if (registry.contains("ignored") && std::find(registry["ignored"].begin(), registry["ignored"].end(),
                                                              local["key"]) != registry["ignored"].end())
                    continue;
                const auto kind = text(local, "kind", 40);
                if ((category == "textures" && kind != "texture") || (category == "magic" && kind != "code") ||
                    (category == "characters" && kind != "legacy-character") ||
                    (vehicle_store_category(category) && kind != "vehicle") ||
                    (category == "tracks" && kind != "track" && kind != "legacy-track"))
                    continue;
                bool owned = false;
                for (const auto& record : registry["records"])
                    if (!record.value("stored", false))
                        for (const auto& member : record.value("members", Json::array()))
                            if (same_native_member(member, local)) {
                                owned = true;
                                break;
                            }
                if (!owned)
                    members.push_back(local);
            }
            if (members.empty())
                continue;
            const auto adopt = [&](const Json& owned) {
                auto members_with_proofs = owned;
                for (auto& m : members_with_proofs) {
                    const auto path = text(m, "path", 4096);
                    if (!path.empty() && path_exists(child(config, path)))
                        m["content"] = text(m, "kind", 40) == "track" ? track_hash(child(config, path), stop)
                                                                      : tree_hash(child(config, path), stop);
                }
                Json record = {{"instance", uuid()},
                               {"item", item_id},
                               {"release", text(release, "id", 160)},
                               {"title", text(item, "title", 200)},
                               {"category", category},
                               {"version", text(release, "version", 80)},
                               {"publishedAt", text(release, "createdAt", 80)},
                               {"digest", digest},
                               {"members", members_with_proofs},
                               {"follow", true},
                               {"skip", ""},
                               {"origin", "existing verified installation"},
                               {"stored", false}};
                const auto file =
                    root / "downloads" / digest /
                    fs::u8path(package_filename(text(item, "title", 200), text(release, "fileName", 180)));
                if (path_exists(file)) {
                    record["file"] = path_text(file.lexically_relative(config));
                    record["bytes"] = release.value("size", 0ULL);
                }
                registry["records"].push_back(record);
            };
            if (category == "textures")
                for (const auto& member : members)
                    adopt(Json::array({member}));
            else
                adopt(members);
            changed = true;
        }
    }
    Json updates = Json::array(), duplicates = Json::array();
    std::map<std::string, Json> groups, name_groups;
    std::set<std::string> unique_native;
    for (const auto& local : inventory) {
        const auto logical = text(local, "kind", 40) + ":" + text(local, "id", 256);
        if (text(local, "kind", 40).starts_with("legacy-") && !unique_native.insert(logical).second)
            continue;
        const auto key = text(local, "digest", 64);
        if (hash_string(key)) {
            if (!groups.contains(key))
                groups[key] = Json::array();
            groups[key].push_back(local);
        }
        const auto name = text(local, "kind", 40) + ":" + name_key(text(local, "name", 200));
        if (!name_groups.contains(name))
            name_groups[name] = Json::array();
        name_groups[name].push_back(local);
    }
    for (const auto& [key, group] : groups)
        if (group.size() > 1)
            duplicates.push_back({{"identity", key}, {"entries", group}, {"exact", hash_string(key)}});
    for (const auto& [name, group] : name_groups)
        if (group.size() > 1) {
            const auto first = text(group.front(), "digest", 64);
            const bool exact = hash_string(first) && std::all_of(group.begin(), group.end(), [&](const Json& v) {
                                   return text(v, "digest", 64) == first;
                               });
            if (!exact)
                duplicates.push_back({{"identity", name}, {"entries", group}, {"exact", false}});
        }
    for (auto& record : registry["records"]) {
        if (stop.stop_requested())
            return;
        unsigned present = 0, total = 0;
        bool another_track_copy = false;
        for (auto& member : record["members"]) {
            ++total;
            bool found = false;
            for (const auto& local : inventory)
                if (same_native_member(member, local) &&
                    (text(local, "path", 4096).empty() || path_exists(child(config, text(local, "path", 4096))))) {
                    ++present;
                    found = true;
                    for (const auto* field :
                         {"enabled", "hidden", "priority", "settings", "data", "approved", "selected", "path", "group"})
                        if (!(text(member, "kind", 40) == "track" && std::string_view(field) == "path") &&
                            local.contains(field) && (!member.contains(field) || member[field] != local[field])) {
                            member[field] = local[field];
                            changed = true;
                        }
                    break;
                }
            if (!found && !record.value("stored", false) && text(member, "kind", 40) == "track" &&
                hash_string(text(member, "content", 64))) {
                const auto path = child(config, text(member, "path", 4096));
                if (path_exists(path)) {
                    const auto stamp = track_stamp(path), key = path_text(path);
                    if (!track_verified.contains(key) || track_verified[key].first != stamp)
                        track_verified[key] = {stamp, track_hash(path, stop)};
                    // A duplicate can shadow a correctly installed directory.
                    // Count it only with the original verified content proof,
                    // retaining its exact owned path instead of stealing the
                    // currently active copy's folder.
                    if (track_verified[key].second == text(member, "content", 64)) {
                        ++present;
                        another_track_copy |= std::any_of(inventory.begin(), inventory.end(), [&](const Json& local) {
                            return text(local, "key", 512) == text(member, "key", 512) &&
                                   !same_native_member(member, local);
                        });
                    }
                }
            }
        }
        if (record.value("anotherTrackCopyActive", false) != another_track_copy) {
            record["anotherTrackCopyActive"] = another_track_copy;
            changed = true;
        }
        const std::string status = record.value("stored", false) ? "Stored edition"
                                   : present == total && total   ? "Installed"
                                   : present                     ? "Needs repair"
                                                                 : "Missing";
        if (record.value("state", std::string{}) != status) {
            record["state"] = status;
            changed = true;
        }
        for (const auto& item : catalogue)
            if (text(item, "id", 160) == text(record, "item", 160)) {
                auto latest = current_release(item);
                if (latest.empty() || !hash_string(text(latest, "sha256", 64)) || !record.value("follow", true) ||
                    text(latest, "id", 160) == text(record, "release", 160) ||
                    text(latest, "id", 160) == text(record, "skip", 160) ||
                    text(latest, "sha256", 64) == text(record, "digest", 64) || status == "Missing")
                    continue;
                bool downgrade = false;
                try {
                    downgrade = dkr::mods::code::Version::parse(text(latest, "version", 80)) <
                                dkr::mods::code::Version::parse(text(record, "version", 80));
                } catch (...) {
                    const auto old = text(record, "publishedAt", 80), now = text(latest, "createdAt", 80);
                    downgrade = !old.empty() && !now.empty() && now < old;
                }
                updates.push_back({{"instance", record["instance"]},
                                   {"item", item},
                                   {"release", latest},
                                   {"current", record["version"]},
                                   {"title", record["title"]},
                                   {"downgrade", downgrade},
                                   {"codePermission", text(record, "category", 32) == "magic"}});
            }
    }
    Json unlinked = Json::array();
    for (const auto& local : inventory) {
        bool owned = false;
        for (const auto& record : registry["records"])
            for (const auto& m : record.value("members", Json::array()))
                if (same_native_member(m, local))
                    owned = true;
        if (!owned)
            unlinked.push_back(local);
    }
    if (changed)
        save();
    update([&](InstallView& s) {
        const auto identity = [](const Json& rows) {
            std::set<std::string> ids;
            for (const auto& row : rows)
                ids.insert(text(row, "instance", 32) + ":" + text(row["release"], "id", 160));
            return ids;
        };
        if (identity(s.updates) != identity(updates))
            ++s.updates_revision;
        s.records = registry["records"];
        s.updates = std::move(updates);
        s.duplicates = std::move(duplicates);
        s.unlinked = std::move(unlinked);
    });
}
Json InstallManager::Impl::candidates() const {
    Json found = Json::array();
    for (const auto& record : registry["records"]) {
        bool strong = text(record, "item", 160) == proposed.item || text(record, "digest", 64) == proposed.digest;
        bool possible = name_key(text(record, "title", 200)) == name_key(proposed.title) &&
                        text(record, "category", 32) == proposed.category;
        if (strong || possible) {
            auto row = record;
            row["exact"] = strong;
            found.push_back(row);
        }
    }
    for (const auto& local : inventory) {
        bool identity = false;
        if (text(local, "kind", 40) == "track" && text(local, "id", 256) == text(prepared.native, "trackId", 256))
            identity = true;
        for (const auto& member : prepared.native.value("members", Json::array()))
            if (text(member, "kind", 40) == text(local, "kind", 40) &&
                text(member, "id", 256) == text(local, "id", 256))
                identity = true;
        const bool strong = identity || text(local, "digest", 64) == proposed.digest;
        bool possible = name_key(text(local, "name", 200)) == name_key(proposed.title);
        if(text(local,"kind",40)=="vehicle") for(const auto& incoming:prepared.native.value("members",Json::array()))
            possible |= text(incoming,"kind",40)=="vehicle" && incoming.value("type",0U)==local.value("type",0U) &&
                name_key(text(incoming,"name",240))==name_key(text(local,"name",240));
        if (!strong && !possible)
            continue;
        bool already = false;
        for (const auto& record : found)
            for (const auto& member : record.value("members", Json::array()))
                if (same_native_member(member, local))
                    already = true;
        if (!already)
            found.push_back({{"instance", "local:" + text(local, "key", 512)},
                             {"title", local["name"]},
                             {"version", "Local copy"},
                             {"members", Json::array({local})},
                             {"exact", strong},
                             {"stored", false}});
    }
    return found;
}
void InstallManager::Impl::prepare(std::vector<fs::path> roms, std::stop_token stop) {
    lease = std::make_unique<StoreLease>(root / "install.lock");
    load_registry();
    if (!hash_string(proposed.digest) || proposed.file.parent_path() != root / "downloads" / proposed.digest ||
        fs::file_size(proposed.file) != proposed.total ||
        dkr::updates::hash_file(proposed.file, MaxPackage) != proposed.digest)
        throw Failure("The cached package is missing or has changed. Download it again before installing.");
    transaction = root / "transactions" / uuid();
    directories(transaction);
    write(transaction / "preparing.json", {{"id", path_text(transaction.filename())}});
    prepared = {};
    journal = Json::object();
    warnings = Json::array();
    progress(.02F, "Inspecting the downloaded package");
    auto extension = proposed.file.extension().string();
    for (auto& c : extension)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool legacy_patch=extension==".xdelta"||extension==".bps"||extension==".ips";
    bool historical_legacy_archive=false;
    if(vehicle_store_category(proposed.category)&&extension==".zip")
        for(const auto& receipt:registry["records"]) for(const auto* edition:{&receipt,receipt.contains("previous")?&receipt["previous"]:&receipt})
            if(text(*edition,"digest",64)==proposed.digest)
                for(const auto& m:edition->value("members",Json::array()))
                    historical_legacy_archive |= text(m,"kind",40).starts_with("legacy-");
    if(vehicle_store_extension(extension)||(vehicle_store_category(proposed.category)&&!legacy_patch&&!historical_legacy_archive)) {
        if(!vehicle_store_category(proposed.category)) throw Failure("This vehicle is listed under the wrong Store category. Ask its publisher to choose Vehicles.");
        progress(.05F,"Validating vehicle models, portraits and driver seats");
        prepared=prepare_store_vehicle(proposed.file,transaction/"payload",stop);
    } else if (extension == ".nrm" || extension == ".dkrcode") {
        const auto bytes = dkr::mods::read_file(proposed.file, dkr::mods::code::MaxPackage);
        const auto package = dkr::mods::code::inspect(bytes, version);
        const auto payload = transaction / "payload" / "code.dkrcode";
        copy_plain(proposed.file, payload, stop);
        prepared.outputs.push_back({{"kind", "code"},
                                    {"source", path_text(payload)},
                                    {"target", "mods/custom-magic-codes/packages/" + package->digest + ".dkrcode"},
                                    {"id", package->id}});
        prepared.native = {
            {"codeId", package->id},
            {"codeDigest", package->digest},
            {"members", Json::array({{{"kind", "code"},
                                      {"id", package->id},
                                      {"digest", package->digest},
                                      {"path", "mods/custom-magic-codes/packages/" + package->digest + ".dkrcode"},
                                      {"key", "code:" + package->digest},
                                      {"name", package->name}}})}};
    } else if (proposed.category == "textures") {
        prepared = prepare_store_texture(proposed.file, transaction / "payload", proposed.title, stop,
                                         [&](float p, std::string note) { progress(.05F + .7F * p, std::move(note)); });
    } else if (proposed.category == "characters" || historical_legacy_archive || extension == ".xdelta" ||
               extension == ".bps" || extension == ".ips") {
        if (roms.empty())
            throw Failure("Add the patch's required original ROM in Play before preparing this mod.");
        const auto library_root = transaction / "legacy";
        dkr::mods::ModLibrary library;
        library.configure(library_root, worker_path);
        const auto deadline = Clock::now() + std::chrono::minutes(20);
        auto wait = [&] {
            for (;;) {
                if (stop.stop_requested() || Clock::now() > deadline) {
                    library.cancel();
                    throw Failure(
                        "Legacy preparation was cancelled or timed out. The installed version was not changed.");
                }
                library.tick();
                const auto view = library.snapshot();
                progress(.1F + .65F * (view.total ? float(view.completed) / float(view.total) : 0.F), view.stage);
                if (!view.busy) {
                    if (!view.succeeded)
                        throw Failure(view.result);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        };
        wait();
        if (!library.import_file(proposed.file, std::move(roms)))
            throw Failure("The private legacy importer could not start.");
        wait();
        const auto view = library.snapshot();
        Json members = Json::array();
        std::set<std::string> groups;
        for (const bool characters : {false, true}) {
            const auto snapshot = characters ? view.characters : view.tracks;
            if (!snapshot)
                continue;
            for (const auto& item : snapshot->tracks) {
                const auto prefix = characters ? "prepared-characters/" : "prepared/";
                const auto relative = std::string(prefix) + item.group;
                if (groups.insert(relative).second)
                    prepared.outputs.push_back({{"kind", "legacy-group"},
                                                {"source", path_text(library_root / fs::u8path(relative))},
                                                {"target", "mods/legacy/" + relative}});
                members.push_back({{"kind", characters ? "legacy-character" : "legacy-track"},
                                   {"id", item.id},
                                   {"key", std::string(characters ? "legacy-character:" : "legacy-track:") + item.id +
                                               ":" + item.revision},
                                   {"revision", item.revision},
                                   {"digest", item.patch},
                                   {"name", item.name},
                                   {"group", item.group},
                                   {"path", "mods/legacy/" + relative},
                                   {"slot", characters ? item.base_character : item.carrier}});
            }
        }
        const auto reviews = library_root / "reviews";
        if (path_exists(reviews))
            for (const auto& entry : fs::directory_iterator(reviews))
                if (entry.is_directory() && hash_string(path_text(entry.path().filename())))
                    prepared.outputs.push_back(
                        {{"kind", "legacy-review"},
                         {"source", path_text(entry.path())},
                         {"target", "mods/legacy/reviews/" + path_text(entry.path().filename())}});
        if (members.empty())
            throw Failure("This patch did not produce a supported character or track.");
        prepared.native["members"] = members;
    } else {
        prepared = prepare_store_track(proposed.file, transaction / "payload", proposed.title, stop);
        const auto hd = text(prepared.native, "hdSource", 4096);
        if (!hd.empty()) {
            auto textures =
                prepare_store_texture(fs::u8path(hd), transaction / "hd", proposed.title + " HD textures", stop,
                                      [&](float p, std::string note) { progress(.15F + .55F * p, std::move(note)); });
            for (auto& output : textures.outputs) {
                output["owner"] = prepared.native["trackId"];
                output["textureDigest"] = prepared.native["hdDigest"];
                prepared.outputs.push_back(output);
            }
        }
    }
    for (auto& output : prepared.outputs) {
        const auto source = fs::u8path(text(output, "source", 4096));
        if (source.lexically_relative(transaction).empty() || *source.lexically_relative(transaction).begin() == "..")
            throw Failure("Prepared content escaped its private transaction.");
        output["content"] = tree_hash(source, stop);
        output["bytes"] = tree_size(source);
    }
    if (stop.stop_requested())
        throw Failure("Installation cancelled before publishing content.");
    const auto found = candidates();
    bool modified = false, identity_changed = false;
    if (!proposed.instance.empty()) {
        bool valid = false;
        for (const auto& candidate : found)
            if (text(candidate, "instance", 512) == proposed.instance && candidate.value("exact", false)) {
                valid = true;
                for (const auto& m : candidate.value("members", Json::array())) {
                    const auto incoming = text(m, "kind", 40) == "track"  ? text(prepared.native, "trackId", 256)
                                          : text(m, "kind", 40) == "code" ? text(prepared.native, "codeId", 256)
                                                                          : std::string{};
                    if (!incoming.empty() && incoming != text(m, "id", 256))
                        identity_changed = true;
                }
                if (!candidate.value("stored", false))
                    for (const auto& m : candidate.value("members", Json::array())) {
                        const auto path = text(m, "path", 4096), expected = text(m, "content", 64);
                        if (path.empty() || expected.empty())
                            continue;
                        const auto file = child(config, path);
                        if (!path_exists(file) || (text(m, "kind", 40) == "track" ? track_hash(file, stop)
                                                                                  : tree_hash(file, stop)) != expected)
                            modified = true;
                    }
            }
        if (!valid)
            throw Failure("The selected installation changed. Review the mod again before replacing it.");
        if (!modified && !identity_changed) {
            commit("replace", proposed.instance, stop);
            return;
        }
    }
    if (found.empty()) {
        commit("install", {}, stop);
        return;
    }
    update([&](InstallView& s) {
        s.choosing = true;
        s.progress = .8F;
        s.stage = "Waiting for your duplicate choice";
        s.proposal = {{"job", proposed.key},
                      {"title", proposed.title},
                      {"version", proposed.version},
                      {"candidates", found},
                      {"modified", modified},
                      {"identityChanged", identity_changed},
                      {"code", prepared.native.contains("codeId")},
                      {"canRunCopies", proposed.category == "textures" || prepared.native.value("vehicle",false)}};
    });
}
void InstallManager::Impl::retire_legacy(const Json& old, const Json& members, std::stop_token stop) {
    for (const bool characters : {false, true}) {
        const auto kind = characters ? "legacy-character" : "legacy-track";
        const auto prefix = characters ? "prepared-characters/" : "prepared/";
        std::set<std::string> retired, groups;
        for (const auto& m : old.value("members", Json::array())) {
            if (text(m, "kind", 40) != kind)
                continue;
            bool keep = false;
            for (const auto& n : members)
                if (text(n, "kind", 40) == kind && text(n, "id", 256) == text(m, "id", 256) &&
                    text(n, "path", 4096) == text(m, "path", 4096))
                    keep = true;
            for (const auto& r : registry["records"])
                if (text(r, "instance", 512) != text(old, "instance", 512) && !r.value("stored", false))
                    for (const auto& n : r.value("members", Json::array()))
                        if (text(n, "kind", 40) == kind && text(n, "id", 256) == text(m, "id", 256))
                            keep = true;
            if (!keep) {
                retired.insert(text(m, "id", 256));
                groups.insert(text(m, "path", 4096));
            }
        }
        if (retired.empty())
            continue;
        const auto private_root = transaction / (characters ? "retire-characters" : "retire-tracks");
        directories(private_root);
        std::set<std::string> partial;
        for (const auto& relative : groups) {
            if (!relative.starts_with(std::string("mods/legacy/") + prefix))
                throw Failure("Invalid old legacy group ownership.");
            const auto group = child(config, relative);
            const auto name = path_text(group.filename());
            if (!hash_string(name))
                throw Failure("Invalid legacy group identity.");
            const auto manifest = child(group, "prepared.json");
            if (dkr::updates::hash_file(manifest, 1024 * 1024) != name)
                throw Failure("The old legacy group receipt has changed; its files were kept.");
            const auto receipt = read(manifest, 1024 * 1024);
            const auto entries = receipt.value(characters ? "characters" : "tracks", Json::array());
            if (entries.is_array() && !entries.empty() &&
                std::all_of(entries.begin(), entries.end(),
                            [&](const Json& entry) { return retired.contains(text(entry, "id", 256)); })) {
                // No survivors: retain the old immutable directory by a
                // journaled same-volume move, not hundreds of tiny file copies.
                retire_group(relative, stop);
                continue;
            }
            partial.insert(relative);
            protect(relative);
            // Scratch work can be discarded after interruption. Only the real
            // backup and published replacement need per-file durable flushes.
            copy_plain(group, private_root / prefix / fs::u8path(name), stop, false);
        }
        std::vector<dkr::mods::TrackCatalogItem> surviving;
        if (!partial.empty()) {
            dkr::mods::TrackCatalog catalog(characters ? dkr::mods::TrackCatalog::Kind::Character
                                                       : dkr::mods::TrackCatalog::Kind::Track);
            catalog.configure(private_root, worker_path);
            const auto deadline = Clock::now() + std::chrono::minutes(5);
            auto wait = [&] {
                while (catalog.snapshot()->busy) {
                    if (stop.stop_requested() || Clock::now() > deadline) {
                        catalog.cancel();
                        throw Failure("Legacy replacement cancelled before publication.");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                if (!catalog.snapshot()->succeeded)
                    throw Failure(catalog.snapshot()->result);
            };
            wait();
            for (const auto& id : retired) {
                if (std::none_of(catalog.snapshot()->tracks.begin(), catalog.snapshot()->tracks.end(),
                                 [&](const auto& item) { return item.id == id; }))
                    continue;
                if (!catalog.remove(id))
                    throw Failure("Could not prepare a legacy replacement.");
                wait();
            }
            std::set<std::string> survivors;
            const auto prepared_root = private_root / prefix;
            if (path_exists(prepared_root))
                for (const auto& e : fs::directory_iterator(prepared_root))
                    if (e.is_directory() && hash_string(path_text(e.path().filename()))) {
                        const auto relative = "mods/legacy/" + std::string(prefix) + path_text(e.path().filename());
                        put(e.path(), relative, stop);
                        survivors.insert(relative);
                    }
            for (const auto& relative : partial)
                if (!survivors.contains(relative))
                    discard(config, child(config, relative));
            surviving = catalog.snapshot()->tracks;
        }
        const auto enabled_path =
            std::string("mods/legacy/") + (characters ? "character-catalog.json" : "track-catalog.json");
        auto enabled = read(child(config, enabled_path));
        if (enabled.contains("enabled") && enabled["enabled"].is_array()) {
            auto kept = Json::array();
            for (const auto& id : enabled["enabled"])
                if (!retired.contains(id.get<std::string>()) ||
                    std::any_of(members.begin(), members.end(), [&](const Json& m) {
                        return text(m, "kind", 40) == kind && text(m, "id", 256) == id.get<std::string>();
                    }))
                    kept.push_back(id);
            enabled["enabled"] = kept;
            write(child(config, enabled_path), enabled);
        }
        const auto metadata_path =
            std::string("mods/legacy/") + (characters ? "character-library.json" : "track-library.json");
        auto metadata = read(child(config, metadata_path));
        if (metadata.contains("items") && metadata["items"].is_object()) {
            for (const auto& id : retired)
                if (!std::any_of(members.begin(), members.end(), [&](const Json& m) {
                        return text(m, "kind", 40) == kind && text(m, "id", 256) == id;
                    }))
                    metadata["items"].erase(id);
            write(child(config, metadata_path), metadata);
        }
        for (auto& r : registry["records"])
            for (auto& m : r["members"])
                for (const auto& survivor : surviving)
                    if (text(m, "id", 256) == survivor.id && text(m, "revision", 32) == survivor.revision) {
                        m["group"] = survivor.group;
                        m["path"] = "mods/legacy/" + std::string(prefix) + survivor.group;
                        m["content"] = tree_hash(child(config, m["path"].get<std::string>()), stop);
                    }
    }
}
void InstallManager::Impl::commit(std::string mode, std::string instance, std::stop_token stop) {
    if (mode == "cancel") {
        if (path_exists(transaction))
            discard(root / "transactions", transaction);
        lease.reset();
        update([&](InstallView& s) {
            s.busy = false;
            s.choosing = false;
            s.succeeded = false;
            s.completed_key = proposed.key;
            s.result = "Installation cancelled. Your installed mods were not changed.";
            ++s.completion;
        });
        return;
    }
    {
        std::lock_guard lock(mutex);
        if (!safe)
            throw Failure("Return to the launcher and leave the online lobby before installing mods.");
    }
    if (mode != "install" && mode != "replace" && mode != "add")
        throw Failure("Invalid installation choice.");
    Json old = Json::object();
    if (mode == "replace") {
        for (const auto& candidate : candidates())
            if (text(candidate, "instance", 512) == instance)
                old = candidate;
        if (old.empty())
            throw Failure("The selected duplicate is no longer available.");
    }
    if (proposed.intent == "activate") {
        for (const auto& r : registry["records"])
            if (text(r, "item", 160) == proposed.item && !r.value("stored", false) &&
                text(r, "instance", 32) != instance)
                old = r;
        if (old.value("stored", false)) {
            for (const auto& candidate : candidates())
                if (candidate.value("exact", false) && !candidate.value("stored", false) &&
                    text(candidate, "instance", 512).starts_with("local:")) {
                    old = candidate;
                    break;
                }
        }
    }
    progress(.82F, "Protecting the existing installation");
    begin_journal();
    Json members = prepared.native.value("members", Json::array());
    std::pair<bool, bool> native_changes{false, false};
    const bool stored = (mode == "add" && proposed.category != "textures" && !prepared.native.value("vehicle",false)) ||
                        (old.value("stored", false) && proposed.intent != "activate");
    if (proposed.intent == "activate")
        for (auto& r : registry["records"])
            if (text(r, "item", 160) == proposed.item && text(r, "instance", 32) != instance)
                r["stored"] = true;
    if (!stored) {
        auto affected = members;
        for (const auto& member : old.value("members", Json::array()))
            affected.push_back(member);
        progress(.83F, "Saving recovery information");
        native_changes = protect_metadata(affected, prepared.outputs);
        // put(), legacy retirement and obsolete-resource removal protect their
        // actual mutation targets. Do not copy an unchanged multi-GB texture
        // tree just because its source archive was downloaded again.
        for (auto& output : prepared.outputs) {
            progress(.85F, "Installing verified files: " + proposed.title);
            const auto kind = text(output, "kind", 40);
            auto target = text(output, "target", 4096);
            Json previous = Json::object();
            if (kind == "texture" || kind == "track") {
                for (const auto& m : old.value("members", Json::array())) {
                    if (text(m, "kind", 40) != kind)
                        continue;
                    if (kind == "texture" && !text(output, "owner", 256).empty() &&
                        text(m, "owner", 256) != text(output, "owner", 256))
                        continue;
                    if (kind == "track" && text(m, "id", 256) != text(output, "id", 256))
                        warnings.push_back("The publisher changed this track's ID. Cups referencing the previous ID "
                                           "may need the track selected again. The previous edition is retained.");
                    previous = m;
                    target = text(m, "path", 4096);
                    break;
                }
                if (target.empty()) {
                    const auto folder = kind == "texture" ? "texture-packs/" : "custom-tracks/";
                    target = std::string(folder) + text(output, "filename", 256);
                    const auto initial = fs::u8path(target);
                    for (unsigned n = 2; path_exists(child(config, target)); ++n) {
                        if (n > 10000)
                            throw Failure("Too many copies of this mod are installed.");
                        target =
                            path_text(initial.parent_path() / (path_text(initial.stem()) + "-" + std::to_string(n) +
                                                               path_text(initial.extension())));
                    }
                }
            }
            output["target"] = target;
            const auto source = fs::u8path(text(output, "source", 4096));
            if (tree_hash(source, stop) != text(output, "content", 64))
                throw Failure("Prepared mod content changed before publication.");
            if (path_exists(child(config, target))) {
                const auto current = tree_hash(child(config, target), stop);
                if ((kind == "legacy-group" || kind == "legacy-review" || kind == "code" || kind == "vehicle" || kind == "vehicle-portrait") &&
                    current != text(output, "content", 64))
                    throw Failure("An immutable mod identity has different content. Existing files were kept.");
                bool shared = false;
                for (const auto& r : registry["records"])
                    if (text(r, "instance", 32) != instance && !r.value("stored", false))
                        for (const auto& m : r.value("members", Json::array()))
                            if (text(m, "path", 4096) == target)
                                shared = true;
                if (shared && current != text(output, "content", 64)) {
                    if (kind != "texture")
                        throw Failure("Another installed package uses this game identity. Keep the update as a "
                                      "separate edition or remove the conflicting copy first.");
                    const auto initial = fs::u8path(target);
                    for (unsigned n = 2; path_exists(child(config, target)); ++n) {
                        if (n > 10000)
                            throw Failure("Too many copies of this texture pack are installed.");
                        target =
                            path_text(initial.parent_path() / (path_text(initial.stem()) + "-" + std::to_string(n) +
                                                               path_text(initial.extension())));
                    }
                    output["target"] = target;
                }
            }
            put(source, target, stop);
            if (kind == "texture") {
                output["absolute"] = path_text(child(config, target));
                publish_store_texture(output, previous);
                const auto packs = dkr::runtime::texture_packs::snapshot(true);
                for (const auto& pack : packs)
                    if (pack.path.lexically_normal() == child(config, target))
                        members.push_back({{"key", "texture:" + pack.id},
                                           {"kind", "texture"},
                                           {"id", pack.id},
                                           {"name", proposed.title},
                                           {"path", target},
                                           {"digest", proposed.digest},
                                           {"content", output["content"]},
                                           {"enabled", pack.enabled},
                                           {"hidden", pack.hidden},
                                           {"priority", pack.priority},
                                           {"owner", text(output, "owner", 256)}});
            } else if (kind == "track") {
                dkr::runtime::custom_tracks::reload();
                const auto id = text(output, "id", 256);
                if (previous.contains("enabled")) {
                    std::string error;
                    if (!dkr::runtime::custom_tracks::set_enabled(id, previous.value("enabled", true), error))
                        throw Failure(error);
                }
                members.push_back({{"key", "track:" + id},
                                   {"kind", "track"},
                                   {"id", id},
                                   {"name", text(output, "name", 200)},
                                   {"path", target},
                                   {"digest", proposed.digest},
                                   {"content", output["content"]},
                                   {"enabled", previous.value("enabled", true)}});
            }
        }
        if(prepared.native.value("vehicle",false)) {
            progress(.92F,"Updating the vehicle library");
            Json other_owners=Json::array();
            for(const auto& r:registry["records"]) if(text(r,"instance",32)!=instance&&!r.value("stored",false))
                for(const auto& m:r.value("members",Json::array())) other_owners.push_back(m);
            members=publish_store_vehicles(config,prepared,old.value("members",Json::array()),other_owners,stop);
        }
        // Code admission uses the existing verifier and trust model. Changed
        // executable bytes are NEVER approved or enabled by a Store update.
        if (prepared.native.contains("codeId")) {
            progress(.92F, "Updating Custom Magic Code settings");
            dkr::mods::code::Library library(config / "mods/custom-magic-codes", version);
            Json previous = Json::object();
            for (const auto& local : inventory)
                if (text(local, "kind", 40) == "code" && local.value("selected", false) &&
                    text(local, "id", 256) == text(prepared.native, "codeId", 256))
                    previous = local;
            for (const auto& m : old.value("members", Json::array()))
                if (text(m, "kind", 40) == "code" && text(m, "id", 256) != text(prepared.native, "codeId", 256))
                    warnings.push_back("The publisher changed the code mod's ID. Its previous settings and data remain "
                                       "in the protected backup; review the new mod's defaults before approval.");
            const auto payload = dkr::mods::read_file(fs::u8path(text(prepared.outputs.front(), "source", 4096)),
                                                      dkr::mods::code::MaxPackage);
            const auto package = dkr::mods::code::inspect(payload, version);
            const auto digest = library.import(payload);
            if (digest != proposed.digest)
                throw Failure("Prepared code package differs from its verified Store release.");
            bool replacing_selected = old.empty();
            for (const auto& m : old.value("members", Json::array()))
                if (text(m, "digest", 64) == text(previous, "digest", 64))
                    replacing_selected = true;
            if (!previous.empty() && replacing_selected && text(previous, "digest", 64) != digest) {
                library.select(digest);
                Json values = Json::object();
                for (const auto& [key, value] : previous.value("settings", Json::object()).items()) {
                    auto trial = values;
                    trial[key] = value;
                    try {
                        dkr::mods::code::configuration(*package, trial);
                        values = std::move(trial);
                    } catch (...) {
                        warnings.push_back(
                            "Setting '" + key +
                            "' changed in this release. Its original value is kept in the transaction backup.");
                    }
                }
                library.settings(digest, values);
                if (previous.contains("data"))
                    library.data(digest, previous["data"]);
            }
            // Retire the replaced catalogue edition even when another edition
            // (not this one) was selected. Never remove another receipt's blob.
            for (const auto& m : old.value("members", Json::array()))
                if (text(m, "kind", 40) == "code" && text(m, "digest", 64) != digest) {
                    bool shared = false;
                    for (const auto& r : registry["records"])
                        if (text(r, "instance", 32) != instance && !r.value("stored", false))
                            for (const auto& n : r.value("members", Json::array()))
                                if (text(n, "digest", 64) == text(m, "digest", 64))
                                    shared = true;
                    if (!shared)
                        library.remove(text(m, "digest", 64));
                }
        }
        if (prepared.native.contains("members") && !prepared.native.contains("codeId") && !prepared.native.value("vehicle",false)) {
            progress(.93F, "Updating the legacy library");
            // Build survivor groups in private storage. The live library never
            // receives a second, nested removal journal during Store commit.
            retire_legacy(old, members, stop);
            for (auto& n : members)
                for (const auto& m : old.value("members", Json::array()))
                    if (text(n, "kind", 40) == text(m, "kind", 40) && n.value("slot", -1) == m.value("slot", -2) &&
                        text(n, "revision", 32) == text(m, "revision", 32)) {
                        const auto kind = text(n, "kind", 40);
                        dkr::mods::TrackCatalog catalog(kind == "legacy-character"
                                                            ? dkr::mods::TrackCatalog::Kind::Character
                                                            : dkr::mods::TrackCatalog::Kind::Track);
                        catalog.configure(config / "mods/legacy", worker_path);
                        auto wait = [&] {
                            const auto deadline = Clock::now() + std::chrono::minutes(5);
                            while (catalog.snapshot()->busy) {
                                if (stop.stop_requested() || Clock::now() > deadline) {
                                    catalog.cancel();
                                    throw Failure(
                                        "Preserving the previous mod settings timed out. The update was rolled back.");
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            }
                            if (!catalog.snapshot()->succeeded)
                                throw Failure(catalog.snapshot()->result);
                        };
                        wait();
                        if (m.value("hidden", false)) {
                            if (!catalog.set_hidden(text(n, "id", 256), true))
                                throw Failure("Could not preserve hidden state.");
                            wait();
                        } else if (m.value("enabled", false)) {
                            if (!catalog.set_enabled(text(n, "id", 256), true))
                                throw Failure("Could not preserve enabled state.");
                            wait();
                        }
                        n["enabled"] = m.value("enabled", false);
                        n["hidden"] = m.value("hidden", false);
                    }
        }
        // Bundled resources can disappear in a publisher's next version.
        // Retire only this receipt's obsolete outputs, not other owners' files.
        for (const auto& m : old.value("members", Json::array())) {
            const auto kind = text(m, "kind", 40), path = text(m, "path", 4096);
            if ((kind != "texture" && kind != "track") || path.empty())
                continue;
            bool keep = false;
            for (const auto& n : members)
                if (text(n, "path", 4096) == path)
                    keep = true;
            for (const auto& r : registry["records"])
                if (text(r, "instance", 32) != instance && !r.value("stored", false))
                    for (const auto& n : r.value("members", Json::array()))
                        if (text(n, "path", 4096) == path)
                            keep = true;
            if (!keep && path_exists(child(config, path))) {
                protect(path);
                discard(config, child(config, path));
            }
        }
        progress(.96F, "Refreshing the affected library");
        restore_native(native_changes.first, native_changes.second);
    }
    if (!stored)
        for (auto& m : members) {
            const auto path = text(m, "path", 4096);
            if (!path.empty())
                m["content"] = text(m, "kind", 40) == "track" ? track_hash(child(config, path), stop)
                                                              : tree_hash(child(config, path), stop);
        }
    progress(.98F, "Saving the installation receipt");
    Json record = {{"instance", mode == "replace" && instance_id(instance) ? instance : uuid()},
                   {"item", proposed.item},
                   {"release", proposed.release},
                   {"title", proposed.title},
                   {"category", proposed.category},
                   {"version", proposed.version},
                   {"digest", proposed.digest},
                   {"members", members},
                   {"follow", old.value("follow", true)},
                   {"skip", ""},
                   {"origin", "Mod Store"},
                   {"stored", stored},
                   {"state", stored ? "Stored edition" : "Installed"},
                   {"file", path_text(proposed.file.lexically_relative(config))},
                   {"bytes", proposed.total},
                   {"transaction", path_text(transaction.filename())},
                   {"warnings", warnings},
                   {"prepared", {{"outputs", prepared.outputs}, {"native", prepared.native}}}};
    for (const auto& item : catalogue)
        if (text(item, "id", 160) == proposed.item)
            for (const auto& release : item.value("releases", Json::array()))
                if (text(release, "id", 160) == proposed.release)
                    record["publishedAt"] = text(release, "createdAt", 80);
    if (!old.empty()) {
        old.erase("previous");
        old.erase("exact");
        record["previous"] = old;
    }
    if (mode == "replace" && instance_id(instance))
        for (auto it = registry["records"].begin(); it != registry["records"].end();)
            if (text(*it, "instance", 32) == instance)
                it = registry["records"].erase(it);
            else
                ++it;
    registry["records"].push_back(record);
    save();
    journal["phase"] = "committed";
    write(transaction / "journal.json", journal);
    journal = Json::object();
    lease.reset();
    progress(1.F, stored ? "Edition saved" : "Mod installed");
    update([&](InstallView& s) {
        s.records = registry["records"];
        s.busy = false;
        s.choosing = false;
        s.succeeded = true;
        s.completed_key = proposed.key;
        s.result =
            stored
                ? "New edition saved. Use it from Installed mods; only one edition of this game identity can be active."
            : prepared.native.contains("codeId") ? "Code mod installed. Changed code needs approval before it can run."
                                                 : "Mod installed.";
        if (!warnings.empty())
            s.result += " Some settings need review; their previous values are backed up.";
        ++s.completion;
    });
}
void InstallManager::policy(std::string instance, bool follow, std::string skip) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (p.state.busy || !p.state.ready)
            return;
        p.state.busy = true;
        p.state.error.clear();
        p.state.stage = "Saving update preferences";
        p.publish();
    }
    if (!p.queue([&p, instance = std::move(instance), follow, skip = std::move(skip)](std::stop_token stop) {
            p.proposed = {};
            p.lease = std::make_unique<StoreLease>(p.root / "install.lock");
            p.load_registry();
            for (auto& r : p.registry["records"])
                if (text(r, "instance", 32) == instance) {
                    r["follow"] = follow;
                    r["skip"] = skip;
                }
            p.save();
            p.refresh(stop);
            p.lease.reset();
            p.update([](InstallView& s) { s.busy = false; });
        }))
        p.update([](InstallView& s) { s.busy = false; });
}
void InstallManager::link(std::string local_key, std::string item_id) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (p.state.busy || !p.state.ready)
            return;
        p.state.busy = true;
        p.state.error.clear();
        p.state.stage = "Linking installed mod";
        p.publish();
    }
    if (!p.queue([&p, local_key = std::move(local_key), item_id = std::move(item_id)](std::stop_token stop) {
            p.proposed = {};
            p.lease = std::make_unique<StoreLease>(p.root / "install.lock");
            p.load_registry();
            for (const auto& item : p.catalogue)
                if (text(item, "id", 160) == item_id)
                    for (const auto& local : p.inventory)
                        if (text(local, "key", 512) == local_key) {
                            for (const auto& r : p.registry["records"])
                                if (!r.value("stored", false))
                                    for (const auto& m : r["members"])
                                        if (same_native_member(m, local))
                                            throw Failure(
                                                "This local installation is already linked to a Store listing.");
                            const auto kind = text(local, "kind", 40), category = text(item, "category", 32);
                            if (!((kind == "code" && category == "magic") ||
                                  (kind == "texture" && category == "textures") ||
                                  (kind == "vehicle" && vehicle_store_category(category)) ||
                                  (kind == "legacy-character" && category == "characters") ||
                                  ((kind == "track" || kind == "legacy-track") && category == "tracks")))
                                throw Failure("Choose a Store listing for the same type of mod.");
                            // Explicit user association is not a proof of a particular release.
                            p.registry["records"].push_back({{"instance", uuid()},
                                                             {"item", item_id},
                                                             {"title", text(item, "title", 200)},
                                                             {"category", text(item, "category", 32)},
                                                             {"version", "Local version"},
                                                             {"release", ""},
                                                             {"digest", text(local, "digest", 64)},
                                                             {"members", Json::array({local})},
                                                             {"follow", true},
                                                             {"skip", ""},
                                                             {"stored", false},
                                                             {"origin", "Manually linked"}});
                            if (p.registry.contains("ignored")) {
                                auto kept = Json::array();
                                for (const auto& key : p.registry["ignored"])
                                    if (key != local_key)
                                        kept.push_back(key);
                                p.registry["ignored"] = kept;
                            }
                            p.save();
                            p.refresh(stop);
                            p.lease.reset();
                            p.update([](InstallView& s) { s.busy = false; });
                            return;
                        }
            p.lease.reset();
            p.update([](InstallView& s) { s.busy = false; });
        }))
        p.update([](InstallView& s) { s.busy = false; });
}
void InstallManager::forget(std::string instance, bool remove_files) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.safe || p.state.busy || !p.state.ready)
            return;
        p.state.busy = true;
        p.state.error.clear();
        p.publish();
    }
    if (!p.queue([&p, instance = std::move(instance), remove_files](std::stop_token stop) {
            p.remove_instance(instance, remove_files, stop);
        }))
        p.update([](InstallView& s) { s.busy = false; });
}
void InstallManager::Impl::remove_instance(std::string instance, bool remove_files, std::stop_token stop) {
    proposed = {};
    lease = std::make_unique<StoreLease>(root / "install.lock");
    load_registry();
    Json old = Json::object();
    for (const auto& record : registry["records"])
        if (text(record, "instance", 32) == instance)
            old = record;
    if (instance.starts_with("local:"))
        for (const auto& local : inventory)
            if ("local:" + text(local, "key", 512) == instance)
                old = {{"instance", instance}, {"members", Json::array({local})}, {"stored", false}};
    if (old.empty())
        throw Failure("This installation is no longer available.");
    {
        std::lock_guard lock(mutex);
        if (!safe)
            throw Failure("Leave the game or online session before removing mods.");
    }
    transaction = root / "transactions" / uuid();
    directories(transaction);
    begin_journal();
    if (instance.starts_with("local:") && remove_files) {
        const auto selected = old["members"].front();
        for (auto& record : registry["records"]) {
            auto kept = Json::array();
            for (const auto& m : record["members"])
                if (!(text(m, "kind", 40) == text(selected, "kind", 40) &&
                      text(m, "id", 256) == text(selected, "id", 256) &&
                      (text(m, "kind", 40) != "track" || member_path_identity(m) == member_path_identity(selected))))
                    kept.push_back(m);
            record["members"] = kept;
        }
    }
    if (!remove_files) {
        if (!registry.contains("ignored"))
            registry["ignored"] = Json::array();
        for (const auto& member : old["members"])
            registry["ignored"].push_back(member["key"]);
    }
    if (remove_files && !old.value("stored", false)) {
        const auto native_changes = protect_metadata(old["members"], Json::array());
        retire_legacy(old, Json::array(), stop);
        for (const auto& member : old["members"]) {
            const auto kind = text(member, "kind", 40), path = text(member, "path", 4096);
            bool shared = false;
            for (const auto& record : registry["records"])
                if (text(record, "instance", 32) != instance && !record.value("stored", false))
                    for (const auto& other : record.value("members", Json::array()))
                        if (same_native_member(member, other) || (!path.empty() && text(other, "path", 4096) == path))
                            shared = true;
            if (shared)
                continue;
            const bool known = std::any_of(inventory.begin(), inventory.end(),
                                           [&](const Json& local) { return same_native_member(member, local); });
            if (!known) {
                // A stale receipt must not delete a new, unrelated installation
                // which later reused the same directory or derived native ID.
                const auto expected = text(member, "content", 64);
                if (kind == "code" || path.empty() || !hash_string(expected) || !path_exists(child(config, path)))
                    continue;
                const auto actual =
                    kind == "track" ? track_hash(child(config, path), stop) : tree_hash(child(config, path), stop);
                if (actual != expected)
                    continue;
            }
            if (kind == "vehicle") {
                remove_store_vehicle(config,member,stop);
            } else if (kind == "code") {
                dkr::mods::code::Library library(config / "mods/custom-magic-codes", version);
                library.remove(text(member, "digest", 64));
            } else if ((kind == "texture" || kind == "track") && !path.empty() && path_exists(child(config, path))) {
                protect(path);
                if (kind == "track") {
                    if (known) {
                        std::string error;
                        if (!dkr::runtime::custom_tracks::uninstall(text(member, "id", 256), error))
                            throw Failure(error);
                    } else {
                        // The owned, proof-checked copy is shadowed by another
                        // native track with the same ID. Delete only this exact
                        // directory, never the other copy chosen by ID.
                        discard(config, child(config, path));
                    }
                } else
                    discard(config, child(config, path));
            }
        }
        restore_native(native_changes.first, native_changes.second);
    }
    for (auto it = registry["records"].begin(); it != registry["records"].end();)
        if (text(*it, "instance", 32) == instance)
            it = registry["records"].erase(it);
        else
            ++it;
    save();
    journal["phase"] = "committed";
    write(transaction / "journal.json", journal);
    journal = Json::object();
    lease.reset();
    update([&](InstallView& s) {
        s.records = registry["records"];
        s.busy = false;
        s.succeeded = true;
        s.completed_key.clear();
        s.result = remove_files ? "Selected installation removed. Its protected copy is recoverable in Mod Store "
                                  "history; saves and source downloads are kept."
                                : "Store association removed. The installed mod was kept.";
        ++s.completion;
    });
}
bool InstallManager::activate(std::string instance, bool previous, std::vector<fs::path> roms) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.safe || p.state.busy || !p.state.ready)
            return false;
        p.state.busy = true;
        p.state.error.clear();
        p.publish();
    }
    if (!p.queue([&p, instance = std::move(instance), previous, roms = std::move(roms)](std::stop_token stop) {
            Json record;
            for (const auto& r : p.registry["records"])
                if (text(r, "instance", 32) == instance)
                    record = r;
            if (record.is_null())
                throw Failure("That mod edition is no longer registered.");
            const auto selected = previous ? record.value("previous", Json::object()) : record;
            if (selected.empty())
                throw Failure("No previous edition is available.");
            Download job;
            job.item = text(selected, "item", 160);
            job.release = text(selected, "release", 160);
            job.title = text(selected, "title", 200);
            job.category = text(selected, "category", 32);
            job.version = text(selected, "version", 80);
            job.digest = text(selected, "digest", 64);
            job.total = selected.value("bytes", 0ULL);
            job.file = child(p.config, text(selected, "file", 4096));
            job.key = job.item + "_" + job.release;
            job.instance = instance;
            job.intent = "activate";
            p.proposed = job;
            p.prepare(roms, stop);
        })) {
        p.update([](InstallView& s) { s.busy = false; });
        return false;
    }
    return true;
}
} // namespace dkr::store
