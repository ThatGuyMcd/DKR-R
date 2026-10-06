#include "save_manager.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t pak_checksum(std::vector<std::uint8_t> bytes) {
    std::fill(bytes.begin() + 16U, bytes.begin() + 20U, 0U);
    std::uint32_t hash = 2166136261U;
    for (const std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

std::vector<std::uint8_t> valid_pak() {
    std::vector<std::uint8_t> bytes(32U * 1024U, 0U);
    const std::array<std::uint8_t, 8> magic{
        'D', 'K', 'R', 'M', 'P', 'K', '1', 0};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_u32(bytes, 8U, 1U);
    write_u32(bytes, 12U, 1U);
    write_u32(bytes, 16U, pak_checksum(bytes));
    return bytes;
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("dkr-save-manager-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    dkr::runtime::saves::configure(root);
    std::fputs("[test][save-manager] adventure lifecycle\n", stderr);
    assert(!dkr::runtime::saves::adventure_info().exists);

    std::string error;
    if (!dkr::runtime::saves::reset_adventure(error)) {
        std::fprintf(stderr, "reset failed: %s\n", error.c_str());
        assert(false);
    }
    const auto info = dkr::runtime::saves::adventure_info();
    assert(info.exists && info.valid && info.size == 0x200U);

    std::filesystem::path backup;
    assert(dkr::runtime::saves::backup_adventure(backup, error));
    assert(std::filesystem::exists(backup));
    assert(!dkr::runtime::saves::adventure_backups().empty());

    const auto exported = root / "exported.bin";
    assert(dkr::runtime::saves::export_adventure(exported, error));
    assert(dkr::runtime::saves::import_adventure(exported, error));

    // Retail saves can contain a never-used 0xFF adventure slot. It is valid
    // game data and must remain importable/editable even though it is not the
    // canonical checksummed blank emitted by the Save Builder.
    auto erased_slot_adventure = dkr::runtime::saves::codec::blank_bytes();
    std::fill(erased_slot_adventure.begin() + 0x28U,
              erased_slot_adventure.begin() + 0x50U, 0xFFU);
    const auto erased_slot = root / "retail-erased-slot.bin";
    write_bytes(erased_slot, erased_slot_adventure);
    assert(dkr::runtime::saves::import_adventure(erased_slot, error));
    assert(dkr::runtime::saves::adventure_info().valid);
    dkr::runtime::saves::codec::SaveImage erased_slot_image{};
    assert(dkr::runtime::saves::load_adventure(erased_slot_image, error));
    assert(erased_slot_image.slots[1].name.empty());

    std::fputs("[test][save-manager] checksum-only recovery\n", stderr);
    const auto live_save = dkr::runtime::saves::adventure_info().path;
    auto corrupt_live = read_bytes(live_save);
    corrupt_live[0] ^= 0x5AU;
    corrupt_live[0x78U] ^= 0x40U;
    write_bytes(live_save, corrupt_live);
    assert(!dkr::runtime::saves::adventure_info().valid);
    bool repaired = false;
    std::filesystem::path repair_backup;
    assert(dkr::runtime::saves::repair_adventure_checksums(
        repaired, repair_backup, error));
    assert(repaired && std::filesystem::exists(repair_backup));
    assert(read_bytes(repair_backup) == corrupt_live);
    assert(dkr::runtime::saves::adventure_info().valid);
    std::vector<std::uint8_t> canonical_live;
    assert(dkr::runtime::saves::canonical_adventure_bytes(
        canonical_live, error));
    assert(canonical_live == dkr::runtime::saves::codec::blank_bytes());
    repaired = true;
    repair_backup.clear();
    assert(dkr::runtime::saves::repair_adventure_checksums(
        repaired, repair_backup, error));
    assert(!repaired && repair_backup.empty());

    const auto invalid = root / "invalid.bin";
    std::ofstream(invalid, std::ios::binary).put('x');
    assert(!dkr::runtime::saves::import_adventure(invalid, error));
    const auto bad_checksum = root / "bad-checksum.bin";
    auto corrupt_adventure = dkr::runtime::saves::codec::blank_bytes();
    corrupt_adventure[12] ^= 1U;
    write_bytes(bad_checksum, corrupt_adventure);
    assert(dkr::runtime::saves::import_adventure(bad_checksum, error));
    assert(dkr::runtime::saves::adventure_info().valid);
    assert(read_bytes(dkr::runtime::saves::adventure_info().path)[12] ==
           corrupt_adventure[12]);

    std::fputs("[test][save-manager] isolated online saves\n", stderr);
    auto host_image = dkr::runtime::saves::codec::blank_image();
    host_image.slots[0].name = "NET";
    host_image.slots[0].balloons = {1, 0, 0, 0, 0, 0};
    const auto host_save = dkr::runtime::saves::codec::encode(host_image);
    const auto single_player_before_online = read_bytes(
        dkr::runtime::saves::adventure_info().path);
    const std::size_t backups_before_online =
        dkr::runtime::saves::adventure_backups().size();
    std::vector<std::uint8_t> seeded_online;
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer,
        seeded_online, error));
    assert(seeded_online == single_player_before_online);
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) ==
           single_player_before_online);

    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::Fresh,
        seeded_online, error));
    assert(seeded_online == dkr::runtime::saves::codec::blank_bytes());
    assert(!dkr::runtime::saves::previous_online_adventure_info().exists);
    std::vector<std::uint8_t> activated_host;
    std::filesystem::path activated_host_path;
    assert(dkr::runtime::saves::read_online_adventure(true, 1U, activated_host, activated_host_path, error));
    assert(activated_host == seeded_online);
    const auto host_online_info =
        dkr::runtime::saves::previous_online_adventure_info();
    assert(host_online_info.exists && host_online_info.valid);
    write_bytes(host_online_info.path, host_save);
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::Fresh, seeded_online, error));
    // Simulate failed/cancelled lobby creation: the old file must be untouched.
    assert(read_bytes(host_online_info.path) == host_save);
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession,
        seeded_online, error));
    assert(seeded_online == host_save);

    constexpr std::uint64_t match_id = 0x1234ABCDEF987654ULL;
    std::filesystem::path installed_online;
    assert(dkr::runtime::saves::install_synchronized_online_adventure(
        match_id, host_save, installed_online, error));
    assert(read_bytes(installed_online) == host_save);
    assert(installed_online.parent_path().filename() == "1234abcdef987654");
    std::vector<std::uint8_t> readback_online;
    std::filesystem::path readback_path;
    assert(dkr::runtime::saves::read_online_adventure(
        false, match_id, readback_online, readback_path, error));
    assert(readback_path == installed_online && readback_online == host_save);
    assert(!dkr::runtime::saves::install_synchronized_online_adventure(
        0U, host_save, installed_online, error));
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) ==
           single_player_before_online);
    assert(dkr::runtime::saves::adventure_backups().size() ==
           backups_before_online);

    // Modded host progress is keyed by the frozen manifest, never retail or
    // another mod set; clients still use their authenticated session folder.
    const std::string mod_a(64,'a'),mod_b(64,'b');
    assert(!dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::Fresh,seeded_online,error,"../escape"));
    assert(!dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession,seeded_online,error,mod_a));
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer,seeded_online,error,mod_a));
    assert(seeded_online==single_player_before_online);
    assert(dkr::runtime::saves::bind_staged_host_online_adventure(20,error));
    assert(dkr::runtime::saves::read_online_adventure(true,20,activated_host,activated_host_path,error));
    assert(activated_host_path==root/"saves"/"online"/"host"/"mod-profiles"/mod_a/"dkr.us.v77.bin");
    assert(dkr::runtime::saves::commit_online_adventure(true,20,host_save,error));
    assert(dkr::runtime::saves::previous_online_adventure_info(mod_a).valid);
    assert(!dkr::runtime::saves::previous_online_adventure_info(mod_b).exists);
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession,seeded_online,error,mod_a));
    assert(seeded_online==host_save && read_bytes(host_online_info.path)==host_save);
    dkr::runtime::saves::discard_staged_host_online_adventure();
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession,seeded_online,error));
    assert(seeded_online==host_save);
    assert(read_bytes(dkr::runtime::saves::adventure_info().path)==single_player_before_online);

    // Joining cannot grant any operation permission to replace personal saves.
    dkr::runtime::saves::set_online_save_protection(true);
    assert(!dkr::runtime::saves::reset_adventure(error));
    assert(!dkr::runtime::saves::import_adventure(host_online_info.path, error));
    assert(!dkr::runtime::saves::export_stored_adventure(host_online_info.path,
        dkr::runtime::saves::adventure_info().path, error));
    assert(!dkr::runtime::saves::export_bundle(dkr::runtime::saves::adventure_info().path, error));
    assert(!dkr::runtime::saves::export_bundle(root / "save-history" / "controller-pak-1.mpk" / "history.bin", error));
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) == single_player_before_online);
    std::vector<std::uint8_t> online_paks;
    assert(dkr::runtime::saves::read_experimental_online_paks(false, match_id, online_paks, error));
    assert(dkr::runtime::saves::commit_experimental_online_paks(false, match_id, online_paks, error));
    // The deliberately rejected zero-id install cleared its output argument;
    // the successful readback remains the authoritative client session path.
    assert(std::filesystem::exists(readback_path.parent_path() / "experimental-controller-paks.bin"));
    const auto inventory = dkr::runtime::saves::stored_adventures();
    assert(std::any_of(inventory.begin(), inventory.end(), [](const auto& record) {
        return record.scope == 1 && record.info.valid;
    }));
    assert(std::any_of(inventory.begin(), inventory.end(), [](const auto& record) {
        return record.scope == 1 && record.info.valid &&
            record.kind == dkr::runtime::saves::StoredSaveKind::ExperimentalPaks;
    }));
    dkr::runtime::saves::set_online_save_protection(false);

    dkr::runtime::saves::codec::SaveImage editable{};
    assert(dkr::runtime::saves::load_adventure(editable, error));
    editable.slots[0].name = "DKR";
    editable.slots[0].balloons = {47, 8, 8, 8, 8, 8};
    assert(dkr::runtime::saves::commit_adventure(editable, error));
    dkr::runtime::saves::codec::SaveImage reloaded{};
    assert(dkr::runtime::saves::load_adventure(reloaded, error));
    assert(reloaded.slots[0].name == "DKR");
    assert(reloaded.slots[0].balloons[0] == 47);
    editable.slots[0].balloons[1] = 9;
    assert(!dkr::runtime::saves::commit_adventure(editable, error));

    std::fputs("[test][save-manager] controller pak lifecycle\n", stderr);
    const auto pak_source = root / "source.mpk";
    write_bytes(pak_source, valid_pak());
    assert(dkr::runtime::saves::import_controller_pak(0, pak_source, error));
    const auto pak_info = dkr::runtime::saves::controller_pak_info(0);
    assert(pak_info.exists && pak_info.valid && pak_info.size == 32U * 1024U);
    std::filesystem::path pak_backup;
    assert(dkr::runtime::saves::backup_controller_pak(0, pak_backup, error));
    assert(std::filesystem::exists(pak_backup));
    assert(!dkr::runtime::saves::controller_pak_backups(0).empty());
    const auto exported_pak = root / "exported.mpk";
    assert(dkr::runtime::saves::export_controller_pak(0, exported_pak, error));
    assert(!dkr::runtime::saves::export_controller_pak(0, dkr::runtime::saves::adventure_info().path, error));

    auto damaged_pak = valid_pak();
    damaged_pak[128] ^= 0x5A;
    const auto bad_pak = root / "bad.mpk";
    write_bytes(bad_pak, damaged_pak);
    assert(!dkr::runtime::saves::import_controller_pak(0, bad_pak, error));

    std::fputs("[test][save-manager] complete bundle round-trip\n", stderr);
    const auto bundle = root / "garage.dkrsave";
    assert(dkr::runtime::saves::export_bundle(bundle, error));
    assert(dkr::runtime::saves::reset_adventure(error));
    assert(dkr::runtime::saves::import_bundle(bundle, error));
    assert(dkr::runtime::saves::adventure_info().valid);
    assert(dkr::runtime::saves::controller_pak_info(0).valid);

    std::fputs("[test][save-manager] corrupt bundle rejection\n", stderr);
    std::vector<std::uint8_t> corrupt_bundle;
    {
        std::ifstream input(bundle, std::ios::binary);
        corrupt_bundle.assign(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>());
    }
    corrupt_bundle.back() ^= 1U;
    const auto bad_bundle = root / "bad.dkrsave";
    write_bytes(bad_bundle, corrupt_bundle);
    assert(!dkr::runtime::saves::import_bundle(bad_bundle, error));

    const auto oversized_bundle = root / "oversized.dkrsave";
    write_bytes(oversized_bundle, std::vector<std::uint8_t>(200U * 1024U, 0U));
    assert(!dkr::runtime::saves::import_bundle(oversized_bundle, error));

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::puts("[test][save-manager] PASS");
    return 0;
}
