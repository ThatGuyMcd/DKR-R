// .dkrmap artwork inside a legacy mod session.
//
// custom_tracks numbers a track's own textures after the boot texture table and
// writes those IDs into the track's level model. In a mod session that table is
// the character-augmented boot bank, and the textures have to be appended to it
// - and to every scene bank - at exactly those IDs. These checks tie the two
// sides together against the owned ROM given as the first argument.
#include "custom_tracks.hpp"
#include "legacy_mod_launch.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace dkr::mods;
namespace tracks = dkr::runtime::custom_tracks;

namespace {
unsigned checks = 0;
void require(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw Error(why);
}
template <class F> void rejects(F fn, const char* why) {
    bool caught = false;
    try {
        fn();
    } catch (const Error&) {
        caught = true;
    }
    require(caught, why);
}

void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
void write(const std::filesystem::path& path, const std::string& text) {
    write(path, Bytes(text.begin(), text.end()));
}
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}

// One opaque RGBA16 frame, as the addon's BuildTexture writes it.
Bytes texture(std::uint8_t width, std::uint8_t height, std::uint8_t fill) {
    const std::size_t size = (32U + std::size_t(width) * height * 2U + 15U) & ~std::size_t{15U};
    Bytes bytes(size, fill);
    std::fill_n(bytes.begin(), 32, std::uint8_t{0});
    bytes[0x00] = width;
    bytes[0x01] = height;
    bytes[0x02] = 0x11; // OPAQUE, RGBA16
    bytes[0x05] = 1;
    bytes[0x12] = 1; // one frame
    bytes[0x16] = static_cast<std::uint8_t>(size >> 8);
    bytes[0x17] = static_cast<std::uint8_t>(size);
    return bytes;
}

// A level model container whose stored prefix holds the texture table, the
// shape custom_tracks rewrites (see custom_tracks_tests.cpp).
Bytes model(const std::vector<std::int32_t>& ids) {
    constexpr std::uint32_t table = 0x4C;
    const auto prefix = static_cast<std::uint32_t>(table + ids.size() * 8U);
    Bytes body(prefix, 0);
    put32(body, 0, table);
    body[0x19] = static_cast<std::uint8_t>(ids.size());
    put32(body, 0x20, tracks::kCustomSpriteIdBase); // Track-local minimap sprite 0.
    for (std::size_t i = 0; i < ids.size(); ++i) {
        put32(body, table + i * 8U, static_cast<std::uint32_t>(ids[i]));
        body[table + i * 8U + 4] = 16;
        body[table + i * 8U + 5] = 16;
        body[table + i * 8U + 6] = 0x11;
    }
    const auto size = static_cast<std::uint32_t>(body.size() + 64U);
    Bytes out{static_cast<std::uint8_t>(size),
              static_cast<std::uint8_t>(size >> 8),
              static_cast<std::uint8_t>(size >> 16),
              static_cast<std::uint8_t>(size >> 24),
              0x09,
              0x00,
              static_cast<std::uint8_t>(prefix),
              static_cast<std::uint8_t>(prefix >> 8),
              static_cast<std::uint8_t>(~prefix),
              static_cast<std::uint8_t>(~prefix >> 8)};
    out.insert(out.end(), body.begin(), body.end());
    out.insert(out.end(), 64U, 'z');
    return out;
}

struct AuthoredTrack {
    std::string id;
    std::vector<Bytes> textures;
};

void write_track(const std::filesystem::path& root, const AuthoredTrack& track) {
    const auto folder = root / (track.id + ".dkrmap");
    std::string adds = "{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"},"
                       "{\"section\":\"LEVEL_MODELS\",\"file\":\"m.bin\"}";
    std::vector<std::int32_t> ids;
    for (std::size_t j = 0; j < track.textures.size(); ++j) {
        const auto file = "textures/" + std::to_string(j) + ".bin";
        adds += ",{\"section\":\"TEXTURES_3D\",\"file\":\"" + file + "\"}";
        write(folder / file, track.textures[j]);
        ids.push_back(tracks::kCustomTextureIdBase + static_cast<std::int32_t>(j));
    }
    // A header keeps pointing at retail object maps (73 and 5) when the track
    // ships none; 0 would be another level's objects.
    Bytes header(200, 0);
    header[0x37] = 73;
    header[0xBB] = 5;
    write(folder / "h.bin", header);
    write(folder / "m.bin", model(ids));
    Bytes sprite(16, 0);
    sprite[0] = 0x70;
    sprite[3] = 1;
    sprite[13] = 1;
    write(folder / "minimap.bin", texture(16, 16, 'm'));
    write(folder / "sprite.bin", sprite);
    adds += ",{\"section\":\"TEXTURES_2D\",\"file\":\"minimap.bin\"},"
            "{\"section\":\"SPRITES\",\"file\":\"sprite.bin\"}";
    write(folder / "manifest.json",
          "{\"schemaVersion\":1,\"id\":\"" + track.id + "\",\"name\":\"" + track.id + "\",\"adds\":[" + adds + "]}");
}

