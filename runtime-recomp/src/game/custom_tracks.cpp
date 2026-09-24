#include "custom_tracks.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>

#include <json/json.hpp>
#include <miniz/miniz.h>
#include <unordered_map>

namespace {

using dkr::runtime::custom_tracks::Entry;
using dkr::runtime::custom_tracks::MapSlot;
using dkr::runtime::custom_tracks::Section;
using dkr::runtime::custom_tracks::Track;

constexpr std::size_t kSectionCount = 7U;

std::size_t section_slot(Section section) {
    return static_cast<std::size_t>(section);
}

// Per-section state captured during the most recent table build. `end_offset`
// is the retail section's end offset, which doubles as the base of the custom
// range and therefore as the ROM/mod discriminator.
// One added entry's span and the index it received, kept so a header can be
// pointed at the object map its own track supplied.
struct AddedEntry {
    std::string track_id;
    dkr::runtime::custom_tracks::MapSlot slot =
        dkr::runtime::custom_tracks::MapSlot::None;
    std::uint32_t offset = 0;   // absolute section offset
    std::uint32_t size = 0;
    std::uint32_t index = 0;    // index assigned within this section
    // Position among the entries THIS track contributed to THIS section, which
    // is manifest order. A track adds one header and one model, so for those it
    // is always zero; for textures it is the whole of how one is told from
    // another. See resolve_model_textures().
    std::uint32_t within_track = 0;
};

struct SectionState {
    bool built = false;
    std::uint32_t end_offset = 0;
    std::vector<std::uint8_t> blob;      // enabled payloads, concatenated
    std::uint32_t retail_count = 0;
    std::vector<AddedEntry> added;
};

// Track Lab's armed track, by manifest id. Guarded by g_mutex because
// resolving it reads the same table that build_extended_table writes; the
// lookup happens once per level load, so the lock is never contended.
// g_armed_track and g_auto_boot persist to custom-tracks-state.txt so the full
// relaunch that publishes a just-installed track's textures lands back on the
// track. g_auto_boot_consumed is the per-launch one-shot and is never written.
std::string g_armed_track;
bool g_auto_boot = false;
bool g_auto_boot_consumed = false;

std::mutex g_mutex;
std::vector<Track> g_tracks;
std::array<SectionState, kSectionCount> g_sections{};
std::unordered_map<std::string, std::int32_t> g_resolved_level_ids;
std::filesystem::path g_directory;
std::filesystem::path g_working_directory;

// Rescanning replaces the track list, and the section blobs a live level is
// reading are built from it. A rescan requested while a level is loaded is
// therefore deferred to the next table build, which is a level-load boundary.
bool g_rescan_pending = false;

// Entries of one section, in a stable order: track order as scanned, then
// manifest order within a track. The resulting level ids must not shift
// between builds within a session, or a mid-session reload would move a track
// out from under a level id the game is already holding.
std::vector<const Entry*> enabled_entries(Section section) {
    std::vector<const Entry*> result;
    for (const Track& track : g_tracks) {
        if (!track.enabled) {
            continue;
        }
        for (const Entry& entry : track.entries) {
            if (entry.section == section) {
                result.push_back(&entry);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// A model naming its own track's textures
// ---------------------------------------------------------------------------
//
// A track that ships artwork cannot know the ids it will get: a custom
// texture's index is the ROM's retail texture count plus an ordinal, and the
// count belongs to the player's cartridge: 1401 in US v1.0 and 1416 in Rev A,
// both counted from the extractions themselves. So the exporter writes
// kCustomTextureIdBase + ordinal and the real index is substituted here, the
// same way a header's model and object-map fields are.
//
// **Where the ids are, and why they are reachable at all.** A level model
// arrives compressed: track_init_level_model does asset_load and then
// gzip_inflate, so the payload is a five-byte container followed by a raw
// DEFLATE stream. A four-byte field inside a Huffman-coded block has no byte
// offset to patch. The exporter's answer is DEFLATE's own - block type 00 is
// *stored*, byte-aligned and verbatim, and gzip_inflate_block dispatches to
// gzip_inflate_stored for it exactly as it does to the Huffman decoders for the
// other two - so it writes the model's header and texture table as one stored
// block and compresses the rest:
//
//   0..4   container: uncompressed size (LE u32), then the tag 0x09
//   5      the stored block's BFINAL/BTYPE byte, whose remaining five bits the
//          reader discards, which is what "stored is byte-aligned" means
//   6..7   LEN, little endian     8..9   NLEN, LEN's complement
//   10..   the model's own first LEN bytes, uncompressed
//
// Kept in step with level_model_encoder.STORED_PREFIX_AT in the addon.
//
// This runs while the LEVEL_MODELS blob is being assembled rather than as the
// bytes are served, because here they are a plain byte vector: no RDRAM, no
// endian macros, and a unit test can read the result back.

//: LevelModel, from the matching decomp's include/structs.h:
//    /* 0x00 */ TextureInfo *textures;   // a file-relative offset on disk
//    /* 0x18 */ s16 numberOfTextures;
//  and each TextureInfo is { be_int32 id, u8 width, u8 height, u8 format,
//  u8 surfaceType }. tracks.c resolves the id with load_texture(id | 0x8000).
constexpr std::size_t kLevelModelTexturesPointer = 0x00;
constexpr std::size_t kLevelModelTextureCount = 0x18;
constexpr std::size_t kTextureInfoSize = 8U;
constexpr std::size_t kStoredPrefixAt = 10U;
constexpr std::uint8_t kContainerTag = 0x09;

// A model naming more textures than this is not a model: the batch that selects
// an entry does so with a u8 where 0xFF means "none".
constexpr std::int32_t kMaxModelTextures = 255;

std::uint32_t read_be32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

void write_be32(std::uint8_t* bytes, std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes[byte] =
            static_cast<std::uint8_t>((value >> (24U - byte * 8U)) & 0xFFU);
    }
}

// Declared here, defined after the section state it reads.
std::int32_t own_entry_index(Section section, const std::string& track_id,
                             std::uint32_t ordinal);

std::int32_t own_texture_index(const std::string& track_id,
                               std::uint32_t ordinal) {
    return own_entry_index(Section::Textures3D, track_id, ordinal);
}

// The stored prefix's length when `bytes` is a LEVEL_MODELS payload whose
// front the exporter left uncompressed, else 0. See resolve_model_textures.
std::uint32_t stored_prefix(const std::uint8_t* bytes, std::size_t size) {
    if (size < kStoredPrefixAt || bytes[4] != kContainerTag) {
        return 0;
    }
    const std::uint32_t length =
        static_cast<std::uint32_t>(bytes[6]) |
        (static_cast<std::uint32_t>(bytes[7]) << 8);
    const std::uint32_t complement =
        static_cast<std::uint32_t>(bytes[8]) |
        (static_cast<std::uint32_t>(bytes[9]) << 8);
    if ((bytes[5] & 0x07U) != 0x00U || complement != ((~length) & 0xFFFFU) ||
        kStoredPrefixAt + length > size) {
        return 0;
    }
    return length;
}

// Substitute the real texture indices into one model payload, in place.
//
// An id that cannot be resolved becomes 0 rather than being left alone. Leaving
// it would send load_texture past the end of the table it just published with an
// index it range-checks and then uses anyway - textures_sprites.c sets `id = 0`
// on a failed check but still indexes with the unclamped value. Texture 0 is
// wrong and visible, which is the better of the two.
void resolve_model_textures(std::uint8_t* bytes, std::size_t size,
                           const std::string& track_id) {
    if (size < kStoredPrefixAt + kLevelModelTextureCount + 2U ||
        bytes[4] != kContainerTag) {
        return; // Not a container this can read.
    }

    // The stored block has to be there and has to be honest, or the offsets
    // below mean nothing. A package from an older exporter compresses from the
    // first byte; that is a "leave it alone", because such a package predates
    // custom textures and has none to resolve.
    const std::uint32_t length =
        static_cast<std::uint32_t>(bytes[6]) |
        (static_cast<std::uint32_t>(bytes[7]) << 8);
    const std::uint32_t complement =
        static_cast<std::uint32_t>(bytes[8]) |
        (static_cast<std::uint32_t>(bytes[9]) << 8);
    if ((bytes[5] & 0x07U) != 0x00U || complement != ((~length) & 0xFFFFU)) {
        return;
    }

    const std::uint8_t* model = bytes + kStoredPrefixAt;
    const std::uint32_t table = read_be32(model + kLevelModelTexturesPointer);
    const auto count = static_cast<std::int16_t>(
        (static_cast<std::uint16_t>(model[kLevelModelTextureCount]) << 8) |
        model[kLevelModelTextureCount + 1U]);
    if (count <= 0 || count > kMaxModelTextures) {
        return;
    }

    // The table has to lie inside the stored prefix. Past it the bytes are
    // Huffman-coded, and a write there would corrupt the stream rather than
    // change an id.
    const std::uint64_t span = static_cast<std::uint64_t>(table) +
                               static_cast<std::uint64_t>(count) *
                                   kTextureInfoSize;
    if (span > length || kStoredPrefixAt + span > size) {
        std::fprintf(stderr,
                     "[custom-tracks] %s puts its %d model textures at +%u, "
                     "past the %u bytes it left uncompressed; leaving them\n",
                     track_id.c_str(), count, table, length);
        return;
    }

    int resolved = 0;
    int lost = 0;
    for (std::int16_t entry = 0; entry < count; ++entry) {
        std::uint8_t* at = bytes + kStoredPrefixAt + table +
                           static_cast<std::size_t>(entry) * kTextureInfoSize;
        const auto identifier = static_cast<std::int32_t>(read_be32(at));
        if (identifier < dkr::runtime::custom_tracks::kCustomTextureIdBase ||
            identifier >= dkr::runtime::custom_tracks::kCustomTextureIdBase +
                              dkr::runtime::custom_tracks::kCustomTextureIdCount) {
            continue; // A retail texture; the author meant exactly that one.
        }
        const auto ordinal = static_cast<std::uint32_t>(
            identifier - dkr::runtime::custom_tracks::kCustomTextureIdBase);
        const std::int32_t index = own_texture_index(track_id, ordinal);
        if (index < 0) {
            write_be32(at, 0U);
            ++lost;
            continue;
        }
        write_be32(at, static_cast<std::uint32_t>(index));
        ++resolved;
    }

    if (lost != 0) {
        std::fprintf(stderr,
                     "[custom-tracks] %s: %d of its own model textures "
                     "resolved, and %d could not be - those are drawn with "
                     "texture 0\n",
                     track_id.c_str(), resolved, lost);
    } else if (resolved != 0) {
        std::fprintf(stderr,
                     "[custom-tracks] %s: %d of its own textures resolved into "
                     "its model\n",
                     track_id.c_str(), resolved);
    }
}

// Point a model at the minimap sprite its own track shipped, in place.
//
// minimapSpriteIndex is in the model's header, inside the same stored prefix
// as the texture table. A placeholder that cannot be resolved - the track was
// installed after the once-per-boot sprite table was published - becomes
// sprite 0 rather than an index load_sprite_info refuses, and the header that
// names this model is served with its "no minimap" bit set (minimap_hidden),
// so the HUD draws no minimap rather than a stray one.
void resolve_model_minimap(std::uint8_t* bytes, std::size_t size,
                           const std::string& track_id) {
    const std::uint32_t length = stored_prefix(bytes, size);
    if (length < dkr::runtime::custom_tracks::kModelMinimapSprite + 4U) {
        return;
    }
    std::uint8_t* at = bytes + kStoredPrefixAt +
                       dkr::runtime::custom_tracks::kModelMinimapSprite;
    const auto identifier = static_cast<std::int32_t>(read_be32(at));
    if (identifier < dkr::runtime::custom_tracks::kCustomSpriteIdBase ||
        identifier >= dkr::runtime::custom_tracks::kCustomSpriteIdBase +
                          dkr::runtime::custom_tracks::kCustomSpriteIdCount) {
        return; // A retail sprite: the track kept the minimap it had.
    }
    const auto ordinal = static_cast<std::uint32_t>(
        identifier - dkr::runtime::custom_tracks::kCustomSpriteIdBase);
    const std::int32_t index =
        own_entry_index(Section::Sprites, track_id, ordinal);
    if (index < 0) {
        write_be32(at, 0U);
        std::fprintf(stderr,
                     "[custom-tracks] %s: its minimap is not in this session's "
                     "sprite table (installed after boot?); relaunch to show it\n",
                     track_id.c_str());
        return;
    }
    write_be32(at, static_cast<std::uint32_t>(index));
    std::fprintf(stderr, "[custom-tracks] %s: minimap sprite resolved to %d\n",
                 track_id.c_str(), index);
}

// Point a sprite at the 2D textures its own track shipped, in place. The
// sprite table is published after the 2D texture table (tex_init_textures
// loads them in that order), so the texture's index is known here.
void resolve_sprite_texture(std::uint8_t* bytes, std::size_t size,
                            const std::string& track_id) {
    if (size < 2U) {
        return;
    }
    const auto identifier = static_cast<std::int16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
    if (identifier < dkr::runtime::custom_tracks::kCustomTexture2DIdBase ||
        identifier >= dkr::runtime::custom_tracks::kCustomTexture2DIdBase +
                          dkr::runtime::custom_tracks::kCustomTexture2DIdCount) {
        return; // A retail texture; the author meant exactly that one.
    }
    const auto ordinal = static_cast<std::uint32_t>(
        identifier - dkr::runtime::custom_tracks::kCustomTexture2DIdBase);
    const std::int32_t index =
        own_entry_index(Section::Textures2D, track_id, ordinal);
    const std::uint32_t value = index < 0 ? 0U : static_cast<std::uint32_t>(index);
    bytes[0] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    bytes[1] = static_cast<std::uint8_t>(value & 0xFFU);
    if (index < 0) {
        std::fprintf(stderr,
                     "[custom-tracks] %s: its minimap sprite names a 2D texture "
                     "that is not published\n", track_id.c_str());
    }
}

const Track* track_owning(Section section, const Entry* entry) {
    for (const Track& track : g_tracks) {
        for (const Entry& candidate : track.entries) {
            if (&candidate == entry && candidate.section == section) {
                return &track;
            }
        }
    }
    return nullptr;
}

// The index a track's `ordinal`-th own entry in `section` received in the
// published table, or -1. Assumes g_mutex is held, which it is: every caller
// runs inside build_extended_table or takes the lock itself.
std::int32_t own_entry_index(Section section, const std::string& track_id,
                             std::uint32_t ordinal) {
    const SectionState& table = g_sections[section_slot(section)];
    if (!table.built || track_id.empty()) {
        // The texture and sprite tables are published once, at boot. Not
        // built means nothing of this track's is in them, and saying so is the
        // only safe answer.
        return -1;
    }
    for (const AddedEntry& added : table.added) {
        if (added.track_id == track_id && added.within_track == ordinal) {
            return static_cast<std::int32_t>(added.index);
        }
    }
    return -1; // The track ships fewer entries than it names.
}

} // namespace

namespace dkr::runtime::custom_tracks {

// Defined below; all assume g_mutex is already held.
void scan_locked(const std::filesystem::path& directory);
void save_state_locked();   // writes armed track + auto boot to disk
void load_state_locked();   // reads them back, called from scan_locked

std::vector<Track> tracks() {
    std::scoped_lock lock(g_mutex);
    return g_tracks;
}

std::size_t enabled_count() {
    std::scoped_lock lock(g_mutex);
    return static_cast<std::size_t>(std::count_if(
        g_tracks.begin(), g_tracks.end(),
        [](const Track& track) { return track.enabled; }));
}

void set_enabled(const std::string& id, bool enabled) {
    std::scoped_lock lock(g_mutex);
    for (Track& track : g_tracks) {
        if (track.id == id) {
            track.enabled = enabled;
        }
    }
}

std::int32_t resolved_level_id(const std::string& track_id) {
    std::scoped_lock lock(g_mutex);
    const auto found = g_resolved_level_ids.find(track_id);
    return found == g_resolved_level_ids.end() ? -1 : found->second;
}

std::vector<TrackSelectEntry> track_select_entries() {
    std::scoped_lock lock(g_mutex);
    std::vector<TrackSelectEntry> result;
    for (const Track& track : g_tracks) {
        const auto resolved = g_resolved_level_ids.find(track.id);
        // Native preview/selection stores level IDs in signed bytes.
        if (!track.enabled || resolved == g_resolved_level_ids.end() ||
            resolved->second < 0 || resolved->second >= 128) continue;
        const auto header = std::find_if(track.entries.begin(), track.entries.end(),
            [](const Entry& entry) { return entry.section == Section::LevelHeaders; });
        if (header == track.entries.end() || header->bytes.size() < 0xC8 ||
            header->bytes[0] != kCustomTrackWorld || header->bytes[0x4C] != 0) continue;
        const auto vehicles = header->bytes[0x4E];
        if (!vehicles || (vehicles & ~7U)) continue;
        result.push_back({track.id, track.name, resolved->second, vehicles});
    }
    return result;
}

HdPack hd_pack(const std::string& track_id) {
    std::scoped_lock lock(g_mutex);
    for (const Track& track : g_tracks) {
        if (track.id != track_id) {
            continue;
        }
        HdPack pack;
        pack.file = track.hd_pack_file;
        pack.digest = track.hd_pack_digest;
        pack.sibling_archive = track.hd_pack_sibling;
        pack.sibling_mismatch = track.hd_pack_sibling_mismatch;
        return pack;
    }
    return {};
}

bool inspect_texture_payload(const std::vector<std::uint8_t>& bytes,
                             TextureInfo& info, std::string& error) {
    // TextureHeader, from include/structs.h.
    constexpr std::size_t kHeaderSize = 32U;
    constexpr std::size_t kFormat = 0x02U;
    constexpr std::size_t kFrames = 0x12U;
    constexpr std::size_t kTextureSize = 0x16U;
    constexpr std::size_t kIsCompressed = 0x1DU;
    // Bits per texel, by the format's low nibble.
    constexpr std::array<std::uint32_t, 9> kBits{32U, 16U, 8U, 4U, 16U,
                                                  8U, 4U, 4U, 8U};

    info = TextureInfo{};
    // load_texture pulls sizeof(TempTexHeader) bytes to find the frame count
    // before it knows the size. A payload shorter than that peek is served as
    // far as it goes and the rest comes from whatever the ROM holds past the
    // section: a header made of nothing and an allocation sized by it.
    if (bytes.size() < kMinimumTexturePayload) {
        error = std::to_string(bytes.size()) +
                " bytes; a texture is at least " +
                std::to_string(kMinimumTexturePayload) +
                ", which is what load_texture reads before it knows the size";
        return false;
    }
    // load_texture puts the display lists it builds at align16(tex +
    // assetSize) inside an allocation of exactly assetSize plus those lists,
    // so an unaligned payload pushes the last one past its own block.
    if ((bytes.size() % 16U) != 0U) {
        error = std::to_string(bytes.size()) +
                " bytes, and a texture payload has to be a multiple of 16 or "
                "load_texture's display list overruns its allocation";
        return false;
    }

    info.width = bytes[0];
    info.height = bytes[1];
    info.format = static_cast<std::uint8_t>(bytes[kFormat] & 0x0FU);
    info.render_mode = static_cast<std::uint8_t>(bytes[kFormat] >> 4U);
    info.frames = static_cast<std::uint16_t>(bytes[kFrames]);
    info.compressed = bytes[kIsCompressed] != 0U;
    info.translucent = texture_translucent(info.format, info.render_mode);

    if (info.format >= kBits.size()) {
        error = "format " + std::to_string(info.format) +
                " is not one material_init knows";
        return false;
    }
    if (info.format == 7U || info.format == 8U) {
        error = "a colour-indexed texture needs a palette from ASSET_EMPTY_14, "
                "which a track cannot add";
        return false;
    }
    if (info.render_mode > 3U) {
        error = "render mode " + std::to_string(info.render_mode) +
                " is not one of the four material_init knows";
        return false;
    }
    if (info.frames == 0U) {
        error = "the header says the texture has no frames";
        return false;
    }
    if (info.compressed) {
        return true;
    }

    std::size_t at = 0;
    for (std::uint32_t frame = 0; frame < info.frames; ++frame) {
        const std::string which = "frame " + std::to_string(frame + 1U);
        if (at + kHeaderSize > bytes.size()) {
            error = which + " of " + std::to_string(info.frames) +
                    " starts past the end of the payload";
            return false;
        }
        const std::uint32_t width = bytes[at];
        const std::uint32_t height = bytes[at + 1U];
        if (width == 0U || height == 0U) {
            error = which + " is " + std::to_string(width) + "x" +
                    std::to_string(height);
            return false;
        }
        if ((bytes[at + kFormat] & 0x0FU) != info.format) {
            error = which + " is in another format than the first";
            return false;
        }
        const std::size_t texels =
            (static_cast<std::size_t>(width) * height * kBits[info.format] +
             7U) / 8U;
        const std::size_t size =
            (static_cast<std::size_t>(bytes[at + kTextureSize]) << 8U) |
            bytes[at + kTextureSize + 1U];
        if (size < kHeaderSize + texels) {
            error = which + " says it is " + std::to_string(size) +
                    " bytes, and its header and texels take " +
                    std::to_string(kHeaderSize + texels);
            return false;
        }
        if (at + kHeaderSize + texels > bytes.size()) {
            error = which + "'s texels run past the end of the payload";
            return false;
        }
        at += size;
    }
    return true;
}

bool inspect_sprite_payload(const std::vector<std::uint8_t>& bytes,
                            SpriteInfo& info, std::string& error) {
    // SpriteHeader in the asset tool's fileTypes/sprite.hpp.
    constexpr std::size_t kHeaderSize = 12U;
    info = SpriteInfo{};
    if (bytes.size() < kHeaderSize + 2U) {
        error = std::to_string(bytes.size()) + " bytes; a sprite is at least " +
                std::to_string(kHeaderSize + 2U);
        return false;
    }
    if (bytes.size() > kMaxSpritePayload) {
        error = std::to_string(bytes.size()) + " bytes, and asset_load copies a "
                "sprite whole into a " + std::to_string(kMaxSpritePayload) +
                "-byte buffer";
        return false;
    }
    const auto be16 = [&bytes](std::size_t at) {
        return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1U]);
    };
    info.base_texture = static_cast<std::int16_t>(be16(0));
    info.frames = be16(2);
    info.anchor_x = static_cast<std::int16_t>(be16(4));
    info.anchor_y = static_cast<std::int16_t>(be16(6));
    if (info.frames == 0U || info.frames > 0x7FFFU) {
        error = "the sprite says it has " + std::to_string(info.frames) +
                " frames";
        return false;
    }
    if (kHeaderSize + info.frames + 1U > bytes.size()) {
        error = "the sprite's frame table runs past the end of the payload";
        return false;
    }
    if (bytes[kHeaderSize] != 0U) {
        error = "the sprite's first frame does not start at its first texture";
        return false;
    }
    for (std::uint32_t frame = 0; frame < info.frames; ++frame) {
        if (bytes[kHeaderSize + frame + 1U] <= bytes[kHeaderSize + frame]) {
            error = "sprite frame " + std::to_string(frame + 1U) +
                    " has no textures";
            return false;
        }
    }
    info.textures = bytes[kHeaderSize + info.frames];
    if (info.base_texture < 0) {
        error = "the sprite's first texture is " +
                std::to_string(info.base_texture);
        return false;
    }
    return true;
}

std::int64_t model_minimap_sprite(const std::vector<std::uint8_t>& bytes) {
    const std::uint32_t length = stored_prefix(bytes.data(), bytes.size());
    if (length < kModelMinimapSprite + 4U) {
        return -1;
    }
    return static_cast<std::int32_t>(
        read_be32(bytes.data() + kStoredPrefixAt + kModelMinimapSprite));
}

bool minimap_hidden(std::uint32_t header_offset) {
    std::scoped_lock lock(g_mutex);
    const SectionState& headers = g_sections[section_slot(Section::LevelHeaders)];
    const AddedEntry* header = nullptr;
    for (const AddedEntry& added : headers.added) {
        if (header_offset >= added.offset &&
            header_offset < added.offset + added.size) {
            header = &added;
            break;
        }
    }
    if (header == nullptr || header->track_id.empty()) {
        return false; // Retail, or not a track's header.
    }
    const Track* track = nullptr;
    for (const Track& candidate : g_tracks) {
        if (candidate.id == header->track_id) {
            track = &candidate;
        }
    }
    if (track == nullptr || track->sprites.empty()) {
        return false; // Keeps whatever minimap its model names.
    }
    for (std::uint32_t ordinal = 0; ordinal < track->sprites.size(); ++ordinal) {
        if (own_entry_index(Section::Sprites, track->id, ordinal) < 0) {
            return true;
        }
        const SpriteInfo& sprite = track->sprites[ordinal];
        if (sprite.base_texture >= kCustomTexture2DIdBase &&
            own_entry_index(Section::Textures2D, track->id,
                            static_cast<std::uint32_t>(
                                sprite.base_texture - kCustomTexture2DIdBase)) < 0) {
            return true;
        }
    }
    return false;
}

ArtworkSummary artwork(const std::string& track_id) {
    std::lock_guard lock(g_mutex);
    ArtworkSummary summary;
    for (const Track& track : g_tracks) {
        if (track.id != track_id) {
            continue;
        }
        for (const TextureInfo& texture : track.textures) {
            ++summary.textures;
            summary.translucent += texture.translucent ? 1U : 0U;
            summary.animated += texture.frames > 1U ? 1U : 0U;
        }
        summary.minimap = !track.sprites.empty();
        break;
    }
    return summary;
}

bool track_textures_published(const std::string& track_id) {
    std::scoped_lock lock(g_mutex);
    const SectionState& textures =
        g_sections[section_slot(Section::Textures3D)];
    if (!textures.built) {
        return false;
    }
    for (const AddedEntry& added : textures.added) {
        if (added.track_id == track_id) {
            return true;
        }
    }
    return false;
}

void arm_track_override(std::string track_id) {
    std::scoped_lock lock(g_mutex);
    if (g_armed_track == track_id) {
        return;
    }
    g_armed_track = std::move(track_id);
    save_state_locked();
    if (g_armed_track.empty()) {
        std::fprintf(stderr, "[custom-tracks] track override cleared\n");
    } else {
        std::fprintf(stderr, "[custom-tracks] track override armed: %s\n",
                     g_armed_track.c_str());
    }
}

std::string armed_track_id() {
    std::scoped_lock lock(g_mutex);
    return g_armed_track;
}

void set_auto_boot(bool enabled) {
    std::scoped_lock lock(g_mutex);
    if (g_auto_boot == enabled) {
        return;
    }
    g_auto_boot = enabled;
    // A fresh toggle re-arms this launch's one-shot; disabling clears it.
    g_auto_boot_consumed = false;
    save_state_locked();
    std::fprintf(stderr, "[custom-tracks] auto boot %s\n",
                 enabled ? "enabled" : "disabled");
}

bool auto_boot_enabled() {
    std::scoped_lock lock(g_mutex);
    return g_auto_boot;
}

bool consume_auto_boot() {
    std::scoped_lock lock(g_mutex);
    if (!g_auto_boot || g_auto_boot_consumed) {
        return false;
    }
    // The persisted flag stays on - it fires again on the next launch, until
    // the player turns it off - so only the per-launch one-shot is spent here.
    g_auto_boot_consumed = true;
    std::fprintf(stderr, "[custom-tracks] auto boot fired\n");
    return true;
}

std::int32_t track_override() {
    std::scoped_lock lock(g_mutex);
    if (g_armed_track.empty()) {
        return kNoTrackOverride;
    }
    // Resolved late on purpose: the id only exists once build_extended_table
    // has run, so arming in the launcher and racing later both work.
    const auto found = g_resolved_level_ids.find(g_armed_track);
    return found == g_resolved_level_ids.end() ? kNoTrackOverride
                                                : found->second;
}

bool owns_level_id(std::int32_t level_id) {
    std::scoped_lock lock(g_mutex);
    return std::any_of(g_resolved_level_ids.begin(),g_resolved_level_ids.end(),
        [level_id](const auto& entry){return entry.second==level_id;});
}

std::int32_t count_level_model_batches(const std::uint8_t* bytes, std::size_t size) {
    // LevelModel and LevelModelSegment offsets, docs/LEVEL_MODEL_FORMAT.md.
    constexpr std::size_t kContainerHeader = 5U;
    constexpr std::uint32_t kModelHeaderSize = 0x4CU;
    constexpr std::uint32_t kInflatedLimit = 0x100000U;
    constexpr std::size_t kSegmentsPointer = 0x04U;
    constexpr std::size_t kSegmentCount = 0x1AU;
    constexpr std::uint32_t kSegmentSize = 0x44U;
    constexpr std::size_t kSegmentBatchCount = 0x20U;
    if (bytes == nullptr || size <= kContainerHeader || bytes[4] != kContainerTag) {
        return -1;
    }
    const std::uint32_t inflated_size =
        static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
    if (inflated_size < kModelHeaderSize || inflated_size > kInflatedLimit) {
        return -1;
    }
    std::vector<std::uint8_t> model(inflated_size);
    // No TINFL_FLAG_PARSE_ZLIB_HEADER: the container holds raw DEFLATE.
    if (tinfl_decompress_mem_to_mem(model.data(), model.size(), bytes + kContainerHeader,
                                    size - kContainerHeader, 0) != model.size()) {
        return -1;
    }
    const auto be16 = [&](std::size_t at) {
        return static_cast<std::int16_t>((model[at] << 8) | model[at + 1U]);
    };
    const std::uint32_t segments = read_be32(model.data() + kSegmentsPointer);
    const std::int16_t count = be16(kSegmentCount);
    if (count < 0 ||
        std::uint64_t(segments) + std::uint64_t(count) * kSegmentSize > model.size()) {
        return -1;
    }
    std::int32_t batches = 0;
    for (std::int16_t segment = 0; segment < count; ++segment) {
        batches += std::max<std::int16_t>(
            0, be16(segments + std::size_t(segment) * kSegmentSize + kSegmentBatchCount));
    }
    return batches;
}

std::int32_t level_model_batches(std::int32_t level_id) {
    std::scoped_lock lock(g_mutex);
    for (const auto& [track_id, resolved] : g_resolved_level_ids) {
        if (resolved != level_id) {
            continue;
        }
        for (const Track& track : g_tracks) {
            if (track.id != track_id) {
                continue;
            }
            for (const Entry& entry : track.entries) {
                if (entry.section == Section::LevelModels) {
                    return count_level_model_batches(entry.bytes.data(), entry.bytes.size());
                }
            }
        }
    }
    return -1;
}

std::int32_t measure_level_model_arena(const std::uint8_t* bytes, std::size_t size) {
    // LevelModel and LevelModelSegment offsets, docs/LEVEL_MODEL_FORMAT.md.
    constexpr std::size_t kContainerHeader = 5U;
    constexpr std::uint32_t kModelHeaderSize = 0x4CU;
    constexpr std::uint32_t kInflatedLimit = 0x100000U;
    constexpr std::size_t kModelSizeField = 0x48U;
    constexpr std::size_t kSegmentsPointer = 0x04U;
    constexpr std::size_t kSegmentCount = 0x1AU;
    constexpr std::uint32_t kSegmentSize = 0x44U;
    constexpr std::size_t kSegmentTriangles = 0x04U;
    constexpr std::size_t kSegmentBatches = 0x0CU;
    constexpr std::size_t kSegmentFacets = 0x14U;
    constexpr std::size_t kSegmentTriangleCount = 0x1EU;
    constexpr std::size_t kSegmentBatchCount = 0x20U;
    constexpr std::size_t kTriangleStride = 16U;
    constexpr std::size_t kBatchStride = 12U;
    constexpr std::size_t kFacetStride = 8U;
    // TRI_FLAG_80 skips a triangle entirely; RENDER_NO_COLLISION (1 << 9) is
    // the batch opting out of collision; 0x2000 is the batch func_8002C71C
    // records into segment->unk34.
    constexpr std::uint8_t kTriangleSkipped = 0x80U;
    constexpr std::uint32_t kBatchNoCollision = 0x200U;
    constexpr std::uint32_t kBatchSpecial = 0x2000U;
    // track_init_collision packs a shared plane as `index | 0x8000`, so a plane
    // index only has fifteen bits. That is a format ceiling, not a memory one:
    // no larger heap lifts it, which is why it is refused here rather than
    // measured and handed on.
    constexpr std::uint32_t kPlaneLimit = 0x8000U;
    // Nothing this side of a corrupt payload reaches a megabyte of arena per
    // segment; the cap only keeps the accumulator honest.
    constexpr std::uint64_t kArenaLimit = 0x800000U;

    if (bytes == nullptr || size <= kContainerHeader || bytes[4] != kContainerTag) {
        return -1;
    }
    const std::uint32_t inflated_size =
        static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
    if (inflated_size < kModelHeaderSize || inflated_size > kInflatedLimit) {
        return -1;
    }
    std::vector<std::uint8_t> model(inflated_size);
    // No TINFL_FLAG_PARSE_ZLIB_HEADER: the container holds raw DEFLATE.
    if (tinfl_decompress_mem_to_mem(model.data(), model.size(), bytes + kContainerHeader,
                                    size - kContainerHeader, 0) != model.size()) {
        return -1;
    }
    const auto fits = [&](std::uint64_t at, std::uint64_t length) {
        return at + length <= model.size();
    };
    const auto be16 = [&](std::size_t at) -> std::uint32_t {
        return static_cast<std::uint32_t>(model[at] << 8) | model[at + 1U];
    };
    const auto align16 = [](std::uint64_t value) { return (value + 15U) & ~std::uint64_t(15U); };

    const std::uint32_t model_size = read_be32(model.data() + kModelSizeField);
    const std::uint32_t segments = read_be32(model.data() + kSegmentsPointer);
    const std::int16_t count = static_cast<std::int16_t>(be16(kSegmentCount));
    if (model_size < kModelHeaderSize || model_size > inflated_size || count <= 0 ||
        !fits(segments, std::uint64_t(count) * kSegmentSize)) {
        return -1;
    }

    // The loader grows its arena from modelSize, not from the end of the file.
    std::uint64_t constructed = model_size;
    for (std::int16_t index = 0; index < count; ++index) {
        const std::size_t segment = segments + std::size_t(index) * kSegmentSize;
        const std::uint32_t triangles = read_be32(model.data() + segment + kSegmentTriangles);
        const std::uint32_t batches = read_be32(model.data() + segment + kSegmentBatches);
        const std::uint32_t facets = read_be32(model.data() + segment + kSegmentFacets);
        const std::uint32_t triangle_count = be16(segment + kSegmentTriangleCount);
        const std::uint32_t batch_count = be16(segment + kSegmentBatchCount);
        // The batch array carries a sentinel entry past the last batch; the
        // triangle window of batch n is read from n and n + 1 alike.
        if (!fits(triangles, std::uint64_t(triangle_count) * kTriangleStride) ||
            !fits(facets, std::uint64_t(triangle_count) * kFacetStride) ||
            !fits(batches, (std::uint64_t(batch_count) + 1U) * kBatchStride)) {
            return -1;
        }

        // One plane per drawn triangle first, so an edge plane built later can
        // be told apart from a neighbour's base plane by its index alone.
        std::vector<bool> collidable(triangle_count, false);
        std::uint32_t planes = 0;
        std::uint32_t special = 0;
        for (std::uint32_t batch = 0; batch < batch_count; ++batch) {
            const std::size_t entry = batches + std::size_t(batch) * kBatchStride;
            const std::uint32_t first = be16(entry + 4U);
            const std::uint32_t last = be16(entry + kBatchStride + 4U);
            const std::uint32_t flags = read_be32(model.data() + entry + 8U);
            if (flags & kBatchSpecial) {
                ++special;
            }
            if (first > last || last > triangle_count) {
                return -1;
            }
            for (std::uint32_t triangle = first; triangle < last; ++triangle) {
                if (model[triangles + std::size_t(triangle) * kTriangleStride] & kTriangleSkipped) {
                    continue;
                }
                ++planes;
                collidable[triangle] = (flags & kBatchNoCollision) == 0;
            }
        }

        const std::uint32_t base_planes = planes;
        std::vector<std::array<std::uint32_t, 3>> edges(triangle_count);
        for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
            for (unsigned edge = 0; edge < 3; ++edge) {
                edges[triangle][edge] = be16(facets + std::size_t(triangle) * kFacetStride +
                                             2U + std::size_t(edge) * 2U);
            }
        }
        // An edge still naming a base plane has not been built yet: build one,
        // and mark the neighbour's matching entry so the pair shares it.
        for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
            if (!collidable[triangle]) {
                continue;
            }
            const std::uint32_t base = be16(facets + std::size_t(triangle) * kFacetStride);
            if (base >= base_planes) {
                return -1;
            }
            for (unsigned edge = 0; edge < 3; ++edge) {
                const std::uint32_t next = edges[triangle][edge];
                if (next >= base_planes) {
                    continue; // Already built, or shared by the neighbour.
                }
                if (next >= triangle_count) {
                    return -1;
                }
                if (next != base) {
                    for (std::uint32_t& reciprocal : edges[next]) {
                        if (reciprocal == base) {
                            reciprocal = planes | 0x8000U;
                        }
                    }
                }
                edges[triangle][edge] = planes++;
            }
        }
        if (planes >= kPlaneLimit) {
            return -1;
        }

        constructed = align16(constructed + std::uint64_t(triangle_count) * 2U); // unk10
        constructed += std::uint64_t(planes) * 16U;                              // collisionPlanes
        constructed = align16(constructed + std::uint64_t(special) * 2U);        // unk34
        if (constructed > kArenaLimit) {
            return -1;
        }
    }
    return static_cast<std::int32_t>(constructed);
}

