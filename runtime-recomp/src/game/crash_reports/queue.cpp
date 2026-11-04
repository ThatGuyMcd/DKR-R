#include "reporting.hpp"
#include "updates/update_core.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <stdexcept>
#include <json/json.hpp>
#include <miniz/miniz.h>

namespace dkr::crash_reports {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t LogBytes = 1024 * 1024;
constexpr std::size_t DumpBytes = 32 * 1024 * 1024;
bool id_ok(std::string_view id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool regular(const fs::path& path) {
    std::error_code ec;
    if (fs::symlink_status(path, ec).type() != fs::file_type::regular || ec) return false;
    // Canonical equality also rejects a junction/symlink in a parent directory.
    return fs::canonical(path, ec) == fs::absolute(path).lexically_normal() && !ec;
}
void ensure_directory(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) fs::create_directory(path, ec);
    dkr::updates::require_private_tree(path);
    if (fs::canonical(path) != fs::absolute(path).lexically_normal())
        throw std::runtime_error("Diagnostic directory contains a link.");
}
std::string read(const fs::path& path, std::size_t limit, bool ends = false) {
    if (!regular(path)) return {};
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size < 0 || (!ends && std::uint64_t(size) > limit)) return {};
    std::string data(std::min<std::size_t>(std::size_t(size), limit), '\0');
    input.seekg(0);
    if (std::size_t(size) <= limit) input.read(data.data(), data.size());
    else {
        const auto first = limit / 4;
        input.read(data.data(), first);
        input.seekg(size - std::streamoff(limit - first));
        input.read(data.data() + first, limit - first);
        data.insert(first, "\n[diagnostics: middle omitted]\n");
    }
    return input ? data : std::string{};
}
Json document(const fs::path& path) {
    if (fs::exists(fs::symlink_status(path)) && !regular(path))
        throw std::runtime_error("Diagnostic metadata is not an ordinary file.");
    const auto data = read(path, 256 * 1024);
    if (data.empty()) {
        if (fs::exists(path)) throw std::runtime_error("Diagnostic metadata is empty or too large.");
        return Json::object();
    }
    auto result = Json::parse(data, nullptr, false);
    if (!result.is_object()) throw std::runtime_error("Diagnostic preferences or queue need inspection.");
    return result;
}
Json index(const fs::path& root) {
    auto result = document(root / "index.json");
    if (!result.contains("reports")) result["reports"] = Json::array();
    if (!result.contains("seen")) result["seen"] = Json::array();
    if (!result["reports"].is_array() || result["reports"].size() > MaxQueue ||
        !result["seen"].is_array() || result["seen"].size() > 256)
        throw std::runtime_error("Diagnostic queue needs inspection.");
    for (const auto& report : result["reports"])
        if (!report.is_object() || !id_ok(report.value("id", std::string{})))
            throw std::runtime_error("Invalid diagnostic entry.");
    return result;
}
void persist(const fs::path& root, const Json& doc) {
    dkr::updates::durable_write(root / "index.json", doc.dump());
}
void erase(const fs::path& path) {
    if (regular(path)) fs::remove(path); // never follow links or delete directories
}
std::int64_t modified(const fs::path& path) {
#ifdef _MSC_VER
    const auto stamp = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(path));
#else
    const auto stamp = std::chrono::file_clock::to_sys(fs::last_write_time(path));