std::vector<std::int32_t> table_of(const AssetDirectory& directory, unsigned section) {
    const auto bytes = directory.read(section, 0, directory.section_size(section));
    std::vector<std::int32_t> table;
    for (std::size_t at = 0; at + 4 <= bytes.size(); at += 4) {
        table.push_back(static_cast<std::int32_t>(be32(bytes, at)));
        if (table.back() == -1)
            break;
    }
    return table;
}

// Every added model is served naming, in order, bank records whose bytes are
// its own track's texture files.
void require_models_resolve(const std::shared_ptr<const AssetBank>& boot, const std::vector<AuthoredTrack>& authored,
                            std::size_t base) {
    const auto retail = table_of(*AssetDirectory::build(boot), 26);
    const auto extended = tracks::build_extended_table(tracks::Section::LevelModels, retail.data(), retail.size());
    const auto models = retail.size() - 2;
    require(extended.size() == retail.size() + authored.size(), "Each authored track did not add one model.");
    std::size_t matched = 0;
    for (std::size_t index = models; index < models + authored.size(); ++index) {
        const auto size = extended[index + 1] - extended[index];
        const auto served = tracks::payload_for(tracks::Section::LevelModels, extended[index], size);
        require(served != nullptr, "An authored model is not served.");
        const View body(served + 10, static_cast<std::size_t>(size) - 10);
        const auto sprite = be32(body, 0x20);
        require(sprite >= boot->record_count(12) - authored.size() && sprite < boot->record_count(12),
                "An authored model lost its minimap sprite.");
        const auto texture = be16(boot->record(12, sprite), 0);
        require(texture >= boot->record_count(4) - authored.size() && texture < boot->record_count(4),
                "A minimap sprite lost its boot-owned 2D texture.");
        const auto count = be16(body, 0x18);
        const auto* owner = [&]() -> const AuthoredTrack* {
            for (const auto& track : authored)
                if (track.textures.size() == count)
                    return &track;
            return nullptr;
        }();
        require(owner != nullptr, "A served model names an unexpected texture count.");
        for (unsigned j = 0; j < count; ++j) {
            const auto id = be32(body, be32(body, 0) + j * 8U);
            require(id >= base && id < boot->record_count(2), "A model texture points outside the dkrmap range.");
            require(std::ranges::equal(boot->record(2, id), owner->textures[j]),
                    "A model texture ID resolves to another texture in the boot bank.");
        }
        ++matched;
    }
    require(matched == authored.size(), "Not every authored model was checked.");
}

void unit() {
    rejects([] { publish_dkrmap_artwork(nullptr); }, "Artwork was published without a boot bank.");
    rejects([] { AssetBank::append_textures(nullptr, {}); }, "Artwork was appended to no bank.");
    rejects([] { AssetBank::append_artwork(nullptr, {}); }, "Minimaps were appended to no bank.");
}

