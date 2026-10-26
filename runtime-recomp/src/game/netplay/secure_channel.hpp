#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay::secure {

inline constexpr std::size_t kKeyBytes = 32U;
using Key = std::array<std::uint8_t, kKeyBytes>;

struct KeyPair {
    Key secret{};
    Key public_key{};
};

Key generate_key();
KeyPair generate_key_pair();
std::string encode_key(const Key& key);
bool decode_key(std::string_view text, Key& key);

bool derive_peer_key(const Key& local_secret, const Key& remote_public, const Key& invitation_capability,
                     std::uint64_t match_id, Key& peer_key);

std::vector<std::uint8_t> seal_join_request(std::span<const std::uint8_t> plaintext, const Key& client_secret,
                                            const Key& client_public, const Key& host_public,
                                            const Key& invitation_capability, std::uint64_t sender_id,
                                            std::uint64_t match_id, Key& peer_key);
bool open_join_request(std::span<const std::uint8_t> packet, const Key& host_secret, const Key& invitation_capability,
                       std::uint64_t expected_match_id, std::uint64_t& sender_id, Key& client_public, Key& peer_key,
                       std::vector<std::uint8_t>& plaintext);
bool is_join_request(std::span<const std::uint8_t> packet);
bool inspect_join_request(std::span<const std::uint8_t> packet, std::uint64_t& sender_id, std::uint64_t& match_id);
bool inspect_packet(std::span<const std::uint8_t> packet, std::uint64_t& sender_id, std::uint64_t& match_id);

std::vector<std::uint8_t> seal(std::span<const std::uint8_t> plaintext, const Key& key, std::uint64_t sender_id,
                               std::uint64_t sequence, std::uint64_t match_id);
bool open(std::span<const std::uint8_t> packet, const Key& key, std::uint64_t expected_match_id,
          std::uint64_t& sender_id, std::uint64_t& sequence, std::vector<std::uint8_t>& plaintext);

} // namespace dkr::runtime::netplay::secure
