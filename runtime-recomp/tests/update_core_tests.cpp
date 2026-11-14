#include "update_core.hpp"
#include "update_trust.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <json/json.hpp>
#include <monocypher-ed25519.h>

using namespace dkr::updates;
using Json = nlohmann::json;
namespace {
void rejects(const std::function<void()>& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
}
void put(const std::filesystem::path& p, std::string_view s) {
    std::filesystem::create_directories(p.parent_path());
    durable_write(p, s);
}
File file(std::string path, std::string_view value) {
    return {path,
            hash(std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size())),
            {},
            value.size(),
            0644};
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    // Package qualification uses the same native extractor as launcher staging,
    // not Python's ZIP reader. This executable is not included in releases.
    if (argc == 6 && std::filesystem::path(argv[1]) == "--stage-signed") {
        try {
            const auto manifest = verify_manifest(read_text(argv[3]), read_text(argv[4], 128), kUpdatePublicKey);
            const auto* artifact = manifest.find(platform());
            if (!artifact || artifact->kind != "zip")
                throw std::runtime_error("No signed ZIP for this test platform.");
            extract_verified(argv[2], *artifact, argv[5]);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    assert(Version::parse("1.10.0").compare(Version::parse("1.9.99")) > 0);
    assert(Version::parse("1.0.7").compare(Version::parse("1.0.7-rc2")) > 0);
    assert(Version::parse("1.0.7-beta.10").compare(Version::parse("1.0.7-beta.9")) > 0);
    assert(Version::parse("1.0.7+build.2").compare(Version::parse("1.0.7+build.1")) == 0);
    assert(Version::parse("1.0.7-beta.999999999999999999999999999").compare(Version::parse("1.0.7-beta.2")) > 0);
    for (auto s : {"Version1.0.6", "1.0.06", "1.0", "1.0.7-01", "1.0.7-", "1.0.7+", "-1.0.7"})
        rejects([&] { Version::parse(s); });
    for (auto s :
         {"../save.eep", "a/../b", "C:/x", "a\\b", "/file", "NUL.txt", "com1.dll", "a//b", "a. /b", "x:", "file/"})
        assert(!safe_relative(s));
    for (auto s : {"portable.txt", "dkr-runtime-data/config", "mods/pack", "saves/offline.eep", "assets/ROM.z64",
                   ".dkr-update-transaction/journal.json"})
        assert(protected_path(s));
    assert(safe_relative("assets/ui/Icons/DKR-R.png"));
    assert(!protected_path("assets/updates/install.json"));
    for (auto s : {"http://github.com/x", "https://github.com.evil/x", "https://github.com@evil/x",
                   "https://127.0.0.1/x", "https://github.com:443/x", "file:///x"})
        assert(!allowed_https_url(s));
    assert(allowed_https_url("https://release-assets.githubusercontent.com/file?sig=example"));
    assert(hash({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::array<std::uint8_t, 32> seed{}, pub{};
    std::array<std::uint8_t, 64> secret{}, sig{};
    seed[0] = 73;
    crypto_ed25519_key_pair(secret.data(), pub.data(), seed.data());
    Json j = {
        {"schema", 1},
        {"version", "1.0.7"},
        {"tag", "Version1.0.7"},
        {"channel", "stable"},
        {"notes", "https://github.com/ThatGuyMcd/DKR-R/releases/tag/Version1.0.7"},
        {"commit", std::string(40, 'a')},
        {"minimum_updater", 1},
        {"artifacts",
         Json::array(
             {{{"platform", "windows-x64"},
               {"name", "DKR-R-1.0.7-Windows-x64.zip"},
               {"kind", "zip"},
               {"bytes", 100},
               {"sha256", std::string(64, 'b')},
               {"root", "DKR-R-1.0.7-Windows-x64"},
               {"files", Json::array({{{"path", "DKR-R.exe"}, {"bytes", 3}, {"sha256", std::string(64, 'c')}}})}}})}};
    for (const auto* path :
         {"DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll", "assets/updates/install.json",
          "libexec/dkr-r/DKR-R-Updater.exe", "libexec/dkr-r/DKR-R-ModWorker.exe", "libexec/dkr-r/DKR-R-InputHost.exe",
          "libexec/dkr-r/SDL3.dll"})
        j["artifacts"][0]["files"].push_back(Json{{"path", path}, {"bytes", 3}, {"sha256", std::string(64, 'c')}});
    auto signed_parse = [&](std::string bytes) {
        crypto_ed25519_sign(sig.data(), secret.data(), reinterpret_cast<const std::uint8_t*>(bytes.data()),
                            bytes.size());
        return verify_manifest(bytes, hex(sig), hex(pub));
    };
    assert(signed_parse(j.dump()).find("windows-x64"));
    auto private_layout = j;
    for (auto& entry : private_layout["artifacts"][0]["files"]) {
        const auto path = entry["path"].get<std::string>();
        if (path == "DKR-R-Runtime.exe" || path == "SDL2.dll" || path == "dxcompiler.dll" || path == "dxil.dll")
            entry["path"] = "libexec/dkr-r/" + path;
    }
    assert(signed_parse(private_layout.dump()).find("windows-x64"));
    auto mixed_layout = private_layout;
    for (auto& entry : mixed_layout["artifacts"][0]["files"])
        if (entry["path"] == "libexec/dkr-r/SDL2.dll")
            entry["path"] = "SDL2.dll";
    rejects([&] { signed_parse(mixed_layout.dump()); });
    auto encoded = j.dump();
    crypto_ed25519_sign(sig.data(), secret.data(), reinterpret_cast<const std::uint8_t*>(encoded.data()),
                        encoded.size());
    rejects([&] { verify_manifest(encoded + " ", hex(sig), hex(pub)); });
    auto bad = j;
    bad["artifacts"][0]["files"][0]["path"] = "portable.txt";
    rejects([&] { signed_parse(bad.dump()); });
    bad = j;
    bad["artifacts"][0]["files"][0]["path"] = "../save.eep";
    rejects([&] { signed_parse(bad.dump()); });
    bad = j;
    bad["version"] = "1.0.7-beta.1";
    rejects([&] { signed_parse(bad.dump()); });
    bad = j;
    bad["minimum_updater"] = 2;
    rejects([&] { signed_parse(bad.dump()); });
    bad = j;
    bad["artifacts"][0]["kind"] = "apk";
    rejects([&] { signed_parse(bad.dump()); });
    auto duplicate = j.dump();
    duplicate.insert(1, "\"schema\":1,");
    rejects([&] { signed_parse(duplicate); });
    auto root = std::filesystem::temp_directory_path() /
                std::filesystem::path("dkrr-updates-test-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::filesystem::remove_all(p);
        }
    } cleanup{root};
    // Deflated ZIP fixture, including both a Unicode source and staging path.
    const auto zipped = unhex("504b030414000000080000002150c3760bf108000000060000000f0000007570646174652f746573742e"
                              "646c6c2b4ecc2dc8490500504b0102140014000000080000002150c3760bf108000000060000000f0000"
                              "0000000000000000008001000000007570646174652f746573742e646c6c504b05060000000001000100"
                              "3d000000350000000000",
                              136);
    auto unicode = root / std::filesystem::path(u8"café");
    std::filesystem::create_directory(unicode);
    put(unicode / "package.zip", std::string_view(reinterpret_cast<const char*>(zipped.data()), zipped.size()));
    Artifact archive;
    archive.root = "update";
    archive.kind = "zip";
    archive.bytes = zipped.size();
    archive.sha256 = hash(zipped);
    archive.files = {file("test.dll", "sample")};
    extract_verified(unicode / "package.zip", archive, unicode / std::filesystem::path(u8"staging é"));
    assert(read_text(unicode / std::filesystem::path(u8"staging é") / "test.dll") == "sample");
    auto install = root / "install", stage = root / "stage";
    std::filesystem::create_directory(install);
    std::filesystem::create_directory(stage);
    auto receipt =
        Json{{"schema", 1}, {"version", "1.0.6"}, {"files", {"DKR-R.exe", "old.dll", "assets/updates/install.json"}}}
            .dump();
    put(install / "DKR-R.exe", "old-exe");
    put(install / "old.dll", "old-dll");
    put(install / "assets/updates/install.json", receipt);
    put(install / "portable.txt", "portable");
    put(install / "dkr-runtime-data/saves/offline.eep", "offline-save");
    put(install / "personal.txt", "personal");
    auto next =
        Json{{"schema", 1}, {"version", "1.0.7"}, {"files", {"DKR-R.exe", "new.dll", "assets/updates/install.json"}}}
            .dump();
    put(stage / "DKR-R.exe", "new-exe");
    put(stage / "new.dll", "new-dll");
    put(stage / "assets/updates/install.json", next);
    Artifact a;
    a.files = {file("DKR-R.exe", "new-exe"), file("new.dll", "new-dll"), file("assets/updates/install.json", next)};
    install_files(install, stage, a);
    assert(read_text(install / "DKR-R.exe") == "new-exe");
    assert(!std::filesystem::exists(install / "old.dll"));
    assert(read_text(install / "dkr-runtime-data/saves/offline.eep") == "offline-save");
    assert(read_text(install / "personal.txt") == "personal");
    recover_files(install);
    recover_files(install);
    assert(read_text(install / "DKR-R.exe") == "old-exe");
    assert(read_text(install / "old.dll") == "old-dll");
    assert(!std::filesystem::exists(install / "new.dll"));
    assert(read_text(install / "portable.txt") == "portable");
    assert(read_text(install / "assets/updates/install.json") == receipt);
    archive_transaction(install);
    assert(!pending_files(install));
    put(stage / "DKR-R.exe", "new-exe");
    put(stage / "new.dll", "new-dll");
    put(stage / "assets/updates/install.json", next);
    auto conflict = a;
    conflict.files.push_back(file("personal.txt", "replacement"));
    put(stage / "personal.txt", "replacement");
    rejects([&] { install_files(install, stage, conflict); });
    assert(read_text(install / "personal.txt") == "personal");
    install_files(install, stage, a);
    auto data = root / "profile";
    std::filesystem::create_directories(data / "updates");
    std::string token(32, 'a');
    authorize_updated_launch(install, token, data, "1.0.7");
    assert(updated_launch_authorized(install, token));
    assert(!finalize_healthy_update(install));
    put(data / "updates" / ("health-" + token + ".json"),
        Json{{"id", token}, {"version", "1.0.7"}, {"ready", true}}.dump());
    assert(finalize_healthy_update(install));
    assert(!pending_files(install));
    assert(read_text(install / "DKR-R.exe") == "new-exe");
    std::cout << "PASS: versions, signatures, paths, ownership, save protection and idempotent "
                 "rollback\n";
}
