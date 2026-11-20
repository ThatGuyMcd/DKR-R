#include "capture_symbols.hpp"
#include "display_list_symbols.hpp"
#include "guest_call_context.hpp"
#include "memory_symbols.hpp"
#include "shadow_symbols.hpp"
#include "custom_tracks.hpp"
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "replay_probe_capture.hpp"
#endif

#include "game_payload.hpp"
#include "runtime_legacy_mods.hpp"
#include "revision_addresses.hpp"
#include "netplay/runtime_state.hpp"
#include "rom_revision.hpp"

#include "recomp.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Patch Pipeline side of custom track support. The pure table arithmetic lives
// in custom_tracks.cpp and is unit tested; this file only moves it across the
// recompiled boundary.
namespace {

// Both section indices are literals at the retail call sites in
// level_global_init, so they never have to be discovered at runtime:
//
//   8006a6dc  jal  asset_table_load
//   8006a6e0  li   a0,22            <- ASSET_LEVEL_HEADERS_TABLE
//
//   8006a7dc  li   a0,23            <- ASSET_LEVEL_HEADERS
//   8006a7e0  jal  asset_load
//   8006a7e4  li   a3,196           <- sizeof(LevelHeader)
constexpr std::uint32_t kLevelHeadersTableSection = 22U;
constexpr std::uint32_t kLevelHeadersSection = 23U;

// AssetSectionsEnum in the matching decomp's include/asset_enums.h. Each level
// aspect is a (table, data) pair, and the two indices above anchor the numbering
// because level_global_init loads them as literals.
//
//   2/3 3D textures   20/21 object maps   22/23 headers   24/25 names
//   26/27 models
//
// The texture pair is the same numbering read from the other end: the decomp's
// section list runs ASSET_AI_BEHAVIOUR, ASSET_AI_BEHAVIOUR_TABLE,
// ASSET_TEXTURES_3D, ASSET_TEXTURES_3D_TABLE, so the data section is 2 and its
// table is 3 - note the order is data-then-table here, the reverse of the level
// pairs. textures_sprites.c reaches it as
// asset_table_load(ASSET_TEXTURES_3D_TABLE) at boot and asset_load with
// ASSET_TEXTURES_3D per texture, which is the same pair of hooks below.
//
// A minimap is the next two pairs along, data-then-table as well:
// ASSET_TEXTURES_2D and its table are 4/5, and after the game text, menu text
// and screens, ASSET_SPRITES and its table are 12/13. tex_init_textures loads
// all three tables at boot, 2D first and sprites last.
using dkr::runtime::custom_tracks::Section;

struct SectionMapping {
    std::uint32_t table_index;
    std::uint32_t data_index;
    Section section;
};

constexpr SectionMapping kSectionMappings[] = {
    {3U, 2U, Section::Textures3D},        {5U, 4U, Section::Textures2D},     {13U, 12U, Section::Sprites},
    {20U, 21U, Section::LevelObjectMaps}, {22U, 23U, Section::LevelHeaders}, {24U, 25U, Section::LevelNames},
    {26U, 27U, Section::LevelModels},
};

bool section_for_table(std::uint32_t index, Section& out) {
    for (const SectionMapping& mapping : kSectionMappings) {
        if (mapping.table_index == index) {
            out = mapping.section;
            return true;
        }
    }
    return false;
}

bool section_for_data(std::uint32_t index, Section& out) {
    for (const SectionMapping& mapping : kSectionMappings) {
        if (mapping.data_index == index) {
            out = mapping.section;
            return true;
        }
    }
    return false;
}

// Only the indices below are runtime-assigned. Everything else in the header, skybox
// included, is the author's to write. `geometry` is left alone unless the track
// actually ships a model, so a Phase 1 remix keeps pointing at the retail
// geometry it was built on.
// init_track spawns from both object maps:
//
//   init_track(geometry, skybox, players, vehicle, entrance,
//              header->collectables,   // 0x36 -> track_spawn_objects(.., 1)
//              header->unkBA);         // 0xBA -> track_spawn_objects(.., 0)
//
// Ancient Lake is 98 structural objects in map 5 and 86 collectables in map
// 73. Patching only one leaves the other pointing at retail, so the original
// objects keep spawning next to the author's.
using dkr::runtime::custom_tracks::MapSlot;

struct HeaderFixup {
    std::int32_t offset;
    Section section;
    MapSlot slot;
    const char* label;
};

constexpr HeaderFixup kHeaderFixups[] = {
    {0x34, Section::LevelModels, MapSlot::None, "model"},
    {0x36, Section::LevelObjectMaps, MapSlot::Collectables, "collectables map"},
    {0xBA, Section::LevelObjectMaps, MapSlot::Structure, "structure map"},
};

// asset_table_load allocates its result with this tag; the extended table is
// allocated the same way so it is reclaimed on the identical pool lifecycle.
//   80076c94  lui a1,0x7f7f
//   80076ca0  ori a1,a1,0x7fff
constexpr std::uint32_t kColourTagGrey = 0x7F7F7FFFU;

// mode_menu starts a level when menu_loop returns MENU_RESULT_FLAGS_200 with
// the map id in the low seven bits (menu.h, thread3_main.c).
constexpr std::int32_t kMenuResultStartLevel = 1 << 9;
constexpr std::int32_t kMenuResultMapMask = 0x7F;

// Character enum order in the matching decomp's include/enums.h.
constexpr std::uint8_t kCharacterDiddy = 9U;

constexpr std::uint32_t kRdramLow = 0x80000000U;
constexpr std::uint32_t kRdramHigh = 0x807FFFB6U;
constexpr std::uint32_t kRdramEnd = 0x80800000U;
constexpr std::uint32_t kMaxTableEntries = 32769U; // records + end offset + sentinel

// DKR serialises asset DMA on one thread, and Rev A additionally guards it
// with a one-token queue, so neither loader is reentrant. Recording the
// arguments at entry and consuming them at the epilogue is therefore safe.
std::uint32_t g_requested_table = 0xFFFFFFFFU;

struct AssetLoadRequest {
    std::uint32_t section = 0xFFFFFFFFU;
    std::uint32_t destination = 0;
    std::uint32_t offset = 0;
    std::int32_t size = 0;
};
AssetLoadRequest g_load;

gpr rdram_address(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool addressable(std::uint32_t address) {
    return address >= kRdramLow && address <= kRdramHigh;
}

std::int32_t read_word(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int32_t>(MEM_W(0, rdram_address(address)));
}

void write_word(std::uint8_t* rdram, std::uint32_t address, std::int32_t value) {
    MEM_W(0, rdram_address(address)) = static_cast<std::uint32_t>(value);
}

// The level model heap generate_track is about to reserve, or 0 for the retail
// size. Written at level_load's entry, where the level id is known, and taken
// by the generate_track hook, which only sees a model index. Consuming it
// clears it, so a value prepared for one load can never reach a second.
std::int32_t g_track_heap_bytes = 0;

// Leaves most of the four megabytes dkr_custom_tracks_prepare_memory adds for
// the textures, objects and particles that come after the model. Two megabytes
// is nearly four times the retail heap and still a quarter of the growth.
constexpr std::int32_t kTrackHeapCeiling = 0x200000;

// The loader 16-aligns as it walks the segments, and measure_level_model_arena
// accounts for that exactly; this only keeps a rounding difference from ever
// being the thing that overflows.
constexpr std::int32_t kTrackHeapSlack = 0x1000;

// gNumF3dCmdsPerPlayer in thread3_main.c, identical in both revisions.
constexpr std::int32_t kRetailCommands[4] = {4500, 7000, 11000, 11000};
constexpr std::int32_t kCommandsPerBatch = 10;
constexpr std::int32_t kCommandLimit = 0x20000;

// The one-player budget a Track Select preview of the largest .dkrmap course
// needs, decided at boot. 0 when Track Select offers none.
std::int32_t g_menu_commands = 0;

bool owned_vanilla_bootstrap() {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    return dkr_experimental_bootstrap_active();
#else
    return false;
#endif
}

// thread3_main.c's display-list globals, verified against
// ver/symbols/symbol_addrs.us.v{77,80}.txt.
struct DisplayListGlobals {
    std::uint32_t table;            // gNumF3dCmdsPerPlayer
    std::uint32_t previous_players; // gPrevPlayerCount
    std::uint32_t current_commands; // gCurrNumF3dCmdsPerPlayer
    std::uint32_t lists;            // gDisplayLists
};

DisplayListGlobals display_list_globals() {
    const auto& symbols = dkr::runtime::display_list_symbols::for_revision(
        dkr::runtime::revision_addresses::gSelectedRevision == dkr::runtime::rom::Revision::UsV80);
    return {symbols.table, symbols.previous_players, symbols.current_commands, symbols.lists};
}

// A frame's budget with `players` viewports (0 is one player) for a level
// drawing `batches`, or the retail budget when it is not a .dkrmap level.
std::int32_t commands_for(int players, std::int32_t batches) {
    if (batches <= 0) {
        return kRetailCommands[players];
    }
    const std::int64_t budget =
        std::int64_t(kRetailCommands[players]) + std::int64_t(batches) * kCommandsPerBatch * (players + 1);
    return static_cast<std::int32_t>(std::min<std::int64_t>(budget, kCommandLimit));
}

// Writes gNumF3dCmdsPerPlayer for a level drawing `batches`, never below the
// Track Select floor, so the heap a race leaves behind for the menu can still
// draw every preview. Returns whether any entry changed.
bool write_display_list_budget(std::uint8_t* rdram, std::int32_t batches) {
    const DisplayListGlobals globals = display_list_globals();
    bool changed = false;
    for (int players = 0; players < 4; ++players) {
        const std::int32_t commands = std::max(commands_for(players, batches), g_menu_commands);
        const std::uint32_t entry = globals.table + static_cast<std::uint32_t>(players) * 4U;
        if (read_word(rdram, entry) != commands) {
            write_word(rdram, entry, commands);
            changed = true;
        }
    }
    return changed;
}

} // namespace

// Entry of asset_table_load. Only the requested section is recorded; the
// argument register is reused later in the function, so the epilogue can no
// longer recover it.
extern "C" void dkr_custom_tracks_table_load_begin(std::uint8_t*, recomp_context* context) {
    g_requested_table = static_cast<std::uint32_t>(context->r4);
}

static void size_menu_display_lists(std::uint8_t* rdram);
static void publish_extended_table(std::uint8_t* rdram, recomp_context* context, std::uint32_t requested);

// Both the retail epilogue and the mounted legacy namespace reach this: the
// namespace can serve the table without ever running the retail loader.
// Extend the returned table, then size the Track Select display lists the
// appended levels need - the sizing is idempotent past boot.
extern "C" void dkr_custom_tracks_extend_table(std::uint8_t* rdram, recomp_context* context, std::uint32_t requested) {
    // This namespace is already extended against the frozen host manifest.
    // Never append the local offline catalogue a second time.
    if (dkr::runtime::legacy::frozen_online_resources())
        return;
    // The normal experimental lobby temporarily binds an offline constructor
    // while parking retail pre-INTRO state. Installed offline .dkrmap tracks
    // must not enter that checkpoint or resize its retail display-list heaps.
    // Leave the catalogue and the user's enabled-mod settings untouched.
    if (owned_vanilla_bootstrap()) {
        if (requested == kLevelHeadersTableSection) {
            g_menu_commands = 0;
            g_track_heap_bytes = 0;
            std::fprintf(stderr, "[rollback][boot] retained vanilla asset tables and display-list budgets\n");
        }
        return;
    }
    publish_extended_table(rdram, context, requested);
    if (requested == kLevelHeadersTableSection) {
        size_menu_display_lists(rdram);
    }
}

// Publishes a longer level table so the retail count, range check and world
// maximum all grow with it.
static void publish_extended_table(std::uint8_t* rdram, recomp_context* context, std::uint32_t requested) {
    Section section = Section::LevelHeaders;
    if (!section_for_table(requested, section)) {
        return; // Not a section custom tracks contribute to.
    }

    const auto retail_address = static_cast<std::uint32_t>(context->r2);
    if (!addressable(retail_address) || (retail_address & 3U)) {
        return; // Retail returned NULL; leave the failure exactly as authored.
    }

    std::vector<std::int32_t> retail;
    retail.reserve(64);
    const auto available = std::min(kMaxTableEntries, (kRdramEnd - retail_address) / 4U);
    for (std::uint32_t index = 0; index < available; ++index) {
        const std::int32_t entry = read_word(rdram, retail_address + (index * 4U));
        retail.push_back(entry);
        if (entry == -1) {
            break;
        }
    }
    if (retail.empty() || retail.back() != -1) {
        std::fprintf(stderr, "[custom-tracks] refusing unterminated table section=%u address=%08X words=%u\n",
                     requested, retail_address, available);
        return; // Unterminated: refuse rather than guess.
    }

    const std::vector<std::int32_t> extended =
        dkr::runtime::custom_tracks::build_extended_table(section, retail.data(), retail.size());
    if (extended.empty()) {
        return; // Nothing added; the retail table stands unchanged.
    }

    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->mempool_alloc_safe == nullptr) {
        return;
    }

