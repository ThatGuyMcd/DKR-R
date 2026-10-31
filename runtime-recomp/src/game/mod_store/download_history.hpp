#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dkr::store::history {
inline constexpr std::uint64_t max_package_bytes = 2ULL * 1024 * 1024 * 1024;

template <class Json> std::string field(const Json& value, const char* name, std::size_t limit) {
    if (!value.is_object())
        return {};
    const auto it = value.find(name);
    if (it == value.end() || !it->is_string())
        return {};
    const auto& text = it->template get_ref<const std::string&>();
    return text.size() <= limit ? text : std::string{};
}

inline bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 160 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

inline bool digest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
                                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

template <class Json> std::uint64_t bytes(const Json& value) {
    if (!value.is_object())
        return 0;
    const auto it = value.find("bytes");
    if (it == value.end() || !it->is_number_unsigned())
        return 0;
    const auto result = it->template get<std::uint64_t>();
    return result <= max_package_bytes ? result : 0;
}

// Only used by the background history loader. This never claims ownership,
// hashes whole packages, changes files or replaces native-library repair checks.
inline std::filesystem::path utf8_path(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

inline bool member_present(const std::filesystem::path& config, const std::string& relative) {
    if (relative.empty() || relative.find_first_of("\\:") != relative.npos)
        return false;
    const auto path = utf8_path(relative);
    if (path.is_absolute() || path.has_root_name())
        return false;
    auto at = config;
    std::error_code error;
    for (const auto& part : path) {
        if (part.empty() || part == "." || part == "..")
            return false;
        at /= part;
        const auto status = std::filesystem::symlink_status(at, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
            return false;
    }
    return true;
}

template <class Job, class Json, class Present>
const Json* completed_receipt(const Job& job, const Json& registry, Present&& present) {
    if (!registry.is_object() || !registry.contains("schema") || registry["schema"] != 1 ||
        !registry.contains("records") || !registry["records"].is_array())
        return nullptr;
    // Explicit replacement/update attempts may be paused even when an older
    // successful install of that release exists. Leave those attempts alone.
    if (!job.intent.empty() || !job.instance.empty())
        return nullptr;
    for (const auto& record : registry["records"]) {
        if (field(record, "item", 160) != job.item || field(record, "release", 160) != job.release ||
            field(record, "category", 32) != job.category || field(record, "state", 80) != "Installed" ||
            (record.contains("stored") && record["stored"] != false))
            continue;
        const auto hash = field(record, "digest", 64);
        if (!digest(hash) || (!job.digest.empty() && job.digest != hash) || !record.contains("members") ||
            !record["members"].is_array() || record["members"].empty())
            continue;
        const bool complete = std::all_of(record["members"].begin(), record["members"].end(), [&](const Json& member) {
            return !field(member, "key", 512).empty() && !field(member, "kind", 40).empty() &&
                   present(field(member, "path", 4096));
        });
        if (complete)
            return &record;
    }
    return nullptr;
}

template <class Job, class Json, class Present>
std::vector<Job> restore(const Json& saved, const Json& registry, Present&& present) {
    std::vector<Job> result;
    if (!saved.is_object() || !saved.contains("jobs") || !saved["jobs"].is_array() || saved["jobs"].size() > 64)
        return result;
    const bool legacy = !saved.contains("schema") || saved["schema"] == 1;
    if (!legacy && saved["schema"] != 2)
        return result;
    for (const auto& row : saved["jobs"]) {
        Job job;
        job.item = field(row, "item", 160);
        job.release = field(row, "release", 160);
        job.title = field(row, "title", 200);
        job.category = field(row, "category", 32);
        job.version = field(row, "version", 80);
        job.intent = field(row, "intent", 32);
        job.instance = field(row, "instance", 512);
        if (!identifier(job.item) || !identifier(job.release))
            continue;
        job.key = job.item + "_" + job.release + (job.instance.empty() ? "" : "_" + job.instance);
        if (std::any_of(result.begin(), result.end(), [&](const Job& old) { return old.key == job.key; }))
            continue;
        const auto hash = field(row, "sha256", 64);
        if (digest(hash))
            job.digest = hash;
        job.total = bytes(row);
        const auto previous = field(row, "state", 80);
        const Json* receipt = nullptr;
        if (legacy && previous == "Paused")
            receipt = completed_receipt(job, registry, present);
        if (previous == "Installed" || receipt) {
            job.state = "Installed";
            if (receipt) {
                job.digest = field(*receipt, "digest", 64);
                if (!job.total)
                    job.total = bytes(*receipt);
            }
            job.received = job.total;
        } else {
            // Resuming always obtains current integrity metadata and verifies
            // the cache. No filename or queued/active state is trusted on boot.
            job.state = previous == "Failed" || previous == "Install failed" ? "Failed" : "Paused";
            job.error = field(row, "error", 2000);
            if (job.error.empty())
                job.error = previous == "Install failed"
                                ? "Installation did not finish. Resume to verify the saved package."
                                : "Resume to check or reuse the saved download.";
        }
        result.push_back(std::move(job));
    }
    return result;
}

template <class Json, class Jobs> Json save(const Jobs& jobs) {
    Json rows = Json::array();
    for (const auto& job : jobs)
        rows.push_back({{"item", job.item},
                        {"release", job.release},
                        {"title", job.title},
                        {"category", job.category},
                        {"version", job.version},
                        {"state", job.state},
                        {"sha256", job.digest},
                        {"bytes", job.total},
                        {"intent", job.intent},
                        {"instance", job.instance},
                        {"error", job.error.size() <= 2000 ? job.error : std::string{}}});
    return {{"schema", 2}, {"jobs", std::move(rows)}};
}
} // namespace dkr::store::history
