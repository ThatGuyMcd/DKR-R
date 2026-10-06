#include "legacy_mod_stage.hpp"
#include "legacy_mod_dependencies.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <map>
#include <set>

namespace dkr::mods {
namespace {
void report(const ProgressCallback& callback,unsigned patch,unsigned count,const char* stage) {
    if(callback && !callback({patch,count,stage})) throw Error("Import cancelled. No library or save changes were made.");
}
Bytes representation(View canonical,unsigned byte_order) {
    Bytes out(canonical.begin(),canonical.end());
    if(byte_order==1) for(std::size_t i=0;i<out.size();i+=2) std::swap(out[i],out[i+1]);
    if(byte_order==2) for(std::size_t i=0;i<out.size();i+=4) {
        std::swap(out[i],out[i+3]);std::swap(out[i+1],out[i+2]);
    }
    return out;
}
}
void stage_import(const std::filesystem::path& source,
    const std::vector<std::filesystem::path>& owned_roms,
    const std::filesystem::path& destination,const ProgressCallback& progress,
    const std::map<std::string,std::string>& source_labels) {
    if(owned_roms.empty() || owned_roms.size()>8)
        throw Error("Select an imported original Game Pak before importing patches.");
    if(destination.empty() || destination.filename().empty()) throw Error("A generated staging directory is required.");
    const auto parent=destination.parent_path();
    if(!std::filesystem::is_directory(parent) || std::filesystem::is_symlink(parent))
        throw Error("The private staging parent must already exist and must not be a link.");
    report(progress,0,0,"Matching original Game Paks");
    std::map<std::string,Bytes> bases;
    for(const auto& path : owned_roms) {
        auto base=read_file(path,MaxImage);
        canonicalize_rom(base);
        const auto revision=verified_revision(base);
        bases.try_emplace(revision,std::move(base));
    }
    report(progress,0,0,"Reading and checking archive");
    const auto patches=read_patch_inputs(source);
    // create_directory (not create_directories) refuses existing transactions.
    if(!std::filesystem::create_directory(destination))
        throw Error("Refusing to reuse or overwrite an existing staging transaction.");
    bool complete=false;
    struct Cleanup {
        std::filesystem::path path; bool& complete;
        ~Cleanup(){if(!complete){std::error_code e;std::filesystem::remove_all(path,e);}}
    } cleanup{destination,complete};
    std::filesystem::create_directory(destination/"patches");
    std::filesystem::create_directory(destination/"blobs");
    std::set<std::string> blobs;
    std::size_t staged_bytes=0;
    auto store=[&](const std::filesystem::path& path,View bytes) {
        if(bytes.size()>MaxStaged-staged_bytes) throw Error("Converted package exceeds the 256 MiB transaction limit.");
        write_new_file(path,bytes);staged_bytes+=bytes.size();
    };
    using nlohmann::json;
    json packages=json::array(), failures=json::array();
    std::map<std::string,std::size_t> editions;
    unsigned index=0;
    for(const auto& patch : patches) {
        ++index;
        report(progress,index,static_cast<unsigned>(patches.size()),"Decoding patch against verified Game Paks");
        Bytes target;
        const Bytes* matched=nullptr;
        for(const auto& [revision,base] : bases) {
            for(unsigned order=0;order<3;++order) {
                report(progress,index,static_cast<unsigned>(patches.size()),"Verifying patch source and checksum");
                try {target=decode_patch(representation(base,order),patch.data);matched=&base;break;}
                catch(const Error&) {target.clear();}
            }
            if(matched) break;
        }
        const auto patch_id=sha256(patch.data);
        if(!matched) {
            failures.push_back({{"patch_sha256",patch_id},{"reason",
                "No imported original Game Pak satisfied the patch checksum. Import its required revision; chained patches need an explicit recipe."}});
            continue;
        }
        report(progress,index,static_cast<unsigned>(patches.size()),"Finding tracks and checking dependencies");
        const auto analysis=analyze(*matched,target,patch_id);
        if(auto duplicate=editions.find(analysis.asset_digest);duplicate!=editions.end()) {
            packages[duplicate->second]["editions"].push_back(patch_id);
            store(destination/"patches"/(patch_id+".xdelta"),patch.data);
            continue;
        }
        auto package=json::parse(analysis_json(analysis));
        auto label=utf8_path(patch.name).stem().u8string();
        if(label.size()>120)label.resize(120);
        package["display_name"]=std::string(label.begin(),label.end());
        if(const auto found=source_labels.find(patch_id);found!=source_labels.end()) {
            const auto& text=found->second;
            if(text.empty() || text.size()>120 || std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32 || c==127;}))
                throw Error("Invalid retained patch display label.");
            package["display_name"]=text;
        }
        package["editions"]=json::array({patch_id});
        package["status"]="review-required";
        package["enabled"]=false;
        package["online_certified"]=false;
        package["records"]=json::array();
        // Discovery evidence is not a runtime certification. Unknown format
        // changes remain review-only; executable bytes are NEVER stored as assets.
        const AssetImage original(*matched,analysis.source_revision), image(target,analysis.source_revision);
        for(const auto& [section,id] : analysis.changed_records) {
            if(section==25) continue; // Rewritten retail name bank is collateral.
            const auto record=image.record(section,id);
            const auto digest=sha256(record);
            if(blobs.insert(digest).second) store(destination/"blobs"/digest,record);
            package["records"].push_back({{"section",section},{"record",id},
                {"sha256",digest},{"size",record.size()}});
        }
        // A root's unchanged header/map can still be needed by a changed mesh.
        unsigned root_index=0;
        for(auto& root : package["tracks"]) {
            const auto header=image.record(23,root["carrier"].get<unsigned>());
            const auto digest=sha256(header);
            if(blobs.insert(digest).second) store(destination/"blobs"/digest,header);
            root["header_sha256"]=digest;
            const auto dependencies=inspect_track_dependencies(original,image,analysis.tracks.at(root_index++));
            root["static_dependencies"]={{"complete",false},{"runtime_certified",false},
                {"placed_objects",dependencies.placed_objects},{"behaviours",dependencies.behaviours},
                {"blockers",dependencies.blockers},{"records",json::array()}};
            for(const auto& dependency : dependencies.records) {
                root["static_dependencies"]["records"].push_back({{"section",dependency.section},
                    {"record",dependency.id},{"sha256",dependency.sha256},{"changed",dependency.changed}});
            }
        }
        store(destination/"patches"/(patch_id+".xdelta"),patch.data);
        editions.emplace(analysis.asset_digest,packages.size());
        packages.push_back(std::move(package));
    }
    report(progress,index,static_cast<unsigned>(patches.size()),"Preparing import review");
    if(packages.empty()) throw Error("None of these patches could be decoded with the imported original Game Paks. No content was installed.");
    const json receipt={{"schema",Schema},{"state","awaiting-review"},{"packages",packages},
        {"failures",failures},{"stored_bytes",staged_bytes},{"runtime_activated",false}};
    const auto encoded=receipt.dump(2);
    if(encoded.size()>8*MiB) throw Error("Import review exceeds its metadata budget.");
    store(destination/"review.json",View(reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()));
    complete=true;
}
} // namespace dkr::mods
