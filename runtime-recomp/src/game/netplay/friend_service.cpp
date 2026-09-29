#include "friend_service.hpp"
#include "../atomic_snapshot.hpp"
#include "../performance_trace.hpp"

#include "session_transport.hpp"
#include "social_executor.hpp"

#include "monocypher.h"

#if DKR_NETPLAY_WEBRTC
#include "social_json_policy.hpp"
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dkr::runtime::netplay {
namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr std::size_t kFingerprintBytes = 8U;
constexpr std::size_t kCapabilityBytes = 16U;
constexpr std::size_t kInviteTokenBytes = 5U;
constexpr std::size_t kLegacyFriendCodeBytes =
    2U + kFingerprintBytes + kCapabilityBytes + 8U + 1U + 2U;
constexpr std::uint8_t kLegacyCodeVersion = 1U;
// Social v4 binds proofs to the sender identity/nonce and supports durable
// decisions. It does not change gameplay transport or stored friend identities.
constexpr std::uint64_t kPresenceProtocol = 4U;
// Negotiated independently: old v4 presence/lobby peers remain compatible.
constexpr std::uint64_t kRequestProtocol = 1U;
constexpr std::size_t kMaximumLobbyInvites = 16U;
constexpr std::uint64_t kLobbyInviteLifetimeSeconds = 300U;

using Fingerprint = std::array<std::uint8_t, kFingerprintBytes>;
using Capability = std::array<std::uint8_t, kCapabilityBytes>;
using InviteToken = std::array<std::uint8_t, kInviteTokenBytes>;
using Nonce = std::array<std::uint8_t, 16U>;

struct ParsedFriendCode {
    Fingerprint fingerprint{};
    Capability capability{};
    std::optional<InviteToken> invite_token;
    std::uint64_t expires_unix = 0U;
    FriendInviteLifetime lifetime = FriendInviteLifetime::Permanent;
};

std::uint64_t unix_seconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                                         .count());
}

void put64(std::uint8_t* destination, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *destination++ = static_cast<std::uint8_t>(value >> shift);
    }
}

std::uint64_t take64(const std::uint8_t* source) {
    std::uint64_t value = 0U;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8U) | source[index];
    }
    return value;
}

template <std::size_t Size>
std::string hex_encode(const std::array<std::uint8_t, Size>& bytes) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(Size * 2U, '0');
    for (std::size_t index = 0U; index < Size; ++index) {
        result[index * 2U] = alphabet[bytes[index] >> 4U];
        result[index * 2U + 1U] = alphabet[bytes[index] & 0xFU];
    }
    return result;
}

std::string hex_encode_text(std::string_view text) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(text.size() * 2U, '0');
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto byte = static_cast<std::uint8_t>(text[index]);
        result[index * 2U] = alphabet[byte >> 4U];
        result[index * 2U + 1U] = alphabet[byte & 0xFU];
    }
    return result;
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <std::size_t Size>
bool hex_decode(std::string_view text,
                std::array<std::uint8_t, Size>& bytes) {
    if (text.size() != Size * 2U) return false;
    std::array<std::uint8_t, Size> decoded{};
    for (std::size_t index = 0U; index < Size; ++index) {
        const int high = hex_digit(text[index * 2U]);
        const int low = hex_digit(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        decoded[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    bytes = decoded;
    return true;
}

bool hex_decode_text(std::string_view text, std::string& result) {
    if ((text.size() & 1U) != 0U) return false;
    std::string decoded(text.size() / 2U, '\0');
    for (std::size_t index = 0U; index < decoded.size(); ++index) {
        const int high = hex_digit(text[index * 2U]);
        const int low = hex_digit(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        decoded[index] = static_cast<char>((high << 4) | low);
    }
    result = std::move(decoded);
    return true;
}

std::string base32_encode(std::span<const std::uint8_t> bytes) {
    std::string result;
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (const std::uint8_t byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            result.push_back(kAlphabet[(accumulator >> bits) & 31U]);
        }
    }
    if (bits > 0) {
        result.push_back(kAlphabet[(accumulator << (5 - bits)) & 31U]);
    }
    return result;
}

bool base32_decode(std::string_view text, std::vector<std::uint8_t>& bytes) {
    bytes.clear();
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (const char character : text) {
        const std::size_t value = kAlphabet.find(character);
        if (value == std::string_view::npos) return false;
        accumulator = (accumulator << 5U) | static_cast<std::uint32_t>(value);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(accumulator >> bits));
            accumulator &= (1U << bits) - 1U;
        }
    }
    return true;
}

std::string grouped(std::string_view compact) {
    std::string result = "DKRR";
    for (std::size_t index = 0U; index < compact.size(); ++index) {
        if ((index % 4U) == 0U) result.push_back('-');
        result.push_back(compact[index]);
    }
    return result;
}

std::string grouped_friend_code(std::string_view compact) {
    return "DKR-" + std::string(compact);
}

Fingerprint fingerprint(const secure::Key& public_key) {
    Fingerprint result{};
    crypto_blake2b(result.data(), result.size(), public_key.data(),
                   public_key.size());
    return result;
}

std::string identity_of(const secure::Key& public_key) {
    return base32_encode(fingerprint(public_key));
}

bool valid_invite_token(const InviteToken& token);

std::string peer_id_for(std::string_view identity) {
    std::string result = "dkrr-presence-1-";
    for (const char value : identity) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(value))));
    }
    return result;
}

std::string invite_route(const InviteToken& token) {
    return "invite:" + base32_encode(token);
}

bool parse_invite_route(std::string_view route, InviteToken& token) {
    constexpr std::string_view prefix = "invite:";
    if (!route.starts_with(prefix)) return false;
    std::vector<std::uint8_t> decoded;
    const std::string_view encoded = route.substr(prefix.size());
    if (encoded.size() != 8U || !base32_decode(encoded, decoded) ||
        decoded.size() != token.size()) {
        return false;
    }
    std::copy_n(decoded.begin(), token.size(), token.begin());
    return valid_invite_token(token);
}

std::string peer_id_for_invite(const InviteToken& token) {
    std::string result = "dkrr-invite-3-";
    const std::string encoded = base32_encode(token);
    for (const char value : encoded) {
        result.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(value))));
    }
    return result;
}

std::string peer_id_for_route(std::string_view route) {
    InviteToken token{};
    return parse_invite_route(route, token) ? peer_id_for_invite(token)
                                            : peer_id_for(route);
}

std::array<std::uint8_t, 2U> code_checksum(
    std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, 2U> result{};
    crypto_blake2b(result.data(), result.size(), bytes.data(), bytes.size());
    return result;
}

std::uint8_t invite_token_checksum(const InviteToken& token) {
    std::array<std::uint8_t, kInviteTokenBytes> material = token;
    material.back() &= 0xE0U;
    std::array<std::uint8_t, 1U> digest{};
    crypto_blake2b(digest.data(), digest.size(), material.data(),
                   material.size());
    return digest[0] & 0x1FU;
}

bool valid_invite_token(const InviteToken& token) {
    return (token.back() & 0x1FU) == invite_token_checksum(token);
}

InviteToken generate_invite_token() {
    const secure::Key random = secure::generate_key();
    InviteToken result{};
    std::copy_n(random.begin(), result.size(), result.begin());
    result.back() &= 0xE0U;
    result.back() |= invite_token_checksum(result);
    return result;
}

Capability derive_capability(const InviteToken& token) {
    std::array<std::uint8_t, 12U + kInviteTokenBytes> material{};
    constexpr std::array<std::uint8_t, 12U> context{
        'D','K','R','R','-','I','N','V','I','T','E','3'};
    std::copy(context.begin(), context.end(), material.begin());
    std::copy(token.begin(), token.end(), material.begin() + context.size());
    Capability result{};
    crypto_blake2b(result.data(), result.size(), material.data(),
                   material.size());
    return result;
}

std::string encode_friend_code(const ParsedFriendCode& code) {
    if (code.invite_token.has_value()) {
        return grouped_friend_code(base32_encode(*code.invite_token));
    }

    // Codes issued by the first public Friends implementation remain valid.
    // New codes omit metadata that is authoritative on the issuing host.
    std::array<std::uint8_t, kLegacyFriendCodeBytes> bytes{};
    bytes[0] = 'F';
    bytes[1] = kLegacyCodeVersion;
    std::copy(code.fingerprint.begin(), code.fingerprint.end(), bytes.begin() + 2U);
    std::copy(code.capability.begin(), code.capability.end(),
              bytes.begin() + 2U + kFingerprintBytes);
    put64(bytes.data() + 2U + kFingerprintBytes + kCapabilityBytes,
          code.expires_unix);
    bytes[2U + kFingerprintBytes + kCapabilityBytes + 8U] =
        static_cast<std::uint8_t>(code.lifetime);
    const auto checksum = code_checksum(std::span<const std::uint8_t>(
        bytes.data(), bytes.size() - 2U));
    bytes[bytes.size() - 2U] = checksum[0];
    bytes[bytes.size() - 1U] = checksum[1];
    return grouped(base32_encode(bytes));
}

bool parse_friend_code(std::string_view text, ParsedFriendCode& code) {
    const std::string compact = normalize_friend_code(text);
    std::vector<std::uint8_t> bytes;
    if (!base32_decode(compact, bytes)) {
        return false;
    }

    if (compact.size() == 8U && bytes.size() == kInviteTokenBytes) {
        ParsedFriendCode parsed{};
        InviteToken token{};
        std::copy_n(bytes.begin(), token.size(), token.begin());
        if (!valid_invite_token(token)) return false;
        parsed.invite_token = token;
        parsed.capability = derive_capability(token);
        code = parsed;
        return true;
    }

    if (bytes.size() < 2U || bytes[0] != 'F' ||
        bytes[1] != kLegacyCodeVersion ||
        bytes.size() != kLegacyFriendCodeBytes) {
        return false;
    }
    const auto checksum = code_checksum(std::span<const std::uint8_t>(
        bytes.data(), bytes.size() - 2U));
    if (checksum[0] != bytes[bytes.size() - 2U] ||
        checksum[1] != bytes[bytes.size() - 1U]) {
        return false;
    }
    ParsedFriendCode parsed{};
    std::copy_n(bytes.begin() + 2U, kFingerprintBytes,
                parsed.fingerprint.begin());
    std::copy_n(bytes.begin() + 2U + kFingerprintBytes,
                kCapabilityBytes, parsed.capability.begin());
    parsed.expires_unix = take64(
        bytes.data() + 2U + kFingerprintBytes + kCapabilityBytes);
    const std::uint8_t lifetime =
        bytes[2U + kFingerprintBytes + kCapabilityBytes + 8U];
    if (lifetime > static_cast<std::uint8_t>(FriendInviteLifetime::Timed)) {
        return false;
    }
    parsed.lifetime = static_cast<FriendInviteLifetime>(lifetime);
    if (parsed.expires_unix != 0U && parsed.expires_unix < unix_seconds()) {
        return false;
    }
    code = parsed;
    return true;
}

std::vector<std::string> split(std::string_view value, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t found = value.find(separator, start);
        result.emplace_back(value.substr(start, found == std::string_view::npos
            ? value.size() - start : found - start));
        if (found == std::string_view::npos) break;
        start = found + 1U;
    }
    return result;
}

std::string clean_display_name(std::string_view name) {
    std::string result;
    result.reserve(std::min<std::size_t>(name.size(), 24U));
    for (const unsigned char character : name) {
        if (result.size() >= 24U) break;
        if (character >= 32U && character != 127U) {
            result.push_back(static_cast<char>(character));
        }
    }
    while (!result.empty() && std::isspace(
               static_cast<unsigned char>(result.back()))) result.pop_back();
    const auto first = std::find_if_not(result.begin(), result.end(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c));
    });
    result.erase(result.begin(), first);
    return result;
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

#if DKR_NETPLAY_WEBRTC
using Json = nlohmann::json;

constexpr std::string_view kPeerJsFallbackRoot = R"PEM(-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

std::string ca_bundle() {
    if (const char* configured = std::getenv("SSL_CERT_FILE");
        configured != nullptr && configured[0] != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(configured, error)) {
            return configured;
        }
    }
#if !defined(_WIN32)
    for (const char* candidate : {"/etc/ssl/certs/ca-certificates.crt",
                                  "/etc/ssl/cert.pem",
                                  "/etc/pki/tls/certs/ca-bundle.crt"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
#endif
    return std::string(kPeerJsFallbackRoot);
}

std::string random_token(std::size_t size) {
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(0U,
                                                             kAlphabet.size() - 1U);
    std::string result;
    result.reserve(size);
    while (result.size() < size) result.push_back(kAlphabet[distribution(generator)]);
    return result;
}

#if defined(DKR_FRIEND_TESTING)
std::string test_signaling_endpoint;
#endif
std::string signaling_url(std::string_view peer_id) {
#if defined(DKR_FRIEND_TESTING)
    if (!test_signaling_endpoint.empty()) return test_signaling_endpoint + "/" + std::string(peer_id);
#endif
    return "wss://0.peerjs.com/peerjs?key=peerjs&id=" + std::string(peer_id) +
           "&token=" + random_token(24U) + "&version=1.5.5";
}
#endif

} // namespace

