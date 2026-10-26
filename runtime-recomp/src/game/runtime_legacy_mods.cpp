#include "primary_heap.h"
#include "owned_vehicle_menu.hpp"
#include <array>
#include <cstring>
#include "resident_music.hpp"
#include "custom_records.hpp"
#include "runtime_legacy_mods.hpp"
#include "achievements.hpp"
#include "vehicle_binding.hpp"
#include "vehicle_world.hpp"
#include <bit>
#include "mods/magic_code_package.hpp"
#include "atomic_snapshot.hpp"
#include "custom_tracks.hpp"
#include "runtime_netplay.hpp"
#include "game_payload.hpp"
#include "mods/legacy_runtime_assets.hpp"
#include "mods/legacy_runtime_io.hpp"
#include "mods/legacy_track_menu_adapter.hpp"
#include "mods/legacy_character_roster.hpp"
#include "mods/legacy_runtime_character.hpp"
#include "mods/legacy_character_menu_render.hpp"
#include "mods/legacy_character_stage.hpp"
#include "mods/legacy_character_presentation.hpp"
#include "mods/legacy_heap_policy.hpp"
#include "mods/legacy_asset_capacity.hpp"
#include "mods/legacy_model_safety.hpp"
#include "mods/legacy_memory_safety.h"
#include "mods/legacy_checkpoint.hpp"
#include "mods/online_mod_runtime.hpp"
#include "mods/online_course_policy.hpp"
#if DKR_LEGACY_QUALIFICATION
#include "legacy_runtime_qualification.hpp"
#endif
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <bit>
#include <set>

