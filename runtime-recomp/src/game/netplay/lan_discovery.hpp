#pragma once

#include "netplay_types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {

struct LanLobby {
    std::uint64_t match_id = 0U;
    std::string room_name;
    std::string host_name;
    std::string invite;
    Revision revision = Revision::UsV77;
    std::uint8_t players = 0U;
    std::uint8_t maximum_players = static_cast<std::uint8_t>(kSupportedOnlinePlayers);
    bool in_progress = false;
    std::chrono::steady_clock::time_point last_seen{};
};

class LanDiscovery final {
  public:
    LanDiscovery();
    ~LanDiscovery();
    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;

    bool start(std::string& error);
    void stop();
    bool running() const;
    void advertise(const LanLobby& lobby);
    void clear_advertisement();
    void set_probe_addresses(std::vector<std::string> addresses);
    void request_refresh();
    void pump();
    const std::vector<LanLobby>& lobbies() const {
        return lobbies_;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<LanLobby> lobbies_;
};

} // namespace dkr::runtime::netplay
