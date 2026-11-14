#include "secure_channel.hpp"

#include "monocypher.h"

#include <algorithm>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <unistd.h>
#else
#include <cstdlib>
#endif

namespace dkr::runtime::netplay::secure {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'D', 'K', 'R', 'E'};
constexpr std::array<std::uint8_t, 4> kJoinMagic{'D', 'K', 'R', 'J'};
constexpr std::size_t kHeaderBytes = 4U + 8U + 8U + 8U;
constexpr std::size_t kJoinHeaderBytes = 4U + 8U + 8U + kKeyBytes;
constexpr std::size_t kMacBytes = 16U;

void put64(std::uint8_t* out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        *out++ = static_cast<std::uint8_t>(value >> shift);
}

std::uint64_t take64(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8U) | in[index];
    return value;
}

std::array<std::uint8_t, 24> nonce(std::uint64_t sender_id, std::uint64_t sequence, std::uint64_t match_id) {
    std::array<std::uint8_t, 24> result{};
    put64(result.data(), sender_id);
    put64(result.data() + 8U, sequence);
    put64(result.data() + 16U, match_id);
    return result;
}

int hex_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool random_bytes(std::span<std::uint8_t> output) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, output.data(), static_cast<ULONG>(output.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__linux__)
    std::size_t offset = 0U;
    while (offset < output.size()) {
        const ssize_t count = getrandom(output.data() + offset, output.size() - offset, 0);
        if (count <= 0)
            return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
#else
    arc4random_buf(output.data(), output.size());
    return true;
#endif
}

bool all_zero(const Key& key) {
    std::uint8_t value = 0U;
    for (const std::uint8_t byte : key)
        value |= byte;
    return value == 0U;
}

std::array<std::uint8_t, 24> join_nonce(std::uint64_t sender_id, std::uint64_t match_id) {
    std::array<std::uint8_t, 24> result{};
    put64(result.data(), sender_id);
    put64(result.data() + 8U, match_id);
    std::copy(kJoinMagic.begin(), kJoinMagic.end(), result.begin() + 16U);
    return result;
}

} // namespace

Key generate_key() {
    Key key{};
    if (!random_bytes(key))
        std::abort();
    return key;
}

KeyPair generate_key_pair() {
    KeyPair pair{};
    pair.secret = generate_key();
    crypto_x25519_public_key(pair.public_key.data(), pair.secret.data());
    return pair;
}

std::string encode_key(const Key& key) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.resize(key.size() * 2U);
    for (std::size_t index = 0; index < key.size(); ++index) {
        result[index * 2U] = alphabet[key[index] >> 4U];
        result[index * 2U + 1U] = alphabet[key[index] & 0x0FU];
    }
    return result;
}