std::int32_t level_model_arena_bytes(std::int32_t level_id) {
    std::scoped_lock lock(g_mutex);
    for (const auto& [track_id, resolved] : g_resolved_level_ids) {
        if (resolved != level_id) {
            continue;
        }
        for (const Track& track : g_tracks) {
            if (track.id != track_id) {
                continue;
            }
            for (const Entry& entry : track.entries) {
                if (entry.section == Section::LevelModels) {
                    return measure_level_model_arena(entry.bytes.data(), entry.bytes.size());
                }
            }
        }
    }
    return -1;
}

std::vector<std::int32_t> build_extended_table(
    Section section, const std::int32_t* retail_table) {
    std::scoped_lock lock(g_mutex);

    // A rescan asked for while a level was live waits here. This is a level
    // load, so replacing the track list now cannot disturb anything that is
    // already running.
    if (g_rescan_pending) {
        std::fprintf(stderr, "[custom-tracks] applying deferred rescan\n");
        scan_locked(g_directory);
    }

    std::vector<std::int32_t> result;
    if (retail_table == nullptr) {
        return result;
    }

    // Retail layout is [o0 .. o(n-1), oEnd, -1]: count the entries ahead of
    // the terminator, then drop one, exactly as level_global_init does. The
    // dropped slot is the end offset that sizes the final retail entry.
    std::uint32_t counted = 0;
    constexpr std::uint32_t kSanityLimit = 4096U;
    while (counted < kSanityLimit && retail_table[counted] != -1) {
        ++counted;
    }
    if (counted == 0 || counted >= kSanityLimit) {
        std::fprintf(stderr,
                     "[custom-tracks] refusing an unterminated asset table\n");
        return result;
    }
    const std::uint32_t retail_count = counted - 1U;

    const std::vector<const Entry*> entries = enabled_entries(section);
    SectionState& state = g_sections[section_slot(section)];
    state.retail_count = retail_count;
    state.end_offset = static_cast<std::uint32_t>(retail_table[retail_count]);
    state.blob.clear();
    state.added.clear();

    if (section == Section::LevelHeaders) {
        // Drop every previously resolved id before reassigning. Without this a
        // track that has since been disabled would keep reporting the level id
        // it held in an earlier build, and the UI would offer to launch an
        // index the rebuilt table no longer defines.
        g_resolved_level_ids.clear();
    }
    state.built = true;

    if (entries.empty()) {
        // Nothing added: the caller publishes the retail table unchanged.
        return result;
    }

    // Retail offsets are copied verbatim, INCLUDING the end offset, which
    // becomes the first custom entry's offset. That keeps the last retail
    // entry's size (oEnd - o(n-1)) exactly as authored.
    result.assign(retail_table, retail_table + retail_count + 1U);

    std::uint32_t running = state.end_offset;
    std::unordered_map<std::string, std::uint32_t> seen_per_track;
    for (std::uint32_t ordinal = 0; ordinal < entries.size(); ++ordinal) {
        const Entry* entry = entries[ordinal];
        const Track* owner = track_owning(section, entry);
        const std::uint32_t entry_size =
            static_cast<std::uint32_t>(entry->bytes.size());
        const std::string owner_id =
            owner == nullptr ? std::string{} : owner->id;
        const std::uint32_t within = seen_per_track[owner_id]++;

        state.added.push_back(AddedEntry{
            owner_id,
            entry->slot,
            running, entry_size, retail_count + ordinal, within});

        state.blob.insert(state.blob.end(), entry->bytes.begin(),
                          entry->bytes.end());
        if (section == Section::LevelModels && entry_size != 0U) {
            // A model names its own track's textures by ordinal, and only the
            // published texture table knows which index each one got. The copy
            // in the blob is what gets served, so it is the copy that is
            // rewritten - `entry->bytes` stays as the file wrote it, which is
            // what makes a rebuild on the next level load idempotent rather
            // than cumulative.
            resolve_model_textures(state.blob.data() + state.blob.size() -
                                       entry_size,
                                   entry_size, owner_id);
            resolve_model_minimap(state.blob.data() + state.blob.size() -
                                      entry_size,
                                  entry_size, owner_id);
        }
        if (section == Section::Sprites && entry_size != 0U) {
            resolve_sprite_texture(state.blob.data() + state.blob.size() -
                                       entry_size,
                                   entry_size, owner_id);
        }
        running += entry_size;
        result.push_back(static_cast<std::int32_t>(running));

        if (section == Section::LevelHeaders && owner != nullptr) {
            g_resolved_level_ids[owner->id] =
                static_cast<std::int32_t>(retail_count + ordinal);
        }
    }
    result.push_back(-1);

    std::fprintf(stderr,
                 "[custom-tracks] section %zu: %u retail + %zu added\n",
                 section_slot(section), retail_count, entries.size());
    return result;
}