std::string normalize_friend_code(std::string_view code) {
    std::string decorated;
    decorated.reserve(code.size());
    for (const char character : code) {
        if (std::isspace(static_cast<unsigned char>(character))) continue;
        decorated.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    if (decorated.starts_with("DKRR-")) {
        decorated.erase(0U, 5U);
    } else if (decorated.starts_with("DKR-")) {
        decorated.erase(0U, 4U);
    }
    std::string result;
    result.reserve(decorated.size());
    for (const char character : decorated) {
        if (character != '-') result.push_back(character);
    }
    // Also accept manually entered codes with the branded prefix but no dash.
    // Length checks prevent a payload which happens to begin with DKR/R from
    // being mistaken for a second prefix.
    if (result.size() == 64U && result.starts_with("DKRR")) {
        result.erase(0U, 4U);
    } else if (result.size() == 11U && result.starts_with("DKR")) {
        result.erase(0U, 3U);
    }
    return result;
}

bool valid_friend_code(std::string_view code) {
    ParsedFriendCode parsed{};
    return parse_friend_code(code, parsed);
}

struct FriendService::Impl : std::enable_shared_from_this<FriendService::Impl> {
    struct Invite {
        std::uint64_t id = 0U;
        Capability capability{};
        std::optional<InviteToken> token;
        FriendInviteLifetime lifetime = FriendInviteLifetime::Permanent;
        std::uint64_t expires = 0U;
        bool revoked = false;
    };
    struct Friend {
        secure::Key public_key{};
        std::string name;
        std::string nickname;
        bool blocked = false;
        std::uint64_t last_seen = 0U;
        bool online = false;
        bool hosting = false;
        std::string lobby_code;
        std::uint32_t players = 0U;
        std::uint32_t maximum_players = 0U;
        std::uint32_t ping_ms = 0U;
    };
    struct Request {
        std::uint64_t id = 0U;
        secure::Key public_key{};
        std::string expected_identity;
        std::string name;
        bool incoming = false;
        Capability capability{};
        bool cancelled = false;
        std::uint64_t wire_id = 0U;
        bool received = false;
        bool receipt_supported = false;
        bool legacy_retry = false;
        std::string problem;
        std::chrono::steady_clock::time_point manual_retry_after{};
    };
    struct LobbyInvite {
        std::uint64_t id = 0U;
        std::uint64_t wire_id = 0U;
        bool decision_acknowledged = false;
        std::string friend_identity;
        std::string friend_name;
        std::string lobby_code;
        std::string synchronization;
        std::string compatibility;
        std::uint32_t players = 0U;
        std::uint32_t maximum_players =
            static_cast<std::uint32_t>(kSupportedOnlinePlayers);
        std::uint64_t expires = 0U;
        secure::Key admission{};
        FriendLobbyInviteStatus status = FriendLobbyInviteStatus::Sent;
        bool incoming = false;
    };
    struct Decision {
        std::uint64_t id = 0U;
        secure::Key public_key{};
        Capability capability{};
        std::string kind;
        bool acknowledged = false;
        std::uint64_t wire_id = 0U;
    };

#if DKR_NETPLAY_WEBRTC
    struct Peer {
        std::string remote_id;
        std::string connection_id;
        std::shared_ptr<rtc::PeerConnection> connection;
        std::shared_ptr<rtc::DataChannel> channel;
        std::shared_ptr<rtc::WebSocket> signaling_socket;
        secure::Key remote_public{};
        Nonce local_nonce{};
        Nonce remote_nonce{};
        Capability local_invitation{};
        Capability invitation{};
        std::string remote_name = "Racer";
        Json remote_lobby = Json::object();
        bool remote_online = true;
        bool have_remote_hello = false;
        bool invitation_route = false;
        bool authenticated = false;
        bool initiator = false;
        bool request_receipts = false;
        bool send_failed = false;
        bool remote_description_set = false;
        bool retiring = false;
        std::uint64_t last_decision_sent = 0U;
#if defined(DKR_FRIEND_TESTING)
        std::function<void(std::string)> test_send;
#endif
        std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point connected{};
    };
#endif

    mutable std::mutex mutex;
    std::filesystem::path directory;
    std::filesystem::path store_path;
    secure::KeyPair identity{};
    std::string local_identity;
    std::string display_name = "Racer";
    std::string status = "Friend presence is starting...";
    std::map<std::string, Friend> friends;
    std::map<std::uint64_t, Invite> invites;
    std::map<std::uint64_t, Request> requests;
    std::map<std::uint64_t, Decision> decisions;
    std::map<std::uint64_t, LobbyInvite> incoming_lobby_invites;
    std::map<std::uint64_t, LobbyInvite> outgoing_lobby_invites;
    std::uint64_t next_id = 1U;
    FriendLobbyAdvertisement advertisement{};
    FriendLobbyAdvertisement pending_advertisement{};
    bool appear_offline = false;
    bool allow_lobby_invites = true;
    bool configured = false;
    SocialExecutor executor;
    struct Retry {
        std::chrono::steady_clock::time_point due{};
        unsigned attempts = 0U;
    };
    std::map<std::string, Retry> retries;
    std::string last_probe;
    std::chrono::steady_clock::time_point next_attempt{};
    std::atomic<std::uint64_t> generation{0U};
    std::uint64_t save_revision = 0U;
    std::uint64_t saved_revision = 0U;
    bool save_failed = false;
    bool preserve_recovery_backup = false;
    bool preserve_legacy_store = false;
    std::atomic<bool> callbacks_lost{false};
    std::deque<std::string> diagnostic_events;
    void diagnose_locked(std::string_view event) {
        // Call sites supply fixed descriptions only, never identifiers, codes,
        // names, SDP, addresses or backend error strings.
        if (diagnostic_events.size() == 64U) diagnostic_events.pop_front();
        diagnostic_events.push_back(std::to_string(unix_seconds()) + " " + std::string(event));
    }
    void tick();
    void publish_snapshot();
    void publish_requests_locked();
    AtomicSnapshot<const FriendServiceSnapshot> ui_snapshot{std::make_shared<const FriendServiceSnapshot>()};
    std::vector<FriendInviteView> copy_invitations_locked() const;
    std::vector<FriendRequestView> copy_pending_requests_locked() const;
    std::vector<FriendView> copy_friends_locked(bool include_blocked) const;
    std::vector<FriendLobbyInviteView> copy_incoming_lobby_invites_locked() const;
    std::vector<FriendLobbyInviteView> copy_outgoing_lobby_invites_locked() const;

    template<class Function>
    auto callback(Function function) {
        const auto weak = weak_from_this();
        const auto epoch = generation.load();
        return [weak, epoch, function = std::move(function)](auto... args) {
#if DKR_NETPLAY_WEBRTC
            const auto too_large = [](const auto& arg) {
                if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, rtc::message_variant>)
                    return std::visit([](const auto& payload) { return payload.size() > 65536U; }, arg);
                else return false;
            };
            if ((too_large(args) || ... || false)) return;
#endif
            if (const auto owner = weak.lock(); owner && owner->generation.load() == epoch) {
                if (!owner->executor.post([weak, epoch, function, ... args = std::move(args)]() mutable {
                    if (const auto current = weak.lock(); current && current->generation.load() == epoch)
                        function(std::move(args)...);
                })) owner->callbacks_lost.store(true);
            }
        };
    }

#if DKR_NETPLAY_WEBRTC
    std::shared_ptr<rtc::WebSocket> websocket;
    std::map<std::uint64_t, std::shared_ptr<rtc::WebSocket>> invite_websockets;
    struct InviteConnection {
        bool registered = false;
        unsigned failures = 0U;
        std::chrono::steady_clock::time_point started{}, heartbeat{}, retry{};
        std::string status = "Registering incoming requests...";
    };
    std::map<std::uint64_t, InviteConnection> invite_connections;
    std::map<std::string, std::shared_ptr<Peer>> peers;
    std::vector<std::shared_ptr<Peer>> retired_peers;
    struct EarlyCandidate {
        std::string remote, connection_id;
        Json candidate;
        std::weak_ptr<rtc::WebSocket> socket;
        std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
    };
    std::deque<EarlyCandidate> early_candidates;
    std::atomic<bool> closing{false};
    std::atomic<bool> signaling_ready{false};
    std::atomic<bool> signaling_connecting{false};
    std::chrono::steady_clock::time_point heartbeat{};
    std::chrono::steady_clock::time_point registration_started{};
    std::chrono::steady_clock::time_point next_reconnect{};
    std::chrono::steady_clock::time_point next_friend_probe{};
    std::chrono::steady_clock::time_point next_state_broadcast{};
    std::chrono::steady_clock::time_point next_invite_refresh{};
#endif

    void generate_identity() {
        identity = secure::generate_key_pair();
        local_identity = identity_of(identity.public_key);
    }

    void save_locked() { ++save_revision; }

    void flush_save() {
        std::unique_lock lock(mutex);
        if (store_path.empty() || saved_revision == save_revision) return;
        const auto revision = save_revision;
        const auto destination = store_path;
        const auto parent = directory;
        const bool recovering = preserve_recovery_backup;
        const bool migrating = preserve_legacy_store;
        const auto legacy_source = recovering
            ? std::filesystem::path(destination.string() + ".bak") : destination;
        std::ostringstream output;
        output << "schema=6\n";
        output << "identity_secret=" << secure::encode_key(identity.secret) << '\n';
        output << "display_name=" << hex_encode_text(display_name) << '\n';
        output << "appear_offline=" << (appear_offline ? 1 : 0) << '\n';
        output << "allow_lobby_invites=" << (allow_lobby_invites ? 1 : 0) << '\n';
        output << "next_id=" << next_id << '\n';
        for (const auto& [key, value] : friends) {
            output << "friend=" << secure::encode_key(value.public_key) << ','
                   << hex_encode_text(value.name) << ','
                   << hex_encode_text(value.nickname) << ','
                   << (value.blocked ? 1 : 0) << ',' << value.last_seen << '\n';
        }
        for (const auto& [id, value] : invites) {
            output << "invite=" << id << ',' << hex_encode(value.capability) << ','
                   << static_cast<int>(value.lifetime) << ',' << value.expires << ','
                   << (value.revoked ? 1 : 0) << ','
                   << (value.token.has_value() ? hex_encode(*value.token) : "")
                   << '\n';
        }
        for (const auto& [id, value] : requests) {
            output << "request=" << id << ','
                   << secure::encode_key(value.public_key) << ','
                   << value.expected_identity << ','
                   << hex_encode_text(value.name) << ','
                   << (value.incoming ? 1 : 0) << ','
                   << hex_encode(value.capability) << ',' << (value.cancelled ? 1 : 0) << ','
                   << value.wire_id << ',' << value.received << ',' << value.receipt_supported << ','
                   << value.legacy_retry << '\n';
        }
        for (const auto& [id, value] : decisions) {
            output << "decision=" << id << ',' << secure::encode_key(value.public_key) << ','
                   << hex_encode(value.capability) << ',' << value.kind << ','
                   << (value.acknowledged ? 1 : 0) << ',' << value.wire_id << '\n';
        }
        auto bytes = output.str();
        std::array<std::uint8_t, 32U> checksum{};
        crypto_blake2b(checksum.data(), checksum.size(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        bytes += "checksum=" + hex_encode(checksum) + "\n";
        lock.unlock();
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        const auto temporary = std::filesystem::path(destination.string() + ".tmp");
        bool success = !error;
        if (success && migrating && std::filesystem::exists(legacy_source, error)) {
            const auto legacy_backup = std::filesystem::path(destination.string() + ".pre-receipts");
            if (!std::filesystem::exists(legacy_backup, error))
                std::filesystem::copy_file(legacy_source, legacy_backup, error);
            success = !error;
        }
        if (success) {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            file.flush();
            success = static_cast<bool>(file);
            file.close();
            success = success && !file.fail();
        }
        if (success && std::filesystem::exists(destination, error)) {
            std::filesystem::copy_file(destination,
                std::filesystem::path(destination.string() + (recovering ? ".rejected" : ".bak")),
                std::filesystem::copy_options::overwrite_existing, error);
            success = !error;
        }
        if (success) success = replace_file(temporary, destination);
        lock.lock();
        save_failed = !success;
        if (success) { saved_revision = revision; preserve_recovery_backup = false; preserve_legacy_store = false; }
    }

    bool load_file(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 16 * 1024 * 1024) return false;
        std::string file_bytes(static_cast<std::size_t>(file.tellg()), '\0');
        file.seekg(0);
        file.read(file_bytes.data(), static_cast<std::streamsize>(file_bytes.size()));
        if (!file) return false;
        if (file_bytes.starts_with("schema=5\n") || file_bytes.starts_with("schema=6\n")) {
            const auto checksum_start = file_bytes.rfind("\nchecksum=");
            if (checksum_start == std::string::npos) return false;
            std::array<std::uint8_t, 32U> expected{}, actual{};
            const auto checksum_text = std::string_view(file_bytes).substr(checksum_start + 10U);
            if (checksum_text.size() != 65U || checksum_text.back() != '\n' ||
                !hex_decode(checksum_text.substr(0, 64), expected)) return false;
            crypto_blake2b(actual.data(), actual.size(), reinterpret_cast<const std::uint8_t*>(file_bytes.data()), checksum_start + 1U);
            if (actual != expected) return false;
        }
        std::istringstream input(file_bytes);
        secure::Key loaded_secret{};
        bool have_identity = false;
        std::map<std::string, Friend> loaded_friends;
        std::map<std::uint64_t, Invite> loaded_invites;
        std::map<std::uint64_t, Request> loaded_requests;
        std::map<std::uint64_t, Decision> loaded_decisions;
        std::string loaded_name = "Racer";
        bool loaded_appear_offline = false;
        bool loaded_allow_lobby_invites = true;
        std::uint64_t loaded_next_id = 1U;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos) { if (line.empty()) continue; return false; }
            const std::string key = line.substr(0U, separator);
            const std::string value = line.substr(separator + 1U);
            if (key == "schema") {
                if (value != "1" && value != "2" && value != "3" && value != "4" && value != "5" && value != "6") return false;
                if (value == "5" && !file_bytes.starts_with("schema=5\n")) return false;
                if (value == "6" && !file_bytes.starts_with("schema=6\n")) return false;
            } else if (key == "identity_secret") {
                have_identity = secure::decode_key(value, loaded_secret);
            } else if (key == "display_name") {
                if (!hex_decode_text(value, loaded_name)) return false;
            } else if (key == "appear_offline") {
                loaded_appear_offline = value == "1";
            } else if (key == "allow_lobby_invites") {
                loaded_allow_lobby_invites = value != "0";
            } else if (key == "next_id") {
                try { loaded_next_id = std::stoull(value); } catch (...) { return false; }
            } else if (key == "friend") {
                const auto fields = split(value, ',');
                if (fields.size() != 4U && fields.size() != 5U) return false;
                Friend record{};
                std::string name;
                if (!secure::decode_key(fields[0], record.public_key) ||
                    !hex_decode_text(fields[1], name)) return false;
                record.name = clean_display_name(name);
                const bool has_nickname = fields.size() == 5U;
                if (has_nickname) {
                    std::string nickname;
                    if (!hex_decode_text(fields[2], nickname)) return false;
                    record.nickname = clean_display_name(nickname);
                }
                try {
                    record.blocked = std::stoi(fields[has_nickname ? 3U : 2U]) != 0;
                    record.last_seen = std::stoull(fields[has_nickname ? 4U : 3U]);
                } catch (...) { return false; }
                if (!loaded_friends.emplace(identity_of(record.public_key), std::move(record)).second) return false;
            } else if (key == "invite") {
                const auto fields = split(value, ',');
                if (fields.size() != 5U && fields.size() != 6U) return false;
                Invite record{};
                try {
                    record.id = std::stoull(fields[0]);
                    if (!hex_decode(fields[1], record.capability)) return false;
                    const int lifetime = std::stoi(fields[2]);
                    if (lifetime < 0 || lifetime > 2) return false;
                    record.lifetime = static_cast<FriendInviteLifetime>(lifetime);
                    record.expires = std::stoull(fields[3]);
                    record.revoked = std::stoi(fields[4]) != 0;
                    if (fields.size() == 6U && !fields[5].empty()) {
                        InviteToken token{};
                        if (!hex_decode(fields[5], token) ||
                            !valid_invite_token(token)) return false;
                        record.token = token;
                    }
                } catch (...) { return false; }
                if (!loaded_invites.emplace(record.id, record).second) return false;
            } else if (key == "decision") {
                const auto fields = split(value, ',');
                if (fields.size() != 5U && fields.size() != 6U) return false;
                Decision record{};
                try { record.id = std::stoull(fields[0]); } catch (...) { return false; }
                if (!record.id || !secure::decode_key(fields[1], record.public_key) ||
                    !hex_decode(fields[2], record.capability)) return false;
                record.kind = fields[3];
                if (record.kind != "accepted" && record.kind != "rejected" && record.kind != "blocked" && record.kind != "cancelled") return false;
                record.acknowledged = fields[4] == "1";
                if (fields.size() == 6U) {
                    try { record.wire_id = std::stoull(fields[5]); } catch (...) { return false; }
                }
                if (!loaded_decisions.emplace(record.id, record).second) return false;
            } else if (key == "request") {
                const auto fields = split(value, ',');
                if (fields.size() != 5U && fields.size() != 6U && fields.size() != 7U && fields.size() != 11U) return false;
                Request record{};
                try {
                    record.id = std::stoull(fields[0]);
                    const bool modern = fields.size() >= 6U;
                    if (!secure::decode_key(fields[1], record.public_key) ||
                        !hex_decode_text(fields[modern ? 3U : 2U], record.name) ||
                        !hex_decode(fields[modern ? 5U : 4U], record.capability)) return false;
                    record.expected_identity = modern ? fields[2U]
                        : identity_of(record.public_key);
                    record.incoming = std::stoi(fields[modern ? 4U : 3U]) != 0;
                    record.cancelled = fields.size() >= 7U && fields[6] == "1";
                    if (fields.size() == 11U) {
                        record.wire_id = std::stoull(fields[7]);
                        record.received = fields[8] == "1";
                        record.receipt_supported = fields[9] == "1";
                        record.legacy_retry = fields[10] == "1";
                    } else if (!record.incoming) {
                        record.wire_id = record.id;
                        record.legacy_retry = true;
                    }
                } catch (...) { return false; }
                if (!loaded_requests.emplace(record.id, std::move(record)).second) return false;
            }
        }
        if (!have_identity || loaded_secret == secure::Key{} || input.bad()) return false;
        // Reject overflowing/reused IDs before changing the live identity.
        constexpr auto maximum_id = UINT64_MAX - 65536U;
        if (!loaded_next_id || loaded_next_id > maximum_id) return false;
        std::set<std::uint64_t> used_ids;
        const auto validate_ids = [&](const auto& records) {
            for (const auto& [id, value] : records) {
                if (!id || id > maximum_id || !used_ids.insert(id).second) return false;
                loaded_next_id = std::max(loaded_next_id, id + 1U);
            }
            return true;
        };
        if (!validate_ids(loaded_invites) || !validate_ids(loaded_requests) || !validate_ids(loaded_decisions)) return false;
        // Older builds could persist the same destination more than once.
        // Merge identical pending requests, retaining a queued cancellation.
        std::map<std::string, Request*> destinations;
        for (auto it = loaded_requests.begin(); it != loaded_requests.end();) {
            const auto key = std::string(it->second.incoming ? "I:" : "O:") +
                it->second.expected_identity + ":" + hex_encode(it->second.capability) + ":" + std::to_string(it->second.wire_id);
            const auto [previous, inserted] = destinations.emplace(key, &it->second);
            if (inserted) ++it;
            else {
                previous->second->cancelled |= it->second.cancelled;
                it = loaded_requests.erase(it);
            }
        }
        identity.secret = loaded_secret;
        crypto_x25519_public_key(identity.public_key.data(), identity.secret.data());
        local_identity = identity_of(identity.public_key);
        display_name = clean_display_name(loaded_name);
        if (display_name.empty()) display_name = "Racer";
        appear_offline = loaded_appear_offline;
        allow_lobby_invites = loaded_allow_lobby_invites;
        friends = std::move(loaded_friends);
        invites = std::move(loaded_invites);
        requests = std::move(loaded_requests);
        decisions = std::move(loaded_decisions);
        next_id = std::max<std::uint64_t>(loaded_next_id, 1U);
        for (const auto& [id, value] : invites) next_id = std::max(next_id, id + 1U);
        for (const auto& [id, value] : requests) next_id = std::max(next_id, id + 1U);
        for (const auto& [id, value] : decisions) next_id = std::max(next_id, id + 1U);
        if (!next_id) return false;
        if (path.extension() == ".bak") preserve_recovery_backup = true;
        preserve_legacy_store = !file_bytes.starts_with("schema=6\n");
        return true;
    }

    std::optional<std::uint64_t> valid_invite_id(const Capability& capability) {
        const std::uint64_t now = unix_seconds();
        for (auto& [id, invite] : invites) {
            const bool short_match = invite.token.has_value() &&
                derive_capability(*invite.token) == capability;
            if (invite.revoked ||
                (invite.capability != capability && !short_match)) continue;
            if (invite.expires != 0U && invite.expires < now) continue;
            return id;
        }
        return std::nullopt;
    }

