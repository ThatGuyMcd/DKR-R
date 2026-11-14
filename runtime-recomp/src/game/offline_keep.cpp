#include "cache_repair.hpp"
#include "vehicle_online.hpp"
#include "online_mod_profile.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_track_catalog.hpp"
#include "online_mod_cache.hpp"
#include "online_mod_prepare.hpp"
#include "magic_code_game_world.hpp"
#include "online_game_pak.hpp"
#include "../custom_tracks.hpp"
#include <json/json.hpp>
#include <random>
#include <set>
#include <algorithm>
#include <fstream>
#include <array>
#include <optional>
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

namespace {
std::mutex offline_keep_mutex;
using KeepKey=std::tuple<bool,std::string,std::string>;
KeepKey keep_key(bool characters,const TrackCatalogItem& item){return {characters,item.id,item.revision};}
const char* keep_folder(bool characters){return characters?"prepared-characters":"prepared";}
const char* keep_entries(bool characters){return characters?"characters":"tracks";}
void verify_keep_asset(const CatalogSource& source,bool characters,std::stop_token stop) {
    cancelled(stop);const auto& item=source.item;
    for(const auto* id:{&item.group,&item.storage,&item.id,&item.artifact})
        if(!valid_digest(*id))throw Error("Invalid offline mod storage identity.");
    const auto folder=source.library/keep_folder(characters)/item.group/item.storage;
    check_storage(folder,true);const auto path=folder/(characters?"character.json":"track.json");
    check_storage(path,false);const auto raw=read_file(path,MiB);
    if(sha256(raw)!=item.artifact)throw Error("An offline mod manifest needs repair: "+item.name);
    const auto manifest=read(path);
    if(manifest.at("id")!=item.id || manifest.at("revision")!=item.revision)
        throw Error("An offline mod manifest has a conflicting identity: "+item.name);
    std::map<std::string,std::size_t> blobs;
    const auto add=[&](std::string hash,std::size_t size) {
        if(!valid_digest(hash)||size>MaxStaged)throw Error("Invalid retained mod blob descriptor.");
        auto [at,inserted]=blobs.emplace(hash,size);
        if(!inserted&&at->second!=size)throw Error("Conflicting retained mod blob sizes.");
    };
    if(!manifest.at("records").is_array() || manifest.at("records").size()>4096)
        throw Error("Retained mod record budget exceeded.");
    for(const auto& record:manifest.at("records"))
        add(record.at(characters?"hash":"digest").get<std::string>(),record.at("size").get<std::size_t>());
    if(characters) {
        const auto& audio=manifest.at("audio");
        add(audio.at("control"),audio.at("control_size"));
        add(audio.at("samples"),audio.at("sample_size"));
    }
    std::uint64_t total=0;
    for(const auto& [hash,size]:blobs) {
        cancelled(stop);if(size>std::uint64_t(MaxStaged)-total)throw Error("Retained mod asset budget exceeded.");
        total+=size;const auto blob=folder/"blobs"/hash;check_storage(blob,false);
        const auto bytes=read_file(blob,size);
        if(bytes.size()!=size || sha256(bytes)!=hash)throw Error("An offline mod asset needs repair: "+item.name);
    }
}
bool same_keep_content(const TrackCatalogItem& a,const TrackCatalogItem& b) {
    // Names, receipt/artifact encoding and the source patch container are not
    // content identities. Semantic and capability mismatches still fail closed.
    return a.id==b.id&&a.revision==b.revision&&a.bank==b.bank&&a.carrier==b.carrier&&
        a.vehicles==b.vehicles&&a.race_type==b.race_type&&a.base_character==b.base_character&&
        a.behaviour_identity==b.behaviour_identity&&a.behaviour_abi==b.behaviour_abi;
}
struct KeepGroup {bool characters=false;std::filesystem::path source;json receipt;
    std::map<std::pair<std::string,std::string>,TrackCatalogItem> items;};
struct KeepCopy {std::filesystem::path source,target,staged;std::string receipt;
    std::set<std::string> storage;};
}
void retain_legacy_sources(const ProfileSources& sources,const std::filesystem::path& library,std::stop_token stop) {
    cancelled(stop);if(sources.tracks.empty()&&sources.characters.empty())return;
    // Keep jobs from different lobbies serialize only their optional offline
    // work; network admission and the immutable session profile never wait here.
    std::lock_guard lease(offline_keep_mutex);cancelled(stop);
    const auto root=private_storage_path(library);ensure_directory(root);
    for(const auto* folder:{"reviews","prepared","prepared-characters","online-keeps"})ensure_directory(root/folder);
    std::map<KeepKey,CatalogSource> installed;
    std::array<std::size_t,2> item_counts{},group_counts{};
    for(bool characters:{false,true}) {
        const auto type=characters?TrackCatalog::Kind::Character:TrackCatalog::Kind::Track;
        for(const auto* revision:{"us.v77","us.v80"})for(const auto& item:TrackCatalog::installed_items(root,revision,type))
            installed.emplace(keep_key(characters,item),CatalogSource{root,item});
        for(const auto& [key,source]:installed)if(std::get<0>(key)==characters)++item_counts[characters];
        for(const auto& entry:std::filesystem::directory_iterator(root/keep_folder(characters))) {
            cancelled(stop);if(++group_counts[characters]>(characters?MaxInstalledCharacterVariants:1024U))
                throw Error("The offline mod catalogue is full.");
        }
    }
    std::map<std::tuple<bool,std::filesystem::path,std::string>,KeepGroup> groups;
    std::set<KeepKey> selected;unsigned reused=0;
    for(bool characters:{false,true})for(const auto& source:characters?sources.characters:sources.tracks) {
        cancelled(stop);const auto& item=source.item;const auto key=keep_key(characters,item);
        if(!selected.insert(key).second)throw Error("The offline keep selection contains duplicate mod identities.");
        // Always validate the selected source, even when an installed edition
        // can satisfy the optional offline copy. No SHA checks are waived.
        verify_keep_asset(source,characters,stop);
        if(const auto at=installed.find(key);at!=installed.end()) {
            if(!same_keep_content(at->second.item,item))
                throw Error("Downloaded content disagrees with an installed mod identity: "+item.name+". Existing files were not replaced.");
            if(source.library!=root || source.item.group!=at->second.item.group)
                verify_keep_asset(at->second,characters,stop);
            ++reused;continue;
        }
        if(++item_counts[characters]>(characters?MaxInstalledCharacterVariants:1024U))
            throw Error("The offline mod library is full. The online session can still use its verified cache.");
        if(!valid_digest(item.review))throw Error("A retained mod needs a verified source review.");
        const auto path=source.library/keep_folder(characters)/item.group;
        const auto identity=std::tuple{characters,source.library,item.group};
        auto [at,inserted]=groups.try_emplace(identity);
        auto& group=at->second;
        if(inserted) {
            check_storage(path,true);check_storage(path/"prepared.json",false);const auto raw=read_file(path/"prepared.json",MiB);
            if(sha256(raw)!=item.group)throw Error("A selected preparation receipt changed before retention.");
            group={characters,path,read(path/"prepared.json"),{}};
        }
        group.items.emplace(std::pair{item.id,item.revision},item);
    }
    std::vector<KeepCopy> copies;std::map<std::string,std::filesystem::path> reviews;
    for(auto& [identity,group]:groups) {
        cancelled(stop);const auto key=keep_entries(group.characters);auto receipt=group.receipt;
        json entries=json::array();std::set<std::string> storage;
        for(const auto& entry:receipt.at(key)) {
            const auto at=group.items.find({entry.at("id"),entry.at("revision")});if(at==group.items.end())continue;
            const auto& item=at->second;
            if(entry.at("artifact")!=item.artifact || entry.at("patch")!=item.patch ||
               entry.value("storage",item.id)!=item.storage || receipt.at("review")!=item.review)
                throw Error("The selected mod differs from its preparation receipt.");
            entries.push_back(entry);storage.insert(item.storage);
            const auto review=group.source.parent_path().parent_path()/"reviews"/item.review;
            const auto [prev,inserted]=reviews.emplace(item.review,review);
            if(!inserted&&prev->second!=review&&!same_tree(prev->second,review,stop))
                throw Error("Conflicting verified source reviews.");
        }
        if(entries.size()!=group.items.size())throw Error("A selected mod is missing from its preparation receipt.");
        // Retain ONLY selected missing entries. Copying the original whole
        // pack would reintroduce already installed or unselected variants.
        receipt[key]=entries;const auto encoded=receipt.dump();
        const auto name=sha256(View(reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()));
        if(++group_counts[group.characters]>(group.characters?MaxInstalledCharacterVariants:1024U))
            throw Error("Keeping this group would exceed the offline catalogue limit.");
        copies.push_back({group.source,root/keep_folder(group.characters)/name,{},encoded,std::move(storage)});
    }
    for(const auto& [hash,source]:reviews) {
        check_storage(source,true);check_storage(source/"review.json",false);
        if(sha256(read_file(source/"review.json",8*MiB))!=hash)throw Error("A retained source review changed.");
        const auto target=root/"reviews"/hash;
        if(std::filesystem::exists(target)) {
            if(!same_tree(source,target,stop))throw Error("A retained source review conflicts with existing files.");
        }else copies.push_back({source,target,{},{},{}});
    }
    std::uint64_t bytes=0;std::size_t files=0;
    const auto account=[&](std::uint64_t size) {
        if(files>=32768 || size>std::uint64_t(1024*MiB)-bytes)
            throw Error("Offline mod copy exceeds its storage budget.");
        ++files;bytes+=size;
    };
    const auto budget=[&](const std::filesystem::path& source) {
        for(const auto& [name,type]:retained_tree(source,stop)) {
            account(type.second);
        }
    };
    for(const auto& copy:copies) {
        if(copy.receipt.empty())budget(copy.source);
        else {account(copy.receipt.size());for(const auto& storage:copy.storage)budget(copy.source/storage);}
    }
    if(copies.empty()) {
        std::fprintf(stderr,"[online-mods][keep-legacy] reused=%u new-groups=0 copied-bytes=0\n",reused);return;
    }
    if(std::filesystem::space(root).available<bytes+64ULL*MiB)throw Error("Not enough free space to keep offline mods.");
    Job job{root/"online-keeps",root/"online-keeps"/nonce()};
    if(!std::filesystem::create_directory(job.path)){job.path.clear();throw Error("Could not reserve an offline mod keep job.");}
    check_storage(job.path,true);unsigned index=0;
    for(auto& copy:copies) {
        cancelled(stop);copy.staged=job.path/std::to_string(index++);
        if(copy.receipt.empty())copy_tree(copy.source,copy.staged,stop);
        else {
            child(copy.staged);write_new_file(copy.staged/"prepared.json",View(reinterpret_cast<const std::uint8_t*>(copy.receipt.data()),copy.receipt.size()));
            for(const auto& storage:copy.storage)copy_tree(copy.source/storage,copy.staged/storage,stop);
            if(sha256(read_file(copy.staged/"prepared.json",MiB))!=path_text(copy.target.filename()))
                throw Error("The staged preparation receipt changed.");
        }
    }
    // Reviews are published before their prepared groups. Every published
    // directory is complete; interruption can leave only inactive full copies.
    std::stable_sort(copies.begin(),copies.end(),[](const auto& a,const auto& b){return a.receipt.empty()>b.receipt.empty();});
    for(const auto& copy:copies) {
        cancelled(stop);check_storage(copy.target.parent_path(),true);
        if(std::filesystem::exists(copy.target)) {
            check_storage(copy.target,true);
            if(!same_tree(copy.staged,copy.target,stop))throw Error("Existing offline content differs from the staged copy. It was not replaced.");
            continue;
        }
        std::error_code error;std::filesystem::rename(copy.staged,copy.target,error);
        if(error) {
            check_storage(copy.target,true);
            if(!same_tree(copy.staged,copy.target,stop))throw Error("Could not publish a complete offline mod copy.");
        }
    }
    std::fprintf(stderr,"[online-mods][keep-legacy] reused=%u new-groups=%zu copied-bytes=%llu\n",reused,groups.size(),static_cast<unsigned long long>(bytes));
}

}