const std::uint8_t* payload_for(Section section, std::uint32_t offset,
                                 std::int32_t size) {
    std::scoped_lock lock(g_mutex);
    const SectionState& state = g_sections[section_slot(section)];
    if (!state.built || size <= 0 || offset < state.end_offset) {
        return nullptr; // Retail range: the ROM loader owns it.
    }
    const std::uint64_t relative =
        static_cast<std::uint64_t>(offset) - state.end_offset;
    if (relative + static_cast<std::uint64_t>(size) > state.blob.size()) {
        std::fprintf(stderr,
                     "[custom-tracks] rejecting out-of-range custom read\n");
        return nullptr;
    }
    return state.blob.data() + relative;
}

std::int32_t sibling_index(Section from, std::uint32_t offset, Section to,
                            MapSlot slot) {
    std::scoped_lock lock(g_mutex);
    const SectionState& source = g_sections[section_slot(from)];
    const SectionState& target = g_sections[section_slot(to)];
    if (!source.built || !target.built) {
        return -1;
    }
    for (const AddedEntry& entry : source.added) {
        if (offset < entry.offset || offset >= entry.offset + entry.size) {
            continue;
        }
        if (entry.track_id.empty()) {
            return -1;
        }
        for (const AddedEntry& sibling : target.added) {
            if (sibling.track_id == entry.track_id && sibling.slot == slot) {
                return static_cast<std::int32_t>(sibling.index);
            }
        }
        return -1; // The track supplies nothing for that section and slot.
    }
    return -1; // Not a custom offset.
}

} // namespace dkr::runtime::custom_tracks


