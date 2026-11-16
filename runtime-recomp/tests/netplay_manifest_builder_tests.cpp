// Golden manifests for netplay/netplay_manifest_builder.cpp.
//
// The expected hashes were produced by the BuildNetplayManifest that lived in
// runtime_ui.cpp before it moved, over the same inputs. The two default cases
// are also what a built game logged for a fresh profile on each ROM revision
// (arm64, release 1.0.15): manifest hash 3DAC5A0935D9E9D3 / class hash
// 062F7805BEEC0FBA on v77 and ACA6E414949F8BDB / 1F8BFBC82BA29CB8 on v80.
//
// They change on purpose when the protocol version, the authority schema or
// another network ABI constant behind the build fingerprint changes, and they
// must not change otherwise.
#include "netplay/netplay_manifest_builder.hpp"

#include "netplay/determinism_contract.hpp"
#include "rom_revision.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace dkr::runtime::netplay;

namespace {

constexpr std::string_view kPolicy = "v77:c4deefd55aca1dc1af1391a86d5df366624dabed2827a0fab055e8ffad8cd702|"
                                     "v80:b8a89ed014f6e6a7f1113adf72fe8c92b1f8cf8323ac4c35de00a258c28adb9b";
constexpr const char* kEngine77 = "8a5a0d3005a50f2f12d332346c804df6bfa277fd05650e48a639a64796db09c3";
constexpr const char* kEngine80 = "ef12b5452a0ea54b4a1562aef21fd62d11bcd19b0b85122e818d8b350857b6a0";

struct Golden {
    const char* name;
    ManifestInputs inputs;
    std::string mod_manifest_hash;
    std::uint64_t manifest;
    std::uint64_t klass;
    std::uint64_t settings;
    std::uint64_t policy;
    std::uint64_t save;
};

ManifestInputs make(std::string_view release, Revision revision, std::uint64_t rom, std::string_view policy,
                    std::uint32_t codes, std::string_view architecture, std::string contract, std::string engine) {
    ManifestInputs inputs;
    inputs.source_release_version = release;
    inputs.revision = revision;
    inputs.canonical_rom_hash = rom;
    inputs.patch_policy = policy;
    inputs.magic_codes = codes;
    inputs.architecture = architecture;
    inputs.floating_point_mode = std::move(contract);
    inputs.owned_engine_hash = std::move(engine);
    return inputs;
}

} // namespace