    dkr::runtime::GuestCallContext guest(*context);
    recomp_context& call = guest.context;
    call.r4 = static_cast<gpr>(extended.size() * sizeof(std::int32_t));
    call.r5 = static_cast<gpr>(kColourTagGrey);
    payload->mempool_alloc_safe(rdram, guest.get());
    const auto allocated = static_cast<std::uint32_t>(call.r2);
    if (!addressable(allocated) || (allocated & 3U) || extended.size() > (kRdramEnd - allocated) / 4U) {
        return; // Out of pool: keep the retail table rather than fail the load.
    }

    for (std::size_t index = 0; index < extended.size(); ++index) {
        write_word(rdram, allocated + static_cast<std::uint32_t>(index * 4U), extended[index]);
    }
    context->r2 = static_cast<gpr>(static_cast<std::int32_t>(allocated));
    if (payload->asset_release) {
        guest.reset(*context);
        call.r4 = rdram_address(retail_address);
        payload->asset_release(rdram, guest.get());
    }
}

// Common epilogue of asset_table_load.
extern "C" void dkr_custom_tracks_table_load_end(std::uint8_t* rdram, recomp_context* context) {
    const std::uint32_t requested = g_requested_table;
    g_requested_table = 0xFFFFFFFFU;
    dkr_custom_tracks_extend_table(rdram, context, requested);
}

