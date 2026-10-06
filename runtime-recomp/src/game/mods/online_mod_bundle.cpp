#include "online_mod_bundle.hpp"
#include "legacy_track_catalog.hpp"
#include <json/json.hpp>

namespace dkr::mods::online {
namespace {
using nlohmann::json;
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("Online mod preparation cancelled.");}
json document(View bytes) {
    return json::parse(bytes.begin(),bytes.end(),[](int depth,json::parse_event_t,json&){if(depth>12)throw Error("Mod metadata nesting exceeds its limit.");return true;});
}
}
Bundle export_legacy(const std::filesystem::path& input_path,std::string revision,std::stop_token stop) {
    Bundle out;out.manifest.revision=std::move(revision);
    std::uint64_t total=0;
    if(input_path.empty())return out; // Vanilla callers need no library.
    const auto path=private_storage_path(input_path);
    if(!std::filesystem::exists(path))return out;
    check_storage(path,true);
    std::map<std::filesystem::path,json> reviews;
    for(const auto kind:{TrackCatalog::Kind::Track,TrackCatalog::Kind::Character}) {
        for(const auto& item:TrackCatalog::selected_items(path,out.manifest.revision,kind)) {
            cancelled(stop);
            if(!valid_digest(item.review))throw Error("This enabled mod has no retained source patch. Reimport it before hosting a modded lobby.");
            const auto review=path/"reviews"/item.review;check_storage(review,true);check_storage(review/"review.json",false);
            auto cached=reviews.find(review);
            if(cached==reviews.end()) {
            const auto bytes=read_file(review/"review.json",8*MiB);
            if(sha256(bytes)!=item.review)throw Error("Retained mod review verification failed.");
            auto doc=document(bytes);
            if(doc.at("schema")!=Schema || !doc.at("packages").is_array() || doc.at("packages").size()>32)throw Error("Invalid retained mod review.");
            cached=reviews.emplace(review,std::move(doc)).first;
            }
            const auto& doc=cached->second;const json* package=nullptr;
            for(const auto& p:doc.at("packages"))if(p.at("patch_sha256")==item.patch) {
                if(package)throw Error("Ambiguous online mod source patch.");package=&p;
            }
            if(!package)throw Error("Retained mod review does not contain its source patch.");
            if(!out.payloads.contains(item.patch)) {
                const auto file=review/"patches"/(item.patch+".xdelta");check_storage(file,false);
                const auto size=std::filesystem::file_size(file);
                if(!size || size>MaxPatch || size>MaxTransferBytes-total || out.payloads.size()>=MaxPackages)
                    throw Error("The selected online mods exceed their transfer budget.");
                auto data=std::make_shared<const Bytes>(read_file(file,MaxPatch));
                if(data->empty() || sha256(*data)!=item.patch)throw Error("Retained online mod patch verification failed.");
                if(data->size()!=size)throw Error("The retained online patch changed during export.");
                total+=size;
                out.manifest.payloads.push_back({PayloadKind::Xdelta,item.patch,package->at("source_revision").get<std::string>(),data->size(),package->value("display_name",std::string{})});
                out.payloads.emplace(item.patch,std::move(data));
            }
            const bool character=kind==TrackCatalog::Kind::Character;
            out.manifest.content.push_back({character?Kind::Character:Kind::Track,item.id,item.name,item.patch,item.artifact,item.bank,
                character?CharacterAdapterVersion:TrackAdapterVersion});
        }
    }
    out.manifest=canonical(std::move(out.manifest));return out;
}
}
