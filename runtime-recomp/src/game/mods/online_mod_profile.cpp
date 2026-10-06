#include "online_mod_profile.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_track_catalog.hpp"
#include "online_mod_cache.hpp"
#include "../custom_tracks.hpp"
#include <json/json.hpp>
#include <random>
#include <set>
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace dkr::mods::online {
namespace {
using nlohmann::json;
std::string path_text(const std::filesystem::path& path){const auto text=path.generic_u8string();return {text.begin(),text.end()};}
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("Online mod preparation cancelled. Offline mods and saves were not changed.");}
json read(const std::filesystem::path& path,std::size_t limit=MiB){check_storage(path,false);const auto bytes=read_file(path,limit);return json::parse(bytes.begin(),bytes.end(),[](int depth,json::parse_event_t,json&){if(depth>24)throw Error("Online metadata nesting exceeds its limit.");return true;});}
void write(const std::filesystem::path& path,const json& value){const auto data=value.dump();if(data.size()>MaxManifestBytes)throw Error("Online mod job metadata exceeds its budget.");write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(data.data()),data.size()));}
void child(const std::filesystem::path& path){check_storage(path.parent_path(),true);std::filesystem::create_directory(path);check_storage(path,true);}
std::string nonce(){std::random_device random;Bytes bytes(32);for(auto& b:bytes)b=static_cast<std::uint8_t>(random());return sha256(bytes);}
// Only the nonce-named job exclusively reserved by this transaction is ours.
// Never delete a profile/cache/source path in cleanup.
struct Job {
    std::filesystem::path jobs,path;
    ~Job(){try{if(path.empty() || !valid_digest(path_text(path.filename())) || path.parent_path()!=jobs)return;
        check_storage(jobs,true);check_storage(path,true);
        if(std::filesystem::weakly_canonical(path).parent_path()!=std::filesystem::weakly_canonical(jobs))return;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(path))check_storage(entry.path(),entry.is_directory());
        std::filesystem::remove_all(path);
    }catch(...){} }
};
}
Bundle export_host(const std::filesystem::path& library,const std::vector<std::filesystem::path>& labs,
    std::string revision,bool ai,std::stop_token stop) {
    if(labs.size()>MaxTracks)throw Error("Too many enabled Track Lab courses for an online session.");
    auto bundle=export_legacy(library,std::move(revision),stop);bundle.manifest.custom_ai=ai;
    std::uint64_t total=transfer_size(bundle.manifest);
    for(const auto& input_path:labs) {
        const auto path=private_storage_path(input_path);
        cancelled(stop);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(path,track,error))throw Error("Selected Track Lab course is invalid: "+error);
        auto bytes=std::make_shared<const Bytes>(pack_track_lab(path,stop));
        if(bytes->size()>MaxTransferBytes-total || bundle.payloads.size()>=MaxPackages)throw Error("Selected online mods exceed their transfer budget.");
        const auto payload=sha256(*bytes);const auto name=std::string("dkrmap:")+track.id;
        const auto id=sha256(View(reinterpret_cast<const std::uint8_t*>(name.data()),name.size()));
        const auto artifact=sha256(read_file(path/"manifest.json",MiB));
        bundle.manifest.payloads.push_back({PayloadKind::TrackLab,payload,{},bytes->size(),{}});
        bundle.manifest.content.push_back({Kind::TrackLab,id,track.name,payload,artifact,payload,1});
        if(!bundle.payloads.emplace(payload,std::move(bytes)).second)throw Error("An enabled Track Lab course is duplicated.");
        total+=bundle.payloads.at(payload)->size();
    }
    bundle.manifest=canonical(std::move(bundle.manifest));return bundle;
}
ProfileSources profile_sources(const Profile& input_profile) {
    const Profile profile{input_profile.manifest,input_profile.digest,private_storage_path(input_profile.directory)};
    const auto manifest=canonical(profile.manifest);
    if(profile.digest!=identity(manifest))throw Error("Online profile identity differs from its frozen manifest.");
    check_storage(profile.directory,true);
    const auto receipt=read(profile.directory/"verified.json");
    if(receipt.at("schema")!=ManifestSchema || receipt.at("manifest")!=profile.digest)throw Error("Online profile does not have a complete preparation receipt.");
    ProfileSources sources;
    const auto index=profile.directory/"sources.json";
    if(std::filesystem::exists(index)) {
        const auto refs=read(index,MaxManifestBytes);
        if(refs.at("schema")!=1 || refs.at("manifest")!=profile.digest ||
           !refs.at("legacy").is_array() || refs.at("legacy").size()>manifest.content.size() ||
           !refs.at("courses").is_array() || refs.at("courses").size()>MaxTracks)
            throw Error("Invalid local online asset-source index.");
        std::map<std::pair<std::filesystem::path,TrackCatalog::Kind>,std::vector<TrackCatalogItem>> catalogs;
        for(const auto& ref:refs.at("legacy")) {
            const auto kind=ref.at("kind").get<unsigned>();if(kind>1)throw Error("Invalid online source kind.");
            const auto text=ref.at("library").get<std::string>();if(text.size()>32768)throw Error("Online source path is too long.");
            const auto raw= utf8_path(text);if(!raw.is_absolute())throw Error("Online sources must be absolute local paths.");
            auto library=private_storage_path(raw);check_storage(library,true);
            const auto type=kind?TrackCatalog::Kind::Character:TrackCatalog::Kind::Track;
            const auto key=std::make_pair(library,type);
            auto at=catalogs.find(key);
            if(at==catalogs.end())at=catalogs.emplace(key,TrackCatalog::installed_items(library,manifest.revision,type)).first;
            const auto id=ref.at("id").get<std::string>(),group=ref.at("group").get<std::string>();
            const auto item=std::find_if(at->second.begin(),at->second.end(),[&](const auto& c){return c.id==id && c.group==group;});
            if(item==at->second.end())throw Error("A referenced online asset was removed or needs repair. Retry preparation.");
            (kind?sources.characters:sources.tracks).push_back({library,*item});
        }
        for(const auto& ref:refs.at("courses")) {
            const auto text=ref.get<std::string>();if(text.size()>32768)throw Error("Online course path is too long.");
            const auto raw= utf8_path(text);if(!raw.is_absolute())throw Error("Online courses must have absolute local paths.");
            auto path=private_storage_path(raw);
            sources.courses.push_back(std::move(path));
        }
    } else {
        // Earlier caches and isolated worker results remain readable. New
        // session profiles never assemble or copy these physical trees.
        for(const auto kind:{TrackCatalog::Kind::Track,TrackCatalog::Kind::Character})
            for(auto& item:TrackCatalog::selected_items(profile.directory/"legacy",manifest.revision,kind))
                (kind==TrackCatalog::Kind::Character?sources.characters:sources.tracks).push_back({profile.directory/"legacy",std::move(item)});
        check_storage(profile.directory/"tracks",true);
        for(const auto& entry:std::filesystem::directory_iterator(profile.directory/"tracks")) {
            if(sources.courses.size()>=MaxTracks)throw Error("Online Track Lab library exceeds its budget.");
            sources.courses.push_back(entry.path());
        }
    }
    std::map<std::pair<Kind,std::string>,Content> expected;
    for(const auto& item:manifest.content)expected.emplace(std::make_pair(item.kind,item.id),item);
    std::set<std::pair<Kind,std::string>> found;
    std::set<std::filesystem::path> reviews;
    for(const auto kind:{Kind::Track,Kind::Character}) {
        for(const auto& source:kind==Kind::Track?sources.tracks:sources.characters) {
            const auto& item=source.item;
            const auto key=std::make_pair(kind,item.id);
            const auto entry=expected.find(key);
            if(entry==expected.end() || entry->second.name!=item.name || entry->second.artifact!=item.artifact ||
               entry->second.bank!=item.bank || entry->second.payload!=item.patch || !found.insert(key).second)
                throw Error("Online profile's activated legacy assets do not match the host.");
            if(!valid_digest(item.review))throw Error("Referenced online assets need a retained source review.");
            const auto review=source.library/"reviews"/item.review;
            if(reviews.insert(review).second) {
                check_storage(review/"review.json",false);
                if(sha256(read_file(review/"review.json",8*MiB))!=item.review)throw Error("Referenced online source review changed.");
            }
        }
    }
    for(const auto& path:sources.courses) {
        check_storage(path,true);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(path,track,error))throw Error("Online Track Lab course failed revalidation: "+error);
        const auto name=std::string("dkrmap:")+track.id;
        const auto id=sha256(View(reinterpret_cast<const std::uint8_t*>(name.data()),name.size()));
        const auto key=std::make_pair(Kind::TrackLab,id);const auto wanted=expected.find(key);
        if(wanted==expected.end() || wanted->second.name!=track.name ||
           sha256(read_file(path/"manifest.json",MiB))!=wanted->second.artifact ||
           sha256(pack_track_lab(path))!=wanted->second.payload || !found.insert(key).second)
            throw Error("Online Track Lab course differs from its host manifest.");
    }
    if(found.size()!=expected.size())throw Error("Online profile is missing required host content.");
    return sources;
}
void validate_profile(const Profile& profile){(void)profile_sources(profile);}
namespace {
void ensure_directory(const std::filesystem::path& path) {
    if(std::filesystem::exists(path)){check_storage(path,true);return;}
    ensure_directory(path.parent_path());child(path);
}
using Tree=std::map<std::filesystem::path,std::pair<bool,std::uint64_t>>;
// Ancestors were checked before traversal. Check each leaf before descending;
// do not issue dozens of redundant ancestor queries for every tiny asset.
void ordinary_leaf(const std::filesystem::path& path,bool directory) {
    const auto status=std::filesystem::symlink_status(path);
    if(std::filesystem::is_symlink(status) || (directory?!std::filesystem::is_directory(status):!std::filesystem::is_regular_file(status)))
        throw Error("Online mod tree contains a link or unsupported file type.");
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw Error("Online mod tree contains a reparse point.");
#endif
}
Tree retained_tree(const std::filesystem::path& root,std::stop_token stop={}) {
    check_storage(root,true);Tree files;std::uint64_t bytes=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
        cancelled(stop);if(files.size()>=32768)throw Error("Offline mod copy exceeds its file-count budget.");
        const bool directory=entry.is_directory();ordinary_leaf(entry.path(),directory);
        const auto size=directory?0:entry.file_size();
        if(size>std::uint64_t(1024*MiB)-bytes)throw Error("Offline mod copy exceeds its storage budget.");
        bytes+=size;files.emplace(entry.path().lexically_relative(root),std::make_pair(directory,size));
    }
    return files;
}
bool same_tree(const std::filesystem::path& a,const std::filesystem::path& b,std::stop_token stop) {
    const auto left=retained_tree(a,stop);if(left!=retained_tree(b,stop))return false;
    std::array<char,65536> x{},y{};
    for(const auto& [name,type]:left)if(!type.first) {
        cancelled(stop);std::ifstream first(private_storage_path(a/name),std::ios::binary),second(private_storage_path(b/name),std::ios::binary);
        if(!first || !second)return false;
        for(std::uint64_t at=0;at<type.second;) {
            cancelled(stop);const auto size=static_cast<std::streamsize>(std::min<std::uint64_t>(x.size(),type.second-at));
            if(!first.read(x.data(),size) || !second.read(y.data(),size) || !std::equal(x.begin(),x.begin()+size,y.begin()))return false;
            at+=size;
        }
    }
    return true;
}
void copy_tree(const std::filesystem::path& source,const std::filesystem::path& target,std::stop_token stop) {
    const auto entries=retained_tree(source,stop);child(target);
    for(const auto& [name,type]:entries) {
        cancelled(stop);const auto to=private_storage_path(target/name),from=private_storage_path(source/name);
        if(type.first)ensure_directory(to);
        else {ordinary_leaf(to.parent_path(),true);ordinary_leaf(from,false);std::filesystem::copy_file(from,to);}
    }
    if(!same_tree(source,target,stop))throw Error("A verified mod changed while being kept. No existing offline files were replaced.");
}
}
void retain_profile(const Profile& input_profile,const std::filesystem::path& library,
    const std::filesystem::path& track_library,std::stop_token stop) {
    const Profile profile{input_profile.manifest,input_profile.digest,private_storage_path(input_profile.directory)};
    cancelled(stop);const auto sources=profile_sources(profile);
    if(profile.manifest.content.empty())return;
    const auto root=private_storage_path(library),tracks=private_storage_path(track_library);
    ensure_directory(root);ensure_directory(tracks);
    for(const auto* folder:{"reviews","prepared","prepared-characters","online-keeps"})ensure_directory(root/folder);
    std::set<std::filesystem::path> libraries;
    std::map<std::pair<std::string,std::string>,std::filesystem::path> trees;
    for(const auto* selected:{&sources.tracks,&sources.characters})for(const auto& source:*selected) {
        libraries.insert(source.library);
        const auto folder=selected==&sources.tracks?"prepared":"prepared-characters";
        for(const auto& [type,id]:{std::make_pair(std::string(folder),source.item.group),std::make_pair(std::string("reviews"),source.item.review)}) {
            if(!valid_digest(id))throw Error("Invalid retained source identity.");
            const auto path=source.library/type/id;
            const auto [at,inserted]=trees.emplace(std::make_pair(type,id),path);
            if(!inserted && at->second!=path && !same_tree(at->second,path,stop))throw Error("Conflicting verified source groups.");
        }
    }
    for(const auto& library: libraries)if(library!=root)TrackCatalog::validate_retained_merge(root,library);
    struct Copy {std::filesystem::path source,target,staged;};std::vector<Copy> copies;
    std::uint64_t bytes=0;unsigned files=0;
    for(const auto& [identity,source]:trees) {
            const auto& [folder,name]=identity;
            cancelled(stop);
            if(!valid_digest(name))throw Error("Invalid verified offline storage identity.");
            if(folder=="reviews" && sha256(read_file(source/"review.json",8*MiB))!=name)
                throw Error("The retained source review no longer matches its identity.");
            const auto target=root/folder/name;
            // Already installed assets are the source itself: no copy or
            // expensive recursive comparison of an entire multi-mod pack.
            if(source==target)continue;
            if(std::filesystem::exists(target)) {
                if(!same_tree(source,target,stop))throw Error("A kept mod conflicts with existing offline files. They were not replaced.");
                continue;
            }
            copies.push_back({source,target,{}});
            for(const auto& [path,type]:retained_tree(source,stop)) {
                if(++files>32768 || type.second>std::uint64_t(1024*MiB)-bytes)throw Error("Offline mod copy exceeds its storage budget.");
                bytes+=type.second;
            }
    }
    // Track Lab identity, not folder name, owns a slot. Do not overwrite a
    // user's authored/installed variant that happens to have the same id.
    std::map<std::string,std::filesystem::path> existing;
    unsigned inspected=0;
    for(const auto& entry:std::filesystem::directory_iterator(tracks)) {
        if(++inspected>4096)throw Error("Offline Track Lab library exceeds its inspection budget.");
        if(entry.path().extension()!=".dkrmap")continue;
        check_storage(entry.path(),true);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(entry.path(),track,error))
            throw Error("An installed Track Lab course needs repair before keeping new copies: "+error);
        if(!existing.emplace(track.id,entry.path()).second)throw Error("Installed Track Lab course identities are duplicated.");
    }
    std::vector<std::pair<std::filesystem::path,Bytes>> courses;
    for(const auto& course:sources.courses) {
        cancelled(stop);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(course,track,error))throw Error(error);
        const auto data=pack_track_lab(course,stop);
        if(const auto at=existing.find(track.id);at!=existing.end()) {
            if(pack_track_lab(at->second,stop)!=data)throw Error("A downloaded Track Lab course conflicts with an installed id. The installed course was not replaced.");
            continue;
        }
        const auto target=tracks/course.filename();
        if(std::filesystem::exists(target))throw Error("The offline Track Lab destination is already in use.");
        if(data.size()>std::uint64_t(1024*MiB)-bytes)throw Error("Offline Track Lab copy exceeds its storage budget.");
        bytes+=data.size();courses.emplace_back(target,data);
    }
    if(std::filesystem::space(root).available<bytes+64ULL*MiB || std::filesystem::space(tracks).available<bytes+64ULL*MiB)
        throw Error("Not enough free space to keep offline copies. Verified online mods remain in the session cache.");
    ensure_directory(tracks/"online-keeps");
    Job track_job{tracks/"online-keeps",tracks/"online-keeps"/nonce()};
    if(!std::filesystem::create_directory(track_job.path)){track_job.path.clear();throw Error("Could not reserve an offline course copy job.");}
    check_storage(track_job.path,true);
    Job job{root/"online-keeps",root/"online-keeps"/nonce()};
    if(!std::filesystem::create_directory(job.path)){job.path.clear();throw Error("Could not reserve an offline mod copy job.");}
    check_storage(job.path,true);
    unsigned index=0;
    for(auto& copy:copies) {
        copy.staged=job.path/std::to_string(index++);copy_tree(copy.source,copy.staged,stop);
    }
    // Keep native courses inactive with a local sidecar, not an edit to the
    // hashed manifest. It is never exported in online content packages.
    for(const auto& [target,data]:courses) {
        const auto staged=track_job.path/(std::to_string(index++)+".dkrmap");unpack_track_lab(data,staged,stop);
        write_new_file(staged/".online-keep-state",Bytes{'0'});copies.push_back({{},target,staged});
    }
    cancelled(stop);
    for(const auto& copy:copies) {
        cancelled(stop);std::error_code error;std::filesystem::rename(copy.staged,copy.target,error);
        if(error && (!std::filesystem::exists(copy.target) || !same_tree(copy.staged,copy.target,stop)))
            throw Error("Could not publish a complete offline copy. Existing mods and selections were not replaced.");
    }
    std::fprintf(stderr,"[online-mods][keep] unique-trees=%zu copied-trees=%zu copied-files=%u bytes=%llu\n",trees.size(),copies.size(),files,static_cast<unsigned long long>(bytes));
    // No host catalogues, enablement lists, visibility, ROMs or saves copied.
}
void verify_online_worker(const std::filesystem::path& worker,const std::filesystem::path& online_root,std::stop_token stop) {
    cancelled(stop);
    const auto root=private_storage_path(online_root);check_storage(root,true);child(root/"jobs");
    if(!worker.is_absolute() || !std::filesystem::is_regular_file(private_storage_path(worker)))
        throw Error("The packaged ModWorker is missing. Extract the entire new release into a new folder, including libexec/dkr-r. Do not copy only DKR-R.exe.");
    check_storage(worker,false);
    Job job{root/"jobs",root/"jobs"/nonce()};
    if(!std::filesystem::create_directory(job.path)){job.path.clear();throw Error("Could not reserve a worker compatibility probe.");}
    check_storage(job.path,true);
    write(job.path/"request.json",{{"schema",Schema},{"operation","online-capabilities"},{"source",path_text(root)},{"roms",json::array()}});
    const auto outcome=run_worker(worker,job.path/"request.json",[&]{return !stop.stop_requested();},std::chrono::seconds(10));
    cancelled(stop);
    if(outcome.outcome!=WorkerOutcome::Completed || !std::filesystem::exists(job.path/"capabilities.json") ||
        read(job.path/"capabilities.json",4096)!=json({{"schema",Schema},{"online_prepare",1}}))
        throw Error("The installed ModWorker does not support this build's online mod preparation. Extract the entire new release into a new folder, including libexec/dkr-r; every racer must use that complete package.");
    std::fprintf(stderr,"[online-mods][worker] online-prepare=1 path=%s\n",path_text(worker).c_str());
}
void cache_installed_payloads(const Manifest& input,const std::filesystem::path& input_payloads,
    const std::filesystem::path& input_library,std::stop_token stop,const std::filesystem::path& track_library) {
    const auto manifest=canonical(input);
    const auto payloads=private_storage_path(input_payloads);check_storage(payloads,true);
    // Authored Track Lab packages also have a canonical wire representation.
    // Local enablement sidecars are excluded by the existing packer.
    if(!track_library.empty() && std::filesystem::exists(track_library)) {
        std::map<std::string,Payload> courses;
        for(const auto& p:manifest.payloads)if(p.kind==PayloadKind::TrackLab && !cached_payload(payloads,p))courses.emplace(p.digest,p);
        const auto tracks=private_storage_path(track_library);check_storage(tracks,true);unsigned examined=0;
        for(const auto& entry:std::filesystem::directory_iterator(tracks)) {
            cancelled(stop);if(courses.empty())break;
            if(++examined>4096)throw Error("Installed course inspection exceeds its bounded cache budget.");
            if(entry.path().extension()!=".dkrmap")continue;
            try {
                const auto bytes=pack_track_lab(entry.path(),stop);const auto digest=sha256(bytes);
                const auto found=courses.find(digest);if(found==courses.end() || bytes.size()!=found->second.size)continue;
                try{write_new_file(payloads/digest,bytes);}catch(...){if(!cached_payload(payloads,found->second))throw;}
                courses.erase(found);
            }catch(const std::exception&){cancelled(stop);}
        }
    }
    if(input_library.empty())return;
    const auto library=private_storage_path(input_library);
    if(!std::filesystem::exists(library/"reviews"))return;
    check_storage(library,true);check_storage(library/"reviews",true);check_storage(payloads,true);
    std::map<std::string,Payload> missing;
    for(const auto& payload:manifest.payloads)if(payload.kind==PayloadKind::Xdelta && !cached_payload(payloads,payload))missing.emplace(payload.digest,payload);
    unsigned examined=0;
    for(const auto& entry:std::filesystem::directory_iterator(library/"reviews")) {
        cancelled(stop);if(missing.empty())break;
        if(++examined>4096)throw Error("Installed review inspection exceeds its bounded cache budget.");
        const auto id=path_text(entry.path().filename());if(!valid_digest(id))continue;
        try {
            check_storage(entry.path(),true);
            const auto data=read_file(entry.path()/"review.json",8*MiB);
            if(sha256(data)!=id)continue;
            const auto receipt=json::parse(data.begin(),data.end(),[](int depth,json::parse_event_t,const json&){
                if(depth>24)throw Error("Installed review nesting exceeds its bounded cache budget.");return true;
            });
            for(const auto& item:receipt.at("packages")) {
                const auto found=missing.find(item.at("patch_sha256").get<std::string>());if(found==missing.end())continue;
                const auto patch=entry.path()/"patches"/(found->first+".xdelta");check_storage(patch,false);
                const auto bytes=read_file(patch,static_cast<std::size_t>(found->second.size));
                if(bytes.size()!=found->second.size || sha256(bytes)!=found->first)continue;
                try{write_new_file(payloads/found->first,bytes);}catch(...){if(!cached_payload(payloads,found->second))throw;}
                missing.erase(found);
            }
        }catch(const std::exception&) {
            cancelled(stop); // Invalid optional local entries are never trusted.
        }
    }
}
namespace {
void append_sources(ProfileSources& to,ProfileSources from) {
    to.tracks.insert(to.tracks.end(),std::make_move_iterator(from.tracks.begin()),std::make_move_iterator(from.tracks.end()));
    to.characters.insert(to.characters.end(),std::make_move_iterator(from.characters.begin()),std::make_move_iterator(from.characters.end()));
    to.courses.insert(to.courses.end(),std::make_move_iterator(from.courses.begin()),std::make_move_iterator(from.courses.end()));
}
bool reuse_installed(const Manifest& manifest,const std::filesystem::path& library,
    const std::vector<TrackCatalogItem>& tracks,const std::vector<TrackCatalogItem>& characters,
    const std::map<std::string,std::filesystem::path>& courses,ProfileSources& result) {
    ProfileSources selected;
    for(const auto& content:manifest.content) {
        if(content.kind==Kind::TrackLab) {
            const auto at=courses.find(content.payload);if(at==courses.end())return false;
            selected.courses.push_back(at->second);continue;
        }
        if(library.empty())return false;
        const auto& items=content.kind==Kind::Track?tracks:characters;
        const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.id==content.id &&
            item.name==content.name && item.artifact==content.artifact && item.patch==content.payload && item.bank==content.bank && valid_digest(item.review);});
        if(found==items.end())return false;
        (content.kind==Kind::Track?selected.tracks:selected.characters).push_back({library,*found});
    }
    append_sources(result,std::move(selected));return true;
}
void write_sources(const std::filesystem::path& directory,const Manifest& manifest,const ProfileSources& sources) {
    json refs={{"schema",1},{"manifest",identity(manifest)},{"legacy",json::array()},{"courses",json::array()}};
    for(unsigned kind=0;kind<2;++kind)for(const auto& source:kind?sources.characters:sources.tracks)
        refs["legacy"].push_back({{"kind",kind},{"id",source.item.id},{"group",source.item.group},{"library",path_text(source.library)}});
    for(const auto& course:sources.courses)refs["courses"].push_back(path_text(course));
    write(directory/"sources.json",refs);
}
}
std::shared_ptr<const Profile> prepare_profile(const Manifest& input,const std::filesystem::path& input_payloads,
    const std::vector<std::filesystem::path>& roms,const std::filesystem::path& input_root,
    const std::filesystem::path& worker,std::stop_token stop,const ProfileProgress& progress,const std::filesystem::path& installed_library,
    const std::filesystem::path& installed_track_library,const std::vector<std::filesystem::path>& selected_courses) {
    auto out=std::make_shared<Profile>();out->manifest=canonical(input);out->digest=identity(out->manifest);
    if(roms.empty() || roms.size()>8 || !worker.is_absolute())throw Error("Online preparation needs imported Game Paks and the packaged importer.");
    // check_storage normalizes its queries, not the caller's path. Normalize
    // before deriving directories so every create/rename/iterator uses the
    // same long-path representation as validation and file I/O on Windows.
    const auto root=private_storage_path(input_root);
    const auto payloads=private_storage_path(input_payloads);
    check_storage(worker,false);check_storage(payloads,true);cancelled(stop);
    check_storage(root,true);child(root/"source-profiles");child(root/"prepared-payloads");child(root/"jobs");
    verify_online_worker(worker,root,stop);
    out->directory=root/"source-profiles"/out->digest;
    if(std::filesystem::exists(out->directory)) {
        if(!installed_library.empty() || !installed_track_library.empty() || !selected_courses.empty()) {
            // Prefer the current installed sources on rejoin, particularly
            // after a first download was kept offline. An older cache index
            // still references the online staging copy; returning it would
            // force recursive comparisons of those same packs on every keep.
            // Preserve the old receipt and publish only a new tiny index.
            out->directory=root/"source-profiles"/nonce();
        } else {
        if(progress)progress("Revalidating the cached online mod profile");
        try{validate_profile(*out);return out;}
        catch(const std::exception&) {
            cancelled(stop);
            // A user may remove/reimport a group after the last lobby. Leave
            // the old index untouched and build a new fully verified one.
            if(progress)progress("Refreshing stale online asset references");
            out->directory=root/"source-profiles"/nonce();
        }
        }
    }
    std::filesystem::path installed;
    std::vector<TrackCatalogItem> installed_tracks,installed_characters;
    if(!installed_library.empty() && std::filesystem::exists(installed_library)) {
        try {
            installed=private_storage_path(installed_library);
            if(progress)progress("Checking already installed mod resources");
            installed_tracks=TrackCatalog::installed_items(installed,out->manifest.revision,TrackCatalog::Kind::Track);
            installed_characters=TrackCatalog::installed_items(installed,out->manifest.revision,TrackCatalog::Kind::Character);
        }catch(const std::exception&){cancelled(stop);installed.clear();installed_tracks.clear();installed_characters.clear();}
    }
    std::map<std::string,std::filesystem::path> installed_courses;
    if(std::any_of(out->manifest.content.begin(),out->manifest.content.end(),[](const auto& c){return c.kind==Kind::TrackLab;})) {
        auto candidates=selected_courses;
        if(!installed_track_library.empty() && std::filesystem::exists(installed_track_library)) {
            const auto library=private_storage_path(installed_track_library);check_storage(library,true);unsigned examined=0;
            for(const auto& entry:std::filesystem::directory_iterator(library)) {
                if(++examined>4096)throw Error("Installed course inspection exceeds its budget.");
                if(entry.path().extension()==".dkrmap")candidates.push_back(entry.path());
            }
        }
        if(candidates.size()>4096+MaxTracks)throw Error("Installed course candidates exceed their budget.");
        for(const auto& input:candidates) {
            cancelled(stop);
            try {
                const auto path=private_storage_path(input);check_storage(path,true);
                dkr::runtime::custom_tracks::Track track;std::string error;
                if(!dkr::runtime::custom_tracks::inspect_package(path,track,error))continue;
                const auto name=std::string("dkrmap:")+track.id;
                const auto id=sha256(View(reinterpret_cast<const std::uint8_t*>(name.data()),name.size()));
                const auto content=std::find_if(out->manifest.content.begin(),out->manifest.content.end(),[&](const auto& c){return c.kind==Kind::TrackLab && c.id==id && c.name==track.name;});
                if(content==out->manifest.content.end() || installed_courses.contains(content->payload))continue;
                check_storage(path/"manifest.json",false);
                if(sha256(read_file(path/"manifest.json",MiB))!=content->artifact || sha256(pack_track_lab(path,stop))!=content->payload)continue;
                installed_courses.emplace(content->payload,path);
            }catch(const std::exception&){cancelled(stop);} // optional local reuse must never waive verification
        }
    }
    // Reconstruction can expand payloads; refuse early instead of exhausting
    // disk space or damaging unrelated installed content.
    Job job{root/"jobs",root/"jobs"/nonce()};
    if(!std::filesystem::create_directory(job.path)){job.path.clear();throw Error("Could not reserve an isolated online mod job.");}
    check_storage(job.path,true);child(job.path/"parts");child(job.path/"content");
    // Every patch keeps the ordinary importer's CPU/memory/wall-clock limits.
    // A library is not one unbounded import, nor one three-minute job that
    // incorrectly rejects a large set of individually valid patches.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(15);
    const auto begin=std::chrono::steady_clock::now();
    ProfileSources sources;unsigned index=0,workers=0,installed_count=0,cached_count=0;
    for(const auto& payload:out->manifest.payloads) {
        cancelled(stop);
        if(std::chrono::steady_clock::now()>=deadline)throw Error("Online mods exceeded the whole-session preparation limit.");
        const auto part=job.path/"parts"/payload.digest;child(part);
        // AI selection affects the final session, not reconstructed asset bytes.
        // Keep per-mod cache identities independent of that gameplay setting.
        Manifest subset;subset.revision=out->manifest.revision;
        subset.payloads.push_back(payload);
        for(const auto& content:out->manifest.content)if(content.payload==payload.digest)subset.content.push_back(content);
        subset=canonical(std::move(subset));
        const Profile reusable{subset,identity(subset),root/"prepared-payloads"/identity(subset)};
        auto source=reusable.directory;
        const auto started=std::chrono::steady_clock::now();
        const auto prefix="Mod "+std::to_string(++index)+"/"+std::to_string(out->manifest.payloads.size())+": ";
        std::string method="cache";
        const bool reused=reuse_installed(subset,installed,installed_tracks,installed_characters,installed_courses,sources);
        if(reused) {
            method="installed-reference";++installed_count;
            if(progress)progress(prefix+"Reusing matching installed assets");
        } else if(std::filesystem::exists(source)) {
            method="cache-reference";++cached_count;
            if(progress)progress(prefix+"Revalidating previously prepared assets");
            append_sources(sources,profile_sources(reusable));
        } else {
        method="worker";++workers;
        if(std::filesystem::space(root).available<1280ULL*MiB)throw Error("Missing online mods need at least 1.25 GiB of free staging space.");
        write_new_file(part/"manifest.json",encode(subset));
        json request={{"schema",Schema},{"operation","online-prepare"},{"source",path_text(std::filesystem::absolute(payloads))},
            {"manifest",path_text(part/"manifest.json")},{"roms",json::array()}};
        for(const auto& rom:roms)request["roms"].push_back(path_text(std::filesystem::absolute(rom)));
        write(part/"request.json",request);auto poll=std::chrono::steady_clock::now();
        if(progress)progress(prefix+"Reconstructing locally owned assets");
        const auto result=run_worker(worker,part/"request.json",[&] {
            if(stop.stop_requested() || std::chrono::steady_clock::now()>=deadline)return false;
            const auto now=std::chrono::steady_clock::now();if(now<poll)return true;poll=now+std::chrono::milliseconds(200);
            const auto events=part/"progress.jsonl";if(!progress || !std::filesystem::exists(events))return true;
            check_storage(events,false);const auto bytes=read_file(events,MiB);const std::string text(bytes.begin(),bytes.end());
            const auto end=text.find_last_of('\n');if(end==text.npos)return true;
            const auto before=end?text.find_last_of('\n',end-1):text.npos;const auto at=before==text.npos?0:before+1;
            const auto event=json::parse(text.substr(at,end-at));const auto stage=event.at("stage").get<std::string>();
            if(stage.size()>256)throw Error("Invalid private importer progress label.");progress(prefix+stage);return true;
        });
        cancelled(stop);
        if(result.outcome!=WorkerOutcome::Completed) {
            if(std::filesystem::exists(part/"failure.json"))throw Error(read(part/"failure.json",8192).at("error").get<std::string>().substr(0,1024));
            throw Error(result.outcome==WorkerOutcome::TimedOut?"A mod exceeded its bounded preparation time.":"The isolated online importer stopped safely; no profile was activated.");
        }
        // Validate each part before merging. Moving exclusively owned children
        // cannot replace installed/offline files, or a previously cached profile.
        validate_profile({subset,identity(subset),part/"content"});
        cancelled(stop);std::error_code publish_error;
        std::filesystem::rename(part/"content",source,publish_error);
        if(publish_error) {
            if(!std::filesystem::exists(source))throw Error("Could not publish a verified per-mod preparation cache.");
            validate_profile(reusable);
        }
        append_sources(sources,profile_sources(reusable));
        }
        // Each source is referenced once; no aggregate pack/review copy.
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::fprintf(stderr,"[online-mods][prepare] mod=%u/%zu method=%s elapsed-ms=%lld\n",index,out->manifest.payloads.size(),method.c_str(),static_cast<long long>(elapsed));
    }
    write_sources(job.path/"content",out->manifest,sources);
    write(job.path/"content"/"verified.json",{{"schema",ManifestSchema},{"manifest",out->digest}});
    Profile prepared{out->manifest,out->digest,job.path/"content"};validate_profile(prepared);cancelled(stop);
    std::error_code ec;std::filesystem::rename(prepared.directory,out->directory,ec);
    if(ec) {
        if(!std::filesystem::exists(out->directory))throw Error("Could not publish the verified online profile atomically.");
        validate_profile(*out); // another identical transaction won, never replace it
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    std::fprintf(stderr,"[online-mods][profile] roots=%zu payloads=%zu installed=%u cache=%u worker=%u aggregate-copied-files=0 elapsed-ms=%lld\n",
        out->manifest.content.size(),out->manifest.payloads.size(),installed_count,cached_count,workers,static_cast<long long>(elapsed));
    return out;
}
}