#if DKR_NETPLAY_WEBRTC
    std::string signaling_address() const {
        return peer_id_for(local_identity);
    }

    bool send_signal(const std::shared_ptr<rtc::WebSocket>& socket,
                     const Json& message) {
        if (socket && socket->isOpen()) {
            try { socket->send(message.dump()); return true; } catch (...) {}
        }
        return false;
    }

    void send_signal(const Json& message) {
        std::shared_ptr<rtc::WebSocket> socket;
        {
            std::scoped_lock lock(mutex);
            socket = websocket;
        }
        send_signal(socket, message);
    }

    void send_json(const std::shared_ptr<Peer>& peer, const Json& message) {
        if (!peer) return;
#if defined(DKR_FRIEND_TESTING)
        if (peer->test_send) { peer->test_send(message.dump()); return; }
#endif
        const auto weak = std::weak_ptr<Peer>(peer);
        if (!executor.post([this, weak, text = message.dump()] {
            std::shared_ptr<rtc::DataChannel> channel;
            std::shared_ptr<Peer> current;
            {
                std::scoped_lock lock(mutex);
                current = weak.lock();
                if (!current || !current_peer_locked(current)) return;
                channel = current->channel;
            }
            try {
                if (channel && channel->isOpen() && channel->bufferedAmount() < 65536U) {
                    channel->send(text); // false means buffered, not rejected by the SDK.
                    return;
                }
            } catch (...) {}
            std::scoped_lock lock(mutex);
            current->send_failed = true;
        })) callbacks_lost.store(true);
    }

    bool current_peer_locked(const std::shared_ptr<Peer>& peer) const {
        const auto found = peers.find(peer->remote_id);
        return found != peers.end() && found->second == peer && !peer->retiring;
    }

    auto find_identity_route(std::string_view identity_value) {
        return std::find_if(peers.begin(), peers.end(), [&](const auto& entry) {
            return !entry.second->retiring && entry.second->authenticated && identity_of(entry.second->remote_public) == identity_value &&
                entry.second->channel && entry.second->channel->isOpen();
        });
    }

    auto find_incoming_invite(std::string_view identity_value, std::uint64_t wire_id) {
        return std::find_if(incoming_lobby_invites.begin(), incoming_lobby_invites.end(), [&](const auto& entry) {
            return entry.second.friend_identity == identity_value && entry.second.wire_id == wire_id;
        });
    }

    static bool terminal_invite(FriendLobbyInviteStatus status) {
        return status != FriendLobbyInviteStatus::Sent && status != FriendLobbyInviteStatus::Delivered;
    }

    void send_lobby_record_locked(const std::shared_ptr<Peer>& peer, const LobbyInvite& invite) {
        if (invite.incoming) {
            if (invite.status == FriendLobbyInviteStatus::Accepted || invite.status == FriendLobbyInviteStatus::Declined)
                send_json(peer, Json{{"kind", invite.status == FriendLobbyInviteStatus::Accepted ? "lobby-invite-accepted" : "lobby-invite-declined"}, {"id", invite.wire_id}});
        } else if (invite.status == FriendLobbyInviteStatus::Cancelled) {
            send_json(peer, Json{{"kind", "lobby-invite-cancelled"}, {"id", invite.id}});
        } else if (!terminal_invite(invite.status)) {
            send_json(peer, Json{{"kind", "lobby-invite"}, {"id", invite.id},
                {"code", invite.lobby_code}, {"players", invite.players}, {"maximum", invite.maximum_players},
                {"synchronization", invite.synchronization}, {"compatibility", invite.compatibility},
                {"expires", invite.expires}, {"admission", hex_encode(invite.admission)}});
        }
    }

    void send_decisions_locked(const std::shared_ptr<Peer>& peer) {
        if (!peer->authenticated || saved_revision != save_revision) return;
        // Old unacknowledged decisions remain durable, but cannot flood the
        // bounded callback queue or starve newer requests on reconnect.
        auto next = decisions.upper_bound(peer->last_decision_sent);
        unsigned sent = 0U;
        for (std::size_t visited = 0; visited < decisions.size() && sent < 32U; ++visited) {
            if (next == decisions.end()) next = decisions.begin();
            const auto& [id, decision] = *next++;
            if (decision.public_key != peer->remote_public || decision.acknowledged) continue;
            send_json(peer, Json{{"kind", "decision"}, {"id", id}, {"action", decision.kind},
                                 {"capability", hex_encode(decision.capability)}, {"name", display_name},
                                 {"request_id", peer->request_receipts ? decision.wire_id : 0U}});
            peer->last_decision_sent = id;
            ++sent;
        }
    }

    void send_requests_locked(const std::shared_ptr<Peer>& peer) {
        if (!peer->authenticated || !peer->request_receipts || saved_revision != save_revision) return;
        for (const auto& [id, request] : requests) {
            if (request.cancelled || request.public_key != peer->remote_public || !request.wire_id) continue;
            send_json(peer, Json{{"kind", request.incoming ? "request-received" : "friend-request"},
                {"request_id", request.wire_id}, {"capability", hex_encode(request.capability)},
                {"legacy", request.legacy_retry}});
        }
    }

    bool resolve_requests_locked(const std::shared_ptr<Peer>& peer) {
        bool matched = false;
        const auto remote_identity = identity_of(peer->remote_public);
        for (auto it = requests.begin(); it != requests.end();) {
            auto& request = it->second;
            if (request.incoming || (request.expected_identity != remote_identity &&
                peer_id_for_route(request.expected_identity) != peer->remote_id)) { ++it; continue; }
            matched = true;
            if (request.public_key != peer->remote_public || request.name != peer->remote_name ||
                request.receipt_supported != peer->request_receipts) {
                request.public_key = peer->remote_public;
                request.name = peer->remote_name;
                request.expected_identity = remote_identity;
                request.receipt_supported = peer->request_receipts;
                request.problem.clear();
                save_locked();
            }
            retries.erase(remote_identity);
            if (request.cancelled) {
                const auto id = next_id++;
                decisions[id] = Decision{id, peer->remote_public, request.capability, "cancelled", false, request.wire_id};
                it = requests.erase(it);
                save_locked();
            } else ++it;
        }
        return matched;
    }

    std::shared_ptr<Peer> authenticated_peer_locked(std::string_view identity_value) const {
        for (const auto& [route, peer] : peers)
            if (!peer->retiring && peer->authenticated && identity_of(peer->remote_public) == identity_value &&
                peer->channel && peer->channel->isOpen()) return peer;
        return {};
    }

    bool accepted_peer_locked(const std::shared_ptr<Peer>& peer) const {
        if (!peer || !peer->authenticated || !current_peer_locked(peer)) return false;
        const auto found = friends.find(identity_of(peer->remote_public));
        return found != friends.end() && !found->second.blocked;
    }

    void retire_locked(const std::shared_ptr<Peer>& peer) {
        // Strong ownership survives until SDK closes/destructors finish outside
        // the state lock. SDK close callbacks enqueue rather than re-enter.
        if (!peer || peer->retiring) return;
        peer->retiring = true;
        retired_peers.push_back(peer);
    }

    Nonce make_nonce() {
        const secure::Key random = secure::generate_key();
        Nonce result{};
        std::copy_n(random.begin(), result.size(), result.begin());
        return result;
    }

    std::array<std::uint8_t, 32U> proof_for(
        const secure::Key& remote_public, const Nonce& local_nonce,
        const Nonce& remote_nonce, bool sending) const {
        std::array<std::uint8_t, 32U> shared{};
        crypto_x25519(shared.data(), identity.secret.data(), remote_public.data());
        if (shared == secure::Key{}) return {};
        std::array<std::uint8_t, 32U + 16U + 16U + 14U + 64U> material{};
        std::copy(shared.begin(), shared.end(), material.begin());
        const bool local_first = sending;
        const Nonce& first = local_first ? local_nonce : remote_nonce;
        const Nonce& second = local_first ? remote_nonce : local_nonce;
        std::copy(first.begin(), first.end(), material.begin() + 32U);
        std::copy(second.begin(), second.end(), material.begin() + 48U);
        constexpr std::array<std::uint8_t, 14U> context{
            'D','K','R','R','-','F','R','I','E','N','D','-','V','4'};
        std::copy(context.begin(), context.end(), material.begin() + 64U);
        const auto& sender = sending ? identity.public_key : remote_public;
        const auto& receiver = sending ? remote_public : identity.public_key;
        std::copy(sender.begin(), sender.end(), material.begin() + 78U);
        std::copy(receiver.begin(), receiver.end(), material.begin() + 110U);
        std::array<std::uint8_t, 32U> result{};
        crypto_blake2b(result.data(), result.size(), material.data(), material.size());
        crypto_wipe(shared.data(), shared.size());
        crypto_wipe(material.data(), material.size());
        return result;
    }

    Json visible_lobby_locked() const {
        if (appear_offline) {
            return Json{{"hosting", false}, {"code", ""}, {"players", 0U},
                        {"maximum", advertisement.maximum_players},
                        {"synchronization", ""}, {"compatibility", ""}};
        }
        return Json{{"hosting", advertisement.hosting},
                    {"code", advertisement.quick_join_code},
                    {"players", advertisement.players},
                    {"maximum", advertisement.maximum_players},
                    {"synchronization", advertisement.synchronization},
                    {"compatibility", advertisement.compatibility}};
    }

    Json state_message_locked() const {
        return Json{{"kind", "state"}, {"name", display_name},
                    {"online", !appear_offline},
                    {"lobby", visible_lobby_locked()}};
    }

    void send_hello(const std::shared_ptr<Peer>& peer) {
        Json lobby;
        std::string name;
        bool online = true;
        {
            std::scoped_lock lock(mutex);
            name = display_name;
            online = !appear_offline;
            lobby = Json::object(); // No lobby information before friend approval.
        }
        send_json(peer, Json{{"kind", "hello"},
                             {"protocol", kPresenceProtocol},
                             {"request_protocol", kRequestProtocol},
                             {"public", secure::encode_key(identity.public_key)},
                             {"name", name},
                             {"online", online},
                             {"nonce", hex_encode(peer->local_nonce)},
                             {"invite", hex_encode(peer->local_invitation)},
                             {"lobby", std::move(lobby)}});
    }

    void apply_lobby_locked(Friend& record, const Json& lobby) {
        record.hosting = lobby.value("hosting", false);
        record.lobby_code = lobby.value("code", "");
        record.players = lobby.value("players", 0U);
        record.maximum_players = lobby.value("maximum", 0U);
        if (!record.hosting || !valid_quick_join_code(record.lobby_code)) {
            record.hosting = false;
            record.lobby_code.clear();
        }
    }

    void apply_presence_locked(Friend& record, std::string_view name,
                               bool online, const Json& lobby) {
        record.name = clean_display_name(name);
        record.online = online;
        record.last_seen = unix_seconds();
        if (online) {
            apply_lobby_locked(record, lobby);
        } else {
            record.hosting = false;
            record.lobby_code.clear();
            record.players = 0U;
            record.maximum_players = 0U;
        }
    }

    void complete_authentication(const std::shared_ptr<Peer>& peer) {
        const std::string remote_identity = identity_of(peer->remote_public);
        auto friend_found = friends.find(remote_identity);
        if (friend_found != friends.end() && friend_found->second.blocked) {
            // A block always wins over historical decisions or request retries.
            retire_locked(peer);
            return;
        }
        const bool outgoing = resolve_requests_locked(peer);
        if (peer->request_receipts) {
            const bool known_request = std::any_of(requests.begin(), requests.end(), [&](const auto& entry) {
                return entry.second.public_key == peer->remote_public;
            });
            const bool known_decision = std::any_of(decisions.begin(), decisions.end(), [&](const auto& entry) {
                return entry.second.public_key == peer->remote_public;
            });
            if (friend_found == friends.end() && !outgoing && !known_request && !known_decision &&
                !valid_invite_id(peer->invitation)) {
                diagnose_locked("Friend handshake has no active invitation or existing relationship.");
                retire_locked(peer);
                return;
            }
            peer->authenticated = true;
            retries.erase(remote_identity);
            if (friend_found != friends.end()) apply_presence_locked(friend_found->second,
                peer->remote_name, peer->remote_online, peer->remote_lobby);
            diagnose_locked("Friend identity verified; request receipts negotiated.");
            return; // Inbox insertion is an explicit, retryable transaction below.
        }
        const auto decision = std::find_if(decisions.begin(), decisions.end(), [&](const auto& entry) {
            return entry.second.public_key == peer->remote_public &&
                (entry.second.capability == peer->invitation ||
                 entry.second.capability == peer->local_invitation);
        });
        if (decision != decisions.end() && friend_found == friends.end()) {
            peer->authenticated = true;
            decision->second.acknowledged = false;
            save_locked();
            return;
        }
        if (outgoing) { peer->authenticated = true; return; }
        if (friend_found == friends.end()) {
            const auto invite_id = valid_invite_id(peer->invitation);
            if (!invite_id.has_value()) {
                send_json(peer, Json{{"kind", "rejected"},
                                     {"reason", "That Friend Code is no longer active."}});
                retire_locked(peer);
                return;
            }
            const auto duplicate = std::find_if(requests.begin(), requests.end(),
                [&](const auto& entry) {
                    return entry.second.incoming &&
                           entry.second.public_key == peer->remote_public;
                });
            if (duplicate == requests.end()) {
                const std::uint64_t id = next_id++;
                requests[id] = Request{id, peer->remote_public,
                    remote_identity, peer->remote_name, true, peer->invitation};
                status = "A racer sent a friend request.";
                save_locked();
            }
            peer->authenticated = true;
            return;
        }
        Friend& record = friend_found->second;
        apply_presence_locked(record, peer->remote_name, peer->remote_online,
                              peer->remote_lobby);
        peer->authenticated = true;
    }

    void handle_message(const std::shared_ptr<Peer>& peer,
                        const std::string& text) {
        Json message;
        if (text.size() > 8192U) return;
        try { message = Json::parse(text); } catch (...) { return; }
        if (!valid_social_message(message)) return;
        const std::string kind = message.value("kind", "");
        std::scoped_lock lock(mutex);
        if (!current_peer_locked(peer)) return;
        if (kind == "hello") {
            if (peer->have_remote_hello || peer->authenticated) return;
            if (message.value("protocol", 0U) != kPresenceProtocol) {
                status = "Friend uses an incompatible social protocol. Both racers need v1.0.5 Beta 1 or newer.";
                retire_locked(peer);
                return;
            }
            secure::Key remote_public{};
            Nonce remote_nonce{};
            Capability invitation{};
            if (!secure::decode_key(message.value("public", ""), remote_public) ||
                !hex_decode(message.value("nonce", ""), remote_nonce) ||
                !hex_decode(message.value("invite", ""), invitation) ||
                remote_public == secure::Key{} || remote_public == identity.public_key ||
                (!peer->invitation_route &&
                 peer_id_for(identity_of(remote_public)) != peer->remote_id)) {
                return;
            }
            peer->remote_public = remote_public;
            peer->remote_nonce = remote_nonce;
            peer->invitation = invitation;
            peer->remote_name = clean_display_name(message.value("name", "Racer"));
            if (peer->remote_name.empty()) peer->remote_name = "Racer";
            peer->remote_online = message.value("online", true);
            peer->request_receipts = message.value("request_protocol", 0ULL) == kRequestProtocol;
            peer->remote_lobby = message.contains("lobby") && message["lobby"].is_object()
                ? message["lobby"] : Json::object();
            peer->have_remote_hello = true;
            const auto proof = proof_for(remote_public, peer->local_nonce,
                                         peer->remote_nonce, true);
            if (proof == secure::Key{}) { retire_locked(peer); return; }
            send_json(peer, Json{{"kind", "proof"},
                                 {"value", hex_encode(proof)}});
            return;
        }
        if (kind == "proof" && peer->have_remote_hello && !peer->authenticated) {
            std::array<std::uint8_t, 32U> supplied{};
            if (!hex_decode(message.value("value", ""), supplied)) return;
            const auto expected = proof_for(peer->remote_public,
                                            peer->local_nonce,
                                            peer->remote_nonce, false);
            if (expected == secure::Key{} || crypto_verify32(supplied.data(), expected.data()) != 0) return;
            complete_authentication(peer);
            send_json(peer, Json{{"kind", "state-request"}});
            return;
        }
        if (!peer->authenticated) return;
        const std::string remote_identity = identity_of(peer->remote_public);
        if (kind == "friend-request" || kind == "request-received" || kind == "request-error") {
            if (!peer->request_receipts) return;
            const auto wire_id = message.value("request_id", 0ULL);
            Capability capability{};
            if (!wire_id || !hex_decode(message.value("capability", ""), capability)) return;
            const auto blocked = friends.find(remote_identity);
            if (blocked != friends.end() && blocked->second.blocked) return;
            if (kind != "friend-request") {
                for (auto& [id, request] : requests) {
                    if (request.incoming || request.cancelled || request.wire_id != wire_id ||
                        request.public_key != peer->remote_public || request.capability != capability) continue;
                    if (kind == "request-received" && !request.received) {
                        request.received = true;
                        request.problem.clear();
                        save_locked();
                        diagnose_locked("Receiver acknowledged persisted friend request.");
                    } else if (kind == "request-error") {
                        const auto reason = message.value("reason", "");
                        request.problem = reason == "inactive-code" ? "The racer reports that this Friend Code is no longer active. Ask for a new code."
                            : "The racer cannot receive another request yet. Retrying automatically.";
                    }
                }
                return;
            }
            const auto terminal = std::find_if(decisions.begin(), decisions.end(), [&](const auto& entry) {
                const auto& decision = entry.second;
                return decision.public_key == peer->remote_public && decision.capability == capability &&
                    decision.kind != "cancelled" && (decision.wire_id == wire_id ||
                    (!decision.wire_id && message.value("legacy", false)));
            });
            if (terminal != decisions.end()) {
                if (!terminal->second.wire_id) terminal->second.wire_id = wire_id;
                terminal->second.acknowledged = false;
                save_locked();
                return;
            }
            const auto existing = std::find_if(requests.begin(), requests.end(), [&](const auto& entry) {
                return entry.second.incoming && entry.second.public_key == peer->remote_public &&
                    entry.second.capability == capability &&
                    (entry.second.wire_id == wire_id || !entry.second.wire_id);
            });
            if (existing != requests.end()) {
                if (!existing->second.wire_id) {
                    existing->second.wire_id = wire_id;
                    existing->second.receipt_supported = true;
                    save_locked();
                }
                if (saved_revision == save_revision)
                    send_json(peer, Json{{"kind", "request-received"}, {"request_id", wire_id}, {"capability", hex_encode(capability)}});
                return;
            }
            if (!valid_invite_id(capability)) {
                send_json(peer, Json{{"kind", "request-error"}, {"request_id", wire_id},
                    {"capability", hex_encode(capability)}, {"reason", "inactive-code"}});
                return;
            }
            if (requests.size() >= 256U) {
                send_json(peer, Json{{"kind", "request-error"}, {"request_id", wire_id},
                    {"capability", hex_encode(capability)}, {"reason", "inbox-busy"}});
                return;
            }
            const auto id = next_id++;
            if (blocked != friends.end()) {
                // An explicitly re-sent request can reconcile a one-sided
                // friendship; it must still possess a valid invitation.
                decisions[id] = Decision{id, peer->remote_public, capability, "accepted", false, wire_id};
            } else {
                Request request{id, peer->remote_public, remote_identity, peer->remote_name, true, capability};
                request.wire_id = wire_id;
                request.receipt_supported = true;
                requests[id] = std::move(request);
                status = "A racer sent a friend request.";
            }
            save_locked();
            diagnose_locked("Friend request received; awaiting local persistence before acknowledgement.");
            return;
        }
        if (kind == "decision-ack") {
            const auto found = decisions.find(message.value("id", 0ULL));
            if (found != decisions.end() && found->second.public_key == peer->remote_public && !found->second.acknowledged) {
                found->second.acknowledged = true;
                save_locked();
            }
            return;
        }
        if (kind == "decision") {
            const auto id = message.value("id", 0ULL);
            const auto action = message.value("action", "");
            const auto wire_id = message.value("request_id", 0ULL);
            Capability capability{};
            if (!id || !hex_decode(message.value("capability", ""), capability)) return;
            if (action != "accepted" && action != "rejected" && action != "blocked" && action != "cancelled") return;
            const auto matches = [&](const auto& entry) {
                const auto& request = entry.second;
                return request.public_key == peer->remote_public && request.capability == capability &&
                    (!peer->request_receipts || (wire_id ? request.wire_id == wire_id : request.legacy_retry)) &&
                    request.incoming == (action == "cancelled");
            };
            if (std::any_of(requests.begin(), requests.end(), matches)) {
                if (action == "accepted") {
                    Friend accepted{};
                    accepted.public_key = peer->remote_public;
                    accepted.name = peer->remote_name;
                    accepted.online = peer->remote_online;
                    accepted.last_seen = unix_seconds();
                    friends.try_emplace(remote_identity, std::move(accepted));
                }
                std::erase_if(requests, matches);
                save_locked();
                status = action == "accepted" ? "Friend request confirmed." : "Friend request closed.";
            }
            // A cancellation can cross a locally accepted, not yet delivered
            // request. Resolve that same capability to cancelled on both sides
            // instead of leaving a one-sided friendship. Never clear a block
            // or an unrelated friendship merely on receipt of a cancellation.
            if (action == "cancelled") {
                bool crossed_acceptance = false;
                for (auto& [decision_id, decision] : decisions) {
                    if (decision.public_key == peer->remote_public &&
                        decision.capability == capability && decision.kind == "accepted" &&
                        (!peer->request_receipts || (wire_id && decision.wire_id == wire_id))) {
                        decision.kind = "rejected";
                        decision.acknowledged = false;
                        crossed_acceptance = true;
                    }
                }
                if (crossed_acceptance) {
                    const auto found = friends.find(remote_identity);
                    if (found != friends.end() && !found->second.blocked) friends.erase(found);
                    save_locked();
                    status = "The racer cancelled this friend request before confirmation.";
                }
                if (peer->request_receipts && wire_id && valid_invite_id(capability) &&
                    decisions.size() < 4096U && std::none_of(decisions.begin(), decisions.end(), [&](const auto& entry) {
                        return entry.second.public_key == peer->remote_public && entry.second.capability == capability &&
                            entry.second.wire_id == wire_id && entry.second.kind == "rejected";
                    })) {
                    const auto cancellation_id = next_id++;
                    decisions[cancellation_id] = Decision{cancellation_id, peer->remote_public, capability, "rejected", false, wire_id};
                    save_locked(); // A late replay must not resurrect a cancelled request.
                }
            }
            // ACK only after local state has reached durable storage. A replay
            // after the flush follows this same idempotent path and is ACKed.
            if (saved_revision == save_revision)
                send_json(peer, Json{{"kind", "decision-ack"}, {"id", id}});
            return;
        }
        if (kind == "state" || kind == "state-request") {
            if (kind == "state") {
                auto found = friends.find(remote_identity);
                if (found != friends.end() && !found->second.blocked) {
                    const Json lobby = message.contains("lobby") &&
                            message["lobby"].is_object()
                        ? message["lobby"] : Json::object();
                    peer->remote_online = message.value("online", true);
                    apply_presence_locked(
                        found->second,
                        message.value("name", found->second.name),
                        peer->remote_online, lobby);
                }
            }
            if (kind == "state-request" && accepted_peer_locked(peer)) send_json(peer, state_message_locked());
        } else if (kind == "lobby-invite") {
            const auto friend_found = friends.find(remote_identity);
            secure::Key admission{};
            const std::uint64_t id = message.value("id", 0ULL);
            if (friend_found != friends.end() && !friend_found->second.blocked &&
                id != 0U && (appear_offline || !allow_lobby_invites)) {
                send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                     {"id", id}});
                return;
            }
            const std::uint64_t expires = message.value("expires", 0ULL);
            const std::string code = message.value("code", "");
            const std::uint32_t players = message.value("players", 0U);
            const std::uint32_t maximum = message.value("maximum", 0U);
            const std::uint64_t now = unix_seconds();
            if (friend_found == friends.end() || friend_found->second.blocked ||
                id == 0U || !valid_quick_join_code(code) ||
                !hex_decode(message.value("admission", ""), admission) ||
                admission == secure::Key{} || expires <= now ||
                expires > now + kLobbyInviteLifetimeSeconds + 30U ||
                maximum < 2U || maximum > kSupportedOnlinePlayers || players >= maximum) {
                return;
            }
            const auto existing_invite = find_incoming_invite(remote_identity, id);
            if (existing_invite != incoming_lobby_invites.end()) {
                if (terminal_invite(existing_invite->second.status)) send_lobby_record_locked(peer, existing_invite->second);
                else send_json(peer, Json{{"kind", "lobby-invite-delivered"}, {"id", id}});
                return;
            }
            std::erase_if(incoming_lobby_invites, [&](const auto& entry) { return entry.second.expires <= now; });
            if (incoming_lobby_invites.size() >= kMaximumLobbyInvites) {
                status = "Lobby invitation inbox is full. Existing invitations were kept; ask the friend to retry later.";
                send_json(peer, Json{{"kind", "lobby-invite-declined"}, {"id", id}});
                return;
            }
            LobbyInvite invite{};
            invite.id = next_id++;
            invite.wire_id = id;
            invite.friend_identity = remote_identity;
            invite.friend_name = friend_found->second.nickname.empty()
                ? friend_found->second.name : friend_found->second.nickname;
            invite.lobby_code = code;
            invite.synchronization = message.value("synchronization", "");
            invite.compatibility = message.value("compatibility", "");
            invite.players = players;
            invite.maximum_players = maximum;
            invite.expires = expires;
            invite.admission = admission;
            invite.status = FriendLobbyInviteStatus::Delivered;
            invite.incoming = true;
            incoming_lobby_invites[invite.id] = std::move(invite);
            save_locked();
            send_json(peer, Json{{"kind", "lobby-invite-delivered"},
                                 {"id", id}});
            status = friend_found->second.name + " invited you to race.";
        } else if (kind == "lobby-invite-delivered" ||
                   kind == "lobby-invite-accepted" ||
                   kind == "lobby-invite-declined") {
            const std::uint64_t id = message.value("id", 0ULL);
            const auto found = outgoing_lobby_invites.find(id);
            if (found == outgoing_lobby_invites.end() ||
                found->second.friend_identity != remote_identity) return;
            if (kind != "lobby-invite-delivered")
                send_json(peer, Json{{"kind", "lobby-invite-decision-ack"}, {"id", id}});
            if (terminal_invite(found->second.status)) return;
            found->second.status = kind == "lobby-invite-delivered"
                ? FriendLobbyInviteStatus::Delivered
                : kind == "lobby-invite-accepted"
                    ? FriendLobbyInviteStatus::Accepted
                    : FriendLobbyInviteStatus::Declined;
        } else if (kind == "lobby-invite-decision-ack") {
            const auto found = find_incoming_invite(remote_identity, message.value("id", 0ULL));
            if (found != incoming_lobby_invites.end()) found->second.decision_acknowledged = true;
        } else if (kind == "lobby-invite-cancelled") {
            const std::uint64_t id = message.value("id", 0ULL);
            const auto found = find_incoming_invite(remote_identity, id);
            if (found != incoming_lobby_invites.end() &&
                !terminal_invite(found->second.status)) {
                found->second.status = FriendLobbyInviteStatus::Cancelled;
                found->second.admission = {};
            }
        } else if (kind == "accepted") {
            const auto request = std::find_if(requests.begin(), requests.end(),
                [&](const auto& entry) {
                    return !entry.second.incoming &&
                           entry.second.expected_identity == remote_identity;
                });
            if (request != requests.end()) {
                Friend accepted{};
                accepted.public_key = peer->remote_public;
                accepted.name = clean_display_name(
                    message.value("name", "Racer"));
                accepted.last_seen = unix_seconds();
                accepted.online = peer->remote_online;
                friends[remote_identity] = std::move(accepted);
                requests.erase(request);
                status = "Friend added. Their hosted lobby will appear automatically.";
                save_locked();
            }
        } else if (kind == "removed") {
            friends.erase(remote_identity);
            status = "A remote friendship was removed.";
            save_locked();
        } else if (kind == "rejected" || kind == "blocked") {
            for (auto iterator = requests.begin(); iterator != requests.end();) {
                if (!iterator->second.incoming &&
                    iterator->second.expected_identity == remote_identity) {
                    iterator = requests.erase(iterator);
                } else {
                    ++iterator;
                }
            }
            status = kind == "blocked" ? "That racer blocked the request."
                                        : message.value("reason", "Friend request declined.");
            save_locked();
        }
    }

    void send_state(const std::shared_ptr<Peer>& peer) {
        Json message;
        {
            std::scoped_lock lock(mutex);
            if (!accepted_peer_locked(peer)) return;
            message = state_message_locked();
        }
        send_json(peer, message);
    }

    void bind_channel(const std::shared_ptr<Peer>& peer,
                      std::shared_ptr<rtc::DataChannel> channel) {
        if (!peer || !channel || channel->label() != "dkr-r-friends") return;
        {
            std::scoped_lock lock(mutex);
            if (!current_peer_locked(peer)) return;
            peer->channel = std::move(channel);
        }
        const std::weak_ptr<Peer> weak(peer);
        peer->channel->onOpen(callback([this, weak] {
            if (const auto locked = weak.lock()) {
                locked->connected = std::chrono::steady_clock::now();
                send_hello(locked);
            }
        }));
        peer->channel->onMessage(callback([this, weak](rtc::message_variant message) {
            const auto locked = weak.lock();
            if (!locked || !std::holds_alternative<std::string>(message)) return;
            handle_message(locked, std::get<std::string>(message));
        }));
        peer->channel->onClosed(callback([this, weak] {
            const auto locked = weak.lock();
            if (!locked) return;
            std::scoped_lock lock(mutex);
            // create_peer installs a replacement before retiring the old
            // route. The old channel's close callback must not take the
            // replacement's authenticated presence offline.
            const auto current = peers.find(locked->remote_id);
            if (current == peers.end() || current->second != locked) return;

            if (locked->remote_public == secure::Key{}) return;
            const bool alternate_route_online = std::any_of(
                peers.begin(), peers.end(), [&](const auto& entry) {
                    const auto& candidate = entry.second;
                    return candidate && candidate != locked &&
                           candidate->authenticated &&
                           candidate->remote_public == locked->remote_public &&
                           candidate->channel && candidate->channel->isOpen();
                });
            if (alternate_route_online) return;

            const auto found = friends.find(identity_of(locked->remote_public));
            if (found != friends.end()) {
                found->second.online = false;
                found->second.hosting = false;
                found->second.lobby_code.clear();
            }
        }));
    }

    std::shared_ptr<Peer> create_peer(std::string remote_id,
                                      std::string connection_id,
                                      Capability invitation = {},
                                      std::shared_ptr<rtc::WebSocket>
                                          signaling_socket = {}) {
        {
            std::scoped_lock lock(mutex);
            const auto pending = std::count_if(peers.begin(), peers.end(), [](const auto& entry) {
                return !entry.second->authenticated;
            });
            if (remote_id.size() > 128U || connection_id.size() > 128U ||
                (peers.size() >= 64U && !peers.contains(remote_id)) || pending >= 8) return {};
        }
        auto peer = std::make_shared<Peer>();
        peer->remote_id = std::move(remote_id);
        peer->connection_id = std::move(connection_id);
        peer->local_nonce = make_nonce();
        peer->local_invitation = invitation;
        peer->invitation_route =
            peer->remote_id.starts_with("dkrr-invite-3-");
        if (!signaling_socket) {
            std::scoped_lock lock(mutex);
            signaling_socket = websocket;
        }
        peer->signaling_socket = std::move(signaling_socket);
        rtc::Configuration configuration;
#if !defined(DKR_FRIEND_TESTING)
        configuration.iceServers.emplace_back("stun:stun.l.google.com:19302");
#endif
        peer->connection = std::make_shared<rtc::PeerConnection>(configuration);
        const std::weak_ptr<Peer> weak(peer);
        peer->connection->onLocalDescription(
            callback([this, weak](rtc::Description description) {
                const auto locked = weak.lock();
                if (!locked) return;
                { std::scoped_lock lock(mutex); if (!current_peer_locked(locked)) return; }
                const std::string type = description.typeString();
                Json payload{{"connectionId", locked->connection_id},
                             {"type", "data"},
                             {"sdp", {{"sdp", std::string(description)},
                                      {"type", type}}}};
                if (type == "offer") {
                    payload["label"] = locked->connection_id;
                    payload["reliable"] = true;
                    payload["serialization"] = "json";
                }
                if (!send_signal(locked->signaling_socket,
                    Json{{"type", type == "offer" ? "OFFER" : "ANSWER"},
                         {"dst", locked->remote_id},
                         {"payload", std::move(payload)}})) {
                    std::scoped_lock lock(mutex);
                    locked->send_failed = true;
                }
            }));
        peer->connection->onLocalCandidate(
            callback([this, weak](rtc::Candidate candidate) {
                const auto locked = weak.lock();
                if (!locked) return;
                { std::scoped_lock lock(mutex); if (!current_peer_locked(locked)) return; }
                if (!send_signal(locked->signaling_socket, Json{{"type", "CANDIDATE"},
                    {"dst", locked->remote_id},
                    {"payload", {{"connectionId", locked->connection_id},
                     {"type", "data"}, {"candidate", {
                        {"candidate", candidate.candidate()},
                        {"sdpMid", candidate.mid()}}}}}})) {
                    std::scoped_lock lock(mutex);
                    if (!locked->authenticated) locked->send_failed = true;
                }
            }));
        peer->connection->onDataChannel(
            callback([this, weak](std::shared_ptr<rtc::DataChannel> channel) {
                if (const auto locked = weak.lock()) {
                    bind_channel(locked, std::move(channel));
                }
            }));
        peer->connection->onStateChange(callback([this, weak](rtc::PeerConnection::State state) {
            const auto locked = weak.lock();
            if (!locked) return;
            std::scoped_lock lock(mutex);
            if (!current_peer_locked(locked)) return;
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) {
                diagnose_locked("Direct friend connection failed or closed; durable requests retained.");
                locked->send_failed = true;
            }
        }));
        std::shared_ptr<Peer> previous;
        {
            std::scoped_lock lock(mutex);
            const auto old = peers.find(peer->remote_id);
            if (old != peers.end()) previous = old->second;
            peers[peer->remote_id] = peer;
        }
        // Closing a replaced WebRTC route can synchronously invoke callbacks
        // which re-enter the service. Never do that while holding its state
        // mutex.
        if (previous && previous->channel) previous->channel->close();
        if (previous && previous->connection) previous->connection->close();
        return peer;
    }

    void create_offer(std::string_view identity_value,
                      Capability invitation = {}) {
        const std::string remote = peer_id_for_route(identity_value);
        {
            std::scoped_lock lock(mutex);
            const auto existing = peers.find(remote);
            if (existing != peers.end() &&
                ((existing->second->channel && existing->second->channel->isOpen()) ||
                 std::chrono::steady_clock::now() - existing->second->created < std::chrono::seconds(20))) return;
            if (authenticated_peer_locked(identity_value)) return;
        }
        const auto peer = create_peer(remote, "dkrr-friend-" + random_token(16U),
                                      invitation);
        if (!peer) return;
        peer->initiator = true;
        bind_channel(peer, peer->connection->createDataChannel("dkr-r-friends"));
    }

    void handle_signal(std::string_view text,
                       std::shared_ptr<rtc::WebSocket> signaling_socket = {}) {
        Json message;
        if (text.size() > 65536U) return;
        try { message = Json::parse(text); } catch (...) { return; }
        if (!valid_signaling_message(message)) return;
        const std::string type = message.value("type", "");
        if (type == "OPEN") {
            std::scoped_lock lock(mutex);
            if (!signaling_socket || signaling_socket == websocket) {
                signaling_ready.store(true);
                signaling_connecting.store(false);
                status = appear_offline ? "Appearing offline to friends." : "Friend presence online.";
            } else {
                for (const auto& [id, socket] : invite_websockets) {
                    if (socket != signaling_socket) continue;
                    auto& connection = invite_connections[id];
                    connection.registered = true;
                    connection.failures = 0;
                    connection.heartbeat = std::chrono::steady_clock::now();
                    connection.status = "Incoming friend requests ready.";
                    diagnose_locked("Incoming friend-code registration confirmed.");
                    break;
                }
            }
            return;
        }
        if (type == "EXPIRE" || type == "LEAVE" || type == "ERROR") {
            std::scoped_lock lock(mutex);
            const auto found = peers.find(message.value("src", ""));
            if (found != peers.end() && !found->second->authenticated) retire_locked(found->second);
            status = "A friend is unreachable. Requests remain queued and retry automatically.";
            return;
        }
        if (type == "ID-TAKEN") {
            std::scoped_lock lock(mutex);
            if (signaling_socket && signaling_socket != websocket) {
                status = "A short Friend Code route is already active. Generate a new code.";
                for (const auto& [id, socket] : invite_websockets) {
                    if (socket != signaling_socket) continue;
                    auto& connection = invite_connections[id];
                    connection.registered = false;
                    connection.status = "Code registration conflict. Close duplicate copies or generate a new code.";
                    connection.retry = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
                diagnose_locked("Incoming friend-code registration conflict.");
                return;
            }
            status = "This Online Profile is already active on another DKR-R instance.";
            signaling_ready.store(false, std::memory_order_release);
            return;
        }
        if (type != "OFFER" && type != "ANSWER" && type != "CANDIDATE") return;
        const std::string remote = message.value("src", "");
        if (remote.empty() || remote.size() > 128U || !message.contains("payload") || !message["payload"].is_object()) return;
        const Json& payload = message["payload"];
        const std::string connection_id = payload.value("connectionId", "");
        if (connection_id.empty()) return;
        std::shared_ptr<Peer> peer;
        {
            std::scoped_lock lock(mutex);
            const auto found = peers.find(remote);
            if (found != peers.end() &&
                found->second->connection_id == connection_id) peer = found->second;
            if (!peer && type == "OFFER" && found != peers.end() && found->second->initiator &&
                !found->second->authenticated && peer_id_for(local_identity) < remote) return;
        }
        if (!peer && type == "OFFER") {
            peer = create_peer(remote, connection_id, {},
                               std::move(signaling_socket));
        }
        if (type == "CANDIDATE" && (!peer || !peer->remote_description_set)) {
            if (!payload.contains("candidate")) return;
            std::scoped_lock lock(mutex);
            std::erase_if(early_candidates, [](const auto& item) {
                return std::chrono::steady_clock::now() - item.created > std::chrono::seconds(20);
            });
            if (early_candidates.size() < 128U && std::count_if(early_candidates.begin(), early_candidates.end(), [&](const auto& item) {
                return item.remote == remote && item.connection_id == connection_id;
            }) < 16) early_candidates.push_back({remote, connection_id, payload["candidate"], signaling_socket ? signaling_socket : websocket});
            return;
        }
        if (!peer) return;
        try {
            if ((type == "OFFER" || type == "ANSWER") && payload.contains("sdp")) {
                const Json& sdp = payload["sdp"];
                peer->connection->setRemoteDescription(rtc::Description(
                    sdp.value("sdp", ""), sdp.value("type", "")));
                peer->remote_description_set = true;
                std::deque<EarlyCandidate> pending;
                {
                    std::scoped_lock lock(mutex);
                    for (auto it = early_candidates.begin(); it != early_candidates.end();) {
                        if (it->remote == remote && it->connection_id == connection_id &&
                            it->socket.lock() == peer->signaling_socket) {
                            pending.push_back(std::move(*it));
                            it = early_candidates.erase(it);
                        } else ++it;
                    }
                }
                for (const auto& item : pending) {
                    if (std::chrono::steady_clock::now() - item.created <= std::chrono::seconds(20))
                        peer->connection->addRemoteCandidate(rtc::Candidate(item.candidate.value("candidate", ""), item.candidate.value("sdpMid", "")));
                }
            } else if (type == "CANDIDATE" && payload.contains("candidate")) {
                const Json& candidate = payload["candidate"];
                peer->connection->addRemoteCandidate(rtc::Candidate(
                    candidate.value("candidate", ""),
                    candidate.value("sdpMid", "")));
            }
        } catch (...) {
            std::scoped_lock lock(mutex);
            peer->send_failed = true;
            diagnose_locked("Friend connection negotiation failed; retry scheduled.");
        }
    }

    void open_signaling() {
        bool expected = false;
        if (!signaling_connecting.compare_exchange_strong(expected, true)) return;
        registration_started = std::chrono::steady_clock::now();
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto replacement = std::make_shared<rtc::WebSocket>(configuration);
        const std::weak_ptr<rtc::WebSocket> weak_socket(replacement);
        replacement->onOpen(callback([this, weak_socket] {
            std::scoped_lock lock(mutex);
            if (weak_socket.lock() != websocket) return;
            status = "Registering friend presence...";
        }));
        replacement->onClosed(callback([this, weak_socket] {
            if (weak_socket.lock() != websocket) return;
            signaling_ready.store(false, std::memory_order_release);
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(2);
        }));
        replacement->onError(callback([this, weak_socket](std::string) {
            if (weak_socket.lock() != websocket) return;
            signaling_ready.store(false, std::memory_order_release);
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(3);
        }));
        replacement->onMessage(callback([this, weak_socket](rtc::message_variant message) {
            if (weak_socket.lock() != websocket) return;
            if (std::holds_alternative<std::string>(message)) {
                handle_signal(std::get<std::string>(message));
            }
        }));
        std::shared_ptr<rtc::WebSocket> previous;
        {
            std::scoped_lock lock(mutex);
            previous = std::move(websocket);
            websocket = replacement;
        }
        if (previous) previous->close();
        try {
            replacement->open(signaling_url(signaling_address()));
            heartbeat = std::chrono::steady_clock::now();
        } catch (...) {
            signaling_connecting.store(false, std::memory_order_release);
            next_reconnect = std::chrono::steady_clock::now() +
                             std::chrono::seconds(3);
        }
    }

    void open_invite_signaling(std::uint64_t invite_id,
                               const InviteToken& token) {
        {
            std::scoped_lock lock(mutex);
            if (invite_websockets.contains(invite_id)) return;
            auto& connection = invite_connections[invite_id];
            const auto now = std::chrono::steady_clock::now();
            if (now < connection.retry) return;
            connection.registered = false;
            connection.started = now;
            connection.status = "Registering incoming friend requests...";
        }
        rtc::WebSocket::Configuration configuration{};
        configuration.caCertificatePemFile = ca_bundle();
        configuration.connectionTimeout = std::chrono::seconds(15);
        auto route = std::make_shared<rtc::WebSocket>(configuration);
        const std::weak_ptr<rtc::WebSocket> weak(route);
        route->onClosed(callback([this, invite_id, weak] {
            const auto closed = weak.lock();
            std::scoped_lock lock(mutex);
            const auto found = invite_websockets.find(invite_id);
            if (found != invite_websockets.end() && found->second == closed) {
                invite_websockets.erase(found);
                invite_disconnected_locked(invite_id);
            }
        }));
        route->onError(callback([this, invite_id, weak](std::string) {
            const auto failed = weak.lock();
            std::scoped_lock lock(mutex);
            const auto found = invite_websockets.find(invite_id);
            if (found != invite_websockets.end() && found->second == failed) {
                invite_websockets.erase(found);
                invite_disconnected_locked(invite_id);
            }
        }));
        route->onMessage(callback([this, weak, invite_id](rtc::message_variant message) {
            const auto socket = weak.lock();
            {
                std::scoped_lock lock(mutex);
                const auto current = invite_websockets.find(invite_id);
                if (current == invite_websockets.end() || current->second != socket) return;
            }
            if (socket && std::holds_alternative<std::string>(message)) {
                handle_signal(std::get<std::string>(message), socket);
            }
        }));
        {
            std::scoped_lock lock(mutex);
            invite_websockets[invite_id] = route;
        }
        try {
            route->open(signaling_url(peer_id_for_invite(token)));
        } catch (...) {
            std::scoped_lock lock(mutex);
            invite_websockets.erase(invite_id);
            invite_disconnected_locked(invite_id);
        }
    }

    void invite_disconnected_locked(std::uint64_t id) {
        auto& connection = invite_connections[id];
        connection.registered = false;
        const auto now = std::chrono::steady_clock::now();
        if (connection.retry <= now) {
            connection.retry = now + std::chrono::seconds(std::min(30U, 2U << std::min(connection.failures++, 4U)));
            connection.status = "Incoming requests reconnecting; your code is unchanged.";
        }
        diagnose_locked("Incoming friend-code route disconnected; retry scheduled.");
    }

    void refresh_invite_signaling() {
        std::vector<std::pair<std::uint64_t, InviteToken>> active;
        std::vector<std::shared_ptr<rtc::WebSocket>> obsolete;
        std::vector<std::shared_ptr<rtc::WebSocket>> heartbeats;
        const auto clock_now = std::chrono::steady_clock::now();
        const std::uint64_t now = unix_seconds();
        {
            std::scoped_lock lock(mutex);
            std::set<std::uint64_t> active_ids;
            for (const auto& [id, invite] : invites) {
                if (!invite.revoked && invite.token.has_value() &&
                    (invite.expires == 0U || invite.expires >= now)) {
                    active.emplace_back(id, *invite.token);
                    active_ids.insert(id);
                }
            }
            for (auto iterator = invite_websockets.begin();
                 iterator != invite_websockets.end();) {
                if (!active_ids.contains(iterator->first)) {
                    invite_connections.erase(iterator->first);
                    obsolete.push_back(iterator->second);
                    iterator = invite_websockets.erase(iterator);
                } else {
                    auto& connection = invite_connections[iterator->first];
                    if ((!connection.registered && clock_now - connection.started >= std::chrono::seconds(15)) ||
                        (connection.registered && !iterator->second->isOpen())) {
                        invite_disconnected_locked(iterator->first);
                        obsolete.push_back(iterator->second);
                        iterator = invite_websockets.erase(iterator);
                    } else {
                        if (connection.registered && clock_now - connection.heartbeat >= std::chrono::seconds(5)) {
                            heartbeats.push_back(iterator->second);
                            connection.heartbeat = clock_now;
                        }
                        ++iterator;
                    }
                }
            }
        }
        for (const auto& socket : obsolete) {
            if (socket) socket->close();
        }
        for (const auto& socket : heartbeats) send_signal(socket, Json{{"type", "HEARTBEAT"}});
        for (const auto& [id, token] : active) {
            open_invite_signaling(id, token);
        }
    }
#endif
};