void guest_word(Bytes& memory, std::uint32_t address, std::uint32_t value) {
    const auto at = address & 0x1fffffffU;
    for (unsigned i = 0; i < 4; ++i)
        memory.at((at + i) ^ 3) = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
struct ResidentFixture {
    Bytes memory = Bytes(8 * MiB);
    ResidentAssetLayout layout;
    explicit ResidentFixture(const AssetBank& bank, const ResidentBank& resident)
        : layout(resident_asset_layout(bank.revision())) {
        for (unsigned i = 0; i < resident.tables().size(); ++i) {
            const auto address = 0x80200000U + i * 0x10000U;
            const auto& bytes = resident.tables()[i];
            require(bytes.size() < 0x10000, "Fixture tables overlap.");
            guest_word(memory, layout.table_pointers[i], address);
            for (unsigned j = 0; j < bytes.size(); ++j)
                memory.at(((address & 0x1fffffffU) + j) ^ 3) = bytes[j];
        }
        for (unsigned i = 0; i < 3; ++i) {
            guest_word(memory, layout.caches[i].count_address, 0);
            guest_word(memory, layout.caches[i].pointer_address, 0x80400000U + i * 0x10000U);
        }
    }
    void live(unsigned kind, unsigned id) {
        const auto cache = 0x80400000U + kind * 0x10000U;
        guest_word(memory, layout.caches[kind].count_address, 1);
        guest_word(memory, cache, id);
        guest_word(memory, cache + 4, 0x80500000U + kind * 0x10000U);
        guest_word(memory, 0x80500000U + kind * 0x10000U, 0x12345678);
    }
};

void corpus(const std::filesystem::path& rom) {
    const auto stock = AssetBank::stock(read_file(rom, MaxImage));
    const auto n2 = stock->record_count(2), n4 = stock->record_count(4), n12 = stock->record_count(12),
               n29 = stock->record_count(29), n32 = stock->record_count(32);
    // custom_tracks keeps its state files beside the scanned folder, so the
    // folder is nested to keep them inside this test's own directory.
    const auto sandbox =
        std::filesystem::temp_directory_path() /
        ("dkr-dkrmap-artwork-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = sandbox / "custom-tracks";
    std::filesystem::create_directories(root);

    // Nothing authored: the stock boot path stays the untouched cartridge.
    tracks::scan(root);
    require(publish_dkrmap_artwork(stock).empty(), "Artwork appeared without a track.");
    require(AssetBank::append_textures(stock, {}) == stock, "An empty artwork list changed the bank.");
    require(!RuntimeSession(stock).acquire().route(), "A session without additions left the cartridge path.");

    const std::vector<AuthoredTrack> authored{
        {"alpha", {texture(32, 16, 'a'), texture(16, 16, 'b')}},
        {"beta", {texture(64, 32, 'c')}},
    };
    for (const auto& track : authored)
        write_track(root, track);
    tracks::scan(root);
    require(tracks::tracks().size() == authored.size(), "The authored tracks were not scanned.");

    // A character namespace owning one texture and one model, built the way
    // allocate_characters' additions are.
    const auto character_texture = texture(16, 16, 'k');
    AssetBank::Overrides additions{{{2, static_cast<unsigned>(n2)}, character_texture},
                                   {{29, static_cast<unsigned>(n29)}, Bytes(64, 0x5A)}};
    // A large portrait/frame namespace, matching the failure class from the
    // 42-character report. Its sentinel is beyond the old 4096-word bound.
    for (unsigned i = 0; i < 4200; ++i)
        additions.emplace(AssetKey{4, static_cast<unsigned>(n4) + i}, character_texture);
    const auto original_ids = stock->stock_section(30);
    Bytes ids(original_ids.begin(), original_ids.begin() + (n29 + 1) * 2);
    ids.push_back(static_cast<std::uint8_t>(n32 >> 8));
    ids.push_back(static_cast<std::uint8_t>(n32));
    const auto characters = AssetBank::augment(stock, additions, ids);
    require(characters->record_count(2) == n2 + 1, "The character texture was not appended.");

    // Characters first, dkrmap artwork after them, in custom_tracks' order.
    const auto artwork = publish_dkrmap_artwork(characters);
    require(artwork.size() == 7, "Not every authored texture/minimap was published.");
    const auto boot = AssetBank::append_artwork(characters, artwork);
    require(boot->augmented() && boot->record_count(2) == n2 + 4, "Boot artwork count is wrong.");
    require(boot->record_count(4) == n4 + 4202 && boot->record_count(12) == n12 + 2,
            "Minimap namespace was not boot-owned.");
    require(std::ranges::equal(boot->record(2, static_cast<unsigned>(n2)), character_texture),
            "dkrmap artwork displaced a character texture.");
    require(boot->record_count(29) == n29 + 1 && boot->record(29, static_cast<unsigned>(n29)).size() == 64,
            "dkrmap artwork dropped a character model.");
    for (const auto& [key, bytes] : artwork)
        require(std::ranges::equal(boot->record(key.first, key.second), bytes), "Boot bank artwork is out of order.");
    // The models resolve against the final bank too, not just the table.
    require_models_resolve(boot, authored, n2 + 1);
    {
        const auto directory = AssetDirectory::build(boot);
        const auto table = directory->read(3, 0, directory->section_size(3));
        const auto count = boot->record_count(2);
        require(be32(table, 4 * count) == directory->section_size(2), "Texture table does not span the artwork.");
        require(be32(table, 4 * (count + 1)) == 0xFFFFFFFFU, "Texture table sentinel is misplaced.");
    }

    // Every scene carries the same artwork at the same IDs.
    AssetBus bus;
    require(bool(ResidentBank::prepare(boot, boot, bus)->route()), "The artwork boot bank does not route.");
    auto changed2D = Bytes(stock->record(4, 0).begin(), stock->record(4, 0).end());
    changed2D.resize(changed2D.size() + 16);
    auto changedSprite = Bytes(stock->record(12, 0).begin(), stock->record(12, 0).end());
    changedSprite.resize(changedSprite.size() + 4);
    const auto course = AssetBank::derive(stock, std::string(64, 'a'),
                                          {{{27, 5}, Bytes{1, 2, 3}}, {{4, 0}, changed2D}, {{12, 0}, changedSprite}});
    const auto scene = AssetBank::append_artwork(AssetBank::augment(course, additions, ids), artwork);
    require(bool(ResidentBank::prepare(boot, scene, bus)->route()), "A legacy course lost the shared artwork.");
    require(scene->digest() == course->digest() && scene->record(27, 5)[0] == 1, "Artwork discarded a legacy course.");
    rejects([&] { ResidentBank::prepare(boot, AssetBank::augment(course, additions, ids), bus); },
            "A scene without the artwork was accepted.");
    rejects([&] { ResidentBank::prepare(characters, scene, bus); }, "A scene grew the boot texture table.");
    auto reordered = artwork;
    std::swap(reordered.at({2, static_cast<unsigned>(n2 + 1)}), reordered.at({2, static_cast<unsigned>(n2 + 2)}));
    rejects(
        [&] {
            ResidentBank::prepare(
                boot, AssetBank::append_artwork(AssetBank::augment(course, additions, ids), reordered), bus);
        },
        "A scene moved a live artwork ID.");

    // Artwork alone, without characters, starts right after the cartridge's.
    const auto alone = publish_dkrmap_artwork(stock);
    const auto stock_boot = AssetBank::append_artwork(stock, alone);
    require(stock_boot->augmented() && std::ranges::equal(stock_boot->record(2, static_cast<unsigned>(n2)),
                                                          alone.at({2, static_cast<unsigned>(n2)})),
            "Artwork alone does not follow the cartridge textures.");
    require_models_resolve(stock_boot, authored, n2);
    RuntimeSession session(stock, nullptr, {}, [alone](auto bank) { return AssetBank::append_artwork(bank, alone); });
    require(bool(session.acquire().route()), "An artwork session read the cartridge's smaller table.");
    rejects([&] { ResidentBank::prepare(stock_boot, course, bus); }, "A course without the artwork was accepted.");

    // Reproduce the previous bug: a post-boot sprite hook replaces the old
    // sentinel with a new end offset, without updating its declared owner.
    const auto prior = AssetBank::augment(stock, additions, ids);
    const auto prior_resident = ResidentBank::prepare(prior, prior, bus);
    ResidentFixture broken(*prior, *prior_resident);
    ResidentAssetState old_state(prior_resident);
    guest_word(broken.memory, 0x80220000U + static_cast<unsigned>((n12 + 1) * 4), 1234);
    rejects([&] { old_state.prepare(broken.memory, prior_resident); },
            "The original ownership guard stopped detecting mutated tables.");

    const auto initial = ResidentBank::prepare(boot, boot, bus), next = ResidentBank::prepare(boot, scene, bus);
    ResidentFixture fixture(*boot, *initial);
    ResidentAssetState state(initial);
    fixture.live(0, static_cast<unsigned>(n4 + 4200));
    fixture.live(1, static_cast<unsigned>(n12));
    const auto unchanged = fixture.memory;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        auto plan = state.prepare(fixture.memory, next);
        require(state.commit(fixture.memory, std::move(plan)) == ResidentAssetState::Commit::Published,
                "Track Select bank switch failed.");
        plan = state.prepare(fixture.memory, initial);
        require(state.commit(fixture.memory, std::move(plan)) == ResidentAssetState::Commit::Published,
                "Return to the boot bank failed.");
        require(fixture.memory == unchanged, "Bank switches changed shared minimap cache IDs, pointers or refcounts.");
    }
    RuntimeSession augmented_session(stock, nullptr, {}, [additions, ids, artwork](auto bank) {
        return AssetBank::append_artwork(AssetBank::augment(bank, additions, ids), artwork);
    });
    PreparedTrack prepared{};
    prepared.bank = course;
    prepared.root.content_id = course->digest();
    prepared.root.carrier = 5;
    prepared.artifact_digest = std::string(64, 'b');
    augmented_session.admit(std::move(prepared));
    const auto copy = augmented_session.instantiate();
    require(copy->boot_bank()->fingerprint() == boot->fingerprint(),
            "Prepared session did not retain artwork ownership.");
    AssetBus session_bus;
    const auto session_boot = ResidentBank::prepare(boot, boot, session_bus);
    ResidentFixture session_guest(*boot, *session_boot);
    session_guest.live(0, static_cast<unsigned>(n4 + 4200));
    session_guest.live(1, static_cast<unsigned>(n12));
    const auto boot_memory = session_guest.memory;
    const auto pristine = copy->checkpoint();
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        copy->request(course->digest(), 5);
        copy->begin_scene(session_guest.memory, 5);
        require(copy->current_content() == course->digest(), "The scene adapter did not select the legacy bank.");
        copy->begin_scene(session_guest.memory, static_cast<unsigned>(stock->record_count(23)), true);
        require(copy->current_content().empty() && session_guest.memory == boot_memory,
                "The scene adapter did not restore shared artwork for the native preview.");
    }
    auto restore = copy->stage_checkpoint(pristine);
    require(copy->commit_checkpoint(std::move(restore)), "Shared artwork checkpoint did not restore.");
    require(copy->current_content().empty(), "Restoring the shared namespace changed scene identity.");
    rejects([&] { AssetBank::append_artwork(characters, {{{27, 0}, Bytes{1}}}); },
            "Artwork admitted an unrelated section.");
    rejects(
        [&] { AssetBank::append_artwork(characters, {{{4, static_cast<unsigned>(n4 + 4201)}, character_texture}}); },
        "Artwork admitted a noncontiguous minimap ID.");

    std::filesystem::remove_all(sandbox);
}
} // namespace

