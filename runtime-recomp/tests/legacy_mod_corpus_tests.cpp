#include "legacy_mod_format.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_asset_bank.hpp"
#include "legacy_asset_directory.hpp"
#include "legacy_asset_bus.hpp"
#include "legacy_asset_io.hpp"
#include "legacy_audio_bank.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_track_materialize.hpp"
#include "legacy_mod_dependencies.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace dkr::mods;
using nlohmann::json;
void require(bool condition, const char* message) {
    if (!condition)
        throw Error(message);
}
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
struct Temp {
    std::filesystem::path path;
    Temp() {
        path = std::filesystem::current_path() /
               ("legacy-corpus-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path), "Could not create private test transaction.");
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
void ownership_test(const std::filesystem::path& rom) {
    const auto base = AssetBank::stock(read_file(rom, MaxImage));
    const auto original = sha256(base->record(27, 5));
    const auto untouched = sha256(base->record(27, 6));
    // Synthetic records intentionally distinguish owners of the SAME retail
    // record ID. These bytes are never presented as playable track geometry.
    const auto a = AssetBank::derive(base, std::string(64, 'a'), {{{27, 5}, Bytes{1, 2, 3}}});
    const auto b = AssetBank::derive(base, std::string(64, 'b'), {{{27, 5}, Bytes{4, 5, 6}}});
    const auto forged = AssetBank::derive(base, std::string(64, 'a'), {{{27, 5}, Bytes{7, 8, 9}}});
    require(a->fingerprint() != forged->fingerprint(), "Bank identity did not bind actual asset bytes.");
    const auto stock_directory = AssetDirectory::build(base);
    for (unsigned section = 0; section < 50; ++section) {
        const auto stock = base->stock_section(section);
        require(sha256(stock_directory->read(section, 0, stock.size())) == sha256(stock),
                "Stock section changed in virtual directory.");
    }
    auto custom_directory = AssetDirectory::build(a);
    const auto table = custom_directory->read(26, 0, custom_directory->section_size(26));
    const auto start = be32(table, 5 * 4), end = be32(table, 6 * 4);
    require(end - start == 3 && custom_directory->read(27, start, 3) == Bytes({1, 2, 3}),
            "Custom data and lookup table disagree.");
    const auto address = custom_directory->address(27, start);
    custom_directory.reset();
    require(address.read(3) == Bytes({1, 2, 3}), "Outstanding asset address lost its immutable owner.");
    auto next = AssetDirectory::build(b);
    require(address.read(3) == Bytes({1, 2, 3}), "New bank changed a prior asset address.");
    const auto next_table = next->read(26, 0, next->section_size(26));
    const auto next_offset = be32(next_table, 6 * 4);
    require(sha256(next->read(27, next_offset, base->record(27, 6).size())) == untouched,
            "Shifted table offset points to the wrong stock record.");
    const auto crossing = next->read(27, be32(next_table, 5 * 4) + 1, 4);
    require(crossing[0] == 5 && crossing[1] == 6 && crossing[2] == base->record(27, 6)[0],
            "Partial read across a record boundary was corrupted.");
    bool bad_range = false;
    try {
        next->read(27, next->section_size(27), 1);
    } catch (const Error&) {
        bad_range = true;
    }
    require(bad_range, "Out-of-range virtual DMA was accepted.");
    AssetBus bus;
    require(!bus.resolve(0x100000, 16), "Original cartridge address was intercepted.");
    auto mounted_a = bus.mount(address.owner);
    const auto virtual_a = mounted_a->address(27, start);
    auto retained = bus.resolve(virtual_a, 3);
    require(retained && retained->bytes() == Bytes({1, 2, 3}), "Virtual DMA resolved the wrong bank.");
    const auto mounted_b = bus.mount(next);
    require(mounted_b->address(27, start) != virtual_a, "Different content aliased a virtual address.");
    mounted_a.reset();
    require(retained->bytes() == Bytes({1, 2, 3}), "In-flight DMA lost its owner on scene retirement.");
    Bytes memory(128, 0xcc);
    retained->copy_to_guest(memory, 0x80000009);
    require(memory[9 ^ 3] == 1 && memory[0xa ^ 3] == 2 && memory[11 ^ 3] == 3 && memory[8 ^ 3] == 0xcc &&
                memory[12 ^ 3] == 0xcc,
            "Virtual DMA corrupted guest word-swapped bytes or adjacent data.");
    for (const auto destination : {0x8000007fU, 0x90000000U, 0x40000000U, 0xffffffffU}) {
        const auto before = memory;
        bool invalid = false;
        try {
            retained->copy_to_guest(memory, destination);
        } catch (const Error&) {
            invalid = true;
        }
        require(invalid && memory == before, "Rejected DMA changed guest memory.");
    }
    for (const auto segment : {0U, 0x80000000U, 0xa0000000U}) {
        retained->copy_to_guest(memory, segment + 13);
        require(memory[13 ^ 3] == 1 && memory[14 ^ 3] == 2 && memory[15 ^ 3] == 3,
                "Physical/KSEG DMA aliases disagree.");
    }
    for (const auto invalid_address :
         {mounted_b->address(27, next->section_size(27)) + 1, AssetBus::End - 1, AssetBus::End, 0x7fffffffU}) {
        bool invalid = false;
        try {
            bus.resolve(invalid_address, 1);
        } catch (const Error&) {
            invalid = true;
        }
        require(invalid, "Unbacked virtual address could reach cartridge DMA.");
    }
    bool overflow = false;
    try {
        bus.resolve(virtual_a, ~std::size_t{});
    } catch (const Error&) {
        overflow = true;
    }
    require(overflow, "Overflowing virtual read was accepted.");
    retained.reset();
    bool stale_read = false;
    try {
        bus.resolve(virtual_a, 3);
    } catch (const Error&) {
        stale_read = true;
    }
    require(stale_read, "Retired bank silently resolved through current content.");
    const auto remounted = bus.mount(address.owner);
    require(remounted->address(27, start) == virtual_a,
            "Same immutable content consumed new addresses on every visit.");
    // Simultaneous game/audio lookups must pin the same immutable owner; no
    // callback, guest write or decompression runs under the address-map mutex.
    std::atomic_bool concurrent_ok{true};
    std::jthread reader([&] {
        try {
            for (unsigned i = 0; i < 500; ++i)
                if (bus.resolve(virtual_a, 3)->bytes() != Bytes({1, 2, 3}))
                    concurrent_ok = false;
        } catch (...) {
            concurrent_ok = false;
        }
    });
    for (unsigned i = 0; i < 500; ++i)
        require(bus.mount(next) == mounted_b, "Live immutable mount was duplicated.");
    reader.join();
    require(concurrent_ok, "Concurrent audio/game bank reads disagreed.");
    Bytes guest_io(4096, 0xcc);
    const auto set_word = [&](std::uint32_t address, std::uint32_t value) {
        const auto p = address & 0x1fffffff;
        for (unsigned i = 0; i < 4; ++i)
            guest_io[(p + i) ^ 3] = value >> (24 - 8 * i);
    };
    set_word(0x110, 0x80000800);
    set_word(0x114, 3);
    set_word(0x118, 0x80000200);
    set_word(0x210, 1);
    set_word(0x214, 0x80000300);
    struct CompletionState {
        const AssetBus* bus;
        std::uint32_t source, queue = 0;
        unsigned count = 0;
        Bytes expected{1, 2, 3};
    };
    CompletionState completed{&bus, virtual_a};
    const PiCompletion complete{&completed, [](void* opaque, std::uint32_t queue) {
                                    auto& state = *static_cast<CompletionState*>(opaque);
                                    state.queue = queue;
                                    ++state.count;
                                    require(state.bus->resolve(state.source, 3)->bytes() == state.expected,
                                            "PI completion lost its bank or retained the bus lock.");
                                }};
    require(intercept_virtual_pi_dma(bus, guest_io, {0, virtual_a, 0x80000100}, complete),
            "Guest PI call did not route to its bank.");
    require(completed.count == 1 && completed.queue == 0x80000200 && guest_io[0x800 ^ 3] == 1 &&
                guest_io[0x802 ^ 3] == 3,
            "Guest PI call did not preserve bytes and exactly-one completion semantics.");
    require(!intercept_virtual_pi_dma(bus, {}, {0, 0x100000, 0xffffffff}, {}),
            "Original PI path touched guest stack or required mod state.");
    const auto before_invalid = guest_io;
    for (const PiDmaCall call :
         {PiDmaCall{1, virtual_a, 0x80000100}, PiDmaCall{0, virtual_a, 0x80000ff0},
          PiDmaCall{0, remounted->address(27, address.owner->section_size(27) - 1), 0x80000100}}) {
        bool invalid = false;
        try {
            intercept_virtual_pi_dma(bus, guest_io, call, complete);
        } catch (const Error&) {
            invalid = true;
        }
        require(invalid && completed.count == 1 && guest_io == before_invalid,
                "Invalid PI call changed bytes or emitted a completion.");
    }
    set_word(0x210, 0);
    bool invalid_queue = false;
    try {
        intercept_virtual_pi_dma(bus, guest_io, {0, virtual_a, 0x80000100}, complete);
    } catch (const Error&) {
        invalid_queue = true;
    }
    require(invalid_queue && completed.count == 1, "Invalid PI completion queue was accepted.");
    set_word(0x210, 1);
    for (const auto overlapping_destination : {0x80000110U, 0x80000200U, 0x80000300U}) {
        set_word(0x110, overlapping_destination);
        const auto original_io = guest_io;
        bool rejected = false;
        try {
            intercept_virtual_pi_dma(bus, guest_io, {0, virtual_a, 0x80000100}, complete);
        } catch (const Error&) {
            rejected = true;
        }
        require(rejected && completed.count == 1 && original_io == guest_io,
                "Virtual DMA overwrote its own queue or call arguments.");
    }
    set_word(0x110, 0x80000800);
    for (const auto bad_message_pointer : {0x300U, 0xa0000300U, 0x80000ffeU}) {
        set_word(0x214, bad_message_pointer);
        const auto original_io = guest_io;
        bool rejected = false;
        try {
            intercept_virtual_pi_dma(bus, guest_io, {0, virtual_a, 0x80000100}, complete);
        } catch (const Error&) {
            rejected = true;
        }
        require(rejected && completed.count == 1 && original_io == guest_io,
                "PI completion accepted an incompatible message-pointer ABI.");
    }
    set_word(0x214, 0x80000300);
    for (unsigned source_lane = 0; source_lane < 4; ++source_lane)
        for (unsigned lane = 0; lane < 8; ++lane) {
            completed.source = virtual_a + source_lane;
            const auto expected_bytes = bus.resolve(completed.source, 3)->bytes();
            completed.expected = expected_bytes;
            set_word(0x110, 0x80000800 + lane);
            const auto before = guest_io;
            const auto completions = completed.count;
            require(intercept_virtual_pi_dma(bus, guest_io, {0, completed.source, 0x80000100}, complete),
                    "Byte-aligned virtual asset was rejected");
            for (unsigned i = 0; i < guest_io.size(); ++i) {
                const auto logical = i ^ 3U;
                const auto expected = logical >= 0x800 + lane && logical < 0x803 + lane
                                          ? expected_bytes[logical - (0x800 + lane)]
                                          : before[i];
                require(guest_io[i] == expected, "Unaligned asset copy changed a neighbouring byte lane");
            }
            require(completed.count == completions + 1, "Unaligned asset copy lost its single completion");
        }
    const auto stock_mount = bus.mount(stock_directory);
    for (unsigned section = 0; section < 50; ++section) {
        const auto expected = base->stock_section(section);
        const auto dma = bus.resolve(stock_mount->address(section), expected.size());
        Bytes guest((expected.size() + 3) & ~std::size_t(3), 0);
        dma->copy_to_guest(guest, 0x80000000);
        for (std::size_t i = 0; i < expected.size(); ++i)
            require(guest[i ^ 3] == expected[i], "An original section changed at the guest DMA boundary.");
    }
    SceneBankSlot slot(base, 5);
    auto render = slot.acquire();
    auto audio = slot.acquire();
    const auto first = slot.request({a->digest(), a->fingerprint(), 5, 1});
    require(slot.publish(first) == SceneBankSlot::Publish::NotPrepared, "Unprepared bank was published.");
    require(slot.prepared(first, a), "First prepared bank was rejected.");
    require(slot.publish(first) == SceneBankSlot::Publish::WaitingForBorrowers,
            "Live renderer/audio references were retired early.");
    render = {};
    require(slot.publish(first) == SceneBankSlot::Publish::WaitingForBorrowers,
            "Live audio reference was retired early.");
    audio = {};
    require(slot.publish(first) == SceneBankSlot::Publish::Published, "Drained bank could not publish.");
    {
        auto lease = slot.acquire();
        require(lease.record(27, 5)[0] == 1, "Custom A did not resolve its own record.");
        require(sha256(lease.record(27, 6)) == untouched, "Unchanged stock dependency was replaced.");
    }
    const auto second = slot.request({"", base->fingerprint(), 5, 2});
    require(slot.prepared(second, base) && slot.publish(second) == SceneBankSlot::Publish::Published,
            "Stock restoration failed.");
    {
        auto lease = slot.acquire();
        require(sha256(lease.record(27, 5)) == original, "Stock bytes changed after Custom A.");
    }
    const auto stale = slot.request({a->digest(), a->fingerprint(), 5, 3});
    const auto third = slot.request({b->digest(), b->fingerprint(), 5, 4});
    require(!slot.prepared(stale, a) && slot.publish(stale) == SceneBankSlot::Publish::Stale,
            "Late preview result replaced a newer selection.");
    require(slot.prepared(third, b) && slot.publish(third) == SceneBankSlot::Publish::Published,
            "Custom B could not publish.");
    {
        auto lease = slot.acquire();
        require(lease.record(27, 5)[0] == 4, "Custom B inherited Custom A's record.");
    }
    const auto fourth = slot.request({"", base->fingerprint(), 5, 5});
    slot.prepared(fourth, base);
    require(slot.publish(fourth) == SceneBankSlot::Publish::Published, "Second stock restoration failed.");
    {
        auto lease = slot.acquire();
        require(sha256(lease.record(27, 5)) == original, "Stock bytes changed after Custom B.");
    }
    const auto cancelled = slot.request({a->digest(), a->fingerprint(), 5, 6});
    slot.cancel(cancelled);
    require(!slot.prepared(cancelled, a) && slot.publish(cancelled) == SceneBankSlot::Publish::Stale,
            "Cancelled import was mounted.");
    bool rejected = false;
    try {
        slot.request({a->digest(), a->fingerprint(), 5, 6});
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected, "Cancelled content generation was reused.");
    const auto integrity = slot.request({a->digest(), a->fingerprint(), 5, 7});
    rejected = false;
    try {
        slot.prepared(integrity, forged);
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected, "Claimed package ID bypassed bank fingerprint validation.");
    std::cout << base->revision() << " bank ownership, drain, stale-completion and restoration tests passed.\n";
}
json run(const std::filesystem::path& worker, const std::filesystem::path& source,
         const std::vector<std::filesystem::path>& roms, WorkerOutcome expected = WorkerOutcome::Completed) {
    Temp transaction;
    json request = {{"schema", Schema}, {"source", utf8(source)}, {"roms", json::array()}};
    for (const auto& rom : roms)
        request["roms"].push_back(utf8(rom));
    const auto encoded = request.dump();
    const auto file = transaction.path / "request.json";
    write_new_file(file, View(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size()));
    const auto result = run_worker(worker, file, {});
    require(result.outcome == expected, "Unexpected worker outcome.");
    if (expected != WorkerOutcome::Completed) {
        require(!std::filesystem::exists(transaction.path / "content" / "review.json"),
                "Failed import created a completed review.");
        require(std::filesystem::is_regular_file(transaction.path / "failure.json"),
                "Failed import has no explanation.");
        return {};
    }
    const auto receipt = read_file(transaction.path / "content" / "review.json", 2 * MiB);
    const auto value = json::parse(receipt.begin(), receipt.end());
    require(value.at("runtime_activated") == false, "Import must not activate any runtime bank.");
    for (const auto& package : value.at("packages")) {
        require(package.at("enabled") == false && package.at("online_certified") == false,
                "Uncertified content was enabled.");
        for (const auto& record : package.at("records")) {
            const auto hash = record.at("sha256").get<std::string>();
            const auto data = read_file(transaction.path / "content" / "blobs" / hash, MaxImage);
            require(data.size() == record.at("size") && sha256(data) == hash,
                    "Staged blob does not match its receipt.");
        }
        for (const auto& root : package.at("tracks")) {
            const auto& dependencies = root.at("static_dependencies");
            require(dependencies.at("blockers").empty(), "Known corpus track failed its static-reference checks.");
            std::cout << root.at("name").get<std::string>() << ": " << dependencies.at("records").size()
                      << " static dependencies, " << dependencies.at("placed_objects") << " placed objects";
            for (const auto& blocker : dependencies.at("blockers"))
                std::cout << "; review: " << blocker.get<std::string>();
            std::cout << '\n';
        }
    }
    // No platform paths/times in the receipt: compare this digest between OSes.
    std::cout << "receipt-sha256=" << sha256(receipt) << '\n';
    return value;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "Usage: corpus-tests worker owned-v77 owned-v80 bigboo-zip haunter-zip community-zip\n";
        return 2;
    }
    try {
        std::vector<std::filesystem::path> paths;
        std::vector<std::string> initial;
        for (int i = 1; i < argc; ++i) {
            paths.push_back(std::filesystem::absolute(utf8_path(argv[i])));
            initial.push_back(sha256(read_file(paths.back(), MaxArchive)));
        }
        const std::vector<std::filesystem::path> roms{paths[1], paths[2]};
        // Also exercise the parsers in this process, so a sanitizer build of
        // the controller instruments actual corpus decoding/analysis. The
        // child worker remains separately covered by resource-limit tests.
        const auto source = read_file(paths[1], MaxImage);
        const AssetImage original(source, verified_revision(source));
        const auto resident_original = AssetBank::stock(source);
        AssetBus resident_bus;
        for (unsigned p = 3; p < 6; ++p)
            for (const auto& patch : read_patch_inputs(paths[p])) {
                const auto reconstructed = decode_patch(source, patch.data);
                const auto analysis = analyze(source, reconstructed, sha256(patch.data));
                const AssetImage target(reconstructed, analysis.source_revision);
                for (const auto& root : analysis.tracks) {
                    require(inspect_track_dependencies(original, target, root).blockers.empty(),
                            "Direct corpus dependency analysis failed.");
                    const auto prepared = prepare_track_bank(source, reconstructed, sha256(patch.data), root.carrier);
                    const auto resident = ResidentBank::prepare(resident_original, prepared.bank, resident_bus);
                    require(resident->route() &&
                                resident->route()->directory()->fingerprint() == prepared.bank->fingerprint(),
                            "Prepared resident tables lost their scoped track bank.");
                    require(!prepared.runtime_certified, "Preparation improperly certified a playable track.");
                    require(prepared.bank->digest() == root.content_id,
                            "Prepared bank lost its public track identity.");
                    require(
                        std::ranges::equal(prepared.bank->record(27, root.geometry), target.record(27, root.geometry)),
                        "Prepared course omitted its geometry.");
                    require(std::ranges::equal(prepared.bank->stock_section(25), original.sections[25]),
                            "Prepared course replaced stock names.");
                    // The deleted collateral level from these packs must remain
                    // the byte-identical original in every prepared course.
                    const auto collateral = p == 3 ? 20U : 19U;
                    require(std::ranges::equal(prepared.bank->record(27, collateral), original.record(27, collateral)),
                            "A collateral deletion escaped into the mounted bank.");
                    const auto stock_songs = inspect_sequence_directory(original.record(39, 5));
                    const auto prepared_songs = inspect_sequence_directory(prepared.bank->record(39, 5));
                    require(prepared_songs.maximum_loaded_length <= stock_songs.maximum_loaded_length,
                            "Prepared music exceeded retained audio buffers.");
                    for (unsigned song = 0; song < stock_songs.records.size(); ++song) {
                        const bool chosen = std::find(prepared.music_sequences.begin(), prepared.music_sequences.end(),
                                                      song) != prepared.music_sequences.end();
                        if (!chosen)
                            require(stock_songs.records[song].digest == prepared_songs.records[song].digest,
                                    "Another course's music leaked into this track.");
                    }
                    if (p == 3) {
                        require(prepared.music_sequences == std::vector<unsigned>{10},
                                "Big Boo lost its selected custom music.");
                        require(std::ranges::equal(prepared.bank->record(29, 380), target.record(29, 380)),
                                "Big Boo omitted its dynamically spawned projectile.");
                        for (unsigned icon : {306U, 307U, 308U})
                            require(std::ranges::equal(prepared.bank->record(4, icon), target.record(4, icon)),
                                    "Big Boo omitted its weapon HUD icons.");
                    }
                    std::cout << root.name << ": prepared " << prepared.included.size() << " scoped records, "
                              << prepared.music_sequences.size()
                              << " selected music overrides; runtime certification pending.\n";
                }
            }
        std::cout << "Direct corpus decoding and static-reference checks passed.\n";
        ownership_test(paths[1]);
        ownership_test(paths[2]);
        const auto boo = run(paths[0], paths[3], roms);
        require(boo.at("packages").size() == 1, "Big Boo should produce one package.");
        const auto& track = boo["packages"][0]["tracks"];
        require(track.size() == 1 && track[0]["carrier"] == 5 && track[0]["name"] == "Big Boo's Haunt",
                "Big Boo track discovery regressed.");
        require(track[0]["blockers"].empty(), "Big Boo structural check failed.");
        const auto haunter = run(paths[0], paths[4], roms);
        require(haunter["packages"].size() == 1, "Equivalent Haunter variants were duplicated.");
        require(haunter["packages"][0]["editions"].size() == 2, "Both Haunter editions must be retained.");
        require(haunter["packages"][0]["characters"].size() == 1 && haunter["packages"][0]["tracks"].empty(),
                "Character patch was misclassified as a track.");
        const auto community = run(paths[0], paths[5], roms);
        require(community["packages"].size() == 1 && community["packages"][0]["tracks"].size() == 6,
                "Community Pack should produce six tracks.");
        const auto& cookie = community["packages"][0]["tracks"][5];
        require(cookie["carrier"] == 29 && cookie["object_map"] == 30 && cookie["geometry"] == 30,
                "Object map and carrier IDs were conflated.");
        run(paths[0], paths[3], {paths[2]}, WorkerOutcome::Failed); // Patch requires v77 source, not v80.
        Temp bad;
        const auto bad_patch = bad.path / "invalid.xdelta";
        write_new_file(bad_patch, Bytes{0xd6, 0xc3, 0xc4, 0, 0xff});
        run(paths[0], bad_patch, roms, WorkerOutcome::Failed);
        for (unsigned i = 0; i < paths.size(); ++i)
            require(sha256(read_file(paths[i], MaxArchive)) == initial[i], "A source fixture was modified.");
        std::cout << "Corpus imports, source preservation, revision mismatch and failure isolation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