namespace {

const std::unordered_map<std::string, Section>& section_names() {
    static const std::unordered_map<std::string, Section> names{
        {"LEVEL_HEADERS", Section::LevelHeaders},
        {"LEVEL_OBJECT_MAPS", Section::LevelObjectMaps},
        {"LEVEL_NAMES", Section::LevelNames},
        {"LEVEL_MODELS", Section::LevelModels},
        {"TEXTURES_3D", Section::Textures3D},
        {"TEXTURES_2D", Section::Textures2D},
        {"SPRITES", Section::Sprites},
    };
    return names;
}

bool read_file(const std::filesystem::path& path,
               std::vector<std::uint8_t>& bytes) {
    std::error_code code;
    const auto size = std::filesystem::file_size(path, code);
    if (code || size == 0 || size > (16U * 1024U * 1024U)) {
        return false;
    }
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return read == bytes.size();
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return extension;
}

// One entry out of a zip, into `bytes`. False when the zip or entry is missing.
// Used for the pack stamp; the payloads a track ships are plain files.
bool read_zip_entry(const std::filesystem::path& zip_path,
                    const char* entry_name,
                    std::vector<std::uint8_t>& bytes) {
    mz_zip_archive zip{};
    if (mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0) == MZ_FALSE) {
        return false;
    }
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, entry_name, &size, 0);
    mz_zip_reader_end(&zip);
    if (data == nullptr) {
        return false;
    }
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.assign(first, first + size);
    mz_free(data);
    return true;
}