std::vector<FriendInviteView> FriendService::Impl::copy_invitations_locked() const {
    std::vector<FriendInviteView> result;
    for (const auto& [id, invite] : this->invites) {
        if (invite.revoked ||
            (invite.expires != 0U && invite.expires < unix_seconds())) continue;
        ParsedFriendCode code{fingerprint(this->identity.public_key),
                              invite.capability, invite.token, invite.expires,
                              invite.lifetime};
        std::string connection_status = "Registering incoming friend requests...";
#if DKR_NETPLAY_WEBRTC
        if (const auto connection = invite_connections.find(id); connection != invite_connections.end())
            connection_status = connection->second.status;
#else
        connection_status = "Friend connections are unavailable in this build.";
#endif
        result.push_back({id, encode_friend_code(code), invite.lifetime,
                          invite.expires, std::move(connection_status)});
    }
    return result;
}

std::vector<FriendRequestView> FriendService::Impl::copy_pending_requests_locked() const {
    std::vector<FriendRequestView> result;
    for (const auto& [id, request] : this->requests) {
        // Cancelled outgoing entries are background reconciliation records,
        // not pending user actions. Keep them durable without displaying them.
        if (!request.incoming && request.cancelled) continue;
        std::string delivery = request.incoming ? "Awaiting your response" : "Saved locally; waiting to connect.";
        if (!request.incoming) {
            if (request.received) delivery = "Delivered — receiver saved the request; awaiting their response.";
            else if (request.public_key != secure::Key{}) delivery = request.receipt_supported
                ? "Identity verified; confirming delivery."
                : "Connected to an older build; delivery is unconfirmed. Both racers should update.";
#if DKR_NETPLAY_WEBRTC
            else if (!signaling_ready.load()) delivery = "Saved locally; reconnecting to friend signalling.";
            else if (const auto peer = peers.find(peer_id_for_route(request.expected_identity)); peer != peers.end() && !peer->second->retiring)
                delivery = "Connecting to the racer; checking direct connectivity.";
#endif
            if (!request.problem.empty()) delivery = request.problem;
        }
        if (save_failed) delivery = "Cannot save friend state locally. Check available space and permissions; not confirmed.";
        else if (save_revision != saved_revision) delivery = "Saving locally...";
        result.push_back({id, request.expected_identity, request.name, request.incoming, std::move(delivery),
            !request.incoming && !request.received && std::chrono::steady_clock::now() >= request.manual_retry_after});
    }
    return result;
}

