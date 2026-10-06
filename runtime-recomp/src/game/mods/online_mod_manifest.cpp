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
    switch(value) {case Kind::Track:return "track";case Kind::Character:return "character";case Kind::TrackLab:return "tracklab";}
    throw Error("Invalid online mod kind.");
}
const char* payload_kind(PayloadKind value) {
    switch(value) {case PayloadKind::Xdelta:return "xdelta";case PayloadKind::TrackLab:return "tracklab";}
    throw Error("Invalid online mod payload kind.");
}
void keys(const json& object,std::initializer_list<const char*> names) {
    if(!object.is_object() || object.size()!=names.size())throw Error("Unsupported online mod manifest fields.");
    for(const auto* name:names)if(!object.contains(name))throw Error("Incomplete online mod manifest.");
}
unsigned number(const json& value) {
    if(!value.is_number_unsigned() || value.get<std::uint64_t>()>0xffffffffULL)throw Error("Invalid online mod version.");
    return value.get<unsigned>();
}
bool label(std::string_view value) {
    return !value.empty() && value.size()<=256 && std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c==127;});
}
}
bool valid_digest(std::string_view value) noexcept {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
Manifest canonical(Manifest value) {
    if(value.schema!=ManifestSchema || (value.revision!="us.v77" && value.revision!="us.v80") ||
       value.payloads.size()>MaxPackages || value.content.size()>MaxTracks+MaxCharacters)
        throw Error("Unsupported online mod manifest or content budget.");
    std::sort(value.payloads.begin(),value.payloads.end(),[](const auto& a,const auto& b){return a.digest<b.digest;});
    std::sort(value.content.begin(),value.content.end(),[](const auto& a,const auto& b){return std::tie(a.kind,a.id)<std::tie(b.kind,b.id);});
    std::map<std::string,PayloadKind> payloads;std::uint64_t bytes=0;
    for(const auto& p:value.payloads) {
        const auto limit=p.kind==PayloadKind::Xdelta?MaxPatch:MaxStaged;
        payload_kind(p.kind);
        if(!valid_digest(p.digest) || !p.size || p.size>limit || p.size>MaxTransferBytes-bytes ||
           (!p.source_label.empty() && (!label(p.source_label) || p.source_label.size()>120)) ||
           !payloads.emplace(p.digest,p.kind).second ||
           (p.kind==PayloadKind::Xdelta && p.source_revision!="us.v77" && p.source_revision!="us.v80") ||
           (p.kind==PayloadKind::TrackLab && !p.source_revision.empty()))
            throw Error("Invalid, duplicate or oversized online mod payload.");
        bytes+=p.size;
    }
    std::set<std::string> ids,used;std::size_t tracks=0,characters=0;
    for(const auto& c:value.content) {
        kind(c.kind);
        if(!valid_digest(c.id) || !label(c.name) || !valid_digest(c.artifact) || !valid_digest(c.bank) ||
           !ids.insert(c.id).second || !payloads.contains(c.payload))throw Error("Invalid online mod content identity.");
        const auto expected=c.kind==Kind::Character?CharacterAdapterVersion:c.kind==Kind::Track?TrackAdapterVersion:1U;
        if(c.adapter!=expected || payloads.at(c.payload)!=(c.kind==Kind::TrackLab?PayloadKind::TrackLab:PayloadKind::Xdelta))
            throw Error("This build cannot safely prepare the required online mod adapter.");
        used.insert(c.payload);
        if(c.kind==Kind::Character)++characters;else ++tracks;
    }
    if(tracks>MaxTracks || characters>MaxCharacters || used.size()!=payloads.size())
        throw Error("Online mod selection exceeds the supported budget or includes unused payloads.");
    return value;
}
Bytes encode(const Manifest& input) {
    const auto value=canonical(input);
    json doc={{"schema",value.schema},{"revision",value.revision},{"custom_ai",value.custom_ai},
              {"payloads",json::array()},{"content",json::array()}};
    for(const auto& p:value.payloads)doc["payloads"].push_back({{"kind",payload_kind(p.kind)},{"sha256",p.digest},{"source_revision",p.source_revision},{"size",p.size},{"source_label",p.source_label}});
    for(const auto& c:value.content)doc["content"].push_back({{"kind",kind(c.kind)},{"id",c.id},{"name",c.name},{"payload",c.payload},{"artifact",c.artifact},{"bank",c.bank},{"adapter",c.adapter}});
    const auto text=doc.dump();if(text.size()>MaxManifestBytes)throw Error("Online mod manifest exceeds its byte limit.");
    return Bytes(text.begin(),text.end());
}
Manifest decode(View bytes) {
    if(bytes.empty() || bytes.size()>MaxManifestBytes)throw Error("Invalid online mod manifest size.");
    // Duplicate JSON keys must not be silently collapsed by the parser.
    std::vector<std::set<std::string>> object_keys;
    const auto doc=json::parse(bytes.begin(),bytes.end(),[&](int depth,json::parse_event_t event,json& value) {
        if(depth>5)throw Error("Online mod manifest nesting exceeds its limit.");
        if(event==json::parse_event_t::object_start)object_keys.emplace_back();
        if(event==json::parse_event_t::key && (object_keys.empty() || !object_keys.back().insert(value.get<std::string>()).second))
            throw Error("Duplicate online mod manifest field.");
        if(event==json::parse_event_t::object_end)object_keys.pop_back();return true;
    });
    keys(doc,{"schema","revision","custom_ai","payloads","content"});
    Manifest out;out.schema=number(doc.at("schema"));out.revision=doc.at("revision").get<std::string>();out.custom_ai=doc.at("custom_ai").get<bool>();
    const auto& payloads=doc.at("payloads");const auto& contents=doc.at("content");
    if(!payloads.is_array() || !contents.is_array() || payloads.size()>MaxPackages || contents.size()>MaxTracks+MaxCharacters)
        throw Error("Online mod manifest exceeds its entry limit.");
    for(const auto& p:payloads) {
        keys(p,{"kind","sha256","source_revision","size","source_label"});
        const auto tag=p.at("kind").get<std::string>();
        if(tag!="xdelta" && tag!="tracklab")throw Error("Unknown online mod payload.");
        if(!p.at("size").is_number_unsigned())throw Error("Invalid online mod payload size.");
        out.payloads.push_back({tag=="xdelta"?PayloadKind::Xdelta:PayloadKind::TrackLab,p.at("sha256").get<std::string>(),p.at("source_revision").get<std::string>(),p.at("size").get<std::uint64_t>(),p.at("source_label").get<std::string>()});
    }
    for(const auto& c:contents) {
        keys(c,{"kind","id","name","payload","artifact","bank","adapter"});
        const auto tag=c.at("kind").get<std::string>();
        if(tag!="track" && tag!="character" && tag!="tracklab")throw Error("Unknown online mod content.");
        out.content.push_back({tag=="track"?Kind::Track:tag=="character"?Kind::Character:Kind::TrackLab,
            c.at("id").get<std::string>(),c.at("name").get<std::string>(),c.at("payload").get<std::string>(),
            c.at("artifact").get<std::string>(),c.at("bank").get<std::string>(),number(c.at("adapter"))});
    }
    return canonical(std::move(out));
}
std::string identity(const Manifest& value) {return sha256(encode(value));}
std::uint64_t transfer_size(const Manifest& value) {
    const auto checked=canonical(value);std::uint64_t result=0;for(const auto& p:checked.payloads)result+=p.size;return result;
}
}