bool decode_key(std::string_view text, Key& key) {
    if (text.size() != key.size() * 2U)
        return false;
    Key parsed{};
    for (std::size_t index = 0; index < parsed.size(); ++index) {
        const int high = hex_value(text[index * 2U]);
        const int low = hex_value(text[index * 2U + 1U]);
        if (high < 0 || low < 0)
            return false;
        parsed[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    key = parsed;
    return true;
}

bool derive_peer_key(const Key& local_secret, const Key& remote_public, const Key& invitation_capability,
                     std::uint64_t match_id, Key& peer_key) {
    Key shared{};
    crypto_x25519(shared.data(), local_secret.data(), remote_public.data());
    if (all_zero(shared))
        return false;
    std::array<std::uint8_t, kKeyBytes * 2U + 16U> material{};
    std::copy(shared.begin(), shared.end(), material.begin());
    std::copy(invitation_capability.begin(), invitation_capability.end(),
              material.begin() + static_cast<std::ptrdiff_t>(kKeyBytes));
    put64(material.data() + kKeyBytes * 2U, match_id);
    constexpr std::array<std::uint8_t, 8> context{'D', 'K', 'R', '-', 'P', 'E', 'E', 'R'};
    std::copy(context.begin(), context.end(), material.begin() + static_cast<std::ptrdiff_t>(kKeyBytes * 2U + 8U));
    crypto_blake2b(peer_key.data(), peer_key.size(), material.data(), material.size());
    crypto_wipe(shared.data(), shared.size());
    crypto_wipe(material.data(), material.size());
    return !all_zero(peer_key);
}

std::vector<std::uint8_t> seal_join_request(std::span<const std::uint8_t> plaintext, const Key& client_secret,
                                            const Key& client_public, const Key& host_public,
                                            const Key& invitation_capability, std::uint64_t sender_id,
                                            std::uint64_t match_id, Key& peer_key) {
    if (!derive_peer_key(client_secret, host_public, invitation_capability, match_id, peer_key))
        return {};
    std::vector<std::uint8_t> result(kJoinHeaderBytes + kMacBytes + plaintext.size());
    std::copy(kJoinMagic.begin(), kJoinMagic.end(), result.begin());
    put64(result.data() + 4U, match_id);
    put64(result.data() + 12U, sender_id);
    std::copy(client_public.begin(), client_public.end(), result.begin() + 20U);
    const auto packet_nonce = join_nonce(sender_id, match_id);
    crypto_aead_lock(result.data() + kJoinHeaderBytes + kMacBytes, result.data() + kJoinHeaderBytes, peer_key.data(),
                     packet_nonce.data(), result.data(), kJoinHeaderBytes, plaintext.data(), plaintext.size());
    return result;
}

bool open_join_request(std::span<const std::uint8_t> packet, const Key& host_secret, const Key& invitation_capability,
                       std::uint64_t expected_match_id, std::uint64_t& sender_id, Key& client_public, Key& peer_key,
                       std::vector<std::uint8_t>& plaintext) {
    plaintext.clear();
    if (packet.size() < kJoinHeaderBytes + kMacBytes || !is_join_request(packet))
        return false;
    const std::uint64_t match_id = take64(packet.data() + 4U);
    if (match_id != expected_match_id)
        return false;
    sender_id = take64(packet.data() + 12U);
    std::copy_n(packet.data() + 20U, kKeyBytes, client_public.begin());
    if (!derive_peer_key(host_secret, client_public, invitation_capability, match_id, peer_key))
        return false;
    plaintext.resize(packet.size() - kJoinHeaderBytes - kMacBytes);
    const auto packet_nonce = join_nonce(sender_id, match_id);
    if (crypto_aead_unlock(plaintext.data(), packet.data() + kJoinHeaderBytes, peer_key.data(), packet_nonce.data(),
                           packet.data(), kJoinHeaderBytes, packet.data() + kJoinHeaderBytes + kMacBytes,
                           plaintext.size()) != 0) {
        plaintext.clear();
        return false;
    }
    return true;
}

bool is_join_request(std::span<const std::uint8_t> packet) {
    return packet.size() >= kJoinHeaderBytes + kMacBytes &&
           std::equal(kJoinMagic.begin(), kJoinMagic.end(), packet.begin());
}

bool inspect_join_request(std::span<const std::uint8_t> packet, std::uint64_t& sender_id, std::uint64_t& match_id) {
    if (!is_join_request(packet))
        return false;
    match_id = take64(packet.data() + 4U);
    sender_id = take64(packet.data() + 12U);
    return true;
}

bool inspect_packet(std::span<const std::uint8_t> packet, std::uint64_t& sender_id, std::uint64_t& match_id) {
    if (packet.size() < kHeaderBytes + kMacBytes || !std::equal(kMagic.begin(), kMagic.end(), packet.begin()))
        return false;
    sender_id = take64(packet.data() + 4U);
    match_id = take64(packet.data() + 20U);
    return true;
}

std::vector<std::uint8_t> seal(std::span<const std::uint8_t> plaintext, const Key& key, std::uint64_t sender_id,
                               std::uint64_t sequence, std::uint64_t match_id) {
    std::vector<std::uint8_t> result(kHeaderBytes + kMacBytes + plaintext.size());
    std::copy(kMagic.begin(), kMagic.end(), result.begin());
    put64(result.data() + 4U, sender_id);
    put64(result.data() + 12U, sequence);
    put64(result.data() + 20U, match_id);
    const auto packet_nonce = nonce(sender_id, sequence, match_id);
    crypto_aead_lock(result.data() + kHeaderBytes + kMacBytes, result.data() + kHeaderBytes, key.data(),
                     packet_nonce.data(), result.data(), kHeaderBytes, plaintext.data(), plaintext.size());
    return result;
}

bool open(std::span<const std::uint8_t> packet, const Key& key, std::uint64_t expected_match_id,
          std::uint64_t& sender_id, std::uint64_t& sequence, std::vector<std::uint8_t>& plaintext) {
    plaintext.clear();
    if (packet.size() < kHeaderBytes + kMacBytes || !std::equal(kMagic.begin(), kMagic.end(), packet.begin())) {
        return false;
    }
    sender_id = take64(packet.data() + 4U);
    sequence = take64(packet.data() + 12U);
    const std::uint64_t match_id = take64(packet.data() + 20U);
    if (match_id != expected_match_id)
        return false;
    plaintext.resize(packet.size() - kHeaderBytes - kMacBytes);
    const auto packet_nonce = nonce(sender_id, sequence, match_id);
    if (crypto_aead_unlock(plaintext.data(), packet.data() + kHeaderBytes, key.data(), packet_nonce.data(),
                           packet.data(), kHeaderBytes, packet.data() + kHeaderBytes + kMacBytes,
                           plaintext.size()) != 0) {
        plaintext.clear();
        return false;
    }
    return true;
}

} // namespace dkr::runtime::netplay::secure
