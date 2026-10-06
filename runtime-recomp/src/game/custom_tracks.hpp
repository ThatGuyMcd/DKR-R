#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Project-owned custom track support.
//
// DKR reaches every level through one indirection that is entirely data
// driven, so added tracks need no instruction patching:
//
//   gTempAssetTable = asset_table_load(ASSET_LEVEL_HEADERS_TABLE);
//   for (i = 0; gTempAssetTable[i] != -1; i++) {}   // count, then i--
//   if (levelId >= i) { /* retail range check, derived from the table */ }
//   offset = gTempAssetTable[levelId];
//   size   = gTempAssetTable[levelId + 1] - offset; // size BY DIFFERENCE
//   asset_load(ASSET_LEVEL_HEADERS, dest, offset, size);
//
// Two consequences drive this module:
//
//   * Publishing a longer table before the -1 terminator grows the level
//     count, the retail range check and gGlobalLevelTable together. A header
//     whose `world` field exceeds the retail maximum also grows
//     gNumberOfWorlds, because level_global_init derives it as a running max.
//
//   * Because size comes from the difference between consecutive entries, a
//     custom offset cannot be an arbitrary token: overwriting the retail end
//     offset would corrupt the size of the LAST retail entry. Custom offsets
//     therefore begin exactly AT the retail end offset and advance by exact
//     entry sizes, so the payload behaves as though appended to the section:
//
//       retail : [o0, o1, ..., o(n-1), oEnd, -1]                 -> n entries
//       result : [o0, o1, ..., o(n-1), oEnd, oEnd+s0, ..., E, -1] -> n+K
//
//     Retail arithmetic is untouched (o(n-1)'s size is still oEnd - o(n-1)),
//     custom entry j sits at table index n+j, and `offset >= oEnd` is the
//     discriminator that routes a load to a mod payload instead of the ROM.
namespace dkr::runtime::custom_tracks {

// Sections a custom track can contribute to. Each has a parallel `_TABLE`
// section in DKR's asset LUT; the table is what this module rewrites.
//
// Textures3D is the one that is not a level aspect, and it earns its place by
// being reached through exactly the same arithmetic. `tex_init_textures` and
// `load_texture` in textures_sprites.c:
//
//   gTextureAssetTable[TEX_TABLE_3D] = asset_table_load(ASSET_TEXTURES_3D_TABLE);
//   for (i = 0; table[i] != -1; i++) {}   // count, then i--
//   if (assetIndex >= gTextureTableSize[..]) { /* range check from the table */ }
//   assetOffset = table[assetIndex];
//   assetSize   = table[assetIndex + 1] - assetOffset;   // size BY DIFFERENCE
//   asset_load(ASSET_TEXTURES_3D, dest, assetOffset, assetSize);
//
// So a longer table grows the texture count and the range check together, and
// an appended payload loads, with no new mechanism at all. What it adds is that
// a track can ship artwork rather than only pick from the ROM's 1401 images.
//
// Two things are different in kind from the four above, and both are here
// rather than in the .cpp because they constrain callers:
//
//   * A track contributes MANY texture entries, not one, and their order in its
//     manifest is their identity. A level model refers to them by that order
//     through kCustomTextureIdBase below, and build_extended_table substitutes
//     the real indices into the model payload as it assembles it.
//   * The texture table is loaded ONCE, at boot, from thread3_main. The level
//     tables are rebuilt per load, so a rescan can renumber them; this one
//     cannot be renumbered after the fact, so a track discovered later has no
//     textures in the published table at all. Its model's ids are reset to
//     texture 0 rather than left pointing past the end of the table.
//
// Textures2D and Sprites are a track's own MINIMAP, and they arrive the same
// way. tex_init_textures loads ASSET_TEXTURES_2D_TABLE, then the 3D table, then
// ASSET_SPRITES_TABLE, each counted to its terminator exactly like the level
// tables, and load_texture / tex_load_sprite / load_sprite_info read an entry
// by table difference. So a picture appended to the 2D table and a sprite
// appended to the sprite table load with no new mechanism, published once at
// boot like Textures3D. Two ids are placeholders the runtime substitutes, in
// the stored prefix of the model (minimapSpriteIndex, 0x20) and in the sprite
// itself (baseTextureId); see kCustomSpriteIdBase below.
enum class Section {
    LevelHeaders,
    LevelObjectMaps,
    LevelNames,
    LevelModels,
    Textures3D,
    Textures2D,
    Sprites,
};

// A level owns two object maps, and init_track spawns from both:
//
//   init_track(geometry, skybox, players, vehicle, entrance,
//              header->collectables,   // 0x36 - coins, balloons, items
//              header->unkBA);         // 0xBA - checkpoints, spawns, cameras
//
// They are the same asset section, so a track that replaces objects has to say
// which of the two a payload is. Merging them loses that split: structural
// objects would spawn with the collectable flag while the retail structure map
// kept spawning alongside.
enum class MapSlot {
    None,          // not an object map
    Structure,     // header 0xBA
    Collectables,  // header 0x36
};

struct Entry {
    Section section = Section::LevelHeaders;
    MapSlot slot = MapSlot::None;
    // Position among this section's added entries, in manifest order. The
    // resulting level id is the retail count plus this ordinal, resolved at
    // table-build time and reported back through resolved_level_id().
    std::uint32_t ordinal = 0;
    std::vector<std::uint8_t> bytes;
};

// ---------------------------------------------------------------------------
// A track's own texture, as its header describes it
// ---------------------------------------------------------------------------
//
// A TEXTURES_3D payload is what dkr_assets_tool's BuildTexture writes: a
// 32-byte TextureHeader and its texels for every frame, the frames laid end to
// end and walked by each header's own textureSize (load_texture). material_init
// reads three things out of each header that decide how the game draws it:
//
//   format & 0x0F       the texel format;
//   format >> 4         the render mode. TRANSPARENT (0) and TRANSPARENT_2 (2)
//                       give RGBA32, RGBA16 and CI4 RENDER_SEMI_TRANSPARENT;
//                       IA16, IA8 and IA4 get it whatever the nibble says;
//   numOfTextures >> 8  how many frames the animation has.
//
// A semi-transparent texture is drawn only in render_level_segment's second
// pass, so a level model has to put the batches drawing it past
// numberofOpaqueBatches or they are never drawn at all. The Blender exporter
// does that; this reads the header so the runtime can say what a track ships
// and refuse a payload the loader would misread.
struct TextureInfo {
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t format = 0;        // TextureHeader.format & 0x0F
    std::uint8_t render_mode = 0;   // TextureHeader.format >> 4
    std::uint16_t frames = 0;       // TextureHeader.numOfTextures >> 8
    bool compressed = false;        // TextureHeader.isCompressed
    bool translucent = false;       // what material_init makes of it
};

// material_init's rule for RENDER_SEMI_TRANSPARENT, format by format.
[[nodiscard]] constexpr bool texture_translucent(std::uint8_t format,
                                                 std::uint8_t render_mode) noexcept {
    switch (format) {
    case 0U:   // RGBA32
    case 1U:   // RGBA16
    case 7U:   // CI4
        return render_mode == 0U || render_mode == 2U;
    case 4U:   // IA16
    case 5U:   // IA8
    case 6U:   // IA4
        return true;
    default:   // I8, I4, CI8
        return false;
    }
}

// ---------------------------------------------------------------------------
// A track's own minimap
// ---------------------------------------------------------------------------
//
// A SPRITES payload is what dkr_assets_tool's BuildSprite writes: a 12-byte
// SpriteHeader - s16 baseTextureId, s16 numberOfFrames, s16 anchor x, s16
// anchor y, s32 the game fills - then numberOfFrames + 1 bytes of frame
// boundaries, padded to 16. tex_load_sprite loads frameTexOffsets[frames]
// textures from ASSET_TEXTURES_2D starting at baseTextureId, and
// minimap_marker_pos places every racer's dot relative to the anchor.
struct SpriteInfo {
    std::int16_t base_texture = 0;   // baseTextureId as the payload holds it
    std::uint16_t frames = 0;
    std::int16_t anchor_x = 0;
    std::int16_t anchor_y = 0;
    std::uint16_t textures = 0;      // frameTexOffsets[frames]
};

// gCurrentSpriteAsset is a 512-byte buffer (MAX_SPRITE_ASSET_SIZE), and
// asset_load copies a whole sprite into it before anything is checked.
inline constexpr std::size_t kMaxSpritePayload = 512;

// The placeholder a model's minimapSpriteIndex (LevelModel 0x20) holds for the
// track's own minimap sprite, and the one that sprite's baseTextureId holds for
// the track's own 2D texture. The real indices are the ROM's counts plus an
// ordinal - 193 sprites and 906 2D textures in US v1.0 - which only the
// player's cartridge knows, so the exporter writes these and the runtime
// substitutes them, exactly as it does kCustomTextureIdBase. 0x7000 fits the
// s16 both ids travel in. Kept identical in
// tools/blender/track_lab/minimap.py.
inline constexpr std::int32_t kCustomSpriteIdBase = 0x7000;
inline constexpr std::int32_t kCustomSpriteIdCount = 16;
inline constexpr std::int32_t kCustomTexture2DIdBase = 0x7000;
inline constexpr std::int32_t kCustomTexture2DIdCount = 255;

// Where minimapSpriteIndex sits in a LevelModel (include/structs.h).
inline constexpr std::size_t kModelMinimapSprite = 0x20;
// Level header byte 0xBC, bit 0: hud_render_general draws no minimap at all.
// Hubs and cutscenes set it.
inline constexpr std::size_t kHeaderMinimapFlags = 0xBC;
inline constexpr std::uint8_t kHeaderNoMinimap = 0x01;

// Reads a SPRITES payload into `info`. False, with the reason in `error`, for
// one the loader would misread: shorter than its own header and frame table,
// longer than the buffer asset_load copies it into, no frames, or frame
// boundaries that do not start at 0 and climb.
[[nodiscard]] bool inspect_sprite_payload(const std::vector<std::uint8_t>& bytes,
                                          SpriteInfo& info, std::string& error);

// The minimapSpriteIndex a LEVEL_MODELS payload carries in its stored prefix,
// or -1 when the payload has no readable stored prefix (an older exporter's,
// which predates own minimaps and so names a retail sprite anyway).
[[nodiscard]] std::int64_t model_minimap_sprite(const std::vector<std::uint8_t>& bytes);

// Whether the header served at `header_offset` must hide the minimap: its
// track ships a minimap of its own that the once-per-boot sprite or 2D texture
// table does not hold - installed after boot - so the model's placeholder could
// not be resolved and the HUD would draw a stray sprite with the dots scattered
// round it. Nothing is hidden for a track without one, or for a retail header.
[[nodiscard]] bool minimap_hidden(std::uint32_t header_offset);

// A track's own recorded music: manifest schema 2, key "music". DKR cannot
// play it - its music player runs sequences against the ROM's instrument bank
// from a 13 KB buffer - so custom_music decodes the file on the host and mixes
// it into the audio output while the retail song the header names, the
// carrier, plays silently. Following the carrier is how the game's own fades,
// pause and final-lap speed-up reach the file. See docs/CUSTOM_MUSIC_PLAN.md.
//
// Everything here was checked at scan: the file sits inside the track
// directory (symlinks resolved), has the size the manifest records and starts
// like the codec it claims, and the carrier is the header's own /music byte.
// The bytes are only decoded, and their digest checked, when a race needs them.
inline constexpr const char* kMusicFormat = "audio-stream-v1";
inline constexpr std::uint64_t kMaxMusicBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t kMaxMusicVolume = 200;   // percent
inline constexpr std::size_t kHeaderMusic = 0x52;        // level header /music
inline constexpr std::size_t kHeaderInstruments = 0x54;  // level header /instruments (u16)

// The other kind of music a track can carry: a native DKR sequence (compact
// ALCSeq, `music/main.cseq`), usually converted from a MIDI file by the addon.
// The game plays it itself, through its own instrument bank: custom_music
// copies it into the music buffer when the carrier starts, so the player, the
// fades, the final-lap speed-up and the MidiFade/MidiChSet objects all work on
// it unchanged. Validated at scan the way the player will read it.
inline constexpr const char* kSequenceFormat = "dkr-alcseq-v1";
inline constexpr const char* kSequenceBank = "dkr-stock-v1";
inline constexpr std::uint32_t kMaxSequenceBytes = 64U * 1024U;
inline constexpr std::uint32_t kMaxSequenceVolume = 127;  // gMusicBaseVolume

enum class MusicKind : std::uint8_t { Recording, Sequence };
enum class MusicCodec : std::uint8_t { Mp3, Wav };

struct MusicInfo {
    MusicKind kind = MusicKind::Recording;
    MusicCodec codec = MusicCodec::Mp3;
    std::filesystem::path file;          // absolute, inside the track directory
    std::string sha256;                  // lowercase hex, as the manifest says
    std::uint64_t bytes = 0;
    std::uint32_t sample_rate = 0;       // what the addon measured
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;            // decoded sample frames
    std::uint8_t carrier = 0;            // header /music, never 0
    std::uint32_t volume = 100;          // percent of the file's own level
    std::uint64_t loop_start = 0;        // sample frames
    std::uint64_t loop_end = 0;          // sample frames; 0 is the end of file
    bool final_lap_speedup = true;       // follow the carrier's tempo changes