int main() {
    using dkr::runtime::rom::kUsV77Xxh3;
    using dkr::runtime::rom::kUsV80Xxh3;
    const std::string strict = "strict-ieee754-v1";
    const std::vector<std::uint8_t> save = [] {
        std::vector<std::uint8_t> bytes(16);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>(0xA0 + i);
        return bytes;
    }();

    auto with_save = make("1.0.15", Revision::UsV80, kUsV80Xxh3, kPolicy, 0x402U, "x86_64", strict, kEngine80);
    with_save.adventure_save = std::span<const std::uint8_t>(save);

    const Golden cases[] = {
        {"v77 default arm64", make("1.0.15", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "arm64", strict, kEngine77), "",
         0x3DAC5A0935D9E9D3ULL, 0x062F7805BEEC0FBAULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"v80 default arm64", make("1.0.15", Revision::UsV80, kUsV80Xxh3, kPolicy, 0, "arm64", strict, kEngine80), "",
         0xACA6E414949F8BDBULL, 0x1F8BFBC82BA29CB8ULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"v77 x86_64: same class hash, other manifest hash",
         make("1.0.15", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "x86_64", strict, kEngine77), "",
         0x0913B79F30633A9FULL, 0x062F7805BEEC0FBAULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"v77 unverified architecture",
         make("1.0.15", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "riscv64", "unverified-riscv64", kEngine77), "",
         0x0ABE0182F35A73DAULL, 0xBCD9C157EA5992C6ULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"v80 magic codes, adventure save, mods", with_save, std::string(64, 'a'), 0xBE7B6EBEB7AEF625ULL,
         0xDD417D4FFF72240CULL, 0x447261C7975AD9D5ULL, 0xD3BAF229D6234A6CULL, 0x51438A6AF17BCAF5ULL},
        {"v77 without the owned engine (host preset)",
         make("1.0.15", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "arm64", strict, ""), "", 0x7927E3EE8725FCD2ULL,
         0xA0078578313904FBULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"release version is trimmed",
         make(" 1.0.16-rc\n", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "arm64", strict, kEngine77), "",
         0x09152167525355CBULL, 0xBEDD2CB4C304AEF2ULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"empty release version is development",
         make("", Revision::UsV77, kUsV77Xxh3, kPolicy, 0, "arm64", strict, kEngine77), "", 0x1D83A0CBECABA841ULL,
         0xAA0DD9F93E4EBB44ULL, 0x53DA1871B26E44E0ULL, 0xD3BAF229D6234A6CULL, 0},
        {"another patch policy",
         make("1.0.15", Revision::UsV77, kUsV77Xxh3, "v77:00|v80:11", 0, "arm64", strict, kEngine77), "",
         0xA1BEA9A29B286CD6ULL, 0x854FF4EE31208FE5ULL, 0x53DA1871B26E44E0ULL, 0xBD8A0849DB74C007ULL, 0},
    };

    for (const auto& golden : cases) {
        auto manifest = build_netplay_manifest(golden.inputs);
        manifest.mod_manifest_hash = golden.mod_manifest_hash; // the lobby sets this, not the builder
        const bool ok = manifest_hash(manifest) == golden.manifest && manifest_class_hash(manifest) == golden.klass &&
                        manifest.gameplay_settings_hash == golden.settings &&
                        manifest.patch_policy_hash == golden.policy && manifest.session_save_hash == golden.save;
        if (!ok)
            std::fprintf(stderr, "%s: manifest %016llX class %016llX settings %016llX policy %016llX save %016llX\n",
                         golden.name, static_cast<unsigned long long>(manifest_hash(manifest)),
                         static_cast<unsigned long long>(manifest_class_hash(manifest)),
                         static_cast<unsigned long long>(manifest.gameplay_settings_hash),
                         static_cast<unsigned long long>(manifest.patch_policy_hash),
                         static_cast<unsigned long long>(manifest.session_save_hash));
        assert(ok);
    }

    // Fields the builder copies or fixes, read straight off one manifest.
    const auto manifest = build_netplay_manifest(cases[4].inputs);
    assert(manifest.protocol_version == kProtocolVersion);
    assert(manifest.release_version == "1.0.15");
    assert(manifest.build_fingerprint.starts_with("DKR-R/1.0.15/netplay-protocol-"));
    assert(manifest.revision == Revision::UsV80 && manifest.canonical_rom_hash == kUsV80Xxh3);
    assert(manifest.magic_codes_hash == 0x402U && manifest.simulation_rate == 30U);
    assert(manifest.architecture == "x86_64" && manifest.floating_point_mode == strict);
    assert(manifest.owned_engine_hash == kEngine80);
    assert(manifest.mod_manifest_hash.empty() && manifest.owned_resources_hash.empty());

    // An empty save is a save (its hash is the empty-string hash), not a missing one.
    auto empty_save = cases[0].inputs;
    empty_save.adventure_save = std::span<const std::uint8_t>();
    assert(build_netplay_manifest(empty_save).session_save_hash == stable_hash({}));
    assert(build_netplay_manifest(cases[0].inputs).session_save_hash == 0U);

    // Each input moves the manifest hash, and only the architecture leaves the class hash alone.
    const auto base = build_netplay_manifest(cases[0].inputs);
    const auto moved = [&](auto change) {
        auto inputs = cases[0].inputs;
        change(inputs);
        return build_netplay_manifest(inputs);
    };
    assert(manifest_hash(moved([](ManifestInputs& i) { i.magic_codes = 1; })) != manifest_hash(base));
    assert(manifest_hash(moved([](ManifestInputs& i) { i.canonical_rom_hash ^= 1; })) != manifest_hash(base));
    assert(manifest_hash(moved([](ManifestInputs& i) { i.patch_policy = "x"; })) != manifest_hash(base));
    assert(manifest_hash(moved([](ManifestInputs& i) { i.owned_engine_hash = "x"; })) != manifest_hash(base));
    const auto other_arch = moved([](ManifestInputs& i) { i.architecture = "x86_64"; });
    assert(manifest_hash(other_arch) != manifest_hash(base));
    assert(manifest_class_hash(other_arch) == manifest_class_hash(base));
    std::puts("netplay manifest builder goldens passed");
    return 0;
}