int main(int argc, char** argv) {
    try {
        unit();
        if (argc >= 2)
            corpus(utf8_path(argv[1]));
        if (argc == 3) {
            const auto sandbox =
                std::filesystem::temp_directory_path() /
                ("dkr-minimap-pack-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            tracks::scan(sandbox / "tracks");
            std::string error;
            tracks::InstallOutcome installed;
            const bool imported = tracks::install(utf8_path(argv[2]), error, &installed);
            require(imported, error.c_str());
            const auto stock = AssetBank::stock(read_file(utf8_path(argv[1]), MaxImage));
            const auto artwork = publish_dkrmap_artwork(stock);
            const auto boot = AssetBank::append_artwork(stock, artwork);
            require(artwork.size() == 30,
                    "Supplied Shrine pack did not publish 28 textures plus its minimap texture/sprite.");
            require(boot->record_count(12) == stock->record_count(12) + 1,
                    "Supplied minimap sprite was not boot-owned.");
            require(be16(boot->record(12, static_cast<unsigned>(stock->record_count(12))), 0) == stock->record_count(4),
                    "Supplied minimap texture reference was not resolved.");
            const auto model_table = table_of(*AssetDirectory::build(stock), 26);
            const auto extended =
                tracks::build_extended_table(tracks::Section::LevelModels, model_table.data(), model_table.size());
            const auto model_id = stock->record_count(27);
            const auto size = extended.at(model_id + 1) - extended.at(model_id);
            const auto bytes = tracks::payload_for(tracks::Section::LevelModels, extended.at(model_id), size);
            require(bytes != nullptr, "Supplied model payload was not served.");
            require(tracks::model_minimap_sprite(Bytes(bytes, bytes + size)) == stock->record_count(12),
                    "Supplied model's minimap stayed unresolved.");
            const auto header_table = table_of(*AssetDirectory::build(stock), 22);
            const auto headers =
                tracks::build_extended_table(tracks::Section::LevelHeaders, header_table.data(), header_table.size());
            require(!tracks::minimap_hidden(headers.at(stock->record_count(23))),
                    "Supplied minimap was hidden despite being published.");
            if (!installed.temp_root.empty())
                tracks::discard_install_temp(installed.temp_root);
            std::filesystem::remove_all(sandbox);
        } else if (argc > 3)
            throw Error("Usage: tests [owned-v77-or-v80-rom [dkrmap-pack.zip]]");
        std::cout << checks << " dkrmap artwork checks passed"
                  << (argc >= 2 ? "." : "; pass an owned ROM for the bank checks.") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