    // MusicKind::Sequence only. The bytes are read at scan (they are small),
    // so a race never touches the disk; `volume` is then DKR's own base
    // volume (0-127), not a percentage.
    std::shared_ptr<const std::vector<std::uint8_t>> sequence;
    std::uint8_t tempo_bpm = 0;          // the song's tempo, as gSeqSoundTable holds it
    std::uint8_t reverb = 1;             // alFxReverbSet's setting
    std::uint16_t channel_mask = 0xFFFF; // header /instruments
};

struct Track {
    std::string id;
    std::string name;
    std::string author;
    std::filesystem::path source;
    std::vector<Entry> entries;
    // One per TEXTURES_3D entry, in manifest order - the order that is each
    // texture's identity.
    std::vector<TextureInfo> textures;
    // One per TEXTURES_2D and SPRITES entry, in manifest order.
    std::vector<TextureInfo> textures_2d;
    std::vector<SpriteInfo> sprites;
    bool enabled = true;
    bool kept_online = false; // Local offline copy; activation is a local sidecar, never part of its manifest.
    // manifest.hdTexturePack, and the sibling archive resolved at scan time.
    // See "A track's high-resolution texture pack" below.
    std::string hd_pack_file;                 // hdTexturePack.file, "" when none
    std::string hd_pack_digest;               // hdTexturePack.textureDigest
    std::filesystem::path hd_pack_sibling;    // matched <track>-hd.zip, else empty
    bool hd_pack_sibling_mismatch = false;    // sibling present, digest differs
    std::optional<MusicInfo> music;           // manifest.music (schema 2)
};

// Scans `directory` for *.dkrmap archives and parses their manifests. Invalid
// archives are reported and skipped; one bad mod never prevents startup.
void scan(const std::filesystem::path& directory);

// Where scan() last looked. The importer copies into it, so the install
// location is decided in one place rather than repeated in the UI.
[[nodiscard]] std::filesystem::path directory();

// An second directory, chosen by the author, scanned in place alongside the
// install directory. Copying is right for a track you want to keep; it is
// wrong while authoring, where the folder the exporter writes to should simply
// be the folder the game reads. Point this at that folder and a re-export is
// picked up by a rescan with no copy step at all.
//
// Persisted beside the settings so it survives a restart. Empty disables it.
void set_working_directory(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path working_directory();

// What install() found beside the track, for the caller to finish wiring up.
// The HD-pack import itself runs in the UI layer, which can see texture_packs;
// this module deliberately cannot (its unit test builds it on its own).
struct InstallOutcome {
    std::string track_id;
    // A <track>-hd.zip the track declares that was found with a matching
    // digest, ready for texture_packs::import_archive. Empty when the track
    // declares no pack, or the sibling was absent or from another export.
    std::filesystem::path hd_pack_archive;
    std::string hd_pack_digest;           // manifest digest, for import idempotence
    bool hd_pack_mismatch = false;        // sibling present but from another export
    // Set when hd_pack_archive points inside a temporary unpacking of a wrapper
    // zip. The caller imports the pack, then calls discard_install_temp(this).
    std::filesystem::path temp_root;
};

// Installs a track into the install location and rescans. `source` may be the
// *.dkrmap directory, a *.zip of that directory, or a *.zip that also wraps the
// <track>-hd.zip beside it. Returns false with a reason in `error` when the
// source is not a track. `outcome`, when given, reports the HD pack sibling.
bool install(const std::filesystem::path& source, std::string& error,
             InstallOutcome* outcome = nullptr);

// Only managed copies directly inside directory() can be uninstalled. Tracks
// read from the author's working folder are never deleted by the launcher.
[[nodiscard]] bool is_installed(const Track& track);
// Caller must be in the launcher, with no game or import running. Removes the
// managed track, clears its Track Lab selection and preserves HD packs/saves.
bool uninstall(const std::string& id, std::string& error);

// Removes a temporary directory reported in InstallOutcome::temp_root. A no-op
// for an empty path.
void discard_install_temp(const std::filesystem::path& temp_root);

// Returns a snapshot. The UI thread reads this while an authoring reload can
// be replacing the backing vector, so a reference would dangle.
[[nodiscard]] std::vector<Track> tracks();
// Independent package validation for the isolated online import worker.
// Does not change the installed library, activation, auto-boot or working path.
bool inspect_package(const std::filesystem::path& directory,Track& track,std::string& error);
[[nodiscard]] std::size_t enabled_count();
void set_enabled(const std::string& id, bool enabled);
bool set_kept_enabled(const std::string& id, bool enabled, std::string& error);

// Re-reads every archive from disk. Paired with the retail restart path this
// is the authoring hot-reload: save in the editor, restart the track, race the
// new geometry without leaving the process.
void reload();

// Level id assigned to a track's header after the last table build, or -1
// when the track is disabled or contributes no header.
[[nodiscard]] std::int32_t resolved_level_id(const std::string& track_id);
[[nodiscard]] bool owns_level_id(std::int32_t level_id);
// Called with the level id at every level load - races, restarts and the Track
// Select previews alike - from the level_load scene-reset hook. custom_music
// registers here so this module, and the tests that build it alone, never
// depend on the audio code.
using LevelLoadObserver = void (*)(std::int32_t level_id);
void set_level_load_observer(LevelLoadObserver observer);
// The recorded music of the enabled track that owns `level_id`, if it has any.
// Asked at every level load, so it copies only the descriptor.
[[nodiscard]] std::optional<MusicInfo> music_for_level(std::int32_t level_id);

// WORLD_CUSTOM_TRACKS in the Blender addon. Track Select appends these races
// to the same logical category as legacy courses, outside retail world arrays.
inline constexpr std::uint8_t kCustomTrackWorld = 6;
struct TrackSelectEntry {
    std::string id, name;
    std::int32_t level_id;
    std::uint8_t vehicles;
};

// Pure, launch-time Track Lab namespace. All tables and payloads are built
// against the session's character-augmented bank, not the offline catalogue.
// Once prepared, replay reads immutable records: no rescans, file I/O,
// sticky boot overrides or mutable process-global table state are involved.
struct PreparedTracks {
    std::array<std::vector<std::vector<std::uint8_t>>,7> additions;
    std::vector<TrackSelectEntry> courses;
    std::vector<Track> tracks;
    std::vector<std::int32_t> model_arenas,model_batches;
};
[[nodiscard]] PreparedTracks prepare_tracks(std::vector<Track> tracks,
    const std::array<std::vector<std::int32_t>,7>& base_tables);
[[nodiscard]] std::vector<TrackSelectEntry> track_select_entries();

// Triangle batches in the level model the track at `level_id` ships, summed
// over its segments. render_level_segment spends several display-list
// commands on each one, so this sizes the game's display-list heap before the
// level loads. -1 when the level is not a .dkrmap track, ships no model of its
// own, or the model cannot be read.
[[nodiscard]] std::int32_t level_model_batches(std::int32_t level_id);
// The same count for one compressed LEVEL_MODELS payload.
[[nodiscard]] std::int32_t count_level_model_batches(const std::uint8_t* bytes,
                                                     std::size_t size);

// The retail size of gTrackModelHeap: LEVEL_MODEL_MAX_SIZE in tracks.c. It
// covers the inflated blob AND the scratch arena the loader appends past
// modelSize, so a model well under it can still overflow on collision alone.
inline constexpr std::int32_t kRetailTrackHeap = 0x82A00;

// Bytes generate_track will have constructed by the time track_init_collision
// finishes: the inflated blob plus, per segment, two bytes a triangle, sixteen
// per collision plane and two per special batch, each 16-aligned. Retail only
// reports an overflow through a stubbed rmonPrintf and then writes past the
// heap, so this is measured before the load and the heap sized to match.
//
// The plane bookkeeping mirrors track_init_collision (tracks.c:3064). The same
// walk lives in mods/legacy_mod_geometry.cpp, which *rejects* what overflows
// rather than measuring it; keep the two in step. -1 when the level is not a
// .dkrmap track, ships no model of its own, or the payload cannot be read.
[[nodiscard]] std::int32_t level_model_arena_bytes(std::int32_t level_id);
// The same measurement for one compressed LEVEL_MODELS payload.
[[nodiscard]] std::int32_t measure_level_model_arena(const std::uint8_t* bytes,
                                                      std::size_t size);

// ---------------------------------------------------------------------------
// A track's high-resolution texture pack
// ---------------------------------------------------------------------------
//
// The Blender export writes each of a track's own pictures twice: the 64x32
// the console can load, inside the .dkrmap, and the author's full-resolution
// original in a Rice pack named <track>-hd.zip, written BESIDE the .dkrmap
// (HD_TEXTURE_PLAN.md keeps the two files separate so a track can be shipped
// without the pack). manifest.json names the pack and carries a digest of the
// payloads it was built from; the pack's own dkr-r-track.json carries the same
// digest. Equal digests are what prove a pack belongs to this export of this
// track. Nothing here imports or enables the pack - that is the UI layer's
// job, next to texture_packs - this only reports what is on disk.
struct HdPack {
    std::string file;                        // hdTexturePack.file ("" when none)
    std::string digest;                      // hdTexturePack.textureDigest
    std::filesystem::path sibling_archive;   // <track>-hd.zip beside the source,
                                             // digest matched; else empty
    bool sibling_mismatch = false;           // sibling present but another export
};

// The HD pack the given track declares, resolved against the folder it was
// scanned from. A track scanned from the install copy has no sibling there
// (install() handed its pack to the importer separately); a track watched in a
// working folder has the sibling the exporter just wrote.
[[nodiscard]] HdPack hd_pack(const std::string& track_id);

// True once the once-per-boot 3D texture table has been published with this
// track's own texture entries in it. False means the track joined after that
// table was built, so nothing it draws - the HD pack or the 64x32 in the
// .dkrmap - can load until DKR-R is relaunched. Always false for a track that
// ships no artwork of its own.
[[nodiscard]] bool track_textures_published(const std::string& track_id);

// Track Lab: force every level load to resolve to one level id, so an author
// can reach a track without walking the retail menus for it.
//
// DKR already owns this path. get_track_id_to_load() returns gTrackIdToLoad
// whenever tracks mode or gTrackSpecifiedWithTrackIdToLoad is set, and the
// retail Track Select and Trophy Race both drive it. The override reuses that
// return value instead of introducing a second way to choose a level.
//
// The override is deliberately sticky rather than one-shot: paired with the
// retail L+Z restart it gives an authoring loop where the same track reloads
// on every restart. Pass kNoTrackOverride to return to retail selection.
// The override is armed by track identity, not by level id, because the id a
// track receives is only known once the extended table has been built - which
// happens on the first level load, long after the launcher has drawn its list.
// Resolution is therefore deferred to the moment the game asks.
//
// The armed track persists beside the settings (custom-tracks-state.txt), so
// the full relaunch that loads a just-installed track's artwork lands back on
// it rather than the retail menus. An empty string clears it and the file.
inline constexpr std::int32_t kNoTrackOverride = -1;

void arm_track_override(std::string track_id);   // empty string disarms
[[nodiscard]] std::string armed_track_id();
[[nodiscard]] std::int32_t track_override();

// Auto boot: skip the logos, title, file select and character select and drop
// straight into the armed track.
//
// This does not fabricate a load. mode_menu already starts a level whenever
// menu_loop returns MENU_RESULT_FLAGS_200 with a map id in its low bits, so
// auto boot returns exactly that result and lets retail run its own sequence:
// vehicle default, entrance, cutscene, game mode and load_level_game.
// Before returning that result, the host prepares the single-player inputs,
// Diddy's slot, the native unlock-dependent AI roster and Settings::racers.
// The level_load lifecycle hook synchronizes the armed track's authored
// default vehicle with player selections, which the native AI also reads.
//
// It fires once per launch. Restarting in place with L+Z keeps reloading the
// same track, while quitting still returns to the menus rather than trapping
// the player in a loop they cannot leave.
//
// The setting persists beside the settings (custom-tracks-state.txt): it stays
// on across relaunches until the player turns it off, so a track installed
// once boots straight in on every launch after that. consume_auto_boot() is
// the per-launch one-shot; the persisted flag is untouched by it.
void set_auto_boot(bool enabled);
[[nodiscard]] bool auto_boot_enabled();

// Returns true exactly once per launch while auto boot is enabled.
[[nodiscard]] bool consume_auto_boot();

// Builds the replacement table for `section` from the retail table (terminated
// by -1). Returns an empty vector when nothing is added, meaning the caller
// must publish the retail table unchanged.
[[nodiscard]] std::vector<std::int32_t> build_extended_table(
    Section section, const std::int32_t* retail_table);

// Resolves an offset produced by build_extended_table. Returns nullptr when
// the offset belongs to the ROM or does not cover `size` bytes, in which case
// the caller must fall back to the retail loader.
[[nodiscard]] const std::uint8_t* payload_for(
    Section section, std::uint32_t offset, std::int32_t size);

// A level header names its object map by index, and that index only exists once
// the extended object-map table has been built. Given a custom entry in `from`
// at `offset`, this returns the index the same track's entry received in `to`,
// so a header can be fixed up as it is served instead of asking the author to
// write a number they cannot know.
//
// Returns -1 when the offset is not custom, when the track contributes nothing
// to `to`, or when `to`'s table has not been built yet.
// `slot` selects between a section's two payloads where one exists; pass
// MapSlot::None for sections that have only one.
[[nodiscard]] std::int32_t sibling_index(Section from, std::uint32_t offset,
                                          Section to, MapSlot slot);

// The id range the exporter writes into a level model's texture table for the
// track's own artwork, standing in for indices only the runtime can assign.
//
// Any value works that no retail id reaches and that survives load_texture's
// `id & 0x7FFF` after tracks.c ors in 0x8000. 0x7000 is an order of magnitude
// above the largest retail table (1401 in US v1.0) and half the 15-bit space
// below the mask. The count is the ceiling on a model's texture table, which is
// indexed by a u8 with 0xFF meaning "none".
//
// Kept identical in tools/blender/track_lab/textures.py.
inline constexpr std::int32_t kCustomTextureIdBase = 0x7000;
inline constexpr std::int32_t kCustomTextureIdCount = 255;

// The smallest texture payload that can be served safely. load_texture reads
// sizeof(TempTexHeader) bytes before it knows how large the texture is, so a
// shorter payload would have the loader fall off the end of it and read the
// ROM's own bytes as a TextureHeader.
inline constexpr std::size_t kMinimumTexturePayload = 40;

// Reads a TEXTURES_3D payload's headers into `info`. Returns false, with the
// reason in `error`, for a payload the game would misread: shorter than
// load_texture's peek, not a multiple of 16, a colour-indexed format (its
// palette would have to come from ASSET_EMPTY_14, which a track cannot add), a
// render mode material_init does not know, no frames, or a frame whose header
// or texels run past the payload. A compressed payload's frames are packed and
// are not walked; its first header, which the loader reads raw, still is.
[[nodiscard]] bool inspect_texture_payload(const std::vector<std::uint8_t>& bytes,
                                           TextureInfo& info,
                                           std::string& error);

// What an installed track's own artwork is, for Track Lab.
struct ArtworkSummary {
    std::size_t textures = 0;
    std::size_t translucent = 0;
    std::size_t animated = 0;
    bool minimap = false;   // ships a minimap of its own
};
[[nodiscard]] ArtworkSummary artwork(const std::string& track_id);

} // namespace dkr::runtime::custom_tracks