// Entry of asset_load. The destination register is clobbered by the DMA call
// before the epilogue is reached, so every argument is captured here.
extern "C" void dkr_custom_tracks_asset_load_begin(std::uint8_t*, recomp_context* context) {
    g_load.section = static_cast<std::uint32_t>(context->r4);
    g_load.destination = static_cast<std::uint32_t>(context->r5);
    g_load.offset = static_cast<std::uint32_t>(context->r6);
    g_load.size = static_cast<std::int32_t>(context->r7);
}

namespace {
bool apply_custom_payload(std::uint8_t* rdram, const AssetLoadRequest& request);
}

// Consult only ranges assigned by build_extended_table. This must precede
// the legacy bank's strict section-bounds check: appended Blender bytes are
// owned by this loader, not by the immutable legacy source bank.
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t* rdram, recomp_context* context) {
    const AssetLoadRequest request{std::uint32_t(context->r4), std::uint32_t(context->r5), std::uint32_t(context->r6),
                                   std::int32_t(context->r7)};
    if (!apply_custom_payload(rdram, request))
        return 0;
    context->r2 = request.size;
    return 1;
}

// Reached only on asset_load's DMA path. A custom offset lies just past the
// section, so the retail DMA has read unrelated but in-ROM bytes into the
// destination; replacing them here keeps the retail loader untouched and needs
// no instruction patch.
extern "C" void dkr_custom_tracks_asset_load_end(std::uint8_t* rdram, recomp_context*) {
    const AssetLoadRequest request = g_load;
    g_load = AssetLoadRequest{};
    apply_custom_payload(rdram, request);
}

namespace {
bool apply_custom_payload(std::uint8_t* rdram, const AssetLoadRequest& request) {
    if (dkr::runtime::legacy::frozen_online_resources())
        return false;
    if (owned_vanilla_bootstrap())
        return false;
    Section section = Section::LevelHeaders;
    if (!section_for_data(request.section, section) || request.size <= 0 || !addressable(request.destination) ||
        std::uint32_t(request.size) > kRdramHigh - request.destination + 1U) {
        return false;
    }

    const std::uint8_t* payload = dkr::runtime::custom_tracks::payload_for(section, request.offset, request.size);
    if (payload == nullptr) {
        return false; // Retail/legacy range: leave its original owner in charge.
    }

    for (std::int32_t index = 0; index < request.size; ++index) {
        MEM_B(index, rdram_address(request.destination)) = static_cast<std::uint8_t>(payload[index]);
    }

    if (section != Section::LevelHeaders) {
        return true;
    }

    // WORLD_CUSTOM_TRACKS only files a course under Track Select's Custom
    // Tracks, and the catalogue reads it from the payload. The game itself
    // indexes five-world arrays with header->world - 1: postrace_init's mosaic
    // read past gTracksMenuBgTextureIndices, and bgdraw_texture then tiled a
    // non-texture until its display list ran out of memory. A sixth world
    // also grows gNumberOfWorlds, and with it the save file's per-world
    // fields. So the game is served the world whose background Track Select
    // already draws for that category.
    constexpr std::uint8_t kWorldDinoDomain = 1;
    if (static_cast<std::uint8_t>(payload[0]) == dkr::runtime::custom_tracks::kCustomTrackWorld) {
        MEM_B(0, rdram_address(request.destination)) = kWorldDinoDomain;
    }

    // A header names its model and object map by index, and both indices are
    // assigned when those extended tables are built. Whatever the author wrote
    // is a retail value, so leaving it would silently load the original
    // track's geometry or objects. Each field is only touched when the track
    // actually supplies that section.
    for (const HeaderFixup& fixup : kHeaderFixups) {
        if (request.size <= fixup.offset + 1) {
            continue;
        }
        const std::int32_t index = dkr::runtime::custom_tracks::sibling_index(Section::LevelHeaders, request.offset,
                                                                              fixup.section, fixup.slot);
        if (index < 0) {
            continue; // Track ships none, or that table is not built yet.
        }
        MEM_B(fixup.offset, rdram_address(request.destination)) = static_cast<std::uint8_t>((index >> 8) & 0xFF);
        MEM_B(fixup.offset + 1, rdram_address(request.destination)) = static_cast<std::uint8_t>(index & 0xFF);
        std::fprintf(stderr,
                     "[custom-tracks] header at offset %u now points at %s "
                     "%d\n",
                     request.offset, fixup.label, index);
    }

    // A track whose own minimap is not in this session's sprite table - it
    // was installed after boot, and that table is published once - would draw
    // sprite 0 with its dots placed for another picture. The header's own
    // "no minimap" bit, the one hubs use, hides it until the next launch.
    namespace tracks = dkr::runtime::custom_tracks;
    if (request.size > static_cast<std::int32_t>(tracks::kHeaderMinimapFlags) &&
        tracks::minimap_hidden(request.offset)) {
        const auto at = static_cast<std::int32_t>(tracks::kHeaderMinimapFlags);
        MEM_BU(at, rdram_address(request.destination)) |= tracks::kHeaderNoMinimap;
        std::fprintf(stderr,
                     "[custom-tracks] header at offset %u hides its minimap: "
                     "relaunch to load it\n",
                     request.offset);
    }
    return true;
}
} // namespace

// Common return convergence of get_track_id_to_load. All three retail paths -
// new game, settings->courseId, and the gTrackIdToLoad override that Track
// Select and Trophy Race drive - reach this instruction with the chosen level
// already in v0, so Track Lab replaces the answer without disturbing any of
// them or introducing a second way to pick a level.
extern "C" void dkr_custom_tracks_track_id_override(std::uint8_t*, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_online_resources())
        return;
    if (owned_vanilla_bootstrap())
        return;
    const std::int32_t armed = dkr::runtime::custom_tracks::track_override();
    if (armed == dkr::runtime::custom_tracks::kNoTrackOverride) {
        return;
    }
    context->r2 = static_cast<gpr>(armed);
}

// Immediately after mode_menu reads menu_loop's result. Auto boot answers with
// the same value the menus produce when the player picks a track, so retail
// runs its own start sequence - vehicle default, entrance, cutscene, game mode
// and load_level_game - instead of this file reproducing it.
extern "C" void dkr_custom_tracks_auto_boot(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_online_resources())
        return;
    if (owned_vanilla_bootstrap())
        return;
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (dkr_private_replay_boot(rdram, context))
        return;
