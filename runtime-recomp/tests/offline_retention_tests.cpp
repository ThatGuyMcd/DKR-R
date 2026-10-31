#include "offline_keep.hpp"
#include <json/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
using namespace dkr::mods;
using namespace dkr::mods::online;
using nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw Error(why);
}
template <class F> void rejects(F f, const char* why) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, why);
}
Bytes bytes(std::string_view s) {
    return Bytes(s.begin(), s.end());
}
std::string hash(std::string_view s) {
    return sha256(bytes(s));
}
void put(const std::filesystem::path& p, const json& j) {
    const auto s = j.dump();
    write_new_file(p, bytes(s));
}
void dirs(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "reviews");
    std::filesystem::create_directories(root / "prepared");
    std::filesystem::create_directories(root / "prepared-characters");
}
struct Entry {
    TrackCatalogItem item;
    json manifest;
    Bytes asset;
};
Entry entry(std::string seed, bool character = false, std::string name = {}, std::string revision = "us.v77") {
    Entry out;
    auto& i = out.item;
    i.id = hash("id:" + seed);
    i.name = name.empty() ? seed : name;
    i.revision = revision;
    i.bank = character ? i.id : hash("bank:" + seed);
    i.patch = hash("patch:" + seed);
    i.storage = i.id;
    i.vehicles = 7;
    i.carrier = character ? 0 : 3;
    i.base_character = character ? 2 : 0;
    out.asset = bytes("synthetic asset " + seed);
    const auto blob = sha256(out.asset);
    out.manifest = {{"id", i.id}, {"revision", revision}, {"name", i.name}, {"records", json::array()}};
    out.manifest["records"].push_back(
        {{character ? "hash" : "digest", blob}, {"size", out.asset.size()}, {"section", 21}, {"record", 0}});
    if (character)
        out.manifest["audio"] = {{"control", blob},
                                 {"samples", blob},
                                 {"control_size", out.asset.size()},
                                 {"sample_size", out.asset.size()}};
    i.artifact = hash(out.manifest.dump());
    return out;
}
std::vector<CatalogSource> group(const std::filesystem::path& root, std::vector<Entry> entries,
                                 bool character = false) {
    dirs(root);
    json review = {{"schema", 1}, {"source", "synthetic fixture"}};
    const auto review_id = hash(review.dump());
    std::filesystem::create_directory(root / "reviews" / review_id);
    if (!std::filesystem::exists(root / "reviews" / review_id / "review.json"))
        put(root / "reviews" / review_id / "review.json", review);
    const auto key = character ? "characters" : "tracks";
    json receipt = {{"schema", 1},
                    {"adapter", character ? CharacterAdapterVersion : TrackAdapterVersion},
                    {"enabled", false},
                    {"review", review_id},
                    {key, json::array()}};
    for (auto& e : entries) {
        const auto& i = e.item;
        json row = {{"id", i.id},       {"name", i.name}, {"revision", i.revision}, {"artifact", i.artifact},
                    {"patch", i.patch}, {"vehicles", 7},  {"storage", i.storage}};
        if (character)
            row["behaviour"] = i.base_character;
        else {
            row["bank"] = i.bank;
            row["carrier"] = i.carrier;
            row["race_type"] = 0;
        }
        receipt[key].push_back(row);
    }
    const auto group_id = hash(receipt.dump());
    const auto folder = character ? "prepared-characters" : "prepared";
    const auto path = root / folder / group_id;
    std::filesystem::create_directory(path);
    put(path / "prepared.json", receipt);
    std::vector<CatalogSource> result;
    for (auto& e : entries) {
        e.item.group = group_id;
        e.item.review = review_id;
        const auto storage = path / e.item.storage;
        std::filesystem::create_directory(storage);
        std::filesystem::create_directory(storage / "blobs");
        put(storage / (character ? "character.json" : "track.json"), e.manifest);
        write_new_file(storage / "blobs" / sha256(e.asset), e.asset);
        result.push_back({root, e.item});
    }
    return result;
}
auto catalogue(const std::filesystem::path& root, bool character = false, std::string_view revision = "us.v77") {
    return TrackCatalog::installed_items(root, revision,
                                         character ? TrackCatalog::Kind::Character : TrackCatalog::Kind::Track);
}
using Snapshot = std::map<std::filesystem::path, std::string>;
Snapshot snapshot(const std::filesystem::path& root) {
    Snapshot out;
    if (std::filesystem::exists(root))
        for (const auto& e : std::filesystem::recursive_directory_iterator(root))
            if (e.is_regular_file())
                out.emplace(e.path().lexically_relative(root), sha256(read_file(e.path(), MaxStaged)));
    return out;
}
void preserve(const std::filesystem::path& root, const Snapshot& before) {
    for (const auto& [name, digest] : before)
        check(std::filesystem::exists(root / name) && sha256(read_file(root / name, MaxStaged)) == digest,
              "an existing offline file was modified");
}
} // namespace
int main(int argc, char** argv) try {
    if (argc != 2)
        throw Error("Supply a fresh exclusively reserved fixture path.");
    const auto root = private_storage_path(std::filesystem::absolute(argv[1]));
    check(std::filesystem::create_directory(root), "fixture path must be new");
    for (bool character : {false, true}) {
        const auto area = root / (character ? "characters" : "tracks"), offline = area / "offline",
                   incoming = area / "incoming";
        std::filesystem::create_directory(area);
        auto original = entry("shared", character, "Installed label");
        group(offline, {original}, character);
        const auto activation = character ? "character-catalog.json" : "track-catalog.json";
        put(offline / activation, {{"schema", 1}, {"enabled", json::array({original.item.id})}});
        write_new_file(offline / "save-sentinel", Bytes{1, 2, 3, 4});
        auto renamed = entry("shared", character, "Host label");
        renamed.item.patch = hash("another patch container");
        auto fresh = entry("new", character), unselected = entry("not selected", character);
        const auto refs = group(incoming, {renamed, fresh, unselected}, character);
        ProfileSources sources;
        auto& selection = character ? sources.characters : sources.tracks;
        selection.push_back(refs.at(0));
        selection.push_back(refs.at(1));
        const auto before = snapshot(offline);
        rejects([&] { TrackCatalog::validate_retained_merge(offline, incoming); },
                "old preflight must reproduce the packaging/label conflict");
        retain_legacy_sources(sources, offline);
        auto items = catalogue(offline, character);
        check(items.size() == 2, "keep selected missing entries without unrelated pack content");
        check(std::none_of(items.begin(), items.end(), [&](const auto& i) { return i.id == unselected.item.id; }),
              "unselected pack entry was retained");
        auto old = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.id == original.item.id; });
        auto added = std::find_if(items.begin(), items.end(), [&](const auto& i) { return i.id == fresh.item.id; });
        check(old != items.end() && old->name == "Installed label" && old->enabled,
              "existing label and activation preserved");
        check(added != items.end() && !added->enabled, "new copy starts disabled");
        preserve(offline, before);
        const auto kept = snapshot(offline);
        for (unsigned repeat = 0; repeat < 3; ++repeat)
            retain_legacy_sources(sources, offline);
        check(snapshot(offline) == kept, "repeated lobby joins should not copy or mutate existing files");
        // A whole source pack may have conflicting unrelated entries. Selecting
        // only one genuinely new entry must not let those poison the keep job.
        auto conflict = renamed;
        conflict.item.bank = hash("conflicting bank");
        if (character)
            conflict.item.behaviour_identity = hash("conflicting behaviour");
        auto more = entry("another", character);
        const auto other = group(area / "other", {conflict, more}, character);
        ProfileSources only;
        auto& only_selection = character ? only.characters : only.tracks;
        only_selection.push_back(other.at(1));
        retain_legacy_sources(only, offline);
        check(catalogue(offline, character).size() == 3, "unselected conflict blocked a new selected mod");
        ProfileSources bad;
        auto& bad_selection = character ? bad.characters : bad.tracks;
        bad_selection.push_back(other.at(0));
        const auto intact = snapshot(offline);
        rejects([&] { retain_legacy_sources(bad, offline); }, "semantic identity conflicts must still be rejected");
        preserve(offline, intact);
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { retain_legacy_sources(sources, area / "cancelled", stop.get_token()); },
                "cancellation must stop the keep");
        check(!std::filesystem::exists(area / "cancelled"), "cancelled keep created destination");
        // Real corruption must not become a successful deduplication.
        const auto asset = offline / (character ? "prepared-characters" : "prepared") / old->group / old->storage /
                           "blobs" / sha256(original.asset);
        {
            std::ofstream file(asset, std::ios::binary | std::ios::trunc);
            file << "damaged";
        }
        rejects([&] { retain_legacy_sources(sources, offline); }, "corrupt installed blobs were silently reused");
    }
    {
        const auto refs =
            group(root / "revision-source", {entry("rev77", false, {}, "us.v77"), entry("rev80", false, {}, "us.v80")});
        ProfileSources source;
        source.tracks = refs;
        retain_legacy_sources(source, root / "revision-kept");
        check(catalogue(root / "revision-kept", false, "us.v77").size() == 1, "v77 retained");
        check(catalogue(root / "revision-kept", false, "us.v80").size() == 1, "v80 retained");
    }
    {
        const auto refs = group(root / "concurrent-source", {entry("concurrent")});
        ProfileSources source;
        source.tracks = refs;
        std::exception_ptr first, second;
        std::thread a([&] {
            try {
                retain_legacy_sources(source, root / "concurrent-kept");
            } catch (...) {
                first = std::current_exception();
            }
        });
        std::thread b([&] {
            try {
                retain_legacy_sources(source, root / "concurrent-kept");
            } catch (...) {
                second = std::current_exception();
            }
        });
        a.join();
        b.join();
        check(!first && !second, "concurrent keeps must serialize safely");
        check(catalogue(root / "concurrent-kept").size() == 1, "concurrent keep duplicated catalogue entry");
        auto traversal = source;
        traversal.tracks[0].item.storage = "../outside";
        const auto before = snapshot(root / "concurrent-kept");
        rejects([&] { retain_legacy_sources(traversal, root / "concurrent-kept"); },
                "unsafe storage identity accepted");
        preserve(root / "concurrent-kept", before);
    }
#ifndef _WIN32
    {
        const auto refs = group(root / "link-source", {entry("linked")});
        const auto path = refs[0].library / "prepared" / refs[0].item.group / refs[0].item.storage / "blobs" /
                          hash("synthetic asset linked");
        std::filesystem::remove(path);
        std::filesystem::create_symlink(root / "tracks/offline/save-sentinel", path);
        ProfileSources source;
        source.tracks = refs;
        rejects([&] { retain_legacy_sources(source, root / "link-kept"); }, "symlink source blob must fail closed");
    }
#endif
    std::cout << checks << " offline retention checks passed; synthetic files only.\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "OFFLINE KEEP FAILURE: " << e.what() << '\n';
    return 1;
}
