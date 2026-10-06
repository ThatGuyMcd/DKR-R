#pragma once
#include "netplay/experimental_session.hpp"
#include "netplay/experimental_presentation.hpp"
#include "rom_revision.hpp"
#include <functional>
#include <memory>

namespace dkr::mods::online { class RuntimeResources; }

namespace dkr::runtime::netplay::experimental {
struct OwnedBootOptions {
    bool normal_boot = false;
    std::uint8_t players = 0;
    HostControlPolicy host_control = HostControlPolicy::EveryAssignedPort;
    // Construction only. The host checkpoint admits this policy on all peers;
    // replay never reads a participant's live/local sound preferences.
    bool restore_multiplayer_music = false;
    // Immutable reconstruction proof, never an offline catalogue or a live
    // world's mutable caches. Each owned CPU creates its own native sidecar.
    std::shared_ptr<const dkr::mods::online::RuntimeResources> mods;
    // Native cache/menu sidecar captured AFTER the cold bootstrap's workers
    // have joined. Never borrow the normal world's mutable RuntimeSession.
    std::vector<std::uint8_t> mod_bootstrap;
};
struct ConfirmedAudio {
    std::uint64_t epoch = 0;
    std::uint32_t frame = 0, rate = 0;
    // Canonical little-endian stereo s16 PCM. Borrowed for this callback only;
    // an output device must copy into its own bounded queue, not retain a span.
    std::span<const std::uint8_t> pcm;
};
using ConfirmedAudioSink = std::function<bool(ConfirmedAudio, std::string&)>;
using ConfirmedRumbleSink = std::function<void(unsigned,bool)>;
struct OwnedSceneView {
    bool menu = false;
    std::uint32_t menu_id = 0;
    std::uint8_t owners = 0;
    std::int32_t level = -1, race_type = -1;
    std::uint8_t viewports = 0;
    bool two_player_adventure = false;
    // Observation only: never a topology/ownership/confirmation decision.
    std::uint8_t finished_racers = 0;
    bool tt_camera = false;
};
class OwnedGame : public SceneSimulation {
public:
    // Owner-thread view of actual guest state, never a requested/faked scene.
    virtual OwnedSceneView scene_view() const = 0;
    virtual bool publish_current(std::uint64_t epoch, std::uint32_t frame, std::string& error) = 0;
    // Initial, zero-frame canonical checkpoint only. NOT a host-state repair
    // operation during play and never a raw RuntimeState live-thread restore.
    virtual bool admit_initial(std::span<const std::uint8_t> state, std::string& error) = 0;
    virtual std::uint32_t confirmed_count() const = 0;
    virtual std::span<const std::uint8_t> confirmed_save() const = 0;
    virtual std::span<const std::uint8_t> confirmed_paks() const = 0;
    virtual bool install_initial_paks(std::uint64_t epoch,std::span<const std::uint8_t> images,std::string& error) = 0;
    virtual void set_confirmed_rumble_sink(ConfirmedRumbleSink sink) = 0;
};
// Production registry contains only linked, isolated full-scene engines.
bool owned_adapter_available(dkr::runtime::rom::Revision revision);
std::string_view owned_adapter_identity(dkr::runtime::rom::Revision revision);
std::unique_ptr<OwnedGame> make_owned_game_for_revision(dkr::runtime::rom::Revision revision,
    std::span<const std::uint8_t> local_bootstrap,
    std::span<const std::uint8_t> canonical_rom, std::uint64_t epoch,
    PresentationMailbox& presentation, ConfirmedAudioSink sink, std::string& error,
    bool start_at_title = false, std::uint32_t magic_codes = 0,
    std::span<const std::uint8_t> initial_save = {}, OwnedBootOptions boot = {});
// Isolated CPU adapter used by the opt-in backend and private qualification.
// Production passes normal_boot at the retail pre-INTRO boundary; the separate
// start_at_title fixture preparation must not be combined with normal_boot.
// Requires a CPU-quiesced canonical user ROM, canonical CPU presentation policy,
// an admitted mod namespace (or vanilla), one simulation owner and a separate
// immutable render consumer. Modded bootstrap admission is separately gated.
// The user ROM and local bootstrap are never included in distributed packages.
std::unique_ptr<OwnedGame> make_owned_game(std::span<const std::uint8_t> local_bootstrap,
    std::span<const std::uint8_t> canonical_rom, std::uint64_t epoch,
    PresentationMailbox& presentation, ConfirmedAudioSink sink, std::string& error,
    bool start_at_title = false, std::uint32_t magic_codes = 0,
    std::span<const std::uint8_t> initial_save = {}, OwnedBootOptions boot = {});
}