#endif
    namespace tracks_ns = dkr::runtime::custom_tracks;
    if (!tracks_ns::auto_boot_enabled()) {
        return;
    }

    // The map id travels in the low seven bits of the menu result, so a track
    // beyond 127 cannot be reached this way.
    const std::int32_t level = tracks_ns::track_override();
    if (level < 0 || level > kMenuResultMapMask) {
        return; // Not resolvable yet; stay armed and try the next frame.
    }
    const auto* payload = dkr::runtime::active_payload();
    if (!payload || !payload->titlescreen_controller_assign || !payload->input_assign_players ||
        !payload->unlock_drumstick || !payload->unlock_tt || !payload->charselect_assign_ai ||
        !payload->init_racer_headers || !payload->set_time_trial_enabled) {
        return;
    }
    if (!tracks_ns::consume_auto_boot()) {
        return;
    }

    namespace addresses = dkr::runtime::revision_addresses;
    const auto invoke = [&](dkr::runtime::RecompiledEntrypoint function, gpr argument = 0) {
        dkr::runtime::GuestCallContext call(*context);
        call->r4 = argument;
        function(rdram, call.get());
        return call->r2;
    };
    // Reproduce the nonvisual character/game-select setup skipped by auto boot.
    // Merely writing a player slot leaves Settings::racers zeroed (all Krunch).
    invoke(payload->titlescreen_controller_assign, 0);
    invoke(payload->input_assign_players);
    // The character-mod adapter shares charselect_assign_ai's native commit
    // boundary and expects every active player to have confirmed a character.
    for (int player = 0; player < 4; ++player) {
        MEM_B(player, rdram_address(addresses::current().CharacterSelectStatus)) = player == 0 ? 2 : 0;
    }
    MEM_W(0, rdram_address(addresses::current().NumberOfReadyPlayers)) = 1;
    MEM_W(0, rdram_address(addresses::current().TracksMode)) = 1;
    invoke(payload->set_time_trial_enabled, 0);

    // charselect_assign_ai needs the unlock-dependent table normally selected
    // by menu_character_select_init. Do not load that menu's graphics/music.
    // Addresses verified against ver/symbols/symbol_addrs.us.v{77,80}.txt.
    const bool rev_a = addresses::gSelectedRevision == dkr::runtime::rom::Revision::UsV80;
    const bool drumstick = invoke(payload->unlock_drumstick) != 0;
    const bool tt = invoke(payload->unlock_tt) != 0;
    const auto& capture = dkr::runtime::capture_symbols::for_revision(rev_a);
    const unsigned table = unsigned(drumstick) | (unsigned(tt) << 1);
    write_word(rdram, capture.select_data, static_cast<std::int32_t>(capture.tables[table]));
    MEM_B(0, rdram_address(addresses::current().CharacterIdSlots)) = kCharacterDiddy;
    invoke(payload->charselect_assign_ai, 1);
    invoke(payload->init_racer_headers);
    std::fprintf(stderr, "[track-lab] single-player roster:");
    for (int racer = 0; racer < 8; ++racer) {
        std::fprintf(stderr, " %d", int(MEM_B(racer, rdram_address(addresses::current().CharacterIdSlots))));
    }
    std::fprintf(stderr, "\n");
    // mode_menu's direct-level result uses players minus one.
    write_word(rdram, capture.game_players, 0);

    context->r2 = static_cast<gpr>(kMenuResultStartLevel | level);
}

// mmInit sizes DKR's main pool to the 4 MB console (0x80400000), while the
// runtime maps and reports 8 MB that nothing else uses. A .dkrmap track can
// ship up to 255 64x32 textures on top of eight distinct racers, which the
// retail pool cannot hold: allocations then return NULL and the next particle
// or HUD setup writes through it. So the pool's tail is grown over the unused
// expansion RAM, once. The allocator itself is untouched and the pool never
// shrinks. Returns the bytes added, or 0 when the pool is already grown or is
// not one this code understands.
static std::uint32_t grow_main_pool(std::uint8_t* rdram) {
    constexpr std::uint32_t kRetailRamEnd = 0x80400000U;
    constexpr std::uint32_t kExpansionRamEnd = 0x80800000U;
    constexpr std::uint32_t kSlotSize = 0x14U;       // MemoryPoolSlot in memory.h
    constexpr std::int32_t kSlotIndexLimit = 0x7FFF; // slot links are s16
    // gMemoryPools, verified against ver/symbols/symbol_addrs.us.v{77,80}.txt.
    // Pool 0 is the main pool; MemoryPool is {maxNumSlots, curNumSlots, slots, size}.
    const std::uint32_t pool =
        dkr::runtime::memory_symbols::for_revision(dkr::runtime::revision_addresses::gSelectedRevision ==
                                                   dkr::runtime::rom::Revision::UsV80)
            .pools;
    const std::int32_t max_slots = read_word(rdram, pool);
    const std::int32_t used_slots = read_word(rdram, pool + 4U);
    const auto slots = static_cast<std::uint32_t>(read_word(rdram, pool + 8U));
    if (!addressable(slots) || max_slots <= 0 || max_slots > kSlotIndexLimit || used_slots <= 0 ||
        used_slots >= max_slots) {
        return 0;
    }
    const auto slot = [&](std::int32_t index) { return slots + static_cast<std::uint32_t>(index) * kSlotSize; };
    // The slot list is kept in address order, so its tail ends the pool.
    std::int32_t tail = 0;
    for (std::int32_t steps = 0;; ++steps) {
        const std::int32_t next = static_cast<std::int16_t>(MEM_H(12, rdram_address(slot(tail))));
        if (next == -1) {
            break;
        }
        if (next < 0 || next >= max_slots || steps >= max_slots) {
            return 0; // Not a pool this code understands; leave it alone.
        }
        tail = next;
    }
    const auto tail_data = static_cast<std::uint32_t>(read_word(rdram, slot(tail)));
    const std::int32_t tail_size = read_word(rdram, slot(tail) + 4U);
    const std::uint32_t end = tail_data + static_cast<std::uint32_t>(tail_size);
    // mmInit aligns the first slot's data without shrinking it, so the retail
    // end can sit a few bytes past 0x80400000. Anything else is already grown.
    if (tail_size < 0 || end < kRetailRamEnd || end >= kRetailRamEnd + 16U) {
        return 0;
    }
    const std::uint32_t extra = kExpansionRamEnd - end;
    if (MEM_H(8, rdram_address(slot(tail))) == 0) { // SLOT_FREE
        write_word(rdram, slot(tail) + 4U, tail_size + static_cast<std::int32_t>(extra));
    } else {
        // mempool_slot_assign's split: slots[curNumSlots].index is the next
        // spare slot, and mempool_slot_find refuses the pool's last one.
        if (used_slots + 1 >= max_slots) {
            return 0;
        }
        const std::int32_t spare = static_cast<std::int16_t>(MEM_H(14, rdram_address(slot(used_slots))));
        if (spare < 0 || spare >= max_slots) {
            return 0;
        }
        write_word(rdram, slot(spare), static_cast<std::int32_t>(end));
        write_word(rdram, slot(spare) + 4U, static_cast<std::int32_t>(extra));
        MEM_H(8, rdram_address(slot(spare))) = 0;
        MEM_H(10, rdram_address(slot(spare))) = static_cast<std::int16_t>(tail);
        MEM_H(12, rdram_address(slot(spare))) = -1;
        write_word(rdram, slot(spare) + 16U, 0);
        MEM_H(12, rdram_address(slot(tail))) = static_cast<std::int16_t>(spare);
        write_word(rdram, pool + 4U, used_slots + 1);
    }
    write_word(rdram, pool + 12U, read_word(rdram, pool + 12U) + static_cast<std::int32_t>(extra));
    return extra;
}