std::vector<FriendView> FriendService::Impl::copy_friends_locked(bool include_blocked) const {
    std::vector<FriendView> result;
    for (const auto& [identity, record] : this->friends) {
        if (record.blocked && !include_blocked) continue;
        result.push_back({identity, record.name, record.nickname, record.online,
                          record.blocked, record.hosting, record.lobby_code,
                          record.players, record.maximum_players,
                          record.ping_ms, record.last_seen});
    }
    std::sort(result.begin(), result.end(), [](const FriendView& left,
                                               const FriendView& right) {
        if (left.online != right.online) return left.online > right.online;
        return left.display_name < right.display_name;
    });
    return result;
}

std::vector<FriendLobbyInviteView> FriendService::Impl::copy_incoming_lobby_invites_locked() const {
    std::vector<FriendLobbyInviteView> result;
    const std::uint64_t now = unix_seconds();
    for (const auto& [id, invite] : this->incoming_lobby_invites) {
        FriendLobbyInviteStatus state = invite.status;
        if (invite.expires <= now && state != FriendLobbyInviteStatus::Accepted &&
            state != FriendLobbyInviteStatus::Declined &&
            state != FriendLobbyInviteStatus::Cancelled) {
            state = FriendLobbyInviteStatus::Expired;
        }
        result.push_back({id, invite.friend_identity, invite.friend_name,
            invite.lobby_code, invite.synchronization, invite.compatibility,
            invite.players, invite.maximum_players, invite.expires,
            invite.admission, state, true});
    }
    std::sort(result.begin(), result.end(), [](const auto& left,
                                               const auto& right) {
        return left.expires_unix > right.expires_unix;
    });
    return result;
}

