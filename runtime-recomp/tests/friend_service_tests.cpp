#include "netplay/friend_service.hpp"
#include "online_profile/profile_bridge.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool require(bool condition, const char* message) {
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    namespace netplay = dkr::runtime::netplay;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("dkrr-friend-service-" + std::to_string(stamp));
    std::error_code filesystem_error;
    std::filesystem::create_directories(root, filesystem_error);
    if (!require(!filesystem_error, "temporary profile directory could not be created")) {
        return 1;
    }

    netplay::FriendService service;
    service.configure(root, "Timber Tester");
    const std::string identity = service.identity_label();
    bool passed = true;
    passed &= require(!identity.empty(), "profile identity was not generated");
    passed &= require(service.display_name() == "Timber Tester", "display name was not initialized");
    passed &= require(!service.appear_offline(), "new profiles did not default to visible presence");
    passed &= require(service.allow_lobby_invites(), "new profiles did not default to accepting lobby invites");

    netplay::FriendInviteView permanent{};
    std::string error;
    passed &= require(
        service.create_invite(netplay::FriendInviteLifetime::Permanent, std::chrono::seconds(0), permanent, error),
        "permanent Friend Code was not generated");
    if (!netplay::valid_friend_code(permanent.code)) {
        std::cerr << "Generated code: " << permanent.code << '\n';
    }
    passed &= require(netplay::valid_friend_code(permanent.code), "generated Friend Code failed validation");
    const auto card = dkr::profile::bridge::card();
    passed &=
        require(!card.public_key.empty() && card.identity.size() == 13 && netplay::valid_friend_code(card.friend_code),
                "lobby profile did not bind a valid native identity");
    passed &= require(dkr::profile::bridge::verify(card.public_key, card.identity, card.friend_code),
                      "lobby profile failed its identity/code binding");
    passed &= require(!dkr::profile::bridge::verify(card.public_key, "OTHERIDENTITY", card.friend_code),
                      "lobby code accepted a substituted identity");
    const auto server = netplay::secure::generate_key_pair();
    const auto proof = dkr::profile::bridge::proof(netplay::secure::encode_key(server.public_key));
    passed &= require(proof.size() == 64 &&
                          proof == dkr::profile::bridge::proof(netplay::secure::encode_key(server.public_key)),
                      "profile key ownership proof was not stable for one challenge");
    passed &= require(dkr::profile::bridge::proof(std::string(64, '0')).empty(), "zero challenge key was accepted");
    passed &= require(
        !dkr::profile::bridge::verify(netplay::secure::encode_key(server.public_key), card.identity, card.friend_code),
        "profile card accepted a different public key");
    dkr::profile::bridge::own_avatar("https://example.invalid/picture.png");
    passed &= require(dkr::profile::bridge::own_avatar().empty(), "unapproved avatar host was advertised");
    passed &= require(permanent.code.size() == 12U && permanent.code.starts_with("DKR-"),
                      "Friend Code was not emitted in the 12-character format");
    const std::string payload_only = permanent.code.substr(4U);
    passed &= require(netplay::valid_friend_code(payload_only), "eight-character controller entry was not accepted");
    passed &= require(netplay::normalize_friend_code(permanent.code) ==
                          netplay::normalize_friend_code(" " + permanent.code + " "),
                      "Friend Code normalization was not whitespace tolerant");

    std::string tampered = permanent.code;
    tampered.back() = tampered.back() == 'A' ? 'B' : 'A';
    passed &= require(!netplay::valid_friend_code(tampered), "tampered Friend Code passed its checksum");
    passed &= require(
        netplay::valid_friend_code("DKRR-J2AU-DCEU-PUBE-UFSM-MEDR-LYS9-4BPJ-J5TM-U2BT-A9JD-D6AA-AAAA-AAAA-AAAA-EZ5S"),
        "legacy Friend Code compatibility regressed");

    netplay::FriendLobbyAdvertisement lobby{};
    lobby.hosting = true;
    lobby.quick_join_code = "ABCDE";
    lobby.players = 1U;
    lobby.maximum_players = static_cast<std::uint32_t>(netplay::kSupportedOnlinePlayers);
    lobby.synchronization = "ROLLBACK";
    lobby.compatibility = "test-manifest";
    netplay::FriendLobbyInviteView lobby_invite{};
    const auto admission = netplay::secure::generate_key();
    passed &= require(!service.send_lobby_invite("not-a-friend", lobby, admission, lobby_invite, error),
                      "an unauthenticated identity received a lobby invitation");
    passed &= require(service.incoming_lobby_invites().empty() && service.outgoing_lobby_invites().empty(),
                      "a rejected lobby invitation leaked into runtime state");
    passed &= require(!service.respond_lobby_invite(1U, true, lobby_invite, error),
                      "a nonexistent lobby invitation was accepted");
    passed &= require(!service.cancel_lobby_invite(1U, error), "a nonexistent outgoing invitation was cancelled");

    passed &= require(service.set_display_name("Pipsy Pilot", error), "display name update failed");
    passed &= require(service.set_appear_offline(true, error), "appear-offline preference update failed");
    passed &= require(service.set_allow_lobby_invites(false, error), "lobby-invitation preference update failed");
    service.shutdown();
    service.configure(root, "Ignored Name");
    passed &= require(service.identity_label() == identity, "profile identity did not survive restart");
    passed &= require(service.display_name() == "Pipsy Pilot", "profile display name did not survive restart");
    passed &= require(service.appear_offline(), "appear-offline preference did not survive restart");
    passed &= require(!service.allow_lobby_invites(), "lobby-invitation preference did not survive restart");
    passed &= require(!service.invitations().empty() && service.invitations().front().code == permanent.code,
                      "active Friend Code did not survive restart");
    service.shutdown();

    std::filesystem::remove_all(root, filesystem_error);
    return passed ? 0 : 1;
}