// Decides the level model heap for the level about to load, and leaves it for
// the generate_track hook below.
//
// generate_track reserves LEVEL_MODEL_MAX_SIZE (0x82A00) for the inflated blob
// AND the arena track_init_collision appends past modelSize - sixteen bytes per
// collision plane, which is where most of a large track's memory goes. Retail
// checks the total only to report it through rmonPrintf, which this build
// stubs, and then writes past the heap regardless: straight over
// gCollisionCandidates, gCollisionSurfaces and the pool slot list behind them.
// The symptom is a wild pointer several frames later, never the overflow.
//
// A .dkrmap track can exceed it honestly - Bluey, retail's largest, is near 80%
// of the budget with a fraction of the triangles an exported track carries. So
// the arena is measured from the payload here and the heap sized to fit. Every
// other level leaves this at zero and keeps the retail heap byte for byte.
static void prepare_track_heap(std::int32_t level) {
    namespace custom_tracks = dkr::runtime::custom_tracks;
    g_track_heap_bytes = 0;
    if (!custom_tracks::owns_level_id(level)) {
        return;
    }
    const std::int32_t arena = custom_tracks::level_model_arena_bytes(level);
    if (arena <= custom_tracks::kRetailTrackHeap) {
        return; // Fits retail, or the payload could not be measured.
    }
    const std::int64_t wanted = (std::int64_t(arena) + kTrackHeapSlack + 15) & ~std::int64_t(15);
    if (wanted > kTrackHeapCeiling) {
        // Saying so is the whole point: past here the track corrupts memory the
        // same way it did before, and nothing else in the log would name it.
        std::fprintf(stderr,
                     "[custom-tracks] level %d needs %d bytes of level model and "
                     "collision data, over the %d this runtime can reserve; it "
                     "will overflow. Reduce collidable triangles or geometry\n",
                     static_cast<int>(level), static_cast<int>(arena), static_cast<int>(kTrackHeapCeiling));
        g_track_heap_bytes = kTrackHeapCeiling;
        return;
    }
    g_track_heap_bytes = static_cast<std::int32_t>(wanted);
    // Only the measurement is reported here. The line that says the heap was
    // raised belongs to the hook that raises it, so a payload built without
    // that hook - which is easy to do, since the call lives in regenerated
    // code - shows this line and not that one, instead of claiming both.
    std::fprintf(stderr,
                 "[custom-tracks] level %d builds %d bytes of level model and "
                 "collision data, over the retail %d\n",
                 static_cast<int>(level), static_cast<int>(arena), static_cast<int>(custom_tracks::kRetailTrackHeap));
}

static void grow_main_pool_for_level(std::uint8_t* rdram, std::int32_t level) {
    if (!dkr::runtime::custom_tracks::owns_level_id(level)) {
        return; // Retail and legacy levels never grow the pool.
    }
    const std::uint32_t extra = grow_main_pool(rdram);
    if (extra != 0) {
        std::fprintf(stderr, "[custom-tracks] main memory pool grown into expansion RAM for level %d (+%u KB)\n",
                     static_cast<int>(level), extra / 1024U);
    }
}

// The most batches any .dkrmap course in Track Select draws.
static std::int32_t track_select_batches() {
    namespace custom_tracks = dkr::runtime::custom_tracks;
    std::int32_t most = 0;
    for (const custom_tracks::TrackSelectEntry& entry : custom_tracks::track_select_entries()) {
        most = std::max(most, custom_tracks::level_model_batches(entry.level_id));
    }
    return most;
}

// Track Select previews a course by loading it behind the menu, through
// load_level_for_menu - often on thread30 while the main thread keeps drawing
// the menu - and renders it into the one-player display list the menu already
// holds. Nothing on that path resizes the list, and it cannot be resized
// safely there. So the budget is decided before the lists first exist:
// level_global_init loads the header table (publishing each course's level
// id) right before default_alloc_displaylist_heap, and gDisplayLists is still
// NULL then. Every later load keeps this floor; see write_display_list_budget.
static void size_menu_display_lists(std::uint8_t* rdram) {
    const DisplayListGlobals globals = display_list_globals();
    if (read_word(rdram, globals.lists) != 0) {
        return; // Past boot: the lists exist and are sized per level.
    }
    const std::int32_t batches = track_select_batches();
    g_menu_commands = batches > 0 ? commands_for(0, batches) : 0;
    if (batches <= 0) {
        return;
    }
    // The larger lists live for the whole session; keep the retail headroom.
    const std::uint32_t extra = grow_main_pool(rdram);
    if (extra != 0) {
        std::fprintf(stderr,
                     "[custom-tracks] main memory pool grown into expansion RAM for Track Select "
                     "previews (+%u KB)\n",
                     extra / 1024U);
    }
    write_display_list_budget(rdram, -1);
    std::fprintf(stderr,
                 "[custom-tracks] Track Select previews draw up to %d batches; display lists sized "
                 "for %d commands with one player\n",
                 static_cast<int>(batches), static_cast<int>(g_menu_commands));
}

namespace {
dkr::runtime::custom_tracks::LevelLoadObserver g_level_load_observer = nullptr;
} // namespace

void dkr::runtime::custom_tracks::set_level_load_observer(LevelLoadObserver observer) {
    g_level_load_observer = observer;
}

// A boss race that has not been visited is not loaded at all the first time:
// level_load (game.c, the RACETYPE_BOSS branch) pushes it and loads the boss's
// introduction scene instead, found by scanning ASSET_MISC_67 for pairs of
// {boss level, intro level} until the level id matches. A .dkrmap level is in
// no such pair, so the scan runs past the table and loads whatever level id it
// meets - the camera then sits frozen in an unrelated scene. Marking the course
// visited skips that intro. racer_boss_finish scans the same table before its
// cleared check; the custom-boss-finish guest hook skips that lookup and selects
// the ordinary rematch transition explicitly. world 6 is served as Dino
// Domain, so the Wizpig worlds' unconditional intro never applies to a .dkrmap
// header, and a boss level never runs a silver coin race (objects.c tests
// level_type()), so the cleared bit unlocks nothing.
//
// The bits are only lent. Whatever this function set is taken back when another
// level loads and, through return_lent_boss_flags, wherever Track Select takes
// back its lent vehicle choice. The current save layout serializes stock course
// IDs only; these transient flags must still not outlive the custom level.
namespace {
struct LentBossFlags {
    std::int32_t level = -1;
    std::int32_t bits = 0;
};
LentBossFlags g_lent_boss_flags{};
constexpr std::uint32_t kCourseFlagsPointer = 4U; // Settings::courseFlagsPtr
constexpr std::int32_t kRaceVisitedAndCleared = 1 | 2;

// The address of courseFlags[level], or 0 when it cannot be reached.
std::uint32_t course_flags_at(std::uint8_t* rdram, recomp_context* context, std::int32_t level) {
    const auto* payload = dkr::runtime::active_payload();
    if (level < 0 || !payload || !payload->get_settings) {
        return 0U;
    }
    dkr::runtime::GuestCallContext call(*context);
    payload->get_settings(rdram, call.get());
    const auto settings = static_cast<std::uint32_t>(call->r2);
    if (!addressable(settings + kCourseFlagsPointer)) {
        return 0U;
    }
    const auto flags = static_cast<std::uint32_t>(read_word(rdram, settings + kCourseFlagsPointer));
    const std::uint32_t at = flags + 4U * static_cast<std::uint32_t>(level);
    return addressable(flags) && addressable(at) ? at : 0U;
}
} // namespace