std::vector<FriendLobbyInviteView> FriendService::Impl::copy_outgoing_lobby_invites_locked() const {
    std::vector<FriendLobbyInviteView> result;
    const std::uint64_t now = unix_seconds();
    for (const auto& [id, invite] : this->outgoing_lobby_invites) {
        FriendLobbyInviteStatus state = invite.status;
        if (invite.expires <= now && state != FriendLobbyInviteStatus::Accepted &&
            state != FriendLobbyInviteStatus::Declined &&
            state != FriendLobbyInviteStatus::Cancelled) {
            state = FriendLobbyInviteStatus::Expired;
        }
        result.push_back({id, invite.friend_identity, invite.friend_name,
            invite.lobby_code, invite.synchronization, invite.compatibility,
            invite.players, invite.maximum_players, invite.expires,
            invite.admission, state, false});
    }
    std::sort(result.begin(), result.end(), [](const auto& left,
                                               const auto& right) {
        return left.expires_unix > right.expires_unix;
    });
    return result;
}

void FriendService::Impl::publish_snapshot() {
    performance_trace::Scope measure(performance_trace::Region::FriendSnapshot);
    auto next = std::make_shared<FriendServiceSnapshot>();
    // Serialize publication with immediate user-action updates. Publishing an
    // older worker snapshot after Cancel would otherwise revive its card.
    std::scoped_lock lock(mutex);
    next->friends = copy_friends_locked(true);
    next->requests = copy_pending_requests_locked();
    next->invitations = copy_invitations_locked();
    next->incoming_lobby_invites = copy_incoming_lobby_invites_locked();
    next->outgoing_lobby_invites = copy_outgoing_lobby_invites_locked();
    if (*ui_snapshot.load() != *next) ui_snapshot.store(std::move(next));
}

void FriendService::Impl::publish_requests_locked() {
    auto requests = copy_pending_requests_locked();
    const auto current = ui_snapshot.load();
    if (current->requests == requests) return;
    auto next = std::make_shared<FriendServiceSnapshot>(*current);
    next->requests = std::move(requests);
    ui_snapshot.store(std::move(next));
}

std::shared_ptr<const FriendServiceSnapshot> FriendService::snapshot() const {
    return impl_->ui_snapshot.load();
}

FriendService::FriendService() : impl_(std::make_shared<Impl>()) {}
FriendService::~FriendService() { shutdown(); }

void FriendService::configure(const std::filesystem::path& config_directory,
                              std::string_view requested_name) {
    shutdown();
    // Old callbacks retain only weak ownership of the old generation.
    impl_ = std::make_shared<Impl>();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->directory = config_directory / "online-profile";
        impl_->store_path = impl_->directory / "friends-v1.ini";
        impl_->incoming_lobby_invites.clear();
        impl_->outgoing_lobby_invites.clear();
        impl_->appear_offline = false;
        impl_->allow_lobby_invites = true;
        if (!impl_->load_file(impl_->store_path) &&
            !impl_->load_file(std::filesystem::path(impl_->store_path.string() +
                                                    ".bak"))) {
            std::error_code exists_error;
            if (std::filesystem::exists(impl_->store_path, exists_error) ||
                std::filesystem::exists(impl_->store_path.string() + ".bak", exists_error) || exists_error) {
                impl_->status = "Online profile could not be recovered. Existing files were preserved; restore its backup before using Friends.";
                impl_->store_path.clear();
                return;
            }
            impl_->generate_identity();
            const std::string clean = clean_display_name(requested_name);
            impl_->display_name = clean.empty() ? "Racer" : clean;
            impl_->save_locked();
        }
        bool migrated_invites = false;
        for (auto& [id, invite] : impl_->invites) {
            (void)id;
            if (invite.token.has_value()) continue;
            do {
                invite.token = generate_invite_token();
            } while (std::any_of(impl_->invites.begin(), impl_->invites.end(),
                     [&](const auto& entry) {
                         return &entry.second != &invite &&
                                entry.second.token == invite.token;
                     }));
            migrated_invites = true;
        }
        if (migrated_invites) impl_->save_locked();
        impl_->configured = true;
    }
#if DKR_NETPLAY_WEBRTC
    impl_->closing.store(false, std::memory_order_release);
#else
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->status = "Friend presence is unavailable in this build.";
    }
#endif
    impl_->executor.start([owner = impl_.get()] { owner->tick(); },
        [owner = impl_.get()] {
            std::scoped_lock lock(owner->mutex);
            owner->status = "A social-network operation failed. Pending requests remain queued for retry.";
        });
}

void FriendService::shutdown() {
    ++impl_->generation;
    impl_->executor.stop();
    impl_->flush_save();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->configured = false;
    }
#if DKR_NETPLAY_WEBRTC
    impl_->closing.store(true, std::memory_order_release);
    std::shared_ptr<rtc::WebSocket> socket;
    std::map<std::uint64_t, std::shared_ptr<rtc::WebSocket>> invite_sockets;
    std::map<std::string, std::shared_ptr<Impl::Peer>> peers;
    {
        std::scoped_lock lock(impl_->mutex);
        socket = std::move(impl_->websocket);
        invite_sockets.swap(impl_->invite_websockets);
        peers.swap(impl_->peers);
        impl_->invite_connections.clear();
        impl_->early_candidates.clear();
        for (auto& [identity, record] : impl_->friends) {
            (void)identity;
            record.online = false;
            record.hosting = false;
            record.lobby_code.clear();
        }
    }
    for (auto& [id, peer] : peers) {
        (void)id;
        if (peer->channel) peer->channel->close();
        if (peer->connection) peer->connection->close();
    }
    for (const auto& peer : impl_->retired_peers) {
        if (peer->channel) peer->channel->close();
        if (peer->connection) peer->connection->close();
    }
    impl_->retired_peers.clear();
    if (socket) socket->close();
    for (auto& [id, invite_socket] : invite_sockets) {
        (void)id;
        if (invite_socket) invite_socket->close();
    }
    impl_->signaling_ready.store(false, std::memory_order_release);
    impl_->signaling_connecting.store(false, std::memory_order_release);
#endif
}

void FriendService::pump(const FriendLobbyAdvertisement& advertisement) {
    std::scoped_lock lock(impl_->mutex);
    impl_->pending_advertisement = advertisement;
}

