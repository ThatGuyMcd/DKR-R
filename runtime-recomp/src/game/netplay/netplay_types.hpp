#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

// Protocol 29 carries the pointer-free authored actor registry, lossless
// bounded authoritative-state compression and transport-independent admission
// bootstrap used by five-character Quick Join. Refuse older peers rather than
// silently accepting an incomplete world-state contract or incompatible wire
// framing.
// Protocol 34 adds an acknowledged gameplay-resume barrier. Player 1 may not
// begin the race timeline until every client has armed the new input epoch.
// Keeping this compatibility gate distinct prevents older peers from entering
// the race while another machine is still parked on its loading frame.
// Protocol 36 adds resumable Quick Join routes and explicit disconnect causes.
// Protocol 37 adds an authenticated completed-simulation watermark. Player 1
// uses it only for bounded pacing backpressure; it never replaces the
// immutable frame-commit ledger or changes the fixed 30 Hz simulation.
// Protocol 38 adds a compact post-frame racer-orientation correction stream.
// It keeps plane camera and visual-heading accumulators aligned to Player 1
// without placing presentation fields in the strict determinism digest.
// v39 adds an optional, one-use Friend lobby admission credential to Hello.
// Manual Quick Join sends an all-zero credential and retains host approval.
// v40 makes the host's pre-launch countdown authoritative lobby state so every
// racer sees the same five-second start signal before the loading barrier.
// v43 isolates online EEPROM data from single-player saves and requires both a
// disk read-back acknowledgement and an active-runtime save hash before frame
// zero can be released.
// v44 isolates reliable checkpoint traffic from the control/commit channels.
// v45 admits zero-racer cinematics at the existing load barrier and keeps all
// admitted frontend ticks on the same fixed logic step as racing. Older peers
// can reject the cinematic barrier or reach its boundary on a different tick.
// v46 adds the checkpoint pre-flight lane and boundary-safe recovery admission.
// v47 versions refreshed future input samples and bounds host prediction.
// Mixing with older peers would silently restore stale-input behaviour.
// v48 binds the immutable gameplay mod manifest into every launch and adds
// consent-based, provisional mod admission. Older clients cannot skip it.
// v49 expands private character sound tokens and versions sparse mod sidecars.
// An older peer must never interpret these tokens/checkpoints as its roster.
inline constexpr std::uint32_t kProtocolVersion = 49U;
inline constexpr std::uint8_t kMaximumInputDelayFrames = 9U;
inline constexpr std::size_t kMaximumPlayers = 4U;
// Regular races/minigames support 2-4 peers. Adventure remains two-player.
inline constexpr std::size_t kSupportedOnlinePlayers = 4U;
inline constexpr std::size_t kMaximumLobbyNameBytes = 48U;
inline constexpr std::size_t kMaximumPlayerNameBytes = 24U;
inline constexpr std::size_t kMaximumChatBytes = 256U;
// Release candidates and platform-qualified test builds legitimately carry a
// longer identifier than a bare semantic version (for example,
// "1.0.0-netplay-admission-restore-rc6"). Keep it bounded, but do not silently
// turn an otherwise valid encrypted Hello into an empty payload.
inline constexpr std::size_t kMaximumReleaseVersionBytes = 96U;
inline constexpr std::size_t kMaximumBuildFingerprintBytes = 192U;

enum class Visibility : std::uint8_t { Public, Private, Unlisted, Lan };
enum class ConnectionMethod : std::uint8_t {
    Lan,
    VirtualLan,
    DirectInternet,
    QuickJoin,
};
enum class RoomPhase : std::uint8_t {
    Waiting,
    ReadyCheck,
    Loading,
    Running,
    Finished,
};
enum class HostControlPolicy : std::uint8_t {
    GuidedUntilCharacterSelect,
    HostSharedMenus,
    EveryAssignedPort,
};
enum class Route : std::uint8_t { Unknown, Lan, Direct, Relay };
enum class Revision : std::uint8_t { UsV77, UsV80 };
// Values 0/1 are frozen: saved settings and existing lobbies keep their exact
// meaning. Experimental rollback is a distinct, explicitly negotiated mode.
enum class SynchronizationMode : std::uint8_t { Rollback = 0, Lockstep = 1, ExperimentalRollback = 2 };

constexpr bool valid_synchronization_mode(SynchronizationMode mode) {
    return mode == SynchronizationMode::Rollback || mode == SynchronizationMode::Lockstep ||
           mode == SynchronizationMode::ExperimentalRollback;
}
constexpr bool synchronization_has_prediction_window(SynchronizationMode mode) {
    return mode == SynchronizationMode::Rollback || mode == SynchronizationMode::ExperimentalRollback;
}
constexpr const char* synchronization_name(SynchronizationMode mode) {
    switch (mode) {
    case SynchronizationMode::Rollback: return "Host prediction (legacy Rollback)";
    case SynchronizationMode::Lockstep: return "Strict input sync (Lockstep)";
    case SynchronizationMode::ExperimentalRollback: return "Experimental rollback";
    }
    return "Unknown synchronization mode";
}

