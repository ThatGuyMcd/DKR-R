#pragma once

#include "netplay_types.hpp"
#include "secure_channel.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::runtime::netplay {

enum class FriendInviteLifetime : std::uint8_t {
    Permanent,
    SingleUse,
    Timed,
};

enum class FriendLobbyInviteStatus : std::uint8_t {
    Sent,
    Delivered,
    Accepted,
    Declined,
    Expired,
    Cancelled,
};

struct FriendView {
    bool operator==(const FriendView&) const = default;
    std::string identity;
    std::string display_name;
    std::string nickname;
    bool online = false;
    bool blocked = false;
    bool hosting = false;
    std::string lobby_code;
    std::uint32_t players = 0U;
    std::uint32_t maximum_players = 0U;
    std::uint32_t ping_ms = 0U;
    std::uint64_t last_seen_unix = 0U;
};

struct FriendRequestView {
    bool operator==(const FriendRequestView&) const = default;
    std::uint64_t request_id = 0U;
    std::string identity;
    std::string display_name;
    bool incoming = false;
    std::string delivery_status;
    bool can_retry = false;
};

struct FriendInviteView {
    bool operator==(const FriendInviteView&) const = default;
    std::uint64_t invite_id = 0U;
    std::string code;
    FriendInviteLifetime lifetime = FriendInviteLifetime::Permanent;
    std::uint64_t expires_unix = 0U;
    std::string connection_status;
};

struct FriendLobbyAdvertisement {
    bool hosting = false;
    std::string quick_join_code;
    std::uint32_t players = 0U;
    std::uint32_t maximum_players = static_cast<std::uint32_t>(kSupportedOnlinePlayers);
    std::string synchronization;
    std::string compatibility;
};

struct FriendLobbyInviteView {
    bool operator==(const FriendLobbyInviteView&) const = default;
    std::uint64_t invite_id = 0U;
    std::string friend_identity;
    std::string friend_display_name;
    std::string lobby_code;
    std::string synchronization;
    std::string compatibility;
    std::uint32_t players = 0U;
    std::uint32_t maximum_players = static_cast<std::uint32_t>(kSupportedOnlinePlayers);
    std::uint64_t expires_unix = 0U;
    secure::Key admission{};
    FriendLobbyInviteStatus status = FriendLobbyInviteStatus::Sent;
    bool incoming = false;
};

struct FriendServiceSnapshot {
    std::vector<FriendView> friends;
    std::vector<FriendRequestView> requests;
    std::vector<FriendInviteView> invitations;
    std::vector<FriendLobbyInviteView> incoming_lobby_invites;
    std::vector<FriendLobbyInviteView> outgoing_lobby_invites;
    bool operator==(const FriendServiceSnapshot&) const = default;
};

class FriendService {
  public:
    FriendService();
    ~FriendService();
    FriendService(const FriendService&) = delete;
    FriendService& operator=(const FriendService&) = delete;

    void configure(const std::filesystem::path& config_directory, std::string_view display_name);
    void shutdown();
    void pump(const FriendLobbyAdvertisement& advertisement);
    std::shared_ptr<const FriendServiceSnapshot> snapshot() const;

    std::string display_name() const;
    bool set_display_name(std::string_view name, std::string& error);
    std::string identity_label() const;
    std::string status() const;
    bool presence_available() const;
    bool appear_offline() const;
    bool set_appear_offline(bool enabled, std::string& error);
    bool allow_lobby_invites() const;
    bool set_allow_lobby_invites(bool enabled, std::string& error);

    bool create_invite(FriendInviteLifetime lifetime, std::chrono::seconds duration, FriendInviteView& invite,
                       std::string& error);
    bool revoke_invite(std::uint64_t invite_id, std::string& error);
    std::vector<FriendInviteView> invitations() const;

    bool submit_friend_code(std::string_view code, std::string& error);
    bool retry_request(std::uint64_t request_id, std::string& error);
    std::string diagnostics() const;
    std::vector<FriendRequestView> pending_requests() const;
    bool accept_request(std::uint64_t request_id, std::string& error);
    bool reject_request(std::uint64_t request_id, bool block, std::string& error);

    std::vector<FriendView> friends(bool include_blocked = false) const;
    bool set_friend_nickname(std::string_view identity, std::string_view nickname, std::string& error);
    bool remove_friend(std::string_view identity, std::string& error);
    bool block_friend(std::string_view identity, std::string& error);
    bool unblock_friend(std::string_view identity, std::string& error);

    bool send_lobby_invite(std::string_view friend_identity, const FriendLobbyAdvertisement& lobby,
                           const secure::Key& admission, FriendLobbyInviteView& invite, std::string& error);
    std::vector<FriendLobbyInviteView> incoming_lobby_invites() const;
    std::vector<FriendLobbyInviteView> outgoing_lobby_invites() const;
    bool respond_lobby_invite(std::uint64_t invite_id, bool accept, FriendLobbyInviteView& invite, std::string& error);
    bool cancel_lobby_invite(std::uint64_t invite_id, std::string& error);

  private:
    friend struct FriendServiceTestAccess;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

FriendService& friend_service();

// Pure helpers used by validation tests and by the UI's controller keyboard.
bool valid_friend_code(std::string_view code);
std::string normalize_friend_code(std::string_view code);

} // namespace dkr::runtime::netplay