// Called by the funcs-only finish hook after get_settings. Consult host metadata
// without changing guest registers or memory; stock and online bosses stay retail.
extern "C" int dkr_custom_tracks_is_boss_level(std::int32_t level) {
    if (dkr::runtime::legacy::frozen_online_resources() || owned_vanilla_bootstrap()) {
        return 0;
    }
    return dkr::runtime::custom_tracks::level_race_type(level) == dkr::runtime::custom_tracks::kRaceTypeBoss;
}

void dkr::runtime::custom_tracks::return_lent_boss_flags(std::uint8_t* rdram, recomp_context* context,
                                                         std::int32_t keep_level) {
    const LentBossFlags lent = g_lent_boss_flags;
    if (lent.level < 0 || lent.level == keep_level) {
        return;
    }
    g_lent_boss_flags = {};
    if (const std::uint32_t at = course_flags_at(rdram, context, lent.level)) {
        write_word(rdram, at, read_word(rdram, at) & ~lent.bits);
    }
}

static void skip_custom_boss_intro(std::uint8_t* rdram, recomp_context* context, std::int32_t level) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    tracks_ns::return_lent_boss_flags(rdram, context, level);
    if (level < 0 || tracks_ns::level_race_type(level) != tracks_ns::kRaceTypeBoss) {
        return;
    }
    const std::uint32_t at = course_flags_at(rdram, context, level);
    if (at == 0U) {
        return;
    }
    const std::int32_t value = read_word(rdram, at);
    const std::int32_t missing = kRaceVisitedAndCleared & ~value;
    if (missing != 0) {
        write_word(rdram, at, value | missing);
        g_lent_boss_flags = {level, (g_lent_boss_flags.level == level ? g_lent_boss_flags.bits : 0) | missing};
        std::fprintf(stderr, "[custom-tracks] level %d: boss race runs without the retail intro and outro scenes\n",
                     static_cast<int>(level));
    }
}

// Called by the existing level_load scene-reset hook, before the level
// allocates anything. Every load passes here - races, Track Lab and restarts,
// and also the Track Select previews that load_level_game's hook never sees -
// so this is where a .dkrmap level gets its pool and its model heap.
extern "C" void dkr_custom_tracks_prepare_memory(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_course_hook("prepare_memory", rdram, context))
        return;
    if (owned_vanilla_bootstrap()) {
        // A previous offline Track Lab song must not survive a vanilla online
        // boot. The observer's no-level branch clears only native bindings.
        if (g_level_load_observer != nullptr)
            g_level_load_observer(-1);
        return;
    }
    const auto level = static_cast<std::int32_t>(context->r4);
    grow_main_pool_for_level(rdram, level);
    prepare_track_heap(level);
    skip_custom_boss_intro(rdram, context, level);
    if (g_level_load_observer != nullptr) {
        g_level_load_observer(level);
    }

    // A preview that outgrows the display list it is drawn into runs over the
    // matrices behind it. The boot sizing covers every course Track Select
    // offered then; a course enabled later is named here rather than guessed at.
    const std::int32_t batches = dkr::runtime::custom_tracks::level_model_batches(level);
    const std::int32_t held = read_word(rdram, display_list_globals().current_commands);
    if (batches > 0 && held > 0 && held < commands_for(0, batches)) {
        std::fprintf(stderr,
                     "[custom-tracks] level %d draws up to %d batches, more than the %d commands "
                     "this display list holds; restart the game before previewing it\n",
                     static_cast<int>(level), static_cast<int>(batches), static_cast<int>(held));
    }
}

// Called inside generate_track, on the instruction after the one that finishes
// materialising LEVEL_MODEL_MAX_SIZE into s5. That register carries the
// constant to both of the places that matter in the same function:
//
//   8002c0f4  lui   s5, 0x0008
//   8002c0f8  ori   s5, s5, 0x2a00    <- the constant is complete here
//   8002c104  jal   mempool_alloc_safe
//   8002c108  or    a0, s5, zero      <- delay slot: the heap's size
//   8002c1c0  addu  t6, s0, s5        <- where the compressed blob is landed,
//                                        at the heap's tail, before inflating
//
// so one write moves the reservation and keeps the compressed payload at the
// tail of the larger heap, which is what guarantees the inflate cannot overrun
// its own source. The third use, the overflow test, only feeds the stubbed
// rmonPrintf. Anything other than the retail constant in s5 means this is not
// the instruction this code believes it is - a revision whose prologue differs
// keeps the retail heap instead of having a register it misread overwritten.
extern "C" void dkr_custom_tracks_track_heap(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_course_hook("track_heap", rdram, context))
        return;
    if (owned_vanilla_bootstrap())
        return;
    const std::int32_t wanted = g_track_heap_bytes;
    g_track_heap_bytes = 0;
    if (wanted <= dkr::runtime::custom_tracks::kRetailTrackHeap) {
        return;
    }
    if (static_cast<std::int32_t>(context->r21) != dkr::runtime::custom_tracks::kRetailTrackHeap) {
        std::fprintf(stderr, "[custom-tracks] generate_track does not hold the retail "
                             "track heap size where it was expected; leaving it alone\n");
        return;
    }
    context->r21 = static_cast<gpr>(wanted);
    std::fprintf(stderr, "[custom-tracks] track heap raised from %d to %d bytes\n",
                 static_cast<int>(dkr::runtime::custom_tracks::kRetailTrackHeap), static_cast<int>(wanted));
}