struct PackedInput {
    std::uint16_t buttons = 0;
    std::int8_t stick_x = 0;
    std::int8_t stick_y = 0;

    constexpr bool operator==(const PackedInput&) const = default;
};
static_assert(sizeof(PackedInput) == 4U);

using FrameInputs = std::array<PackedInput, kMaximumPlayers>;

struct CompatibilityManifest {
    std::uint32_t protocol_version = kProtocolVersion;
    std::string release_version;
    std::string build_fingerprint;
    Revision revision = Revision::UsV77;
    std::uint64_t canonical_rom_hash = 0;
    std::uint64_t patch_policy_hash = 0;
    std::uint64_t gameplay_settings_hash = 0;
    std::uint64_t magic_codes_hash = 0;
    std::uint64_t session_save_hash = 0;
    std::uint32_t simulation_rate = 30U;
    std::string architecture;
    std::string floating_point_mode;
    // Empty for vanilla sessions; otherwise lowercase SHA-256 of the exact
    // canonical online mod manifest. Cosmetics are intentionally excluded.
    std::string mod_manifest_hash;

    bool operator==(const CompatibilityManifest&) const = default;
};

struct Rules {
    HostControlPolicy host_control =
        HostControlPolicy::GuidedUntilCharacterSelect;
    std::uint8_t maximum_players = 2U;
    bool automatic_input_delay = true;
    std::uint8_t manual_input_delay = 2U;
    std::uint8_t rollback_window = 10U;
    SynchronizationMode synchronization = SynchronizationMode::Rollback;
    bool relay_only = false;
    bool record_replay = true;

    bool operator==(const Rules&) const = default;
};

struct Player {
    bool occupied = false;
    bool host = false;
    bool ready = false;
    bool compatible = false;
    bool loaded = false;
    std::uint8_t slot = 0U;
    std::string peer_id;
    std::string display_name;
    Route route = Route::Unknown;
    std::uint16_t ping_ms = 0U;
    std::uint16_t jitter_ms = 0U;
    float packet_loss_percent = 0.0F;
};

struct Room {
    std::string room_id;
    std::string join_code;
    std::string name;
    Visibility visibility = Visibility::Private;
    RoomPhase phase = RoomPhase::Waiting;
    Rules rules{};
    CompatibilityManifest manifest{};
    std::array<Player, kMaximumPlayers> players{};
    std::uint64_t generation = 0U;
};

struct LaunchDescriptor {
    std::uint64_t match_id = 0U;
    std::uint64_t lobby_generation = 0U;
    std::uint64_t compatibility_hash = 0U;
    std::uint8_t occupied_mask = 0U;
    std::uint8_t player_count = 0U;
    std::uint8_t input_delay_frames = 0U;
    std::uint8_t rollback_window = 0U;
    SynchronizationMode synchronization = SynchronizationMode::Rollback;
    HostControlPolicy host_control =
        HostControlPolicy::GuidedUntilCharacterSelect;

    constexpr bool occupied(std::size_t slot) const {
        return slot < kMaximumPlayers &&
               (occupied_mask & static_cast<std::uint8_t>(1U << slot)) != 0U;
    }

    bool operator==(const LaunchDescriptor&) const = default;
};

bool valid_launch_descriptor(const LaunchDescriptor& descriptor);
std::uint64_t launch_descriptor_hash(const LaunchDescriptor& descriptor);

bool valid_display_name(std::string_view value);
bool valid_room_name(std::string_view value);
bool valid_rules(const Rules& rules);
std::uint64_t stable_hash(std::string_view bytes);
std::uint64_t manifest_hash(const CompatibilityManifest& manifest);
bool valid_mod_manifest_hash(std::string_view hash);

enum class OnlineFailureCode : std::uint8_t {
    None,
    ProtocolMismatch,
    BuildMismatch,
    GamePakMismatch,
    PatchPolicyMismatch,
    GameplaySettingsMismatch,
    MagicCodesMismatch,
    SaveMismatch,
    SimulationRateMismatch,
    PlatformMismatch,
    InvitationInvalid,
    InvitationExpired,
    HostUnreachable,
    RequestRejected,
    Blocked,
    LobbyFull,
    LobbyLocked,
    BaselineFailure,
    DeterminismFailure,
    RecoveryFailure,
    FinishBarrierTimeout,
    TransportFailure,
    Unknown,
};

struct OnlineFailure {
    OnlineFailureCode code = OnlineFailureCode::None;
    std::uint64_t expected_magic_codes = 0U;
    std::uint64_t candidate_magic_codes = 0U;
    bool has_magic_code_details = false;
};

std::string incompatibility_reason(const CompatibilityManifest& expected,
                                   const CompatibilityManifest& candidate);
// Eligibility only: neither manifest is modified. Actual save installation
// and consent-based mod reconstruction remain mandatory before admission.
std::string session_content_sync_incompatibility(const CompatibilityManifest& expected,
    const CompatibilityManifest& candidate,bool host_offers_mods);
OnlineFailure classify_online_failure(std::string_view message);
std::string online_failure_display_message(std::string_view message);

} // namespace dkr::runtime::netplay
