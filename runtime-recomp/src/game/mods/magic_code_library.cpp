#include "magic_code_library.hpp"
#include "online_mod_bundle.hpp"
#include <algorithm>
#include <cerrno>
#include <functional>
#include <random>
#include <tuple>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dkr::mods::code {
namespace {
constexpr std::size_t CatalogLimit = 4 * MiB, LibraryLimit = 512, ResourceLimit = 256 * MiB;
[[noreturn]] void bad(const std::string& text) {
    throw Error("Custom Magic Codes: " + text);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested())
        bad("operation cancelled");
}
bool digest(std::string_view s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string nonce() {
    std::random_device random;
    Bytes bytes(16);
    for (auto& value : bytes)
        value = std::uint8_t(random());
    return sha256(bytes);
}
struct Lease {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    explicit Lease(const std::filesystem::path& root) {
        auto ancestor = root;
        while (!std::filesystem::exists(ancestor)) {
            const auto parent = ancestor.parent_path();
            if (parent == ancestor || parent.empty())
                bad("storage root is unavailable");
            ancestor = parent;
        }
        online::check_storage(ancestor, true);
        std::filesystem::create_directories(root);
        online::check_storage(root, true);
        const auto path = private_storage_path(root / "library.lock");
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            bad("the code library is busy or not writable");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            bad("unsafe library lock");
        }
#else
        handle = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0)
            bad("the code library is not writable");
        struct stat info {};
        if (fstat(handle, &info) || !S_ISREG(info.st_mode)) {
            close(handle);
            handle = -1;
            bad("unsafe library lock");
        }
        if (flock(handle, LOCK_EX | LOCK_NB)) {
            close(handle);
            handle = -1;
            bad("the code library is busy");
        }
#endif
    }
    ~Lease() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (handle >= 0)
            close(handle);
#endif
    }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
};
void child(const std::filesystem::path& path) {
    std::filesystem::create_directory(path);
    online::check_storage(path, true);
}
void sync_directory(const std::filesystem::path& path) {
#ifndef _WIN32
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        bad("cannot open code storage for a durability checkpoint");
    const int result = fsync(fd), failure = errno;
    close(fd);
    if (result && failure != EINVAL && failure != ENOTSUP)
        bad("cannot confirm code storage durability");
#else
    (void)path;
#endif
}
void rename_new(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(private_storage_path(source).c_str(), private_storage_path(destination).c_str(),
                     MOVEFILE_WRITE_THROUGH))
        bad("cannot publish a code library file");
#else
    if (std::filesystem::exists(destination))
        bad("code library destination already exists");
    std::filesystem::rename(source, destination);
    sync_directory(source.parent_path());
    if (destination.parent_path() != source.parent_path())
        sync_directory(destination.parent_path());