// Called at load_level_game's entry, before alloc_displaylist_heap. DKR sizes
// each frame's display list from gNumF3dCmdsPerPlayer (4500 commands for one
// player), and the matrix heap follows it directly. render_level_segment spends
// three to ten commands on every visible batch, and a .dkrmap model can have
// far more batches than a retail one (1214 in a 68-segment export whose PVS
// sees everything). Past the budget the list runs into the matrices the same
// frame writes, and the renderer receives garbage. So a custom level gets a
// budget for its whole model in every viewport, and every other level gets the
// retail table back - raised to the Track Select floor when the menu previews
// .dkrmap courses. A changed table invalidates gPrevPlayerCount, and that
// makes the retail allocator rebuild the heap in this same call. The model
// heap is left to level_load's hook, which this load reaches next.
extern "C" void dkr_custom_tracks_prepare_level(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_course_hook("prepare_level", rdram, context))
        return;
    if (owned_vanilla_bootstrap())
        return;
    const auto level = static_cast<std::int32_t>(context->r4);
    // The heap alloc_displaylist_heap is about to build comes out of the pool.
    grow_main_pool_for_level(rdram, level);

    const std::int32_t batches = dkr::runtime::custom_tracks::level_model_batches(level);
    const DisplayListGlobals globals = display_list_globals();
    if (!write_display_list_budget(rdram, batches)) {
        return;
    }
    write_word(rdram, globals.previous_players, -1);
    if (batches > 0) {
        std::fprintf(stderr,
                     "[custom-tracks] level %d draws up to %d batches; display lists sized for "
                     "%d commands with one player\n",
                     static_cast<int>(level), static_cast<int>(batches),
                     static_cast<int>(read_word(rdram, globals.table)));
    }
}

// Called by the existing level_load scene-reset hook before retail consumes
// the vehicle argument. Only gameplay on the armed track changes; menus and
// Track Select retain their choices. Also covers L+Z and switching test tracks.
extern "C" void dkr_custom_tracks_prepare_vehicle(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_online_resources())
        return;
    if (owned_vanilla_bootstrap())
        return;
    namespace addresses = dkr::runtime::revision_addresses;
    const auto level = static_cast<std::int32_t>(context->r4);
    const auto players_minus_one = static_cast<std::int32_t>(context->r5);
    const auto armed = dkr::runtime::custom_tracks::track_override();
    if (armed < 0 || level != armed || players_minus_one < 0 || players_minus_one >= 4 ||
        read_word(rdram, addresses::current().GameMode) != 0) { // GAMEMODE_INGAME
        return;
    }
    const auto* payload = dkr::runtime::active_payload();
    if (!payload || !payload->leveltable_vehicle_default || !payload->leveltable_vehicle_usable ||
        !payload->set_level_default_vehicle) {
        return;
    }
    dkr::runtime::GuestCallContext guest(*context);
    recomp_context& call = guest.context;
    payload->leveltable_vehicle_default(rdram, guest.get());
    const auto vehicle = static_cast<std::int32_t>(call.r2);
    auto selected = vehicle;
    // The addon's debug override may name a boss/special vehicle. Preserve it
    // as the load argument, but player-selection arrays index three vehicle
    // records and must remain within the authored normal vehicles for the AI.
    if (selected < 0 || selected >= 3) {
        guest.reset(*context);
        payload->leveltable_vehicle_usable(rdram, guest.get());
        const auto allowed = static_cast<std::uint32_t>(call.r2);
        selected = (allowed & 1U) ? 0 : (allowed & 2U) ? 1 : (allowed & 4U) ? 2 : 0;
    }
    for (int player = 0; player <= players_minus_one; ++player) {
        MEM_B(player, rdram_address(addresses::current().PlayerSelectVehicle)) = selected;
    }
    guest.reset(*context);
    call.r4 = vehicle;
    payload->set_level_default_vehicle(rdram, guest.get());
    context->r7 = static_cast<gpr>(vehicle);
    std::fprintf(stderr, "[track-lab] level=%d vehicle=%d selected=%d players=%d\n", level, vehicle, selected,
                 players_minus_one + 1);
}

// In track_spawn_objects, on the instruction after gzip_inflate returns
// (0x8000CAC8 in US 1.0 and Rev A alike) and before any entry is spawned:
// s0 still holds the object map's asset offset, the one asset_load was given,
// and 0x48(sp) the buffer the map was inflated into - a 16-byte header that
// starts with the entries' byte count, then the entries. A hub EXIT that leads to another .dkrmap track was exported
// with a placeholder destination, since that track's level id depends on what is installed; the package's levelLinks
// name the byte and the track, and the id resolved for this session is written in before the exit spawns. A link whose
// track is missing keeps the placeholder (the exporter writes a retail hub).
extern "C" void dkr_custom_tracks_object_map_links(std::uint8_t* rdram, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_online_resources())
        return;
    if (owned_vanilla_bootstrap())
        return;
    const auto offset = static_cast<std::uint32_t>(context->r16);
    const auto links = dkr::runtime::custom_tracks::level_links_for_object_map(offset);
    if (links.empty()) {
        return;
    }
    namespace tracks_ns = dkr::runtime::custom_tracks;
    const auto stack = static_cast<std::uint32_t>(context->r29);
    if (!addressable(stack + 0x48U)) {
        return;
    }
    const auto buffer = static_cast<std::uint32_t>(read_word(rdram, stack + 0x48U));
    // The whole OBJECT_MAP_SIZE buffer must be reachable. The entry byte count
    // comes from the inflated map, which a package controls, so it only ever
    // narrows the range inside that buffer, never widens it.
    if (!addressable(buffer) || !addressable(buffer + tracks_ns::kObjectMapBufferBytes - 1U)) {
        return;
    }
    const std::int32_t entry_bytes = read_word(rdram, buffer);
    const std::uint32_t entries_end =
        0x10U + std::min<std::uint32_t>(entry_bytes > 0 ? static_cast<std::uint32_t>(entry_bytes) : 0U,
                                        tracks_ns::kObjectMapBufferBytes - 0x10U);
    for (const auto& link : links) {
        if (link.offset < 0x10U || link.offset >= entries_end) {
            std::fprintf(stderr, "[custom-tracks] level link to %s at byte %u is outside the object map\n",
                         link.track.c_str(), link.offset);
            continue;
        }
        if (link.level_id < 0 || link.level_id > tracks_ns::kMaxLevelId) {
            std::fprintf(stderr, "[custom-tracks] level link to %s: track not installed, exit keeps its placeholder\n",
                         link.track.c_str());
            continue;
        }
        MEM_B(static_cast<std::int32_t>(link.offset), rdram_address(buffer)) = static_cast<std::int8_t>(link.level_id);
        std::fprintf(stderr, "[custom-tracks] level link to %s now leads to level %d\n", link.track.c_str(),
                     static_cast<int>(link.level_id));
    }
}

namespace {
// Guest threads (simulation and renderer) can both reach the shadow hooks, so the
// count is atomic. It reports the first drop and then every power of two, which
// keeps a floor that drops a piece every frame from filling the log.
std::atomic<std::uint32_t> g_shadow_drops{0};
void note_shadow_drop(const char* what, std::int32_t a = 0, std::int32_t b = 0) {
    const std::uint32_t count = g_shadow_drops.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if ((count & (count - 1U)) == 0U) {
        std::fprintf(stderr, "[custom-tracks] %s (%d, %d); the rest of that shadow is dropped (%u so far)\n", what, a,
                     b, count);
    }
}
} // namespace

