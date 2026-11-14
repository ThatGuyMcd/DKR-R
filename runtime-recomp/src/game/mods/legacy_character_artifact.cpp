#include "legacy_character_artifact.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <set>

namespace dkr::mods {
namespace {
using nlohmann::json;
bool digest(std::string_view s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void ordinary(const std::filesystem::path& p) {
    if (std::filesystem::is_symlink(p) || !std::filesystem::is_directory(p))
        throw Error("Character storage must use ordinary directories.");
}
std::string text(const json& j, const char* key) {
    auto s = j.at(key).get<std::string>();
    if (s.empty() || s.size() > 256 || std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32; }))
        throw Error("Invalid character metadata text.");
    return s;
}
} // namespace
std::string write_character_artifact(const std::filesystem::path& folder, const PreparedCharacter& c,
                                     const AssetBank& stock) {
    if (validate_prepared_character(c, stock) != c.root.content_id)
        throw Error("Character identity changed before persistence.");
    ordinary(folder.parent_path());
    if (!std::filesystem::create_directory(folder))
        throw Error("Refusing to overwrite prepared character assets.");
    std::filesystem::create_directory(folder / "blobs");
    json value = {{"schema", 1},
                  {"adapter", CharacterAdapterVersion},
                  {"base", c.base_fingerprint},
                  {"revision", c.source_revision},
                  {"id", c.root.content_id},
                  {"name", c.root.name},
                  {"behaviour", c.root.base_character},
                  {"portrait", c.root.portrait},
                  {"headers", c.root.headers},
                  {"stage_header", c.stage_header},
                  {"records", json::array()},
                  {"animations", json::array()}};
    std::set<std::string> written;
    if (c.root.behaviour.abi)
        value["behaviour_contract"] = json::parse(encode_behaviour(c.root.behaviour));
    value["audio"] = {{"control", sha256(c.audio.control)},
                      {"samples", sha256(c.audio.samples)},
                      {"control_size", c.audio.control.size()},
                      {"sample_size", c.audio.samples.size()},
                      {"cues", json::array()}};
    for (const auto* blob : {&c.audio.control, &c.audio.samples}) {
        const auto hash = sha256(*blob);
        if (written.insert(hash).second)
            write_new_file(folder / "blobs" / hash, *blob);
    }
    for (const auto& cue : c.audio.cues)
        value["audio"]["cues"].push_back({cue.sound, cue.volume, cue.pitch, cue.priority});
    for (const auto& [key, bytes] : c.records) {
        const auto hash = sha256(bytes);
        if (written.insert(hash).second)
            write_new_file(folder / "blobs" / hash, bytes);
        value["records"].push_back(
            {{"section", key.first}, {"record", key.second}, {"hash", hash}, {"size", bytes.size()}});
    }
    for (const auto& [model, animations] : c.model_animations)
        value["animations"].push_back({{"model", model}, {"ids", animations}});
    const auto encoded = value.dump();
    if (encoded.size() > MiB)
        throw Error("Character manifest is too large.");
    const View bytes(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size());
    write_new_file(folder / "character.json", bytes);
    return sha256(bytes);
}
PreparedCharacter read_character_artifact(const std::filesystem::path& folder, const std::string& expected,
                                          const AssetBank& stock) {
    if (!digest(expected))
        throw Error("Invalid character artifact hash.");
    ordinary(folder);
    ordinary(folder / "blobs");
    if (std::filesystem::is_symlink(folder / "character.json"))
        throw Error("Character manifest cannot be a link.");
    const auto bytes = read_file(folder / "character.json", MiB);
    if (sha256(bytes) != expected)
        throw Error("Character manifest integrity check failed.");
    const auto value = json::parse(bytes.begin(), bytes.end(), [](int depth, json::parse_event_t, json&) {
        if (depth > 8)
            throw Error("Character metadata is nested too deeply.");
        return true;
    });
    if (value.at("schema") != 1 || value.at("adapter") != CharacterAdapterVersion)
        throw Error("Prepare this character for the selected Game Pak and adapter version first.");
    PreparedCharacter c;
    c.source_revision = text(value, "revision");
    c.base_fingerprint = text(value, "base");
    if (c.base_fingerprint != revision_fingerprint(c.source_revision))
        throw Error("Character source provenance is invalid.");
    c.root.content_id = text(value, "id");
    c.root.name = text(value, "name");
    c.root.base_character = value.at("behaviour").get<unsigned>();
    c.root.portrait = value.at("portrait").get<unsigned>();
    if (value.contains("behaviour_contract"))
        c.root.behaviour = decode_behaviour(value.at("behaviour_contract").dump());
    if (!value.at("headers").is_array() || value.at("headers").size() != 3)
        throw Error("Invalid vehicle roots.");
    c.root.headers = value.at("headers").get<std::array<unsigned, 3>>();
    c.stage_header = value.at("stage_header").get<unsigned>();
    const auto& audio = value.at("audio");
    auto load_audio = [&](const char* name, const char* size_key, std::size_t limit) {
        const auto hash = text(audio, name);
        const auto size = audio.at(size_key).get<std::size_t>();
        if (!digest(hash) || !size || size > limit)
            throw Error("Invalid selection audio blob descriptor.");
        const auto path = folder / "blobs" / hash;
        if (std::filesystem::is_symlink(path))
            throw Error("Audio blob cannot be a link.");
        auto blob = read_file(path, size);
        if (blob.size() != size || sha256(blob) != hash)
            throw Error("Selection audio blob failed integrity validation.");
        return blob;
    };
    c.audio.control = load_audio("control", "control_size", 256 * 1024);
    c.audio.samples = load_audio("samples", "sample_size", 4 * MiB);
    if (!audio.at("cues").is_array() || audio.at("cues").size() != 3)
        throw Error("Missing character selection audio actions.");
    for (unsigned i = 0; i < 3; ++i) {
        const auto row = audio.at("cues").at(i).get<std::array<unsigned, 4>>();
        c.audio.cues[i] = {row[0], row[1], row[2], row[3]};
    }
    const auto& records = value.at("records");
    if (!records.is_array() || records.empty() || records.size() > 1024)
        throw Error("Invalid character dependency count.");
    std::size_t total = 0;
    for (const auto& record : records) {
        const std::pair<unsigned, unsigned> key{record.at("section").get<unsigned>(),
                                                record.at("record").get<unsigned>()};
        const auto hash = text(record, "hash");
        const auto size = record.at("size").get<std::size_t>();
        if (!digest(hash) || !size || size > 16 * MiB - total || c.records.contains(key))
            throw Error("Invalid character dependency descriptor.");
        const auto path = folder / "blobs" / hash;
        if (std::filesystem::is_symlink(path))
            throw Error("Character dependencies cannot be links.");
        auto data = read_file(path, size);
        if (data.size() != size || sha256(data) != hash)
            throw Error("A character dependency failed its integrity check.");
        total += size;
        c.records.emplace(key, std::move(data));
    }
    const auto& animations = value.at("animations");
    if (!animations.is_array() || animations.size() > 128)
        throw Error("Invalid character model animation count.");
    for (const auto& row : animations) {
        const auto model = row.at("model").get<unsigned>();
        const auto& ids = row.at("ids");
        if (!ids.is_array() || ids.size() > 128 || c.model_animations.contains(model))
            throw Error("Invalid model animation range.");
        c.model_animations.emplace(model, ids.get<std::vector<unsigned>>());
    }
    if (validate_prepared_character(c, stock) != c.root.content_id)
        throw Error("Character content identity does not match its validated resources.");
    return c;
}
} // namespace dkr::mods