#endif
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        stamp.time_since_epoch()).count();
}
std::string archive(const std::map<std::string, std::string>& files) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) throw std::runtime_error("Could not prepare diagnostics.");
    bool okay = true;
    for (const auto& [name, value] : files)
        okay = okay && mz_zip_writer_add_mem(&zip, name.c_str(), value.data(), value.size(), MZ_BEST_SPEED);
    void* data = nullptr; size_t size = 0;
    okay = okay && mz_zip_writer_finalize_heap_archive(&zip, &data, &size);
    mz_zip_writer_end(&zip);
    std::string result;
    if (okay && size <= MaxUpload) result.assign(static_cast<const char*>(data), size);
    mz_free(data);
    if (!okay) throw std::runtime_error("Could not complete diagnostics.");
    return result;
}
std::string without_dump(const std::string& bytes) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        throw std::runtime_error("Diagnostic archive needs inspection.");
    std::map<std::string, std::string> files;
    for (const auto* name : {"runtime.log", "runtime-previous.log", "manifest.json"}) {
        const int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
        mz_zip_archive_file_stat stat{};
        if (idx < 0 || !mz_zip_reader_file_stat(&zip, idx, &stat) || stat.m_uncomp_size > 2 * LogBytes) continue;
        size_t size = 0; void* data = mz_zip_reader_extract_to_heap(&zip, idx, &size, 0);
        if (data) { files[name] = std::string(static_cast<char*>(data), size); mz_free(data); }
    }
    mz_zip_reader_end(&zip);
    auto manifest = Json::parse(files["manifest.json"]);
    manifest["rawDump"] = false; manifest["dumpStatus"] = "Consent withdrawn before delivery";
    files["manifest.json"] = manifest.dump(2);
    return archive(files);
}
} // namespace

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string redact(std::string text, std::span<const std::string> identities) {
    // Best effort only: free-form diagnostic text cannot promise anonymity.
    // Drop entire identity/chat/authentication lines, then scrub common paths,
    // endpoints and identifiers from the remaining technical diagnostics.
    static const std::regex private_line(
        R"((token|ticket|password|authorization|cookie|credential|secret|webhook|nickname|username|friend|chat|invite|account|profile|lobby.?code|room.?code|session.?id|bearer))",
        std::regex::icase);
    std::string clean;
    for (std::size_t at = 0; at < text.size();) {
        const auto end = text.find('\n', at);
        auto line = text.substr(at, end == std::string::npos ? end : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.size() > 16384 || std::regex_search(line, private_line)) { clean += "[private diagnostic line removed]\n"; continue; }
        for (auto& c : line) if (static_cast<unsigned char>(c) < 32 && c != '\t') c = ' ';
        clean += line + '\n';
    }
    for (const auto* pattern : {
        R"((https?|wss?)://[^\s<>"']+)",
        R"(([A-Za-z]:[\\/]|\\\\)[^\r\n]*)",
        R"(/(home|Users|mnt|tmp|run|var|opt|media|Volumes)/[^\r\n]*)",
        R"([A-Za-z0-9_.+%-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})",
        R"(\b([0-9]{1,3}\.){3}[0-9]{1,3}(:[0-9]+)?\b)",
        R"(\b([0-9a-f]{0,4}:){2,}[0-9a-f:]+(%[A-Za-z0-9]+)?\b)",
        R"([A-Za-z0-9_-]{64,})"})
        clean = std::regex_replace(clean, std::regex(pattern, std::regex::icase), "[redacted]");
    for (const auto& identity : identities) {
        if (identity.size() < 3) continue;
        std::string escaped;
        for (char c : identity) { if (std::string_view(R"(\.^$|()[]{}*+?)").find(c) != std::string_view::npos) escaped += '\\'; escaped += c; }
        clean = std::regex_replace(clean, std::regex(escaped, std::regex::icase), "[identity]");
    }
    return clean;
}
Consent read_consent(const fs::path& root) {
    const auto doc = document(root / "consent.json");
    Consent result{doc.value("choice", -1), doc.value("dumps", false), doc.value("since", std::int64_t{0})};
    if (result.choice < -1 || result.choice > 1 || (result.choice == 1 && result.since <= 0))
        throw std::runtime_error("Diagnostic consent needs inspection.");
    if (result.choice != 1) result.dumps = false;
    return result;
}
void write_consent(const fs::path& root, Consent consent) {
    if (consent.choice < 0 || consent.choice > 1 || (consent.choice == 1 && consent.since <= 0))
        throw std::runtime_error("Invalid diagnostic consent.");
    ensure_directory(root);
    dkr::updates::durable_write(root / "consent.json", Json{{"schema", 1}, {"choice", consent.choice},
        {"dumps", consent.choice == 1 && consent.dumps}, {"since", consent.since}}.dump());
}
Queue::Queue(fs::path profile, std::string version, std::vector<std::string> identities)
    : profile_(fs::canonical(profile)), root_(profile_ / "crash-reports" / "queue"),
      version_(std::move(version)), identities_(std::move(identities)) {
    ensure_directory(profile_ / "crash-reports"); ensure_directory(root_);
}
void Queue::reconcile(Consent consent, std::int64_t now) {
    auto doc = index(root_); auto& reports = doc["reports"];
    const auto before = doc;
    for (auto it = reports.begin(); it != reports.end();) {
        const auto path = root_ / (it->at("id").get<std::string>() + ".zip");
        if (consent.choice != 1 || now - it->value("created", now) > RetentionMs) {
            erase(path); it = reports.erase(it);
        } else {
            if (!consent.dumps && it->value("raw", false)) {
                const auto bytes = without_dump(read(path, MaxUpload));
                if (bytes.empty()) throw std::runtime_error("Could not remove queued dump.");
                dkr::updates::durable_write(path, bytes); (*it)["raw"] = false;
            }
            ++it;
        }
    }
    if (doc != before) persist(root_, doc);
    // Recover an interrupted enqueue/ack: remove only strictly owned orphan
    // archives. Never recurse or touch original logs, dumps, saves or mods.
    for (const auto& entry : fs::directory_iterator(root_)) {
        const auto name = entry.path().filename().string();
        if (!name.ends_with(".zip") || !id_ok(name.substr(0, name.size() - 4))) continue;
        const auto id = name.substr(0, 32);
        if (std::none_of(reports.begin(), reports.end(), [&](const auto& r) { return r["id"] == id; })) erase(entry.path());
    }
}
void Queue::discover(Consent consent, std::int64_t now) {
    if (consent.choice != 1) return;
    auto doc = index(root_);
    if (doc["reports"].size() >= MaxQueue) return;
    auto log = profile_ / "logs" / "runtime-previous.log";
    if (!regular(log)) log = profile_ / "logs" / "previous-runtime.log";
    if (!regular(log) || modified(log) <= consent.since) return;
    const auto crashed = read(log, LogBytes, true);
    if (crashed.find("[boot][crash]") == std::string::npos) return;
    const auto seed = crashed + std::to_string(modified(log));
    const auto id = dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size())).substr(0, 32);
    if (std::find(doc["seen"].begin(), doc["seen"].end(), id) != doc["seen"].end()) return;
    std::map<std::string, std::string> files{
        {"runtime-previous.log", redact(crashed, identities_)},
        {"runtime.log", redact(read(profile_ / "logs" / "runtime.log", LogBytes, true), identities_)}};
    Json manifest{{"schema", 1}, {"reportId", id}, {"version", version_},
        {"rawDump", false}, {"dumpStatus", "Not requested or not available"},
        {"privacy", "Redaction is best effort. Raw dumps may contain private process memory."}};
    // Only the exact dump referenced by this crashed session is eligible.
    // OS-wide cores and unrelated historical dumps are deliberately excluded.
    std::smatch match;
    if (consent.dumps && std::regex_search(crashed, match, std::regex(R"(DKR-R-crash-[0-9]{8}-[0-9]{6}\.dmp)"))) {
        const auto dump = read(profile_ / "crash-dumps" / match.str(), DumpBytes);
        if (!dump.empty()) { files["crash.dmp"] = dump; manifest["rawDump"] = true; manifest["dumpStatus"] = "Included with separate consent"; }
        else manifest["dumpStatus"] = "Missing, linked, unreadable or larger than 32 MiB; original retained locally";
    }
    files["manifest.json"] = manifest.dump(2);
    auto zip = archive(files);
    if (zip.empty() && files.erase("crash.dmp")) {
        manifest["rawDump"] = false; manifest["dumpStatus"] = "Exceeds 8 MiB compressed upload limit; original retained locally";
        files["manifest.json"] = manifest.dump(2); zip = archive(files);
    }
    if (zip.empty()) throw std::runtime_error("Diagnostic archive exceeds upload limit.");
    dkr::updates::durable_write(root_ / (id + ".zip"), zip);
    doc["reports"].push_back({{"id", id}, {"created", now}, {"next", now}, {"attempts", 0},
        {"raw", manifest["rawDump"]}, {"blocked", false}});
    if (doc["seen"].size() == 256) doc["seen"].erase(doc["seen"].begin());
    doc["seen"].push_back(id); persist(root_, doc);
}
Pending Queue::due(std::int64_t now) const {
    const auto doc = index(root_);
    for (const auto& report : doc["reports"]) {
        if (report.value("blocked", false) || report.value("next", now) > now) continue;
        const auto id = report.at("id").get<std::string>(); const auto bytes = read(root_ / (id + ".zip"), MaxUpload);
        if (!bytes.empty()) return {id, {bytes.begin(), bytes.end()}};
    }
    return {};
}
void Queue::delivered(const std::string& id, Delivery result, std::int64_t now) {
    auto doc = index(root_); auto& reports = doc["reports"];
    for (auto it = reports.begin(); it != reports.end(); ++it) {
        if (it->at("id") != id) continue;
        if (result.confirmed && result.status >= 200 && result.status < 300) {
            reports.erase(it); persist(root_, doc); erase(root_ / (id + ".zip")); return;
        }
        const unsigned attempts = std::min(30U, it->value("attempts", 0U) + 1);
        (*it)["attempts"] = attempts;
        (*it)["blocked"] = result.status >= 400 && result.status < 500 && result.status != 408 && result.status != 429;
        const auto delay = std::max(std::min(86400U, result.retry_seconds), std::min(3600U, 30U << std::min(7U, attempts - 1)));
        (*it)["next"] = now + std::int64_t(delay) * 1000;
        persist(root_, doc); return;
    }
}
std::size_t Queue::count() const { return index(root_)["reports"].size(); }
bool Queue::blocked() const {
    const auto doc = index(root_);
    return std::any_of(doc["reports"].begin(), doc["reports"].end(), [](const auto& r) { return r.value("blocked", false); });
}
} // namespace dkr::crash_reports
