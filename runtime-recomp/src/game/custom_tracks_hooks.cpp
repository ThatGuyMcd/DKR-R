#include "custom_tracks.hpp"
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "replay_probe_capture.hpp"
#endif

#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "rom_revision.hpp"

#include "recomp.h"

#include <algorithm>
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
using dkr::runtime::custom_tracks::Section;

struct SectionMapping {
    std::uint32_t table_index;
    std::uint32_t data_index;
    Section section;
};

constexpr SectionMapping kSectionMappings[] = {
    {3U, 2U, Section::Textures3D},
    {20U, 21U, Section::LevelObjectMaps},
    {22U, 23U, Section::LevelHeaders},
    {24U, 25U, Section::LevelNames},
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
constexpr std::uint32_t kMaxTableEntries = 4096U;

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

void write_word(std::uint8_t* rdram, std::uint32_t address,
                std::int32_t value) {
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
    if (dkr::runtime::revision_addresses::gSelectedRevision ==
        dkr::runtime::rom::Revision::UsV80) {
        return {0x800DD920U, 0x80123A8CU, 0x80123AA8U, 0x80121770U};
    }
    return {0x800DD3B0U, 0x8012350CU, 0x80123528U, 0x801211F0U};
}

// A frame's budget with `players` viewports (0 is one player) for a level
// drawing `batches`, or the retail budget when it is not a .dkrmap level.
std::int32_t commands_for(int players, std::int32_t batches) {
    if (batches <= 0) {
        return kRetailCommands[players];
    }
    const std::int64_t budget = std::int64_t(kRetailCommands[players]) +
                                std::int64_t(batches) * kCommandsPerBatch * (players + 1);
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
extern "C" void dkr_custom_tracks_table_load_begin(std::uint8_t*,
                                                    recomp_context* context) {
    g_requested_table = static_cast<std::uint32_t>(context->r4);
}

static void size_menu_display_lists(std::uint8_t* rdram);
static void publish_extended_table(std::uint8_t* rdram, recomp_context* context,
                                   std::uint32_t requested);

// Both the retail epilogue and the mounted legacy namespace reach this: the
// namespace can serve the table without ever running the retail loader.
// Extend the returned table, then size the Track Select display lists the
// appended levels need - the sizing is idempotent past boot.
extern "C" void dkr_custom_tracks_extend_table(std::uint8_t* rdram, recomp_context* context,
                                               std::uint32_t requested) {
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
static void publish_extended_table(std::uint8_t* rdram, recomp_context* context,
                                   std::uint32_t requested) {
    Section section = Section::LevelHeaders;
    if (!section_for_table(requested, section)) {
        return; // Not a section custom tracks contribute to.
    }

    const auto retail_address = static_cast<std::uint32_t>(context->r2);
    if (!addressable(retail_address)) {
        return; // Retail returned NULL; leave the failure exactly as authored.
    }

    std::vector<std::int32_t> retail;
    retail.reserve(64);
    for (std::uint32_t index = 0; index < kMaxTableEntries; ++index) {
        const std::int32_t entry =
            read_word(rdram, retail_address + (index * 4U));
        retail.push_back(entry);
        if (entry == -1) {
            break;
        }
    }
    if (retail.empty() || retail.back() != -1) {
        return; // Unterminated: refuse rather than guess.
    }

    const std::vector<std::int32_t> extended =
        dkr::runtime::custom_tracks::build_extended_table(section,
                                                          retail.data());
    if (extended.empty()) {
        return; // Nothing added; the retail table stands unchanged.
    }

    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->mempool_alloc_safe == nullptr) {
        return;
    }

    recomp_context call = *context;
    call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h;
    call.r4 = static_cast<gpr>(extended.size() * sizeof(std::int32_t));
    call.r5 = static_cast<gpr>(kColourTagGrey);
    payload->mempool_alloc_safe(rdram, &call);
    const auto allocated = static_cast<std::uint32_t>(call.r2);
    if (!addressable(allocated)) {
        return; // Out of pool: keep the retail table rather than fail the load.
    }

    for (std::size_t index = 0; index < extended.size(); ++index) {
        write_word(rdram, allocated + static_cast<std::uint32_t>(index * 4U),
                   extended[index]);
    }
    context->r2 = static_cast<gpr>(static_cast<std::int32_t>(allocated));
    if(payload->asset_release) {
        call=*context;call.r4=rdram_address(retail_address);
        payload->asset_release(rdram,&call);
    }
}

// Common epilogue of asset_table_load.
extern "C" void dkr_custom_tracks_table_load_end(std::uint8_t* rdram,
                                                  recomp_context* context) {
    const std::uint32_t requested = g_requested_table;
    g_requested_table = 0xFFFFFFFFU;
    dkr_custom_tracks_extend_table(rdram, context, requested);
}

// Entry of asset_load. The destination register is clobbered by the DMA call
// before the epilogue is reached, so every argument is captured here.
extern "C" void dkr_custom_tracks_asset_load_begin(std::uint8_t*,
                                                    recomp_context* context) {
    g_load.section = static_cast<std::uint32_t>(context->r4);
    g_load.destination = static_cast<std::uint32_t>(context->r5);
    g_load.offset = static_cast<std::uint32_t>(context->r6);
    g_load.size = static_cast<std::int32_t>(context->r7);
}

namespace {
bool apply_custom_payload(std::uint8_t* rdram,const AssetLoadRequest& request);
}

// Consult only ranges assigned by build_extended_table. This must precede
// the legacy bank's strict section-bounds check: appended Blender bytes are
// owned by this loader, not by the immutable legacy source bank.
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t* rdram,recomp_context* context) {
    const AssetLoadRequest request{std::uint32_t(context->r4),std::uint32_t(context->r5),
        std::uint32_t(context->r6),std::int32_t(context->r7)};
    if(!apply_custom_payload(rdram,request))return 0;
    context->r2=request.size;return 1;
}

// Reached only on asset_load's DMA path. A custom offset lies just past the
// section, so the retail DMA has read unrelated but in-ROM bytes into the
// destination; replacing them here keeps the retail loader untouched and needs
// no instruction patch.
extern "C" void dkr_custom_tracks_asset_load_end(std::uint8_t* rdram,
                                                  recomp_context*) {
    const AssetLoadRequest request = g_load;
    g_load = AssetLoadRequest{};
    apply_custom_payload(rdram,request);
}

namespace {
bool apply_custom_payload(std::uint8_t* rdram,const AssetLoadRequest& request) {
    if (owned_vanilla_bootstrap()) return false;
    Section section = Section::LevelHeaders;
    if (!section_for_data(request.section, section) || request.size <= 0 ||
        !addressable(request.destination) || std::uint32_t(request.size)>kRdramHigh-request.destination+1U) {
        return false;
    }

    const std::uint8_t* payload = dkr::runtime::custom_tracks::payload_for(
        section, request.offset, request.size);
    if (payload == nullptr) {
        return false; // Retail/legacy range: leave its original owner in charge.
    }

    for (std::int32_t index = 0; index < request.size; ++index) {
        MEM_B(index, rdram_address(request.destination)) =
            static_cast<std::uint8_t>(payload[index]);
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
    if (static_cast<std::uint8_t>(payload[0]) ==
        dkr::runtime::custom_tracks::kCustomTrackWorld) {
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
        const std::int32_t index = dkr::runtime::custom_tracks::sibling_index(
            Section::LevelHeaders, request.offset, fixup.section, fixup.slot);
        if (index < 0) {
            continue; // Track ships none, or that table is not built yet.
        }
        MEM_B(fixup.offset, rdram_address(request.destination)) =
            static_cast<std::uint8_t>((index >> 8) & 0xFF);
        MEM_B(fixup.offset + 1, rdram_address(request.destination)) =
            static_cast<std::uint8_t>(index & 0xFF);
        std::fprintf(stderr,
                     "[custom-tracks] header at offset %u now points at %s "
                     "%d\n",
                     request.offset, fixup.label, index);
    }
    return true;
}
} // namespace


// Common return convergence of get_track_id_to_load. All three retail paths -
// new game, settings->courseId, and the gTrackIdToLoad override that Track
// Select and Trophy Race drive - reach this instruction with the chosen level
// already in v0, so Track Lab replaces the answer without disturbing any of
// them or introducing a second way to pick a level.
extern "C" void dkr_custom_tracks_track_id_override(std::uint8_t*,
                                                     recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
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
extern "C" void dkr_custom_tracks_auto_boot(std::uint8_t* rdram,
                                              recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
#if defined(DKR_REPLAY_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (dkr_private_replay_boot(rdram, context)) return;
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
        recomp_context call = *context;
        call.r4 = argument;
        function(rdram, &call);
        return call.r2;
    };
    // Reproduce the nonvisual character/game-select setup skipped by auto boot.
    // Merely writing a player slot leaves Settings::racers zeroed (all Krunch).
    invoke(payload->titlescreen_controller_assign, 0);
    invoke(payload->input_assign_players);
    // The character-mod adapter shares charselect_assign_ai's native commit
    // boundary and expects every active player to have confirmed a character.
    for (int player = 0; player < 4; ++player) {
        MEM_B(player, rdram_address(addresses::CharacterSelectStatus)) = player == 0 ? 2 : 0;
    }
    MEM_W(0, rdram_address(addresses::NumberOfReadyPlayers)) = 1;
    MEM_W(0, rdram_address(addresses::TracksMode)) = 1;
    invoke(payload->set_time_trial_enabled, 0);

    // charselect_assign_ai needs the unlock-dependent table normally selected
    // by menu_character_select_init. Do not load that menu's graphics/music.
    // Addresses verified against ver/symbols/symbol_addrs.us.v{77,80}.txt.
    const bool rev_a = addresses::gSelectedRevision == dkr::runtime::rom::Revision::UsV80;
    const bool drumstick = invoke(payload->unlock_drumstick) != 0;
    const bool tt = invoke(payload->unlock_tt) != 0;
    const std::uint32_t tables77[] = {0x800DFDD0U, 0x800DFE40U, 0x800DFEC0U, 0x800DFF40U};
    const std::uint32_t tables80[] = {0x800E0350U, 0x800E03C0U, 0x800E0440U, 0x800E04C0U};
    const unsigned table = unsigned(drumstick) | (unsigned(tt) << 1);
    write_word(rdram, rev_a ? 0x8012696CU : 0x801263CCU,
               static_cast<std::int32_t>((rev_a ? tables80 : tables77)[table]));
    MEM_B(0, rdram_address(addresses::CharacterIdSlots)) = kCharacterDiddy;
    invoke(payload->charselect_assign_ai, 1);
    invoke(payload->init_racer_headers);
    std::fprintf(stderr, "[track-lab] single-player roster:");
    for (int racer = 0; racer < 8; ++racer) {
        std::fprintf(stderr, " %d", int(MEM_B(racer, rdram_address(addresses::CharacterIdSlots))));
    }
    std::fprintf(stderr, "\n");
    // mode_menu's direct-level result uses players minus one.
    write_word(rdram, rev_a ? 0x80123A80U : 0x80123500U, 0);

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
    constexpr std::uint32_t kSlotSize = 0x14U; // MemoryPoolSlot in memory.h
    constexpr std::int32_t kSlotIndexLimit = 0x7FFF; // slot links are s16
    // gMemoryPools, verified against ver/symbols/symbol_addrs.us.v{77,80}.txt.
    // Pool 0 is the main pool; MemoryPool is {maxNumSlots, curNumSlots, slots, size}.
    const std::uint32_t pool =
        dkr::runtime::revision_addresses::gSelectedRevision == dkr::runtime::rom::Revision::UsV80
            ? 0x80123B00U
            : 0x80123580U;
    const std::int32_t max_slots = read_word(rdram, pool);
    const std::int32_t used_slots = read_word(rdram, pool + 4U);
    const auto slots = static_cast<std::uint32_t>(read_word(rdram, pool + 8U));
    if (!addressable(slots) || max_slots <= 0 || max_slots > kSlotIndexLimit ||
        used_slots <= 0 || used_slots >= max_slots) {
        return 0;
    }
    const auto slot = [&](std::int32_t index) {
        return slots + static_cast<std::uint32_t>(index) * kSlotSize;
    };
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
    const std::int64_t wanted =
        (std::int64_t(arena) + kTrackHeapSlack + 15) & ~std::int64_t(15);
    if (wanted > kTrackHeapCeiling) {
        // Saying so is the whole point: past here the track corrupts memory the
        // same way it did before, and nothing else in the log would name it.
        std::fprintf(stderr,
                     "[custom-tracks] level %d needs %d bytes of level model and "
                     "collision data, over the %d this runtime can reserve; it "
                     "will overflow. Reduce collidable triangles or geometry\n",
                     static_cast<int>(level), static_cast<int>(arena),
                     static_cast<int>(kTrackHeapCeiling));
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
                 static_cast<int>(level), static_cast<int>(arena),
                 static_cast<int>(custom_tracks::kRetailTrackHeap));
}

static void grow_main_pool_for_level(std::uint8_t* rdram, std::int32_t level) {
    if (!dkr::runtime::custom_tracks::owns_level_id(level)) {
        return; // Retail and legacy levels never grow the pool.
    }
    const std::uint32_t extra = grow_main_pool(rdram);
    if (extra != 0) {
        std::fprintf(stderr,
                     "[custom-tracks] main memory pool grown into expansion RAM for level %d (+%u KB)\n",
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

// Called by the existing level_load scene-reset hook, before the level
// allocates anything. Every load passes here - races, Track Lab and restarts,
// and also the Track Select previews that load_level_game's hook never sees -
// so this is where a .dkrmap level gets its pool and its model heap.
extern "C" void dkr_custom_tracks_prepare_memory(std::uint8_t* rdram,
                                                 recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
    const auto level = static_cast<std::int32_t>(context->r4);
    grow_main_pool_for_level(rdram, level);
    prepare_track_heap(level);

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
extern "C" void dkr_custom_tracks_track_heap(std::uint8_t*,
                                             recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
    const std::int32_t wanted = g_track_heap_bytes;
    g_track_heap_bytes = 0;
    if (wanted <= dkr::runtime::custom_tracks::kRetailTrackHeap) {
        return;
    }
    if (static_cast<std::int32_t>(context->r21) !=
        dkr::runtime::custom_tracks::kRetailTrackHeap) {
        std::fprintf(stderr,
                     "[custom-tracks] generate_track does not hold the retail "
                     "track heap size where it was expected; leaving it alone\n");
        return;
    }
    context->r21 = static_cast<gpr>(wanted);
    std::fprintf(stderr,
                 "[custom-tracks] track heap raised from %d to %d bytes\n",
                 static_cast<int>(dkr::runtime::custom_tracks::kRetailTrackHeap),
                 static_cast<int>(wanted));
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
extern "C" void dkr_custom_tracks_prepare_level(std::uint8_t* rdram,
                                                recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
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
extern "C" void dkr_custom_tracks_prepare_vehicle(std::uint8_t* rdram,
                                                  recomp_context* context) {
    if (owned_vanilla_bootstrap()) return;
    namespace addresses = dkr::runtime::revision_addresses;
    const auto level = static_cast<std::int32_t>(context->r4);
    const auto players_minus_one = static_cast<std::int32_t>(context->r5);
    const auto armed = dkr::runtime::custom_tracks::track_override();
    if (armed < 0 || level != armed || players_minus_one < 0 || players_minus_one >= 4 ||
        read_word(rdram, addresses::GameMode) != 0) { // GAMEMODE_INGAME
        return;
    }
    const auto* payload = dkr::runtime::active_payload();
    if (!payload || !payload->leveltable_vehicle_default || !payload->leveltable_vehicle_usable ||
        !payload->set_level_default_vehicle) {
        return;
    }
    recomp_context call = *context;
    payload->leveltable_vehicle_default(rdram, &call);
    const auto vehicle = static_cast<std::int32_t>(call.r2);
    auto selected = vehicle;
    // The addon's debug override may name a boss/special vehicle. Preserve it
    // as the load argument, but player-selection arrays index three vehicle
    // records and must remain within the authored normal vehicles for the AI.
    if (selected < 0 || selected >= 3) {
        call = *context;
        payload->leveltable_vehicle_usable(rdram, &call);
        const auto allowed = static_cast<std::uint32_t>(call.r2);
        selected = (allowed & 1U) ? 0 : (allowed & 2U) ? 1 : (allowed & 4U) ? 2 : 0;
    }
    for (int player = 0; player <= players_minus_one; ++player) {
        MEM_B(player, rdram_address(addresses::PlayerSelectVehicle)) = selected;
    }
    call = *context;
    call.r4 = vehicle;
    payload->set_level_default_vehicle(rdram, &call);
    context->r7 = static_cast<gpr>(vehicle);
    std::fprintf(stderr, "[track-lab] level=%d vehicle=%d selected=%d players=%d\n",
                 level, vehicle, selected, players_minus_one + 1);
}
