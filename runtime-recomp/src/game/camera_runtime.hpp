#pragma once
#include "camera_session.hpp"
#include <atomic>
#include <chrono>

namespace dkr::runtime::camera {
struct Activation {
    mods::code::FrozenSet frozen;
    std::shared_ptr<SettingsChannel> settings;
    std::shared_ptr<SettingsEditor> editor;
    rom::Revision revision = rom::Revision::Unsupported;
    bool online = false;
};
void begin(const std::filesystem::path& config, const std::string& version, rom::Revision, bool online,
           bool development_tools = false) noexcept;
void reset() noexcept;
std::shared_ptr<const Activation> activation();
Actions& actions();
bool owns_controls(unsigned physical_profile);
void presented(unsigned physical_profile, bool active);
std::string diagnostic();
void report(const std::string&);
inline std::atomic<bool> editor_open{false};
} // namespace dkr::runtime::camera
