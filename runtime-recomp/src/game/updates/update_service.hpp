#pragma once
#include "update_http.hpp"
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace dkr::updates {
enum class State { Idle, Checking, Current, Available, Downloading, Ready, Preparing, Failed };
struct Preferences {
    bool automatic = true, beta = false;
    std::string skipped;
};
struct Snapshot {
    State state = State::Idle;
    Preferences preferences;
    std::string message = "Not checked yet.", version, notes, last_check, installation_result;
    std::uint64_t received{}, total{};
    bool manual_only = false, busy = false;
};
struct Configuration {
    std::filesystem::path data, application, helper;
    std::string version, key, platform;
    std::vector<std::string> arguments;
};
class Service {
    Configuration config;
    Transport transport;
    mutable std::mutex mutex;
    Snapshot view;
    std::jthread worker;
    std::optional<Manifest> release;
    std::filesystem::path package;
    bool startup_checked = false;
    std::uint64_t retry_after{};
    void publish(State, std::string);
    void save_preferences();
    void read_installation_result();
    void run(std::function<void(std::stop_token)>);
    HttpResponse request(HttpRequest, std::stop_token, const Progress& = {});
    void discover(std::stop_token);

  public:
    explicit Service(Configuration, Transport = https_get);
    ~Service();
    Snapshot snapshot() const;
    void startup();
    void check();
    void download();
    void cancel();
    void preferences(Preferences);
    void skip();
    // This only stages and starts a helper; it never shuts the caller down.
    // The launcher exits normally after Snapshot state Preparing becomes ready.
    bool install(std::string& error);
    bool install_handoff_ready() const;
    void launcher_ready();
    const Configuration& configuration() const {
        return config;
    }
};
void configure(const std::filesystem::path& data, const std::filesystem::path& application,
               const std::filesystem::path& helper, std::string version);
Service* service();
void remember_arguments(int argc, char** argv);
} // namespace dkr::updates
