#include "netplay_types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>

namespace dkr::runtime::netplay {

bool valid_launch_descriptor(const LaunchDescriptor& descriptor) {
    if (descriptor.match_id == 0U || descriptor.lobby_generation == 0U ||
        descriptor.compatibility_hash == 0U || descriptor.player_count < 2U ||
        descriptor.player_count > kSupportedOnlinePlayers ||
        descriptor.input_delay_frames > kMaximumInputDelayFrames ||
        descriptor.rollback_window > 20U ||
        !valid_synchronization_mode(descriptor.synchronization) ||
        (synchronization_has_prediction_window(descriptor.synchronization) &&
         descriptor.rollback_window < 2U) ||
        (descriptor.synchronization == SynchronizationMode::Lockstep &&
         descriptor.rollback_window != 0U) ||
        descriptor.host_control > HostControlPolicy::EveryAssignedPort ||
        (descriptor.occupied_mask & 1U) == 0U) {
        return false;
    }
    const std::uint8_t expected_mask = static_cast<std::uint8_t>(
        (1U << descriptor.player_count) - 1U);
    return descriptor.occupied_mask == expected_mask;
}

std::uint64_t launch_descriptor_hash(const LaunchDescriptor& descriptor) {
    std::string bytes;
    bytes.reserve(36U);
    auto append = [&bytes](std::uint64_t value, std::size_t width) {
        for (std::size_t index = 0; index < width; ++index) {
            bytes.push_back(static_cast<char>(value >> (index * 8U)));
        }
    };
    append(descriptor.match_id, 8U);
    append(descriptor.lobby_generation, 8U);
    append(descriptor.compatibility_hash, 8U);
    append(descriptor.occupied_mask, 1U);
    append(descriptor.player_count, 1U);
    append(descriptor.input_delay_frames, 1U);
    append(descriptor.rollback_window, 1U);
    append(static_cast<std::uint8_t>(descriptor.synchronization), 1U);
    append(static_cast<std::uint8_t>(descriptor.host_control), 1U);
    return stable_hash(bytes);
}
namespace {

bool ValidText(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= 0x20U && c != 0x7FU;
    });
}

void HashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

template <typename T>
void HashInteger(std::uint64_t& hash, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        HashByte(hash, static_cast<std::uint8_t>(value & 0xFFU));
        value = static_cast<T>(value >> 8U);
    }
}

void HashText(std::uint64_t& hash, std::string_view value) {
    HashInteger(hash, static_cast<std::uint32_t>(value.size()));
    for (const unsigned char c : value) {
        HashByte(hash, c);
    }
}

} // namespace

bool valid_display_name(std::string_view value) {
    return ValidText(value, kMaximumPlayerNameBytes);
}

bool valid_room_name(std::string_view value) {
    return ValidText(value, kMaximumLobbyNameBytes);
}

bool valid_rules(const Rules& rules) {
    return rules.maximum_players >= 2U && rules.maximum_players <= kSupportedOnlinePlayers &&
           rules.manual_input_delay <= kMaximumInputDelayFrames &&
           valid_synchronization_mode(rules.synchronization) &&
           ((synchronization_has_prediction_window(rules.synchronization) &&
             rules.rollback_window >= 2U && rules.rollback_window <= 20U) ||
            (rules.synchronization == SynchronizationMode::Lockstep &&
             rules.rollback_window == 0U));
}

std::uint64_t stable_hash(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        HashByte(hash, byte);
    }
    return hash;
}

