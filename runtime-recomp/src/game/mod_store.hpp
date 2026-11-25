#pragma once
#include "cloud_saves/sync.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <json/json.hpp>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace dkr::cloud_saves {
struct ProgressBatch;
}

namespace dkr::store {
using Json = nlohmann::json;
using PublishedReplayTime = std::pair<std::string, std::int64_t>;
struct Download {
    std::string key, item, release, title, category, state = "Queued", error, digest, version, intent, instance;
    std::filesystem::path file;
    std::uint64_t received = 0, total = 0;
};
struct Image {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
struct LeaderboardTrackQuery {
    std::string id, vehicle, racer;
    bool custom = false, best_lap = false;
    unsigned offset = 0;
    bool full_game = false;
};
struct Snapshot {
    std::shared_ptr<const Json> items = std::make_shared<const Json>(Json::array());
    std::shared_ptr<const Json> speedrun_catalogue, speedrun_board;
    std::string speedrun_error;
    bool speedrun_loading = false;
    Json user = nullptr, personal = Json::object(), staff = Json::object(), admin = Json::object();
    Json reviews = Json::object(), uploads = Json::array(), assets = Json::array(), upload = Json::object();
    std::vector<Download> downloads;
    std::string cloud_saves_account, cloud_saves_status;
    cloud_saves::SyncStatus cloud_sync = cloud_saves::SyncStatus::Idle;
    std::string status, error, last_action;
    Json last_result;
    std::uint64_t generation = 0, mutation = 0;
    bool loading = false, signing_in = false, cached = false, remember = false, uploading = false, writing = false;
    int layout = 0;
};
// No network/disk/decoder activity in view(). All mutations are background
// jobs, with immutable UI snapshots and account-generation fencing.
class Service {
  public:
    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void configure(const std::filesystem::path&, std::string version);
    std::shared_ptr<const Snapshot> view() const;
    void refresh();
    void load_leaderboards(std::string cursor = {}, std::optional<LeaderboardTrackQuery> track = {});
    Json overall_rankings(const std::string& player = {}, unsigned offset = 0);
    Json ranked_attempt(std::string board);
    Json adventure_record(const std::string& category);
    Json submit_ranked_replay(const std::filesystem::path&, bool link_local = false);
    Json ranked_runs(bool review = false);
    std::vector<PublishedReplayTime> published_replay_times();
    void review_ranked_run(const std::string& id, bool publish, std::string reason);
    void save_ranked_replay(const std::string& id, const std::filesystem::path& destination, bool own_run);
    void gameplay(bool active);
    void set_cloud_saves(const std::string& account, bool enabled);
    void sync_cloud_saves(const std::string& account, cloud_saves::SyncScope scope = cloud_saves::SyncScope::All);
    bool refresh_names();
    void layout(int);
    void remember(bool);
    void reviews(std::string item);
    void action(std::string name, Json input);
    void login();
    void cancel_login();
    void logout();
    void download(const Json& item, const Json& release, std::string intent = {}, std::string instance = {});
    void verify_release(const Json& item, const Json& release, bool download_after = false);
    void pause_download(std::string key);
    void resume_download(std::string key);
    void clear_completed_history();
    void clear_messages();
    bool begin_install(std::string key);
    void installed(std::string key, bool success, std::string message);
    void request_image(std::string url);
    std::shared_ptr<const Image> image(const std::string& url) const;
    void load_staff_files();
    void upload(std::filesystem::path source, std::string kind, std::string category, std::string resume_id = {});
    void pause_upload();
    void cancel_upload(std::string id);
    std::string take_browser_url();

  private:
    void sync_cloud_saves(const std::string& account, cloud_saves::SyncScope scope,
                          const cloud_saves::ProgressBatch* progress, std::stop_token stop);
    void publish_progress(std::stop_token stop);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Service& service();
std::string text(const Json&, const char* key, std::size_t max = 4096);
bool staff(const Json&);
bool admin(const Json&);
bool trusted_url(const std::string&, bool api = false);
// Presentation only: never rewrite installed artifacts or their online proofs.
std::string package_filename(const std::string& title, const std::string& published);
std::string download_key(const std::string& item, const std::string& release, const std::string& instance = {});
bool generic_package_name(const std::string&);
std::uint64_t library_names_generation();
std::string library_display_name(const std::string& digest, const std::string& category, const std::string& fallback);
std::string texture_display_name(const std::filesystem::path&, const std::string& fallback);
void refresh_library_names(const std::filesystem::path& config, const Json& items, std::stop_token);
} // namespace dkr::store
