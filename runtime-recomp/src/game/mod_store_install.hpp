#pragma once
#include "mod_store.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace dkr::store {
inline std::filesystem::path store_io_path(const std::filesystem::path& path) {
#ifdef _WIN32
    auto preferred = std::filesystem::absolute(path).lexically_normal();
    preferred.make_preferred();
    const auto absolute = preferred.native();
    if (absolute.starts_with(L"\\\\?\\"))
        return preferred;
    if (absolute.starts_with(L"\\\\"))
        return std::filesystem::path(L"\\\\?\\UNC\\" + absolute.substr(2));
    return std::filesystem::path(L"\\\\?\\" + absolute);
#else
    return path;
#endif
}
struct InstallView {
    Json records = Json::array(), updates = Json::array(), duplicates = Json::array(), unlinked = Json::array();
    Json proposal = Json::object();
    std::string stage, error, result, completed_key;
    std::uint64_t generation = 0, completion = 0, updates_revision = 0;
    float progress = 0;
    bool busy = false, choosing = false, succeeded = false, ready = false;
};
// All filesystem/decoder work belongs to this serial background service.
// UI supplies immutable native-library inventory and an explicit safe-to-write
// lease. No proposal can mutate game content before its duplicate decision.
class InstallManager {
  public:
    InstallManager();
    ~InstallManager();
    void configure(std::filesystem::path config, std::string version, std::filesystem::path worker);
    std::shared_ptr<const InstallView> view() const;
    void refresh(Json items, Json inventory);
    bool propose(Download job, std::vector<std::filesystem::path> roms);
    void decide(std::string mode, std::string instance = {});
    void policy(std::string instance, bool follow, std::string skip = {});
    void forget(std::string instance, bool remove_files = false);
    void link(std::string local_key, std::string item);
    bool activate(std::string instance, bool previous, std::vector<std::filesystem::path> roms);
    void safe_to_write(bool);
    void retry_recovery();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
InstallManager& installations();
Json current_release(const Json& item);
// Read-only linked-cache input is copied to a fresh ordinary file, then the
// normal strict verifier checks the copy. No global updater guard is relaxed.
bool recover_cached_package(const std::filesystem::path& folder, const std::filesystem::path& target,
                            const std::string& digest, std::uint64_t bytes, std::stop_token);
struct StorePrepared {
    Json outputs = Json::array();
    Json native = Json::object();
};
// Narrow staged first-party adapters. These only prepare private output.
StorePrepared prepare_store_texture(const std::filesystem::path& source, const std::filesystem::path& stage,
                                    const std::string& title, std::stop_token,
                                    const std::function<void(float, std::string)>&);
StorePrepared prepare_store_track(const std::filesystem::path& source, const std::filesystem::path& stage,
                                  const std::string& title, std::stop_token);
void publish_store_texture(const Json& output, const Json& previous);
} // namespace dkr::store
