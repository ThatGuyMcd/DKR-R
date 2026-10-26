#include "legacy_track_catalog.hpp"
#include "legacy_mod_browser.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace dkr::mods;
using nlohmann::json;
unsigned checks = 0;
void check(bool ok) {
    ++checks;
    if (!ok)
        throw Error("Mod management assertion " + std::to_string(checks));
}
void write(const std::filesystem::path& path, const json& value) {
    const auto text = value.dump();
    write_new_file(path, View(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
std::string hash(const json& value) {
    const auto text = value.dump();
    return sha256(View(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
void wait(TrackCatalog& catalog, bool expected = true) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (catalog.snapshot()->busy && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(!catalog.snapshot()->busy);
    if (catalog.snapshot()->succeeded != expected)
        throw Error(catalog.snapshot()->result);
    check(catalog.snapshot()->succeeded == expected);
}
json entry(char id, const char* name, bool character = false, bool second = false) {
    const std::string identity(64, id), storage(64, second ? 'f' : id);
    json value = {{"id", identity},
                  {"storage", storage},
                  {"name", name},
                  {"revision", second ? "us.v80" : "us.v77"},
                  {"artifact", hash(json::object())},
                  {"patch", std::string(64, '9')},
                  {"vehicles", 7}};
    if (character)
        value["behaviour"] = 0;
    else {
        value["bank"] = identity;
        value["carrier"] = 4;
        value["race_type"] = 0;
    }
    return value;
}
json receipt(const json& entries, bool characters = false) {
    return {{"schema", 1},
            {"adapter", characters ? CharacterAdapterVersion : TrackAdapterVersion},
            {"enabled", false},
            {characters ? "characters" : "tracks", entries},
            {"blocked", json::array()}};
}
// Synthetic catalogue/management fixtures only. These are deliberately not
// playable artifacts and are never passed to the runtime asset loader.
std::filesystem::path install(const std::filesystem::path& prepared, const json& data, bool chars = false) {
    const auto folder = prepared / hash(data);
    std::filesystem::create_directories(folder);
    write(folder / "prepared.json", data);
    for (const auto& item : data.at(chars ? "characters" : "tracks")) {
        const auto path = folder / item.at("storage").get<std::string>();
        std::filesystem::create_directory(path);
        write(path / (chars ? "character.json" : "track.json"), json::object());
        write(path / "payload.json", {{"untouched", true}});
    }
    return folder;
}
} // namespace
int main() {
    const auto root = private_storage_path(
        std::filesystem::current_path() /
        ("mod-management-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        std::filesystem::create_directory(root);
        write(root / "save-sentinel.json", {{"save", true}});
        const std::string a(64, 'a'), b(64, 'b'), c(64, 'c');
        const auto original =
            receipt(json::array({entry('a', "Long Custom Track Name"),
                                 entry('a', "Long Custom Track Name", false, true), entry('b', "Surviving Track")}));
        const auto initial = install(root / "prepared", original);
        install(root / "prepared-characters",
                receipt(json::array({entry('a', "Mario", true), entry('b', "Dixie", true), entry('c', "Link", true)}),
                        true),
                true);
        {
            TrackCatalog tracks, characters(TrackCatalog::Kind::Character);
            tracks.configure(root, {});
            characters.configure(root, {});
            wait(tracks);
            wait(characters);
            check(tracks.snapshot()->tracks.size() == 3);
            check(tracks.snapshot()->size_scans == 3 && tracks.snapshot()->display_cache_hits == 0);
            check(characters.snapshot()->size_scans == 3);
            const auto cards = browser::cards(*tracks.snapshot(), false);
            check(cards.size() == 2);
            check(cards[0].revisions == 3);
            check(cards[0].item.managed_bytes > 0);
            browser::Filters filter;
            filter.revision = 2;
            filter.query = "LONG";
            check(browser::select(cards, filter).size() == 1);
            filter.query = "";
            filter.compatibility = 2;
            check(browser::select(cards, filter).size() == 1);
            const auto inactive = characters.snapshot();
            for (const auto& id : {a, b}) {
                check(characters.set_enabled(id, true));
                wait(characters);
                check(characters.snapshot()->display_cache_hits == 3 && characters.snapshot()->size_scans == 0);
                check(characters.snapshot()->source_reads == 0);
            }
            for (const auto& item : inactive->tracks)
                check(!item.enabled); // immutable prior snapshot
            // The current same-stage roster admits more than the original
            // two mods. Keep this fixture aligned with the existing feature.
            check(characters.set_enabled(c, true));
            wait(characters);
            check(browser::active_count(browser::cards(*characters.snapshot(), true)) == 3);
            check(characters.set_hidden(a, true));
            wait(characters);
            check(characters.snapshot()->size_scans == 0 && characters.snapshot()->display_cache_hits == 3);
            check(browser::active_count(browser::cards(*characters.snapshot(), true)) == 2);
            check(characters.set_enabled(a, true));
            wait(characters, false);
            check(characters.set_hidden(a, false));
            wait(characters);
            check(!characters.snapshot()->tracks[1].hidden); // names are sorted, not activation order.
            check(characters.set_enabled(c, true));
            wait(characters);
            check(tracks.set_enabled(a, true));
            wait(tracks);
            check(tracks.set_enabled(b, true));
            wait(tracks);
            check(tracks.snapshot()->size_scans == 0 && tracks.snapshot()->display_cache_hits == 3);
            for (const auto& item : tracks.snapshot()->tracks)
                check(item.enabled); // both revision variants change together
            check(tracks.set_hidden(a, true));
            wait(tracks);
            for (const auto& item : tracks.snapshot()->tracks)
                if (item.id == a)
                    check(item.hidden && !item.enabled);
            check(tracks.set_hidden(a, false));
            wait(tracks);
            check(tracks.remove("../bad"));
            wait(tracks, false);
            check(std::filesystem::exists(initial));
            check(tracks.remove(a));
            wait(tracks);
            check(tracks.snapshot()->tracks.size() == 1);
            const auto survivor = tracks.snapshot()->tracks.front();
            check(survivor.id == b && survivor.enabled);
            check(!std::filesystem::exists(initial));
            check(read_file(root / "prepared" / survivor.group / survivor.storage / "payload.json", 1024) ==
                  read_file(root / "prepared-characters" / characters.snapshot()->tracks.front().group /
                                characters.snapshot()->tracks.front().storage / "payload.json",
                            1024));
            check(std::filesystem::exists(root / "save-sentinel.json"));
            check(characters.snapshot()->tracks.size() == 3);
            check(tracks.refresh());
            wait(tracks);
            check(tracks.snapshot()->tracks.size() == 1);
            check(std::filesystem::is_empty(root / "prepare-jobs"));
            check(tracks.remove(b));
            wait(tracks);
            check(tracks.snapshot()->tracks.empty());
            check(characters.remove(c));
            wait(characters);
            check(characters.snapshot()->tracks.size() == 2);
        }
        // Selection only reuses display metadata for identical verified
        // receipts; fresh manifests and activation files are still checked.
        for (const bool chars : {false, true}) {
            const auto library = root / (chars ? "warm-characters" : "warm-tracks");
            const auto review = json{{"packages", json::array({json{{"patch_sha256", std::string(64, '9')},
                                                                    {"display_name", "Test source.zip"}}})}};
            const auto review_id = hash(review);
            const auto review_dir = library / "reviews" / review_id;
            std::filesystem::create_directories(review_dir);
            write(review_dir / "review.json", review);
            auto data = receipt(json::array({entry('a', "Same Name", chars), entry('b', "Same Name", chars)}), chars);
            data["review"] = review_id;
            auto folder = install(library / (chars ? "prepared-characters" : "prepared"), data, chars);
            TrackCatalog catalog(chars ? TrackCatalog::Kind::Character : TrackCatalog::Kind::Track);
            catalog.configure(library, {});
            wait(catalog);
            check(catalog.snapshot()->source_reads == 1 && catalog.snapshot()->size_scans == 2);
            check(catalog.snapshot()->tracks.front().source_name == "Test source.zip");
            check(catalog.set_enabled(a, true));
            wait(catalog);
            check(catalog.snapshot()->display_cache_hits == 2 && catalog.snapshot()->size_scans == 0 &&
                  catalog.snapshot()->source_reads == 0);
            // Even after warm-up, changing a manifest fails closed.
            const auto manifest = folder / a / (chars ? "character.json" : "track.json");
            const auto retained = library / "retained-manifest.json";
            std::filesystem::rename(manifest, retained);
            write(manifest, {{"tampered", true}});
            check(catalog.set_enabled(b, true));
            wait(catalog, false);
            bool rejected = false;
            try {
                TrackCatalog::selected_items(library, "us.v77",
                                             chars ? TrackCatalog::Kind::Character : TrackCatalog::Kind::Track);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected);
            std::filesystem::remove(manifest);
            std::filesystem::rename(retained, manifest);
            check(catalog.set_enabled(b, true));
            wait(catalog);
            check(catalog.set_enabled(a, false));
            wait(catalog);
            check(catalog.snapshot()->size_scans == 0);
            // A new hashed receipt cannot inherit old display metadata.
            data[chars ? "characters" : "tracks"][0]["name"] = "Updated edition";
            const auto replacement = folder.parent_path() / hash(data);
            std::filesystem::rename(folder, replacement);
            folder = replacement;
            std::filesystem::remove(folder / "prepared.json");
            write(folder / "prepared.json", data);
            check(catalog.set_enabled(a, true));
            wait(catalog);
            check(catalog.snapshot()->size_scans == 2 && catalog.snapshot()->display_cache_hits == 0);
            check(catalog.refresh());
            wait(catalog);
            check(catalog.snapshot()->size_scans == 2 && catalog.snapshot()->source_reads == 1);
            check(catalog.remove(a));
            wait(catalog);
            check(catalog.snapshot()->tracks.size() == 1 && catalog.snapshot()->tracks.front().id == b &&
                  catalog.snapshot()->tracks.front().enabled);
            check(std::filesystem::exists(review_dir / "review.json"));
        }
        // Recovery at each publication boundary: journal, retirement, publish.
        const std::string job_id(64, '8');
        for (int stage = 0; stage < 3; ++stage) {
            const auto recovery = root / ("recovery" + std::to_string(stage));
            const auto old = install(recovery / "prepared", original);
            const auto job = recovery / "prepare-jobs" / job_id;
            std::filesystem::create_directories(job / "retired");
            const auto survivors = receipt(json::array({entry('b', "Surviving Track")}));
            const auto next = install(job / "incoming", survivors);
            write(recovery / "track-removal.json", {{"schema", 1},
                                                    {"id", a},
                                                    {"job", job_id},
                                                    {"old", json::array({hash(original)})},
                                                    {"new", json::array({hash(survivors)})}});
            if (stage >= 1)
                std::filesystem::rename(old, job / "retired" / hash(original));
            if (stage >= 2)
                std::filesystem::rename(next, recovery / "prepared" / hash(survivors));
            TrackCatalog catalog;
            catalog.configure(recovery, {});
            wait(catalog);
            check(catalog.snapshot()->tracks.size() == 1);
            check(catalog.snapshot()->tracks.front().id == b);
            check(!std::filesystem::exists(old));
            check(!std::filesystem::exists(job));
        }
        // A corrupted recovery journal must not discard surviving entries.
        const auto broken = root / "broken";
        const auto preserved = install(broken / "prepared", original);
        const auto badjob = broken / "prepare-jobs" / job_id;
        std::filesystem::create_directories(badjob / "retired");
        std::filesystem::create_directory(badjob / "incoming");
        write(broken / "track-removal.json", {{"schema", 1},
                                              {"id", a},
                                              {"job", job_id},
                                              {"old", json::array({hash(original)})},
                                              {"new", json::array()}});
        {
            TrackCatalog catalog;
            catalog.configure(broken, {});
            wait(catalog, false);
            check(std::filesystem::exists(preserved));
        }
        // Failure to read/write presentation metadata never deletes content.
        const auto unwritable = root / "unwritable";
        const auto kept = install(unwritable / "prepared", original);
        std::filesystem::create_directory(unwritable / "track-library.json");
        {
            TrackCatalog catalog;
            catalog.configure(unwritable, {});
            wait(catalog, false);
            check(catalog.remove(a));
            wait(catalog, false);
            check(std::filesystem::exists(kept));
        }
        // Links inside managed content may not be followed by copy or removal.
        const auto linked = root / "linked";
        const auto linked_group = install(linked / "prepared", original);
        const auto outside = root / "outside";
        std::filesystem::create_directory(outside);
        write(outside / "sentinel.json", {{"safe", true}});
        std::error_code link_error;
        std::filesystem::create_directory_symlink(outside, linked_group / a / "redirect", link_error);
        if (!link_error) {
            TrackCatalog catalog;
            catalog.configure(linked, {});
            wait(catalog, false);
            check(catalog.remove(a));
            wait(catalog, false);
            check(std::filesystem::exists(outside / "sentinel.json"));
            check(std::filesystem::exists(linked_group));
        }
        // Stable filtering, hidden-state counts and dates with unknowns last.
        TrackCatalogView view;
        TrackCatalogItem item;
        item.id = a;
        item.name = "Zebra";
        item.enabled = true;
        item.revision = "us.v77";
        view.tracks.push_back(item);
        item.id = b;
        item.name = "Alpha";
        item.hidden = true;
        item.enabled = false;
        item.imported_at = 123;
        view.tracks.push_back(item);
        const auto cards = browser::cards(view, true);
        browser::Filters filter;
        filter.visibility = 1;
        filter.sort = 5;
        check(browser::select(cards, filter).front().item.id == b);
        check(browser::active_count(cards) == 1);
        filter.visibility = 2;
        check(browser::select(cards, filter).size() == 1);
        check(!browser::can_activate(cards[0], true, MaxActiveStageCharacters, 1, false));
        check(!browser::can_activate(cards[0], true, 0, 1, true));
        // Revision variants and repeated IDs are one installed mod, not copies.
        const auto duplicates = browser::duplicates({{a, " Mario ", "patch:0"},
                                                     {a, " Mario ", "patch:0"},
                                                     {b, "MARIO", "other:0"},
                                                     {c, "Mario Deluxe", "patch:0"},
                                                     {"d", "Other", "patch:1"},
                                                     {"e", "", {}}});
        check(browser::name_key("  Some   Name \t ") == "some name");
        check(duplicates.at(a).same_name == 2 && duplicates.at(a).same_source == 2);
        check(duplicates.at(b).matches() && !duplicates.at(b).matches(2));
        check(duplicates.at(c).matches(2) && !duplicates.at("d").matches());
        check(!duplicates.at("e").matches());
        std::filesystem::remove_all(root);
        std::cout << checks << " management, recovery and browser checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "; fixture retained at " << root << '\n';
        return 1;
    }
}