// The textureDigest stamped into a <track>-hd.zip by the Blender export
// (rice_pack.STAMP_NAME). Equal to the track manifest's hdTexturePack digest
// exactly when the pack is from this export of this track.
bool read_hd_pack_digest(const std::filesystem::path& archive,
                         std::string& digest) {
    std::vector<std::uint8_t> bytes;
    if (!read_zip_entry(archive, "dkr-r-track.json", bytes)) {
        return false;
    }
    const nlohmann::json stamp = nlohmann::json::parse(
        bytes.begin(), bytes.end(), nullptr, false);
    if (stamp.is_discarded() || !stamp.is_object()) {
        return false;
    }
    digest = stamp.value("textureDigest", std::string{});
    return !digest.empty();
}

// Unpacks every entry of `zip_path` under `destination`. Rejects absolute
// paths and `..` so an archive can never write outside the target directory.
bool extract_zip(const std::filesystem::path& zip_path,
                 const std::filesystem::path& destination,
                 std::string& error) {
    mz_zip_archive zip{};
    if (mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0) == MZ_FALSE) {
        error = "the .zip could not be opened";
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip, index, &stat) == MZ_FALSE) {
            continue;
        }
        std::string name = stat.m_filename;
        if (name.empty() || name.front() == '/' || name.front() == '\\' ||
            name.find("..") != std::string::npos ||
            (name.size() > 1U && name[1] == ':')) {
            mz_zip_reader_end(&zip);
            error = "the .zip contains an unsafe path";
            return false;
        }
        const std::filesystem::path target =
            destination / std::filesystem::u8path(name);
        std::error_code code;
        if (mz_zip_reader_is_file_a_directory(&zip, index) == MZ_TRUE) {
            std::filesystem::create_directories(target, code);
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), code);
        if (mz_zip_reader_extract_to_file(
                &zip, index, target.string().c_str(), 0) == MZ_FALSE) {
            mz_zip_reader_end(&zip);
            error = "the .zip could not be fully extracted";
            return false;
        }
    }
    mz_zip_reader_end(&zip);
    return true;
}

