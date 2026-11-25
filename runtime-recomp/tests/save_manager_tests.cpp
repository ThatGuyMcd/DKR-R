#include "cloud_saves/progress.hpp"
#include "cloud_saves/sync.hpp"
#include "custom_records.hpp"
#include "personal_adventure_paths.hpp"
#include "save_manager.hpp"
#include "save_storage.hpp"
#include "shared_saves.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
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
    const std::array<std::uint8_t, 8> magic{'D', 'K', 'R', 'M', 'P', 'K', '1', 0};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_u32(bytes, 8U, 1U);
    write_u32(bytes, 12U, 1U);
    write_u32(bytes, 16U, pak_checksum(bytes));
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::optional<dkr::cloud_saves::ProgressBatch> take_progress(dkr::cloud_saves::ProgressWrites& writes,
                                                             std::chrono::milliseconds timeout) {
    std::stop_source stop;
    std::jthread watchdog([&](std::stop_token done) {
        std::mutex mutex;
        std::condition_variable_any changed;
        std::unique_lock lock(mutex);
        changed.wait_for(lock, done, timeout, [] { return false; });
        if (!done.stop_requested())
            stop.request_stop();
    });
    return writes.take(stop.get_token());
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkr-save-manager-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    std::fill(erased_slot_adventure.begin() + 0x28U, erased_slot_adventure.begin() + 0x50U, 0xFFU);
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
    assert(dkr::runtime::saves::repair_adventure_checksums(repaired, repair_backup, error));
    assert(repaired && std::filesystem::exists(repair_backup));
    assert(read_bytes(repair_backup) == corrupt_live);
    assert(dkr::runtime::saves::adventure_info().valid);
    std::vector<std::uint8_t> canonical_live;
    assert(dkr::runtime::saves::canonical_adventure_bytes(canonical_live, error));
    assert(canonical_live == dkr::runtime::saves::codec::blank_bytes());
    repaired = true;
    repair_backup.clear();
    assert(dkr::runtime::saves::repair_adventure_checksums(repaired, repair_backup, error));
    assert(!repaired && repair_backup.empty());

    const auto invalid = root / "invalid.bin";
    std::ofstream(invalid, std::ios::binary).put('x');
    assert(!dkr::runtime::saves::import_adventure(invalid, error));
    const auto bad_checksum = root / "bad-checksum.bin";
    auto corrupt_adventure = dkr::runtime::saves::codec::blank_bytes();
    corrupt_adventure[12] ^= 1U;
    write_bytes(bad_checksum, corrupt_adventure);
    // v1.0.11 (shared-saves): import no longer rewrites checksums. A save that
    // needs repair is refused and the live save is left untouched; repair runs
    // only through Backups & Recovery (repair_adventure_checksums above).
    const auto live_before_import = read_bytes(live_save);
    assert(!dkr::runtime::saves::import_adventure(bad_checksum, error));
    assert(error.find("needs repair") != std::string::npos);
    assert(dkr::runtime::saves::adventure_info().valid);
    assert(read_bytes(live_save) == live_before_import);

    std::fputs("[test][save-manager] isolated online saves\n", stderr);
    auto host_image = dkr::runtime::saves::codec::blank_image();
    host_image.slots[0].name = "NET";
    host_image.slots[0].balloons = {1, 0, 0, 0, 0, 0};
    const auto host_save = dkr::runtime::saves::codec::encode(host_image);
    const auto single_player_before_online = read_bytes(dkr::runtime::saves::adventure_info().path);
    const std::size_t backups_before_online = dkr::runtime::saves::adventure_backups().size();
    std::vector<std::uint8_t> seeded_online;
    assert(dkr::runtime::saves::prepare_host_online_adventure(dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer,
                                                              seeded_online, error));
    assert(seeded_online == single_player_before_online);
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) == single_player_before_online);

    assert(dkr::runtime::saves::prepare_host_online_adventure(dkr::runtime::saves::OnlineSaveSeedMode::Fresh,
                                                              seeded_online, error));
    assert(seeded_online == dkr::runtime::saves::codec::blank_bytes());
    assert(!dkr::runtime::saves::previous_online_adventure_info().exists);
    std::vector<std::uint8_t> activated_host;
    std::filesystem::path activated_host_path;
    assert(dkr::runtime::saves::read_online_adventure(true, 1U, activated_host, activated_host_path, error));
    assert(activated_host == seeded_online);
    const auto host_online_info = dkr::runtime::saves::previous_online_adventure_info();
    assert(host_online_info.exists && host_online_info.valid);
    write_bytes(host_online_info.path, host_save);
    assert(dkr::runtime::saves::prepare_host_online_adventure(dkr::runtime::saves::OnlineSaveSeedMode::Fresh,
                                                              seeded_online, error));
    // Simulate failed/cancelled lobby creation: the old file must be untouched.
    assert(read_bytes(host_online_info.path) == host_save);
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession, seeded_online, error));
    assert(seeded_online == host_save);

    constexpr std::uint64_t match_id = 0x1234ABCDEF987654ULL;
    std::filesystem::path installed_online;
    assert(dkr::runtime::saves::install_synchronized_online_adventure(match_id, host_save, installed_online, error));
    assert(read_bytes(installed_online) == host_save);
    assert(installed_online.parent_path().filename() == "1234abcdef987654");
    std::vector<std::uint8_t> readback_online;
    std::filesystem::path readback_path;
    assert(dkr::runtime::saves::read_online_adventure(false, match_id, readback_online, readback_path, error));
    assert(readback_path == installed_online && readback_online == host_save);
    assert(!dkr::runtime::saves::install_synchronized_online_adventure(0U, host_save, installed_online, error));
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) == single_player_before_online);
    assert(dkr::runtime::saves::adventure_backups().size() == backups_before_online);

    // Superseded in v1.0.11 (shared-saves): host progress is no longer keyed
    // by the mod manifest; every mod set continues the one host Adventure
    // ("mod manifests identify simulation, never durable progress"). The
    // per-mod folders must stay gone. Clients still use their authenticated
    // session folder, and malformed profiles are still refused.
    const std::string mod_a(64, 'a'), mod_b(64, 'b');
    assert(!dkr::runtime::saves::prepare_host_online_adventure(dkr::runtime::saves::OnlineSaveSeedMode::Fresh,
                                                               seeded_online, error, "../escape"));
    assert(dkr::runtime::saves::previous_online_adventure_info(mod_a).path ==
           dkr::runtime::saves::previous_online_adventure_info(mod_b).path);
    assert(dkr::runtime::saves::prepare_host_online_adventure(dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer,
                                                              seeded_online, error, mod_a));
    assert(seeded_online == single_player_before_online);
    assert(dkr::runtime::saves::bind_staged_host_online_adventure(20, error));
    assert(dkr::runtime::saves::read_online_adventure(true, 20, activated_host, activated_host_path, error));
    assert(activated_host_path == root / "saves" / "online" / "host" / "dkr.us.v77.bin");
    assert(dkr::runtime::saves::commit_online_adventure(true, 20, host_save, error));
    assert(dkr::runtime::saves::previous_online_adventure_info(mod_a).valid);
    assert(dkr::runtime::saves::previous_online_adventure_info(mod_b).valid);
    assert(!std::filesystem::exists(root / "saves" / "online" / "host" / "mod-profiles"));
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession, seeded_online, error, mod_b));
    assert(seeded_online == host_save && read_bytes(host_online_info.path) == host_save);
    dkr::runtime::saves::discard_staged_host_online_adventure();
    assert(dkr::runtime::saves::prepare_host_online_adventure(
        dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession, seeded_online, error));
    assert(seeded_online == host_save);
    assert(read_bytes(dkr::runtime::saves::adventure_info().path) == single_player_before_online);

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
    assert(std::any_of(inventory.begin(), inventory.end(),
                       [](const auto& record) { return record.scope == 1 && record.info.valid; }));
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
        corrupt_bundle.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    corrupt_bundle.back() ^= 1U;
    const auto bad_bundle = root / "bad.dkrsave";
    write_bytes(bad_bundle, corrupt_bundle);
    assert(!dkr::runtime::saves::import_bundle(bad_bundle, error));

    const auto oversized_bundle = root / "oversized.dkrsave";
    write_bytes(oversized_bundle, std::vector<std::uint8_t>(200U * 1024U, 0U));
    assert(!dkr::runtime::saves::import_bundle(oversized_bundle, error));

    std::fputs("[test][save-manager] cloud save isolation and checkout priority\n", stderr);
    using namespace dkr::cloud_saves;
    static_assert(next_action("local", "", "") == Action::Upload);
    static_assert(next_action("local", "", "private-pilot") == Action::Upload);
    static_assert(next_action("local", "remote", "") == Action::Download);
    static_assert(next_action("", "", "previous") == Action::None);
    static_assert(next_action("", "remote", "") == Action::Download);
    static_assert(next_action("base", "remote", "base") == Action::Download);
    static_assert(next_action("local", "base", "base") == Action::Upload);
    static_assert(next_action("local", "remote", "base") == Action::Download);
    static_assert(next_action("same", "same", "") == Action::None);
    constexpr auto lobby_return = SyncScope::PublishHostedOnline;
    static_assert(includes_slot(lobby_return, "online-adventure"));
    static_assert(includes_slot(lobby_return, "online-paks"));
    static_assert(includes_slot(lobby_return, "online-records"));
    static_assert(!includes_slot(lobby_return, "offline-adventure"));
    static_assert(!includes_slot(lobby_return, "modded-records"));
    static_assert(next_action("played", "base", "base", lobby_return) == Action::Upload);
    static_assert(next_action("played", "", "", lobby_return) == Action::Upload);
    static_assert(next_action("played", "newer", "base", lobby_return) == Action::Defer);
    static_assert(next_action("", "newer", "", lobby_return) == Action::Defer);
    static_assert(sync_failure(true, 0) == SyncStatus::Unreachable);
    static_assert(sync_failure(false, 503) == SyncStatus::Unreachable);
    static_assert(sync_failure(false, 522) == SyncStatus::Unreachable);
    static_assert(sync_failure(false, 409) == SyncStatus::Error);
    static_assert(sync_failure(false, 415) == SyncStatus::Error);
    static_assert(sync_failure(false, 0) == SyncStatus::Error);
    namespace saves = dkr::runtime::saves;
    std::vector<std::uint8_t> local, cloud;
    assert(saves::cloud_save_slots().size() == 10);
    assert(saves::read_cloud_save("modded-adventure", local, error) && local.empty());
    assert(saves::personal_adventure_path(root, false) == saves::adventure_info().path);
    assert(!std::filesystem::exists(saves::personal_adventure_path(root, true)));
    assert(saves::read_cloud_save("offline-adventure", local, error));
    assert(saves::read_cloud_save("online-adventure", cloud, error));
    assert(local != cloud);
    assert(!saves::replace_cloud_save("offline-adventure", {}, cloud, error));
    auto bad_cloud = cloud;
    bad_cloud[12] ^= 1;
    assert(!saves::replace_cloud_save("offline-adventure", local, bad_cloud, error));
    assert(read_bytes(saves::adventure_info().path) == local);
    assert(saves::replace_cloud_save("offline-adventure", local, cloud, error));
    assert(read_bytes(saves::adventure_info().path.string() + ".bak") == local);
    assert(!saves::replace_cloud_save("offline-adventure", local, local, error));
    assert(read_bytes(saves::adventure_info().path) == cloud);
    assert(saves::read_cloud_save("offline-pak-1", cloud, error) && cloud == valid_pak());
    assert(saves::read_cloud_save("online-paks", cloud, error) && cloud.empty());
    assert(!saves::read_cloud_save("online/sessions/experimental-controller-paks.bin", cloud, error));

    // Both devices have valid but different progress. The first upload must
    // seed public slots even with pilot history. The second device checks out
    // the cloud versions through the save backend and preserves its originals.
    const auto windows = root / "windows", deck = root / "deck";
    const auto make_adventure = [](const char* name, std::uint8_t balloons = 0) {
        auto image = saves::codec::blank_image();
        image.slots[0].name = name;
        image.slots[0].balloons[0] = balloons;
        return saves::codec::encode(image);
    };
    const auto make_record = [](std::uint16_t time) {
        saves::records::Record record;
        record.key.assign(64, 'a');
        record.lap = time;
        return saves::records::encode({{{record.key, record.vehicle}, record}});
    };
    struct Checkout {
        const char* key;
        std::filesystem::path path;
        std::vector<std::uint8_t> windows, deck;
    };
    const Checkout slots[]{
        {"offline-adventure", "saves/dkr.us.v77.bin", make_adventure("WIN", 2), make_adventure("DEK", 3)},
        {"modded-records", "saves/custom-records-v1.bin", make_record(100), make_record(200)},
        {"online-adventure", "saves/online/host/dkr.us.v77.bin", make_adventure("NWN"), make_adventure("NDK")},
        {"modded-adventure", "saves/modded/dkr.us.v77.bin", make_adventure("MWN"), make_adventure("MDK", 1)},
    };
    for (const auto& slot : slots) {
        write_bytes(windows / slot.path, slot.windows);
        write_bytes(deck / slot.path, slot.deck);
        saves::configure(windows);
        assert(next_action("windows", "", "pilot") == Action::Upload);
        assert(saves::read_cloud_save(slot.key, cloud, error) && cloud == slot.windows);
        saves::configure(deck);
        assert(saves::read_cloud_save(slot.key, local, error) && local == slot.deck);
        assert(next_action("deck", "windows", "older") == Action::Download);
        assert(saves::replace_cloud_save(slot.key, local, cloud, error));
        assert(read_bytes(deck / slot.path) == slot.windows);
        assert(read_bytes((deck / slot.path).string() + ".bak") == slot.deck);
        const auto history = (deck / slot.path).parent_path() / "save-history" / slot.path.filename();
        assert(read_bytes(std::filesystem::directory_iterator(history)->path()) == slot.deck);
        assert(next_action("deck-next", "windows", "windows") == Action::Upload);
    }

    const auto vanilla_save_path = saves::personal_adventure_path(deck, false);
    const auto modded_save_path = saves::personal_adventure_path(deck, true);
    assert(read_bytes(vanilla_save_path) == slots[0].windows && read_bytes(modded_save_path) == slots[3].windows);
    assert(saves::replace_cloud_save("modded-adventure", slots[3].windows, make_adventure("MOD", 5), error));
    assert(read_bytes(vanilla_save_path) == slots[0].windows);
    // The two editors can hold drafts concurrently. Each revision belongs to
    // the exact bytes loaded by that editor, including during a cloud update.
    saves::codec::SaveImage vanilla_draft, modded_draft;
    std::string vanilla_revision, modded_revision;
    assert(saves::shared::load_draft(vanilla_draft, vanilla_revision, error, false));
    assert(saves::shared::load_draft(modded_draft, modded_revision, error, true));
    modded_draft.slots[0].balloons[0] = 6;
    assert(saves::shared::apply(modded_draft, modded_revision, error, true));
    assert(read_bytes(vanilla_save_path) == slots[0].windows);
    assert(read_bytes(modded_save_path) == make_adventure("MOD", 6));
    assert(!saves::shared::apply(modded_draft, modded_revision, error, true));
    const auto modded_backups = saves::adventure_backups(true);
    assert(!modded_backups.empty() && read_bytes(modded_backups.front()) == make_adventure("MOD", 5));
    vanilla_draft.slots[0].balloons[0] = 4;
    assert(saves::shared::apply(vanilla_draft, vanilla_revision, error, false));
    const auto edited_vanilla = read_bytes(vanilla_save_path);
    const auto edited_modded = read_bytes(modded_save_path);
    const auto custom_records = deck / slots[1].path;
    const auto existing_records = read_bytes(custom_records);
    const auto modded_bundle = root / "modded.dkrsave";
    assert(saves::export_bundle(modded_bundle, error, true));
    assert(saves::shared::reset(error, true));
    assert(read_bytes(modded_save_path) == saves::codec::blank_bytes());
    assert(read_bytes(vanilla_save_path) == edited_vanilla && read_bytes(custom_records) == existing_records);
    assert(saves::import_bundle(modded_bundle, error, true));
    assert(read_bytes(modded_save_path) == edited_modded && read_bytes(vanilla_save_path) == edited_vanilla);
    assert(saves::shared::adopt(modded_backups.front(), false, error, true));
    assert(read_bytes(modded_save_path) == make_adventure("MOD", 5));
    assert(read_bytes(vanilla_save_path) == edited_vanilla);
    saves::set_online_save_protection(true);
    assert(!saves::shared::reset(error, true));
    assert(!saves::read_cloud_save("modded-adventure", local, error));
    saves::set_online_save_protection(false);

    std::fputs("[test][save-manager] synced lobby seed and retained lobby protection\n", stderr);
    const auto host_path = deck / "saves/online/host/dkr.us.v77.bin";
    std::vector<std::uint8_t> seed;
    assert(saves::prepare_host_online_adventure(saves::OnlineSaveSeedMode::ContinuePreviousSession, seed, error));
    assert(seed == slots[2].windows);
    assert(saves::bind_staged_host_online_adventure(101, error));
    const auto played = make_adventure("NEW");
    assert(saves::commit_online_adventure(true, 101, played, error));
    saves::set_online_save_protection(true);
    assert(!saves::read_cloud_save("offline-adventure", local, error));
    assert(saves::read_cloud_save("online-adventure", local, error) && local == played);
    assert(next_action("played", "newer", "windows", lobby_return) == Action::Defer);
    assert(read_bytes(host_path) == played);
    saves::set_online_save_protection(false);
    assert(next_action("played", "newer", "windows") == Action::Download);
    assert(saves::replace_cloud_save("online-adventure", played, slots[2].deck, error));
    assert(read_bytes(host_path) == slots[2].deck);
    assert(read_bytes(host_path.string() + ".bak") == played);

    std::fputs("[test][save-manager] confirmed writes and background cloud scope\n", stderr);
    constexpr auto live_sync = SyncScope::PublishProgress;
    static_assert(next_action("played", "base", "base", live_sync) == Action::Upload);
    static_assert(next_action("played", "newer", "base", live_sync) == Action::Defer);
    static_assert(next_action("played", "", "", live_sync) == Action::Upload);
    auto& writes = progress_writes;
    writes.account(7U);
    writes.gameplay(true);
    assert(saves::commit_online_adventure(true, 101, played, error));
    assert(read_bytes(host_path) == played);
    const auto batch = take_progress(writes, std::chrono::seconds(1));
    assert(batch && batch->account_generation == 7U && batch->slots.size() == 1 &&
           batch->slots.contains("online-adventure"));
    assert(writes.visible());
    assert(saves::commit_online_adventure(true, 101, played, error));
    assert(!take_progress(writes, std::chrono::milliseconds(50))); // Unchanged save.
    assert(saves::replace_cloud_save("online-adventure", played, slots[2].deck, error));
    assert(!take_progress(writes, std::chrono::milliseconds(50))); // Cloud checkout.
    const auto new_modded = make_adventure("MOD", 8);
    const auto valid_adventure = [](const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
        bytes = read_bytes(path);
        return saves::codec::validate(bytes);
    };
    assert(saves::storage::write_progress(modded_save_path, new_modded, valid_adventure, error));
    const auto modded_batch = take_progress(writes, std::chrono::seconds(1));
    assert(modded_batch && modded_batch->slots.size() == 1 && modded_batch->slots.contains("modded-adventure"));
    assert(read_bytes(modded_save_path) == new_modded);
    assert(read_bytes(vanilla_save_path) == edited_vanilla);
    writes.gameplay(false);
    writes.account(std::nullopt);

    ProgressWrites notice;
    notice.gameplay(true);
    const auto start = ProgressWrites::Clock::time_point(std::chrono::seconds(5));
    notice.committed("any-gameplay-save", start);
    notice.committed("second-write", start + std::chrono::milliseconds(800));
    assert(notice.visible(start + std::chrono::milliseconds(849)));
    assert(!notice.visible(start + std::chrono::milliseconds(850)));
    assert(!notice.visible(start + std::chrono::seconds(1)));

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::puts("[test][save-manager] PASS");
    return 0;
}
