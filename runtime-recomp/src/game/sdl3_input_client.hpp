#pragma once

#include "controller_snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace dkr::runtime::sdl3_input {

struct Device {
    int instance = -1;
    std::string persistent_key;
    std::string name;
    bool rumble = false;
    bool gyro = false;
    bool mapped = true;
    std::string mapping_source;
    controllers::ControllerSnapshot snapshot{};
};

struct State {
    std::uint64_t sequence = 0U;
    std::vector<Device> devices;
};

class Client {
  public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool start(const std::filesystem::path& host, const std::filesystem::path& mapping_database, std::string& error,
               std::stop_token stop_token = {});
    void stop();
    bool healthy() const;
    std::string detail() const;
    State state() const;
    bool rumble(int instance, std::uint16_t low_frequency, std::uint16_t high_frequency, std::uint32_t duration_ms);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dkr::runtime::sdl3_input
