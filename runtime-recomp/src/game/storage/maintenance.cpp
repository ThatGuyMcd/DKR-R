#include "maintenance.hpp"
#include "updates/update_core.hpp"
#include "updates/update_process.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dkr::storage {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Time = fs::file_time_type;
constexpr std::uint64_t MiB = 1024 * 1024;
fs::path from_utf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}
std::string utf8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
fs::path native(fs::path p) {
    p = fs::absolute(p).lexically_normal().make_preferred();
#ifdef _WIN32
    auto s = p.native();
    if (!s.starts_with(L"\\\\?\\"))
        p = s.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + s.substr(2) : L"\\\\?\\" + s;
#endif
    return p;
}
bool hex(const std::string& s, std::size_t n = 64) {
    return s.size() == n &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool beneath(const fs::path& p, const fs::path& root) {
    const auto rel = p.lexically_relative(root);
    return !rel.empty() && rel != "." && *rel.begin() != ".." && !rel.is_absolute();
}
void ordinary(fs::path p) {
    p = native(p);
    const auto s = fs::symlink_status(p);
    if (s.type() != fs::file_type::regular && s.type() != fs::file_type::directory)
        throw std::runtime_error("Linked or unknown storage was left untouched.");
#ifdef _WIN32
    const auto flags = GetFileAttributesW(p.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Linked storage was left untouched.");
#endif
    if (fs::is_regular_file(s) && fs::hard_link_count(p) != 1)
        throw std::runtime_error("Shared file was left untouched.");
}
bool present(const fs::path& p) {
    std::error_code ec;
    return fs::symlink_status(native(p), ec).type() != fs::file_type::not_found && !ec;
}
bool checked(fs::path p, fs::path root) {
    p = native(p);
    root = native(root);
    if (!beneath(p, root))
        return false;
    auto at = root;
    ordinary(at);
    for (const auto& part : p.lexically_relative(root)) {
        at /= part;
        ordinary(at);
    }
    return beneath(native(fs::weakly_canonical(p)), native(fs::canonical(root)));
}
Json read(fs::path p, std::size_t max = 16 * MiB) {
    p = native(p);
    if (!present(p))
        return Json::object();
    ordinary(p);
    if (fs::file_size(p) > max)
        throw std::runtime_error("Storage metadata is too large; files were kept.");
    return Json::parse(dkr::updates::read_text(p, max));
}
std::string text(const Json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() && it->get_ref<const std::string&>().size() <= 32768
               ? it->get<std::string>()
               : "";
}
fs::path member(const fs::path& root, const std::string& s) {
    if (s.empty() || s.find_first_of("\\:") != s.npos)
        throw std::runtime_error("Invalid storage receipt; files were kept.");
    const auto rel = from_utf8(s);
    if (rel.is_absolute() || rel.has_root_path())
        throw std::runtime_error("Invalid storage receipt.");
    for (const auto& part : rel)
        if (part == "." || part == ".." || part.empty())
            throw std::runtime_error("Invalid storage receipt.");
    return (root / rel).make_preferred();
}
bool managed(const std::string& p) {
    return p.starts_with("texture-packs/") || p.starts_with("custom-tracks/") || p.starts_with("mods/legacy/") ||
           p.starts_with("mods/vehicles/") || p.starts_with("mods/custom-magic-codes/");
}
struct Engine {
    fs::path root, install;
    Options options;
    View result;
    Json document(const fs::path& p, std::size_t max = 16 * MiB) {
        if (present(p) && !checked(p, root) && !(!install.empty() && checked(p, install)))
            throw std::runtime_error("Storage metadata points outside its owner; files were kept.");
        return read(p, max);
    }
    void stop() const {
        if (options.stop.stop_requested() || publishers_waiting.load())
            throw std::runtime_error("Storage work paused.");
    }
    Row size(fs::path p) {
        p = native(p);
        Row row;
        if (!present(p))
            return row;
        ordinary(p);
        auto file = [&](const fs::path& f) {
            stop();
            ordinary(f);
            ++row.files;
            row.bytes += fs::file_size(f);
        };
        if (fs::is_regular_file(p))
            file(p);
        else
            for (const auto& e : fs::recursive_directory_iterator(p)) {
                stop();
                ordinary(e.path());
                if (e.is_regular_file())
                    file(e.path());
                if (row.files > 1000000)
                    throw std::runtime_error("Storage scan limit reached.");
            }
        return row;
    }
    void discard_retired(fs::path job) {
        job = native(job);
        const auto marker = document(job / "retirement.json", 8192);
        if (marker.value("schema", 0) != 1 || text(marker, "owner") != "DKR-R storage")
            return;
        const auto payload = job / "payload";
        if (present(payload)) {
            if (!checked(payload, root))
                return;
            (void)size(payload); // Reject links and unexpected nodes before any deletion.
            if (options.dry_run) {
                const auto bytes = size(payload);
                result.removed_bytes += bytes.bytes;
                result.removed_files += bytes.files;
                return;
            }
            std::vector<fs::path> files, dirs;
            if (fs::is_directory(payload))
                for (const auto& e : fs::recursive_directory_iterator(payload)) {
                    stop();
                    ordinary(e.path());
                    (e.is_directory() ? dirs : files).push_back(e.path());
                }
            else
                files.push_back(payload);
            for (const auto& file : files) {
                stop();
                ordinary(file);
                const auto bytes = fs::file_size(file);
                if (fs::remove(file)) {
                    result.removed_bytes += bytes;
                    ++result.removed_files;
                }
            }
            std::sort(dirs.begin(), dirs.end(),
                      [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
            for (const auto& dir : dirs) {
                stop();
                ordinary(dir);
                fs::remove(dir);
            }
            if (present(payload)) {
                stop();
                ordinary(payload);
                fs::remove(payload);
            }
        }
        stop();
        fs::remove(job / "retirement.json");
        fs::remove(job);
    }
    void retired() {
        const auto base = native(root / "storage/retired");
        for (const auto& p : children(base))
            if (hex(utf8(p.filename()), 32) && fs::is_directory(p))
                discard_retired(p);
    }
    void erase(fs::path p, fs::path owner) {
        p = native(p);
        owner = native(owner);
        stop();
        if (!present(p) || !checked(p, owner))
            return;
        const auto bytes = size(p);
        stop();
        if (!checked(p, owner))
            return;
        if (options.dry_run) {
            result.removed_bytes += bytes.bytes;
            result.removed_files += bytes.files;
            return;
        }
        if (beneath(p, root)) {
            // Retirement makes an interrupted cleanup resumable without
            // leaving half of a published transaction at its original path.
            const auto retired_base = native(root / "storage/retired");
            for (const auto& dir : {root / "storage", retired_base}) {
                if (!present(dir))
                    fs::create_directory(dir);
                if (!checked(dir, root))
                    return;
            }
            const auto job = retired_base / dkr::updates::unique_id();
            fs::create_directory(job);
            dkr::updates::durable_write(
                job / "retirement.json",
                Json{{"schema", 1}, {"owner", "DKR-R storage"}, {"source", utf8(p.lexically_relative(root))}}.dump());
            stop();
            fs::rename(p, job / "payload");
            discard_retired(job);
        } else {
            // Install backups are on the installation volume. Only old,
            // completed backups reach here; the newest remains protected.
            fs::remove_all(p);
            result.removed_bytes += bytes.bytes;
            result.removed_files += bytes.files;
        }
    }
    bool old(fs::path p, unsigned days) {
        p = native(p);
        return present(p) && Time::clock::now() - fs::last_write_time(p) > std::chrono::hours(24ULL * days);
    }
    std::vector<fs::path> children(fs::path p) {
        p = native(p);
        std::vector<fs::path> out;
        if (!present(p))
            return out;
        if (p != root && !checked(p, root))
            return out;
        ordinary(p);
        for (const auto& e : fs::directory_iterator(p)) {
            stop();
            try {
                ordinary(e.path());
                out.push_back(e.path());
            } catch (const std::exception&) {
            }
        }
        return out;
    }
    std::string digest(fs::path p, bool track = false) {
        p = native(p);
        ordinary(p);
        stop();
        if (fs::is_regular_file(p))
            return dkr::updates::hash_file(p, 16ULL * 1024 * MiB);
        std::map<std::string, std::string> files;
        for (const auto& e : fs::recursive_directory_iterator(p)) {
            stop();
            ordinary(e.path());
            if (track && e.path().lexically_relative(p) == ".online-keep-state")
                continue;
            if (e.is_regular_file())
                files[utf8(e.path().lexically_relative(p))] = dkr::updates::hash_file(e.path(), 16ULL * 1024 * MiB);
        }
        const auto data = Json(files).dump();
        return dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    }
    std::set<std::string> transactions, packages, installed, recorded;
    Json registry = Json::object();
    bool registry_valid = false;
    void receipts() {
        registry = document(root / "mod-store/installations.json");
        const bool missing_registry = !present(root / "mod-store/installations.json");
        if (registry.empty()) {
            if (!missing_registry)
                throw std::runtime_error("Mod receipts need recovery.");
            registry = {{"schema", 1}, {"records", Json::array()}};
        }
        if (registry.value("schema", 0) != 1 || !registry.contains("records") || !registry["records"].is_array() ||
            registry["records"].size() > 4096)
            throw std::runtime_error("Mod receipts need recovery. Mod storage was kept.");
        for (const auto& r : registry["records"]) {
            for (const auto& row : r.contains("previous") ? Json::array({r, r["previous"]}) : Json::array({r})) {
                const auto tx = text(row, "transaction"), hash = text(row, "digest");
                if (!tx.empty()) {
                    if (!hex(tx, 32))
                        throw std::runtime_error("Invalid mod transaction receipt.");
                    transactions.insert(tx);
                }
                if (!hash.empty() && !hex(hash))
                    throw std::runtime_error("Invalid mod package receipt.");
                if (!hash.empty())
                    recorded.insert(hash);
                if (!row.contains("members") || !row["members"].is_array())
                    throw std::runtime_error("Incomplete mod receipt.");
                for (const auto& m : row["members"]) {
                    const auto p = text(m, "path");
                    if (!managed(p))
                        throw std::runtime_error("Unrecognised mod receipt.");
                    (void)member(root, p);
                }
            }
            if (r.value("stored", false))
                packages.insert(text(r, "digest"));
            if (r.contains("previous"))
                packages.insert(text(r["previous"], "digest"));
            bool installed_present = !r.value("stored", false) && !r["members"].empty();
            for (const auto& m : r["members"]) {
                const auto p = member(root, text(m, "path"));
                installed_present = installed_present && present(p) && checked(p, root);
            }
            if (installed_present)
                installed.insert(text(r, "digest"));
        }
        for (const auto& tx : children(root / "mod-store/transactions")) {
            const auto id = utf8(tx.filename());
            if (!hex(id, 32) || !fs::is_directory(tx))
                continue;
            const auto j = document(tx / "journal.json");
            if (missing_registry && text(j, "phase") == "committed")
                throw std::runtime_error("The installation registry is missing; recovery files were kept.");
            if (!j.empty() && text(j, "id") == id && text(j, "phase") != "committed" &&
                text(j, "phase") != "rolled-back")
                throw std::runtime_error("An unfinished mod installation needs recovery; its files were kept.");
        }
        registry_valid = true;
    }
    void store() {
        const auto base = root / "mod-store", txs = base / "transactions";
        if (!registry_valid)
            return;
        // Compaction is permitted only for the current installed edition, and
        // only after comparing the published content with its recorded hash.
        for (const auto& r : registry["records"]) {
            stop();
            if (r.value("stored", false) || !r.contains("prepared"))
                continue;
            const auto id = text(r, "transaction");
            if (!hex(id, 32))
                continue;
            const auto tx = txs / id;
            if (!present(tx) || !checked(tx, root))
                continue;
            const auto journal = document(tx / "journal.json");
            if (text(journal, "phase") != "committed" || text(journal, "id") != id)
                continue;
            const auto outputs = r["prepared"].value("outputs", Json::array());
            if (!outputs.is_array() || outputs.empty())
                continue;
            bool verified = true;
            std::set<fs::path> sources;
            for (const auto& o : outputs) {
                const auto source_text = text(o, "source"), target = text(o, "target"), hash = text(o, "content");
                if (source_text.empty() || !managed(target) || !hex(hash)) {
                    verified = false;
                    break;
                }
                const auto source = native(from_utf8(source_text)), dest = member(root, target);
                if (!beneath(source, tx) || !present(dest) || !checked(dest, root)) {
                    verified = false;
                    break;
                }
                if (present(source)) {
                    if (!checked(source, root) || digest(source) != hash || digest(dest) != hash) {
                        verified = false;
                        break;
                    }
                    sources.insert(tx / *source.lexically_relative(tx).begin());
                }
            }
            if (verified)
                for (const auto& p : sources)
                    if (p.filename() == "payload" || p.filename() == "legacy" || p.filename() == "hd")
                        erase(p, tx);
        }
        std::vector<fs::path> completed;
        std::uint64_t total = 0;
        for (const auto& tx : children(txs)) {
            const auto id = utf8(tx.filename());
            if (!hex(id, 32) || !fs::is_directory(tx) || transactions.contains(id))
                continue;
            const auto j = document(tx / "journal.json");
            if (j.empty()) {
                if (document(tx / "preparing.json").value("id", "") == id && old(tx, 7))
                    erase(tx, txs);
            } else if (text(j, "id") == id && (text(j, "phase") == "committed" || text(j, "phase") == "rolled-back")) {
                completed.push_back(tx);
                total += size(tx).bytes;
            }
        }
        std::sort(completed.begin(), completed.end(),
                  [](const auto& a, const auto& b) { return fs::last_write_time(a) > fs::last_write_time(b); });
        for (std::size_t i = completed.size(); i-- > 0;) {
            const auto& p = completed[i];
            if (options.action == Action::TrimHistory || i >= options.history_count || old(p, 30) ||
                total > options.history_budget) {
                const auto bytes = size(p).bytes;
                erase(p, txs);
                total -= bytes;
            }
        }
    }
    void downloads() {
        if (!registry_valid)
            return;
        const auto base = root / "mod-store/downloads";
        const auto history = document(root / "mod-store/downloads.json");
        if (history.contains("jobs") && !history["jobs"].is_array())
            return;
        std::set<std::string> pending, completed, failed;
        for (const auto& row : history.value("jobs", Json::array())) {
            const auto state = text(row, "state"), hash = text(row, "sha256");
            if (state == "Installed" && hex(hash))
                completed.insert(hash);
            if ((state == "Failed" || state == "Install failed") && hex(hash))
                failed.insert(hash);
            if (state != "Installed" && state != "Failed" && state != "Install failed" && hex(hash))
                pending.insert(hash);
        }
        for (const auto& dir : children(base)) {
            const auto hash = utf8(dir.filename());
            if (!hex(hash) || !fs::is_directory(dir) || pending.contains(hash))
                continue;
            const auto entries = children(dir);
            const auto archives = std::count_if(entries.begin(), entries.end(), [](const auto& p) {
                const auto ext = utf8(p.extension());
                return ext == ".zip" || ext == ".rtz" || ext == ".dkrmap" || ext == ".xdelta" || ext == ".nrm" ||
                       ext == ".dkrcode" || ext == ".7z" || ext == ".ips" || ext == ".bps";
            });
            bool disposable =
                !packages.contains(hash) &&
                (installed.contains(hash) || (!recorded.contains(hash) && ((completed.contains(hash) && old(dir, 30)) ||
                                                                           (failed.contains(hash) && old(dir, 7)))));
            if (disposable && archives && recorded.contains(hash)) {
                bool verified = false;
                for (const auto& r : registry["records"])
                    if (text(r, "digest") == hash && !r.value("stored", false) && !r["members"].empty()) {
                        bool complete = true;
                        for (const auto& m : r["members"]) {
                            const auto p = member(root, text(m, "path"));
                            const auto expected = text(m, "content");
                            if (!hex(expected) || !present(p) || !checked(p, root) ||
                                digest(p, text(m, "kind") == "track") != expected) {
                                complete = false;
                                break;
                            }
                        }
                        verified |= complete;
                    }
                disposable = verified;
            }
            std::vector<fs::path> verified;
            for (const auto& p : entries) {
                const auto ext = utf8(p.extension());
                if (ext == ".zip" || ext == ".rtz" || ext == ".dkrmap" || ext == ".xdelta" || ext == ".nrm" ||
                    ext == ".dkrcode" || ext == ".7z" || ext == ".ips" || ext == ".bps") {
                    if ((disposable || archives > 1) && digest(p) == hash)
                        verified.push_back(p);
                } else if (p.filename() == "download.part" && old(p, 7) && !packages.contains(hash))
                    erase(p, dir);
                else if (utf8(p.filename()).starts_with("verified-") && ext == ".tmp" && old(p, 7))
                    erase(p, dir);
            }
            // Previous editions keep one verified archive. Activation can find
            // it by digest even when a historical filename was an alias.
            std::set<fs::path> referenced;
            for (const auto& r : registry["records"])
                for (const auto& row : r.contains("previous") ? Json::array({r, r["previous"]}) : Json::array({r}))
                    if (text(row, "digest") == hash && !text(row, "file").empty())
                        referenced.insert(member(root, text(row, "file")));
            std::sort(verified.begin(), verified.end(), [&](const auto& a, const auto& b) {
                return std::pair{!referenced.contains(a), utf8(a.filename())} <
                       std::pair{!referenced.contains(b), utf8(b.filename())};
            });
            bool kept = false;
            for (const auto& p : verified) {
                if (disposable || kept)
                    erase(p, dir);
                else
                    kept = true;
            }
        }
    }
    void images() {
        const auto base = root / "mod-store/images";
        auto files = children(base);
        std::uint64_t total = 0;
        std::erase_if(files, [](const auto& p) { return !hex(utf8(p.filename())) || !fs::is_regular_file(p); });
        for (const auto& p : files)
            total += fs::file_size(p);
        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) { return fs::last_write_time(a) < fs::last_write_time(b); });
        for (const auto& p : files)
            if (options.action == Action::ClearCaches || old(p, 30) || total > options.image_budget) {
                const auto bytes = fs::file_size(p);
                erase(p, base);
                total -= bytes;
            }
    }
    void online() {
        const auto base = root / "online-mods";
        std::vector<fs::path> candidates;
        std::uint64_t total = 0;
        for (const auto* name :
             {"profiles", "source-profiles", "prepared-payloads", "payloads", "partials", "quarantined-payloads"})
            for (const auto& p : children(base / name)) {
                const auto n = utf8(p.filename());
                if (!hex(n) && !(std::string(name) == "quarantined-payloads" && n.size() == 129 &&
                                 hex(n.substr(0, 64)) && hex(n.substr(65))))
                    continue;
                candidates.push_back(p);
                total += size(p).bytes;
            }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return fs::last_write_time(a) < fs::last_write_time(b); });
        std::set<fs::path> retiring_indexes;
        for (const auto& p : candidates)
            if (p.parent_path().filename() == "source-profiles" && old(p, 1) &&
                (options.action == Action::ClearCaches || old(p, 30) || total > options.online_budget))
                retiring_indexes.insert(p);
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.parent_path().filename() == "source-profiles" && b.parent_path().filename() != "source-profiles";
        });
        // Sources indexes may refer to prepared payload directories. Retain
        // those dependencies while their indexes are retained, even over quota.
        std::set<fs::path> pinned;
        for (const auto& p : candidates)
            if (p.parent_path().filename() == "source-profiles" && !retiring_indexes.contains(p)) {
                const auto refs = document(p / "sources.json");
                auto pin = [&](const std::string& s) {
                    if (s.empty())
                        return;
                    const auto q = native(from_utf8(s));
                    if (!beneath(q, base))
                        return;
                    const auto rel = q.lexically_relative(base);
                    auto it = rel.begin();
                    auto scope = *it++;
                    if (it != rel.end())
                        pinned.insert(base / scope / *it);
                };
                for (const auto& r : refs.value("legacy", Json::array()))
                    pin(text(r, "library"));
                for (const auto& r : refs.value("courses", Json::array()))
                    if (r.is_string())
                        pin(r.get<std::string>());
                for (const auto& r : refs.value("code", Json::object()))
                    if (r.is_string())
                        pin(r.get<std::string>());
            }
        for (const auto& p : candidates) {
            if (pinned.contains(p) || !old(p, 1))
                continue;
            if (retiring_indexes.contains(p) || options.action == Action::ClearCaches || old(p, 30) ||
                total > options.online_budget) {
                const auto bytes = size(p).bytes;
                erase(p, p.parent_path());
                total -= bytes;
            }
        }
    }
    void abandoned() {
        for (const auto& p : children(root / "mod-store/verification")) {
            const auto id = utf8(p.filename());
            if (!id.empty() && id.size() <= 160 &&
                id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == id.npos &&
                fs::is_directory(p) && old(p, 7))
                erase(p, p.parent_path());
        }
        for (const auto& p : children(root / "mod-store/vehicle-uploads"))
            if (hex(utf8(p.filename())) && fs::is_directory(p) && old(p, 30))
                erase(p, p.parent_path());
        for (const auto* name :
             {"online-mods/jobs", "mods/legacy/jobs", "mods/legacy/prepare-jobs", "mods/legacy/online-keeps"})
            for (const auto& p : children(root / name))
                if (hex(utf8(p.filename())) && fs::is_directory(p) && old(p, 7) &&
                    (present(p / "request.json") || present(p / "failure.json") || present(p / "content")))
                    erase(p, root / name);
        for (const auto& p : children(root / "texture-packs"))
            if (utf8(p.filename()).ends_with(".rice.importing") && fs::is_directory(p) && old(p, 7))
                erase(p, root / "texture-packs");
        for (const auto& p : children(root)) {
            const auto name = utf8(p.filename()), prefix = std::string(".custom-track-import-");
            if (name.starts_with(prefix) && name.size() > prefix.size() && fs::is_directory(p) && old(p, 7) &&
                std::all_of(name.begin() + prefix.size(), name.end(), [](char c) { return c >= '0' && c <= '9'; }))
                erase(p, root);
        }
    }
    void immutable_libraries() {
        for (const auto* name : {"mods/custom-magic-codes", "mods/vehicles"}) {
            const auto base = root / name;
            const auto current = document(base / "library.json");
            const bool code = std::string(name) == "mods/custom-magic-codes";
            const auto schema = current.value("schema", 0);
            if (current.empty() || (schema != 1 && !(code && schema == 2)) || !current.contains("items") ||
                !current["items"].is_object())
                continue;
            std::set<std::string> keep;
            for (const auto& [hash, value] : current["items"].items()) {
                (void)value;
                if (!hex(hash))
                    throw std::runtime_error("Invalid installed package index.");
                keep.insert(hash);
            }
            for (const auto& selected : current.value("selected", Json::object()))
                if (!current["items"].contains(text(selected, "digest")))
                    throw std::runtime_error("Installed package selections need recovery.");
            auto history = children(base / "history");
            std::erase_if(history, [](const auto& p) { return p.extension() != ".json" || !hex(utf8(p.stem())); });
            std::sort(history.begin(), history.end(),
                      [](const auto& a, const auto& b) { return fs::last_write_time(a) > fs::last_write_time(b); });
            std::map<fs::path, Json> saved_history;
            for (const auto& p : history) {
                const auto saved = document(p);
                const auto saved_schema = saved.value("schema", 0);
                if ((saved_schema != 1 && !(code && saved_schema == 2)) || !saved.contains("items") ||
                    !saved["items"].is_object() || digest(p) != utf8(p.stem()))
                    throw std::runtime_error("Package recovery history needs checking; original packages were kept.");
                saved_history.emplace(p, saved);
            }
            for (std::size_t i = 0; i < history.size(); ++i) {
                if ((i >= 20 && old(history[i], 30)) || options.action == Action::TrimHistory)
                    erase(history[i], base / "history");
                else {
                    const auto& saved = saved_history.at(history[i]);
                    for (const auto& [hash, value] : saved["items"].items()) {
                        (void)value;
                        keep.insert(hash);
                    }
                }
            }
            // Previous/stored editions may name a package absent from the current index.
            for (const auto& r : registry.value("records", Json::array()))
                for (const auto& row : r.contains("previous") ? Json::array({r, r["previous"]}) : Json::array({r}))
                    for (const auto& m : row.value("members", Json::array())) {
                        const auto p = text(m, "path");
                        if (p.starts_with(std::string(name) + "/packages/"))
                            keep.insert(from_utf8(p).stem().string());
                    }
            for (const auto* folder : {"packages", "portraits"})
                for (const auto& p : children(base / folder)) {
                    const auto ext = utf8(p.extension());
                    const bool owned =
                        std::string(folder) == "portraits"
                            ? ext == ".rgba"
                            : ext == ".dkrcode" || ext == ".dkrkart" || ext == ".dkrhover" || ext == ".dkrplane";
                    if (owned && fs::is_regular_file(p) && hex(utf8(p.stem())) && !keep.contains(utf8(p.stem())) &&
                        old(p, 30) && (std::string(folder) == "portraits" || digest(p) == utf8(p.stem())))
                        erase(p, base / folder);
                }
        }
    }
    void updates() {
        const auto base = root / "updates";
        std::set<std::string> protected_packages;
        bool unresolved = false, unknown = false, success = false;
        for (const auto& p : children(base)) {
            const auto n = utf8(p.filename());
            if (!n.starts_with("handoff-") || !hex(n.substr(8), 32) || !fs::is_directory(p))
                continue;
            const auto result = document(p / "result.json");
            const auto state = text(result, "state");
            if (state != "complete" && state != "recovered") {
                unresolved = true;
                const auto m = document(p / "manifest.json");
                if (!m.contains("artifacts") || !m["artifacts"].is_array())
                    unknown = true;
                else
                    for (const auto& a : m["artifacts"])
                        if (hex(text(a, "sha256")))
                            protected_packages.insert(text(a, "sha256"));
                continue;
            }
            success |= state == "complete";
            // Keep request, signed metadata, journals and the last result. Only
            // the copied helper is disposable after the helper has exited.
            for (const auto& f : children(p))
                if (fs::is_regular_file(f) &&
                    (f.filename() == "DKR-R-Updater.exe" || f.filename() == "DKR-R-Updater") && old(f, 1))
                    erase(f, p);
        }
        if (success && !unknown)
            for (const auto& p : children(base)) {
                const auto n = utf8(p.filename());
                if (!n.starts_with("download-") || !hex(n.substr(9)) || !fs::is_directory(p))
                    continue;
                if (protected_packages.contains(n.substr(9)))
                    continue;
                // The signed current manifest may describe an update not installed
                // yet. Keep its package/partial until that exact version succeeds.
                bool exact = false;
                for (const auto& h : children(base))
                    if (utf8(h.filename()).starts_with("handoff-") && present(h / "result.json")) {
                        const auto r = document(h / "result.json");
                        if (text(r, "state") != "complete")
                            continue;
                        const auto m = document(h / "manifest.json");
                        if (m.contains("artifacts") && m["artifacts"].is_array())
                            for (const auto& a : m["artifacts"])
                                if (text(a, "sha256") == n.substr(9))
                                    exact = true;
                    }
                if (exact)
                    erase(p, base);
            }
        if (install.empty() || !present(install))
            return;
        if (success && !unresolved) {
            std::vector<std::pair<Time, fs::path>> images;
            for (const auto& h : children(base)) {
                const auto n = utf8(h.filename());
                if (!n.starts_with("handoff-") || !hex(n.substr(8), 32))
                    continue;
                const auto r = document(h / "result.json"), request = document(h / "request.json"),
                           j = document(h / "single-journal.json");
                if (text(r, "state") != "complete" || text(request, "kind") != "appimage" ||
                    text(j, "old_hash").empty())
                    continue;
                const auto target = native(from_utf8(text(request, "target"))),
                           backup = install / (".dkr-update-backup-" + n.substr(8));
                if (target.parent_path() != install || text(j, "backup").empty() || text(j, "target").empty() ||
                    native(from_utf8(text(j, "backup"))) != backup || native(from_utf8(text(j, "target"))) != target ||
                    !present(backup) || !checked(backup, install) || !hex(text(j, "old_hash")))
                    continue;
                if (digest(backup) == text(j, "old_hash"))
                    images.emplace_back(fs::last_write_time(h / "result.json"), backup);
            }
            std::sort(images.begin(), images.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (std::size_t i = 1; i < images.size(); ++i)
                erase(images[i].second, install);
        }
        // Recovery backups belong to this actual install, not an arbitrary
        // path stored in a user-editable handoff. Keep the newest completed one.
        const auto backups = install / ".dkr-update-backups";
        if (!present(backups) || !checked(backups, install))
            return;
        std::vector<fs::path> completed;
        for (const auto& e : fs::directory_iterator(backups)) {
            const auto n = utf8(e.path().filename());
            ordinary(e.path());
            if (n.empty() || !std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; }))
                continue;
            const auto j = document(e.path() / "journal.json");
            if (text(j, "state") == "committed" || text(j, "state") == "recovered")
                completed.push_back(e.path());
        }
        std::sort(completed.begin(), completed.end(),
                  [](const auto& a, const auto& b) { return fs::last_write_time(a) > fs::last_write_time(b); });
        if (success && !unresolved)
            for (std::size_t i = 1; i < completed.size(); ++i) {
                const auto journal = document(completed[i] / "journal.json");
                bool verified = journal.contains("files") && journal["files"].is_array() && !journal["files"].empty();
                if (verified)
                    for (const auto& file : journal["files"]) {
                        if (!file.value("old", false))
                            continue;
                        const auto p = member(completed[i] / "backup", text(file, "path"));
                        const auto hash = text(file, "old_hash");
                        if (!hex(hash) || !present(p) || !checked(p, install) || digest(p) != hash) {
                            verified = false;
                            break;
                        }
                    }
                if (verified)
                    erase(completed[i], backups);
            }
    }
    void inventory() {
        for (const auto& [name, dirs] : std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"Installed mods and texture packs", {"mods", "custom-tracks", "texture-packs"}},
                 {"Library downloads and artwork",
                  {"mod-store/downloads", "mod-store/images", "mod-store/verification"}},
                 {"Mod recovery history", {"mod-store/transactions"}},
                 {"Online cache", {"online-mods"}},
                 {"Updates", {"updates"}},
                 {"Files awaiting cleanup", {"storage/retired"}},
                 {"Saves and recovery (kept)",
                  {"saves", "save-backups", "save-migration-v1", "save-recovery", "save-transactions"}},
                 {"Logs and crash reports", {"logs", "crash-dumps", "crash-reports"}},
                 {"Recordings and exports (kept)", {"netplay", "texture-dumps", "support-reports", "development"}}}) {
            Row row;
            row.name = name;
            for (const auto& dir : dirs) {
                const auto p = root / dir;
                if (!present(p) || !checked(p, root))
                    continue;
                const auto s = size(p);
                row.bytes += s.bytes;
                row.files += s.files;
            }
            result.rows.push_back(std::move(row));
        }
    }
};
} // namespace
fs::path cached_package(const fs::path& folder, const std::string& hash, std::uint64_t bytes) {
    Activity activity;
    try {
        const auto dir = native(folder);
        if (!hex(hash) || utf8(dir.filename()) != hash || dir.parent_path().filename() != "downloads" ||
            dir.parent_path().parent_path().filename() != "mod-store" || !bytes || bytes > 2ULL * 1024 * MiB)
            return {};
        Engine e{dir.parent_path().parent_path().parent_path(), {}, Options{}, {}};
        const auto entries = e.children(dir);
        if (entries.size() > 64)
            return {};
        for (const auto& p : entries) {
            const auto ext = utf8(p.extension());
            if ((ext == ".zip" || ext == ".rtz" || ext == ".dkrmap" || ext == ".xdelta" || ext == ".nrm" ||
                 ext == ".dkrcode" || ext == ".7z" || ext == ".ips" || ext == ".bps") &&
                fs::is_regular_file(p) && fs::file_size(p) == bytes && e.digest(p) == hash)
                return folder / p.filename();
        }
    } catch (...) {
    }
    return {};
}
View maintain(const fs::path& profile, const Options& options, const fs::path& installation) {
    std::unique_lock activity(activity_mutex, std::try_to_lock);
    if (!activity.owns_lock()) {
        View waiting;
        waiting.deferred = true;
        waiting.status = "Storage cleanup is waiting for the current operation.";
        return waiting;
    }
    Engine e{native(profile), installation.empty() ? fs::path{} : native(installation), options, {}};
    if (options.action == Action::Preview) {
        e.options.dry_run = true;
        e.result.preview = true;
    }
    try {
        ordinary(e.root);
        if (options.action == Action::Scan)
            e.inventory();
        else if (options.action == Action::Images)
            e.images();
        else if (options.action == Action::ClearCaches) {
            e.images();
            e.online();
        } else if (options.action == Action::OldDumps) {
            for (const auto& p : e.children(e.root / "crash-dumps")) {
                const auto n = utf8(p.filename());
                if (n.size() == 31 && n.starts_with("DKR-R-crash-") && n.ends_with(".dmp") && n[20] == '-' &&
                    std::all_of(n.begin() + 12, n.begin() + 20, [](char c) { return c >= '0' && c <= '9'; }) &&
                    std::all_of(n.begin() + 21, n.begin() + 27, [](char c) { return c >= '0' && c <= '9'; }) &&
                    e.old(p, 30))
                    e.erase(p, e.root / "crash-dumps");
            }
        } else {
            if (options.action == Action::Preview)
                e.inventory();
            e.retired();
            // A malformed owner index protects the whole mod area. Other
            // categories can still be maintained without interpreting it.
            try {
                e.receipts();
                e.store();
                e.downloads();
                e.immutable_libraries();
            } catch (const std::exception&) {
                e.result.status = "Some mod recovery files were kept because their receipts need checking.";
            }
            e.images();
            e.online();
            e.abandoned();
            e.updates();
        }
        if (e.result.status == "Storage has not been measured yet.")
            e.result.status = e.result.preview ? "Safe cleanup preview ready."
                              : options.action == Action::Scan
                                  ? "Storage measured. Saves, installed content and user exports are protected."
                                  : "Storage cleanup finished. Saves and installed content were kept.";
    } catch (const std::exception& error) {
        e.result.status = error.what();
        e.result.deferred = options.stop.stop_requested() || publishers_waiting.load() != 0;
    }
    return e.result;
}
} // namespace dkr::storage
