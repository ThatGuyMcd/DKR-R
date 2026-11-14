#include "netplay/friend_service.hpp"
#include "online_profile/profile_bridge.hpp"
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) try {
    const bool fixture = argc == 4 && std::string_view(argv[1]) == "--fixture";
    const auto root = fixture ? std::filesystem::path(argv[2])
                              : std::filesystem::temp_directory_path() /
                                    ("dkrr-profile-bridge-" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (fixture && root.filename().string().find("profile-proof-fixture") != 0)
        throw std::runtime_error("Use a dedicated profile-proof-fixture directory.");
    dkr::runtime::netplay::FriendService service;
    service.configure(root, "Profile Fixture");
    const auto card = dkr::profile::bridge::card();
    if (!dkr::profile::bridge::verify(card.public_key, card.identity, card.friend_code))
        throw std::runtime_error("Profile binding failed");
    if (fixture) {
        const auto proof = dkr::profile::bridge::proof(argv[3]);
        std::cout << "{\"publicKey\":\"" << card.public_key << "\",\"identity\":\"" << card.identity
                  << "\",\"friendCode\":\"" << card.friend_code << "\",\"proof\":\"" << proof << "\"}\n";
    }
    service.shutdown();
    if (!dkr::profile::bridge::proof(card.public_key).empty())
        throw std::runtime_error("Retired profile key was still accessible");
    if (!fixture) {
        std::filesystem::remove_all(root);
        std::cout << "Profile binding and retirement checks passed\n";
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