#endif
}
void quarantine(const std::filesystem::path& root, const std::filesystem::path& path) {
    online::check_storage(path, false);
    const auto directory = root / "quarantine";
    child(directory);
    const auto destination = directory / (path.filename().string() + "-" + nonce());
    rename_new(path, destination);
}
Json catalog(const std::filesystem::path& root) {
    const auto path = root / "library.json";
    if (!std::filesystem::exists(path))
        return {{"schema", 2}, {"items", Json::object()}, {"selected", Json::object()}};
    online::check_storage(path, false);
    auto doc = parse_document(read_file(path, CatalogLimit), CatalogLimit);
    if (doc.value("schema", 0) == 1 && doc.contains("selected") && doc["selected"].is_object()) {
        for (auto& [id, p] : doc["selected"].items()) {
            if (!p.is_object() || p.size() != 4)
                bad("invalid earlier code activation profile");
            p["data"] = Json::object();
        }
        doc["schema"] = 2;
    }
    if (doc.size() != 3 || doc.value("schema", 0) != 2 || !doc.contains("items") || !doc["items"].is_object() ||
        doc["items"].size() > LibraryLimit || !doc.contains("selected") || !doc["selected"].is_object() ||
        doc["selected"].size() > LibraryLimit)
        bad("invalid code library catalogue");
    for (const auto& [hash, item] : doc["items"].items()) {
        if (!digest(hash) || !item.is_object() || (item.size() != 7 && item.size() != 8) ||
            (item.size() == 8 && (!item.contains("presentation") || !item["presentation"].is_boolean())) ||
            (item.size() == 7 && item.contains("presentation")) || !item.contains("id") || !item["id"].is_string() ||
            !valid_id(item["id"].get<std::string>()) || !item.contains("kept_online") ||
            !item["kept_online"].is_boolean() || !item.contains("deterministic") || !item["deterministic"].is_boolean())
            bad("invalid code library entry");
        for (const auto* field : {"name", "version", "description"})
            if (!item.contains(field) || !item[field].is_string() ||
                item[field].get_ref<const std::string&>().size() > 32768)
                bad("invalid code library metadata");
        if (!item.contains("options") || !item["options"].is_array() || item["options"].size() > 128)
            bad("invalid cached settings schema");
        Package cached;
        cached.options = item["options"];
        configuration(cached, Json::object());
    }
    for (const auto& [id, p] : doc["selected"].items()) {
        if (!valid_id(id) || !p.is_object() || p.size() != 5 || !p.contains("digest") || !p["digest"].is_string() ||
            !digest(p["digest"].get<std::string>()) || !doc["items"].contains(p["digest"].get<std::string>()) ||
            doc["items"][p["digest"].get<std::string>()]["id"] != id || !p.contains("enabled") ||
            !p["enabled"].is_boolean() || !p.contains("approved") || !p["approved"].is_boolean() ||
            !p.contains("settings") || !p["settings"].is_object() || p["settings"].size() > 128 ||
            !p.contains("data") || (p["enabled"].get<bool>() && !p["approved"].get<bool>()))
            bad("invalid code activation profile");
        structured_data(p["data"]);
    }
    return doc;
}
void export_share_file(const std::filesystem::path& path, View bytes) {
    if (path.extension() != ".dkrcup")
        bad("choose a .dkrcup filename");
    const auto parent = std::filesystem::absolute(path).parent_path();
    online::check_storage(parent, true);
    if (std::filesystem::exists(path))
        online::check_storage(path, false);
    const auto temporary = parent / ("cup-export-" + nonce() + ".tmp");
    write_new_file(temporary, bytes);
    try {
#ifdef _WIN32
        if (!MoveFileExW(private_storage_path(temporary).c_str(), private_storage_path(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            bad("cannot finish exporting cups");
#else
        std::filesystem::rename(temporary, path);
        sync_directory(parent);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
void publish(const std::filesystem::path& root, const Json& doc) {
    const auto value = doc.dump();
    if (value.size() > CatalogLimit)
        bad("code library settings exceed their storage budget");
    const View bytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    const auto path = root / "library.json", history = root / "history";
    child(history);
    if (std::filesystem::exists(path)) {
        online::check_storage(path, false);
        const auto old = read_file(path, CatalogLimit);
        const auto backup = history / (sha256(old) + ".json");
        if (!std::filesystem::exists(backup))
            write_new_file(backup, old);
        else {
            online::check_storage(backup, false);
            if (std::filesystem::file_size(backup) > CatalogLimit || read_file(backup, CatalogLimit) != old) {
                quarantine(root, backup);
                write_new_file(backup, old);
            }
        }
        sync_directory(history);
    }
    const auto temporary = root / ("catalog-" + nonce() + ".tmp");
    write_new_file(temporary, bytes);
    try {
#ifdef _WIN32
        if (!MoveFileExW(private_storage_path(temporary).c_str(), private_storage_path(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            bad("cannot commit code library settings");
#else
        std::filesystem::rename(temporary, path);
        sync_directory(root);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
std::shared_ptr<const Bytes> read_payload(const std::filesystem::path& root, std::string_view hash) {
    if (!digest(hash))
        bad("invalid package identity");
    const auto path = root / "packages" / (std::string(hash) + ".dkrcode");
    online::check_storage(path, false);
    auto bytes = std::make_shared<Bytes>(read_file(path, MaxPackage));
    if (sha256(*bytes) != hash)
        bad("a stored code package is damaged; import the original again");
    return bytes;
}
const Json& item(const Json& doc, std::string_view hash) {
    if (!digest(hash) || !doc["items"].contains(std::string(hash)))
        bad("code package is not installed");
    return doc["items"][std::string(hash)];
}
Json& selection(Json& doc, std::string_view hash) {
    const auto id = item(doc, hash)["id"].get<std::string>();
    if (!doc["selected"].contains(id) || doc["selected"][id]["digest"].get_ref<const std::string&>() != hash)
        bad("select this package edition first");
    return doc["selected"][id];
}
} // namespace
Library::Library(std::filesystem::path root, std::string version)
    : root_(std::filesystem::absolute(root).lexically_normal()), version_(std::move(version)) {
    Version::parse(version_);
}
void Library::export_cups(const std::filesystem::path& path, View bytes) {
    export_share_file(path, bytes);
}
std::vector<LibraryItem> Library::scan(std::stop_token stop) const {
    Lease lease(root_);
    const auto doc = catalog(root_);
    std::vector<LibraryItem> result;
    for (const auto& [hash, item] : doc["items"].items()) {
        cancelled(stop);
        LibraryItem entry;
        entry.digest = hash;
        entry.id = item["id"].get<std::string>();
        entry.name = item["name"].get<std::string>();
        entry.version = item["version"].get<std::string>();
        entry.description = item["description"].get<std::string>();
        entry.kept_online = item["kept_online"].get<bool>();
        entry.deterministic = item["deterministic"].get<bool>();
        entry.presentation = item.value("presentation", false);
        entry.compatibility = compatibility_issue(hash);
        entry.options = item["options"];
        if (doc["selected"].contains(entry.id)) {
            const auto& p = doc["selected"][entry.id];
            entry.selected = p["digest"] == hash;
            if (entry.selected) {
                entry.enabled = p["enabled"].get<bool>();
                entry.approved = p["approved"].get<bool>();
                entry.settings = p["settings"];
                entry.data = p["data"];
            }
        }
        result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tie(a.name, a.id, a.version, a.digest) < std::tie(b.name, b.id, b.version, b.digest);
    });
    return result;
}
std::string Library::import(View bytes, bool kept_online) {
    const auto package = inspect(bytes, version_);
    Lease lease(root_);
    auto doc = catalog(root_);
    if (!doc["items"].contains(package->digest) && doc["items"].size() >= LibraryLimit)
        bad("code library is full");
    const auto directory = root_ / "packages";
    child(directory);
    const auto path = directory / (package->digest + ".dkrcode");
    if (std::filesystem::exists(path)) {
        online::check_storage(path, false);
        if (std::filesystem::file_size(path) > MaxPackage || sha256(read_file(path, MaxPackage)) != package->digest)
            quarantine(root_, path);
    }
    if (!std::filesystem::exists(path)) {
        const auto temporary = directory / ("incoming-" + nonce() + ".tmp");
        write_new_file(temporary, bytes);
        try {
            if (sha256(read_file(temporary, MaxPackage)) != package->digest)
                bad("written package did not pass verification");
            rename_new(temporary, path);
        } catch (...) {
            std::error_code ec;
            std::filesystem::remove(private_storage_path(temporary), ec);
            throw;
        }
    }
    const bool was_kept =
        doc["items"].contains(package->digest) && doc["items"][package->digest].value("kept_online", false);
    doc["items"][package->digest] = {{"id", package->id},
                                     {"name", package->name},
                                     {"version", package->version},
                                     {"description", package->description},
                                     {"deterministic", package->deterministic},
                                     {"presentation", package->modules.contains("presentation")},
                                     {"kept_online", kept_online || was_kept},
                                     {"options", package->options}};
    // Network receipt NEVER selects, approves, enables or reconfigures an
    // offline edition. A manual import only selects an initially disabled one.
    if (!kept_online && !doc["selected"].contains(package->id))
        doc["selected"][package->id] = {{"digest", package->digest},
                                        {"enabled", false},
                                        {"approved", false},
                                        {"settings", configuration(*package, Json::object())},
                                        {"data", Json::object()}};
    publish(root_, doc);
    return package->digest;
}
void Library::select(std::string_view hash) {
    Lease lease(root_);
    auto doc = catalog(root_);
    const auto p = inspect(*read_payload(root_, hash), version_);
    item(doc, hash);
    if (doc["selected"].contains(p->id) && doc["selected"][p->id]["digest"].get_ref<const std::string&>() == hash)
        return;
    auto data = Json::object();
    // Same schema, same first-party sample identity: retain the user's cups
    // while replacing the faulty executable. Trust/enabled state NEVER moves
    // to a different digest, and arbitrary third-party edition data does not
    // silently migrate across incompatible schemas.
    if (p->id == "dkr_custom_trophies" && p->modules.contains("trophies") && doc["selected"].contains(p->id)) {
        const auto& previous = doc["selected"][p->id].at("data");
        if (previous.contains("trophies"))
            data["trophies"] = previous.at("trophies");
    }
    doc["selected"][p->id] = {{"digest", hash},
                              {"enabled", false},
                              {"approved", false},
                              {"settings", configuration(*p, Json::object())},
                              {"data", structured_data(data)}};
    publish(root_, doc);
}
void Library::approve(std::string_view hash, bool approved) {
    Lease lease(root_);
    auto doc = catalog(root_);
    auto& p = selection(doc, hash);
    p["approved"] = approved;
    if (!approved)
        p["enabled"] = false;
    publish(root_, doc);
}
void Library::enable(std::string_view hash, bool enabled) {
    if (enabled)
        if (const auto issue = compatibility_issue(hash); !issue.empty())
            bad(issue);
    Lease lease(root_);
    auto doc = catalog(root_);
    auto& p = selection(doc, hash);
    if (enabled && !p["approved"].get<bool>())
        bad("approve executable code before enabling this package");
    p["enabled"] = enabled;
    publish(root_, doc);
}
void Library::settings(std::string_view hash, const Json& values) {
    Lease lease(root_);
    auto doc = catalog(root_);
    auto& profile = selection(doc, hash);
    const auto p = inspect(*read_payload(root_, hash), version_);
    profile["settings"] = configuration(*p, values);
    publish(root_, doc);
}
void Library::data(std::string_view hash, const Json& values) {
    const auto checked = structured_data(values);
    Lease lease(root_);
    auto doc = catalog(root_);
    selection(doc, hash)["data"] = checked;
    publish(root_, doc);
}
void Library::remove(std::string_view hash) {
    Lease lease(root_);
    auto doc = catalog(root_);
    const auto id = item(doc, hash)["id"].get<std::string>();
    if (doc["selected"].contains(id) && doc["selected"][id]["digest"].get_ref<const std::string&>() == hash)
        doc["selected"].erase(id);
    doc["items"].erase(std::string(hash));
    publish(root_, doc);
    // Content-addressed blobs remain recoverable in storage/history. Active
    // session pins and another profile must not be invalidated by UI cleanup.
}
std::shared_ptr<const Bytes> Library::payload(std::string_view hash) const {
    Lease lease(root_);
    return read_payload(root_, hash);
}
FrozenSet Library::freeze(std::string_view revision, bool online, const std::map<std::string, unsigned>& modules,
                          std::stop_token stop, bool presentation) const {
    Lease lease(root_);
    const auto doc = catalog(root_);
    FrozenSet out;
    std::vector<std::shared_ptr<const Package>> packages;
    std::set<std::string> enabled;
    std::size_t total = 0;
    for (const auto& [id, profile] : doc["selected"].items())
        if (profile["enabled"].get<bool>()) {
            cancelled(stop);
            if (!profile["approved"].get<bool>())
                bad("unapproved code package was selected");
            auto bytes = read_payload(root_, profile["digest"].get<std::string>());
            auto p = inspect(*bytes, version_);
            if (p->id != id)
                bad("package ID differs from its offline profile");
            // Derive the domain from authenticated package metadata, never the
            // mutable catalogue hint. Local cameras cannot enter race/save hashes.
            if (p->modules.contains("presentation") != presentation)
                continue;
            std::size_t resource_bytes = bytes->size() + p->binary.size() * 2;
            for (const auto& [name, r] : p->resources)
                resource_bytes += r.size();
            if (resource_bytes > ResourceLimit - total)
                bad("enabled package resources exceed the session budget");
            total += resource_bytes;
            enabled.insert(id);
            out.settings.emplace(id, configuration(*p, profile["settings"]));
            out.data.emplace(id, structured_data(profile["data"]));
            out.payloads.emplace(p->digest, std::move(bytes));
            packages.push_back(std::move(p));
        }
    out.ordered = resolve(packages, enabled, revision, presentation ? false : online, modules);
    out.identity = session_identity(out.ordered, out.settings, revision, out.data);
    return out;
}
} // namespace dkr::mods::code
