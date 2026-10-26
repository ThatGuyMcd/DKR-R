#pragma once
#include "mods/legacy_runtime_session.hpp"
#include "mods/legacy_mod_launch.hpp"
#include "custom_tracks.hpp"
#include "mods/online_mod_music.hpp"
#include "mods/legacy_track_menu_adapter.hpp"
#include "game_payload.hpp"
namespace dkr::mods::online {
class RuntimeResources;
}

namespace dkr::runtime::legacy {
// Called with the game stopped. A null session restores zero-overhead stock
// routing. Keep the session alive until all runtime guest threads have joined.
void begin_session(std::shared_ptr<mods::RuntimeSession> session);
// Offline, validated launcher admission. Never called while guest threads run.
void begin_prepared(std::shared_ptr<const mods::PreparedModLaunch> prepared);
// Only pinned, session-only resources may use this path. It never reads or
// changes the launcher's offline enable list or offline save namespace.
void begin_online(std::shared_ptr<const mods::online::RuntimeResources> resources);
// Only after every normal bootstrap worker has joined. Transfers native
// asset/cache ownership into a separately constructed private replay world.
mods::Bytes online_bootstrap_checkpoint();
bool frozen_online_resources();
// Renderer reads the pinned catalogue, never the private replay world's
// mutable adapters. The catalogue stays alive until render workers retire.
std::optional<water::enhanced::Metadata> renderer_water_for_level(std::int32_t);
bool frozen_course_hook(const char*, std::uint8_t*, recomp_context*);
bool frozen_music_hook(const char*, std::uint8_t*, recomp_context*);
bool frozen_music_volume(std::uint8_t*, recomp_context*);
bool frozen_music_render(float*, std::size_t, unsigned);
std::shared_ptr<const mods::PreparedModLaunch> prepared_launch();
void begin_track_menu(bool allow_races = false);
void set_trophy_menu(std::vector<mods::TrackMenuCup>);
std::string selected_trophy();
std::string current_course();
std::string asset_inventory();
// Explicit roster seeding is reserved for development qualification.
void begin_character_roster(const std::array<std::string, 4>& selected);
void begin_character_menu();
void request_scene(const std::string& id, unsigned carrier);
std::string failure();

// Exclusive, private native participant for the owned online CPU. It reuses
// the reviewed normal adapters, but never their process-global mutable state,
// guest function table, offline Track Lab catalogue or failure/quit route.
class ModWorld {
  public:
    struct Restore {
        virtual ~Restore() = default;
    };
    ModWorld(std::shared_ptr<mods::RuntimeSession>, GamePayload private_calls, RecompiledEntrypoint animation_override,
             std::shared_ptr<const custom_tracks::PreparedTracks>, bool custom_ai,
             RecompiledEntrypoint private_random = nullptr,
             std::shared_ptr<const mods::online::MusicLibrary> music = nullptr,
             RecompiledEntrypoint private_texture_animation = nullptr, std::vector<mods::TrackMenuCup> cups = {});
    ~ModWorld();
    ModWorld(const ModWorld&) = delete;
    ModWorld& operator=(const ModWorld&) = delete;
    mods::Bytes checkpoint() const;
    std::unique_ptr<Restore> stage_checkpoint(mods::View) const;
    bool commit_checkpoint(std::unique_ptr<Restore>);
    void mix_music(std::uint8_t* ram, std::span<std::uint8_t> pcm, unsigned rate);
    // Noexcept outer boundary: all native exceptions and guest callback
    // faults return AFTER C++ locks/leases unwind; the caller then traps in C.
    int dispatch(const char*, std::uint8_t*, recomp_context*, const std::uint64_t*, unsigned, const std::uint32_t*,
                 unsigned, std::uint64_t&) noexcept;
    const mods::AssetBus& bus() const;
    const std::string& error() const;
    void select_course(const std::string&, unsigned);
    std::string selected_trophy() const;
    std::string current_course() const;
    std::string asset_inventory() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace dkr::runtime::legacy
