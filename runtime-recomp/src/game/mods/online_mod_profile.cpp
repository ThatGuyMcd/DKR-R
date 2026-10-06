#include "online_mod_profile.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_track_catalog.hpp"
#include "../custom_tracks.hpp"
#include <json/json.hpp>
#include <random>
#include <set>
#include <algorithm>
#include <fstream>

namespace dkr::mods::online {
namespace {
using nlohmann::json;
std::string path_text(const std::filesystem::path& path){const auto text=path.generic_u8string();return {text.begin(),text.end()};}
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("Online mod preparation cancelled. Offline mods and saves were not changed.");}
json read(const std::filesystem::path& path,std::size_t limit=MiB){check_storage(path,false);const auto bytes=read_file(path,limit);return json::parse(bytes.begin(),bytes.end());}
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
void validate_profile(const Profile& input_profile) {
    const Profile profile{input_profile.manifest,input_profile.digest,private_storage_path(input_profile.directory)};
    const auto manifest=canonical(profile.manifest);
    if(profile.digest!=identity(manifest))throw Error("Online profile identity differs from its frozen manifest.");
    check_storage(profile.directory,true);
    const auto receipt=read(profile.directory/"verified.json");
    if(receipt.at("schema")!=ManifestSchema || receipt.at("manifest")!=profile.digest)throw Error("Online profile does not have a complete preparation receipt.");
    std::map<std::pair<Kind,std::string>,Content> expected;
    for(const auto& item:manifest.content)expected.emplace(std::make_pair(item.kind,item.id),item);
    std::set<std::pair<Kind,std::string>> found;
    for(const auto kind:{TrackCatalog::Kind::Track,TrackCatalog::Kind::Character}) {
        for(const auto& item:TrackCatalog::selected_items(profile.directory/"legacy",manifest.revision,kind)) {
            const auto key=std::make_pair(kind==TrackCatalog::Kind::Track?Kind::Track:Kind::Character,item.id);
            const auto entry=expected.find(key);
            if(entry==expected.end() || entry->second.name!=item.name || entry->second.artifact!=item.artifact ||
               entry->second.bank!=item.bank || entry->second.payload!=item.patch || !found.insert(key).second)
                throw Error("Online profile's activated legacy assets do not match the host.");
        }
    }
    unsigned examined=0;check_storage(profile.directory/"tracks",true);
    for(const auto& entry:std::filesystem::directory_iterator(profile.directory/"tracks")) {
        if(++examined>MaxTracks)throw Error("Online Track Lab library exceeds its budget.");
        check_storage(entry.path(),true);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(entry.path(),track,error))throw Error("Online Track Lab course failed revalidation: "+error);
        const auto name=std::string("dkrmap:")+track.id;
        const auto id=sha256(View(reinterpret_cast<const std::uint8_t*>(name.data()),name.size()));
        const auto key=std::make_pair(Kind::TrackLab,id);const auto wanted=expected.find(key);
        if(wanted==expected.end() || wanted->second.name!=track.name ||
           sha256(read_file(entry.path()/"manifest.json",MiB))!=wanted->second.artifact ||
           sha256(pack_track_lab(entry.path()))!=wanted->second.payload || !found.insert(key).second)
            throw Error("Online Track Lab course differs from its host manifest.");
    }
    if(found.size()!=expected.size())throw Error("Online profile is missing required host content.");
}
namespace {
void ensure_directory(const std::filesystem::path& path) {
    if(std::filesystem::exists(path)){check_storage(path,true);return;}
    ensure_directory(path.parent_path());child(path);
}
using Tree=std::map<std::filesystem::path,std::pair<bool,std::uint64_t>>;
Tree retained_tree(const std::filesystem::path& root) {
    check_storage(root,true);Tree files;std::uint64_t bytes=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
        if(files.size()>=32768)throw Error("Offline mod copy exceeds its file-count budget.");
        const bool directory=entry.is_directory();check_storage(entry.path(),directory);
        const auto size=directory?0:entry.file_size();
        if(size>std::uint64_t(1024*MiB)-bytes)throw Error("Offline mod copy exceeds its storage budget.");
        bytes+=size;files.emplace(entry.path().lexically_relative(root),std::make_pair(directory,size));
    }
    return files;
}
bool same_tree(const std::filesystem::path& a,const std::filesystem::path& b,std::stop_token stop) {
    const auto left=retained_tree(a);if(left!=retained_tree(b))return false;
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
    const auto entries=retained_tree(source);child(target);
    for(const auto& [name,type]:entries) {
        cancelled(stop);const auto to=private_storage_path(target/name),from=private_storage_path(source/name);
        if(type.first)ensure_directory(to);
        else {ensure_directory(to.parent_path());check_storage(from,false);std::filesystem::copy_file(from,to);}
    }
    if(!same_tree(source,target,stop))throw Error("A verified mod changed while being kept. No existing offline files were replaced.");
}
}
void retain_profile(const Profile& input_profile,const std::filesystem::path& library,
    const std::filesystem::path& track_library,std::stop_token stop) {
    const Profile profile{input_profile.manifest,input_profile.digest,private_storage_path(input_profile.directory)};
    cancelled(stop);validate_profile(profile);
    if(profile.manifest.content.empty())return;
    const auto root=private_storage_path(library),tracks=private_storage_path(track_library);
    ensure_directory(root);ensure_directory(tracks);
    for(const auto* folder:{"reviews","prepared","prepared-characters","online-keeps"})ensure_directory(root/folder);
    TrackCatalog::validate_retained_merge(root,profile.directory/"legacy");
    struct Copy {std::filesystem::path source,target,staged;};std::vector<Copy> copies;
    std::uint64_t bytes=0;unsigned files=0;
    for(const auto* folder:{"reviews","prepared","prepared-characters"}) {
        for(const auto& entry:std::filesystem::directory_iterator(profile.directory/"legacy"/folder)) {
            cancelled(stop);const auto name=path_text(entry.path().filename());
            if(!valid_digest(name))throw Error("Invalid verified offline storage identity.");
            if(std::string_view(folder)=="reviews" && sha256(read_file(entry.path()/"review.json",8*MiB))!=name)
                throw Error("The retained source review no longer matches its identity.");
            const auto target=root/folder/entry.path().filename();
            if(std::filesystem::exists(target)) {
                if(!same_tree(entry.path(),target,stop))throw Error("A kept mod conflicts with existing offline files. They were not replaced.");
                continue;
            }
            copies.push_back({entry.path(),target,{}});
            for(const auto& [path,type]:retained_tree(entry.path())) {
                if(++files>32768 || type.second>std::uint64_t(1024*MiB)-bytes)throw Error("Offline mod copy exceeds its storage budget.");
                bytes+=type.second;
            }
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
    for(const auto& entry:std::filesystem::directory_iterator(profile.directory/"tracks")) {
        cancelled(stop);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(entry.path(),track,error))throw Error(error);
        const auto data=pack_track_lab(entry.path(),stop);
        if(const auto at=existing.find(track.id);at!=existing.end()) {
            if(pack_track_lab(at->second,stop)!=data)throw Error("A downloaded Track Lab course conflicts with an installed id. The installed course was not replaced.");
            continue;
        }
        const auto target=tracks/entry.path().filename();
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
std::shared_ptr<const Profile> prepare_profile(const Manifest& input,const std::filesystem::path& input_payloads,
    const std::vector<std::filesystem::path>& roms,const std::filesystem::path& input_root,
    const std::filesystem::path& worker,std::stop_token stop,const ProfileProgress& progress) {
    auto out=std::make_shared<Profile>();out->manifest=canonical(input);out->digest=identity(out->manifest);
    if(roms.empty() || roms.size()>8 || !worker.is_absolute())throw Error("Online preparation needs imported Game Paks and the packaged importer.");
    // check_storage normalizes its queries, not the caller's path. Normalize
    // before deriving directories so every create/rename/iterator uses the
    // same long-path representation as validation and file I/O on Windows.
    const auto root=private_storage_path(input_root);
    const auto payloads=private_storage_path(input_payloads);
    check_storage(worker,false);check_storage(payloads,true);cancelled(stop);
    check_storage(root,true);child(root/"profiles");child(root/"jobs");
    verify_online_worker(worker,root,stop);
    out->directory=root/"profiles"/out->digest;
    if(std::filesystem::exists(out->directory)){if(progress)progress("Revalidating the cached online mod profile");validate_profile(*out);return out;}
    // Reconstruction can expand payloads; refuse early instead of exhausting
    // disk space or damaging unrelated installed content.
    if(std::filesystem::space(root).available<1280ULL*MiB)throw Error("Online mod preparation needs at least 1.25 GiB of free staging space.");
    Job job{root/"jobs",root/"jobs"/nonce()};
    if(!std::filesystem::create_directory(job.path)){job.path.clear();throw Error("Could not reserve an isolated online mod job.");}
    check_storage(job.path,true);child(job.path/"parts");child(job.path/"content");
    child(job.path/"content"/"legacy");child(job.path/"content"/"tracks");
    for(const auto* name:{"reviews","prepared","prepared-characters","jobs"})child(job.path/"content"/"legacy"/name);
    // Every patch keeps the ordinary importer's CPU/memory/wall-clock limits.
    // A library is not one unbounded import, nor one three-minute job that
    // incorrectly rejects a large set of individually valid patches.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(15);
    std::uint64_t staged_bytes=0;unsigned staged_files=0,index=0;
    for(const auto& payload:out->manifest.payloads) {
        cancelled(stop);
        if(std::chrono::steady_clock::now()>=deadline)throw Error("Online mods exceeded the whole-session preparation limit.");
        const auto part=job.path/"parts"/payload.digest;child(part);
        Manifest subset;subset.revision=out->manifest.revision;subset.custom_ai=out->manifest.custom_ai;
        subset.payloads.push_back(payload);
        for(const auto& content:out->manifest.content)if(content.payload==payload.digest)subset.content.push_back(content);
        write_new_file(part/"manifest.json",encode(subset));
        json request={{"schema",Schema},{"operation","online-prepare"},{"source",path_text(std::filesystem::absolute(payloads))},
            {"manifest",path_text(part/"manifest.json")},{"roms",json::array()}};
        for(const auto& rom:roms)request["roms"].push_back(path_text(std::filesystem::absolute(rom)));
        write(part/"request.json",request);auto poll=std::chrono::steady_clock::now();
        const auto prefix="Mod "+std::to_string(++index)+"/"+std::to_string(out->manifest.payloads.size())+": ";
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
        for(const auto& entry:std::filesystem::recursive_directory_iterator(part/"content")) {
            if(++staged_files>32768)throw Error("Online preparation exceeded its whole-session file-count budget.");
            check_storage(entry.path(),entry.is_directory());
            if(entry.is_regular_file()) {
                const auto bytes=entry.file_size();
                if(bytes>std::uint64_t(1024*MiB)-staged_bytes)throw Error("Online preparation exceeded its whole-session storage budget.");
                staged_bytes+=bytes;
            }
        }
        for(const auto* folder:{"reviews","prepared","prepared-characters","jobs","../tracks"}) {
            const auto from=(part/"content"/"legacy"/folder).lexically_normal();
            const auto to=(job.path/"content"/"legacy"/folder).lexically_normal();
            for(const auto& entry:std::filesystem::directory_iterator(from)) {
                const auto target=to/entry.path().filename();
                if(std::filesystem::exists(target))throw Error("Duplicate prepared online asset publication.");
                std::filesystem::rename(entry.path(),target);
            }
        }
    }
    std::set<std::string> tracks,characters;
    for(const auto& content:out->manifest.content) {
        if(content.kind==Kind::Track)tracks.insert(content.id);
        else if(content.kind==Kind::Character)characters.insert(content.id);
    }
    write(job.path/"content"/"legacy"/"track-catalog.json",{{"schema",1},{"enabled",tracks}});
    write(job.path/"content"/"legacy"/"character-catalog.json",{{"schema",1},{"enabled",characters}});
    write(job.path/"content"/"verified.json",{{"schema",ManifestSchema},{"manifest",out->digest}});
    Profile prepared{out->manifest,out->digest,job.path/"content"};validate_profile(prepared);cancelled(stop);
    std::error_code ec;std::filesystem::rename(prepared.directory,out->directory,ec);
    if(ec) {
        if(!std::filesystem::exists(out->directory))throw Error("Could not publish the verified online profile atomically.");
        validate_profile(*out); // another identical transaction won, never replace it
    }
    return out;
}
}
