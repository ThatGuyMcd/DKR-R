#include "camera_runtime.hpp"
#include <cstdio>
#include <mutex>

namespace dkr::runtime::camera {
namespace {
AtomicSnapshot<const Activation> current;
Actions input;
std::array<std::atomic<std::chrono::steady_clock::duration::rep>, 4> owned{};
std::mutex error_mutex;
std::string error;
} // namespace
std::shared_ptr<const Activation> activation() {
    return current.load();
}
Actions& actions() {
    return input;
}
std::string diagnostic() {
    std::scoped_lock lock(error_mutex);
    return error;
}
void report(const std::string& message) {
    std::scoped_lock lock(error_mutex);
    if (error != message && !message.empty())
        std::fprintf(stderr, "[camera++][fallback] %s\n", message.c_str());
    error = message;
}
void begin(const std::filesystem::path& config, const std::string& version, rom::Revision revision, bool online,
           bool development_tools) noexcept {
    reset();
    try {
        mods::code::Library library(config / "mods" / "custom-magic-codes", version);
        auto set = library.freeze(revision == rom::Revision::UsV77 ? "us.v77" : "us.v80", false,
                                  Program::policy().modules, {}, true);
        if (set.ordered.empty()) {
            if (!development_tools)
                return;
            auto next = std::make_shared<Activation>();
            next->revision = revision;
            next->online = online;
            next->settings = std::make_shared<SettingsChannel>();
            std::array<Json, 4> defaults;
            for (auto& value : defaults)
                value = Json::object();
            next->settings->publish(defaults);
            current.store(std::move(next));
            return;
        }
        if (set.ordered.size() != 1)
            throw mods::Error("Enable only one camera mod.");
        auto next = std::make_shared<Activation>();
        next->frozen = std::move(set);
        next->revision = revision;
        next->online = online;
        next->settings = std::make_shared<SettingsChannel>();
        const auto& package = next->frozen.ordered[0];
        next->editor = std::make_shared<SettingsEditor>(package, next->frozen.settings.at(package->id),
                                                        next->frozen.data.at(package->id), *next->settings);
        // Link/validate without executing. Rejection only disables the camera,
        // never the shared race or its save route.
        Program checked(package, next->frozen.settings.at(package->id),
                        revision == rom::Revision::UsV77 ? "us.v77" : "us.v80");
        current.store(std::move(next));
        std::fprintf(stderr, "[camera++][ready] provider=%s domain=local-presentation online=%u\n", package->id.c_str(),
                     unsigned(online));
    } catch (const std::exception& e) {
        report(e.what());
    }
}
void reset() noexcept {
    current.store(nullptr);
    editor_open.store(false);
    input.reset();
    for (auto& port : owned)
        port.store(0);
    std::scoped_lock lock(error_mutex);
    error.clear();
}
void presented(unsigned profile, bool active) {
    if (profile < 4)
        owned[profile].store(
            active ? (std::chrono::steady_clock::now() + std::chrono::milliseconds(300)).time_since_epoch().count()
                   : 0);
}
bool owns_controls(unsigned profile) {
    return profile < 4 && !editor_open.load() &&
           owned[profile].load() > std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace dkr::runtime::camera
