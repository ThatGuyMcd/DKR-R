#include "legacy_behaviour.hpp"
#include "legacy_runtime_character.hpp"
#include "legacy_character_menu.hpp"
#include "legacy_track_artifact.hpp"
#include "legacy_character_artifact.hpp"
#include "legacy_runtime_session.hpp"
#include "online_mod_manifest.hpp"
#include "legacy_mod_stage.hpp"
#include <json/json.hpp>
#include <bit>
#include <chrono>
#include <iostream>
#include <set>

using namespace dkr::mods;
namespace {
unsigned checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw Error(message);
}
template <class F> void rejects(F fn) {
    bool caught = false;
    try {
        fn();
    } catch (const std::exception&) {
        caught = true;
    }
    check(caught, "Unreviewed behaviour accepted.");
}
BehaviourContract reviewed() {
    BehaviourContract c;
    c.abi = BehaviourAbi;
    c.evidence = std::string(64, 'a');
    return c;
}
void put(Bytes& data, std::size_t at, std::uint32_t value) {
    for (unsigned b = 0; b < 4; ++b)
        data.at(at + b) = value >> (24 - 8 * b);
}
unsigned animation_calls = 0;
void texture_fixture(std::uint8_t* ram, recomp_context* ctx) {
    ++animation_calls;
    CharacterMenuFields f;
    f.fill(0x80000000U);
    CharacterMenuMemory g({ram, 8192}, f);
    check(unsigned(ctx->r7) == 2 && unsigned(ctx->r4) >= 0x80000700 && unsigned(ctx->r29) == 0x80001d00,
          "Texture callback arguments/scratch differ.");
    g.write(unsigned(ctx->r6), g.read(unsigned(ctx->r6)) + 128);
    g.write(unsigned(ctx->r5),
            (g.read(unsigned(ctx->r5)) & ~0x06000000U) | ((animation_calls & 1) ? 0x02000000U : 0x04000000U));
}
void animation_profile() {
    Bytes memory(8192);
    CharacterMenuFields f;
    f.fill(0x80000000U);
    CharacterMenuMemory g(memory, f);
    recomp_context ctx{};
    ctx.r4 = 0x80000100;
    ctx.r5 = 2;
    ctx.r29 = 0x80001f00;
    g.write(0x80000140, 0x80000200);
    g.write(0x80000255, 1, 1);
    g.write(0x80000168, 0x80000300);
    g.write(0x80000300, 0x80000340);
    g.write(0x80000340, 0x80000400);
    g.write(0x80000450, 1, 2);
    g.write(0x80000428, 7, 2);
    g.write(0x80000422, 3, 2);
    g.write(0x80000400, 0x80000600);
    g.write(0x80000438, 0x80000900);
    g.write(0x80000600, 0x80000700);
    g.write(0x80000608, 0x80000700);
    g.write(0x80000610, 0x80000740);
    for (unsigned i = 0; i < 7; ++i) {
        g.write(0x80000900 + i * 12, i == 2 ? 1 : i == 3 ? 2 : i == 4 ? 255 : 0, 1);
        g.write(0x80000907 + i * 12, i, 1);
        g.write(0x80000908 + i * 12, i == 5 ? 0x00400000 : i == 0 || i == 6 ? 0x10000 : 0x00810000);
    }
    const auto before = memory;
    animation_calls = 0;
    animate_character_textures(memory, ctx, texture_fixture);
    check(animation_calls == 4, "Repeated random texture consumed extra native RNG calls.");
    check(g.read(0x80000907 + 24, 1) == g.read(0x80000907 + 12, 1),
          "Aliased texture phase did not share first random batch.");
    check((g.read(0x80000908 + 24) & 0x06000000) == (g.read(0x80000908 + 12) & 0x06000000),
          "Random direction did not propagate.");
    check(g.read(0x80000907, 1) == 2 && g.read(0x80000907 + 72, 1) == 8, "Non-random batches were deduplicated.");
    check(g.read(0x80000907 + 48, 1) == 4 && g.read(0x80000907 + 60, 1) == 5, "Unanimated/missing textures changed.");
    check(ctx.r4 == 0x80000100 && ctx.r5 == 2 && ctx.r29 == 0x80001f00, "Intercepted CPU context changed.");
    const auto after = memory;
    memory = before;
    animation_calls = 0;
    animate_character_textures(memory, ctx, texture_fixture);
    check(memory == after && animation_calls == 4, "Restored animation input did not replay deterministically.");
    memory = before;
    g.write(0x80000900 + 72, 3, 1);
    const auto malformed = memory;
    animation_calls = 0;
    rejects([&] { animate_character_textures(memory, ctx, texture_fixture); });
    check(memory == malformed && !animation_calls, "Malformed final texture partially changed earlier batches/RNG.");
    memory = before;
    ctx.r29 = 0x80000080;
    rejects([&] { animate_character_textures(memory, ctx, texture_fixture); });
    ctx.r29 = 0x80001f00;
    ctx.r4 = UINT32_MAX;
    rejects([&] { animate_character_textures(memory, ctx, texture_fixture); });
}
void contracts() {
    BehaviourContract empty;
    check(decode_behaviour(encode_behaviour(empty)) == empty, "Asset-only round trip.");
    check(behaviour_identity(empty).empty(), "Asset-only identity changed.");
    for (unsigned i = 0; i < BehaviourSites; ++i) {
        auto c = reviewed();
        c.changed_bytes = 4;
        c.instructions[i] = behaviour_sites()[i].replacement;
        check(decode_behaviour(encode_behaviour(c)) == c, "Reviewed component round trip.");
        const auto value = execute_behaviour_component(c, i, 0x12345678);
        check(value == (((c.instructions[i] >> 26) == 9 && (c.instructions[i] & 0x8000)) ? UINT64_MAX
                                                                                         : c.instructions[i] & 0xffffU),
              "MIPS immediate sign extension.");
        check(execute_behaviour_component(c, (i + 1) % BehaviourSites, 0x12345678) == 0x12345678,
              "Unchanged site was overridden.");
        auto bad = c;
        bad.instructions[i] ^= 1;
        rejects([&] { validate_behaviour(bad); });
        rejects([&] { execute_behaviour_component(bad, i, 0); });
        bad = c;
        bad.abi = 0;
        rejects([&] { validate_behaviour(bad); });
    }
    auto c = reviewed();
    c.stat_mask = 3;
    c.stat_bits = {std::bit_cast<std::uint32_t>(1.25f), std::bit_cast<std::uint32_t>(4.0f), 0};
    check(decode_behaviour(encode_behaviour(c)) == c, "Racer parameters round trip.");
    const auto id = behaviour_identity(c);
    c.stat_bits[0] = std::bit_cast<std::uint32_t>(1.5f);
    check(behaviour_identity(c) != id, "Stats omitted from identity.");
    for (auto bits : {0U, 0x7f800000U, 0x7fc00000U, 0xbf800000U}) {
        auto bad = c;
        bad.stat_bits[0] = bits;
        rejects([&] { validate_behaviour(bad); });
    }
    auto bad = c;
    bad.stat_bits[2] = 1;
    rejects([&] { validate_behaviour(bad); });
    bad = c;
    bad.stat_mask = 8;
    rejects([&] { validate_behaviour(bad); });
    auto json = nlohmann::json::parse(encode_behaviour(c));
    json["instructions"][0] = 0x0c000000U;
    rejects([&] { decode_behaviour(json.dump()); });
    json = nlohmann::json::parse(encode_behaviour(c));
    json["abi"] = -1;
    rejects([&] { decode_behaviour(json.dump()); });
    json["abi"] = UINT64_MAX;
    rejects([&] { decode_behaviour(json.dump()); });
    json = nlohmann::json::parse(encode_behaviour(c));
    json["network"] = "native.dll";
    rejects([&] { decode_behaviour(json.dump()); });
    rejects([&] { decode_behaviour("{\"abi\":1,\"abi\":1}"); });
    rejects([&] { decode_behaviour(std::string(2049, ' ')); });
    rejects([&] { execute_behaviour_component(c, BehaviourSites, 0); });
    auto ns = std::make_shared<CharacterNamespace>();
    for (unsigned i = 0; i < 3; ++i) {
        AllocatedCharacter a;
        a.id = std::string(64, 'a' + i);
        a.base_character = 3;
        a.behaviour = c;
        a.behaviour.stat_bits[0] = std::bit_cast<std::uint32_t>(1.0f + i);
        ns->characters.push_back(a);
    }
    CharacterRoster roster(ns);
    roster.request(0, ns->characters[0].id);
    roster.request(1, ns->characters[1].id);
    roster.commit(std::array<std::uint8_t, 3>{3, 3, 3}, 3);
    Bytes guest(4096);
    CharacterMenuFields fields;
    fields.fill(0x80000000U);
    CharacterMenuMemory g(guest, fields);
    const auto native = std::bit_cast<std::uint32_t>(0.75f);
    for (unsigned player = 0; player < 3; ++player) {
        g.write(0x80000100, player, 2);
        g.write(0x80000102, player, 1);
        g.write(0x80000103, 3, 1);
        check(character_stat(roster, guest, 0, 0x80000100, native) ==
                  (player < 2 ? ns->characters[player].behaviour.stat_bits[0] : native),
              "Same-donor stat ownership.");
        check(character_stat(roster, guest, 2, 0x80000100, native) == native, "Unchanged stat overridden.");
    }
    const auto cp = roster.checkpoint();
    const auto memory = guest;
    for (unsigned i = 0; i < 100000; ++i)
        check(character_stat(roster, guest, 0, 0x80000100, native) == native, "Stock hot-path changed.");
    check(guest == memory && roster.checkpoint() == cp, "Parameter evaluation changed checkpoint or guest memory.");
    roster = roster.stage_checkpoint(cp);
    g.write(0x80000100, 0, 2);
    g.write(0x80000103, 9, 1);
    check(character_stat(roster, guest, 0, 0x80000100, native) == native, "Other donor inherited custom stats.");
    g.write(0x80000100, 0xffff, 2);
    g.write(0x80000102, 0, 1);
    g.write(0x80000103, 3, 1);
    check(character_stat(roster, guest, 0, 0x80000100, native) == native, "AI/ghost aliased human stats.");
    std::array<std::uint8_t, 8> racers{3, 3, 3, 3};
    roster.assign_ai(racers, 3, 4, std::array<std::uint8_t, 1>{3}, [](unsigned) { return 0; });
    g.write(0x80000102, 3, 1);
    check(roster.ai(3) &&
              character_stat(roster, guest, 0, 0x80000100, native) == ns->characters[2].behaviour.stat_bits[0],
          "AI custom stats not applied.");
    g.write(0x80000100, 3, 2);
    check(character_stat(roster, guest, 0, 0x80000100, native) == native, "AI was treated as a human controller.");
    rejects([&] { character_stat(roster, guest, 3, 0x80000100, native); });
    rejects([&] { character_stat(roster, guest, 0, 0xffffffff, native); });
    rejects([&] { character_stat(roster, guest, 0, 0x80000fff, native); });
    namespace online = dkr::mods::online;
    online::Manifest m;
    m.revision = "us.v77";
    m.payloads.push_back({online::PayloadKind::Xdelta, std::string(64, 'b'), "us.v77", 32});
    m.content.push_back({online::Kind::Character, std::string(64, 'c'), "Custom", std::string(64, 'b'),
                         std::string(64, 'd'), std::string(64, 'c'), CharacterAdapterVersion, id, BehaviourAbi});
    check(online::decode(online::encode(m)) == m, "Online contract round trip.");
    const auto mid = online::identity(m);
    m.content[0].behaviour = behaviour_identity(c);
    check(online::identity(m) != mid, "Online identity omitted behaviour.");
    m.content[0].behaviour_abi = 0;
    rejects([&] { online::encode(m); });
    m.content[0].behaviour_abi = 99;
    rejects([&] { online::encode(m); });
    Bytes header(0x80);
    put(header, 12, std::bit_cast<std::uint32_t>(1.0f));
    auto modified = header;
    put(modified, 12, std::bit_cast<std::uint32_t>(1.2f));
    check(object_parameter_fields(header, modified) == 4, "Native header scale not recognized.");
    for (auto at : {0x10, 0x14, 0x53, 0x54, 0x55, 0x56, 0x57, 0x5a, 0x7f}) {
        auto invalid = modified;
        invalid[at] ^= 1;
        rejects([&] { object_parameter_fields(header, invalid); });
    }
    for (auto bits : {0U, 0x7f800000U, 0x7fc00000U, 0xbf800000U}) {
        auto invalid = modified;
        put(invalid, 12, bits);
        rejects([&] { object_parameter_fields(header, invalid); });
    }
    c.header_fields = 4;
    check(decode_behaviour(encode_behaviour(c)) == c, "Header capability round trip.");
    c.header_fields = 512;
    rejects([&] { validate_behaviour(c); });
    auto profile = reviewed();
    profile.changed_bytes = 123;
    profile.native_profile = 2;
    check(decode_behaviour(encode_behaviour(profile)) == profile, "Linked adapter contract round trip.");
    const auto pid = behaviour_identity(profile);
    profile.native_profile = 1;
    check(behaviour_identity(profile) != pid, "Linked adapter identity omitted profile.");
    profile.native_profile = 3;
    rejects([&] { validate_behaviour(profile); });
    profile.native_profile = 2;
    profile.unresolved_bytes = 1;
    rejects([&] { validate_behaviour(profile); });
    check(!linked_behaviour_profile(Bytes(922320)), "Unknown rebuilt ROM prefix accepted.");
}
void owned(const std::filesystem::path& path) {
    auto base = read_file(path, MaxImage);
    canonicalize_rom(base);
    const auto rev = verified_revision(base);
    const AssetImage original(base, rev);
    auto target = base;
    // An authored header difference identifies a real course; no patched ROM is
    // installed, persisted or booted by this headless fixture.
    const unsigned carrier = 3;
    const auto header = original.record(23, carrier);
    target.at(header.data() - base.data() + 0x52) ^= 1;
    const auto a = analyze(base, target, std::string(64, 'a'));
    check(!a.tracks.empty() && a.behaviour.abi == 0, "Asset-only course discovery.");
    for (unsigned site = 0; site < BehaviourSites; ++site) {
        const auto& s = behaviour_sites()[site];
        const auto pc = rev == "us.v77" ? s.pc77 : s.pc80;
        if (!pc)
            continue;
        const auto at = std::size_t(pc - 0x80000400U) + 0x1000;
        check(be32(base, at) == (rev == "us.v77" ? s.original77 : s.original80), "Reviewed revision source changed.");
        auto changed = target;
        put(changed, at, s.replacement);
        const auto b = analyze(base, changed, std::string(64, 'a'));
        check(b.asset_digest == a.asset_digest && b.behaviour.unresolved_bytes == 0 &&
                  b.behaviour.instructions[site] == s.replacement,
              "Reviewed code edit was not detected.");
        check(b.tracks[0].content_id != a.tracks[0].content_id, "Code-only editions collapsed.");
        const auto legacy = analyze(base, changed, std::string(64, 'a'), false);
        check(legacy.tracks[0].content_id == a.tracks[0].content_id && legacy.tracks[0].behaviour.abi == 0,
              "Explicit old-edition identity lost.");
    }
    auto stats = base;
    const auto table = original.record(15, 9);
    put(stats, table.data() - base.data(), std::bit_cast<std::uint32_t>(1.25f));
    const auto b = analyze(base, stats, std::string(64, 'a'));
    check(b.character_roots.size() == 1 && b.character_roots[0].base_character == 0 &&
              b.character_roots[0].behaviour.stat_mask == 1,
          "Stats-only custom character not detected.");
    const auto prepared = prepare_character(base, stats, std::string(64, 'a'), 0);
    check(validate_prepared_character(prepared, *AssetBank::stock(base)) == prepared.root.content_id,
          "Stats lost at materialization.");
    check(analyze(base, stats, std::string(64, 'a'), false).character_roots.empty(),
          "Asset-only policy accidentally enabled new stats.");
    auto scaled = base;
    const auto h = original.record(34, 2);
    put(scaled, h.data() - base.data() + 12, std::bit_cast<std::uint32_t>(1.2f));
    const auto scale = analyze(base, scaled, std::string(64, 'a'));
    check(scale.character_roots.size() == 1 && scale.character_roots[0].behaviour.header_fields == 4,
          "Object-parameter-only character not detected.");
    const auto larger = prepare_character(base, scaled, std::string(64, 'a'), 0);
    check(validate_prepared_character(larger, *AssetBank::stock(base)) == larger.root.content_id,
          "Object parameter materialization identity.");
    auto invalid = larger;
    invalid.root.behaviour.header_fields = 0;
    rejects([&] { validate_prepared_character(invalid, *AssetBank::stock(base)); });
    std::cout << rev << ": reviewed sites and data-only racer qualified without booting a game.\n";
}
void corpus(const std::filesystem::path& rom77, const std::filesystem::path& rom80,
            const std::filesystem::path& library) {
    const auto base77 = read_file(rom77, MaxImage), base80 = read_file(rom80, MaxImage);
    const auto stock77 = AssetBank::stock(base77), stock80 = AssetBank::stock(base80);
    const auto temp =
        std::filesystem::current_path() /
        ("behaviour-corpus-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(temp), "Cannot create private corpus directory.");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temp};
    const auto tracks_before = sha256(read_file(library / "track-catalog.json", MiB));
    const auto characters_before = sha256(read_file(library / "character-catalog.json", MiB));
    std::map<std::string, std::pair<nlohmann::json, std::filesystem::path>> patches;
    for (const auto& directory : std::filesystem::directory_iterator(library / "reviews")) {
        if (!directory.is_directory() || directory.is_symlink())
            continue;
        const auto bytes = read_file(directory.path() / "review.json", MiB);
        const auto doc = nlohmann::json::parse(bytes.begin(), bytes.end());
        for (const auto& package : doc.at("packages"))
            patches.try_emplace(package.at("patch_sha256").get<std::string>(), package, directory.path());
    }
    unsigned serial = 0, tracks = 0, characters = 0, asset_only = 0, adapted = 0, partial = 0, blocked = 0;
    for (const auto& [digest, source_package] : patches) {
        const auto& [package, review] = source_package;
        std::cout << "Checking " << package.value("display_name", digest) << std::endl;
        const auto patch = read_file(review / "patches" / (digest + ".xdelta"), MaxPatch);
        check(sha256(patch) == digest, "Corpus patch integrity failed.");
        ImportPreparation source;
        source.bases.emplace("us.v77", base77);
        source.bases.emplace("us.v80", base80);
        reconstruct_import(source, patch);
        const auto& a = source.analysis;
        check(a.blockers.empty(), "Existing corpus no longer passes analysis.");
        if (!a.behaviour.abi)
            ++asset_only;
        else if (a.behaviour.unresolved_bytes)
            ++partial;
        else
            ++adapted;
        const auto& base = source.bases.at(a.source_revision);
        const auto source_stock = a.source_revision == "us.v77" ? stock77 : stock80;
        for (const auto& root : a.character_roots) {
            if (!root.blockers.empty()) {
                ++blocked;
                std::cout << "  blocked character: " << root.name << ": " << root.blockers.front() << std::endl;
                continue;
            }
            const auto c = prepare_character(base, source.target, digest, root.base_character);
            const auto folder = temp / std::to_string(serial++);
            const auto hash = write_character_artifact(folder, c, *source_stock);
            for (const auto& stock : {stock77, stock80}) {
                const auto loaded = read_character_artifact(folder, hash, *stock);
                check(loaded.root.behaviour == c.root.behaviour,
                      "Character contract changed across revision/persistence.");
                const auto ns = allocate_characters(stock, {loaded});
                const auto augmented = ns.apply(stock);
                for (unsigned section : {4U, 34U})
                    for (unsigned i = 0; i < stock->record_count(section); ++i)
                        check(std::ranges::equal(stock->record(section, i), augmented->record(section, i)),
                              "Custom racer changed an original header/portrait.");
                RuntimeSession session(stock, std::make_shared<CharacterNamespace>(ns));
                check(session.instantiate()->checkpoint() == session.checkpoint(),
                      "Character behaviour world instantiation drift.");
            }
            ++characters;
        }
        for (const auto& root : a.tracks)
            for (const auto& native : {base77, base80}) {
                if (!root.blockers.empty()) {
                    ++blocked;
                    std::cout << "  blocked track: " << root.name << ": " << root.blockers.front() << std::endl;
                    continue;
                }
                const auto track = prepare_track_bank(base, source.target, digest, root.carrier, native);
                const auto stock = verified_revision(native) == "us.v77" ? stock77 : stock80;
                const auto folder = temp / std::to_string(serial++);
                const auto hash = write_track_artifact(folder, track, digest, a.profile);
                const auto loaded = read_track_artifact(folder, hash, stock);
                check(loaded.root.behaviour == root.behaviour && loaded.root.content_id == root.content_id,
                      "Track contract changed across revision/persistence.");
                RuntimeSession session(stock);
                session.admit(loaded);
                check(session.instantiate()->checkpoint() == session.checkpoint(),
                      "Track behaviour world instantiation drift.");
                ++tracks;
            }
        std::cout << package.value("display_name", digest) << ": characters=" << a.character_roots.size()
                  << " tracks=" << a.tracks.size() << " " << behaviour_summary(a.behaviour) << std::endl;
    }
    check(tracks_before == sha256(read_file(library / "track-catalog.json", MiB)) &&
              characters_before == sha256(read_file(library / "character-catalog.json", MiB)),
          "User activation changed during qualification.");
    std::cout << patches.size() << " retained patches: " << asset_only << " asset-only, " << adapted << " reviewed, "
              << partial << " partial; " << characters << " characters and " << tracks
              << " revision-specific tracks qualified; " << blocked
              << " explicitly blocked roots/variants. Original library unchanged.\n";
}
// Explicit local research output only, never invoked by CTest or packaging.
// Prefix bytes contain copyrighted game data: keep them private and do not
// include them in a release. Sources/reviews are read-only.
void inspect_code(const std::filesystem::path& rom77, const std::filesystem::path& rom80,
                  const std::filesystem::path& library, const std::filesystem::path& output) {
    if (!std::filesystem::create_directory(output))
        throw Error("Inspection destination must be new.");
    ImportPreparation source;
    source.bases.emplace("us.v77", read_file(rom77, MaxImage));
    source.bases.emplace("us.v80", read_file(rom80, MaxImage));
    std::set<std::string> seen;
    nlohmann::json report = nlohmann::json::array();
    for (const auto& dir : std::filesystem::directory_iterator(library / "reviews")) {
        if (!dir.is_directory() || dir.is_symlink())
            continue;
        const auto raw = read_file(dir.path() / "review.json", 8 * MiB);
        const auto review = nlohmann::json::parse(raw.begin(), raw.end());
        for (const auto& package : review.at("packages")) {
            const auto digest = package.at("patch_sha256").get<std::string>();
            if (!seen.insert(digest).second)
                continue;
            const auto patch = read_file(dir.path() / "patches" / (digest + ".xdelta"), MaxPatch);
            if (sha256(patch) != digest)
                throw Error("Patch hash mismatch.");
            reconstruct_import(source, patch);
            const auto& a = source.analysis;
            if (!a.behaviour.unresolved_bytes)
                continue;
            const AssetImage image(source.target, a.source_revision);
            write_new_file(output / (digest + ".prefix"), View(source.target).first(image.lut_start));
            report.push_back({{"patch", digest},
                              {"target", a.target_digest},
                              {"name", package.value("display_name", digest)},
                              {"revision", a.source_revision},
                              {"lut", image.lut_start},
                              {"analysis", nlohmann::json::parse(analysis_json(a))}});
        }
    }
    const auto json = report.dump(2);
    write_new_file(output / "report.json", View(reinterpret_cast<const std::uint8_t*>(json.data()), json.size()));
    std::cout << report.size() << " private prefixes written; not distributable.\n";
}
void verify_linked(const std::filesystem::path& folder) {
    const auto raw = read_file(folder / "proof-r2.json", MiB);
    const auto proof = nlohmann::json::parse(raw.begin(), raw.end());
    check(proof.size() == 18, "Linked reference corpus is incomplete.");
    for (const auto& row : proof) {
        check(row.at("issues").empty(), "Reference audit has unresolved differences.");
        const auto prefix = read_file(folder / (row.at("patch").get<std::string>() + ".prefix"), MiB);
        const unsigned expected = row.at("delta").get<unsigned>() ? 2 : 1;
        check(linked_behaviour_profile(prefix) == expected, "Exact linked profile failed admission.");
        for (unsigned i = 0; i < 16; ++i) {
            auto altered = prefix;
            altered.at((i * 61541U + 16) % altered.size()) ^= 1;
            check(!linked_behaviour_profile(altered), "Unknown code/data mutation inherited a linked profile.");
        }
    }
    std::cout << "18 linked reference prefixes and 288 unknown mutations qualified.\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        contracts();
        animation_profile();
        if (argc == 3 && std::string_view(argv[1]) == "--verify-linked")
            verify_linked(utf8_path(argv[2]));
        else if (argc == 6 && std::string_view(argv[1]) == "--inspect-code")
            inspect_code(utf8_path(argv[2]), utf8_path(argv[3]), utf8_path(argv[4]), utf8_path(argv[5]));
        else if (argc == 5 && std::string_view(argv[1]) == "--corpus")
            corpus(utf8_path(argv[2]), utf8_path(argv[3]), utf8_path(argv[4]));
        else
            for (int i = 1; i < argc; ++i)
                owned(utf8_path(argv[i]));
        std::cout << checks << " behaviour safety/identity/ownership checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