std::uint64_t manifest_hash(const CompatibilityManifest& manifest) {
    std::uint64_t hash = 14695981039346656037ULL;
    HashInteger(hash, manifest.protocol_version);
    HashText(hash, manifest.release_version);
    HashText(hash, manifest.build_fingerprint);
    HashByte(hash, static_cast<std::uint8_t>(manifest.revision));
    HashInteger(hash, manifest.canonical_rom_hash);
    HashInteger(hash, manifest.patch_policy_hash);
    HashInteger(hash, manifest.gameplay_settings_hash);
    HashInteger(hash, manifest.magic_codes_hash);
    HashInteger(hash, manifest.session_save_hash);
    HashInteger(hash, manifest.simulation_rate);
    HashText(hash, manifest.architecture);
    HashText(hash, manifest.floating_point_mode);
    HashText(hash, manifest.mod_manifest_hash);
    return hash;
}
bool valid_mod_manifest_hash(std::string_view hash) {
    return hash.empty() || (hash.size()==64 && std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');}));
}

std::string incompatibility_reason(const CompatibilityManifest& expected,
                                   const CompatibilityManifest& candidate) {
    if (candidate.protocol_version != expected.protocol_version) {
        return "Netplay protocol versions do not match.";
    }
    if (candidate.release_version != expected.release_version ||
        candidate.build_fingerprint != expected.build_fingerprint) {
        return "DKR-R builds do not match.";
    }
    if (candidate.revision != expected.revision ||
        candidate.canonical_rom_hash != expected.canonical_rom_hash) {
        return "Game Pak revisions do not match.";
    }
    if (candidate.patch_policy_hash != expected.patch_policy_hash) {
        return "Recomp patch policies do not match.";
    }
    if (candidate.magic_codes_hash != expected.magic_codes_hash) {
        return "Magic Codes do not match.|magic:" +
               std::to_string(expected.magic_codes_hash) + ":" +
               std::to_string(candidate.magic_codes_hash);
    }
    if (candidate.gameplay_settings_hash != expected.gameplay_settings_hash) {
        return "Gameplay settings do not match.";
    }
    if (!valid_mod_manifest_hash(expected.mod_manifest_hash) || !valid_mod_manifest_hash(candidate.mod_manifest_hash) ||
        candidate.mod_manifest_hash != expected.mod_manifest_hash) {
        return "Gameplay mod manifests do not match.";
    }
    if (candidate.session_save_hash != expected.session_save_hash) {
        return "Adventure save contents differ.";
    }
    if (candidate.simulation_rate != expected.simulation_rate) {
        return "Simulation rates do not match.";
    }
    if (candidate.architecture != expected.architecture ||
        candidate.floating_point_mode != expected.floating_point_mode) {
        return "This platform pair has not passed deterministic compatibility.";
    }
    return {};
}

std::string online_failure_display_message(std::string_view message) {
    const std::size_t metadata = message.find('|');
    return std::string(message.substr(0U, metadata));
}

std::string session_content_sync_incompatibility(const CompatibilityManifest& expected,
    const CompatibilityManifest& candidate,bool host_offers_mods) {
    auto comparable=candidate;
    comparable.session_save_hash=expected.session_save_hash;
    if(host_offers_mods && !expected.mod_manifest_hash.empty() &&
       valid_mod_manifest_hash(comparable.mod_manifest_hash))
        comparable.mod_manifest_hash=expected.mod_manifest_hash;
    return incompatibility_reason(expected,comparable);
}

OnlineFailure classify_online_failure(std::string_view message) {
    OnlineFailure failure{};
    if (message.empty()) {
        return failure;
    }

    std::string lower(message);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    const auto contains = [&lower](std::string_view needle) {
        return lower.find(needle) != std::string::npos;
    };

    if (contains("protocol")) {
        failure.code = OnlineFailureCode::ProtocolMismatch;
    } else if (contains("builds do not match") ||
               contains("build version") ||
               contains("build fingerprint")) {
        failure.code = OnlineFailureCode::BuildMismatch;
    } else if (contains("game pak") || contains("rom revision") ||
               contains("roms do not match")) {
        failure.code = OnlineFailureCode::GamePakMismatch;
    } else if (contains("patch polic")) {
        failure.code = OnlineFailureCode::PatchPolicyMismatch;
    } else if (contains("magic code")) {
        failure.code = OnlineFailureCode::MagicCodesMismatch;
    } else if (contains("gameplay settings")) {
        failure.code = OnlineFailureCode::GameplaySettingsMismatch;
    } else if (contains("save") || contains("eeprom")) {
        failure.code = OnlineFailureCode::SaveMismatch;
    } else if (contains("simulation rate")) {
        failure.code = OnlineFailureCode::SimulationRateMismatch;
    } else if (contains("platform pair") || contains("floating point") ||
               contains("architecture")) {
        failure.code = OnlineFailureCode::PlatformMismatch;
    } else if (contains("expired")) {
        failure.code = OnlineFailureCode::InvitationExpired;
    } else if (contains("invalid invitation") ||
               contains("malformed invitation") ||
               contains("invalid invite") || contains("invalid code")) {
        failure.code = OnlineFailureCode::InvitationInvalid;
    } else if (contains("did not answer") || contains("unreachable") ||
               contains("firewall") || contains("udp port")) {
        failure.code = OnlineFailureCode::HostUnreachable;
    } else if (contains("rejected") || contains("declined")) {
        failure.code = OnlineFailureCode::RequestRejected;
    } else if (contains("blocked")) {
        failure.code = OnlineFailureCode::Blocked;
    } else if (contains("lobby is full") || contains("lobby full")) {
        failure.code = OnlineFailureCode::LobbyFull;
    } else if (contains("lobby is locked") || contains("lobby locked")) {
        failure.code = OnlineFailureCode::LobbyLocked;
    } else if (contains("baseline") || contains("moving actor") ||
               contains("outside rdram")) {
        failure.code = OnlineFailureCode::BaselineFailure;
    } else if (contains("determinism") || contains("frame commit") ||
               contains("authoritative timeline")) {
        failure.code = OnlineFailureCode::DeterminismFailure;
    } else if (contains("finish barrier")) {
        failure.code = OnlineFailureCode::FinishBarrierTimeout;
    } else if (contains("recovery") || contains("frame ledger") ||
               contains("bounded queue")) {
        failure.code = OnlineFailureCode::RecoveryFailure;
    } else if (contains("transport") || contains("connection") ||
               contains("disconnect") || contains("packet")) {
        failure.code = OnlineFailureCode::TransportFailure;
    } else {
        failure.code = OnlineFailureCode::Unknown;
    }

    constexpr std::string_view marker = "|magic:";
    const std::size_t details = message.find(marker);
    if (details != std::string_view::npos) {
        const std::string_view values = message.substr(details + marker.size());
        const std::size_t separator = values.find(':');
        if (separator != std::string_view::npos) {
            const std::string_view expected = values.substr(0U, separator);
            const std::string_view candidate = values.substr(separator + 1U);
            const auto expected_result = std::from_chars(
                expected.data(), expected.data() + expected.size(),
                failure.expected_magic_codes);
            const auto candidate_result = std::from_chars(
                candidate.data(), candidate.data() + candidate.size(),
                failure.candidate_magic_codes);
            failure.has_magic_code_details =
                expected_result.ec == std::errc{} &&
                expected_result.ptr == expected.data() + expected.size() &&
                candidate_result.ec == std::errc{} &&
                candidate_result.ptr == candidate.data() + candidate.size();
        }
    }
    return failure;
}

} // namespace dkr::runtime::netplay
