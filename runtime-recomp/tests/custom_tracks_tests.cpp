#include "custom_tracks.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <miniz/miniz.h>

using namespace dkr::runtime::custom_tracks;

namespace {

// A retail-shaped table: [o0, o1, o2, oEnd, -1] describes three levels, the
// final entry existing only to size the last one by difference.
constexpr std::int32_t kRetail[] = {0x0, 0x100, 0x250, 0x400, -1};

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_file_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// A zip with the given (name, contents) entries. Names may carry '/'.
void write_zip(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& entries) {
    std::filesystem::create_directories(path.parent_path());
    mz_zip_archive zip{};
    const bool opened =
        mz_zip_writer_init_file(&zip, path.string().c_str(), 0) != MZ_FALSE;
    assert(opened);
    (void) opened;
    for (const auto& [name, contents] : entries) {
        const bool added = mz_zip_writer_add_mem(
            &zip, name.c_str(), contents.data(), contents.size(),
            MZ_BEST_COMPRESSION) != MZ_FALSE;
        assert(added);
        (void) added;
    }
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
}

std::string track_manifest(const std::string& id, const std::string& extra) {
    return "{\"schemaVersion\":1,\"id\":\"" + id + "\",\"name\":\"" + id +
           "\"," + extra +
           "\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}";
}

// A TEXTURES_3D payload as BuildTexture writes one: a TextureHeader and its
// texels per frame, each frame 16-aligned, filled with `fill`.
std::string texture_payload(std::uint8_t width, std::uint8_t height,
                            std::uint8_t format, std::uint8_t render_mode,
                            std::uint8_t frames = 1, char fill = 't') {
    static const std::uint32_t kBits[] = {32, 16, 8, 4, 16, 8, 4, 4, 8};
    const std::size_t texels =
        (static_cast<std::size_t>(width) * height * kBits[format] + 7U) / 8U;
    const std::size_t size = (32U + texels + 15U) & ~std::size_t{15U};
    std::string out;
    for (std::uint8_t frame = 0; frame < frames; ++frame) {
        std::string one(size, fill);
        std::fill(one.begin(), one.begin() + 32, '\0');
        one[0x00] = static_cast<char>(width);
        one[0x01] = static_cast<char>(height);
        one[0x02] = static_cast<char>((render_mode << 4) | format);
        one[0x05] = 1;
        one[0x12] = static_cast<char>(frames);
        one[0x16] = static_cast<char>((size >> 8) & 0xFF);
        one[0x17] = static_cast<char>(size & 0xFF);
        out += one;
    }
    return out;
}

std::vector<std::uint8_t> as_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// A SPRITES payload as BuildSprite writes one: a 12-byte SpriteHeader, the
// frame boundaries, padded to 16.
std::string sprite_payload(std::int32_t base_texture, std::uint16_t frames = 1,
                           std::int16_t anchor_x = 61, std::int16_t anchor_y = 62) {
    std::string out(16U, '\0');
    out[0] = static_cast<char>((base_texture >> 8) & 0xFF);
    out[1] = static_cast<char>(base_texture & 0xFF);
    out[2] = static_cast<char>((frames >> 8) & 0xFF);
    out[3] = static_cast<char>(frames & 0xFF);
    out[4] = static_cast<char>((anchor_x >> 8) & 0xFF);
    out[5] = static_cast<char>(anchor_x & 0xFF);
    out[6] = static_cast<char>((anchor_y >> 8) & 0xFF);
    out[7] = static_cast<char>(anchor_y & 0xFF);
    for (std::uint16_t frame = 0; frame <= frames && 12U + frame < out.size(); ++frame) {
        out[12U + frame] = static_cast<char>(frame);
    }
    return out;
}

// The same model with minimapSpriteIndex (0x20, inside the stored prefix) set.
std::string with_minimap(std::string payload, std::int32_t sprite) {
    const std::size_t at = 10U + 0x20U;
    const auto value = static_cast<std::uint32_t>(sprite);
    payload[at] = static_cast<char>((value >> 24) & 0xFF);
    payload[at + 1U] = static_cast<char>((value >> 16) & 0xFF);
    payload[at + 2U] = static_cast<char>((value >> 8) & 0xFF);
    payload[at + 3U] = static_cast<char>(value & 0xFF);
    return payload;
}

std::int32_t served_minimap_sprite(std::uint32_t offset, std::int32_t size) {
    const std::uint8_t* bytes = payload_for(Section::LevelModels, offset, size);
    assert(bytes != nullptr);
    const std::uint8_t* at = bytes + 10U + 0x20U;
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(at[0]) << 24) |
                                     (static_cast<std::uint32_t>(at[1]) << 16) |
                                     (static_cast<std::uint32_t>(at[2]) << 8) | at[3]);
}

// A header that names retail object maps, so a track needs ship none.
std::string minimal_header() {
    std::string header(200U, '\0');
    header[0x37] = 73;
    header[0xBB] = 5;
    return header;
}

void write_minimap_track(const std::filesystem::path& root, const std::string& id,
                         const std::string& sprite, const std::string& model,
                         bool texture = true) {
    std::filesystem::create_directories(root / "minimap");
    std::string adds =
        "{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"},"
        "{\"section\":\"LEVEL_MODELS\",\"file\":\"m.bin\"}";
    if (texture) {
        adds += ",{\"section\":\"TEXTURES_2D\",\"file\":\"minimap/texture.bin\"}";
        write_file(root / "minimap" / "texture.bin", texture_payload(48, 60, 5, 0));
    }
    if (!sprite.empty()) {
        adds += ",{\"section\":\"SPRITES\",\"file\":\"minimap/sprite.bin\"}";
        write_file(root / "minimap" / "sprite.bin", sprite);
    }
    write_file(root / "manifest.json",
               "{\"schemaVersion\":1,\"id\":\"" + id + "\",\"name\":\"" + id +
                   "\",\"adds\":[" + adds + "]}");
    write_file(root / "h.bin", minimal_header());
    write_file(root / "m.bin", model);
}

void write_track(const std::filesystem::path& root, const std::string& id,
                 std::size_t payload_size) {
    std::filesystem::create_directories(root);
    write_file(root / "manifest.json",
               "{\"schemaVersion\":1,\"id\":\"" + id + "\",\"name\":\"" + id +
                   "\",\"adds\":[{\"section\":\"LEVEL_HEADERS\","
                   "\"file\":\"h.bin\"}]}");
    write_file(root / "h.bin", std::string(payload_size, 'x'));
}

// A level model payload shaped the way the addon writes one: a five-byte
// container, a stored DEFLATE block holding the header and texture table, and
// then whatever would be the compressed remainder. Nothing here inflates it, so
// the tail is filler - what is under test is that the ids in the stored prefix
// are found and rewritten.
std::string model_payload(const std::vector<std::int32_t>& texture_ids,
                          std::uint32_t table_at = 0x4C) {
    const std::uint32_t count =
        static_cast<std::uint32_t>(texture_ids.size());
    const std::uint32_t prefix = table_at + count * 8U;

    std::string model(prefix, '\0');
    // LevelModel: textures at 0x00 (big endian), numberOfTextures at 0x18.
    model[0] = static_cast<char>((table_at >> 24) & 0xFF);
    model[1] = static_cast<char>((table_at >> 16) & 0xFF);
    model[2] = static_cast<char>((table_at >> 8) & 0xFF);
    model[3] = static_cast<char>(table_at & 0xFF);
    model[0x18] = static_cast<char>((count >> 8) & 0xFF);
    model[0x19] = static_cast<char>(count & 0xFF);
    for (std::uint32_t entry = 0; entry < count; ++entry) {
        const std::uint32_t at = table_at + entry * 8U;
        const auto id = static_cast<std::uint32_t>(texture_ids[entry]);
        model[at + 0] = static_cast<char>((id >> 24) & 0xFF);
        model[at + 1] = static_cast<char>((id >> 16) & 0xFF);
        model[at + 2] = static_cast<char>((id >> 8) & 0xFF);
        model[at + 3] = static_cast<char>(id & 0xFF);
        model[at + 4] = 64;   // width
        model[at + 5] = 32;   // height
        model[at + 6] = 0x11; // OPAQUE | RGBA16
        model[at + 7] = 0;    // surfaceType
    }

    const std::string tail(64U, 'z');
    const std::uint32_t uncompressed =
        static_cast<std::uint32_t>(model.size() + tail.size());

    std::string payload;
    payload += static_cast<char>(uncompressed & 0xFF);
    payload += static_cast<char>((uncompressed >> 8) & 0xFF);
    payload += static_cast<char>((uncompressed >> 16) & 0xFF);
    payload += static_cast<char>((uncompressed >> 24) & 0xFF);
    payload += static_cast<char>(0x09);            // container tag
    payload += static_cast<char>(0x00);            // BFINAL 0, BTYPE 00
    payload += static_cast<char>(prefix & 0xFF);   // LEN, little endian
    payload += static_cast<char>((prefix >> 8) & 0xFF);
    const std::uint32_t nlen = (~prefix) & 0xFFFF;
    payload += static_cast<char>(nlen & 0xFF);     // NLEN
    payload += static_cast<char>((nlen >> 8) & 0xFF);
    payload += model;
    payload += tail;
    return payload;
}