// Shadow generation (tracks.c) keeps its work in fixed arrays that its appends never
// bound-check: D_8011B120[32] (vertices, counted by D_8011B118), D_8011C238[32]
// (polygons, counted by D_8011C230, each with unk2[8]) and D_8011B330[120] (clip
// points, in four groups of 32 counted by D_8011B320[4], although the array holds
// only 120: the fourth group's entries 24 to 31 land on D_8011C230 and the polygon
// array behind it). Retail floors do not overflow them, but a custom floor cut into
// many small faces can: under a .dkrmap's covered plaza a plane's shadow gathered
// more than 32 vertices, ran over the arrays behind them into the polygon count, and
// the next store through that count wrote far outside RDRAM (stray triangles on
// screen, then an access violation).
//
// These funcs-only hooks protect offline play. The owned online payload is
// unchanged. In particular, the polygon array must not spill into D_8011C3B8:
// that storage is live input during the water pass.

// After func_8002FF6C returns the clipped polygon's vertex count in v0, before the
// caller's `someCount >= 3` test: the polygon's vertices are at sp+`vertices_at`
// (the clip's a1), 0x10 bytes each. The caller then appends the polygon to
// D_8011C238 and each vertex whose unkE is negative to D_8011B120 unless an equal
// (x, z) is already there, counting the ones this polygon has just added. When that
// would pass an array v0 is set to 0, and the polygon is skipped.
static void shadow_polygon_budget(std::uint8_t* rdram, recomp_context* context, std::uint32_t vertices_at) {
    if (dkr::runtime::legacy::frozen_online_resources() || owned_vanilla_bootstrap())
        return;
    const auto& addresses = dkr::runtime::shadow_symbols::for_revision(
        dkr::runtime::revision_addresses::gSelectedRevision == dkr::runtime::rom::Revision::UsV80);
    constexpr std::int32_t kVertexSlots = 32;    // D_8011B120[32]: entry 32 is the clip counters
    constexpr std::int32_t kPolygonVertices = 8; // D_8011C238[n].unk2[8]
    constexpr std::int32_t polygon_limit = 32;   // D_8011C238[32]; live water input follows
    const auto clipped = static_cast<std::int32_t>(context->r2);
    if (clipped < 3) {
        return;
    }
    const std::int32_t vertices = read_word(rdram, addresses.vertex_count);
    const std::int32_t polygons = read_word(rdram, addresses.polygon_count);
    if (polygons < 0 || polygons >= polygon_limit) {
        context->r2 = 0;
        note_shadow_drop("a shadow's polygons would run onto the water input after the polygon array", polygons,
                         polygon_limit);
        return;
    }
    if (clipped > kPolygonVertices || vertices < 0 || vertices > kVertexSlots) {
        context->r2 = 0;
        note_shadow_drop("a shadow polygon has more vertices than the game's polygon entry holds", clipped, vertices);
        return;
    }
    // The polygon is on the guest's own stack, written a moment ago by the clip. Its place
    // is bounded by the guest address space itself, the 16 MiB DKR runs in (the 8 MiB base
    // image plus the project's extended region; the owned engine keeps its stack at
    // 0x80FDFC50, so the retail 8 MiB is not the limit). A stack pointer that puts it
    // anywhere else is refused rather than read.
    const std::uint32_t polygon = static_cast<std::uint32_t>(context->r29) + vertices_at;
    const std::uint64_t polygon_end = std::uint64_t{polygon} + 0x10U * static_cast<std::uint64_t>(clipped);
    if (polygon < kRdramLow || polygon_end > kRdramLow + dkr::runtime::netplay::kRollbackMemoryBytes) {
        context->r2 = 0;
        note_shadow_drop("a shadow polygon is outside guest memory", static_cast<std::int32_t>(polygon), clipped);
        return;
    }
    const auto floats_equal = [&](std::uint32_t a, std::uint32_t b) {
        // x at +0 and z at +8, compared as floats (so 0.0 == -0.0, NaN never equal).
        const std::int32_t words[4] = {read_word(rdram, a), read_word(rdram, a + 8U), read_word(rdram, b),
                                       read_word(rdram, b + 8U)};
        const float ax = std::bit_cast<float>(words[0]), az = std::bit_cast<float>(words[1]);
        const float bx = std::bit_cast<float>(words[2]), bz = std::bit_cast<float>(words[3]);
        return ax == bx && az == bz;
    };
    std::int32_t added = 0;
    std::uint32_t added_at[kPolygonVertices]{};
    for (std::int32_t i = 0; i < clipped; ++i) {
        const std::uint32_t vertex = polygon + 0x10U * static_cast<std::uint32_t>(i);
        if (static_cast<std::int16_t>(read_word(rdram, vertex + 0xCU) & 0xFFFF) >= 0) {
            continue; // An existing index: nothing is appended.
        }
        bool found = false;
        for (std::int32_t j = 0; j < vertices && !found; ++j) {
            found = floats_equal(vertex, addresses.vertices + 0x10U * static_cast<std::uint32_t>(j));
        }
        for (std::int32_t j = 0; j < added && !found; ++j) {
            found = floats_equal(vertex, added_at[j]);
        }
        if (!found) {
            added_at[added++] = vertex;
            if (vertices + added > kVertexSlots) {
                context->r2 = 0;
                note_shadow_drop("a shadow's vertices would run onto the clip counters after the vertex array",
                                 vertices, added);
                return;
            }
        }
    }
}

// func_8002E904 (floor shadows): the clipped polygon is at sp+0xD0.
extern "C" void dkr_shadow_polygon_budget(std::uint8_t* rdram, recomp_context* context) {
    shadow_polygon_budget(rdram, context, 0xD0U);
}

// func_8002EEEC (water shadows) appends the same way, from sp+0xA8.
extern "C" void dkr_shadow_water_polygon_budget(std::uint8_t* rdram, recomp_context* context) {
    shadow_polygon_budget(rdram, context, 0xA8U);
}

// Inside func_8002FF6C, where it appends a clip point to D_8011B330: v1 is the
// entry's index, group * 32 + the group's count (the count is clamped at 31 just
// before). D_8011B330 holds 120 entries, so an index of 120 or more is a write
// past the array. It is redirected to the last entry (119), which overwrites one
// clip point of this shadow, instead of the polygon count and the arrays behind.
// Both callers go through here, so this covers floor and water shadows.
extern "C" void dkr_shadow_clip_index(std::uint8_t*, recomp_context* context) {
    if (dkr::runtime::legacy::frozen_online_resources() || owned_vanilla_bootstrap())
        return;
    constexpr std::uint32_t kEntries = 120; // D_8011B330[120]
    const auto index = static_cast<std::uint32_t>(context->r3);
    if (index >= kEntries) {
        context->r3 = static_cast<gpr>(kEntries - 1U);
        // The guest increments a0 and saves it as this group's count. Saturate
        // that too, or the next edge reads entries beyond the 120-point array.
        context->r4 = 23;
        note_shadow_drop("a shadow's clip points would run onto the polygon count after the clip array",
                         static_cast<std::int32_t>(index), static_cast<std::int32_t>(kEntries));
    }
}
