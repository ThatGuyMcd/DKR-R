#include "vehicle_protocol.hpp"
#include "online_mod_manifest.hpp"
#include "legacy_character_artifact.hpp"
#include "legacy_track_artifact.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace dkr::mods::online {
namespace {
using nlohmann::json;
const char* kind(Kind value) {
    if (value == CarContent)
        return "car";
    switch (value) {
    case Kind::Track:
        return "track";
    case Kind::Character:
        return "character";
    case Kind::TrackLab:
        return "tracklab";
    }
    throw Error("Invalid online mod kind.");
}
const char* payload_kind(PayloadKind value) {
    if (value == CarPayload)
        return "car";
    switch (value) {
    case PayloadKind::Xdelta:
        return "xdelta";
    case PayloadKind::TrackLab:
        return "tracklab";
    case PayloadKind::Code:
        return "code";
    }
    throw Error("Invalid online mod payload kind.");
}
void keys(const json& object, std::initializer_list<const char*> names) {
    if (!object.is_object() || object.size() != names.size())
        throw Error("Unsupported online mod manifest fields.");
    for (const auto* name : names)
        if (!object.contains(name))
            throw Error("Incomplete online mod manifest.");
}
unsigned number(const json& value) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > 0xffffffffULL)
        throw Error("Invalid online mod version.");
    return value.get<unsigned>();
}
bool label(std::string_view value) {
    return !value.empty() && value.size() <= 256 &&
           std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool code_id(std::string_view value) {
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
void canonical_object(const std::string& text, std::size_t limit) {
    if (text.size() > limit)
        throw Error("Online code configuration exceeds its bound.");
    std::vector<std::set<std::string>> keys;
    const auto value = json::parse(text, [&](int depth, json::parse_event_t event, json& token) {
        if (depth > 20)
            throw Error("Online code configuration is nested too deeply.");
        if (event == json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == json::parse_event_t::key && (keys.empty() || !keys.back().insert(token.get<std::string>()).second))
            throw Error("Duplicate online code configuration field.");
        if (event == json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    if (!value.is_object() || value.dump() != text)
        throw Error("Online code configuration is not canonical.");
}
} // namespace
bool valid_digest(std::string_view value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
                                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
Manifest canonical(Manifest value) {
    if (value.schema != ManifestSchema || (value.revision != "us.v77" && value.revision != "us.v80") ||
        value.payloads.size() > MaxPackages || value.content.size() > MaxTracks + MaxCharacters ||
        value.code.size() > 128)
        throw Error("Unsupported online mod manifest or content budget.");
    std::sort(value.payloads.begin(), value.payloads.end(),
              [](const auto& a, const auto& b) { return a.digest < b.digest; });
    std::sort(value.content.begin(), value.content.end(),
              [](const auto& a, const auto& b) { return std::tie(a.kind, a.id) < std::tie(b.kind, b.id); });
    std::map<std::string, PayloadKind> payloads;
    std::uint64_t bytes = 0;
    for (const auto& p : value.payloads) {
        const auto limit = p.kind == PayloadKind::Xdelta                           ? MaxPatch
                           : (p.kind == PayloadKind::Code || p.kind == CarPayload) ? 32 * MiB
                                                                                   : MaxStaged;
        payload_kind(p.kind);
        if (!valid_digest(p.digest) || !p.size || p.size > limit || p.size > MaxTransferBytes - bytes ||
            (!p.source_label.empty() && (!label(p.source_label) || p.source_label.size() > 120)) ||
            !payloads.emplace(p.digest, p.kind).second ||
            (p.kind == PayloadKind::Xdelta && p.source_revision != "us.v77" && p.source_revision != "us.v80") ||
            (p.kind != PayloadKind::Xdelta && !p.source_revision.empty()))
            throw Error("Invalid, duplicate or oversized online mod payload.");
        bytes += p.size;
    }
    std::set<std::string> ids, used;
    std::size_t tracks = 0, characters = 0, cars = 0;
    for (const auto& c : value.content) {
        kind(c.kind);
        if (!valid_digest(c.id) || !label(c.name) || !valid_digest(c.artifact) || !valid_digest(c.bank) ||
            !ids.insert(c.id).second || !payloads.contains(c.payload))
            throw Error("Invalid online mod content identity.");
        if (c.kind == CarContent) {
            const auto p = std::find_if(value.payloads.begin(), value.payloads.end(),
                                        [&](const auto& p) { return p.digest == c.payload; });
            if (c.adapter != CarAdapterVersion || c.artifact != c.payload || c.bank != c.payload || c.id != c.payload ||
                !c.behaviour.empty() || c.behaviour_abi || payloads.at(c.payload) != CarPayload ||
                p == value.payloads.end() || p->source_label.empty() ||
                p->source_label.find_first_of("/\\:") != std::string::npos || p->source_label.size() < 9 ||
                p->source_label.substr(p->source_label.size() - 8) != ".dkrkart")
                throw Error("This build cannot safely prepare the required custom car.");
            ++cars;
            used.insert(c.payload);
            continue;
        }
        const auto expected = c.kind == Kind::Character ? CharacterAdapterVersion
                              : c.kind == Kind::Track   ? TrackAdapterVersion
                                                        : 1U;
        if (c.adapter != expected ||
            payloads.at(c.payload) != (c.kind == Kind::TrackLab ? PayloadKind::TrackLab : PayloadKind::Xdelta))
            throw Error("This build cannot safely prepare the required online mod adapter.");
        if ((c.behaviour.empty() && c.behaviour_abi) ||
            (!c.behaviour.empty() &&
             (!valid_digest(c.behaviour) || c.behaviour_abi != BehaviourAbi || c.kind == Kind::TrackLab)))
            throw Error("This build cannot safely execute the lobby's legacy behaviour ABI.");
        used.insert(c.payload);
        if (c.kind == Kind::Character)
            ++characters;
        else
            ++tracks;
    }
    std::set<std::string> code_ids;
    for (const auto& code : value.code) {
        if (!code_id(code.id) || !label(code.name) || !label(code.version) || code.version.size() > 64 ||
            !code_ids.insert(code.id).second || !payloads.contains(code.payload) ||
            payloads.at(code.payload) != PayloadKind::Code)
            throw Error("Invalid or duplicate online code selection.");
        canonical_object(code.settings, 512 * 1024);
        canonical_object(code.data, 65536);
        used.insert(code.payload);
    }
    if (tracks > MaxTracks || characters > MaxCharacters || cars > 16 || used.size() != payloads.size())
        throw Error("Online mod selection exceeds the supported budget or includes unused payloads.");
    return value;
}
Bytes encode(const Manifest& input) {
    const auto value = canonical(input);
    json doc = {{"schema", value.schema},    {"revision", value.revision}, {"custom_ai", value.custom_ai},
                {"payloads", json::array()}, {"content", json::array()},   {"code", json::array()}};
    for (const auto& p : value.payloads)
        doc["payloads"].push_back({{"kind", payload_kind(p.kind)},
                                   {"sha256", p.digest},
                                   {"source_revision", p.source_revision},
                                   {"size", p.size},
                                   {"source_label", p.source_label}});
    for (const auto& c : value.content)
        doc["content"].push_back({{"kind", kind(c.kind)},
                                  {"id", c.id},
                                  {"name", c.name},
                                  {"payload", c.payload},
                                  {"artifact", c.artifact},
                                  {"bank", c.bank},
                                  {"adapter", c.adapter},
                                  {"behaviour", c.behaviour},
                                  {"behaviour_abi", c.behaviour_abi}});
    for (const auto& c : value.code)
        doc["code"].push_back({{"id", c.id},
                               {"name", c.name},
                               {"version", c.version},
                               {"payload", c.payload},
                               {"settings", c.settings},
                               {"data", c.data}});
    const auto text = doc.dump();
    if (text.size() > MaxManifestBytes)
        throw Error("Online mod manifest exceeds its byte limit.");
    return Bytes(text.begin(), text.end());
}
Manifest decode(View bytes) {
    if (bytes.empty() || bytes.size() > MaxManifestBytes)
        throw Error("Invalid online mod manifest size.");
    // Duplicate JSON keys must not be silently collapsed by the parser.
    std::vector<std::set<std::string>> object_keys;
    const auto doc = json::parse(bytes.begin(), bytes.end(), [&](int depth, json::parse_event_t event, json& value) {
        if (depth > 5)
            throw Error("Online mod manifest nesting exceeds its limit.");
        if (event == json::parse_event_t::object_start)
            object_keys.emplace_back();
        if (event == json::parse_event_t::key &&
            (object_keys.empty() || !object_keys.back().insert(value.get<std::string>()).second))
            throw Error("Duplicate online mod manifest field.");
        if (event == json::parse_event_t::object_end)
            object_keys.pop_back();
        return true;
    });
    keys(doc, {"schema", "revision", "custom_ai", "payloads", "content", "code"});
    Manifest out;
    out.schema = number(doc.at("schema"));
    out.revision = doc.at("revision").get<std::string>();
    out.custom_ai = doc.at("custom_ai").get<bool>();
    const auto& payloads = doc.at("payloads");
    const auto& contents = doc.at("content");
    if (!payloads.is_array() || !contents.is_array() || payloads.size() > MaxPackages ||
        contents.size() > MaxTracks + MaxCharacters || !doc.at("code").is_array() || doc.at("code").size() > 128)
        throw Error("Online mod manifest exceeds its entry limit.");
    for (const auto& p : payloads) {
        keys(p, {"kind", "sha256", "source_revision", "size", "source_label"});
        const auto tag = p.at("kind").get<std::string>();
        if (tag != "xdelta" && tag != "tracklab" && tag != "code" && tag != "car")
            throw Error("Unknown online mod payload.");
        if (!p.at("size").is_number_unsigned())
            throw Error("Invalid online mod payload size.");
        out.payloads.push_back({tag == "car"      ? CarPayload
                                : tag == "xdelta" ? PayloadKind::Xdelta
                                : tag == "code"   ? PayloadKind::Code
                                                  : PayloadKind::TrackLab,
                                p.at("sha256").get<std::string>(), p.at("source_revision").get<std::string>(),
                                p.at("size").get<std::uint64_t>(), p.at("source_label").get<std::string>()});
    }
    for (const auto& c : contents) {
        keys(c, {"kind", "id", "name", "payload", "artifact", "bank", "adapter", "behaviour", "behaviour_abi"});
        const auto tag = c.at("kind").get<std::string>();
        if (tag != "track" && tag != "character" && tag != "tracklab" && tag != "car")
            throw Error("Unknown online mod content.");
        out.content.push_back({tag == "car"         ? CarContent
                               : tag == "track"     ? Kind::Track
                               : tag == "character" ? Kind::Character
                                                    : Kind::TrackLab,
                               c.at("id").get<std::string>(), c.at("name").get<std::string>(),
                               c.at("payload").get<std::string>(), c.at("artifact").get<std::string>(),
                               c.at("bank").get<std::string>(), number(c.at("adapter")),
                               c.at("behaviour").get<std::string>(), number(c.at("behaviour_abi"))});
    }
    for (const auto& c : doc.at("code")) {
        keys(c, {"id", "name", "version", "payload", "settings", "data"});
        out.code.push_back({c.at("id").get<std::string>(), c.at("name").get<std::string>(),
                            c.at("version").get<std::string>(), c.at("payload").get<std::string>(),
                            c.at("settings").get<std::string>(), c.at("data").get<std::string>()});
    }
    return canonical(std::move(out));
}
std::string identity(const Manifest& value) {
    return sha256(encode(value));
}
std::uint64_t transfer_size(const Manifest& value) {
    const auto checked = canonical(value);
    std::uint64_t result = 0;
    for (const auto& p : checked.payloads)
        result += p.size;
    return result;
}
} // namespace dkr::mods::online