// A whole level model with the given batch count in each segment, compressed
// with Huffman-coded DEFLATE the way the retail assets are.
std::string batch_model_payload(const std::vector<std::int16_t>& batches,
                                std::uint8_t tag = 0x09) {
    const std::uint32_t segments = 0x4C;
    std::string model(segments + batches.size() * 0x44U + 32U, '\0');
    model[4] = static_cast<char>((segments >> 24) & 0xFF);
    model[5] = static_cast<char>((segments >> 16) & 0xFF);
    model[6] = static_cast<char>((segments >> 8) & 0xFF);
    model[7] = static_cast<char>(segments & 0xFF);
    model[0x1A] = static_cast<char>((batches.size() >> 8) & 0xFF);
    model[0x1B] = static_cast<char>(batches.size() & 0xFF);
    for (std::size_t segment = 0; segment < batches.size(); ++segment) {
        const std::size_t at = segments + segment * 0x44U + 0x20U;
        model[at] = static_cast<char>((batches[segment] >> 8) & 0xFF);
        model[at + 1] = static_cast<char>(batches[segment] & 0xFF);
    }
    std::size_t compressed_size = 0;
    void* compressed = tdefl_compress_mem_to_heap(model.data(), model.size(),
                                                  &compressed_size, TDEFL_DEFAULT_MAX_PROBES);
    assert(compressed != nullptr);
    const auto size = static_cast<std::uint32_t>(model.size());
    std::string payload;
    for (int byte = 0; byte < 4; ++byte) {
        payload += static_cast<char>((size >> (byte * 8)) & 0xFF);
    }
    payload += static_cast<char>(tag);
    payload.append(static_cast<const char*>(compressed), compressed_size);
    mz_free(compressed);
    return payload;
}

std::int32_t count_batches(const std::string& payload) {
    return count_level_model_batches(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
}

// The texture ids a served model payload now names.
std::vector<std::int32_t> served_texture_ids(Section section,
                                            std::uint32_t offset,
                                            std::int32_t size) {
    const std::uint8_t* bytes = payload_for(section, offset, size);
    std::vector<std::int32_t> found;
    if (bytes == nullptr) {
        return found;
    }
    const std::uint8_t* model = bytes + 10;   // STORED_PREFIX_AT
    const std::uint32_t table = (static_cast<std::uint32_t>(model[0]) << 24) |
                               (static_cast<std::uint32_t>(model[1]) << 16) |
                               (static_cast<std::uint32_t>(model[2]) << 8) |
                               model[3];
    const std::uint32_t count =
        (static_cast<std::uint32_t>(model[0x18]) << 8) | model[0x19];
    for (std::uint32_t entry = 0; entry < count; ++entry) {
        const std::uint8_t* at = model + table + entry * 8U;
        found.push_back(static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(at[0]) << 24) |
            (static_cast<std::uint32_t>(at[1]) << 16) |
            (static_cast<std::uint32_t>(at[2]) << 8) | at[3]));
    }
    return found;
}

