#include "netplay_lobby.hpp"

#include <algorithm>
#include <utility>

namespace dkr::runtime::netplay {

bool Lobby::create(std::string room_id, std::string join_code,
                   std::string room_name, Visibility visibility,
                   std::string host_peer_id, std::string host_name,
                   const CompatibilityManifest& manifest, const Rules& rules,
                   std::string& error) {
    if (room_id.empty() || join_code.size() < 6U ||
        !valid_room_name(room_name) || !valid_display_name(host_name) ||
        host_peer_id.empty() || !valid_rules(rules)) {
        error = "The lobby settings are invalid.";
        return false;
    }
    room_ = {};
    room_.room_id = std::move(room_id);
    room_.join_code = std::move(join_code);
    room_.name = std::move(room_name);
    room_.visibility = visibility;
    room_.manifest = manifest;
    room_.rules = rules;
    room_.phase = RoomPhase::Waiting;
    room_.generation = 1U;
    room_.players[0] = Player{true, true, false, true, false, 0U,
                              std::move(host_peer_id), std::move(host_name)};
    error.clear();
    return true;
}

std::optional<std::uint8_t> Lobby::join(
    std::string peer_id, std::string display_name,
    const CompatibilityManifest& manifest, std::string& error) {
    if (room_.room_id.empty() || room_.phase != RoomPhase::Waiting) {
        error = "This lobby is not accepting racers.";
        return std::nullopt;
    }
    if (peer_id.empty() || !valid_display_name(display_name) || find(peer_id)) {
        error = "This racer identity is invalid or already connected.";
        return std::nullopt;
    }
    const std::string mismatch = incompatibility_reason(room_.manifest, manifest);
    if (!mismatch.empty()) {
        error = mismatch;
        return std::nullopt;
    }
    const std::size_t limit = room_.rules.maximum_players;
    for (std::size_t i = 1U; i < limit; ++i) {
        if (!room_.players[i].occupied) {
            room_.players[i] = Player{true, false, false, true, false,
                                      static_cast<std::uint8_t>(i),
                                      std::move(peer_id),
                                      std::move(display_name)};
            invalidate_ready();
            error.clear();
            return static_cast<std::uint8_t>(i);
        }
    }
    error = "This lobby is full.";
    return std::nullopt;
}

bool Lobby::leave(std::string_view peer_id, bool compact_slots) {
    Player* player = find(peer_id);
    if (!player) {
        return false;
    }
    if (player->host) {
        room_.phase = RoomPhase::Finished;
        for (auto& slot : room_.players) {
            slot.ready = false;
            slot.loaded = false;
        }
        ++room_.generation;
        return true;
    }
    *player = {};
    if (compact_slots) normalize_slots();
    invalidate_ready();
    return true;
}

bool Lobby::move_client(std::string_view host_peer_id,
                        std::string_view client_peer_id,
                        std::uint8_t destination_slot, std::string& error) {
    const Player* host = find(host_peer_id);
    Player* client = find(client_peer_id);
    if (!host || !host->host || !client || client->host ||
        destination_slot == 0U || destination_slot >= room_.rules.maximum_players) {
        error = "Only the host can move the connected client to Player 2.";
        return false;
    }
    const std::size_t source_slot = client->slot;
    std::swap(room_.players[source_slot], room_.players[destination_slot]);
    room_.players[source_slot].slot = static_cast<std::uint8_t>(source_slot);
    room_.players[destination_slot].slot = destination_slot;
    invalidate_ready();
    error.clear();
    return true;
}

bool Lobby::set_ready(std::string_view peer_id, bool ready, std::string& error) {
    Player* player = find(peer_id);
    if (!player || room_.phase != RoomPhase::Waiting || !player->compatible) {
        error = "This racer cannot change Ready state.";
        return false;
    }
    if (player->ready != ready) {
        player->ready = ready;
        ++room_.generation;
    }
    room_.phase = RoomPhase::Waiting;
    error.clear();
    return true;
}

bool Lobby::set_loaded(std::string_view peer_id, bool loaded) {
    Player* player = find(peer_id);
    if (!player || room_.phase != RoomPhase::Loading) {
        return false;
    }
    if (player->loaded != loaded) {
        player->loaded = loaded;
        ++room_.generation;
    }
    return true;
}

bool Lobby::set_network_metrics(std::string_view peer_id, Route route,
                                std::uint16_t ping_ms, std::uint16_t jitter_ms,
                                float packet_loss_percent) {
    Player* player = find(peer_id);
    if (!player) return false;
    player->route = route;
    player->ping_ms = ping_ms;
    player->jitter_ms = jitter_ms;
    player->packet_loss_percent = std::clamp(packet_loss_percent, 0.0F, 100.0F);
    return true;
}

bool Lobby::set_rules(std::string_view host_peer_id, const Rules& rules,
                      std::string& error) {
    const Player* host = find(host_peer_id);
    if (!host || !host->host || room_.phase != RoomPhase::Waiting ||
        !valid_rules(rules) || rules.maximum_players < player_count()) {
        error = "The room rules cannot be changed to those values.";
        return false;
    }
    if (!(room_.rules == rules)) {
        room_.rules = rules;
        invalidate_ready();
    }
    error.clear();
    return true;
}

bool Lobby::can_start(std::string* reason) const {
    auto reject = [reason](std::string text) {
        if (reason) {
            *reason = std::move(text);
        }
        return false;
    };
    if (room_.phase != RoomPhase::Waiting) {
        return reject("The lobby is not waiting at the starting grid.");
    }
    if (player_count() < 2U) {
        return reject("At least two racers are required.");
    }
    // JOINTVENTURE is the retail two-player Adventure mode, not a four-player
    // race modifier. Reject that unsupported combination before any launch.
    if (player_count() > 2U && (room_.manifest.magic_codes_hash & (1ULL << 24U))) {
        return reject("Two-player Adventure (JOINTVENTURE) requires exactly two racers. Disable it for three- or four-player races and minigames.");
    }
    for (const Player& player : room_.players) {
        if (player.occupied && (!player.compatible || !player.ready)) {
            return reject("Every connected racer must be compatible and Ready.");
        }
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

bool Lobby::begin_loading(std::string_view host_peer_id, std::string& error) {
    const Player* host = find(host_peer_id);
    if (!host || !host->host || !can_start(&error)) {
        if (error.empty()) {
            error = "Only Player 1 can start the lobby.";
        }
        return false;
    }
    room_.phase = RoomPhase::Loading;
    ++room_.generation;
    for (auto& player : room_.players) {
        player.loaded = false;
    }
    return true;
}

bool Lobby::begin_running(std::string_view host_peer_id, std::string& error) {
    const Player* host = find(host_peer_id);
    if (!host || !host->host || room_.phase != RoomPhase::Loading) {
        error = "Only Player 1 can release the synchronized start.";
        return false;
    }
    for (const Player& player : room_.players) {
        if (player.occupied && !player.loaded) {
            error = "Every racer must finish loading before frame zero.";
            return false;
        }
    }
    room_.phase = RoomPhase::Running;
    ++room_.generation;
    error.clear();
    return true;
}

void Lobby::return_to_waiting() {
    room_.phase = RoomPhase::Waiting;
    invalidate_ready();
}

void Lobby::refresh_online_save_hash(std::uint64_t hash) {
    if (room_.phase != RoomPhase::Waiting || !hash) return;
    room_.manifest.session_save_hash = hash;
    invalidate_ready();
}

std::size_t Lobby::player_count() const {
    return static_cast<std::size_t>(std::count_if(
        room_.players.begin(), room_.players.end(),
        [](const Player& player) { return player.occupied; }));
}

Player* Lobby::find(std::string_view peer_id) {
    auto it = std::find_if(room_.players.begin(), room_.players.end(),
                           [peer_id](const Player& player) {
                               return player.occupied && player.peer_id == peer_id;
                           });
    return it == room_.players.end() ? nullptr : &*it;
}

const Player* Lobby::find(std::string_view peer_id) const {
    auto it = std::find_if(room_.players.begin(), room_.players.end(),
                           [peer_id](const Player& player) {
                               return player.occupied && player.peer_id == peer_id;
                           });
    return it == room_.players.end() ? nullptr : &*it;
}

void Lobby::invalidate_ready() {
    ++room_.generation;
    for (auto& player : room_.players) {
        player.ready = false;
        player.loaded = false;
    }
}

void Lobby::normalize_slots() {
    for (std::size_t i = 1U; i + 1U < room_.players.size(); ++i) {
        if (room_.players[i].occupied) {
            continue;
        }
        for (std::size_t j = i + 1U; j < room_.players.size(); ++j) {
            if (room_.players[j].occupied) {
                room_.players[i] = std::move(room_.players[j]);
                room_.players[j] = {};
                room_.players[i].slot = static_cast<std::uint8_t>(i);
                break;
            }
        }
    }
}

} // namespace dkr::runtime::netplay