void FriendService::Impl::tick() {
#if DKR_NETPLAY_WEBRTC
    std::vector<std::shared_ptr<Peer>> retired;
    std::vector<std::shared_ptr<rtc::WebSocket>> failed_sockets;
    {
        std::scoped_lock lock(mutex);
        if (callbacks_lost.exchange(false)) {
            diagnose_locked("Social event queue overflow; rebuilding connections without discarding requests.");
            for (const auto& [id, peer] : peers) retire_locked(peer);
            if (websocket) failed_sockets.push_back(std::move(websocket));
            signaling_ready.store(false);
            signaling_connecting.store(false);
            next_reconnect = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (const auto& [id, socket] : invite_websockets) {
                failed_sockets.push_back(socket);
                invite_disconnected_locked(id);
            }
            invite_websockets.clear();
            early_candidates.clear();
        }
        retired.swap(retired_peers);
    }
    // Never depend on spare executor capacity to release a connection, and
    // never run synchronous SDK close callbacks while holding the state lock.
    for (const auto& peer : retired) {
        if (peer->channel) peer->channel->close();
        if (peer->connection) peer->connection->close();
    }
    for (const auto& socket : failed_sockets) if (socket) socket->close();
#endif
    flush_save();
    publish_snapshot();
    FriendLobbyAdvertisement current_advertisement;
    { std::scoped_lock lock(mutex); current_advertisement = pending_advertisement; }
    const auto& advertisement = current_advertisement;
    auto* impl_ = this;
    std::vector<std::pair<std::string, Capability>> probes;
#if DKR_NETPLAY_WEBRTC
    std::vector<std::shared_ptr<Impl::Peer>> broadcast;
    bool refresh_invites = false;
    bool refreshed_probes = false;
    const auto now = std::chrono::steady_clock::now();
    if (signaling_connecting.load() && now - registration_started > std::chrono::seconds(15)) {
        signaling_connecting.store(false);
        next_reconnect = now + std::chrono::seconds(3);
        std::scoped_lock lock(mutex);
        status = "Friend presence registration timed out. Requests are safe and will retry.";
    }
    {
        std::scoped_lock lock(impl_->mutex);
        const bool host_invitation_changed =
            impl_->advertisement.hosting &&
            (!advertisement.hosting ||
             impl_->advertisement.quick_join_code !=
                 advertisement.quick_join_code);
        const std::uint64_t wall_now = unix_seconds();
        for (auto& [id, invite] : impl_->incoming_lobby_invites) {
            (void)id;
            if (invite.expires <= wall_now &&
                invite.status != FriendLobbyInviteStatus::Accepted &&
                invite.status != FriendLobbyInviteStatus::Declined &&
                invite.status != FriendLobbyInviteStatus::Cancelled) {
                invite.status = FriendLobbyInviteStatus::Expired;
                invite.admission = {};
            }
        }
        for (auto& [id, invite] : impl_->outgoing_lobby_invites) {
            if (invite.expires <= wall_now &&
                invite.status != FriendLobbyInviteStatus::Accepted &&
                invite.status != FriendLobbyInviteStatus::Declined &&
                invite.status != FriendLobbyInviteStatus::Cancelled) {
                invite.status = FriendLobbyInviteStatus::Expired;
                invite.admission = {};
            } else if (host_invitation_changed &&
                       (invite.status == FriendLobbyInviteStatus::Sent ||
                        invite.status == FriendLobbyInviteStatus::Delivered)) {
                const auto peer = impl_->find_identity_route(invite.friend_identity);
                if (peer != impl_->peers.end() && peer->second->authenticated) {
                    impl_->send_json(peer->second,
                        Json{{"kind", "lobby-invite-cancelled"}, {"id", id}});
                }
                invite.status = FriendLobbyInviteStatus::Cancelled;
                invite.admission = {};
            }
        }
        impl_->advertisement = advertisement;
        if (now >= impl_->next_invite_refresh) {
            impl_->next_invite_refresh = now + std::chrono::seconds(1);
            refresh_invites = true;
        }
        // A new request may reuse a healthy connection. Resolve it explicitly
        // instead of requiring another one-shot authentication callback.
        for (const auto& [id, peer] : peers) {
            if (peer->authenticated && !peer->retiring) resolve_requests_locked(peer);
        }
        if (impl_->configured && !impl_->signaling_ready.load() &&
            !impl_->signaling_connecting.load() && now >= impl_->next_reconnect) {
            // Open outside the lock below.
        }
        if (impl_->signaling_ready.load() && now >= impl_->next_friend_probe) {
            refreshed_probes = true;
            impl_->next_friend_probe = now + std::chrono::milliseconds(500);
            for (const auto& [identity, record] : impl_->friends) {
                if (!record.blocked && impl_->local_identity < identity) {
                    probes.emplace_back(identity, Capability{});
                }
            }
            for (const auto& [id, request] : impl_->requests) {
                (void)id;
                if (!request.incoming) {
                    probes.emplace_back(request.expected_identity,
                                        request.capability);
                }
            }
            for (const auto& [id, decision] : decisions) {
                if (!decision.acknowledged && decision.public_key != secure::Key{})
                    probes.emplace_back(identity_of(decision.public_key), decision.capability);
            }
        }
        if (impl_->signaling_ready.load() && now >= impl_->next_state_broadcast) {
            impl_->next_state_broadcast = now + std::chrono::seconds(2);
            for (const auto& [id, peer] : impl_->peers) {
                (void)id;
                if (peer->authenticated && peer->channel && peer->channel->isOpen()) {
                    send_requests_locked(peer);
                    send_decisions_locked(peer);
                    if (accepted_peer_locked(peer)) {
                        const auto identity_value = identity_of(peer->remote_public);
                        for (const auto& [invite_id, invite] : outgoing_lobby_invites)
                            if (invite.friend_identity == identity_value && invite.expires > wall_now)
                                send_lobby_record_locked(peer, invite);
                        for (const auto& [invite_id, invite] : incoming_lobby_invites)
                            if (invite.friend_identity == identity_value && invite.expires > wall_now && !invite.decision_acknowledged)
                                send_lobby_record_locked(peer, invite);
                    }
                    broadcast.push_back(peer);
                }
            }
        }
    }
    if (impl_->configured && !impl_->signaling_ready.load() &&
        !impl_->signaling_connecting.load() && now >= impl_->next_reconnect) {
        impl_->open_signaling();
    }
    if (refresh_invites) impl_->refresh_invite_signaling();
    if (impl_->signaling_ready.load() &&
        now - impl_->heartbeat >= std::chrono::seconds(5)) {
        impl_->send_signal(Json{{"type", "HEARTBEAT"}});
        impl_->heartbeat = now;
    }
    // Rotate sorted destinations for fairness. Retain every durable request,
    // but allow at most one new attempt per half second and four negotiations.
    {
        std::scoped_lock lock(impl_->mutex);
        for (auto it = peers.begin(); it != peers.end();) {
            const auto& peer = it->second;
            if (peer->retiring || peer->send_failed || (!peer->authenticated && now - peer->created >= std::chrono::seconds(25)) ||
                (peer->channel && peer->channel->isClosed())) {
                for (auto& [id, request] : requests) {
                    if (!request.incoming && !request.received &&
                        (request.expected_identity == identity_of(peer->remote_public) ||
                         peer_id_for_route(request.expected_identity) == peer->remote_id))
                        request.problem = "Connection attempt did not complete. Request retained; retrying automatically.";
                }
                if (const auto record = friends.find(identity_of(peer->remote_public)); record != friends.end()) {
                    const bool alternate = std::any_of(peers.begin(), peers.end(), [&](const auto& entry) {
                        const auto& other = entry.second;
                        return other != peer && !other->retiring && other->authenticated &&
                            other->remote_public == peer->remote_public && other->channel && other->channel->isOpen();
                    });
                    if (!alternate) { record->second.online = false; record->second.hosting = false; record->second.lobby_code.clear(); }
                }
                retire_locked(peer);
                it = peers.erase(it);
            } else ++it;
        }
    }
    std::sort(probes.begin(), probes.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        // A pending transaction must retain its authentication capability;
        // an ordinary accepted-friend presence probe has none.
        return a.second > b.second;
    });
    probes.erase(std::unique(probes.begin(), probes.end(), [](const auto& a, const auto& b) {
        return a.first == b.first;
    }), probes.end());
    if (refreshed_probes) std::erase_if(retries, [&](const auto& entry) {
        return std::none_of(probes.begin(), probes.end(),
            [&](const auto& probe) { return probe.first == entry.first; });
    });
    const auto pivot = std::upper_bound(probes.begin(), probes.end(), last_probe,
        [](const auto& key, const auto& value) { return key < value.first; });
    std::rotate(probes.begin(), pivot, probes.end());
    for (const auto& [identity_value, capability] : probes) {
        if (now < next_attempt) break;
        {
            std::scoped_lock lock(mutex);
            if (std::count_if(peers.begin(), peers.end(), [](const auto& entry) {
                return !entry.second->authenticated;
            }) >= 4) break;
            if (authenticated_peer_locked(identity_value)) continue;
            const auto found = peers.find(peer_id_for_route(identity_value));
            if (found != peers.end()) continue;
        }
        auto& retry = retries[identity_value];
        if (now < retry.due) continue;
        const auto delay = std::min(60U, 4U << std::min(retry.attempts++, 4U));
        retry.due = now + std::chrono::seconds(delay) +
            std::chrono::milliseconds(std::hash<std::string>{}(identity_value) % 1000U);
        next_attempt = now + std::chrono::milliseconds(500);
        last_probe = identity_value;
        impl_->create_offer(identity_value, capability);
    }
    for (const auto& peer : broadcast) impl_->send_state(peer);
#else
    std::scoped_lock lock(impl_->mutex);
    impl_->advertisement = advertisement;
#endif
}

std::string FriendService::display_name() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->display_name;
}

bool FriendService::set_display_name(std::string_view name, std::string& error) {
    const std::string clean = clean_display_name(name);
    if (clean.size() < 2U) {
        error = "Display names must contain at least two visible characters.";
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->display_name = clean;
    impl_->save_locked();
    error.clear();
    return true;
}

std::string FriendService::identity_label() const {
    std::scoped_lock lock(impl_->mutex);
    return grouped(impl_->local_identity);
}

std::string FriendService::status() const {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->save_failed) return "Cannot save Online Profile. Changes are in memory only; check storage and retry before closing DKR-R.";
    if (impl_->save_revision != impl_->saved_revision) return "Saving Online Profile changes...";
    return impl_->status;
}

bool FriendService::presence_available() const {
#if DKR_NETPLAY_WEBRTC
    return impl_->signaling_ready.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool FriendService::appear_offline() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->appear_offline;
}

bool FriendService::set_appear_offline(bool enabled, std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::vector<std::shared_ptr<Impl::Peer>> broadcast;
    std::vector<std::pair<std::shared_ptr<Impl::Peer>, std::uint64_t>> decline;
#endif
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->appear_offline == enabled) {
            error.clear();
            return true;
        }
        impl_->appear_offline = enabled;
        if (enabled) {
            for (auto& [id, invite] : impl_->incoming_lobby_invites) {
                if (invite.status != FriendLobbyInviteStatus::Delivered ||
                    invite.expires <= unix_seconds()) continue;
#if DKR_NETPLAY_WEBRTC
                const auto peer = impl_->find_identity_route(invite.friend_identity);
                if (peer != impl_->peers.end() && peer->second->authenticated &&
                    peer->second->channel && peer->second->channel->isOpen()) {
                    decline.emplace_back(peer->second, invite.wire_id);
                }
#endif
                invite.status = FriendLobbyInviteStatus::Declined;
                invite.admission = {};
            }
        }
        impl_->status = enabled ? "Appearing offline to friends."
                                : "Friend presence is online.";
        impl_->save_locked();
#if DKR_NETPLAY_WEBRTC
        for (const auto& [id, peer] : impl_->peers) {
            (void)id;
            if (peer->authenticated && peer->channel && peer->channel->isOpen()) {
                broadcast.push_back(peer);
            }
        }
#endif
    }
#if DKR_NETPLAY_WEBRTC
    for (const auto& [peer, id] : decline) {
        impl_->send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                    {"id", id}});
    }
    for (const auto& peer : broadcast) impl_->send_state(peer);
#endif
    error.clear();
    return true;
}

bool FriendService::allow_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->allow_lobby_invites;
}

bool FriendService::set_allow_lobby_invites(bool enabled,
                                             std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::vector<std::pair<std::shared_ptr<Impl::Peer>, std::uint64_t>> decline;
#endif
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->allow_lobby_invites == enabled) {
            error.clear();
            return true;
        }
        impl_->allow_lobby_invites = enabled;
        if (!enabled) {
            for (auto& [id, invite] : impl_->incoming_lobby_invites) {
                if (invite.status != FriendLobbyInviteStatus::Delivered ||
                    invite.expires <= unix_seconds()) continue;
#if DKR_NETPLAY_WEBRTC
                const auto peer = impl_->find_identity_route(invite.friend_identity);
                if (peer != impl_->peers.end() && peer->second->authenticated &&
                    peer->second->channel && peer->second->channel->isOpen()) {
                    decline.emplace_back(peer->second, invite.wire_id);
                }
#endif
                invite.status = FriendLobbyInviteStatus::Declined;
                invite.admission = {};
            }
        }
        impl_->status = enabled ? "Friend lobby invitations are enabled."
                                : "Friend lobby invitations are disabled.";
        impl_->save_locked();
    }
#if DKR_NETPLAY_WEBRTC
    for (const auto& [peer, id] : decline) {
        impl_->send_json(peer, Json{{"kind", "lobby-invite-declined"},
                                    {"id", id}});
    }
#endif
    error.clear();
    return true;
}

bool FriendService::create_invite(FriendInviteLifetime lifetime,
                                  std::chrono::seconds duration,
                                  FriendInviteView& result,
                                  std::string& error) {
    Impl::Invite invite{};
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->configured) { error = "Online Profile is unavailable."; return false; }
        if (std::count_if(impl_->invites.begin(), impl_->invites.end(), [](const auto& entry) {
                return !entry.second.revoked && (entry.second.expires == 0U || entry.second.expires > unix_seconds());
            }) >= 8) {
            error = "Eight Friend Codes are already active. Revoke an unused code before creating another.";
            return false;
        }
        invite.id = impl_->next_id++;
        do {
            invite.token = generate_invite_token();
        } while (std::any_of(impl_->invites.begin(), impl_->invites.end(),
                 [&](const auto& entry) {
                     return entry.second.token == invite.token;
                 }));
        invite.capability = derive_capability(*invite.token);
        invite.lifetime = lifetime;
        if (lifetime == FriendInviteLifetime::Timed) {
            const auto clamped = std::clamp(
                duration, std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::minutes(1)),
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::hours(24 * 30)));
            invite.expires = unix_seconds() +
                static_cast<std::uint64_t>(clamped.count());
        }
        impl_->invites[invite.id] = invite;
        impl_->save_locked();
    }
    ParsedFriendCode code{fingerprint(impl_->identity.public_key),
                          invite.capability, invite.token, invite.expires,
                          invite.lifetime};
    result = {invite.id, encode_friend_code(code), invite.lifetime,
              invite.expires};
#if DKR_NETPLAY_WEBRTC
    // The social worker observes the new route; UI code never opens sockets.
#endif
    error.clear();
    return true;
}

