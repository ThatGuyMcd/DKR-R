#pragma once

#include "netplay_types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

class Lobby final {
public:
    bool create(std::string room_id, std::string join_code,
                std::string room_name, Visibility visibility,
                std::string host_peer_id, std::string host_name,
                const CompatibilityManifest& manifest, const Rules& rules,
                std::string& error);
    std::optional<std::uint8_t> join(std::string peer_id,
                                     std::string display_name,
                                     const CompatibilityManifest& manifest,
                                     std::string& error);
    bool leave(std::string_view peer_id, bool compact_slots = true);
    bool move_client(std::string_view host_peer_id,
                     std::string_view client_peer_id,
                     std::uint8_t destination_slot, std::string& error);
    bool set_ready(std::string_view peer_id, bool ready, std::string& error);
    bool set_loaded(std::string_view peer_id, bool loaded);
    bool set_network_metrics(std::string_view peer_id, Route route,
                             std::uint16_t ping_ms, std::uint16_t jitter_ms,
                             float packet_loss_percent);
    bool set_rules(std::string_view host_peer_id, const Rules& rules,
                   std::string& error);
    bool begin_loading(std::string_view host_peer_id, std::string& error);
    bool begin_running(std::string_view host_peer_id, std::string& error);
    void return_to_waiting();
    // Only the ended experimental match may refresh its isolated save seed.
    void refresh_online_save_hash(std::uint64_t hash);

    const Room& room() const { return room_; }
    bool can_start(std::string* reason = nullptr) const;
    std::size_t player_count() const;

private:
    Player* find(std::string_view peer_id);
    const Player* find(std::string_view peer_id) const;
    void invalidate_ready();
    void normalize_slots();

    Room room_{};
};

} // namespace dkr::runtime::netplay
