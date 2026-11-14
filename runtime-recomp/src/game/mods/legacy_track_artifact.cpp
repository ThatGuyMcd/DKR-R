#include "legacy_track_artifact.hpp"
#include "legacy_audio_bank.hpp"
#include "legacy_mod_geometry.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <set>

namespace dkr::mods {
namespace {
using nlohmann::json;
bool digest(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void normal_directory(const std::filesystem::path& path) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_directory(path))
        throw Error("Prepared content must be stored in an ordinary directory.");
}
bool known_profile(std::string_view name) {
    return name == "sixtyfour-big-boo-2018" || name == "community-track-pack" || name == "native-asset-graph-v1";
}
std::string text(const json& value, const char* field, std::size_t max = 256) {
    auto result = value.at(field).get<std::string>();
    if (result.empty() || result.size() > max ||
        std::any_of(result.begin(), result.end(), [](unsigned char c) { return c < 32; }))
        throw Error("Invalid prepared-content text field.");
    return result;
}
void validate_record(unsigned section, View bytes) {
    if (bytes.empty())
        throw Error("Prepared content contains an empty record.");
    switch (section) {
    case 2:
    case 4:
        validate_texture_record(bytes);
        break;
    case 12:
        inspect_sprite_textures(bytes);
        break;
    case 27:
        validate_geometry(inflate_asset(bytes, 4 * MiB), bytes.size());
        break;
    case 29:
        inspect_model_textures(bytes);
        break;
    case 39:
        inspect_sequence_directory(bytes);
        break;
    case 34:
        slice(bytes, 0, 0x78);
        break; // native signature/field ownership checked against stock below
    case 21:
    case 23:
    case 32:
        break; // root/profile validation was performed by the isolated worker
    default:
        throw Error("Prepared content uses an unallocated asset section.");
    }
}
} // namespace
std::string write_track_artifact(const std::filesystem::path& destination, const TrackMaterialization& track,
                                 const std::string& patch, const std::string& profile) {
    validate_behaviour(track.root.behaviour);
    if (!track.bank || !track.directory || !digest(patch) || !known_profile(profile) ||
        track.bank->digest() != track.root.content_id || !track.root.blockers.empty())
        throw Error("Only a reviewed, internally consistent track can be prepared.");
    normal_directory(destination.parent_path());
    if (!std::filesystem::create_directory(destination))
        throw Error("Prepared content must not overwrite an existing directory.");
    // The caller owns the exclusive transaction and its failure cleanup.
    std::filesystem::create_directory(destination / "blobs");
    const auto& root = track.root;
    json manifest = {{"schema", TrackArtifactSchema},
                     {"adapter", TrackAdapterVersion},
                     {"profile", profile},
                     {"patch", patch},
                     {"base", track.bank->base_fingerprint()},
                     {"revision", track.bank->revision()},
                     {"bank", track.bank->fingerprint()},
                     {"id", root.content_id},
                     {"name", root.name},
                     {"carrier", root.carrier},
                     {"geometry", root.geometry},
                     {"objects", root.object_map},
                     {"collectables", root.collectables},
                     {"race_type", root.race_type},
                     {"vehicles", root.vehicles},
                     {"records", json::array()}};
    std::set<std::string> stored;
    std::set<AssetKey> keys;
    std::size_t total = 0;
    if (root.behaviour.abi)
        manifest["behaviour_contract"] = json::parse(encode_behaviour(root.behaviour));
    for (const auto& key : track.included) {
        if (!keys.insert(key).second || keys.size() > 4096)
            throw Error("Invalid prepared record set.");
        const auto bytes = track.bank->record(key.first, key.second);
        validate_record(key.first, bytes);
        const auto hash = sha256(bytes);
        manifest["records"].push_back(
            {{"section", key.first}, {"record", key.second}, {"digest", hash}, {"size", bytes.size()}});
        if (stored.insert(hash).second) {
            if (bytes.size() > MaxStaged - total)
                throw Error("Prepared content exceeds its storage budget.");
            write_new_file(destination / "blobs" / hash, bytes);
            total += bytes.size();
        }
    }
    const auto encoded = manifest.dump();
    if (encoded.size() > MiB)
        throw Error("Prepared content manifest is too large.");
    const View bytes(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size());
    write_new_file(destination / "track.json", bytes);
    return sha256(bytes);
}
PreparedTrack read_track_artifact(const std::filesystem::path& directory, const std::string& expected,
                                  std::shared_ptr<const AssetBank> stock) {
    if (!digest(expected) || !stock || !stock->digest().empty())
        throw Error("Prepared content requires a verified base and manifest digest.");
    normal_directory(directory);
    normal_directory(directory / "blobs");
    if (std::filesystem::is_symlink(directory / "track.json"))
        throw Error("Prepared manifest must not be a link.");
    const auto encoded = read_file(directory / "track.json", MiB);
    if (sha256(encoded) != expected)
        throw Error("Prepared content manifest failed its integrity check.");
    const auto manifest = json::parse(encoded.begin(), encoded.end(), [](int depth, json::parse_event_t, json&) {
        if (depth > 8)
            throw Error("Prepared content nesting exceeds its limit.");
        return true;
    });
    if (manifest.at("schema") != TrackArtifactSchema || manifest.at("adapter") != TrackAdapterVersion ||
        manifest.at("base") != stock->fingerprint() || manifest.at("revision") != stock->revision())
        throw Error("Prepared content needs regeneration for this Game Pak or adapter version.");
    PreparedTrack result;
    result.profile = text(manifest, "profile");
    result.patch_digest = text(manifest, "patch");
    if (!known_profile(result.profile) || !digest(result.patch_digest))
        throw Error("Prepared content has no reviewed adapter.");
    result.root.content_id = text(manifest, "id");
    result.root.name = text(manifest, "name");
    if (manifest.contains("behaviour_contract"))
        result.root.behaviour = decode_behaviour(manifest.at("behaviour_contract").dump());
    result.root.carrier = manifest.at("carrier").get<unsigned>();
    result.root.geometry = manifest.at("geometry").get<unsigned>();
    result.root.object_map = manifest.at("objects").get<unsigned>();
    result.root.collectables = manifest.at("collectables").get<unsigned>();
    result.root.race_type = manifest.at("race_type").get<unsigned>();
    result.root.vehicles = manifest.at("vehicles").get<unsigned>();
    if (!digest(result.root.content_id) || result.root.carrier >= stock->record_count(23) ||
        result.root.geometry >= stock->record_count(27) ||
        (result.root.object_map != 65535 && result.root.object_map >= stock->record_count(21)) ||
        (result.root.collectables != 65535 && result.root.collectables >= stock->record_count(21)) ||
        result.root.vehicles > 7 || result.root.vehicles == 0 || result.root.race_type > 255)
        throw Error("Prepared course metadata is out of range.");
    const auto& records = manifest.at("records");
    if (!records.is_array() || records.empty() || records.size() > 4096)
        throw Error("Invalid prepared record count.");
    AssetBank::Overrides overrides;
    std::size_t total = 0;
    for (const auto& record : records) {
        const AssetKey key{record.at("section").get<unsigned>(), record.at("record").get<unsigned>()};
        const auto hash = text(record, "digest");
        const auto size = record.at("size").get<std::size_t>();
        if (!digest(hash) || size == 0 || size > MaxImage || size > MaxStaged - total || overrides.contains(key))
            throw Error("Prepared record descriptor is invalid or exceeds its budget.");
        const auto path = directory / "blobs" / hash;
        if (std::filesystem::is_symlink(path))
            throw Error("Prepared assets must not be links.");
        auto bytes = read_file(path, size);
        if (bytes.size() != size || sha256(bytes) != hash)
            throw Error("A prepared asset failed its integrity check.");
        validate_record(key.first, bytes);
        if (key.first == 39)
            validate_changed_sequences(stock->record(key.first, key.second), bytes);
        if (key.first == 34 &&
            (object_parameter_fields(stock->record(34, key.second), bytes) & ~result.root.behaviour.header_fields))
            throw Error("Prepared object parameters exceed their course behaviour contract.");
        total += size;
        overrides.emplace(key, std::move(bytes));
    }
    if (!overrides.contains({23, result.root.carrier}))
        throw Error("Prepared course is missing its owned level header.");
    result.bank = AssetBank::derive(std::move(stock), result.root.content_id, std::move(overrides));
    if (result.bank->fingerprint() != text(manifest, "bank"))
        throw Error("Prepared course bank identity does not match its assets.");
    const auto header = result.bank->record(23, result.root.carrier);
    if (header.size() < 0xc4 || be16(header, 0x34) != result.root.geometry ||
        be16(header, 0xba) != result.root.object_map || be16(header, 0x36) != result.root.collectables ||
        header[0x4c] != result.root.race_type || (result.root.vehicles & header[0x4e]) != result.root.vehicles)
        throw Error("Prepared course header disagrees with its catalogue metadata.");
    result.artifact_digest = expected;
    return result;
}
} // namespace dkr::mods