extern "C" void osPiStartDma_recomp(std::uint8_t*, recomp_context*);
#if DKR_RUNTIME_HAS_RT64
extern "C" void dkr_track_select_background_cover(std::uint8_t*, recomp_context*);
#endif
extern "C" void dkr_character_select_animation_fraction(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_extend_table(std::uint8_t*, recomp_context*, std::uint32_t);
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t*, recomp_context*);
namespace dkr::runtime::legacy {
namespace {
struct MenuState {
    std::mutex mutex;
    mods::TrackMenuAdapter adapter;
    bool allow_races, catalog_ready = false;
    MenuState(std::vector<mods::Root> roots, bool races, std::vector<mods::TrackMenuCup> cups = {})
        : adapter(std::move(roots), races, std::move(cups)), allow_races(races) {}
    explicit MenuState(mods::TrackMenuAdapter value)
        : adapter(std::move(value)), allow_races(true), catalog_ready(true) {}
};
struct CharacterState {
    // A private sound/spawn callback can synchronously re-enter the same
    // adapter through its reviewed guest entry. The outer owner remains
    // exclusive; re-entrancy must not deadlock the native sidecar lock.
    std::recursive_mutex mutex;
    mods::CharacterRoster roster;
    mods::CharacterAiState ai;
    std::unique_ptr<mods::CharacterMenuAdapter> selector;
    mods::CharacterStage stage;
    mods::CharacterMenuRenderer hints;
    mods::CharacterPresentation presentation;
    bool menu_active = false;
    std::array<bool, 8> portrait_logged{};
    std::set<unsigned> shared_phase_headers; // immutable admission data, rebuilt after restore
    explicit CharacterState(std::shared_ptr<const mods::CharacterNamespace> assets) : roster(assets) {
        if (assets)
            for (const auto& c : assets->characters)
                if (c.behaviour.native_profile == 2) {
                    for (auto id : c.headers)
                        shared_phase_headers.insert(id);
                    shared_phase_headers.insert(c.stage_header);
                }
    }
};
struct MusicBinding {
    std::mutex mutex;
    mods::online::resident_music::State state;
    MusicBinding(std::shared_ptr<const mods::online::MusicLibrary> songs, bool rev80)
        : state(std::move(songs), rev80) {}
    explicit MusicBinding(mods::online::resident_music::State value) : state(std::move(value)) {}
};
struct WorldState {
    std::shared_ptr<vehicles::World> vehicles;
    AtomicSnapshot<mods::RuntimeSession> session;
    AtomicSnapshot<const mods::PreparedModLaunch> launch;
    AtomicSnapshot<MenuState> menu;
    AtomicSnapshot<CharacterState> characters;
    AtomicSnapshot<MusicBinding> music;
    GamePayload calls{};
    RecompiledEntrypoint animation = nullptr;
    RecompiledEntrypoint random = nullptr;
    RecompiledEntrypoint texture_animation = nullptr;
    std::shared_ptr<const custom_tracks::PreparedTracks> authored;
    std::vector<mods::TrackMenuCup> cups;
    bool private_owner = false, frozen_online = false, custom_ai = false;
    bool records_restored = false;
    mods::Bytes records_last;
    std::uint32_t pending_course_heap = 0;
};
WorldState ordinary_world;
std::atomic<bool> ordinary_music_enabled{false};
thread_local WorldState* private_world = nullptr;
struct OwnedVehicleDrawCallbacks {
    RecompiledEntrypoint frame = nullptr, text_width = nullptr;
};
thread_local OwnedVehicleDrawCallbacks owned_vehicle_draw_callbacks;
WorldState& current_world() {
    return private_world ? *private_world : ordinary_world;
}
template <class T, AtomicSnapshot<T> WorldState::*Member> struct WorldSlot {
    std::shared_ptr<T> load() const {
        return (current_world().*Member).load();
    }
    void store(std::shared_ptr<T> value) const {
        (current_world().*Member).store(std::move(value));
    }
};
const WorldSlot<mods::RuntimeSession, &WorldState::session> session;
const WorldSlot<const mods::PreparedModLaunch, &WorldState::launch> launch;
const WorldSlot<MenuState, &WorldState::menu> menu;
const WorldSlot<CharacterState, &WorldState::characters> characters;
const GamePayload* guest_payload() {
    return private_world ? &private_world->calls : active_payload();
}
std::size_t guest_memory_size() {
    return private_world ? 8U * 1024 * 1024 : recomp::mem_size;
}
bool owns_level(std::int32_t level) {
    const auto& world = current_world();
    if (!world.private_owner && !world.frozen_online)
        return custom_tracks::owns_level_id(level);
    if (!world.authored)
        return false;
    return std::ranges::any_of(world.authored->courses, [&](const auto& course) { return course.level_id == level; });
}
std::vector<custom_tracks::TrackSelectEntry> authored_courses() {
    const auto& world = current_world();
    return world.private_owner || world.frozen_online
               ? (world.authored ? world.authored->courses : std::vector<custom_tracks::TrackSelectEntry>{})
               : custom_tracks::track_select_entries();
}
#include "records_guest.inl"
bool frozen_courses() {
    return current_world().private_owner || current_world().frozen_online;
}
// Retail Tracks mode only lets a player change vehicle on a course already
// cleared in Adventure: func_80092188 tests courseFlags & RACE_CLEARED before
// it reads the stick, and trackmenu_setup_render greys the other vehicles out
// without it. No Adventure door leads to an authored course, so the vehicles
// its header allows were unreachable and only the default one could be raced.
// The flag is lent while the vehicle screen is open - from
// trackmenu_assets(TRACKMENU_TYPE_FREE) until trackmenu_assets with any other
// type - and taken back before load_level_game marks the save, so the EEPROM
// and the course grid never see it. An authored course has no Adventure
// progress to keep, so taking it back clears the flag unconditionally.
void lend_course_clear(std::uint8_t* rdram, const recomp_context& ctx, unsigned event, const std::uint32_t* fields) {
    constexpr std::int32_t RaceCleared = 2;
    constexpr std::uint32_t TrackmenuFree = 1, SettingsCourseFlags = 4;
    // trackmenu_assets, and as backstops menu_track_select_init,
    // load_level_game and menu_init: whichever way the screen is left.
    if (event != 12 && event != 0 && event != 14 && event != 15)
        return;
    const bool lend = event == 12 && static_cast<std::uint32_t>(ctx.r4) == TrackmenuFree;
    const auto courses = authored_courses();
    if (courses.empty())
        return;
    const auto payload = guest_payload();
    if (!payload || !payload->get_settings)
        return;
    const auto valid = [](std::uint32_t address) {
        return address >= 0x80000000U && address % 4 == 0 && address - 0x80000000U <= guest_memory_size() - 4;
    };
    const auto word = [&](std::uint32_t address) -> std::int32_t& {
        return MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(address)));
    };
    auto call = ctx;
    payload->get_settings(rdram, &call);
    const auto settings = static_cast<std::uint32_t>(call.r2);
    if (!valid(settings + SettingsCourseFlags))
        return;
    const auto flags = static_cast<std::uint32_t>(word(settings + SettingsCourseFlags));
    if (lend) {
        const auto preview = fields ? fields[static_cast<unsigned>(dkr::mods::MenuField::PreviewCarrier)] : 0U;
        if (!valid(preview))
            return;
        const auto level = word(preview);
        const bool authored =
            std::ranges::any_of(courses, [&](const auto& course) { return course.level_id == level; });
        if (authored && valid(flags + 4U * static_cast<std::uint32_t>(level))) {
            word(flags + 4U * static_cast<std::uint32_t>(level)) |= RaceCleared;
            std::fprintf(stderr, "[custom-tracks] level %d: Track Select vehicle choice opened\n",
                         static_cast<int>(level));
        }
        return;
    }
    for (const auto& course : courses) {
        const auto at = flags + 4U * static_cast<std::uint32_t>(course.level_id);
        if (course.level_id >= 0 && valid(at))
            word(at) &= ~RaceCleared;
    }
}
std::vector<mods::Root> frozen_roots(const mods::RuntimeSession& assets, const custom_tracks::PreparedTracks* courses) {
    auto roots = assets.tracks();
    if (courses)
        for (const auto& course : courses->courses) {
            mods::Root root;
            const auto identity = "dkrmap:" + course.id;
            root.content_id =
                mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(identity.data()), identity.size()));
            root.name = course.name;
            root.carrier = course.level_id;
            root.vehicles = course.vehicles;
            roots.push_back(std::move(root));
        }
    if (roots.size() > 512)
        throw mods::Error("Frozen custom course menu exceeds its bound.");
    return roots;
}
void course_budget(std::uint8_t* ram, bool install) {
    const auto& world = current_world();
    if (!frozen_courses())
        return;
    mods::CharacterMenuFields fields;
    fields.fill(0x80000000U);
    mods::CharacterMenuMemory guest({ram, guest_memory_size()}, fields);
    const auto assets = session.load();
    const bool rev80 = assets->boot_bank()->revision() == "us.v80";
    const auto table = rev80 ? 0x800DD920U : 0x800DD3B0U, lists = rev80 ? 0x80121770U : 0x801211F0U;
    const auto wanted = mods::online::course_display_budgets(world.authored.get());
    // Section 22 is read at global initialization AND on every level load,
    // including the first INTRO background after display heaps exist. A
    // repeated read is not new admission. Validate the boot-owned contract
    // without writing/resizing a live allocation; only cold boot may install.
    const bool allocated = guest.read(lists) != 0;
    for (unsigned i = 0; i < 4; ++i)
        if (guest.read(table + 4 * i) != wanted[i]) {
            if (allocated)
                throw mods::Error("Custom scene display-list contract differs from its existing allocation.");
            if (!install)
                throw mods::Error("Custom scene display-list contract changed after boot.");
            guest.write(table + 4 * i, wanted[i]);
        }
}
struct WorldScope {
    WorldState* before;
    explicit WorldScope(WorldState& value) : before(private_world) {
        private_world = &value;
    }
    ~WorldScope() {
        private_world = before;
    }
};
mods::CharacterPresentationCalls presentation_calls(const GamePayload& p) {
    return {p.asset_allocate,  p.menu_texture_load, p.sound_bank_relocate,
            p.sound_bank_play, p.sound_parameter,   p.sound_spatial_point};
}
void prepare_presentation(CharacterState& state, std::uint8_t* ram, const recomp_context& ctx, unsigned racer,
                          bool audio) {
    if (!state.selector || racer >= 8 || state.roster.active(racer).empty())
        return;
    const auto p = guest_payload();
    if (!p)
        throw mods::Error("Missing private presentation revision.");
    const auto assets = session.load();
    const auto& entries = state.selector->entries();
    const auto found = std::ranges::find(entries, state.roster.active(racer), &mods::AllocatedCharacter::id);
    if (found == entries.end())
        throw mods::Error("Active presentation character was not admitted.");
    const auto i = static_cast<unsigned>(found - entries.begin());
    state.presentation.prepare({ram, guest_memory_size()}, ctx, presentation_calls(*p), entries, i,
                               assets->character_race_sample_address(i), audio);
}
std::mutex error_mutex;
std::string last_error;
[[noreturn]] void fail(const char* error) {
    if (private_world)
        throw mods::Error(error);
    {
        std::lock_guard lock(error_mutex);
        last_error = error;
    }
    std::fprintf(stderr, "[legacy][fatal] %s\n", error);
    // Never return a fake DMA success or throw an importer exception through
    // the guest scheduler. Its existing termination boundary owns teardown.
    ultramodern::quit();
    throw ultramodern::thread_terminated{};
}
} // namespace
void begin_session(std::shared_ptr<mods::RuntimeSession> value) {
    {
        std::lock_guard lock(error_mutex);
        last_error.clear();
    }
    launch.store(nullptr);
    characters.store(nullptr);
    menu.store(nullptr);
    session.store(std::move(value));
    ordinary_world.frozen_online = false;
    ordinary_world.authored.reset();
    ordinary_world.custom_ai = false;
    ordinary_world.pending_course_heap = 0;
    ordinary_world.records_restored = false;
    ordinary_world.records_last.clear();
    ordinary_world.cups.clear();
    ordinary_music_enabled = false;
    ordinary_world.music.store(nullptr);
    const auto assets = session.load();
    const auto cars = mods::vehicles::resources_for(assets.get());
    ordinary_world.vehicles = cars && !cars->library.packages.empty()
                                  ? std::make_shared<vehicles::World>(cars, assets->boot_bank()->revision() == "us.v80")
                                  : nullptr;
    mods::vehicles::publish_render_resources(cars);
}
void begin_online(std::shared_ptr<const mods::online::RuntimeResources> resources) {
    if (!resources)
        throw mods::Error("Online mod admission has no pinned game resources.");
    begin_session(resources->new_session());
    ordinary_world.frozen_online = true;
    ordinary_world.authored = resources->authored();
    ordinary_world.custom_ai = resources->profile()->manifest.custom_ai;
    ordinary_world.music.store(
        std::make_shared<MusicBinding>(resources->music(), session.load()->boot_bank()->revision() == "us.v80"));
    ordinary_music_enabled = true;
    auto roots = frozen_roots(*session.load(), ordinary_world.authored.get());
    if (!roots.empty()) {
        auto prepared_menu = std::make_shared<MenuState>(std::move(roots), true);
        prepared_menu->catalog_ready = true;
        menu.store(std::move(prepared_menu));
    }
    if (session.load()->characters())
        begin_character_menu();
}
void begin_prepared(std::shared_ptr<const mods::PreparedModLaunch> prepared) {
    begin_session(prepared ? prepared->session : nullptr);
    if (!prepared || (!prepared->session && !prepared->code))
        return;
    if (prepared->save_path.empty() || prepared->pak_directory.empty())
        throw mods::Error("Runtime admission requires a frozen personal save owner.");
    if (prepared->session && !prepared->session->tracks().empty())
        begin_track_menu(true);
    if (prepared->session && prepared->session->characters())
        begin_character_menu();
    launch.store(std::move(prepared));
}
std::shared_ptr<const mods::PreparedModLaunch> prepared_launch() {
    return launch.load();
}
bool frozen_online_resources() {
    return frozen_courses();
}
std::optional<water::enhanced::Metadata> renderer_water_for_level(std::int32_t level) {
    if (!ordinary_world.frozen_online)
        return custom_tracks::water_for_level(level);
    const auto& authored = ordinary_world.authored;
    if (!authored)
        return {};
    for (const auto& course : authored->courses)
        if (course.level_id == level)
            for (const auto& track : authored->tracks)
                if (track.id == course.id)
                    return track.water;
    return {};
}
bool frozen_course_hook(const char* hook, std::uint8_t* ram, recomp_context* ctx) {
    if (!frozen_courses())
        return false;
    try {
        auto& world = current_world();
        const std::string_view name(hook);
        if (name == "prepare_memory") {
            mods::online::resident_music::PreparationScope ordinary_load(!world.private_owner);
            world.pending_course_heap = mods::online::course_heap(world.authored.get(), static_cast<unsigned>(ctx->r4));
            if (const auto music = world.music.load()) {
                std::lock_guard lock(music->mutex);
                mods::CharacterMenuFields fields;
                fields.fill(0x80000000U);
                mods::CharacterMenuMemory guest({ram, guest_memory_size()}, fields);
                music->state.load(guest, static_cast<unsigned>(ctx->r4));
            }
        } else if (name == "prepare_level")
            course_budget(ram, false);
        else if (name == "track_heap") {
            if (world.pending_course_heap && std::uint32_t(ctx->r21) != custom_tracks::kRetailTrackHeap)
                throw mods::Error("Frozen custom collision heap hook ABI changed.");
            if (world.pending_course_heap)
                ctx->r21 = world.pending_course_heap;
            world.pending_course_heap = 0;
        } else
            throw mods::Error("Unknown frozen custom-course hook.");
        return true;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}
bool frozen_music_hook(const char* operation, std::uint8_t* ram, recomp_context* ctx) {
    if (!frozen_courses())
        return false;
    try {
        if (const auto music = current_world().music.load()) {
            std::lock_guard lock(music->mutex);
            mods::CharacterMenuFields fields;
            fields.fill(0x80000000U);
            mods::CharacterMenuMemory guest({ram, guest_memory_size()}, fields);
            const std::string_view name(operation);
            if (name == "loaded")
                music->state.loaded(guest, *ctx);
            else if (name == "started")
                music->state.started(guest);
            else if (name == "tick")
                music->state.tick(guest);
            else
                throw mods::Error("Unknown frozen music callback.");
        }
        return true;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}
bool frozen_music_volume(std::uint8_t* ram, recomp_context* ctx) {
    if (!frozen_courses())
        return false;
    try {
        const auto music = current_world().music.load();
        if (!music)
            return false;
        std::lock_guard lock(music->mutex);
        mods::CharacterMenuFields fields;
        fields.fill(0x80000000U);
        mods::CharacterMenuMemory guest({ram, guest_memory_size()}, fields);
        return music->state.volume(guest, *ctx);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}
bool frozen_music_render(float* output, std::size_t frames, unsigned rate) {
    if (!ordinary_world.frozen_online)
        return false;
    if (ordinary_music_enabled)
        if (const auto music = ordinary_world.music.load()) {
            std::lock_guard lock(music->mutex);
            music->state.render(output, frames, rate);
        }
    return true; // suppress stale offline music, including owned confirmed PCM
}
void begin_character_roster(const std::array<std::string, 4>& selected) {
    auto active = session.load();
    if (!active || !active->characters())
        throw mods::Error("No characters are prepared for this session.");
    auto state = std::make_shared<CharacterState>(active->characters());
    for (unsigned i = 0; i < 4; ++i)
        state->roster.request(i, selected[i]);
    characters.store(std::move(state));
}
void begin_character_menu() {
    auto active = session.load();
    if (!active || !active->characters())
        throw mods::Error("No character library is admitted.");
    auto state = std::make_shared<CharacterState>(active->characters());
    state->selector = std::make_unique<mods::CharacterMenuAdapter>(active->characters());
    characters.store(std::move(state));
}
void begin_track_menu(bool allow_races) {
    auto active = session.load();
    if (!active)
        throw mods::Error("No catalogue is admitted for the native menu.");
    menu.store(std::make_shared<MenuState>(active->tracks(), allow_races));
}
void set_trophy_menu(std::vector<mods::TrackMenuCup> cups) {
    ordinary_world.cups = std::move(cups);
    if (ordinary_world.cups.empty())
        return;
    const auto assets = ordinary_world.session.load();
    auto roots = assets ? frozen_roots(*assets, ordinary_world.authored.get()) : std::vector<mods::Root>{};
    auto state = std::make_shared<MenuState>(std::move(roots), true, ordinary_world.cups);
    // Frozen online IDs already exist at bootstrap. Offline Track Lab IDs
    // are appended at retail initialization, so rebuild once at menu entry.
    state->catalog_ready = ordinary_world.frozen_online;
    ordinary_world.menu.store(std::move(state));
}
#include "mods/sdk2_asset_inventory.inl"
std::string asset_inventory() {
    return sdk2_asset_inventory(session.load());
}
std::string current_course() {
    const auto active = session.load();
    return active ? active->current_content() : std::string{};
}
std::string selected_trophy() {
    const auto state = menu.load();
    if (!state)
        return {};
    std::lock_guard lock(state->mutex);
    return state->adapter.selected_cup();
}
void request_scene(const std::string& id, unsigned carrier) {
    // Appended .dkrmap level IDs load through the custom-track asset hooks.
    // begin_scene detects them and returns a mounted legacy bank to stock.
    if (owns_level(static_cast<std::int32_t>(carrier)))
        return;
    auto active = session.load();
    if (!active) {
        if (id.empty())
            return;
        throw mods::Error("No custom-content session is prepared.");
    }
    active->request(id, carrier);
}
std::string failure() {
    std::lock_guard lock(error_mutex);
    return last_error;
}
} // namespace dkr::runtime::legacy
extern "C" int dkr_legacy_texture_animation(std::uint8_t* ram, recomp_context* ctx,
                                            dkr::mods::OriginalPiHandler texture) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || state->shared_phase_headers.empty())
        return 0;
    try {
        std::lock_guard lock(state->mutex);
        dkr::mods::CharacterMenuFields fields;
        fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory g({ram, legacy::guest_memory_size()}, fields);
        const auto object = std::uint32_t(ctx->r4);
        if (object > UINT32_MAX - 0x68)
            throw dkr::mods::Error("Animated object address overflow.");
        if (!state->shared_phase_headers.contains(g.read(object + 0x2c, 2)))
            return 0;
        dkr::mods::animate_character_textures({ram, legacy::guest_memory_size()}, *ctx, texture);
        return 1;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" void dkr_legacy_character_event(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                           std::uint32_t roster_address) {
    using namespace dkr::runtime;
    if (event >= 200) {
        try {
            const auto cars = legacy::current_world().vehicles;
            if (event == 200)
                ctx->r2 = cars ? cars->render_begin({rdram, legacy::guest_memory_size()}, roster_address) : 0;
            else if (event == 201) {
                if (cars)
                    cars->render_end({rdram, legacy::guest_memory_size()}, roster_address, unsigned(ctx->r8));
            } else if (event == 202 || event == 203) {
                if (cars)
                    cars->wheel_scope({rdram, legacy::guest_memory_size()}, roster_address, event == 202);
            } else if (event == 204) {
                if (cars)
                    cars->advance_wheels({rdram, legacy::guest_memory_size()}, roster_address,
                                         std::bit_cast<float>(std::uint32_t(ctx->r6)));
            } else
                throw dkr::mods::Error("Unknown custom vehicle render event.");
            return;
        } catch (const ultramodern::thread_terminated&) {
            throw;
        } catch (const std::exception& error) {
            legacy::fail(error.what());
        }
    }
    auto state = legacy::characters.load();
    if (!state)
        return;
    try {
        std::lock_guard lock(state->mutex);
        const auto original = ctx->r4;
        if (event == 0 && state->stage.redirect_spawn({rdram, legacy::guest_memory_size()}, *ctx))
            return;
        dkr::mods::dispatch_character_event(state->roster, {rdram, legacy::guest_memory_size()}, *ctx, event,
                                            roster_address, state->ai.spawn);
        // Allocate only committed participants, on the fixed guest logic
        // path. Rendering/replay suppression cannot change the heap order.
        if (event == 1 && state->selector)
            for (unsigned i = 0; i < unsigned(ctx->r4); ++i)
                legacy::prepare_presentation(*state, rdram, *ctx, i, true);
        if (event == 0 && ctx->r4 != original)
            std::fprintf(stderr, "[legacy][character] racer root %u -> %u\n", unsigned(original), unsigned(ctx->r4));
        if (event == 1)
            for (unsigned i = 0; i < unsigned(ctx->r4); ++i)
                std::fprintf(stderr, "[legacy][character] P%u committed %s\n", i + 1,
                             state->roster.active(i).empty() ? "stock" : state->roster.active(i).c_str());
        if (event == 1 || event == 2)
            state->portrait_logged = {};
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" void dkr_legacy_character_ai_event(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                              dkr::mods::OriginalPiHandler random) {
    using namespace dkr::runtime;
    // Cars have their own primary-racer lifecycle. Do not gate this on a
    // character selector, the custom-AI preference, or an offline-only path.
    try {
        if (const auto cars = legacy::current_world().vehicles)
            cars->race({rdram, legacy::guest_memory_size()}, *ctx, event);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }

    const auto state = legacy::characters.load();
    if (!state || !state->selector ||
        (legacy::frozen_courses() ? !legacy::current_world().custom_ai : netplay::session().active()))
        return;
    try {
        std::array<std::uint8_t, 10> pool{0, 9, 1, 5, 3, 2, 7, 4};
        unsigned count = 8;
        if (event == 1) {
            const auto p = legacy::guest_payload();
            if (!p)
                throw dkr::mods::Error("Missing race AI revision.");
            auto drum = *ctx, tt = *ctx;
            // Same scratch discipline as the AI RNG; do not alias the live
            // track_setup_racers caller frame or its float registers.
            const auto sp = std::uint32_t(ctx->r29);
            if ((sp & 0xe0000000U) != 0x80000000U || (sp & 0x1fffffffU) < 0x100 || (sp & 7))
                throw dkr::mods::Error("Invalid race AI unlock stack.");
            drum.r29 = tt.r29 = static_cast<std::int32_t>(sp - 0x100);
            drum.f_odd = drum.mips3_float_mode ? &drum.f1.u32l : &drum.f0.u32h;
            tt.f_odd = tt.mips3_float_mode ? &tt.f1.u32l : &tt.f0.u32h;
            p->unlock_drumstick(rdram, &drum);
            p->unlock_tt(rdram, &tt);
            if (drum.r2)
                pool[count++] = 6;
            if (tt.r2)
                pool[count++] = 8;
        }
        std::lock_guard lock(state->mutex);
        dkr::mods::dispatch_character_ai_event(state->roster, state->ai, {rdram, legacy::guest_memory_size()}, *ctx,
                                               event, std::span<const std::uint8_t>(pool.data(), count), random);
        if (event == 1)
            for (unsigned i = 0; i < 8; ++i)
                legacy::prepare_presentation(*state, rdram, *ctx, i, true);
        if (event == 0 || event == 1)
            state->portrait_logged = {};
        if (event == 1)
            for (unsigned i = state->ai.humans; i < state->ai.racers; ++i)
                std::fprintf(stderr, "[legacy][character] AI racer %u: %s\n", i + 1,
                             state->roster.active(i).empty() ? "stock" : state->roster.active(i).c_str());
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" int dkr_legacy_character_menu(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                         const std::uint32_t* fields) {
    using namespace dkr::runtime;
    auto state = legacy::characters.load();
    if (!state || !state->selector)
        return 0;
    try {
        if (!fields || event > 8)
            throw dkr::mods::Error("Invalid character-menu hook ABI.");
        dkr::mods::CharacterMenuFields addresses;
        std::copy_n(fields, addresses.size(), addresses.begin());
        dkr::mods::CharacterMenuMemory guest({rdram, legacy::guest_memory_size()}, addresses);
        const auto p = legacy::guest_payload();
        if (!p)
            throw dkr::mods::Error("Character menu has no selected ROM revision.");
        const dkr::mods::CharacterStageCalls stage{p->stage_spawn,
                                                   p->stage_free,
                                                   p->stage_music_fraction,
                                                   p->stage_particles,
                                                   legacy::private_world ? legacy::private_world->animation
                                                                         : dkr_character_select_animation_fraction,
                                                   p->stage_camera};
        if (event == 0) {
            auto drum = *ctx, tt = *ctx;
            p->unlock_drumstick(rdram, &drum);
            p->unlock_tt(rdram, &tt);
            {
                std::lock_guard lock(state->mutex);
                state->selector->reset();
                state->selector->set_unlocks(drum.r2 != 0, tt.r2 != 0);
            }
            state->stage.initialize({rdram, legacy::guest_memory_size()}, addresses, *ctx, stage,
                                    state->selector->entries());
            state->menu_active = true;
            const auto active = legacy::session.load();
            std::vector<std::uint32_t> samples;
            for (unsigned i = 0; i < state->selector->entries().size(); ++i)
                samples.push_back(active->character_race_sample_address(i));
            state->presentation.initialize({rdram, legacy::guest_memory_size()}, *ctx, legacy::presentation_calls(*p),
                                           state->selector->entries(), samples);
            std::fprintf(stderr, "[legacy][stage] %zu characters enabled, %zu visible stage actors\n",
                         state->selector->entries().size(),
                         std::min<std::size_t>(dkr::mods::CustomStageSlots, state->selector->entries().size()));
        } else if (event == 2) {
            dkr::mods::CharacterMenuView view;
            {
                std::lock_guard lock(state->mutex);
                view = state->selector->view(guest);
            }
            const dkr::mods::CharacterMenuDrawCalls draw{
                p->asset_allocate, p->asset_release, p->menu_texture_load, p->menu_texture_free, p->menu_texture_draw,
                p->menu_font,      p->menu_colour,   p->menu_background,   p->menu_text,         p->menu_render_reset};
            state->hints.draw_hint({rdram, legacy::guest_memory_size()}, addresses, *ctx, draw,
                                   state->selector->entries(), view);
        } else if (event == 6) {
            dkr::mods::CharacterMenuView view;
            {
                std::lock_guard lock(state->mutex);
                view = state->selector->view(guest);
            }
            return state->stage.update({rdram, legacy::guest_memory_size()}, addresses, *ctx, stage, view);
        } else if (event == 7) {
            // Checked call site keeps the physical cursor index in s0.
            const auto player = static_cast<unsigned>(ctx->r16);
            if (player >= 4)
                throw dkr::mods::Error("Native stage cursor is invalid.");
            std::lock_guard lock(state->mutex);
            if (state->selector->custom(player))
                ctx->r2 = static_cast<gpr>(-1);
        } else if (event == 3) {
            state->menu_active = false;
            state->stage.release({rdram, legacy::guest_memory_size()}, *ctx, stage);
        } else if (event == 8) {
            if (!state->menu_active)
                return 0;
            const auto handle = static_cast<std::uint32_t>(ctx->r5);
            const auto handles = guest.address(dkr::mods::CharacterMenuField::SoundHandles);
            if (handle < handles || handle >= handles + 16 || (handle - handles) % 4)
                return 0;
            const unsigned player = (handle - handles) / 4;
            std::optional<std::size_t> selected;
            {
                std::lock_guard lock(state->mutex);
                selected = state->selector->custom(player);
            }
            if (!selected)
                return 0;
            const auto sound = static_cast<unsigned>(ctx->r4);
            constexpr unsigned bases[]{0x87, 0x93, 0x19e};
            unsigned action = 3;
            for (unsigned a = 0; a < 3; ++a)
                if (sound >= bases[a] && sound < bases[a] + 10)
                    action = a;
            if (action == 3)
                return 0; // unrelated native audio retains its path
            const auto& cue = state->selector->entries().at(*selected).audio.cues[action];
            if (!cue.sound) {
                guest.write(handle, 0);
                return 1;
            }
            const auto active = legacy::session.load();
            const auto bank = state->presentation.selection_bank(
                {rdram, legacy::guest_memory_size()}, *ctx, legacy::presentation_calls(*p), state->selector->entries(),
                static_cast<unsigned>(*selected), active->character_sample_address(static_cast<unsigned>(*selected)));
            auto play = *ctx;
            play.r4 = static_cast<gpr>(static_cast<std::int32_t>(bank));
            play.r5 = cue.sound;
            play.r6 = cue.priority;
            play.r7 = static_cast<gpr>(static_cast<std::int32_t>(handle));
            p->sound_bank_play(rdram, &play);
            const auto voice = guest.read(handle);
            if (voice) {
                auto param = *ctx;
                param.r4 = static_cast<gpr>(static_cast<std::int32_t>(voice));
                param.r5 = 8;
                param.r6 = cue.volume * 256;
                p->sound_parameter(rdram, &param);
                param = *ctx;
                param.r4 = static_cast<gpr>(static_cast<std::int32_t>(voice));
                param.r5 = 16;
                param.r6 = std::bit_cast<std::uint32_t>(cue.pitch / 100.0f);
                p->sound_parameter(rdram, &param);
            }
            return 1;
        } else if (event == 4) {
            std::lock_guard lock(state->mutex);
            state->selector->commit(guest, state->roster, static_cast<unsigned>(ctx->r4));
        } else {
            if (event == 5) {
                std::lock_guard lock(state->mutex);
                if (!state->selector->has_custom())
                    return 0;
            }
            if (!p->filtered_cheats)
                throw dkr::mods::Error("Character menu cheat policy is unavailable.");
            auto cheats = *ctx;
            p->filtered_cheats(rdram, &cheats);
            const bool duplicates = (static_cast<std::uint32_t>(cheats.r2) & (1U << 22)) != 0;
            std::vector<unsigned> sounds;
            auto previous = *state->selector;
            std::array<unsigned, 4> previous_indices{};
            for (unsigned i = 0; i < 4; ++i)
                previous_indices[i] = guest.get(dkr::mods::CharacterMenuField::NativeIndices, i, 1);
            const auto previous_music = guest.get(dkr::mods::CharacterMenuField::CurrentMusic);
            {
                std::lock_guard lock(state->mutex);
                if (event == 1)
                    sounds = state->selector->input(guest, duplicates);
                else {
                    const bool moved = state->selector->native_move(guest, static_cast<unsigned>(ctx->r4),
                                                                    static_cast<std::uint32_t>(ctx->r5),
                                                                    static_cast<unsigned>(ctx->r6), duplicates);
                    const auto stack = static_cast<std::uint32_t>(ctx->r29);
                    if (stack > 0xffffffffU - 16)
                        throw dkr::mods::Error("Character sound argument overflow.");
                    sounds.push_back(moved ? static_cast<unsigned>(ctx->r7) : guest.read(stack + 16));
                }
            }
            if (event == 1 && !state->stage.synchronize({rdram, legacy::guest_memory_size()}, *ctx, stage,
                                                        state->selector->entries(), state->selector->view(guest))) {
                std::lock_guard lock(state->mutex);
                *state->selector = std::move(previous);
                for (unsigned i = 0; i < 4; ++i)
                    guest.set(dkr::mods::CharacterMenuField::NativeIndices, previous_indices[i], i, 1);
                guest.set(dkr::mods::CharacterMenuField::CurrentMusic, previous_music);
                sounds = {0x15C};
                std::fprintf(stderr,
                             "[legacy][stage] Browsing retained the prior roster: native model budget unavailable\n");
            }
            for (auto id : sounds) {
                if (!p->menu_sound_play)
                    throw dkr::mods::Error("Character menu sound callback is unavailable.");
                auto sound = *ctx;
                sound.r4 = id;
                sound.r5 = 0;
                p->menu_sound_play(rdram, &sound);
            }
            if (event == 5)
                return 1;
        }
        return 0;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" void dkr_legacy_heap_capacity(std::uint8_t*, recomp_context* ctx) {
    const auto admitted = dkr::mods::legacy_heap_end(std::uint32_t(ctx->r15), dkr::runtime::legacy::guest_memory_size(),
                                                     bool(dkr::runtime::legacy::session.load()));
    const auto end = dkr_primary_heap_end(admitted, dkr::runtime::legacy::guest_memory_size());
    if (end != std::uint32_t(ctx->r15))
        std::fprintf(
            stderr,
            "[memory][primary-pool] end=0x%08X mapped=%zu; deterministic residency headroom; deferred frees unchanged\n",
            end, dkr::runtime::legacy::guest_memory_size());
    ctx->r15 = static_cast<gpr>(static_cast<std::int32_t>(end));
}
extern "C" std::uint32_t dkr_legacy_course_component(std::uint8_t*, recomp_context*, unsigned site,
                                                     std::uint32_t native_value) {
    try {
        const auto assets = dkr::runtime::legacy::session.load();
        if (site >= 3)
            throw dkr::mods::Error("Unknown custom-course executable component.");
        return assets ? assets->course_component(site, native_value) : native_value;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        dkr::runtime::legacy::fail(error.what());
    }
}
extern "C" std::uint32_t dkr_legacy_racer_stat(std::uint8_t* ram, recomp_context*, unsigned stat, std::uint32_t racer,
                                               std::uint32_t native_bits) {
    try {
        if (stat >= 3)
            throw dkr::mods::Error("Unknown custom-racer parameter.");
        const auto state = dkr::runtime::legacy::characters.load();
        if (!state)
            return native_bits;
        std::lock_guard lock(state->mutex);
        return dkr::mods::character_stat(state->roster, {ram, dkr::runtime::legacy::guest_memory_size()}, stat, racer,
                                         native_bits);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        dkr::runtime::legacy::fail(error.what());
    }
}
extern "C" std::uint32_t dkr_legacy_asset_cache_capacity(std::uint8_t*, recomp_context*, unsigned kind) {
    using namespace dkr::runtime;
    if (kind >= 4)
        legacy::fail("Unknown legacy asset cache kind.");
    const auto state = legacy::session.load();
    const auto bank = state ? state->boot_bank() : nullptr;
    if (!bank || !bank->augmented())
        return dkr::mods::legacy_asset_cache_capacities(false, 0, 0, 0, 0)[kind];
    return dkr::mods::legacy_asset_cache_capacities(
        true, static_cast<std::uint32_t>(bank->record_count(2)), static_cast<std::uint32_t>(bank->record_count(4)),
        static_cast<std::uint32_t>(bank->record_count(12)), static_cast<std::uint32_t>(bank->record_count(29)))[kind];
}
extern "C" void dkr_legacy_asset_cache_guard(std::uint8_t* ram, recomp_context* ctx, unsigned kind,
                                             std::uint32_t index) {
    if (index >= dkr_legacy_asset_cache_capacity(ram, ctx, kind))
        dkr::runtime::legacy::fail(
            "Asset cache capacity exceeded; stopped before guest-memory corruption. Please report the active mods and runtime.log.");
}
extern "C" void dkr_legacy_racer_spawn_failed(std::uint8_t*, recomp_context*) {
    dkr::runtime::legacy::fail(
        "Race setup could not allocate a racer; stopped before a null guest-address write. Please report the active mods and runtime.log.");
}
extern "C" int dkr_legacy_model_safety(std::uint8_t* ram, recomp_context*, unsigned operation, std::uint32_t address) {
    using namespace dkr::runtime;
    const std::span<const std::uint8_t> memory{ram, legacy::guest_memory_size()};
    const auto check = dkr::mods::check_model_operation(memory, operation, address);
    if (check.failure) {
        std::fprintf(
            stderr,
            "[legacy][model-reference] operation=%u failed=%s object=%08X header=%08X slots=%08X count=%u index=%u instance=%08X model=%08X normals=%08X\n",
            operation, check.failure, check.object, check.header, check.slots, check.count, check.index, check.instance,
            check.model, check.normals);
        legacy::fail(
            "Invalid model allocation detected before a guest-memory access. The game was stopped safely; installed mods and saves were retained. Please send runtime.log.");
    }
    return check.no_shading ? 1 : 0;
}
extern "C" std::uint32_t dkr_legacy_memory_service(std::uint8_t* ram, recomp_context*, unsigned operation,
                                                   unsigned revision, std::uint32_t a, std::uint32_t b) {
    using namespace dkr::runtime;
    if (revision != 77 && revision != 80)
        legacy::fail("Unreviewed allocator safety revision.");
    const dkr_memory_view g{ram, legacy::guest_memory_size(), revision};
    const auto report = [&](const char* reason) {
        const auto m = dkr_memory_measure(g);
        std::fprintf(
            stderr,
            "[memory][safety] reason=%s revision=%u request=%u caller=%08X free=%u largest=%u slots=%u/%u queue=%u peak=%u valid=%d\n",
            reason, revision, a, b, m.free_bytes, m.largest_free, m.used_slots, m.max_slots, m.queue, m.high_water,
            m.valid);
        // Bounded allocation metadata only; no saves, ROM data or audio samples.
        if (m.valid) {
            const auto base = dkr_memory_read(g, dkr_memory_pools(g) + 8, 4);
            unsigned i = 0;
            for (unsigned n = 0; n < 12 && i != 65535U; ++n) {
                const auto s = base + 20U * i;
                std::fprintf(stderr, "[memory][slot] index=%u address=%08X bytes=%u flags=%u tag=%08X\n", i,
                             dkr_memory_read(g, s, 4), dkr_memory_read(g, s + 4, 4), dkr_memory_read(g, s + 8, 2),
                             dkr_memory_read(g, s + 16, 4));
                i = dkr_memory_read(g, s + 12, 2);
            }
        }
        std::fflush(stderr);
    };
    std::uint32_t value = 0;
    switch (operation) {
    case 0: {
        const auto d = dkr_memory_descriptor(g);
        for (unsigned i = 0; i < 32; i += 4)
            dkr_memory_write(g, d + i, 0, 4);
        dkr_memory_write(g, dkr_memory_count_address(g), 0, 4);
        const auto state = legacy::session.load();
        const auto bank = state ? state->boot_bank() : nullptr;
        if (!bank || !bank->augmented())
            return 1600;
        std::uint64_t records = 0;
        for (auto section : {2, 4, 12, 29})
            records += bank->record_count(section);
        return static_cast<unsigned>(std::clamp<std::uint64_t>((records * 2U + 15U) & ~15ULL, 4096U, 8192U));
    }
    case 1:
        value = dkr_memory_pool(g, 0);
        if (value != DKR_MEMORY_ERROR)
            return dkr_memory_reserve_bytes(value);
        break;
    case 2:
        if (!a)
            return 0;
        value = dkr_memory_install(g, a, dkr_memory_read(g, dkr_memory_pools(g), 4)) ? 0 : DKR_MEMORY_ERROR;
        break;
    case 3:
        value = dkr_memory_queue(g);
        break;
    case 4:
        value = dkr_memory_enqueue(g, a);
        break;
    case 5:
        value = dkr_memory_acquire(g, a);
        break;
    case 6:
        value = dkr_memory_release(g, a);
        break;
    case 7:
        value = dkr_memory_pool(g, a);
        break;
    case 8:
        value = dkr_memory_mutation(g, a, b) ? 0 : DKR_MEMORY_ERROR;
        break;
    case 9: {
        const auto d = dkr_memory_descriptor(g);
        if (dkr_memory_read(g, d, 4) == DKR_MEMORY_MAGIC) {
            const auto n = dkr_memory_read(g, d + 24, 4);
            dkr_memory_write(g, d + 24, n + 1, 4);
            if (n >= 4)
                return 0; // repeated pressure never floods the runtime log
        }
        report("transition-fallback");
        return 0;
    }
    case 10:
        report("required-allocation-failed");
        legacy::fail(
            "Not enough guest memory for a required game resource. Game stopped safely; mods and saves were retained. Please send runtime.log.");
    default:
        value = DKR_MEMORY_ERROR;
        break;
    }
    if (value == DKR_MEMORY_ERROR) {
        report("invalid-allocator-or-free-queue");
        legacy::fail(
            "Invalid guest allocator metadata detected before a memory write. Game stopped safely; mods and saves were retained. Please send runtime.log.");
    }
    return value;
}
extern "C" std::uint32_t dkr_legacy_character_portrait_lookup(std::uint8_t* rdram, recomp_context* ctx,
                                                              std::uint32_t racer_base) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load(); // Hold ownership through guest callbacks.
    if (!state || !state->selector)
        return 0;
    try {
        const auto p = legacy::guest_payload();
        if (!p)
            throw dkr::mods::Error("Missing portrait revision.");
        auto settings = *ctx;
        p->get_settings(rdram, &settings);
        const auto base = std::uint32_t(settings.r2);
        // Native sites pass Settings + racer_index * sizeof(Racer). The
        // original character lookup at +0x59 is otherwise left intact.
        if (racer_base < base || (racer_base - base) % 0x18 || (racer_base - base) / 0x18 >= 8)
            return 0;
        std::lock_guard lock(state->mutex);
        const auto racer = (racer_base - base) / 0x18;
        legacy::prepare_presentation(*state, rdram, *ctx, racer, false);
        const auto cell = state->presentation.portrait(state->roster, state->selector->entries(), racer);
        if (cell && !state->portrait_logged[racer]) {
            const auto& entries = state->selector->entries();
            const auto found = std::find_if(entries.begin(), entries.end(),
                                            [&](const auto& c) { return c.id == state->roster.active(racer); });
            if (found != entries.end())
                std::fprintf(
                    stderr, "[legacy][portrait] P%u %s: %s, private texture %u\n", racer + 1, found->name.c_str(),
                    found->custom_portrait ? "supplied custom face" : "patch retains original face", found->portrait);
            state->portrait_logged[racer] = true;
        }
        return cell;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" unsigned dkr_legacy_character_race_sound(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t racer,
                                                    unsigned sound) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return sound;
    try {
        dkr::mods::CharacterMenuFields fields;
        fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram, legacy::guest_memory_size()}, fields);
        const auto player = dkr::mods::character_presentation_slot(guest.read(racer, 2), guest.read(racer + 2, 1)),
                   native_character = guest.read(racer + 3, 1);
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state, rdram, *ctx, player, true);
        return state->presentation.sound(state->roster, state->selector->entries(), player, native_character, sound);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" void dkr_legacy_character_hud_bind(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t hud,
                                              std::uint32_t racer) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return;
    try {
        dkr::mods::CharacterMenuFields fields;
        fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram, legacy::guest_memory_size()}, fields);
        // Explicit Adventure slots remain human; AI uses its participant
        // index, never the shared signed -1 player sentinel.
        const auto player =
            racer < 4 ? racer : dkr::mods::character_presentation_slot(guest.read(racer, 2), guest.read(racer + 2, 1));
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state, rdram, *ctx, player, false);
        state->presentation.bind_hud(std::uint32_t(ctx->r29), hud, state->roster, state->selector->entries(), player);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" void dkr_legacy_character_hud_unbind(std::uint8_t*, recomp_context* ctx) {
    const auto state = dkr::runtime::legacy::characters.load();
    if (!state)
        return;
    std::lock_guard lock(state->mutex);
    state->presentation.unbind_hud(std::uint32_t(ctx->r29));
}
extern "C" std::uint32_t dkr_legacy_character_hud_lookup(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t stack,
                                                         std::uint32_t hud) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return 0;
    try {
        dkr::mods::CharacterMenuFields fields;
        fields.fill(0x80000000U);
        dkr::mods::CharacterMenuMemory guest({rdram, legacy::guest_memory_size()}, fields);
        const auto sprite = guest.read(hud + 6, 2);
        std::lock_guard lock(state->mutex);
        return state->presentation.hud_lookup(stack, hud, sprite);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" int dkr_legacy_character_play_sound(std::uint8_t* rdram, recomp_context* ctx, unsigned kind) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return 0;
    try {
        const auto p = legacy::guest_payload();
        if (!p)
            throw dkr::mods::Error("Missing custom audio revision.");
        return state->presentation.play({rdram, legacy::guest_memory_size()}, *ctx, legacy::presentation_calls(*p),
                                        state->selector->entries(), kind);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" unsigned dkr_legacy_character_cinematic_id(std::uint8_t* rdram, recomp_context* ctx, unsigned racer,
                                                      unsigned native_id) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return native_id;
    try {
        std::lock_guard lock(state->mutex);
        legacy::prepare_presentation(*state, rdram, *ctx, racer, false);
        return state->presentation.cinematic_id(state->roster, state->selector->entries(), racer, native_id);
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" std::uint32_t dkr_legacy_character_cinematic_portrait(std::uint8_t*, recomp_context*, unsigned id) {
    using namespace dkr::runtime;
    const auto state = legacy::characters.load();
    if (!state || !state->selector)
        return 0;
    try {
        return state->presentation.cinematic_portrait(id);
    } catch (const std::exception& e) {
        legacy::fail(e.what());
    }
}
extern "C" void dkr_vehicle_menu_draw(std::uint8_t* rdram, recomp_context* ctx,
                                      dkr::runtime::RecompiledEntrypoint frame,
                                      dkr::runtime::RecompiledEntrypoint text_width) {
    using namespace dkr::runtime;
    try {
        const auto cars = legacy::current_world().vehicles;
        const auto calls = legacy::guest_payload();
        if (cars && calls)
            cars->draw({rdram, legacy::guest_memory_size()}, *ctx, *calls, frame, text_width);
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}

extern "C" int dkr_legacy_track_menu(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                     const std::uint32_t* fields, unsigned observed) {
    if (event >= 100) {
        using namespace dkr::runtime;
        try {
            const auto cars = legacy::current_world().vehicles;
            if (!cars)
                return 0;
            const auto calls = legacy::guest_payload();
            if (!calls)
                throw dkr::mods::Error("Custom vehicles have no admitted native menu callbacks.");
            const auto result =
                cars->menu({rdram, legacy::guest_memory_size()}, *ctx, event, *calls, [&](unsigned player) {
                    if (const auto state = legacy::characters.load()) {
                        std::lock_guard lock(state->mutex);
                        if (const auto entry = state->roster.entry(player))
                            return entry->headers[0];
                    }
                    dkr::mods::CharacterMenuFields addresses;
                    addresses.fill(0x80000000U);
                    dkr::mods::CharacterMenuMemory g({rdram, legacy::guest_memory_size()}, addresses);
                    const auto table =
                        legacy::session.load()->boot_bank()->revision() == "us.v80" ? 0x80126990U : 0x801263f0U;
                    constexpr unsigned headers[]{2, 3, 4, 5, 6, 7, 8, 9, 1, 0};
                    const auto base = g.read(table + player, 1);
                    if (base >= 10)
                        throw dkr::mods::Error("Invalid selected native driver.");
                    return headers[base];
                });
            return result ? 1 : 0;
        } catch (const ultramodern::thread_terminated&) {
            throw;
        } catch (const std::exception& error) {
            legacy::fail(error.what());
        }
    }

    using namespace dkr::runtime;
    // trackmenu_assets(TRACKMENU_TYPE_LOAD_LEVEL): Track Select confirmed a
    // course. That choice supersedes Track Lab's sticky load override before
    // get_track_id_to_load can replace it, with or without legacy courses.
    if (!legacy::frozen_courses() && event == 12 && static_cast<std::uint32_t>(ctx->r4) == 2)
        custom_tracks::arm_track_override({});
    auto state = legacy::menu.load();
    try {
        if (!legacy::private_world && (event == 0 || event == 1 || event == 15)) {
            auto& owner = legacy::ordinary_world;
            auto bytes = legacy::course_records(rdram, *ctx, !owner.records_restored);
            if (!bytes.empty()) {
                owner.records_restored = true;
                if (!netplay::session().active() && bytes != owner.records_last) {
                    saves::records::submit(bytes);
                    owner.records_last = std::move(bytes);
                }
            }
        }
        // Frozen/owned worlds keep the reviewed retail vehicle gate and must
        // never consult the live process's transport session.
        if (!legacy::frozen_courses() && !netplay::session().active())
            legacy::lend_course_clear(rdram, *ctx, event, fields);
        // Level IDs are assigned at game initialization, after launcher setup.
        // Build the combined catalogue on the first Track Select entry, also
        // when there are no legacy mods enabled for this session.
        if (event == 0 && (!state || !state->catalog_ready) &&
            (legacy::frozen_courses() || !netplay::session().active())) {
            const auto authored = legacy::authored_courses();
            if (!authored.empty() || !legacy::current_world().cups.empty()) {
                const auto active = legacy::session.load();
                auto roots = active ? active->tracks() : std::vector<dkr::mods::Root>{};
                for (const auto& track : authored) {
                    if (roots.size() == 512)
                        break;
                    dkr::mods::Root root;
                    const auto identity = "dkrmap:" + track.id;
                    root.content_id = dkr::mods::sha256(
                        dkr::mods::View(reinterpret_cast<const std::uint8_t*>(identity.data()), identity.size()));
                    root.name = track.name.substr(0, 255);
                    root.carrier = static_cast<unsigned>(track.level_id);
                    root.vehicles = track.vehicles;
                    roots.push_back(std::move(root));
                }
                state = std::make_shared<legacy::MenuState>(std::move(roots), state ? state->allow_races : true,
                                                            legacy::current_world().cups);
                legacy::menu.store(state);
            }
            if (state)
                state->catalog_ready = true;
        }
        if (!state)
            return 0;
        if (!fields)
            throw dkr::mods::Error("Native menu hook has no verified revision fields.");
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram, ctx, event, fields, false);
#endif
        dkr::mods::TrackMenuFields addresses;
        std::copy_n(fields, addresses.size(), addresses.begin());
        if (event == 1) {
            std::size_t bytes = 0;
            {
                std::lock_guard lock(state->mutex);
                if (state->adapter.needs_names())
                    bytes = state->adapter.name_bytes().size();
            }
            if (bytes) {
                const auto payload = legacy::guest_payload();
                if (!payload || !payload->asset_allocate)
                    throw dkr::mods::Error("Native menu heap callback is unavailable.");
                auto allocation = *ctx;
                allocation.r4 = bytes;
                allocation.r5 = 0x7f7f7fff;
                // Original guest allocation may yield: never hold a host mutex.
                payload->asset_allocate(rdram, &allocation);
                std::lock_guard lock(state->mutex);
                state->adapter.install_names({rdram, legacy::guest_memory_size()},
                                             static_cast<std::uint32_t>(allocation.r2));
            }
        }
        dkr::mods::TrackMenuEffect effect;
        {
            std::lock_guard lock(state->mutex);
            effect = state->adapter.apply(event, {rdram, legacy::guest_memory_size()}, addresses,
                                          static_cast<std::uint32_t>(ctx->r4), observed);
        }
        // Appended levels have their own vehicle table entry. Preserve the
        // author's default instead of the legacy carrier's first usable one.
        if (event == 10 && legacy::owns_level(static_cast<std::int32_t>(ctx->r4)))
            effect.override_return = false;
        if (effect.scene)
            legacy::request_scene(effect.scene->id, effect.scene->carrier);
        if (!effect.backgrounds.empty()) {
            const auto payload = legacy::guest_payload();
            if (!payload || !payload->menu_material_set)
                throw dkr::mods::Error("Native background material callback is unavailable.");
            const auto dl_field = effect.background_display_list;
            for (const auto& background : effect.backgrounds) {
                auto call = *ctx;
                call.r4 = static_cast<std::int32_t>(dl_field);
                call.r5 = static_cast<std::int32_t>(background.texture);
                call.r6 = 0;
                call.r7 = 0;
                call.f_odd = call.mips3_float_mode ? &call.f1.u32l : &call.f0.u32h;
                // Guest callbacks may yield: no adapter lock is held.
                payload->menu_material_set(rdram, &call);
                if (payload->menu_background_cover) {
                    auto cover = *ctx;
                    cover.r8 = static_cast<std::int32_t>(background.vertices);
                    cover.f_odd = cover.mips3_float_mode ? &cover.f1.u32l : &cover.f0.u32h;
                    // The private payload records the existing background-cover
                    // event. Local aspect settings NEVER mutate shared RAM.
                    payload->menu_background_cover(rdram, &cover);
                }
                // Original F3DDKR vertex/polygon commands, original orthographic
                // camera and double buffer. Continue the caller-owned CURRENT
                // list; sMenuCurrDisplayList still belongs to the previous menu
                // pulse here and must not be written or published early.
                dkr::mods::append_track_background_quad({rdram, legacy::guest_memory_size()}, dl_field, background);
            }
        }
        if (effect.navigation_sound) {
            const auto payload = legacy::guest_payload();
            if (!payload || !payload->menu_sound_play)
                throw dkr::mods::Error("Native menu sound callback is unavailable.");
            // Match stock trackmenu_input: SOUND_MENU_PICK2 (0xEB), no handle.
            // Guest audio can yield: call only after releasing the host lock,
            // and preserve the original hook's register context.
            auto sound = *ctx;
            sound.r4 = 0xEB;
            sound.r5 = 0;
            payload->menu_sound_play(rdram, &sound);
        }
#if DKR_LEGACY_QUALIFICATION
        legacy::qualify_native_menu(rdram, ctx, event, fields, true);
#endif
        if (effect.override_return)
            ctx->r2 = static_cast<gpr>(static_cast<std::int32_t>(effect.return_value));
        return effect.override_return;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" void dkr_legacy_scene_begin(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace dkr::runtime;
    auto active = legacy::session.load();
    if (!active)
        return;
    try {
        const auto before = active->current_content();
        const auto level = static_cast<std::uint32_t>(ctx->r4);
        active->begin_scene({rdram, legacy::guest_memory_size()}, level, legacy::owns_level(level));
        const auto after = active->current_content();
        if (before != after)
            std::fprintf(stderr, "[legacy][scene] generation=%llu carrier=%u content=%s\n",
                         static_cast<unsigned long long>(active->published_scenes()), static_cast<unsigned>(ctx->r4),
                         after.empty() ? "stock" : after.c_str());
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" int dkr_legacy_asset_api(std::uint8_t* rdram, recomp_context* ctx, unsigned operation) {
    using namespace dkr::runtime;
    auto active = legacy::session.load();
    if (!active)
        return 0;
    try {
        auto lease = active->acquire();
        const auto mount = lease.route();
        if (!mount)
            return 0;
        const auto section = static_cast<std::uint32_t>(ctx->r4);
        // All texture/sprite artwork is boot-owned. A legacy course can change
        // earlier record sizes, so only its bank may resolve these offsets.
        const bool shared_artwork = section == 2 || section == 4 || section == 12;
        if (!legacy::frozen_courses() && operation == static_cast<unsigned>(dkr::mods::AssetOperation::PartialLoad) &&
            !shared_artwork && dkr_custom_tracks_asset_override(rdram, ctx))
            return 1;
        const auto payload = legacy::guest_payload();
        if (!payload)
            throw dkr::mods::Error("The custom asset loader has no selected revision.");
        const bool handled = dkr::mods::dispatch_asset_api(
            static_cast<dkr::mods::AssetOperation>(operation), mount, {rdram, legacy::guest_memory_size()}, *ctx,
            {payload->asset_allocate, payload->asset_release, payload->asset_copy});
        if (handled && operation == static_cast<unsigned>(dkr::mods::AssetOperation::TableLoad)) {
            if (legacy::frozen_courses()) {
                if (section == 22)
                    legacy::course_budget(rdram, true);
            } else if (section != 3 && section != 5 && section != 13)
                dkr_custom_tracks_extend_table(rdram, ctx, section);
        }
        return handled;
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}
extern "C" void dkr_legacy_pi_start_dma(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace dkr::runtime;
    try {
        auto active = legacy::session.load();
        dkr::mods::dispatch_pi_dma(active ? &active->bus() : nullptr, {rdram, legacy::guest_memory_size()}, *ctx,
                                   osPiStartDma_recomp, {nullptr, [](void*, std::uint32_t queue) {
                                                             ultramodern::enqueue_external_message_src(
                                                                 static_cast<std::int32_t>(queue), 0, false,
                                                                 ultramodern::EventMessageSource::Pi);
                                                         }});
    } catch (const ultramodern::thread_terminated&) {
        throw;
    } catch (const std::exception& error) {
        legacy::fail(error.what());
    }
}

namespace dkr::runtime::legacy {
struct ModWorld::State : WorldState {
    std::uint64_t mutation = 0;
    std::string error;
};
namespace {
struct WorldRestore final : ModWorld::Restore {
    const void* authority = nullptr;
    std::uint64_t mutation = 0;
    std::uint32_t pending_course_heap = 0;
    mods::RuntimeSession::ReplayRestore session;
    std::shared_ptr<MenuState> menu;
    std::shared_ptr<CharacterState> characters;
    std::shared_ptr<MusicBinding> music;
    std::shared_ptr<vehicles::World> vehicles;
};
} // namespace
ModWorld::ModWorld(std::shared_ptr<mods::RuntimeSession> assets, GamePayload calls, RecompiledEntrypoint animation,
                   std::shared_ptr<const custom_tracks::PreparedTracks> authored, bool ai, RecompiledEntrypoint random,
                   std::shared_ptr<const mods::online::MusicLibrary> music, RecompiledEntrypoint texture,
                   std::vector<mods::TrackMenuCup> cups)
    : state_(std::make_unique<State>()) {
    if (!assets)
        throw mods::Error("An owned mod world needs admitted resources.");
    (void)mods::online::course_display_budgets(authored.get());
    state_->session.store(assets);
    state_->private_owner = true;
    state_->calls = calls;
    if (const auto cars = mods::vehicles::resources_for(assets.get()); cars && !cars->library.packages.empty()) {
        state_->vehicles = std::make_shared<vehicles::World>(cars, assets->boot_bank()->revision() == "us.v80");
        mods::vehicles::publish_render_resources(cars);
    }
    state_->animation = animation;
    state_->random = random;
    state_->authored = std::move(authored);
    state_->custom_ai = ai;
    state_->texture_animation = texture;
    state_->cups = std::move(cups);
    state_->music.store(std::make_shared<MusicBinding>(std::move(music), assets->boot_bank()->revision() == "us.v80"));
    if (ai && !random)
        throw mods::Error("Owned custom AI needs the reviewed private RNG entry.");
    auto roots = frozen_roots(*assets, state_->authored.get());
    if (!roots.empty() || !state_->cups.empty()) {
        auto menu = std::make_shared<MenuState>(std::move(roots), true, state_->cups);
        menu->catalog_ready = true;
        state_->menu.store(std::move(menu));
    }
    if (assets->characters()) {
        auto characters = std::make_shared<CharacterState>(assets->characters());
        characters->selector = std::make_unique<mods::CharacterMenuAdapter>(assets->characters());
        state_->characters.store(std::move(characters));
    }
}
ModWorld::~ModWorld() = default;
const mods::AssetBus& ModWorld::bus() const {
    return state_->session.load()->bus();
}
const std::string& ModWorld::error() const {
    return state_->error;
}
void ModWorld::select_course(const std::string& id, unsigned carrier) {
    // This explicit owner never falls through to the ordinary launcher world.
    if (state_->authored &&
        std::ranges::any_of(state_->authored->courses, [&](const auto& c) { return unsigned(c.level_id) == carrier; }))
        return;
    state_->session.load()->request(id, carrier);
}
std::string ModWorld::asset_inventory() const {
    return sdk2_asset_inventory(state_->session.load());
}
std::string ModWorld::current_course() const {
    return state_->session.load()->current_content();
}
std::string ModWorld::selected_trophy() const {
    const auto menu = state_->menu.load();
    if (!menu)
        return {};
    std::lock_guard lock(menu->mutex);
    return menu->adapter.selected_cup();
}
namespace {
mods::Bytes capture_world(const WorldState& state) {
    mods::CheckpointWriter out;
    out.u32(5);
    out.block(state.session.load()->checkpoint());
    out.flag(bool(state.vehicles));
    if (state.vehicles)
        out.block(state.vehicles->checkpoint());
    out.u32(state.pending_course_heap);
    const auto menu = state.menu.load();
    out.flag(bool(menu));
    if (menu) {
        std::lock_guard lock(menu->mutex);
        out.block(menu->adapter.checkpoint());
    }
    const auto characters = state.characters.load();
    out.flag(bool(characters));
    if (characters) {
        std::lock_guard lock(characters->mutex);
        if (characters->ai.spawn)
            throw mods::Error("Cannot checkpoint a nested AI character spawn.");
        out.block(characters->roster.checkpoint());
        out.block(characters->selector->checkpoint());
        out.block(characters->stage.checkpoint());
        out.block(characters->hints.checkpoint());
        out.block(characters->presentation.checkpoint());
        out.u32(characters->ai.humans);
        out.u32(characters->ai.racers);
        out.flag(characters->menu_active);
    }
    const auto music = state.music.load();
    out.flag(bool(music));
    if (music) {
        std::lock_guard lock(music->mutex);
        out.block(music->state.checkpoint());
    }
    return std::move(out).finish();
}
} // namespace
mods::Bytes ModWorld::checkpoint() const {
    return capture_world(*state_);
}
mods::Bytes online_bootstrap_checkpoint() {
    if (private_world || !ordinary_world.frozen_online || !ordinary_world.session.load())
        throw mods::Error("Online native bootstrap has no exclusive frozen resources.");
    auto result = capture_world(ordinary_world);
    ordinary_music_enabled = false;
    return result;
}
std::unique_ptr<ModWorld::Restore> ModWorld::stage_checkpoint(mods::View bytes) const {
    mods::CheckpointReader in(bytes);
    if (in.u32() != 5)
        throw mods::Error("Unsupported owned mod world schema.");
    auto out = std::make_unique<WorldRestore>();
    out->authority = state_.get();
    out->mutation = state_->mutation;
    const auto assets = state_->session.load();
    out->session = assets->stage_checkpoint(in.block());
    if (in.flag() != bool(state_->vehicles))
        throw mods::Error("Owned vehicle catalogue differs.");
    if (state_->vehicles)
        out->vehicles = state_->vehicles->stage_checkpoint(in.block());
    out->pending_course_heap = in.u32();
    if (!mods::online::admitted_course_heap(state_->authored.get(), out->pending_course_heap))
        throw mods::Error("Owned course checkpoint has an invalid collision heap.");
    const auto menu = state_->menu.load();
    if (in.flag() != bool(menu))
        throw mods::Error("Owned mod menu identity differs.");
    if (menu) {
        std::lock_guard lock(menu->mutex);
        out->menu = std::make_shared<MenuState>(menu->adapter.stage_checkpoint(in.block()));
    }
    const auto characters = state_->characters.load();
    if (in.flag() != bool(characters))
        throw mods::Error("Owned mod character identity differs.");
    if (characters) {
        std::lock_guard lock(characters->mutex);
        auto next = std::make_shared<CharacterState>(assets->characters());
        const auto count = assets->characters()->characters.size();
        next->roster = characters->roster.stage_checkpoint(in.block());
        next->selector =
            std::make_unique<mods::CharacterMenuAdapter>(characters->selector->stage_checkpoint(in.block()));
        next->stage = characters->stage.stage_checkpoint(in.block(), count);
        next->hints = characters->hints.stage_checkpoint(in.block(), count);
        next->presentation = characters->presentation.stage_checkpoint(in.block(), count);
        next->ai.humans = in.bounded(4);
        next->ai.racers = in.bounded(8);
        if (next->ai.racers < next->ai.humans)
            throw mods::Error("Owned mod AI roster is invalid.");
        next->menu_active = in.flag();
        out->characters = std::move(next);
    }
    const auto music = state_->music.load();
    if (in.flag() != bool(music))
        throw mods::Error("Owned mod music identity differs.");
    if (music) {
        std::lock_guard lock(music->mutex);
        out->music = std::make_shared<MusicBinding>(music->state.stage_checkpoint(in.block()));
    }
    in.end();
    return out;
}
bool ModWorld::commit_checkpoint(std::unique_ptr<Restore> value) {
    auto* staged = dynamic_cast<WorldRestore*>(value.get());
    if (!staged || staged->authority != state_.get() || staged->mutation != state_->mutation ||
        state_->mutation == UINT64_MAX || !state_->session.load()->commit_checkpoint(std::move(staged->session)))
        return false;
    state_->menu.store(std::move(staged->menu));
    state_->characters.store(std::move(staged->characters));
    state_->pending_course_heap = staged->pending_course_heap;
    state_->vehicles = std::move(staged->vehicles);
    state_->music.store(std::move(staged->music));
    ++state_->mutation;
    return true;
}
int ModWorld::dispatch(const char* operation, std::uint8_t* ram, recomp_context* ctx, const std::uint64_t* args,
                       unsigned count, const std::uint32_t* fields, unsigned event, std::uint64_t& result) noexcept {
    try {
        if (!operation || !ram || !ctx || (count && !args) || state_->mutation == UINT64_MAX)
            throw mods::Error("Invalid owned mod callback boundary.");
        WorldScope scope(*state_);
        const std::string_view name(operation);
        result = 0;
        const auto arity = [&](unsigned expected) {
            if (count != expected)
                throw mods::Error("Owned mod callback ABI differs.");
        };
        if (name == "dkr_owned_vehicle_menu_draw") {
            arity(0);
            const auto callbacks = owned_vehicle_draw_callbacks;
            if (!state_->private_owner || !state_->vehicles || !callbacks.frame || !callbacks.text_width)
                throw mods::Error("Owned vehicle menu has no private drawing binding.");
            state_->vehicles->draw({ram, guest_memory_size()}, *ctx, state_->calls, callbacks.frame,
                                   callbacks.text_width);
            result = 1;
            static std::atomic<unsigned> reports{0};
            if (reports.fetch_add(1, std::memory_order_relaxed) < 4)
                std::fprintf(
                    stderr,
                    "[custom-vehicles][owned-menu] owned-vehicle-menu-v1: private picker, portraits, frame and text submitted\n");
        } else if (name == "dkr_achievements_context") {
            arity(1);
            const auto assets = session.load();
            result = owns_level(static_cast<std::int32_t>(args[0])) || (assets && !assets->current_content().empty()) ||
                     !selected_trophy().empty();
        } else if (name == "dkr_shared_records_capture") {
            arity(0);
            state_->records_last = course_records(ram, *ctx, false);
            if (state_->records_last.empty())
                state_->records_last = saves::records::encode({});
            result = reinterpret_cast<std::uintptr_t>(&state_->records_last);
        } else if (name == "dkr_shared_records_install") {
            arity(0);
            state_->records_last = course_records(ram, *ctx, true);
            result = !state_->records_last.empty() || authored_courses().empty();
        } else if (name == "dkr_legacy_asset_api") {
            arity(1);
            result = dkr_legacy_asset_api(ram, ctx, static_cast<unsigned>(args[0]));
        } else if (name == "dkr_legacy_scene_begin") {
            arity(0);
            dkr_legacy_scene_begin(ram, ctx);
        } else if (name == "dkr_owned_scene_admission") {
            arity(1);
            if (args[0] > 127)
                throw mods::Error("Custom scene ID is outside its signed-byte range.");
            result = args[0] < 65 || owns_level(static_cast<std::int32_t>(args[0]));
            if (result)
                course_budget(ram, false);
        } else if (name == "dkr_owned_course_prepare") {
            arity(1);
            if (args[0] > 1)
                throw mods::Error("Invalid inline music preview policy.");
            mods::online::resident_music::PreviewScope preview(args[0] != 0);
            frozen_course_hook("prepare_memory", ram, ctx);
        } else if (name == "dkr_custom_music_sequence_loaded") {
            arity(0);
            frozen_music_hook("loaded", ram, ctx);
        } else if (name == "dkr_custom_music_sequence_started") {
            arity(0);
            frozen_music_hook("started", ram, ctx);
        } else if (name == "dkr_audio_mix_tick") {
            arity(0);
            frozen_music_hook("tick", ram, ctx);
        } else if (name == "dkr_scale_sequence_player_volume") {
            arity(0);
            frozen_music_volume(ram, ctx);
        } else if (name == "dkr_custom_tracks_track_heap") {
            arity(0);
            if (state_->pending_course_heap && std::uint32_t(ctx->r21) != custom_tracks::kRetailTrackHeap)
                throw mods::Error("Owned custom collision heap hook ABI changed.");
            if (state_->pending_course_heap)
                ctx->r21 = state_->pending_course_heap;
            state_->pending_course_heap = 0;
        } else if (name == "dkr_legacy_heap_capacity") {
            arity(0);
            dkr_legacy_heap_capacity(ram, ctx);
        } else if (name == "dkr_legacy_course_component") {
            arity(2);
            if (args[0] >= 3 || args[1] > UINT32_MAX)
                throw mods::Error("Invalid owned course-behaviour ABI.");
            result = dkr_legacy_course_component(ram, ctx, static_cast<unsigned>(args[0]),
                                                 static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_racer_stat") {
            arity(3);
            if (args[0] >= 3 || args[1] > UINT32_MAX || args[2] > UINT32_MAX)
                throw mods::Error("Invalid owned racer-parameter ABI.");
            result = dkr_legacy_racer_stat(ram, ctx, static_cast<unsigned>(args[0]),
                                           static_cast<std::uint32_t>(args[1]), static_cast<std::uint32_t>(args[2]));
        } else if (name == "dkr_legacy_texture_animation") {
            arity(0);
            result = dkr_legacy_texture_animation(ram, ctx, state_->texture_animation);
        } else if (name == "dkr_legacy_model_safety") {
            arity(2);
            if (args[0] > 3 || args[1] > 0xffffffffULL)
                throw mods::Error("Invalid owned model-safety ABI.");
            result =
                dkr_legacy_model_safety(ram, ctx, static_cast<unsigned>(args[0]), static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_memory_service") {
            arity(4);
            for (unsigned i = 0; i < 4; ++i)
                if (args[i] > UINT32_MAX)
                    throw mods::Error("Invalid owned memory-safety ABI.");
            result =
                dkr_legacy_memory_service(ram, ctx, static_cast<unsigned>(args[0]), static_cast<unsigned>(args[1]),
                                          static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]));
        } else if (name == "dkr_legacy_asset_cache_capacity") {
            arity(1);
            if (args[0] >= 4)
                throw mods::Error("Unknown owned asset cache kind.");
            result = dkr_legacy_asset_cache_capacity(ram, ctx, static_cast<unsigned>(args[0]));
        } else if (name == "dkr_legacy_asset_cache_guard") {
            arity(2);
            if (args[0] >= 4 || args[1] > UINT32_MAX)
                throw mods::Error("Invalid owned asset cache bound.");
            dkr_legacy_asset_cache_guard(ram, ctx, static_cast<unsigned>(args[0]), static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_racer_spawn_failed") {
            arity(0);
            dkr_legacy_racer_spawn_failed(ram, ctx);
        } else if (name == "dkr_legacy_character_event") {
            arity(2);
            dkr_legacy_character_event(ram, ctx, static_cast<unsigned>(args[0]), static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_character_ai_event") {
            arity(1);
            dkr_legacy_character_ai_event(ram, ctx, static_cast<unsigned>(args[0]), state_->random);
        } else if (name == "dkr_owned_asset_dma") {
            arity(3);
            if (args[0] > UINT32_MAX || args[1] > UINT32_MAX || !args[2] || args[2] > 0x5000)
                throw mods::Error("Owned custom DMA has an invalid range.");
            const auto read = state_->session.load()->bus().resolve(static_cast<std::uint32_t>(args[0]), args[2]);
            if (!read)
                return 0;
            read->copy_to_guest({ram, guest_memory_size()}, static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_character_menu") {
            arity(0);
            result = dkr_legacy_character_menu(ram, ctx, event, fields);
        } else if (name == "dkr_legacy_track_menu") {
            arity(1);
            result = dkr_legacy_track_menu(ram, ctx, event, fields, static_cast<unsigned>(args[0]));
        } else if (name == "dkr_legacy_character_portrait_lookup") {
            arity(1);
            result = dkr_legacy_character_portrait_lookup(ram, ctx, static_cast<std::uint32_t>(args[0]));
        } else if (name == "dkr_legacy_character_race_sound") {
            arity(2);
            result = dkr_legacy_character_race_sound(ram, ctx, static_cast<std::uint32_t>(args[0]),
                                                     static_cast<unsigned>(args[1]));
        } else if (name == "dkr_legacy_character_hud_bind") {
            arity(2);
            dkr_legacy_character_hud_bind(ram, ctx, static_cast<std::uint32_t>(args[0]),
                                          static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_character_hud_unbind") {
            arity(0);
            dkr_legacy_character_hud_unbind(ram, ctx);
        } else if (name == "dkr_legacy_character_hud_lookup") {
            arity(2);
            result = dkr_legacy_character_hud_lookup(ram, ctx, static_cast<std::uint32_t>(args[0]),
                                                     static_cast<std::uint32_t>(args[1]));
        } else if (name == "dkr_legacy_character_play_sound") {
            arity(1);
            result = dkr_legacy_character_play_sound(ram, ctx, static_cast<unsigned>(args[0]));
        } else if (name == "dkr_legacy_character_cinematic_id") {
            arity(2);
            result = dkr_legacy_character_cinematic_id(ram, ctx, static_cast<unsigned>(args[0]),
                                                       static_cast<unsigned>(args[1]));
        } else if (name == "dkr_legacy_character_cinematic_portrait") {
            arity(1);
            result = dkr_legacy_character_cinematic_portrait(ram, ctx, static_cast<unsigned>(args[0]));
        } else
            return 0;
        ++state_->mutation;
        return 1;
    } catch (const std::exception& e) {
        try {
            state_->error = e.what();
        } catch (...) {
        }
        return -1;
    } catch (...) {
        try {
            state_->error = "Owned custom content stopped safely after an unexpected native fault.";
        } catch (...) {
        }
        return -1;
    }
}
int draw_owned_vehicle_menu(ModWorld& world, std::uint8_t* ram, recomp_context* ctx, RecompiledEntrypoint frame,
                            RecompiledEntrypoint text_width) noexcept {
    if (!ram || !ctx || !frame || !text_width)
        return -1;
    // Nested native callbacks retain the same world and restore their caller's
    // entrypoints even when the checked guest trampoline rejects a callback.
    struct Binding {
        OwnedVehicleDrawCallbacks before = owned_vehicle_draw_callbacks;
        Binding(RecompiledEntrypoint f, RecompiledEntrypoint w) {
            owned_vehicle_draw_callbacks = {f, w};
        }
        ~Binding() {
            owned_vehicle_draw_callbacks = before;
        }
    } binding(frame, text_width);
    std::uint64_t result = 0;
    const auto status = world.dispatch("dkr_owned_vehicle_menu_draw", ram, ctx, nullptr, 0, nullptr, 0, result);
    return status == 1 && result == 1 ? 1 : -1;
}
void ModWorld::mix_music(std::uint8_t* ram, std::span<std::uint8_t> pcm, unsigned rate) {
    const auto music = state_->music.load();
    if (!music)
        return;
    std::lock_guard lock(music->mutex);
    mods::CharacterMenuFields fields;
    fields.fill(0x80000000U);
    mods::CharacterMenuMemory guest({ram, 8U * 1024 * 1024}, fields);
    music->state.tick(guest);
    music->state.mix_s16le(pcm, rate);
    if (state_->mutation == UINT64_MAX)
        throw mods::Error("Custom music transaction exhausted.");
    ++state_->mutation;
}
} // namespace dkr::runtime::legacy

namespace dkr::runtime::saves::records {
Bytes snapshot(legacy::ModWorld& world, std::uint8_t* ram) {
    recomp_context call{};
    std::uint64_t result = 0;
    if (world.dispatch("dkr_shared_records_capture", ram, &call, nullptr, 0, nullptr, 0, result) != 1 || !result)
        throw std::runtime_error(
            "Could not capture custom-course records from the owned guest: " +
            (world.error().empty() ? std::string("missing records service result") : world.error()));
    return *reinterpret_cast<const Bytes*>(std::uintptr_t(result));
}
void install(legacy::ModWorld& world, std::uint8_t* ram) {
    if (seed().size() == 20)
        return;
    recomp_context call{};
    std::uint64_t result = 0;
    if (world.dispatch("dkr_shared_records_install", ram, &call, nullptr, 0, nullptr, 0, result) != 1 || !result)
        throw std::runtime_error(
            "Custom records could not be installed at the initial owned boundary; originals were retained: " +
            (world.error().empty() ? std::string("record tables are not ready") : world.error()));
}
} // namespace dkr::runtime::saves::records

extern "C" void dkr_achievements_observe(std::uint8_t* ram, unsigned revision) noexcept {
    using namespace dkr::runtime;
    if (achievements::online_session())
        return;
    try {
        auto sample = achievements::capture(ram, legacy::guest_memory_size(), revision);
        const auto assets = legacy::session.load();
        sample.custom = sample.level >= 65 || legacy::owns_level(sample.level) ||
                        (assets && !assets->current_content().empty()) || !legacy::selected_trophy().empty();
        achievements::submit(sample);
    } catch (...) {
        std::fprintf(stderr, "[achievements] Read-only observer unavailable at this boundary\n");
    }
}

// checked-record-observer-v2: revision-owned RAM access, no guest callback.
