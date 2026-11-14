#include "mod_store/community_policy.hpp"
#include "mod_store/overlay_completion.hpp"
#include <deque>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stop_token>
#include <thread>
#include <vector>

using Json = nlohmann::json;
namespace community = dkr::store::community;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct Error : std::runtime_error {
    unsigned status;
    Error(std::string s, unsigned code = 0) : std::runtime_error(s), status(code) {}
};
struct Request {
    std::string url, method = "GET", body, token, type = "application/json", etag;
    std::uint64_t offset = 0, limit = 4096;
    unsigned timeout = 12000;
};
constexpr const char* Api = "https://api.test";
std::string text(const Json& value, const char* key, std::size_t max = 4096) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_string())
        return {};
    return value[key].get<std::string>().substr(0, max);
}
std::string encode(const std::string& value) {
    return value;
}
struct Snapshot {
    std::shared_ptr<const Json> items = std::make_shared<const Json>(Json::array());
    Json reviews = Json::object(), last_result;
    bool writing = false, loading = false;
    std::string error, status, last_action;
    unsigned mutation = 0;
};
std::vector<Request> requests;
std::map<std::filesystem::path, Json> disk;
std::function<Json(const Request&)> response;
Json document(const Request& r, std::stop_token) {
    requests.push_back(r);
    return response(r);
}
Json read_json(const std::filesystem::path& p) {
    return disk.contains(p) ? disk[p] : Json{};
}
void write_json(const std::filesystem::path& p, const Json& j) {
    disk[p] = j;
}
unsigned http_once(const Request& r, std::stop_token, std::nullptr_t) {
    requests.push_back(r);
    return 302;
}
struct Owner {
    std::mutex mutex;
    std::condition_variable_any cv;
    bool gameplay = false;
    Snapshot state;
    unsigned account = 0;
    std::string token;
    std::deque<std::function<void(std::stop_token)>> jobs;
    bool full = false;
    void publish() {
        community_fence();
    }
    void update(const std::function<void(Snapshot&)>& fn) {
        std::lock_guard lock(mutex);
        fn(state);
        publish();
    }
    bool queue(std::function<void(std::stop_token)> fn) {
        if (full)
            return false;
        jobs.push_back(std::move(fn));
        return true;
    }
    void expire(const std::string& key) {
        if (key == token) {
            ++account;
            token.clear();
            publish();
        }
    }
    void run() {
        while (!jobs.empty()) {
            auto job = std::move(jobs.front());
            jobs.pop_front();
            job({});
        }
    }
#include "mod_store/community_service.inl"
};
Json comment(const std::string& item) {
    return {{"id", "comment_1"},
            {"itemId", item},
            {"text", "a comment"},
            {"revision", 1U},
            {"isOwn", false},
            {"displayName", "Player"},
            {"createdAt", "2026-10-30T00:00:00.000Z"}};
}
Json page(const std::string& item) {
    return {{"comments", Json::array({comment(item)})}, {"total", 1U}, {"nextCursor", nullptr}};
}
void defaults() {
    requests.clear();
    response = [](const Request& r) {
        if (r.method == "POST")
            return Json{{"saved", true}};
        const auto begin = r.url.find("/mods/") + 6, end = r.url.find("/comments", begin);
        return page(r.url.substr(begin, end - begin));
    };
}
Json input(const char* item = "mod_1") {
    return {{"itemId", item}, {"operationId", std::string(32, 'a')}, {"text", "Hello"}};
}
} // namespace
int main() try {
    check(community::identifier("mod_1") && !community::identifier("../bad") &&
              !community::identifier(std::string(97, 'a')),
          "Identifiers must match the shared API");
    check(community::operation(community::operation_id()), "Secure attempt format");
    check(community::count({{"downloadCount", 1234567U}}, "downloadCount") == "1,234,567", "Counter formatting");
    check(community::count({{"downloadCount", nullptr}}, "downloadCount") == "unavailable",
          "Unavailable counters are never zero");
    auto valid = community::page("mod_1", page("mod_1"));
    check(valid["total"] == 1 && valid["nextCursor"] == "", "Shared comment DTO");
    for (auto invalid : {Json{{"comments", Json::array()}, {"total", -1}}, page("mod_other"),
                         Json{{"comments", Json::array({{"id", "bad"}})}, {"total", 1U}}}) {
        bool rejected = false;
        try {
            community::page("mod_1", invalid);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid or cross-mod comment response accepted");
    }
    defaults();
    Owner anonymous;
    anonymous.community_action("community.load", {{"itemId", "mod_1"}});
    anonymous.run();
    check(requests.size() == 1 && requests[0].token.empty(), "Anonymous comments must not need sign-in");
    check(anonymous.state.reviews[community::SnapshotKey]["comments"].size() == 1,
          "Actual production reader did not publish comments");
    defaults();
    Owner switched;
    switched.community_action("community.load", {{"itemId", "mod_old"}});
    switched.community_action("community.load", {{"itemId", "mod_new"}});
    switched.run();
    check(requests.size() == 1 && switched.state.reviews[community::SnapshotKey]["itemId"] == "mod_new",
          "Late selection overwrote the current discussion");
    defaults();
    Owner account;
    account.token = "old-credential";
    account.community_action("community.load", {{"itemId", "mod_1"}});
    ++account.account;
    account.token = "other-credential";
    account.publish();
    account.run();
    check(requests.empty() && !account.state.reviews.contains(community::SnapshotKey),
          "Old-account discussion or ownership leaked");
    defaults();
    Owner full;
    full.full = true;
    full.community_action("community.load", {{"itemId", "mod_1"}});
    check(!full.state.reviews[community::SnapshotKey]["loading"].get<bool>(),
          "Full queue leaves comments loading forever");
    defaults();
    Owner writer;
    writer.token = "credential";
    writer.community_action("community.create", input());
    writer.community_action("community.load", {{"itemId", "mod_2"}});
    writer.run();
    check(requests.size() == 2 && requests[0].method == "POST" &&
              requests[0].url == std::string(Api) + "/v1/mods/mod_1/comments",
          "Comment POST used another route");
    check(!writer.state.writing && writer.state.mutation == 1, "Committed comment not acknowledged");
    check(Json::parse(requests[0].body)["operationId"] == std::string(32, 'a'), "Retry operation ID changed");
    defaults();
    Owner refresh_failure;
    refresh_failure.token = "credential";
    response = [](const Request& r) -> Json {
        if (r.method == "POST")
            return {{"saved", true}};
        throw Error("Read failed", 503);
    };
    refresh_failure.community_action("community.create", input());
    refresh_failure.run();
    check(requests.size() == 2 && refresh_failure.state.mutation == 1 && !refresh_failure.state.writing,
          "Read failure replays or loses a committed mutation");
    check(text(refresh_failure.state.reviews[community::SnapshotKey], "error").find("saved") != std::string::npos,
          "Committed-but-not-refreshed status is misleading");
    defaults();
    Owner edits;
    edits.token = "credential";
    auto change = input();
    change["commentId"] = "comment_1";
    change["expectedRevision"] = 3U;
    edits.community_action("community.edit", change);
    edits.run();
    check(Json::parse(requests[0].body)["action"] == "edit" && Json::parse(requests[0].body)["expectedRevision"] == 3,
          "Revision guard lost on edit");
    defaults();
    edits.community_action("community.delete", change);
    edits.run();
    check(Json::parse(requests[0].body)["action"] == "delete", "Delete operation not revision-checked");
    defaults();
    edits.community_action("community.report", change);
    edits.run();
    check(requests[0].url.ends_with("/comments/comment_1/report"), "Report must use shared moderation route");
    defaults();
    Owner unsigned_writer;
    unsigned_writer.community_action("community.create", input());
    unsigned_writer.run();
    check(requests.empty() && !unsigned_writer.state.writing, "Anonymous comment mutation allowed");
    defaults();
    Owner expired;
    expired.token = "expired";
    response = [](const Request&) -> Json { throw Error("Expired", 401); };
    expired.community_action("community.create", input());
    expired.run();
    check(expired.token.empty() && !expired.state.reviews.contains(community::SnapshotKey),
          "Expired account leaves ownership data");
    defaults();
    Owner tracking;
    const auto one = tracking.community_download("release_1", "marker"),
               two = tracking.community_download("release_1", "marker");
    check(tracking.jobs.empty() && requests.empty(), "Download counting blocks account jobs or file transfer");
    for (const auto& job : tracking.community_downloads)
        tracking.community_send(job, {});
    check(one == two && community::operation(one), "Resuming download double-counts a new attempt");
    check(requests.size() == 2 && requests[0].token.empty() &&
              requests[0].url.find("/v1/downloads/release_1?attempt=") != std::string::npos,
          "Tracking leaked credentials or followed a file redirect");
    defaults();
    Owner bounded;
    for (unsigned i = 0; i < 40; ++i)
        bounded.community_download("release_1", "marker");
    check(bounded.community_downloads.size() == 16 && bounded.jobs.empty(),
          "Statistics queue can exhaust account work");
    bounded.gameplay = true;
    bounded.community_tracking_worker = std::jthread([&](std::stop_token stop) { bounded.community_tracking(stop); });
    bounded.community_tracking_worker.request_stop();
    bounded.cv.notify_all();
    bounded.community_tracking_worker.join();
    check(requests.empty(), "Statistics worker ignored gameplay pause or shutdown");
    dkr::store::overlay::Lifetime life;
    check(!life.can_retire(1, 2), "GPU image retired before completed fence");
    life.submitted = 2;
    life.finish();
    check(life.can_retire(1, 3) && !life.can_retire(3, 3), "GPU frame/current draw lifetime failed");
    life.submitted = 1;
    life.finish();
    check(life.completed == 2, "Repeated older draw regressed GPU completion");
    std::cout << checks << " Mod Store community and GPU lifetime checks passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