// Reproduces the retail counting loop from level_global_init verbatim.
int level_count(const std::vector<std::int32_t>& table) {
    int count = 0;
    while (table[count] != -1) {
        ++count;
    }
    return count - 1;
}

} // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "dkrr-custom-tracks-tests";
    std::filesystem::remove_all(root);
    write_track(root / "beta.dkrmap", "beta", 192U);
    write_track(root / "alpha.dkrmap", "alpha", 128U);

    scan(root);
    assert(tracks().size() == 2U);
    assert(enabled_count() == 2U);

    std::vector<std::int32_t> table =
        build_extended_table(Section::LevelHeaders, kRetail);
    assert(!table.empty());
    assert(level_count(table) == 5);

    // Every retail entry keeps its authored offset AND size. The third one is
    // the load-bearing case: its size still derives from the retail end offset
    // that the first custom entry now shares.
    assert(table[0] == 0x0 && table[1] - table[0] == 0x100);
    assert(table[1] == 0x100 && table[2] - table[1] == 0x150);
    assert(table[2] == 0x250 && table[3] - table[2] == 0x1B0);

    // Custom entries continue from the retail end offset by exact sizes.
    assert(table[3] == 0x400 && table[4] - table[3] == 128);
    assert(table[4] == 0x480 && table[5] - table[4] == 192);
    assert(resolved_level_id("alpha") == 3);
    assert(resolved_level_id("beta") == 4);
    // A legacy mod session asks this at every scene load: an added level is
    // never one of its retail carriers.
    assert(owns_level_id(3) && owns_level_id(4));
    assert(!owns_level_id(0) && !owns_level_id(2) && !owns_level_id(5));
    assert(!owns_level_id(kNoTrackOverride));

    // Retail offsets belong to the ROM; custom offsets resolve to a payload.
    assert(payload_for(Section::LevelHeaders, 0x250, 0x1B0) == nullptr);
    assert(payload_for(Section::LevelHeaders, 0x400, 128) != nullptr);
    assert(payload_for(Section::LevelHeaders, 0x480, 192) != nullptr);
    // A read that runs past the payload is refused rather than served short.
    assert(payload_for(Section::LevelHeaders, 0x480, 1024) == nullptr);

    // Disabling a track must not leave a stale level id behind: the remaining
    // track takes the freed id, and the disabled one reports none.
    set_enabled("alpha", false);
    table = build_extended_table(Section::LevelHeaders, kRetail);
    assert(level_count(table) == 4);
    assert(resolved_level_id("alpha") == -1);
    assert(resolved_level_id("beta") == 3);
    assert(owns_level_id(3) && !owns_level_id(4));
    assert(table[3] == 0x400 && table[4] - table[3] == 192);

    // tracks() must hand back a snapshot, because a reload replaces the
    // backing vector while the UI thread is still reading it.
    const std::vector<Track> snapshot = tracks();
    reload();
    assert(snapshot.size() == 2U);
    bool disable_survived = false;
    for (const Track& track : tracks()) {
        if (track.id == "alpha" && !track.enabled) {
            disable_survived = true;
        }
    }
    assert(disable_survived);

    // A section with nothing added publishes an empty table, telling the
    // caller to keep the retail one untouched.
    assert(build_extended_table(Section::LevelModels, kRetail).empty());

    // A track that ships a header and an object map must have its header
    // pointed at its own map, and the two sections have different retail
    // counts, so the indices genuinely differ.
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path pair = root / "paired.dkrmap";
        std::filesystem::create_directories(pair);
        write_file(pair / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"paired\",\"name\":\"Paired\","
                   "\"adds\":[{\"section\":\"LEVEL_HEADERS\","
                   "\"file\":\"h.bin\"},"
                   "{\"section\":\"LEVEL_OBJECT_MAPS\",\"slot\":\"structure\","
                   "\"file\":\"s.bin\"},"
                   "{\"section\":\"LEVEL_OBJECT_MAPS\",\"slot\":\"collectables\","
                   "\"file\":\"o.bin\"}]}");
        write_file(pair / "h.bin", std::string(200U, 'h'));
        write_file(pair / "s.bin", std::string(48U, 's'));
        write_file(pair / "o.bin", std::string(64U, 'o'));
    }
    scan(root);
    assert(tracks().size() == 1U);

    // Headers: 3 retail entries. Object maps: a different table, 2 entries.
    constexpr std::int32_t kMaps[] = {0x0, 0x80, 0x100, -1};
    const std::vector<std::int32_t> headers =
        build_extended_table(Section::LevelHeaders, kRetail);
    const std::vector<std::int32_t> maps =
        build_extended_table(Section::LevelObjectMaps, kMaps);
    assert(level_count(headers) == 4);   // 3 retail + 1
    assert(level_count(maps) == 4);      // 2 retail + 2 slots

    assert(resolved_level_id("paired") == 3);

    // The two slots must resolve to DIFFERENT indices, and neither to the
    // level id: pointing both header fields at one map would spawn the same
    // objects twice and drop the other layer entirely.
    const std::int32_t structure = sibling_index(
        Section::LevelHeaders, 0x400, Section::LevelObjectMaps,
        MapSlot::Structure);
    const std::int32_t collectables = sibling_index(
        Section::LevelHeaders, 0x400, Section::LevelObjectMaps,
        MapSlot::Collectables);
    assert(structure == 2 && collectables == 3);

    // A retail offset has no sibling, and neither does an unrelated section.
    assert(sibling_index(Section::LevelHeaders, 0x100,
                         Section::LevelObjectMaps, MapSlot::Collectables) == -1);
    assert(sibling_index(Section::LevelHeaders, 0x400,
                         Section::LevelModels, MapSlot::None) == -1);

    // A header with only one of the two maps leaves the other field at 0,
    // which is object map 0 rather than "none" - the game then spawns another
    // level's layer and hangs. Such a package must be refused, not loaded.
    {
        const std::filesystem::path half = root / "halfmaps.dkrmap";
        std::filesystem::create_directories(half);
        write_file(half / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"halfmaps\",\"name\":\"Half\","
                   "\"adds\":[{\"section\":\"LEVEL_HEADERS\","
                   "\"file\":\"h.bin\"},"
                   "{\"section\":\"LEVEL_OBJECT_MAPS\",\"slot\":\"structure\","
                   "\"file\":\"s.bin\"}]}");
        // Zero-filled, which is what the exporter writes into 0x36 and 0xBA
        // because the real indices are assigned at load.
        write_file(half / "h.bin", std::string(200U, '\0'));
        write_file(half / "s.bin", std::string(48U, 's'));
    }
    scan(root);
    for (const Track& track : tracks()) {
        assert(track.id != "halfmaps");
    }
    assert(tracks().size() == 1U);

    // The same header is fine once both maps are there, and equally fine with
    // neither if it names real retail indices instead of zero - a track built
    // on an existing level's objects needs no payload at all.
    {
        const std::filesystem::path retail_maps = root / "retailmaps.dkrmap";
        std::filesystem::create_directories(retail_maps);
        write_file(retail_maps / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"retailmaps\","
                   "\"name\":\"Retail maps\",\"adds\":["
                   "{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
        std::string header(200U, '\0');
        header[0x36] = 0; header[0x37] = 73;   // Ancient Lake's collectables
        header[0xBA] = 0; header[0xBB] = 5;    // and its structure map
        write_file(retail_maps / "h.bin", header);
    }
    scan(root);
    bool retail_loaded = false;
    for (const Track& track : tracks()) {
        if (track.id == "retailmaps") {
            retail_loaded = true;
        }
    }
    assert(retail_loaded);

    // Two tracks with one id break every cross-reference that keys on it, and
    // importing a copy of a track that is also being watched produces exactly
    // that pair. The second must be refused, not silently shadow the first.
    {
        const std::filesystem::path twin = root / "twin.dkrmap";
        std::filesystem::create_directories(twin);
        write_file(twin / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"retailmaps\","
                   "\"name\":\"Twin\",\"adds\":["
                   "{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
        std::string header(200U, '\0');
        header[0x37] = 73;
        header[0xBB] = 5;
        write_file(twin / "h.bin", header);
    }
    scan(root);
    int with_that_id = 0;
    for (const Track& track : tracks()) {
        if (track.id == "retailmaps") {
            ++with_that_id;
        }
    }
    assert(with_that_id == 1);

    // An object map entry without a slot is ambiguous and must be refused
    // outright: merging the two maps is exactly the failure this guards.
    {
        const std::filesystem::path bad = root / "noslot.dkrmap";
        std::filesystem::create_directories(bad);
        write_file(bad / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"noslot\",\"name\":\"No slot\","
                   "\"adds\":[{\"section\":\"LEVEL_OBJECT_MAPS\","
                   "\"file\":\"o.bin\"}]}");
        write_file(bad / "o.bin", std::string(32U, 'o'));
    }
    scan(root);
    for (const Track& track : tracks()) {
        assert(track.id != "noslot");
    }

    // ------------------------------------------------------------------
    // A track that ships artwork of its own
    // ------------------------------------------------------------------
    //
    // Textures are the one section a track contributes MANY entries to, so
    // they are told apart by position in the manifest and nothing else. Three
    // things have to hold and none of them is visible from the level sections
    // above: the table grows the way the *texture* loader counts it, each
    // texture's position earns it an index, and the model that draws them is
    // rewritten to name those indices instead of the placeholders the exporter
    // could not know the answer to.
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path art = root / "art.dkrmap";
        std::filesystem::create_directories(art / "textures");
        write_file(art / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"art\",\"name\":\"Art\","
                   "\"adds\":[{\"section\":\"LEVEL_HEADERS\","
                   "\"file\":\"h.bin\"},"
                   "{\"section\":\"LEVEL_MODELS\",\"file\":\"m.bin\"},"
                   "{\"section\":\"TEXTURES_3D\",\"file\":\"textures/0.bin\"},"
                   "{\"section\":\"TEXTURES_3D\",\"file\":\"textures/1.bin\"}]}");
        std::string header(200U, '\0');
        header[0x37] = 73;
        header[0xBB] = 5;
        write_file(art / "h.bin", header);
        // Two of its own textures and one of the ROM's, so the substitution has
        // to leave the retail id alone.
        write_file(art / "m.bin",
                   model_payload({kCustomTextureIdBase, 1234,
                                  kCustomTextureIdBase + 1}));
        // 64x32 RGBA16 and 32x32 RGBA16, header included, both 16-aligned.
        // The second is see-through (render mode TRANSPARENT).
        write_file(art / "textures" / "0.bin",
                   texture_payload(64, 32, 1, 1, 1, 'a'));
        write_file(art / "textures" / "1.bin",
                   texture_payload(32, 32, 1, 0, 1, 'b'));
    }
    scan(root);
    assert(tracks().size() == 1U);
    assert(tracks().front().textures.size() == 2U);
    assert(!tracks().front().textures[0].translucent);
    assert(tracks().front().textures[1].translucent);
    {
        const ArtworkSummary art = artwork("art");
        assert(art.textures == 2U && art.translucent == 1U && art.animated == 0U);
        assert(artwork("nobody").textures == 0U);
    }

    // tex_init_textures counts exactly as level_global_init does, so the same
    // helper describes both: [o0 .. o(n-1), oEnd, -1] is n textures.
    constexpr std::int32_t kTextures[] = {0x0, 0x800, 0x1000, 0x1800, -1};
    const std::vector<std::int32_t> texture_table =
        build_extended_table(Section::Textures3D, kTextures);
    assert(level_count(texture_table) == 5);   // 3 retail + 2 shipped
    assert(texture_table[3] == 0x1800);
    assert(texture_table[4] - texture_table[3] == 4128);
    assert(texture_table[5] - texture_table[4] == 2080);

    // And they serve as payloads, which is what makes them loadable at all.
    assert(payload_for(Section::Textures3D, 0x1800, 4128) != nullptr);
    assert(payload_for(Section::Textures3D, 0x1000, 0x800) == nullptr);
    // Past the whole blob is refused; past one entry into the next is not,
    // because the bound is the concatenated payloads rather than each entry.
    // That is deliberate and it is what makes the peek below work:
    // load_texture reads sizeof(TempTexHeader) bytes before it knows how large
    // the texture is, so the LAST texture is asked for more than it has.
    assert(payload_for(Section::Textures3D, 0x1800, 4128 + 2080 + 1) == nullptr);
    assert(payload_for(Section::Textures3D, 0x1800,
                       static_cast<std::int32_t>(kMinimumTexturePayload)) !=
           nullptr);
    assert(payload_for(Section::Textures3D, 0x1800 + 4128,
                       static_cast<std::int32_t>(kMinimumTexturePayload)) !=
           nullptr);

    // Now the part the level sections cannot show. The model was written with
    // placeholders, because the index of a shipped texture is the ROM's retail
    // count plus an ordinal and the exporter cannot know the count. Serving it
    // has to substitute the real indices - and leave the retail id untouched.
    const std::string art_model = model_payload(
        {kCustomTextureIdBase, 1234, kCustomTextureIdBase + 1});
    const std::vector<std::int32_t> art_models =
        build_extended_table(Section::LevelModels, kRetail);
    assert(level_count(art_models) == 4);
    assert(art_models[3] == 0x400);

    std::vector<std::int32_t> ids = served_texture_ids(
        Section::LevelModels, 0x400,
        static_cast<std::int32_t>(art_model.size()));
    assert(ids.size() == 3U);
    assert(ids[0] == 3);      // its first texture, at retail count + 0
    assert(ids[1] == 1234);   // one of the ROM's, left exactly as authored
    assert(ids[2] == 4);      // its second

    // Rebuilding must give the same answer rather than substituting into an
    // already-substituted copy: the source of truth is the file, not the blob.
    build_extended_table(Section::LevelModels, kRetail);
    ids = served_texture_ids(Section::LevelModels, 0x400,
                             static_cast<std::int32_t>(art_model.size()));
    assert(ids.size() == 3U && ids[0] == 3 && ids[1] == 1234 && ids[2] == 4);

    // A model naming a texture its track does not ship must not be left
    // pointing past the end of the published table: load_texture range-checks
    // such an index and then indexes with it anyway. Texture 0 is wrong and
    // visible, which is the better of the two.
    {
        const std::filesystem::path greedy = root / "greedy.dkrmap";
        std::filesystem::create_directories(greedy / "textures");
        write_file(greedy / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"greedy\",\"name\":\"Greedy\","
                   "\"adds\":[{\"section\":\"LEVEL_MODELS\","
                   "\"file\":\"m.bin\"},"
                   "{\"section\":\"TEXTURES_3D\",\"file\":\"textures/0.bin\"}]}");
        write_file(greedy / "m.bin",
                   model_payload({kCustomTextureIdBase,
                                  kCustomTextureIdBase + 7}));
        write_file(greedy / "textures" / "0.bin",
                   texture_payload(32, 16, 1, 1, 1, 'c'));
    }
    std::filesystem::remove_all(root / "art.dkrmap");
    scan(root);
    assert(tracks().size() == 1U);
    build_extended_table(Section::Textures3D, kTextures);
    const std::vector<std::int32_t> greedy_models =
        build_extended_table(Section::LevelModels, kRetail);
    const std::string greedy_model =
        model_payload({kCustomTextureIdBase, kCustomTextureIdBase + 7});
    ids = served_texture_ids(Section::LevelModels, 0x400,
                            static_cast<std::int32_t>(greedy_model.size()));
    assert(ids.size() == 2U);
    assert(ids[0] == 3);
    assert(ids[1] == 0);   // reset, not left as a placeholder
    (void) greedy_models;

    // Two tracks: the second one's first texture is NOT the first custom
    // index, and each model resolves only its own. Getting this wrong is the
    // failure that shows up as one track drawing another's pictures.
    {
        const std::filesystem::path more = root / "more.dkrmap";
        std::filesystem::create_directories(more / "textures");
        write_file(more / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"more\",\"name\":\"More\","
                   "\"adds\":[{\"section\":\"LEVEL_MODELS\","
                   "\"file\":\"m.bin\"},"
                   "{\"section\":\"TEXTURES_3D\",\"file\":\"textures/0.bin\"}]}");
        write_file(more / "m.bin", model_payload({kCustomTextureIdBase}));
        write_file(more / "textures" / "0.bin",
                   texture_payload(32, 16, 1, 1, 1, 'd'));
    }
    scan(root);
    assert(tracks().size() == 2U);
    const std::vector<std::int32_t> two_textures =
        build_extended_table(Section::Textures3D, kTextures);
    assert(level_count(two_textures) == 5);   // 3 retail + 1 each
    const std::vector<std::int32_t> two_models =
        build_extended_table(Section::LevelModels, kRetail);
    assert(level_count(two_models) == 5);

    const std::string one_texture_model = model_payload({kCustomTextureIdBase});
    const std::vector<std::int32_t> first = served_texture_ids(
        Section::LevelModels, static_cast<std::uint32_t>(two_models[3]),
        static_cast<std::int32_t>(greedy_model.size()));
    const std::vector<std::int32_t> second = served_texture_ids(
        Section::LevelModels, static_cast<std::uint32_t>(two_models[4]),
        static_cast<std::int32_t>(one_texture_model.size()));
    // Scan order is directory order, so which track is served first is not
    // fixed. What must hold either way is that the two do not share an index.
    assert(!first.empty() && !second.empty());
    assert(first[0] == 3 || first[0] == 4);
    assert(second[0] == 3 || second[0] == 4);
    assert(first[0] != second[0]);

    // A texture payload shorter than load_texture's peek is refused, and so is
    // one that is not 16-aligned: the display list the loader builds sits at
    // align16(tex + size) inside an allocation of exactly that size plus the
    // lists, so an unaligned payload pushes the last one out of its block.
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path tiny = root / "tiny.dkrmap";
        std::filesystem::create_directories(tiny / "textures");
        write_file(tiny / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"tiny\",\"name\":\"Tiny\","
                   "\"adds\":[{\"section\":\"TEXTURES_3D\","
                   "\"file\":\"textures/0.bin\"}]}");
        write_file(tiny / "textures" / "0.bin", std::string(16U, 't'));

        const std::filesystem::path odd = root / "odd.dkrmap";
        std::filesystem::create_directories(odd / "textures");
        write_file(odd / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"odd\",\"name\":\"Odd\","
                   "\"adds\":[{\"section\":\"TEXTURES_3D\","
                   "\"file\":\"textures/0.bin\"}]}");
        write_file(odd / "textures" / "0.bin", std::string(100U, 'o'));

        const std::filesystem::path good = root / "good.dkrmap";
        std::filesystem::create_directories(good / "textures");
        write_file(good / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"good\",\"name\":\"Good\","
                   "\"adds\":[{\"section\":\"TEXTURES_3D\","
                   "\"file\":\"textures/0.bin\"}]}");
        write_file(good / "textures" / "0.bin", texture_payload(4, 2, 1, 1));

        // A header the loader would misread: a palette the track cannot
        // supply, and a render mode material_init has never heard of.
        const std::filesystem::path palette = root / "palette.dkrmap";
        std::filesystem::create_directories(palette / "textures");
        write_file(palette / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"palette\",\"name\":\"P\","
                   "\"adds\":[{\"section\":\"TEXTURES_3D\","
                   "\"file\":\"textures/0.bin\"}]}");
        write_file(palette / "textures" / "0.bin", texture_payload(16, 16, 7, 1));
    }
    scan(root);
    for (const Track& track : tracks()) {
        assert(track.id != "tiny");
        assert(track.id != "odd");
        assert(track.id != "palette");
    }
    assert(tracks().size() == 1U);
    assert(tracks().front().id == "good");

    // ------------------------------------------------------------------
    // What a texture's header says about how it is drawn
    // ------------------------------------------------------------------
    //
    // material_init decides see-through from the render mode for RGBA and CI4,
    // always for IA, never for I; render_level_segment then draws a
    // see-through batch only in its second pass. The exporter places batches
    // by that rule, so the runtime has to read it the same way.
    {
        TextureInfo info;
        std::string why;
        assert(inspect_texture_payload(as_bytes(texture_payload(32, 32, 0, 0)),
                                       info, why));
        assert(info.width == 32 && info.height == 32 && info.format == 0U);
        assert(info.render_mode == 0U && info.frames == 1U && info.translucent);
        assert(inspect_texture_payload(as_bytes(texture_payload(16, 16, 1, 2)),
                                       info, why) && info.translucent);
        assert(inspect_texture_payload(as_bytes(texture_payload(16, 16, 1, 3)),
                                       info, why) && !info.translucent);
        assert(inspect_texture_payload(as_bytes(texture_payload(16, 16, 5, 1)),
                                       info, why) && info.translucent);
        assert(inspect_texture_payload(as_bytes(texture_payload(16, 16, 2, 0)),
                                       info, why) && !info.translucent);
        assert(texture_translucent(4U, 3U) && texture_translucent(6U, 1U));
        assert(!texture_translucent(3U, 0U) && !texture_translucent(8U, 0U));

        // An animated one: frames end to end, each walked by its own size.
        assert(inspect_texture_payload(
            as_bytes(texture_payload(16, 16, 0, 0, 3)), info, why));
        assert(info.frames == 3U);

        // A frame count the payload does not hold.
        std::string short_animation = texture_payload(16, 16, 0, 0, 3);
        short_animation.resize(short_animation.size() * 2U / 3U);
        assert(!inspect_texture_payload(as_bytes(short_animation), info, why));
        assert(why.find("frame 3") != std::string::npos);

        std::string no_frames = texture_payload(16, 16, 1, 1);
        no_frames[0x12] = 0;
        assert(!inspect_texture_payload(as_bytes(no_frames), info, why));

        std::string bad_mode = texture_payload(16, 16, 1, 1);
        bad_mode[0x02] = static_cast<char>((6 << 4) | 1);
        assert(!inspect_texture_payload(as_bytes(bad_mode), info, why));
        assert(why.find("render mode") != std::string::npos);

        std::string small = texture_payload(16, 16, 1, 1);
        small[0x16] = 0;
        small[0x17] = 64;
        assert(!inspect_texture_payload(as_bytes(small), info, why));

        std::string empty = texture_payload(16, 16, 1, 1);
        empty[0x00] = 0;
        assert(!inspect_texture_payload(as_bytes(empty), info, why));

        // A compressed payload is packed past its first header, so only that
        // header is read.
        std::string packed = texture_payload(16, 16, 0, 0);
        packed[0x1D] = 1;
        packed[0x16] = 0;
        packed[0x17] = 0;
        assert(inspect_texture_payload(as_bytes(packed), info, why));
        assert(info.compressed && info.translucent);
    }

    // ------------------------------------------------------------------
    // A track's high-resolution texture pack
    // ------------------------------------------------------------------
    //
    // manifest.hdTexturePack names a <track>-hd.zip the exporter wrote beside
    // the .dkrmap and the digest both files carry. The runtime reads the name
    // and resolves the sibling; equal digests are what say the pack belongs to
    // this export. Nothing here imports it - that is the UI layer's job.
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path dir = root / "hd.dkrmap";
        std::filesystem::create_directories(dir);
        write_file(dir / "manifest.json",
                   track_manifest("hd",
                                  "\"hdTexturePack\":{\"file\":\"hd-hd.zip\","
                                  "\"textureDigest\":\"abc123\"},"));
        write_file(dir / "h.bin", std::string(80U, 'h'));
        write_zip(root / "hd-hd.zip",
                  {{"dkr-r-track.json", "{\"textureDigest\":\"abc123\"}"},
                   {"Diddy Kong Racing#0badf00d#0#2_all.png", "not a real png"}});
    }
    scan(root);
    {
        const HdPack pack = hd_pack("hd");
        assert(pack.file == "hd-hd.zip");
        assert(pack.digest == "abc123");
        assert(!pack.sibling_archive.empty());
        assert(!pack.sibling_mismatch);
    }

    // A pack from a different export - digest does not match - is flagged, not
    // resolved: the track still plays, in 64x32.
    write_zip(root / "hd-hd.zip",
              {{"dkr-r-track.json", "{\"textureDigest\":\"deadbeef\"}"}});
    scan(root);
    {
        const HdPack pack = hd_pack("hd");
        assert(pack.sibling_archive.empty());
        assert(pack.sibling_mismatch);
    }

    // Sibling absent entirely: the pack stays declared but there is nothing to
    // resolve and nothing is wrong - no status line at all.
    std::filesystem::remove(root / "hd-hd.zip");
    scan(root);
    {
        const HdPack pack = hd_pack("hd");
        assert(pack.file == "hd-hd.zip");
        assert(pack.sibling_archive.empty());
        assert(!pack.sibling_mismatch);
    }

    // A track that declares no pack reports nothing.
    assert(hd_pack("good").file.empty());

    // ------------------------------------------------------------------
    // Installing from a .zip
    // ------------------------------------------------------------------
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "install");

    // A bare .zip of a .dkrmap layout.
    write_zip(root / "bare.zip",
              {{"manifest.json", track_manifest("bare", "")},
               {"h.bin", std::string(72U, 'h')}});
    scan(root / "install");
    {
        std::string install_error;
        assert(install(root / "bare.zip", install_error));
        // install() copies synchronously; a rescan while a level table is
        // already built is deferred to the next build (as in game).
        assert(std::filesystem::is_regular_file(
            root / "install" / "bare.dkrmap" / "manifest.json"));
        build_extended_table(Section::LevelHeaders, kRetail);
        bool bare_installed = false;
        for (const Track& track : tracks()) {
            bare_installed = bare_installed || track.id == "bare";
        }
        assert(bare_installed);
    }

    // A .zip that wraps the .dkrmap AND its <track>-hd.zip. install() reports
    // the pack for the caller to import, then hands back the temp to clean.
    write_zip(root / "remix-hd.zip",
              {{"dkr-r-track.json", "{\"textureDigest\":\"c0ffee\"}"},
               {"Diddy Kong Racing#0badf00d#0#2_all.png", "not a real png"}});
    const std::string remix_hd_bytes = read_file_text(root / "remix-hd.zip");
    write_zip(root / "wrapper.zip",
              {{"remix.dkrmap/manifest.json",
                track_manifest("remix",
                               "\"hdTexturePack\":{\"file\":\"remix-hd.zip\","
                               "\"textureDigest\":\"c0ffee\"},")},
               {"remix.dkrmap/h.bin", std::string(64U, 'h')},
               {"remix-hd.zip", remix_hd_bytes}});
    {
        std::string install_error;
        InstallOutcome outcome;
        assert(install(root / "wrapper.zip", install_error, &outcome));
        assert(outcome.track_id == "remix");
        assert(outcome.hd_pack_digest == "c0ffee");
        assert(!outcome.hd_pack_mismatch);
        assert(!outcome.hd_pack_archive.empty());
        assert(std::filesystem::is_regular_file(outcome.hd_pack_archive));
        assert(!outcome.temp_root.empty());
        // The .dkrmap is installed; the pack is NOT copied into custom-tracks/.
        assert(std::filesystem::is_regular_file(
            root / "install" / "remix.dkrmap" / "manifest.json"));
        assert(!std::filesystem::exists(
            root / "install" / "remix-hd.zip"));
        build_extended_table(Section::LevelHeaders, kRetail);
        bool remix_installed = false;
        for (const Track& track : tracks()) {
            remix_installed = remix_installed || track.id == "remix";
        }
        assert(remix_installed);
        // The caller imports the pack, then discards the temp.
        discard_install_temp(outcome.temp_root);
        assert(!std::filesystem::exists(outcome.temp_root));
    }

    // ------------------------------------------------------------------
    // track_textures_published
    // ------------------------------------------------------------------
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path art = root / "pub.dkrmap";
        std::filesystem::create_directories(art / "textures");
        write_file(art / "manifest.json",
                   "{\"schemaVersion\":1,\"id\":\"pub\",\"name\":\"Pub\","
                   "\"adds\":[{\"section\":\"LEVEL_MODELS\",\"file\":\"m.bin\"},"
                   "{\"section\":\"TEXTURES_3D\",\"file\":\"textures/0.bin\"}]}");
        write_file(art / "m.bin", model_payload({kCustomTextureIdBase}));
        write_file(art / "textures" / "0.bin",
                   texture_payload(32, 16, 1, 1, 1, 'a'));

        const std::filesystem::path plain = root / "plain.dkrmap";
        std::filesystem::create_directories(plain);
        write_file(plain / "manifest.json",
                   track_manifest("plain", ""));
        std::string header(200U, '\0');
        header[0x37] = 73;
        header[0xBB] = 5;
        write_file(plain / "h.bin", header);
    }
    scan(root);
    assert(tracks().size() == 2U);
    // Nothing is published until the once-per-boot table is built.
    assert(!track_textures_published("pub"));
    build_extended_table(Section::Textures3D, kTextures);
    assert(track_textures_published("pub"));     // shipped a texture, table has it
    assert(!track_textures_published("plain"));  // ships none of its own

    // ------------------------------------------------------------------
    // The armed track and auto-boot setting persist across a relaunch
    // ------------------------------------------------------------------
    std::filesystem::remove_all(root);
    {
        const std::filesystem::path dir = root / "persist" / "p.dkrmap";
        std::filesystem::create_directories(dir);
        write_file(dir / "manifest.json", track_manifest("p", ""));
        write_file(dir / "h.bin", std::string(64U, 'h'));
    }
    arm_track_override(std::string{});
    set_auto_boot(false);
    scan(root / "persist");
    assert(tracks().size() == 1U);

    // Arming and enabling auto-boot writes the sidecar.
    arm_track_override("p");
    set_auto_boot(true);
    {
        const std::string state =
            read_file_text(root / "custom-tracks-state.txt");
        assert(state.find("armed=p") != std::string::npos);
        assert(state.find("auto_boot=1") != std::string::npos);
    }

    // Clear the in-memory state, then prove a scan reloads it from disk - which
    // is what the relaunch after "Restart & play in HD" relies on.
    arm_track_override(std::string{});
    set_auto_boot(false);
    assert(armed_track_id().empty());
    assert(!auto_boot_enabled());
    write_file(root / "custom-tracks-state.txt", "armed=p\nauto_boot=1\n");
    scan(root / "persist");
    assert(armed_track_id() == "p");
    assert(auto_boot_enabled());

    // consume_auto_boot is the per-launch one-shot; the setting stays on.
    assert(consume_auto_boot());
    assert(!consume_auto_boot());
    assert(auto_boot_enabled());

    // Turning both off removes the sidecar.
    set_auto_boot(false);
    arm_track_override(std::string{});
    assert(!std::filesystem::exists(root / "custom-tracks-state.txt"));

    std::filesystem::remove_all(root);
    // Uninstall only the managed copy; keep source tracks, other installed
    // tracks, saves and separately managed HD packs. Clear persisted testing.
    const auto installed = root / "installed";
    const auto working = root / "working";
    for (const auto& [folder, id] : std::vector<std::pair<std::filesystem::path, std::string>>{
             {installed / "remove.dkrmap", "remove"},
             {installed / "keep.dkrmap", "keep"},
             {working / "source.dkrmap", "source"}}) {
        std::filesystem::create_directories(folder);
        write_file(folder / "manifest.json", track_manifest(id, ""));
        write_file(folder / "h.bin", std::string(64U, 'h'));
    }
    write_file(root / "save.bin", "save data");
    write_file(root / "track-hd.zip", "managed HD pack");
    scan(installed);
    set_working_directory(working);
    scan(installed);
    assert(tracks().size() == 3U);
    for (const auto& track : tracks()) assert(is_installed(track) == (track.id != "source"));
    arm_track_override("remove");
    set_auto_boot(true);
    std::string uninstall_error;
    assert(!uninstall("missing", uninstall_error));
    assert(!uninstall_error.empty());
    assert(!uninstall("source", uninstall_error));
    assert(std::filesystem::exists(working / "source.dkrmap" / "manifest.json"));
    assert(armed_track_id() == "remove");
    assert(uninstall("remove", uninstall_error));
    assert(uninstall_error.empty());
    assert(!std::filesystem::exists(installed / "remove.dkrmap"));
    assert(std::filesystem::exists(installed / "keep.dkrmap" / "manifest.json"));
    assert(read_file_text(root / "save.bin") == "save data");
    assert(read_file_text(root / "track-hd.zip") == "managed HD pack");
    assert(tracks().size() == 2U);
    assert(armed_track_id().empty());
    assert(!auto_boot_enabled());
    assert(!std::filesystem::exists(root / "custom-tracks-state.txt"));
    scan(installed);
    assert(tracks().size() == 2U);
    assert(armed_track_id().empty());
    assert(!uninstall("remove", uninstall_error));

    // A managed-looking symlink to an author's directory must not grant
    // permission to delete it (Windows may not allow creating symlinks).
    std::error_code link_error;
    std::filesystem::create_directory_symlink(working / "source.dkrmap",
                                              installed / "link.dkrmap", link_error);
    if (!link_error) {
        scan(installed);
        assert(!uninstall("source", uninstall_error));
        assert(std::filesystem::exists(working / "source.dkrmap" / "manifest.json"));
        std::filesystem::remove(installed / "link.dkrmap");
    }
    set_working_directory({});

    // The display-list budget reads every segment's batch count out of the
    // inflated model; anything it cannot read counts as unknown.
    {
        assert(count_batches(batch_model_payload({14, 0, 23, -5, 27})) == 64);
        assert(count_batches(batch_model_payload({})) == 0);
        assert(count_batches(batch_model_payload({3}, 0x08)) == -1);
        std::string truncated = batch_model_payload({3, 4});
        truncated.resize(truncated.size() / 2);
        assert(count_batches(truncated) == -1);
        std::string oversized = batch_model_payload({3, 4});
        oversized[0] = static_cast<char>(oversized[0] + 1); // declared size lies
        assert(count_batches(oversized) == -1);
        // numberOfSegments claims more than the inflated blob holds.
        std::string bogus(0x4C, '\0');
        bogus[7] = 0x4C;
        bogus[0x1B] = 9;
        std::size_t length = 0;
        void* deflated = tdefl_compress_mem_to_heap(bogus.data(), bogus.size(), &length,
                                                    TDEFL_DEFAULT_MAX_PROBES);
        const std::string past_end = std::string("\x4C\0\0\0\x09", 5) +
                                     std::string(static_cast<const char*>(deflated), length);
        mz_free(deflated);
        assert(count_batches(past_end) == -1);
        assert(count_level_model_batches(nullptr, 0) == -1);
        // Retail levels and tracks without a model have no count.
        assert(level_model_batches(0) == -1);
    }

    // Only enabled races assigned to Custom Tracks join the native menu.
    {
        const auto library = root / "track-select";
        for (const std::string id : {"race", "retail", "hub", "disabled"}) {
            const auto path = library / (id + ".dkrmap");
            std::filesystem::create_directories(path);
            write_file(path / "manifest.json", track_manifest(id, ""));
            std::string header(200, '\0');
            header[0] = id == "retail" ? 1 : kCustomTrackWorld;
            header[0x4C] = id == "hub" ? 5 : 0;
            header[0x4E] = 7;
            header[0x37] = 73;
            header[0xBB] = 5;
            write_file(path / "h.bin", header);
        }
        scan(library);
        assert(track_select_entries().empty()); // IDs are not published yet.
        set_enabled("disabled", false);
        build_extended_table(Section::LevelHeaders, kRetail);
        auto entries = track_select_entries();
        assert(entries.size() == 1 && entries[0].id == "race");
        assert(entries[0].level_id == resolved_level_id("race"));
        assert(entries[0].name == "race" && entries[0].vehicles == 7);
        set_enabled("race", false);
        assert(track_select_entries().empty());
        set_enabled("race", true);
        // IDs beyond signed-byte range must never wrap into a retail level.
        std::vector<std::int32_t> large_table(130);
        for (unsigned i = 0; i < 129; ++i) large_table[i] = i * 200;
        large_table.back() = -1;
        build_extended_table(Section::LevelHeaders, large_table.data());
        assert(track_select_entries().empty());
    }
    std::filesystem::remove_all(root);
    // A track's own minimap: a picture in the 2D texture table and a sprite in
    // the sprite table, both published once at boot like the 3D textures, and
    // named by placeholders the runtime substitutes as it serves them - the
    // model's minimapSpriteIndex and the sprite's baseTextureId.
    std::filesystem::remove_all(root);
    const std::string mapped_model =
        with_minimap(model_payload({1234}), kCustomSpriteIdBase);
    write_minimap_track(root / "mapped.dkrmap", "mapped",
                        sprite_payload(kCustomTexture2DIdBase), mapped_model);
    scan(root);
    assert(tracks().size() == 1U);
    assert(tracks().front().textures_2d.size() == 1U);
    assert(tracks().front().sprites.size() == 1U);
    assert(tracks().front().sprites.front().anchor_x == 61);
    assert(artwork("mapped").minimap && !artwork("nobody").minimap);
    {
        std::vector<std::uint8_t> bytes = as_bytes(mapped_model);
        assert(model_minimap_sprite(bytes) == kCustomSpriteIdBase);
        assert(model_minimap_sprite(as_bytes(std::string(8U, 'x'))) == -1);
    }

    // Boot: tex_init_textures publishes the 2D table, then the sprite table.
    constexpr std::int32_t kTextures2D[] = {0x0, 0x100, 0x200, -1};           // 2 retail
    constexpr std::int32_t kSprites[] = {0x0, 0x10, 0x20, 0x30, -1};          // 3 retail
    const std::vector<std::int32_t> table_2d =
        build_extended_table(Section::Textures2D, kTextures2D);
    assert(level_count(table_2d) == 3 && table_2d[2] == 0x200);
    const std::vector<std::int32_t> sprite_table =
        build_extended_table(Section::Sprites, kSprites);
    assert(level_count(sprite_table) == 4 && sprite_table[3] == 0x30);
    {
        // The sprite now draws the 2D texture the track shipped: retail count
        // + 0, and its anchor is untouched.
        const std::uint8_t* sprite = payload_for(Section::Sprites, 0x30, 16);
        assert(sprite != nullptr);
        assert(sprite[0] == 0 && sprite[1] == 2);
        assert(sprite[4] == 0 && sprite[5] == 61);
    }

    // Level load: the model now names the track's sprite, retail count + 0.
    const std::vector<std::int32_t> mapped_headers =
        build_extended_table(Section::LevelHeaders, kRetail);
    const std::vector<std::int32_t> mapped_models =
        build_extended_table(Section::LevelModels, kRetail);
    assert(served_minimap_sprite(0x400, static_cast<std::int32_t>(mapped_model.size())) == 3);
    // Rebuilding substitutes into the file's bytes again, not into the blob.
    (void) build_extended_table(Section::LevelModels, kRetail);
    assert(served_minimap_sprite(0x400, static_cast<std::int32_t>(mapped_model.size())) == 3);
    assert(!minimap_hidden(static_cast<std::uint32_t>(mapped_headers[3])));
    assert(!minimap_hidden(0x250));   // a retail header is never touched
    (void) mapped_models;

    // Installed after boot: the level tables are rebuilt and take the track,
    // the sprite and 2D tables are not. Its model falls back to sprite 0 and
    // its header is served with the "no minimap" bit, until a relaunch.
    write_minimap_track(root / "zlate.dkrmap", "zlate",
                        sprite_payload(kCustomTexture2DIdBase), mapped_model);
    scan(root);
    assert(tracks().size() == 2U);
    const std::vector<std::int32_t> late_headers =
        build_extended_table(Section::LevelHeaders, kRetail);
    const std::vector<std::int32_t> late_models =
        build_extended_table(Section::LevelModels, kRetail);
    {
        std::uint32_t mapped_header = 0;
        std::uint32_t late_header = 0;
        for (std::size_t index = 3; index + 2U < late_headers.size(); ++index) {
            const auto offset = static_cast<std::uint32_t>(late_headers[index]);
            const auto size = late_headers[index + 1] - late_headers[index];
            (void) size;
            if (resolved_level_id("mapped") == static_cast<std::int32_t>(index)) {
                mapped_header = offset;
            }
            if (resolved_level_id("zlate") == static_cast<std::int32_t>(index)) {
                late_header = offset;
            }
        }
        assert(mapped_header != 0U && late_header != 0U);
        assert(!minimap_hidden(mapped_header));
        assert(minimap_hidden(late_header));
        const auto model_size = static_cast<std::int32_t>(mapped_model.size());
        const std::int32_t a = served_minimap_sprite(static_cast<std::uint32_t>(late_models[3]), model_size);
        const std::int32_t b = served_minimap_sprite(static_cast<std::uint32_t>(late_models[4]), model_size);
        assert((a == 3 && b == 0) || (a == 0 && b == 3));
    }

    // Refused at scan: a model naming a minimap its track does not ship, a
    // sprite drawing a 2D texture past the track's own, and sprites the loader
    // would misread - no frames, or too long for the buffer it is copied into.
    std::filesystem::remove_all(root);
    write_minimap_track(root / "nosprite.dkrmap", "nosprite", std::string{},
                        mapped_model);
    write_minimap_track(root / "overreach.dkrmap", "overreach",
                        sprite_payload(kCustomTexture2DIdBase + 1), mapped_model);
    write_minimap_track(root / "frameless.dkrmap", "frameless",
                        sprite_payload(kCustomTexture2DIdBase, 0), mapped_model);
    write_minimap_track(root / "huge.dkrmap", "huge",
                        sprite_payload(kCustomTexture2DIdBase) + std::string(512U, '\0'),
                        mapped_model);
    // A sprite naming one of the ROM's own 2D textures needs none of its own.
    write_minimap_track(root / "borrowed.dkrmap", "borrowed", sprite_payload(12),
                        with_minimap(model_payload({1234}), 1), false);
    scan(root);
    assert(tracks().size() == 1U);
    assert(tracks().front().id == "borrowed");
    {
        SpriteInfo info;
        std::string reason;
        assert(inspect_sprite_payload(as_bytes(sprite_payload(12)), info, reason));
        assert(info.frames == 1U && info.textures == 1U && info.base_texture == 12);
        assert(!inspect_sprite_payload(as_bytes(std::string(10U, '\0')), info, reason));
    }

    // Recorded music (schema 2). The header's /music byte is the carrier, the
    // file must be inside the track and look like its codec, and the manifest
    // must not contradict itself.
    std::filesystem::remove_all(root);
    {
        std::string header(0xC8, '\0');
        header[0x37] = 1;            // collectables map: a real retail index
        header[0xBB] = 1;            // structure map likewise
        header[kHeaderMusic] = 12;   // the carrier
        const std::string mp3 = std::string("ID3\x04\0\0\0\0\0\0", 10) +
                                std::string(4096, '\x55');
        const std::string wav = std::string("RIFF\0\0\0\0WAVEfmt ", 16) +
                                std::string(2048, '\0');
        const auto descriptor = [&](const std::string& extra, std::size_t bytes) {
            return std::string("\"music\":{\"format\":\"audio-stream-v1\","
                               "\"sha256\":\"") + std::string(64, 'a') +
                   "\",\"bytes\":" + std::to_string(bytes) +
                   ",\"sampleRate\":44100,\"channels\":2,\"frames\":441000," + extra + "},";
        };
        const auto music_track = [&](const std::string& id, int schema,
                                     const std::string& music_json,
                                     const std::string& name,
                                     const std::string& bytes) {
            const std::filesystem::path dir = root / (id + ".dkrmap");
            std::filesystem::create_directories(dir / "music");
            write_file(dir / "h.bin", header);
            if (!name.empty()) {
                write_file(dir / name, bytes);
            }
            write_file(dir / "manifest.json",
                       "{\"schemaVersion\":" + std::to_string(schema) +
                           ",\"id\":\"" + id + "\",\"name\":\"" + id + "\"," +
                           music_json +
                           "\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
        };
        const std::string ok_mp3 =
            "\"codec\":\"mp3\",\"file\":\"music/main.mp3\",\"carrierSequence\":12,"
            "\"volume\":90,\"loopStartFrame\":44100,\"loopEndFrame\":0,"
            "\"finalLap\":\"constant\"";
        music_track("song", 2, descriptor(ok_mp3, mp3.size()), "music/main.mp3", mp3);
        music_track("wave", 2,
                    descriptor("\"codec\":\"wav\",\"file\":\"music/main.wav\","
                               "\"carrierSequence\":12", wav.size()),
                    "music/main.wav", wav);
        // Refused: schema 1 with music, schema 2 without, a carrier that is
        // not the header's, a file outside the track, a renamed file, a size
        // that disagrees, and a loop past the end.
        music_track("old", 1, descriptor(ok_mp3, mp3.size()), "music/main.mp3", mp3);
        music_track("bare", 2, "", "", "");
        std::string other = ok_mp3;
        other.replace(other.find(":12,"), 4, ":13,");
        music_track("carrier", 2, descriptor(other, mp3.size()), "music/main.mp3", mp3);
        std::string escape = ok_mp3;
        escape.replace(escape.find("music/main.mp3"), 14, "../song.dkrmap/x.mp3");
        music_track("escape", 2, descriptor(escape, mp3.size()), "music/main.mp3", mp3);
        music_track("renamed", 2, descriptor(ok_mp3, wav.size()), "music/main.mp3", wav);
        music_track("resized", 2, descriptor(ok_mp3, mp3.size() + 1), "music/main.mp3", mp3);
        std::string long_loop = ok_mp3;
        long_loop.replace(long_loop.find("\"loopEndFrame\":0"), 16, "\"loopEndFrame\":441001");
        music_track("loop", 2, descriptor(long_loop, mp3.size()), "music/main.mp3", mp3);

        // A symlink that leads out of the track is refused even with a clean
        // name. Skipped where the platform will not create one.
        bool linked = false;
        {
            const std::filesystem::path dir = root / "linked.dkrmap";
            music_track("linked", 2, descriptor(ok_mp3, mp3.size()), "", "");
            write_file(root / "outside.mp3", mp3);
            std::error_code link_error;
            std::filesystem::create_symlink(root / "outside.mp3",
                                            dir / "music" / "main.mp3", link_error);
            linked = !link_error;
        }

        scan(root);
        std::vector<std::string> ids;
        for (const Track& track : tracks()) {
            ids.push_back(track.id);
        }
        std::sort(ids.begin(), ids.end());
        assert((ids == std::vector<std::string>{"song", "wave"}));
        (void) linked;

        (void) build_extended_table(Section::LevelHeaders, kRetail);
        const std::int32_t song_level = resolved_level_id("song");
        const auto music = music_for_level(song_level);
        assert(music.has_value());
        assert(music->codec == MusicCodec::Mp3 && music->carrier == 12);
        assert(music->volume == 90 && music->loop_start == 44100U && music->loop_end == 0U);
        assert(!music->final_lap_speedup);
        assert(music->file.filename() == "main.mp3" && music->bytes == mp3.size());
        assert(music_for_level(resolved_level_id("wave"))->codec == MusicCodec::Wav);
        assert(music_for_level(resolved_level_id("wave"))->final_lap_speedup);
        assert(!music_for_level(0).has_value());
        set_enabled("song", false);
        (void) build_extended_table(Section::LevelHeaders, kRetail);
        assert(!music_for_level(song_level).has_value() ||
               resolved_level_id("wave") == song_level);
        set_enabled("song", true);
    }

    // A native sequence (schema 2, dkr-alcseq-v1). Read and validated at scan;
    // the header's /music is the carrier and its /instruments the channel mask.
    std::filesystem::remove_all(root);
    {
        std::string header(0xC8, '\0');
        header[0x37] = 1;
        header[0xBB] = 1;
        header[kHeaderMusic] = 12;
        header[kHeaderInstruments] = '\xFF';
        header[kHeaderInstruments + 1] = '\xFF';
        // One track, program 1, two notes inside an endless loop (the shared
        // fixture "loop-forever").
        const std::string hex =
            "00000044000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000000180"
            "00ff5107a12000c00100ff2e00ff00903c648140830090436481408300ff2dffff00"
            "00001700ff2f";
        std::string song;
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            song.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        }
        std::string broken = song;
        broken[3] = '\xF0';   // track 1 starts outside the sequence
        const auto sequence_track = [&](const std::string& id, const std::string& extra,
                                        const std::string& bytes, std::size_t recorded,
                                        const std::string& name = "music/main.cseq",
                                        char mask_low = '\xFF') {
            const std::filesystem::path dir = root / (id + ".dkrmap");
            std::filesystem::create_directories(dir / "music");
            std::string own_header = header;
            own_header[kHeaderInstruments + 1] = mask_low;
            write_file(dir / "h.bin", own_header);
            write_file(dir / name, bytes);
            write_file(dir / "manifest.json",
                       "{\"schemaVersion\":2,\"id\":\"" + id + "\",\"name\":\"" + id + "\","
                       "\"music\":{\"format\":\"dkr-alcseq-v1\",\"sha256\":\"" +
                           std::string(64, 'b') + "\",\"bytes\":" + std::to_string(recorded) +
                           "," + extra + "},"
                       "\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"}]}");
        };
        const std::string ok =
            "\"bank\":\"dkr-stock-v1\",\"file\":\"music/main.cseq\",\"carrierSequence\":12,"
            "\"tempoBpm\":120,\"volume\":110,\"reverb\":1,\"channelMask\":65535";
        sequence_track("tune", ok, song, song.size());
        // Refused: an unknown bank, no tempo, a tempo the final lap cannot
        // scale, an overflowing volume, a mask the header contradicts, bytes
        // the player would misread, a size that disagrees and a wrong suffix.
        std::string bank = ok;
        bank.replace(bank.find("dkr-stock-v1"), 12, "gm-v1");
        sequence_track("bank", bank, song, song.size());
        std::string no_tempo = ok;
        no_tempo.replace(no_tempo.find("\"tempoBpm\":120,"), 15, "");
        sequence_track("notempo", no_tempo, song, song.size());
        std::string fast = ok;
        fast.replace(fast.find(":120,"), 5, ":256,");
        sequence_track("fast", fast, song, song.size());
        std::string loud = ok;
        loud.replace(loud.find(":110,"), 5, ":128,");
        sequence_track("loud", loud, song, song.size());
        sequence_track("mask", ok, song, song.size(), "music/main.cseq", '\x0F');
        sequence_track("broken", ok, broken, broken.size());
        sequence_track("resized", ok, song, song.size() + 2);
        std::string mid = ok;
        mid.replace(mid.find("main.cseq"), 9, "main.mid");
        sequence_track("suffix", mid, song, song.size(), "music/main.mid");

        scan(root);
        std::vector<std::string> ids;
        for (const Track& track : tracks()) {
            ids.push_back(track.id);
        }
        assert((ids == std::vector<std::string>{"tune"}));
        (void) build_extended_table(Section::LevelHeaders, kRetail);
        const auto music = music_for_level(resolved_level_id("tune"));
        assert(music.has_value() && music->kind == MusicKind::Sequence);
        assert(music->carrier == 12 && music->tempo_bpm == 120 && music->volume == 110);
        assert(music->reverb == 1 && music->channel_mask == 0xFFFF);
        assert(music->sequence && music->sequence->size() == song.size());
        assert(std::equal(song.begin(), song.end(), music->sequence->begin(),
                          [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; }));
    }

    std::printf("custom_tracks_tests: ok\n");
    return 0;
}
