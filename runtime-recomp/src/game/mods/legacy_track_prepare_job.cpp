#include "legacy_track_prepare_job.hpp"
#include "legacy_track_artifact.hpp"
#include "legacy_character_artifact.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <map>
#include <set>

namespace dkr::mods {
namespace {
bool digest(std::string_view value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
void directory(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_directory(path))
        throw Error("Track preparation requires ordinary private directories.");
}
Bytes reconstruct(View source,View patch) {
    for(unsigned order=0;order<3;++order) {
        Bytes bytes(source.begin(),source.end());
        if(order==1)for(std::size_t i=0;i<bytes.size();i+=2)std::swap(bytes[i],bytes[i+1]);
        if(order==2)for(std::size_t i=0;i<bytes.size();i+=4) {
            std::swap(bytes[i],bytes[i+3]);std::swap(bytes[i+1],bytes[i+2]);
        }
        try{return decode_patch(bytes,patch);}catch(const Error&){}
    }
    throw Error("This patch no longer matches the verified source Game Pak.");
}
}
static void prepare_imported_content(const std::filesystem::path& review,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& destination,const ProgressCallback& progress,bool characters,const ImportPreparation* preparation) {
    using nlohmann::json;
    if(owned_roms.empty() || owned_roms.size()>8)throw Error("Track preparation needs its imported source Game Pak.");
    directory(review);directory(review/"patches");directory(destination.parent_path());
    const auto name=review.filename().string();
    if(!digest(name) || std::filesystem::is_symlink(review/"review.json"))throw Error("Invalid saved import review identity.");
    const auto receipt=read_file(review/"review.json",8*MiB);
    if(sha256(receipt)!=name)throw Error("The saved import review has changed.");
    const auto parsed=json::parse(receipt.begin(),receipt.end(),[](int depth,json::parse_event_t,json&) {
        if(depth>24)throw Error("Import review nesting exceeds its limit.");return true;
    });
    if(parsed.at("schema")!=Schema || parsed.at("state")!="awaiting-review" || parsed.at("runtime_activated")!=false)
        throw Error("Unsupported saved review state.");
    const auto& packages=parsed.at("packages");
    if(!packages.is_array() || packages.empty() || packages.size()>32)throw Error("Invalid saved package count.");
    auto local=preparation?ImportPreparation{}:load_import_sources(owned_roms);
    const auto& bases=preparation?preparation->bases:local.bases;
    if(!std::filesystem::create_directory(destination))throw Error("Refusing to overwrite prepared content.");
    bool complete=false;
    struct Cleanup {
        std::filesystem::path path;bool& complete;
        ~Cleanup(){if(!complete){std::error_code error;std::filesystem::remove_all(path,error);}}
    } cleanup{destination,complete};
    json entries=json::array(),blocked=json::array();std::set<std::string> seen;
    unsigned index=0;std::size_t total=0;
    for(const auto& package:packages) {
        if(progress && !progress({++index,static_cast<unsigned>(packages.size()),characters?"Preparing additive character assets":"Preparing scoped course assets"}))
            throw Error("Track preparation cancelled. No active content changed.");
        const auto revision=package.at("source_revision").get<std::string>();
        const auto patch_id=package.at("patch_sha256").get<std::string>();
        if(!digest(patch_id))throw Error("Invalid saved patch identity.");
        const auto patch_path=review/"patches"/(patch_id+".xdelta");
        if(std::filesystem::is_symlink(patch_path))throw Error("Saved patches must not be links.");
        const auto patch=read_file(patch_path,MaxPatch);
        if(sha256(patch)!=patch_id)throw Error("The saved patch failed its integrity check.");
        if(!bases.contains(revision))throw Error("Import the exact source revision before preparing this content.");
        const auto& base=bases.at(revision);
        const bool shared=preparation && preparation->analysis.patch_digest==patch_id &&
            preparation->analysis.source_revision==revision && !preparation->target.empty();
        const auto reconstructed=shared?Bytes{}:reconstruct(base,patch);
        const View target=shared?View(preparation->target):View(reconstructed);
        const auto analysis=shared?preparation->analysis:analyze(base,target,patch_id);
        std::string details;
        for(const auto& note:analysis.notes){if(!details.empty())details+=' ';details+=note;}
        if(details.empty())details="Asset-only conversion; native game behaviour is retained.";
        if(details.size()>4096)throw Error("Compatibility notes exceed their limit.");
        if(analysis.asset_digest!=package.at("asset_sha256").get<std::string>())throw Error("Reconstructed content no longer matches the saved review.");
        if(!characters)for(const auto& root:analysis.tracks)for(const auto& [runtime_revision,runtime_base]:bases) {
            const auto variant=root.content_id+":"+runtime_revision;
            const auto storage=runtime_revision==revision?root.content_id:
                sha256(View(reinterpret_cast<const std::uint8_t*>(variant.data()),variant.size()));
            if(!seen.insert(variant).second)continue;
            if(seen.size()>512)throw Error("Prepared course count exceeds its limit.");
            try {
                const auto track=prepare_track_bank(base,target,patch_id,root.carrier,runtime_base);
                std::size_t bytes=MiB; // reserve the maximum manifest size as well
                for(const auto& key:track.included)bytes+=track.bank->record(key.first,key.second).size();
                if(bytes>MaxStaged-total)throw Error("Prepared course transaction exceeds 256 MiB.");
                const auto folder=destination/storage;
                const auto hash=write_track_artifact(folder,track,patch_id,analysis.profile);
                // Validate the persisted bytes before publishing the receipt.
                const auto loaded=read_track_artifact(folder,hash,AssetBank::stock(runtime_base));
                if(loaded.root.content_id!=root.content_id)throw Error("Prepared course identity changed during persistence.");
                total+=bytes;
                entries.push_back({{"id",root.content_id},{"name",root.name},{"revision",runtime_revision},{"storage",storage},
                    {"patch",patch_id},{"artifact",hash},{"bank",track.bank->fingerprint()},
                    {"details",details},
                    {"carrier",root.carrier},{"vehicles",root.vehicles},{"race_type",root.race_type}});
            } catch(const Error& error) {
                // A compatibility rejection is not an enabled partial track.
                // Remove only the generated content-id child of this new job.
                const auto child=destination/storage;
                if(digest(storage) && std::filesystem::exists(child)) {
                    directory(child);std::filesystem::remove_all(child);
                }
                blocked.push_back({{"id",root.content_id},{"name",root.name},{"revision",runtime_revision},{"reason",error.what()}});
            }
        }
        if(characters)for(const auto& root:analysis.character_roots) {
          std::string artifact_id;
          try {
            // Complete preparation and validation precede publication. A mixed
            // patch only contributes its explicitly owned character resources.
            auto prepared=prepare_character(base,target,patch_id,root.base_character);
            if(prepared.root.name.starts_with("Custom Racer ") && package.contains("display_name")) {
                const auto label=package.at("display_name").get<std::string>();
                if(!label.empty() && label.size()<=120 && std::none_of(label.begin(),label.end(),[](unsigned char c){return c<32;}))
                    prepared.root.name=label+(analysis.character_roots.size()>1?" - "+prepared.root.name:"");
            }
            const auto id=prepared.root.content_id;
            if(!seen.insert(id).second)continue;
            if(seen.size()>512)throw Error("Prepared character count exceeds its limit.");
            std::size_t bytes=MiB;for(const auto& [key,data]:prepared.records)bytes+=data.size();
            if(bytes>MaxStaged-total)throw Error("Prepared character transaction exceeds its byte budget.");
            const auto stock=AssetBank::stock(base);
            artifact_id=id;
            const auto hash=write_character_artifact(destination/id,prepared,*stock);
            const auto loaded=read_character_artifact(destination/id,hash,*stock);
            if(loaded.root.content_id!=id)throw Error("Character identity changed during persistence.");
            entries.push_back({{"id",id},{"name",prepared.root.name},{"revision",revision},{"patch",patch_id},
                {"artifact",hash},{"behaviour",prepared.root.base_character},{"vehicles",7},{"details",details}});
            total+=bytes;
          } catch(const Error& error) {
            if(digest(artifact_id) && std::filesystem::exists(destination/artifact_id)) {
                directory(destination/artifact_id);std::filesystem::remove_all(destination/artifact_id);
            }
            blocked.push_back({{"name",root.name},{"reason",error.what()}});
          }
        }
        if(!characters)for(const auto& character:analysis.characters)
            blocked.push_back({{"name",character},{"reason","Character content requires the additive racer adapter."}});
    }
    const auto text=json({{"schema",1},{"adapter",characters?CharacterAdapterVersion:TrackAdapterVersion},{"review",name},
        {characters?"characters":"tracks",entries},{"blocked",blocked},{"enabled",false}}).dump();
    if(text.size()>MiB)throw Error("Prepared catalogue receipt exceeds its budget.");
    write_new_file(destination/"prepared.json",View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
    complete=true;
}
void prepare_imported_tracks(const std::filesystem::path& review,const std::vector<std::filesystem::path>& roms,
    const std::filesystem::path& destination,const ProgressCallback& progress,const ImportPreparation* preparation) {
    prepare_imported_content(review,roms,destination,progress,false,preparation);
}
void prepare_imported_characters(const std::filesystem::path& review,const std::vector<std::filesystem::path>& roms,
    const std::filesystem::path& destination,const ProgressCallback& progress,const ImportPreparation* preparation) {
    prepare_imported_content(review,roms,destination,progress,true,preparation);
}
}