// In an unpacked zip under `root`, finds the .dkrmap. A manifest.json at the
// root means the zip was a bare .dkrmap (no wrapped pack). Otherwise the track
// is one folder down and the wrapper's <track>-hd.zip is a *.zip beside it,
// still under `root`. Returns false when no manifest is found.
bool locate_unpacked_track(const std::filesystem::path& root,
                           std::filesystem::path& track_dir,
                           std::filesystem::path& hd_pack) {
    std::error_code code;
    if (std::filesystem::is_regular_file(root / "manifest.json", code)) {
        track_dir = root;
        return true;
    }
    for (const auto& item :
         std::filesystem::directory_iterator(root, code)) {
        if (item.is_directory(code) &&
            std::filesystem::is_regular_file(
                item.path() / "manifest.json", code)) {
            track_dir = item.path();
            break;
        }
    }
    if (track_dir.empty()) {
        return false;
    }
    for (const auto& item :
         std::filesystem::directory_iterator(root, code)) {
        if (item.is_regular_file(code) &&
            lower_extension(item.path()) == ".zip") {
            hd_pack = item.path();
            break;
        }
    }
    return true;
}

// Parses one unpacked track. `.dkrmap` archives are unpacked into this form by
// the importer; the directory form is also what an author edits in place, so
// reload() can pick up an editor's save without a repack.
bool parse_track(const std::filesystem::path& root, Track& track,
                 std::string& error) {
    std::vector<std::uint8_t> manifest_bytes;
    if (!read_file(root / "manifest.json", manifest_bytes)) {
        error = "manifest.json is missing or unreadable";
        return false;
    }

    nlohmann::json manifest = nlohmann::json::parse(
        manifest_bytes.begin(), manifest_bytes.end(), nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        error = "manifest.json is not valid JSON";
        return false;
    }
    if (manifest.value("schemaVersion", 0) != 1) {
        error = "unsupported schemaVersion";
        return false;
    }

    track.id = manifest.value("id", std::string{});
    track.name = manifest.value("name", track.id);
    track.author = manifest.value("author", std::string{});
    track.source = root;
    if (track.id.empty()) {
        error = "manifest.json has no id";
        return false;
    }

    // Informational: the exporter records which <track>-hd.zip it wrote beside
    // the package, and the digest both files carry. Read the keys it knows and
    // nothing else. The file names a sibling, so it must be a bare filename.
    const auto hd = manifest.find("hdTexturePack");
    if (hd != manifest.end() && hd->is_object()) {
        const std::string file = hd->value("file", std::string{});
        if (!file.empty() && file.find("..") == std::string::npos &&
            file.find('/') == std::string::npos &&
            file.find('\\') == std::string::npos) {
            track.hd_pack_file = file;
            track.hd_pack_digest = hd->value("textureDigest", std::string{});
        }
    }

    const auto adds = manifest.find("adds");
    if (adds == manifest.end() || !adds->is_array() || adds->empty()) {
        error = "manifest.json adds nothing";
        return false;
    }
    for (const auto& item : *adds) {
        const auto named =
            section_names().find(item.value("section", std::string{}));
        if (named == section_names().end()) {
            error = "unknown section in adds";
            return false;
        }
        Entry entry;
        entry.section = named->second;

        // A level spawns from two object maps with different roles, so a
        // payload for that section has to say which one it is. Refusing the
        // ambiguous case is deliberate: a merged map silently spawns
        // checkpoints and spawn points as collectables while the retail
        // structure map keeps running alongside it.
        if (entry.section == Section::LevelObjectMaps) {
            const std::string slot = item.value("slot", std::string{});
            if (slot == "structure") {
                entry.slot = MapSlot::Structure;
            } else if (slot == "collectables") {
                entry.slot = MapSlot::Collectables;
            } else {
                error = slot.empty()
                    ? "a LEVEL_OBJECT_MAPS entry needs \"slot\": "
                      "\"structure\" or \"collectables\""
                    : "unknown object map slot \"" + slot + "\"";
                return false;
            }
        }
        const std::string file = item.value("file", std::string{});
        // Keep payloads inside the track directory: a manifest must not be
        // able to name an arbitrary path on the user's machine.
        if (file.empty() || file.find("..") != std::string::npos ||
            std::filesystem::path(file).is_absolute()) {
            error = "adds entry has an unsafe file path";
            return false;
        }
        if (!read_file(root / file, entry.bytes)) {
            error = "could not read " + file;
            return false;
        }

        // A texture payload is the one kind the loader reads before it knows
        // how big it is, and material_init reads how to draw it - format,
        // render mode, frame count - out of its own headers. See
        // inspect_texture_payload.
        if (entry.section == Section::Textures3D) {
            dkr::runtime::custom_tracks::TextureInfo info;
            std::string reason;
            if (!dkr::runtime::custom_tracks::inspect_texture_payload(
                    entry.bytes, info, reason)) {
                error = file + ": " + reason;
                return false;
            }
            track.textures.push_back(info);
        }
        // The minimap's picture is a texture like any other to load_texture,
        // and its sprite is copied whole into a fixed buffer.
        if (entry.section == Section::Textures2D) {
            dkr::runtime::custom_tracks::TextureInfo info;
            std::string reason;
            if (!dkr::runtime::custom_tracks::inspect_texture_payload(
                    entry.bytes, info, reason)) {
                error = file + ": " + reason;
                return false;
            }
            track.textures_2d.push_back(info);
        }
        if (entry.section == Section::Sprites) {
            dkr::runtime::custom_tracks::SpriteInfo info;
            std::string reason;
            if (!dkr::runtime::custom_tracks::inspect_sprite_payload(
                    entry.bytes, info, reason)) {
                error = file + ": " + reason;
                return false;
            }
            track.sprites.push_back(info);
        }
        track.entries.push_back(std::move(entry));
    }

    // A placeholder that names more than the track ships would resolve to
    // nothing, and the HUD would draw some other sprite or texture in its
    // place. Refuse it here, where the author can be told which.
    for (std::size_t index = 0; index < track.sprites.size(); ++index) {
        const auto& sprite = track.sprites[index];
        const std::int32_t base = sprite.base_texture;
        if (base < dkr::runtime::custom_tracks::kCustomTexture2DIdBase) {
            continue;
        }
        const std::int32_t first =
            base - dkr::runtime::custom_tracks::kCustomTexture2DIdBase;
        if (first >= dkr::runtime::custom_tracks::kCustomTexture2DIdCount ||
            static_cast<std::size_t>(first) + sprite.textures >
                track.textures_2d.size()) {
            error = "sprite " + std::to_string(index + 1U) + " draws the track's "
                    "2D texture " + std::to_string(first + 1) + " onward, and "
                    "the track ships " + std::to_string(track.textures_2d.size());
            return false;
        }
    }
    for (const Entry& entry : track.entries) {
        if (entry.section != Section::LevelModels) {
            continue;
        }
        const std::int64_t named =
            dkr::runtime::custom_tracks::model_minimap_sprite(entry.bytes);
        if (named < dkr::runtime::custom_tracks::kCustomSpriteIdBase ||
            named >= dkr::runtime::custom_tracks::kCustomSpriteIdBase +
                         dkr::runtime::custom_tracks::kCustomSpriteIdCount) {
            continue;
        }
        const std::int64_t ordinal =
            named - dkr::runtime::custom_tracks::kCustomSpriteIdBase;
        if (static_cast<std::size_t>(ordinal) >= track.sprites.size()) {
            error = "the model names minimap sprite " +
                    std::to_string(ordinal + 1) + " of the track's own, and the "
                    "track ships " + std::to_string(track.sprites.size()) +
                    " - its minimap would point nowhere";
            return false;
        }
    }

    // A header carries 0 in both object map fields, because the real indices
    // are assigned at load. That is only safe when both are patched: 0 is a
    // valid index, not "no map", so an unpatched field makes the game spawn
    // some other level's layer over this track - which hangs it. Refuse the
    // package rather than let it load and freeze.
    const Entry* header = nullptr;
    for (const Entry& entry : track.entries) {
        if (entry.section == Section::LevelHeaders) {
            header = &entry;
        }
    }
    if (header != nullptr) {
        const auto supplies = [&track](MapSlot slot) {
            return std::any_of(track.entries.begin(), track.entries.end(),
                               [slot](const Entry& e) {
                                   return e.section ==
                                              Section::LevelObjectMaps &&
                                          e.slot == slot;
                               });
        };
        // Reading the authored value matters: a header carrying a real retail
        // index needs no payload and is perfectly valid. Only a zero is
        // dangerous, and only when nothing will overwrite it.
        const auto field_at = [header](std::size_t offset) -> int {
            if (header->bytes.size() < offset + 2U) {
                return -1; // Too short to contain it; nothing to judge.
            }
            return (header->bytes[offset] << 8) | header->bytes[offset + 1];
        };
        struct Check { std::size_t offset; MapSlot slot; const char* name; };
        for (const Check& check : {Check{0x36, MapSlot::Collectables,
                                         "collectables"},
                                   Check{0xBA, MapSlot::Structure,
                                         "structure"}}) {
            if (field_at(check.offset) == 0 && !supplies(check.slot)) {
                error = std::string("the header leaves its ") + check.name +
                        " object map at 0 and the track supplies none; 0 is "
                        "object map 0, not \"no map\", so the game would spawn "
                        "another level's objects";
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace dkr::runtime::custom_tracks {

namespace {

std::filesystem::path working_setting_file() {
    // Beside the install directory, so it lives with the rest of the
    // configuration rather than in the tracks folder it points away from.
    return g_directory.empty()
        ? std::filesystem::path{}
        : g_directory.parent_path() / "custom-tracks-path.txt";
}

std::filesystem::path state_setting_file() {
    return g_directory.empty()
        ? std::filesystem::path{}
        : g_directory.parent_path() / "custom-tracks-state.txt";
}

} // namespace

// The armed track and the auto-boot setting outlive the process: the relaunch
// that publishes a just-installed track's textures has to land back on it.
// Both assume g_mutex is held.
void save_state_locked() {
    const std::filesystem::path file = state_setting_file();
    if (file.empty()) {
        return;
    }
    std::error_code code;
    if (g_armed_track.empty() && !g_auto_boot) {
        std::filesystem::remove(file, code);
        return;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << "armed=" << g_armed_track << "\n"
        << "auto_boot=" << (g_auto_boot ? 1 : 0) << "\n";
}

void load_state_locked() {
    std::vector<std::uint8_t> bytes;
    if (!read_file(state_setting_file(), bytes)) {
        return;
    }
    const std::string text(bytes.begin(), bytes.end());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.rfind("armed=", 0) == 0) {
            g_armed_track = line.substr(6);
        } else if (line.rfind("auto_boot=", 0) == 0) {
            g_auto_boot = line.substr(10) == "1";
        }
    }
}

namespace {

// Appends every *.dkrmap in one directory. Called for the install directory
// and, when set, for the author's working directory.
void scan_one(const std::filesystem::path& directory, const char* label) {
    std::error_code code;
    if (directory.empty() ||
        !std::filesystem::is_directory(directory, code)) {
        return;
    }
    // Filesystems enumerate entries differently. Assign appended asset indices
    // in a stable order on Windows and Linux, preserving installed-first
    // precedence over the author's working folder.
    std::vector<std::filesystem::directory_entry> items;
    for (const auto& item : std::filesystem::directory_iterator(directory, code)) {
        items.push_back(item);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.path().filename().generic_u8string() < b.path().filename().generic_u8string();
    });
    for (const auto& item : items) {
        if (!item.is_directory() || item.path().extension() != ".dkrmap") {
            continue;
        }
        Track track;
        std::string error;
        if (!parse_track(item.path(), track, error)) {
            std::fprintf(stderr, "[custom-tracks] skipped %s: %s\n",
                         item.path().filename().string().c_str(),
                         error.c_str());
            continue;
        }
        if (!track.textures.empty()) {
            std::size_t translucent = 0;
            std::size_t animated = 0;
            for (const auto& texture : track.textures) {
                translucent += texture.translucent ? 1U : 0U;
                animated += texture.frames > 1U ? 1U : 0U;
            }
            std::fprintf(stderr,
                         "[custom-tracks] %s ships %zu texture(s): %zu "
                         "see-through, %zu animated\n",
                         track.id.c_str(), track.textures.size(), translucent,
                         animated);
        }

        // Resolve the HD pack sibling against the folder this track was read
        // from. For an installed copy that folder is the install directory and
        // the pack is not there - install() handed it to the importer already;
        // for a working folder the sibling the exporter just wrote is present.
        if (!track.hd_pack_file.empty()) {
            const std::filesystem::path sibling =
                item.path().parent_path() / track.hd_pack_file;
            std::error_code sib;
            if (std::filesystem::is_regular_file(sibling, sib)) {
                std::string digest;
                if (read_hd_pack_digest(sibling, digest) &&
                    digest == track.hd_pack_digest &&
                    !track.hd_pack_digest.empty()) {
                    track.hd_pack_sibling = sibling;
                } else {
                    track.hd_pack_sibling_mismatch = true;
                }
            }
        }

        // Ids key every cross-reference: which level a track resolves to, and
        // which object map a header is pointed at. Two tracks sharing one id
        // make those lookups pick an arbitrary winner, so a header can end up
        // aimed at the other copy's maps. Importing a track that is also being
        // watched in a working folder produces exactly that pair, so refuse
        // the duplicate rather than load it.
        const auto clash = std::find_if(
            g_tracks.begin(), g_tracks.end(),
            [&track](const Track& existing) {
                return existing.id == track.id;
            });
        if (clash != g_tracks.end()) {
            std::fprintf(stderr,
                         "[custom-tracks] skipped %s: id \"%s\" is already "
                         "loaded from %s\n",
                         item.path().filename().string().c_str(),
                         track.id.c_str(),
                         clash->source.string().c_str());
            continue;
        }

        std::fprintf(stderr, "[custom-tracks] loaded %s by %s (%s)\n",
                     track.name.c_str(),
                     track.author.empty() ? "unknown" : track.author.c_str(),
                     label);
        g_tracks.push_back(std::move(track));
    }
}

} // namespace

void scan(const std::filesystem::path& directory) {
    std::scoped_lock lock(g_mutex);
    scan_locked(directory);
}

void scan_locked(const std::filesystem::path& directory) {
    g_rescan_pending = false;
    g_directory = directory;
    g_tracks.clear();
    g_resolved_level_ids.clear();

    // Restore the author's working directory before scanning so a restart
    // keeps reading wherever they pointed it.
    if (g_working_directory.empty()) {
        std::vector<std::uint8_t> stored;
        if (read_file(working_setting_file(), stored) && !stored.empty()) {
            std::string text(stored.begin(), stored.end());
            while (!text.empty() &&
                   (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }
            g_working_directory = std::filesystem::u8path(text);
        }
    }

    // Restore the armed track and auto-boot setting for the same reason: a
    // relaunch is how a just-installed track's textures get published.
    load_state_locked();

    scan_one(directory, "installed");
    if (g_working_directory != directory) {
        scan_one(g_working_directory, "working folder");
    }
}

void set_working_directory(const std::filesystem::path& directory) {
    {
        std::scoped_lock lock(g_mutex);
        g_working_directory = directory;
        const std::filesystem::path setting = working_setting_file();
        if (!setting.empty()) {
            std::error_code code;
            if (directory.empty()) {
                std::filesystem::remove(setting, code);
            } else {
                const auto utf8 = directory.u8string();
                const std::string text(utf8.begin(), utf8.end());
                std::ofstream out(setting, std::ios::binary);
                out.write(text.data(),
                          static_cast<std::streamsize>(text.size()));
            }
        }
        std::fprintf(stderr, "[custom-tracks] working folder %s\n",
                     directory.empty() ? "cleared"
                                       : directory.string().c_str());
    }
    reload();
}

std::filesystem::path working_directory() {
    std::scoped_lock lock(g_mutex);
    return g_working_directory;
}

std::filesystem::path directory() {
    std::scoped_lock lock(g_mutex);
    return g_directory;
}

void discard_install_temp(const std::filesystem::path& temp_root) {
    if (temp_root.empty()) {
        return;
    }
    std::error_code code;
    std::filesystem::remove_all(temp_root, code);
}

bool install(const std::filesystem::path& source, std::string& error,
             InstallOutcome* outcome) {
    std::filesystem::path destination_root;
    {
        std::scoped_lock lock(g_mutex);
        destination_root = g_directory;
    }
    if (destination_root.empty()) {
        error = "Custom tracks have no install directory yet.";
        return false;
    }

    std::error_code code;

    // A track arrives one of three ways: the .dkrmap folder itself, a .zip of
    // that folder, or a .zip that also wraps the <track>-hd.zip beside it. The
    // zip forms are unpacked to a temp directory and handled as the folder
    // case; the wrapped pack, when present, is reported for the caller to
    // import before it calls discard_install_temp().
    std::filesystem::path track_dir = source;
    std::filesystem::path temp_root;
    std::filesystem::path wrapped_hd_pack;

    if (std::filesystem::is_regular_file(source, code) &&
        lower_extension(source) == ".zip") {
        temp_root = destination_root.parent_path() /
            (".custom-track-import-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count()));
        std::filesystem::remove_all(temp_root, code);
        std::filesystem::create_directories(temp_root, code);
        std::string extract_error;
        if (!extract_zip(source, temp_root, extract_error)) {
            discard_install_temp(temp_root);
            error = "That .zip is not a track: " + extract_error + ".";
            return false;
        }
        if (!locate_unpacked_track(temp_root, track_dir, wrapped_hd_pack)) {
            discard_install_temp(temp_root);
            error = "That .zip does not contain a .dkrmap track.";
            return false;
        }
    } else if (!std::filesystem::is_directory(source, code)) {
        error = "A track is a .dkrmap folder or a .zip of one.";
        return false;
    } else if (source.extension() != ".dkrmap") {
        error = "That folder is not named *.dkrmap.";
        return false;
    }

    if (!std::filesystem::is_regular_file(track_dir / "manifest.json", code)) {
        discard_install_temp(temp_root);
        error = "That track has no manifest.json.";
        return false;
    }

    // Parse before copying so a broken track is rejected rather than installed
    // and then reported as skipped on the next scan.
    Track probe;
    if (!parse_track(track_dir, probe, error)) {
        discard_install_temp(temp_root);
        return false;
    }

    // A zip's inner folder can be named anything; fall back to the manifest id.
    const std::string folder_name =
        track_dir.extension() == ".dkrmap"
            ? track_dir.filename().string()
            : probe.id + ".dkrmap";
    const std::filesystem::path destination = destination_root / folder_name;
    std::filesystem::create_directories(destination_root, code);
    if (std::filesystem::exists(destination, code)) {
        std::filesystem::remove_all(destination, code);
    }
    std::filesystem::copy(track_dir, destination,
                          std::filesystem::copy_options::recursive, code);
    if (code) {
        discard_install_temp(temp_root);
        error = "Could not copy the track: " + code.message();
        return false;
    }

    if (outcome != nullptr) {
        *outcome = InstallOutcome{};
        outcome->track_id = probe.id;
        outcome->hd_pack_digest = probe.hd_pack_digest;
        if (!probe.hd_pack_file.empty()) {
            // Beside the source on disk, or lifted out of the wrapper zip.
            const std::filesystem::path sibling =
                wrapped_hd_pack.empty()
                    ? source.parent_path() / probe.hd_pack_file
                    : wrapped_hd_pack;
            std::string digest;
            if (std::filesystem::is_regular_file(sibling, code)) {
                if (read_hd_pack_digest(sibling, digest) &&
                    !probe.hd_pack_digest.empty() &&
                    digest == probe.hd_pack_digest) {
                    outcome->hd_pack_archive = sibling;
                    // The caller imports from here, then cleans the temp up.
                    outcome->temp_root = temp_root;
                    temp_root.clear();
                } else {
                    outcome->hd_pack_mismatch = true;
                }
            }
        }
    }

    discard_install_temp(temp_root);
    reload();
    error.clear();
    return true;
}

namespace {

// Resolve both paths before allowing deletion: a symlink/junction must not
// turn a managed-looking path into a deletion in the author's source folder.
bool installed_path_locked(const std::filesystem::path& source) {
    if (g_directory.empty() || source.extension() != ".dkrmap") return false;
    std::error_code code;
    const auto root = std::filesystem::canonical(g_directory, code);
    if (code) return false;
    if (!std::filesystem::equivalent(source.parent_path(), root, code) || code) return false;
    const auto resolved = std::filesystem::canonical(source, code);
    if (code || resolved == root) return false;
    return std::filesystem::equivalent(resolved.parent_path(), root, code) && !code;
}

} // namespace

bool is_installed(const Track& track) {
    std::scoped_lock lock(g_mutex);
    return installed_path_locked(track.source);
}

bool uninstall(const std::string& id, std::string& error) {
    std::scoped_lock lock(g_mutex);
    const auto found = std::find_if(g_tracks.begin(), g_tracks.end(),
        [&id](const Track& track) { return track.id == id; });
    if (found == g_tracks.end()) {
        error = "This track is no longer in the library.";
        return false;
    }
    if (!installed_path_locked(found->source)) {
        error = "This track is read from a working folder. Its source files cannot be uninstalled.";
        return false;
    }
    std::error_code code;
    std::filesystem::remove_all(found->source, code);
    if (code) {
        error = "Could not uninstall the track: " + code.message();
        return false;
    }
    g_tracks.erase(found);
    g_resolved_level_ids.erase(id);
    if (g_armed_track == id) {
        g_armed_track.clear();
        g_auto_boot = false;
        save_state_locked();
    }
    error.clear();
    return true;
}

void reload() {
    std::scoped_lock lock(g_mutex);

    // Once a level table has been built the game may be inside a level whose
    // objects come from the blobs a rescan would rebuild. Defer rather than
    // rebuild underneath it; build_extended_table picks this up at the next
    // level load, which is the only safe moment.
    if (g_sections[section_slot(Section::LevelHeaders)].built) {
        g_rescan_pending = true;
        std::fprintf(stderr,
                     "[custom-tracks] rescan deferred to the next level load\n");
        return;
    }

    // Preserve the author's enable/disable choices across the reload.
    std::vector<std::pair<std::string, bool>> previous;
    for (const Track& track : g_tracks) {
        previous.emplace_back(track.id, track.enabled);
    }
    scan_locked(g_directory);
    for (Track& track : g_tracks) {
        for (const auto& [id, enabled] : previous) {
            if (track.id == id) {
                track.enabled = enabled;
            }
        }
    }
}

} // namespace dkr::runtime::custom_tracks