bool FriendService::revoke_invite(std::uint64_t invite_id, std::string& error) {
    {
        std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->invites.find(invite_id);
        if (found == impl_->invites.end()) {
            error = "That Friend Code is no longer active.";
            return false;
        }
        found->second.revoked = true;
        impl_->save_locked();
    }
#if DKR_NETPLAY_WEBRTC
    // The social worker retires revoked routes on its next pass.
#endif
    error.clear();
    return true;
}

std::vector<FriendInviteView> FriendService::invitations() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->copy_invitations_locked();
}

bool FriendService::submit_friend_code(std::string_view code_text,
                                       std::string& error) {
    ParsedFriendCode code{};
    if (!parse_friend_code(code_text, code)) {
        error = "That Friend Code is invalid or expired.";
        return false;
    }
    const std::string identity_value = code.invite_token.has_value()
        ? invite_route(*code.invite_token)
        : base32_encode(code.fingerprint);
    std::scoped_lock lock(impl_->mutex);
    if (code.invite_token.has_value() &&
        std::any_of(impl_->invites.begin(), impl_->invites.end(),
            [&](const auto& entry) {
                return entry.second.token == code.invite_token;
            })) {
        error = "That is your own Friend Code.";
        return false;
    }
    if (identity_value == impl_->local_identity) {
        error = "That is your own Friend Code.";
        return false;
    }
    const auto existing = impl_->friends.find(identity_value);
    if (existing != impl_->friends.end()) {
        error = existing->second.blocked
            ? "Unblock this racer before adding them again."
            : "That racer is already on your Friends list.";
        return false;
    }
    if (!impl_->configured) {
        error = "Online Profile is unavailable. Restore its existing profile before adding friends.";
        return false;
    }
    const auto duplicate = std::find_if(impl_->requests.begin(), impl_->requests.end(), [&](const auto& entry) {
        return !entry.second.incoming && entry.second.capability == code.capability;
    });
    if (duplicate != impl_->requests.end()) {
        if (duplicate->second.cancelled) {
            // Explicitly submitting the code again reverses a cancellation
            // that has not reached authentication/the decision outbox yet.
            duplicate->second.cancelled = false;
            duplicate->second.problem.clear();
            impl_->save_locked();
            impl_->publish_requests_locked();
            impl_->status = "Friend request queued again at your request.";
        } else {
            impl_->status = "This friend request is already queued. No duplicate was created.";
        }
        error.clear();
        return true;
    }
    if (impl_->requests.size() >= 256U) {
        error = "The pending request list is full. Cancel or complete some requests before adding another.";
        return false;
    }
    const std::uint64_t id = impl_->next_id++;
    Impl::Request request{};
    request.id = id;
    request.wire_id = id;
    request.incoming = false;
    request.capability = code.capability;
    request.expected_identity = identity_value;
    request.name = "Pending racer";
    impl_->requests[id] = request;
    impl_->status = "Friend request saved locally. Delivery will be confirmed after the racer receives it.";
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::retry_request(std::uint64_t request_id, std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->requests.find(request_id);
    if (found == impl_->requests.end() || found->second.incoming || found->second.cancelled) {
        error = "That outgoing request is no longer pending."; return false;
    }
    auto& request = found->second;
    const auto now = std::chrono::steady_clock::now();
    if (now < request.manual_retry_after) { error = "A retry was just requested. Please wait a few seconds."; return false; }
    request.manual_retry_after = now + std::chrono::seconds(5);
    request.problem.clear();
#if DKR_NETPLAY_WEBRTC
    impl_->retries.erase(request.expected_identity);
    impl_->next_state_broadcast = now;
    impl_->next_friend_probe = now;
    impl_->next_attempt = now;
#endif
    impl_->diagnose_locked("User requested a rate-limited friend delivery retry.");
    impl_->publish_requests_locked();
    error.clear(); return true;
}

std::string FriendService::diagnostics() const {
    std::scoped_lock lock(impl_->mutex);
    std::ostringstream text;
    text << "DKR-R friend delivery diagnostics (receipts v1)\nNo friend codes, identities, names, keys or addresses included.\n";
    for (const auto& event : impl_->diagnostic_events) text << event << '\n';
    return text.str();
}

std::vector<FriendRequestView> FriendService::pending_requests() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->copy_pending_requests_locked();
}

bool FriendService::accept_request(std::uint64_t request_id,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->requests.find(request_id);
    if (found == impl_->requests.end() || !found->second.incoming) {
        error = "That friend request is no longer pending.";
        return false;
    }
    const std::string identity_value = identity_of(found->second.public_key);
    if (!impl_->valid_invite_id(found->second.capability)) {
        error = "That Friend Code has expired, was revoked, or has already been consumed. Ask the racer to use a new code.";
        return false;
    }
    Impl::Friend accepted{};
    accepted.public_key = found->second.public_key;
    accepted.name = found->second.name;
    accepted.last_seen = unix_seconds();
    accepted.online = false;
#if DKR_NETPLAY_WEBRTC
    if (const auto peer = impl_->authenticated_peer_locked(identity_value)) accepted.online = peer->remote_online;
#endif
    impl_->friends[identity_value] = std::move(accepted);
    const Capability capability = found->second.capability;
    const auto decision_id = impl_->next_id++;
    impl_->decisions[decision_id] = Impl::Decision{decision_id, found->second.public_key, capability, "accepted", false, found->second.wire_id};
    impl_->requests.erase(found);
    for (auto& [id, invite] : impl_->invites) {
        (void)id;
        const bool capability_matches = invite.capability == capability ||
            (invite.token.has_value() &&
             derive_capability(*invite.token) == capability);
        if (capability_matches &&
            invite.lifetime == FriendInviteLifetime::SingleUse) {
            invite.revoked = true;
        }
    }
    impl_->status = "Friend added locally. Confirmation is queued until the racer is reachable.";
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::reject_request(std::uint64_t request_id, bool block,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->requests.find(request_id);
    if (found == impl_->requests.end()) {
        error = "That friend request is no longer pending.";
        return false;
    }
    const std::string identity_value = identity_of(found->second.public_key);
    if (!found->second.incoming && found->second.public_key == secure::Key{}) {
        found->second.cancelled = true;
        impl_->save_locked();
        impl_->publish_requests_locked();
        impl_->status = "Friend request cancelled locally. The other racer will be notified in the background when reachable.";
        error.clear();
        return true;
    }
    if (found->second.public_key != secure::Key{}) {
        const auto decision_id = impl_->next_id++;
        impl_->decisions[decision_id] = Impl::Decision{decision_id, found->second.public_key,
            found->second.capability, !found->second.incoming ? "cancelled" : block ? "blocked" : "rejected", false, found->second.wire_id};
    }
    if (block && found->second.public_key != secure::Key{}) {
        Impl::Friend rejected{};
        rejected.public_key = found->second.public_key;
        rejected.name = found->second.name;
        rejected.blocked = true;
        rejected.last_seen = unix_seconds();
        impl_->friends[identity_value] = std::move(rejected);
    }
    const bool outgoing = !found->second.incoming;
    impl_->requests.erase(found);
    impl_->save_locked();
    impl_->publish_requests_locked();
    if (outgoing) {
        impl_->status = "Friend request cancelled locally. The other racer will be notified in the background when reachable.";
    }
    error.clear();
    return true;
}

std::vector<FriendView> FriendService::friends(bool include_blocked) const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->copy_friends_locked(include_blocked);
}

bool FriendService::set_friend_nickname(std::string_view identity_value,
                                        std::string_view nickname,
                                        std::string& error) {
    const std::string clean = clean_display_name(nickname);
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer on your Friends list.";
        return false;
    }
    found->second.nickname = clean;
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::remove_friend(std::string_view identity_value,
                                  std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer on your Friends list.";
        return false;
    }
#if DKR_NETPLAY_WEBRTC
    const auto peer = impl_->find_identity_route(identity_value);
    if (peer != impl_->peers.end() && peer->second->authenticated) {
        impl_->send_json(peer->second, Json{{"kind", "removed"}});
    }
#endif
    impl_->friends.erase(found);
#if DKR_NETPLAY_WEBRTC
    for (const auto& [route, peer] : impl_->peers) {
        if (identity_of(peer->remote_public) == identity_value) impl_->retire_locked(peer);
    }
#endif
    std::erase_if(impl_->decisions, [&](const auto& entry) { return identity_of(entry.second.public_key) == identity_value; });
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::block_friend(std::string_view identity_value,
                                 std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end()) {
        error = "That racer is no longer known to this profile.";
        return false;
    }
    found->second.blocked = true;
    found->second.online = false;
    found->second.hosting = false;
    found->second.lobby_code.clear();
#if DKR_NETPLAY_WEBRTC
    for (const auto& [route, peer] : impl_->peers) {
        if (identity_of(peer->remote_public) == identity_value) impl_->retire_locked(peer);
    }
#endif
    for (auto& [id, invite] : impl_->incoming_lobby_invites) {
        if (invite.friend_identity == identity_value) { invite.status = FriendLobbyInviteStatus::Cancelled; invite.admission = {}; }
    }
    for (auto& [id, invite] : impl_->outgoing_lobby_invites) {
        if (invite.friend_identity == identity_value) { invite.status = FriendLobbyInviteStatus::Cancelled; invite.admission = {}; }
    }
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::unblock_friend(std::string_view identity_value,
                                   std::string& error) {
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->friends.find(std::string(identity_value));
    if (found == impl_->friends.end() || !found->second.blocked) {
        error = "That racer is not blocked.";
        return false;
    }
    found->second.blocked = false;
    impl_->save_locked();
    error.clear();
    return true;
}

bool FriendService::send_lobby_invite(
    std::string_view friend_identity,
    const FriendLobbyAdvertisement& lobby,
    const secure::Key& admission,
    FriendLobbyInviteView& result,
    std::string& error) {
#if DKR_NETPLAY_WEBRTC
    if (!lobby.hosting || !valid_quick_join_code(lobby.quick_join_code) ||
        lobby.maximum_players < 2U || lobby.maximum_players > kSupportedOnlinePlayers ||
        lobby.players >= lobby.maximum_players || admission == secure::Key{}) {
        error = "The lobby cannot issue a usable friend invitation right now.";
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    const std::string identity_value(friend_identity);
    const auto friend_found = impl_->friends.find(identity_value);
    const auto peer_found = impl_->find_identity_route(identity_value);
    if (friend_found == impl_->friends.end() || friend_found->second.blocked ||
        !friend_found->second.online || peer_found == impl_->peers.end() ||
        !peer_found->second->authenticated || !peer_found->second->channel ||
        !peer_found->second->channel->isOpen()) {
        error = "That friend is offline or their secure presence channel is not ready.";
        return false;
    }
    for (auto iterator = impl_->outgoing_lobby_invites.begin();
         iterator != impl_->outgoing_lobby_invites.end();) {
        if (iterator->second.expires <= unix_seconds() ||
            iterator->second.status == FriendLobbyInviteStatus::Cancelled ||
            iterator->second.status == FriendLobbyInviteStatus::Declined) {
            iterator = impl_->outgoing_lobby_invites.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (impl_->outgoing_lobby_invites.size() >= kMaximumLobbyInvites) {
        error = "Too many friend invitations are already active.";
        return false;
    }
    std::uint64_t id = 0U;
    do {
        const secure::Key random = secure::generate_key();
        id = take64(random.data());
    } while (id == 0U || impl_->outgoing_lobby_invites.contains(id));
    Impl::LobbyInvite invite{};
    invite.id = id;
    invite.friend_identity = identity_value;
    invite.friend_name = friend_found->second.nickname.empty()
        ? friend_found->second.name : friend_found->second.nickname;
    invite.lobby_code = lobby.quick_join_code;
    invite.synchronization = lobby.synchronization;
    invite.compatibility = lobby.compatibility;
    invite.players = lobby.players;
    invite.maximum_players = lobby.maximum_players;
    invite.expires = unix_seconds() + kLobbyInviteLifetimeSeconds;
    invite.admission = admission;
    invite.status = FriendLobbyInviteStatus::Sent;
    impl_->outgoing_lobby_invites[id] = invite;
    impl_->send_json(peer_found->second,
        Json{{"kind", "lobby-invite"}, {"id", id},
             {"code", invite.lobby_code}, {"players", invite.players},
             {"maximum", invite.maximum_players},
             {"synchronization", invite.synchronization},
             {"compatibility", invite.compatibility},
             {"expires", invite.expires},
             {"admission", hex_encode(invite.admission)}});
    result = {invite.id, invite.friend_identity, invite.friend_name,
              invite.lobby_code, invite.synchronization,
              invite.compatibility, invite.players,
              invite.maximum_players, invite.expires, invite.admission,
              invite.status, false};
    impl_->status = "Lobby invitation sent to " + invite.friend_name + ".";
    error.clear();
    return true;
#else
    (void)friend_identity; (void)lobby; (void)admission; (void)result;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

std::vector<FriendLobbyInviteView> FriendService::incoming_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->copy_incoming_lobby_invites_locked();
}

std::vector<FriendLobbyInviteView> FriendService::outgoing_lobby_invites() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->copy_outgoing_lobby_invites_locked();
}

bool FriendService::respond_lobby_invite(std::uint64_t invite_id, bool accept,
                                         FriendLobbyInviteView& result,
                                         std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->incoming_lobby_invites.find(invite_id);
    if (found == impl_->incoming_lobby_invites.end() ||
        found->second.expires <= unix_seconds() ||
        Impl::terminal_invite(found->second.status)) {
        error = "That lobby invitation is no longer active.";
        return false;
    }
    Impl::LobbyInvite& invite = found->second;
    const auto peer = impl_->find_identity_route(invite.friend_identity);
    invite.status = accept ? FriendLobbyInviteStatus::Accepted
                           : FriendLobbyInviteStatus::Declined;
    if (peer != impl_->peers.end()) impl_->send_lobby_record_locked(peer->second, invite);
    result = {invite.id, invite.friend_identity, invite.friend_name,
              invite.lobby_code, invite.synchronization,
              invite.compatibility, invite.players,
              invite.maximum_players, invite.expires, invite.admission,
              invite.status, true};
    if (!accept) invite.admission = {};
    impl_->status = accept ? "Joining " + invite.friend_name + "'s lobby..."
                           : "Lobby invitation declined.";
    error.clear();
    return true;
#else
    (void)invite_id; (void)accept; (void)result;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

bool FriendService::cancel_lobby_invite(std::uint64_t invite_id,
                                        std::string& error) {
#if DKR_NETPLAY_WEBRTC
    std::scoped_lock lock(impl_->mutex);
    const auto found = impl_->outgoing_lobby_invites.find(invite_id);
    if (found == impl_->outgoing_lobby_invites.end()) {
        error = "That lobby invitation is no longer active.";
        return false;
    }
    Impl::LobbyInvite& invite = found->second;
    const auto peer = impl_->find_identity_route(invite.friend_identity);
    if (peer != impl_->peers.end() && peer->second->authenticated) {
        impl_->send_json(peer->second,
            Json{{"kind", "lobby-invite-cancelled"}, {"id", invite.id}});
    }
    invite.status = FriendLobbyInviteStatus::Cancelled;
    invite.admission = {};
    error.clear();
    return true;
#else
    (void)invite_id;
    error = "Friend lobby invitations require the WebRTC presence service.";
    return false;
#endif
}

FriendService& friend_service() {
    static FriendService service;
    return service;
}

} // namespace dkr::runtime::netplay
