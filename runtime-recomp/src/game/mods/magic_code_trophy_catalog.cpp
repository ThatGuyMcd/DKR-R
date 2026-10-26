#include "magic_code_trophy_catalog.hpp"
#include "legacy_track_catalog.hpp"
#include "legacy_asset_directory.hpp"
#include "online_mod_bundle.hpp"
#include <algorithm>
namespace dkr::mods::code::trophy {
Bytes share(const std::vector<Definition>& cups, const std::vector<Course>& courses) {
    if (cups.empty() || cups.size() > 32)
        throw Error("Choose between 1 and 32 cups to export.");
    Json result = {{"format", "dkr-trophy-cups"}, {"version", 1}, {"cups", Json::array()}, {"tracks", Json::array()}};
    std::set<std::string> ids, tracks;
    for (const auto& cup : cups) {
        if (!ids.insert(cup.id).second)
            throw Error("Cup IDs must be unique.");
        result["cups"].push_back(Json::parse(encode(cup)));
        for (const auto& round : freeze(cup, courses, 0).rounds)
            if (tracks.insert(round.course.id).second)
                result["tracks"].push_back(course_document(round.course));
    }
    const auto text = result.dump(2);
    if (text.size() > MaxSharedCups)
        throw Error("These cups are too large to share in one file.");
    return {text.begin(), text.end()};
}
SharedCups read_share(View bytes) {
    const auto doc = parse_document(bytes, MaxSharedCups);
    if (!doc.is_object() || doc.size() != 4 || doc.value("format", std::string{}) != "dkr-trophy-cups" ||
        doc.value("version", 0) != 1 || !doc.contains("cups") || !doc["cups"].is_array() || doc["cups"].empty() ||
        doc["cups"].size() > 32 || !doc.contains("tracks"))
        throw Error("This is not a supported DKR-R trophy cup file.");
    SharedCups result;
    result.tracks = read_catalogue(doc["tracks"]);
    std::set<std::string> ids, used;
    for (const auto& value : doc["cups"]) {
        const auto text = value.dump();
        auto cup = decode(View(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
        if (!ids.insert(cup.id).second)
            throw Error("The cup file contains duplicate cup IDs.");
        for (const auto& round : freeze(cup, result.tracks, 0).rounds)
            used.insert(round.course.id);
        result.cups.push_back(std::move(cup));
    }
    if (used.size() != result.tracks.size())
        throw Error("The cup file contains unused or duplicate track requirements.");
    return result;
}
std::vector<std::string> share_requirements(const SharedCups& shared, const std::vector<Course>& courses) {
    std::vector<std::string> issues;
    for (const auto& required : shared.tracks) {
        const auto found =
            std::find_if(courses.begin(), courses.end(), [&](const auto& c) { return c.id == required.id; });
        if (found == courses.end() || !found->race)
            issues.push_back(required.name + " - install and enable this track");
        else if (!required.id.starts_with("stock:") && required.content != found->content)
            issues.push_back(required.name + " - a different track edition is installed");
    }
    if (issues.empty())
        for (const auto& cup : shared.cups)
            try {
                (void)freeze(cup, courses, 0);
            } catch (const Error& e) {
                issues.push_back(e.what());
            }
    return issues;
}
std::vector<Definition> merge_shared(const std::vector<Definition>& existing, const SharedCups& shared) {
    auto merged = existing;
    for (auto cup : shared.cups) {
        if (std::any_of(merged.begin(), merged.end(), [&](const auto& c) {
                return c.name == cup.name && c.shuffle == cup.shuffle && c.rounds == cup.rounds;
            }))
            continue;
        if (merged.size() >= 32)
            throw Error("There is only room for 32 cups. Remove a cup before importing more.");
        if (std::any_of(merged.begin(), merged.end(), [&](const auto& c) { return c.id == cup.id; })) {
            unsigned next = 1;
            do {
                cup.id = "cup_" + std::to_string(next++);
            } while (std::any_of(merged.begin(), merged.end(), [&](const auto& c) { return c.id == cup.id; }));
        }
        merged.push_back(std::move(cup));
    }
    return merged;
}
namespace {
Bytes encoded(const Json& j) {
    const auto s = j.dump();
    return {s.begin(), s.end()};
}
Course make(unsigned id, std::string name, std::string content, std::string key, View h) {
    if (h.size() < 0x50)
        throw Error("Cup course has an incomplete level header.");
    unsigned vehicles = h[0x4e] & 7, vehicle = h[0x4d];
    if (!vehicles)
        vehicles = 1;
    if (vehicle > 2 || !(vehicles & (1U << vehicle)))
        for (vehicle = 0; vehicle < 3 && !(vehicles & (1U << vehicle)); ++vehicle) {
        }
    if (name.empty())
        name = "Course " + std::to_string(id);
    return {std::move(key), std::move(name), std::move(content), id, vehicles, vehicle, h[0x4c] == 0};
}
} // namespace
Json course_document(const Course& c) {
    return {{"id", c.id},           {"name", c.name},         {"content", c.content},
            {"carrier", c.carrier}, {"vehicles", c.vehicles}, {"default_vehicle", c.default_vehicle},
            {"race", c.race}};
}
std::vector<Course> read_catalogue(const Json& value) {
    if (!value.is_array() || value.size() > 1024)
        throw Error("Invalid trophy course catalogue.");
    std::vector<Course> result;
    for (const auto& c : value) {
        if (c.size() != 7)
            throw Error("Unsupported trophy course metadata.");
        result.push_back({c.at("id").get<std::string>(), c.at("name").get<std::string>(),
                          c.at("content").get<std::string>(), c.at("carrier").get<unsigned>(),
                          c.at("vehicles").get<unsigned>(), c.at("default_vehicle").get<unsigned>(),
                          c.at("race").get<bool>()});
    }
    return result;
}
std::vector<Course> catalogue(const AssetBank& stock, const std::vector<Root>& legacy,
                              const dkr::runtime::custom_tracks::PreparedTracks& labs,
                              const std::map<std::string, unsigned>& legacy_defaults) {
    std::vector<Course> out;
    for (unsigned id = 0; id < stock.record_count(23); ++id) {
        const auto header = stock.record(23, id);
        if (header.size() < 0x50)
            continue;
        const auto names = stock.record(25, id);
        std::string name;
        for (auto c : names) {
            if (!c)
                break;
            if (c >= 32 && c < 127)
                name += char(c);
        }
        const auto identity = stock.base_fingerprint() + ":course:" + std::to_string(id);
        out.push_back(make(id, name,
                           sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()), identity.size())),
                           "stock:" + std::to_string(id), header));
    }
    for (const auto& root : legacy) {
        auto course =
            make(root.carrier, root.name, root.content_id, "legacy:" + root.content_id, stock.record(23, root.carrier));
        course.race = root.race_type == 0;
        if (const auto found = legacy_defaults.find(root.content_id); found != legacy_defaults.end())
            course.default_vehicle = found->second;
        course.vehicles = root.vehicles & 7;
        if (!course.vehicles)
            continue;
        if (course.default_vehicle > 2 || !(course.vehicles & (1U << course.default_vehicle)))
            for (course.default_vehicle = 0;
                 course.default_vehicle < 3 && !(course.vehicles & (1U << course.default_vehicle));
                 ++course.default_vehicle) {
            }
        out.push_back(std::move(course));
    }
    for (const auto& course : labs.courses) {
        const auto track =
            std::find_if(labs.tracks.begin(), labs.tracks.end(), [&](const auto& t) { return t.id == course.id; });
        if (track == labs.tracks.end())
            throw Error("Frozen cup course is missing its Track Lab package.");
        const auto header = std::find_if(track->entries.begin(), track->entries.end(), [](const auto& e) {
            return e.section == dkr::runtime::custom_tracks::Section::LevelHeaders;
        });
        if (header == track->entries.end())
            throw Error("Cup course has no Track Lab header.");
        out.push_back(make(unsigned(course.level_id), course.name, sha256(online::pack_track_lab(track->source)),
                           "dkrmap:" + course.id, header->bytes));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return out;
}
Json sdk2_inventory(const AssetBank& stock, Json characters) {
    std::sort(characters.begin(), characters.end(), [](const auto& a, const auto& b) {
        return a.at("id").template get<std::string>() < b.at("id").template get<std::string>();
    });
    Json banks = Json::object();
    for (const auto [name, section] : std::array<std::pair<const char*, unsigned>, 6>{{{"models", 29},
                                                                                       {"textures-3d", 2},
                                                                                       {"textures-2d", 4},
                                                                                       {"sprites", 12},
                                                                                       {"object-templates", 34},
                                                                                       {"level-models", 27}}})
        banks[name] = {{"section", section},
                       {"count", stock.record_count(section)},
                       {"bank", stock.base_fingerprint()},
                       {"scope", "native-base"}};
    return {{"schema", 2}, {"banks", banks}, {"characters", characters}};
}
void verify_sdk2_inventory(const FrozenSet& code, const AssetBank& stock, const CharacterNamespace* names) {
    Json characters = Json::array();
    if (names)
        for (const auto& item : names->characters)
            characters.push_back({{"id", item.id}, {"name", item.name}, {"donor", item.base_character}});
    const auto actual = sdk2_inventory(stock, std::move(characters));
    for (const auto& package : code.ordered)
        if (package->api == 2 && package->modules.contains("catalogues2")) {
            const auto& expected = code.data.at(package->id).at("_sdk2_catalogue").at("inventory");
            if (expected.at("schema") != 2 || expected.at("banks") != actual.at("banks") ||
                expected.at("characters").size() != actual.at("characters").size())
                throw Error("SDK resource inventory differs from admitted native assets.");
            for (unsigned i = 0; i < actual.at("characters").size(); ++i)
                if (expected.at("characters")[i].at("id") != actual.at("characters")[i].at("id") ||
                    expected.at("characters")[i].at("donor") != actual.at("characters")[i].at("donor"))
                    throw Error("SDK character catalogue differs from the admitted character namespace.");
        }
}
std::vector<Course> installed_catalogue(const std::filesystem::path& rom, const std::filesystem::path& legacy,
                                        const std::vector<std::filesystem::path>& paths, std::stop_token stop,
                                        bool canonical_order, Json* inventory) {
    auto bytes = read_file(rom, MaxImage);
    canonicalize_rom(bytes);
    const auto stock = AssetBank::stock(std::move(bytes));
    if (inventory) {
        Json characters = Json::array();
        for (const auto& item : TrackCatalog::selected_items(legacy, stock->revision(), TrackCatalog::Kind::Character))
            characters.push_back({{"id", item.id}, {"name", item.name}, {"donor", item.base_character}});
        *inventory = sdk2_inventory(*stock, std::move(characters));
    }
    if (stop.stop_requested())
        throw Error("Cup catalogue cancelled.");
    std::vector<Root> roots;
    std::map<std::string, unsigned> defaults;
    if (TrackCatalog::has_enabled(legacy))
        for (const auto& t : TrackCatalog::load_enabled(legacy, stock)) {
            const auto header = t.bank->record(23, t.root.carrier);
            if (header.size() < 0x50)
                throw Error("A custom course is missing its default vehicle header.");
            roots.push_back(t.root);
            defaults.emplace(t.root.content_id, header[0x4d]);
        }
    const auto directory = AssetDirectory::build(stock);
    constexpr unsigned Tables[] = {22, 20, 24, 26, 3, 5, 13};
    std::array<std::vector<std::int32_t>, 7> tables;
    for (unsigned i = 0; i < 7; ++i) {
        const auto data = directory->read(Tables[i], 0, directory->section_size(Tables[i]));
        for (std::size_t at = 0; at + 4 <= data.size(); at += 4) {
            tables[i].push_back(std::int32_t(be32(data, at)));
            if (tables[i].back() == -1)
                break;
        }
    }
    std::vector<dkr::runtime::custom_tracks::Track> tracks;
    for (const auto& path : paths) {
        if (stop.stop_requested())
            throw Error("Cup catalogue cancelled.");
        dkr::runtime::custom_tracks::Track track;
        std::string error;
        if (!dkr::runtime::custom_tracks::inspect_package(path, track, error))
            throw Error(error);
        tracks.push_back(std::move(track));
    }
    if (canonical_order)
        std::sort(tracks.begin(), tracks.end(), [](const auto& a, const auto& b) {
            const auto key = [](const auto& t) {
                const auto id = "dkrmap:" + t.id;
                return sha256(View(reinterpret_cast<const std::uint8_t*>(id.data()), id.size()));
            };
            return key(a) < key(b);
        });
    return catalogue(*stock, roots, dkr::runtime::custom_tracks::prepare_tracks(std::move(tracks), tables), defaults);
}
Json publication(const std::vector<Definition>& cups, std::string_view selected, const std::vector<Course>& courses,
                 std::uint64_t seed) {
    if (cups.empty() || cups.size() > 32)
        throw Error("Keep between 1 and 32 named cups.");
    Json result = {
        {"schema", 1}, {"selected", selected}, {"seed", seed}, {"cups", Json::array()}, {"catalogue", Json::array()}};
    std::set<std::string> ids;
    for (const auto& cup : cups) {
        if (!ids.insert(cup.id).second)
            throw Error("Cup IDs must be unique.");
        result["cups"].push_back(Json::parse(encode(cup)));
    }
    const auto cup = std::find_if(cups.begin(), cups.end(), [&](const auto& d) { return d.id == selected; });
    if (cup == cups.end())
        throw Error("Select a cup before saving.");
    const auto frozen = freeze(*cup, courses, seed);
    result["active"] = Json::parse(encode(frozen));
    std::set<std::string> used;
    for (const auto& definition : cups)
        for (const auto& round : freeze(definition, courses, seed).rounds)
            if (used.insert(round.course.id).second)
                result["catalogue"].push_back(course_document(round.course));
    if (result.dump().size() > 60000)
        throw Error("Cup definitions exceed 60 KiB. Remove unused cups or rounds.");
    return result;
}
Frozen active(const Json& data) {
    if (data.size() != 6 || data.value("schema", 0) != 1)
        throw Error("Create and save a custom cup first.");
    return verify(encoded(data.at("active")), read_catalogue(data.at("catalogue")));
}
std::vector<Frozen> all(const Json& data) {
    const auto chosen = active(data);
    const auto courses = read_catalogue(data.at("catalogue"));
    if (!data.at("cups").is_array() || data.at("cups").empty() || data.at("cups").size() > 32)
        throw Error("Invalid custom trophy menu.");
    std::set<std::string> ids;
    std::vector<Frozen> result;
    bool selected = false;
    for (const auto& value : data.at("cups")) {
        auto definition = decode(encoded(value));
        if (!ids.insert(definition.id).second)
            throw Error("Duplicate custom cup identity.");
        auto cup = freeze(definition, courses, data.at("seed").get<std::uint64_t>());
        if (definition.id == data.at("selected").get<std::string>()) {
            selected = true;
            if (cup.identity != chosen.identity)
                throw Error("Selected cup differs from its frozen definition.");
        }
        result.push_back(std::move(cup));
    }
    if (!selected)
        throw Error("Selected cup is not in its menu.");
    return result;
}
std::string menu_id(std::string_view package, std::string_view cup) {
    const auto key = std::string(package) + "\n" + std::string(cup);
    return sha256(View(reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
}
std::vector<TrackMenuCup> menu_entries(const FrozenSet& code) {
    std::vector<TrackMenuCup> entries;
    for (const auto& package : code.ordered)
        if (package->modules.contains("trophies"))
            for (const auto& cup : all(code.data.at(package->id).at("trophies"))) {
                const auto& first = cup.rounds.front();
                entries.push_back({menu_id(package->id, cup.definition.id),
                                   cup.definition.name,
                                   {first.course.id.starts_with("legacy:") ? first.course.id.substr(7) : std::string{},
                                    first.course.carrier},
                                   first.vehicle});
            }
    if (entries.size() > 32)
        throw Error("Custom trophy menu exceeds 32 cups.");
    return entries;
}
void verify_configuration(const FrozenSet& code, const std::vector<Course>& courses) {
    Json catalogue = Json::array();
    for (const auto& course : courses)
        catalogue.push_back(course_document(course));
    for (const auto& p : code.ordered)
        if (p->api == 2 && p->modules.contains("catalogues2")) {
            const auto& data = code.data.at(p->id);
            if (!data.contains("_sdk2_catalogue") || data.at("_sdk2_catalogue").at("schema") != 2 ||
                data.at("_sdk2_catalogue").at("courses") != catalogue)
                throw Error(p->name + ": frozen SDK 2 resource catalogue differs from the admitted game assets.");
        }
    for (const auto& package : code.ordered)
        for (const auto& required : package->tracks)
            if (std::none_of(courses.begin(), courses.end(), [&](const auto& course) { return course.id == required; }))
                throw Error(package->name + " requires an enabled course: " + required);
    for (const auto& package : code.ordered)
        if (package->modules.contains("trophies")) {
            const auto& data = code.data.at(package->id).at("trophies");
            for (const auto& cup : all(data))
                (void)verify(encode(cup), courses);
        }
}
void rebind(FrozenSet& code, const std::vector<Course>& courses, std::string_view revision, const Json& inventory) {
    Json catalogue = Json::array();
    for (const auto& course : courses)
        catalogue.push_back(course_document(course));
    for (const auto& p : code.ordered)
        if (p->api == 2 && p->modules.contains("catalogues2")) {
            auto& data = code.data[p->id];
            if (data.is_null())
                data = Json::object();
            data["_sdk2_catalogue"] = {{"schema", 2}, {"courses", catalogue}, {"inventory", inventory}};
            (void)structured_data(data);
        }
    for (const auto& package : code.ordered)
        for (const auto& required : package->tracks)
            if (std::none_of(courses.begin(), courses.end(), [&](const auto& course) { return course.id == required; }))
                throw Error(package->name + " requires an enabled course: " + required);
    for (const auto& package : code.ordered)
        if (package->modules.contains("trophies")) {
            auto& data = code.data.at(package->id).at("trophies");
            // Older saved publications only catalogued the editor's selected cup.
            // Preserve every definition and resolve the others against admission's
            // enabled courses, then freeze all of them into the session identity.
            (void)active(data);
            std::vector<Definition> cups;
            for (const auto& cup : data.at("cups"))
                cups.push_back(decode(encoded(cup)));
            const auto previous = read_catalogue(data.at("catalogue"));
            for (const auto& cup : cups)
                for (const auto& round : cup.rounds) {
                    const auto actual = std::find_if(courses.begin(), courses.end(),
                                                     [&](const auto& c) { return c.id == round.course; });
                    const auto old = std::find_if(previous.begin(), previous.end(),
                                                  [&](const auto& c) { return c.id == round.course; });
                    const auto name = old == previous.end() ? round.course : old->name;
                    if (actual == courses.end() || !actual->race)
                        throw Error("Cup \"" + cup.name + "\": \"" + name +
                                    "\" is not an enabled race track. Enable it or change the cup before hosting.");
                    if (old != previous.end() && !round.course.starts_with("stock:") && actual->content != old->content)
                        throw Error("Cup \"" + cup.name + "\": \"" + name +
                                    "\" has changed. Refresh tracks and save this cup in Custom Magic Codes.");
                }
            data = publication(cups, data.at("selected").get<std::string>(), courses,
                               data.at("seed").get<std::uint64_t>());
        }
    code.identity = session_identity(code.ordered, code.settings, revision, code.data);
}
} // namespace dkr::mods::code::trophy
