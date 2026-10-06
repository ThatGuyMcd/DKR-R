#include "legacy_track_catalog.hpp"
#include "legacy_mod_process.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace dkr::mods {
namespace {
using nlohmann::json;
bool digest(std::string_view value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
std::string path_text(const std::filesystem::path& path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
void ordinary(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_directory(path))throw Error("Mod storage must use ordinary directories.");
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error("Mod storage must not use reparse-point directories.");
#endif
}
void child(const std::filesystem::path& path) {std::filesystem::create_directory(path);ordinary(path);}
std::string field(const json& value,const char* key,std::size_t max=256) {
    auto text=value.at(key).get<std::string>();
    if(text.empty() || text.size()>max || std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32;}))
        throw Error("Prepared catalogue contains invalid text.");
    return text;
}
json read_json(const std::filesystem::path& path,std::size_t limit=MiB) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))throw Error("Prepared metadata is missing or is not an ordinary file.");
    const auto bytes=read_file(path,limit);
    return json::parse(bytes.begin(),bytes.end(),[](int depth,json::parse_event_t,json&) {
        if(depth>12)throw Error("Prepared metadata exceeds its nesting limit.");return true;
    });
}
std::string nonce() {
    std::random_device random;Bytes bytes(32);for(auto& byte:bytes)byte=static_cast<std::uint8_t>(random());return sha256(bytes);
}
void write_json(const std::filesystem::path& path,const json& value) {
    const auto encoded=value.dump();
    if(encoded.size()>MiB)throw Error("Prepared metadata exceeds its size limit.");
    write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()));
}
std::set<std::string> enabled_ids(const std::filesystem::path& root,bool characters=false) {
    const auto path=root/(characters?"character-catalog.json":"track-catalog.json");
    if(!std::filesystem::exists(path))return {};
    const auto value=read_json(path);
    if(value.at("schema")!=1 || !value.at("enabled").is_array() || value.at("enabled").size()>(characters?MaxActiveStageCharacters:512U))
        throw Error("Invalid custom track activation catalogue.");
    std::set<std::string> result;
    for(const auto& entry:value.at("enabled")) {
        const auto id=entry.get<std::string>();
        if(!digest(id) || !result.insert(id).second)throw Error("Invalid or duplicated enabled track identity.");
    }
    return result;
}
void save_enabled(const std::filesystem::path& root,const std::set<std::string>& ids,bool characters=false) {
    const auto destination=root/(characters?"character-catalog.json":"track-catalog.json");
    if(std::filesystem::is_symlink(destination))throw Error("Track catalogue must not be a link.");
    const auto temporary=root/("catalog-"+nonce()+".tmp");
    write_json(temporary,{{"schema",1},{"enabled",ids}});
    try {
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw Error("Could not publish the track activation catalogue.");
#else
        std::filesystem::rename(temporary,destination);
#endif
    } catch(...) {std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
// Presentation metadata is deliberately separate from hashed preparation receipts.
std::filesystem::path metadata_path(const std::filesystem::path& root,bool characters) {
    return root/(characters?"character-library.json":"track-library.json");
}
void replace_json(const std::filesystem::path& destination,const json& value) {
    if(std::filesystem::is_symlink(destination))throw Error("Library metadata must not be a link.");
    const auto temporary=destination.parent_path()/("catalog-"+nonce()+".tmp");
    write_json(temporary,value);
    try {
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw Error("Could not publish library metadata.");
#else
        std::filesystem::rename(temporary,destination);
#endif
    } catch(...) {std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
json metadata(const std::filesystem::path& root,bool characters) {
    const auto path=metadata_path(root,characters);
    if(!std::filesystem::exists(path))return {{"schema",1},{"items",json::object()}};
    auto value=read_json(path);
    if(value.at("schema")!=1 || !value.at("items").is_object() || value.at("items").size()>4096)
        throw Error("Invalid mod library metadata.");
    for(const auto& [id,item]:value.at("items").items()) {
        if(!digest(id) || !item.at("hidden").is_boolean() || !item.at("imported_at").is_number_unsigned())
            throw Error("Invalid mod visibility or import date.");
    }
    return value;
}
bool hidden_id(const json& value,const std::string& id) {
    return value.at("items").contains(id) && value.at("items").at(id).at("hidden").get<bool>();
}
// Never follow links/reparse points while copying or deleting managed trees.
std::uint64_t checked_tree(const std::filesystem::path& path) {
    ordinary(path);std::uint64_t bytes=0;unsigned entries=0;
    const auto inspect=[&](const std::filesystem::path& p) {
        if(std::filesystem::is_symlink(p))throw Error("Managed mod files must not be links.");
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(p.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw Error("Managed mod files must not be reparse points.");
#endif
        if(std::filesystem::is_regular_file(p)) {
            const auto size=std::filesystem::file_size(p);
            if(size>MaxStaged || bytes>std::uint64_t(MaxStaged)*8-size)throw Error("Managed mod tree exceeds its byte budget.");
            bytes+=size;
        } else if(!std::filesystem::is_directory(p))throw Error("Unsupported managed mod file type.");
    };
    inspect(path);
    for(const auto& entry:std::filesystem::recursive_directory_iterator(path)) {
        if(++entries>131072)throw Error("Managed mod tree exceeds its entry budget.");
        inspect(entry.path());
    }
    return bytes;
}
std::filesystem::path journal_path(const std::filesystem::path& root,bool characters) {
    return root/(characters?"character-removal.json":"track-removal.json");
}
std::vector<TrackCatalogItem> group(const std::filesystem::path& folder,const std::string& expected,bool characters=false) {
    ordinary(folder);
    if(!digest(expected) || std::filesystem::is_symlink(folder/"prepared.json"))throw Error("Invalid prepared group identity.");
    const auto bytes=read_file(folder/"prepared.json",MiB);
    if(sha256(bytes)!=expected)throw Error("A prepared catalogue receipt has changed.");
    const auto value=read_json(folder/"prepared.json");
    const auto key=characters?"characters":"tracks";
    if(value.at("schema")!=1 || value.at("adapter")!=(characters?CharacterAdapterVersion:TrackAdapterVersion) || value.at("enabled")!=false ||
       !value.at(key).is_array() || value.at(key).size()>512)
        throw Error("Unsupported prepared catalogue receipt.");
    std::vector<TrackCatalogItem> result;
    for(const auto& entry:value.at(key)) {
        TrackCatalogItem item;
        item.id=field(entry,"id");item.name=field(entry,"name");item.revision=field(entry,"revision");
        item.storage=entry.contains("storage")?field(entry,"storage"):item.id;
        if(!digest(item.storage))throw Error("Invalid prepared storage identity.");
        if(entry.contains("details"))item.details=field(entry,"details",4096);
        item.bank=characters?item.id:field(entry,"bank");item.artifact=field(entry,"artifact");item.patch=field(entry,"patch");item.group=expected;
        if(value.contains("review")) {item.review=field(value,"review");if(!digest(item.review))throw Error("Invalid source review identity.");}
        if(characters) {
            item.base_character=entry.at("behaviour").get<unsigned>();
            if(item.base_character>=10)throw Error("Character base behaviour is invalid.");
        } else {item.carrier=entry.at("carrier").get<unsigned>();item.race_type=entry.at("race_type").get<unsigned>();}
        item.vehicles=entry.at("vehicles").get<unsigned>();
        if(!digest(item.id)||!digest(item.bank)||!digest(item.artifact)||!digest(item.patch)||item.carrier>32767||
           !item.vehicles||item.vehicles>7||item.race_type>255||(item.revision!="us.v77"&&item.revision!="us.v80"))
            throw Error("Prepared course identity or capabilities are invalid.");
        ordinary(folder/item.storage);
        const auto manifest=folder/item.storage/(characters?"character.json":"track.json");
        if(std::filesystem::is_symlink(manifest) || sha256(read_file(manifest,MiB))!=item.artifact)
            throw Error("A prepared course manifest has changed.");
        result.push_back(std::move(item));
    }
    return result;
}
// All replacements are staged before a journal is published. Once published,
// cancellation is deferred and startup/refresh can finish the same transaction.
void recover_removal(const std::filesystem::path& root,bool characters) {
    const auto journal=journal_path(root,characters);
    if(!std::filesystem::exists(journal))return;
    const auto transaction=read_json(journal);const auto id=field(transaction,"id"),job_id=field(transaction,"job");
    if(transaction.at("schema")!=1 || !digest(id) || !digest(job_id))throw Error("Invalid mod removal journal.");
    const auto prepared=root/(characters?"prepared-characters":"prepared");
    const auto jobs=root/"prepare-jobs",job=jobs/job_id;
    ordinary(root);ordinary(prepared);ordinary(jobs);ordinary(job);ordinary(job/"incoming");ordinary(job/"retired");
    for(const auto* key:{"old","new"}) {
        const auto& hashes=transaction.at(key);
        if(!hashes.is_array() || hashes.size()>1024)throw Error("Invalid removal group list.");
        std::set<std::string> seen;
        for(const auto& hash:hashes) {const auto text=hash.get<std::string>();if(!digest(text)||!seen.insert(text).second)throw Error("Invalid removal group identity.");}
    }
    // Validate every source and replacement before doing any further recovery.
    std::set<std::string> expected_survivors,actual_survivors;
    const auto entries_key=characters?"characters":"tracks";
    for(const auto& h:transaction.at("new")) {
        const auto hash=h.get<std::string>();
        const auto path=std::filesystem::exists(prepared/hash)?prepared/hash:job/"incoming"/hash;
        const auto survivors=group(path,hash,characters);checked_tree(path);
        if(std::any_of(survivors.begin(),survivors.end(),[&](const auto& item){return item.id==id;}))throw Error("Removal replacement still contains its target.");
        const auto receipt=read_json(path/"prepared.json");
        for(const auto& entry:receipt.at(entries_key))actual_survivors.insert(entry.dump());
    }
    for(const auto& h:transaction.at("old")) {
        const auto hash=h.get<std::string>();
        if(std::find(transaction.at("new").begin(),transaction.at("new").end(),h)!=transaction.at("new").end())throw Error("Removal groups overlap.");
        const auto path=std::filesystem::exists(prepared/hash)?prepared/hash:job/"retired"/hash;
        const auto old=group(path,hash,characters);checked_tree(path);
        if(std::none_of(old.begin(),old.end(),[&](const auto& item){return item.id==id;}))throw Error("Removal journal references unrelated content.");
        const auto receipt=read_json(path/"prepared.json");
        for(const auto& entry:receipt.at(entries_key))if(entry.at("id")!=id)expected_survivors.insert(entry.dump());
    }
    if(expected_survivors!=actual_survivors)throw Error("Removal journal does not preserve the other installed content.");
    auto enabled=enabled_ids(root,characters);enabled.erase(id);save_enabled(root,enabled,characters);
    for(const auto& h:transaction.at("old")) {
        const auto hash=h.get<std::string>();
        if(std::filesystem::exists(prepared/hash))std::filesystem::rename(prepared/hash,job/"retired"/hash);
    }
    for(const auto& h:transaction.at("new")) {
        const auto hash=h.get<std::string>();
        if(!std::filesystem::exists(prepared/hash))std::filesystem::rename(job/"incoming"/hash,prepared/hash);
    }
    auto info=metadata(root,characters);info["items"].erase(id);replace_json(metadata_path(root,characters),info);
    // Receipt publication is complete. Unlink the journal before garbage
    // collection, so interruption during cleanup cannot leave a broken journal.
    std::filesystem::remove(journal);
    checked_tree(job);
    if(std::filesystem::weakly_canonical(job).parent_path()!=std::filesystem::weakly_canonical(jobs))throw Error("Invalid removal cleanup location.");
    std::filesystem::remove_all(job);
}
void remove_content(const std::filesystem::path& root,const std::string& id,bool characters,std::stop_token stop) {
    const auto prepared=root/(characters?"prepared-characters":"prepared");
    const auto job=root/"prepare-jobs"/nonce();child(job);child(job/"incoming");child(job/"retired");
    bool committed=false;
    try {
        json olds=json::array(),news=json::array();std::set<std::string> replacements;unsigned examined=0;
        for(const auto& folder:std::filesystem::directory_iterator(prepared)) {
            if(++examined>(characters?MaxInstalledCharacterVariants:1024U))throw Error("Prepared directory exceeds its entry limit.");
            if(stop.stop_requested())throw Error("Removal cancelled before commit.");
            const auto hash=path_text(folder.path().filename());if(!digest(hash))continue;
            const auto items=group(folder.path(),hash,characters);
            if(std::none_of(items.begin(),items.end(),[&](const auto& item){return item.id==id;}))continue;
            checked_tree(folder.path());olds.push_back(hash);
            auto receipt=read_json(folder.path()/"prepared.json");const auto key=characters?"characters":"tracks";
            json kept=json::array();std::set<std::string> storage;
            for(const auto& entry:receipt.at(key))if(entry.at("id")!=id) {
                kept.push_back(entry);storage.insert(entry.value("storage",entry.at("id").get<std::string>()));
            }
            if(kept.empty())continue;
            receipt[key]=kept;const auto encoded=receipt.dump();
            const auto next=sha256(View(reinterpret_cast<const std::uint8_t*>(encoded.data()),encoded.size()));
            if(!replacements.insert(next).second)continue;
            news.push_back(next);
            if(std::filesystem::exists(prepared/next)){group(prepared/next,next,characters);continue;}
            const auto destination=job/"incoming"/next;child(destination);write_json(destination/"prepared.json",receipt);
            for(const auto& name:storage) {
                if(!digest(name))throw Error("Invalid survivor storage identity.");
                if(stop.stop_requested())throw Error("Removal cancelled before commit.");
                std::filesystem::copy(folder.path()/name,destination/name,std::filesystem::copy_options::recursive);
            }
            group(destination,next,characters);checked_tree(destination);
        }
        if(olds.empty())throw Error("This mod is no longer installed.");
        if(stop.stop_requested())throw Error("Removal cancelled before commit.");
        replace_json(journal_path(root,characters),{{"schema",1},{"id",id},{"job",path_text(job.filename())},{"old",olds},{"new",news}});
        committed=true;recover_removal(root,characters);
    } catch(...) {
        if(!committed) {try {checked_tree(job);std::filesystem::remove_all(job);}catch(...) {}}
        throw;
    }
}
std::vector<TrackCatalogItem> scan(const std::filesystem::path& root,std::stop_token stop={},bool characters=false,bool presentation=false) {
    std::vector<TrackCatalogItem> result;
    if(!std::filesystem::exists(root))return result;
    ordinary(root);
    if(std::filesystem::exists(journal_path(root,characters)))throw Error("A mod removal needs recovery. Refresh the library before launching.");
    const auto enabled=enabled_ids(root,characters);const auto info=metadata(root,characters);
    const auto prepared=root/(characters?"prepared-characters":"prepared");
    if(!std::filesystem::exists(prepared)) {
        if(!enabled.empty())throw Error("Enabled custom tracks are missing. Restore or disable their content before starting.");
        return result;
    }
    ordinary(prepared);
    std::map<std::pair<std::string,std::string>,std::string> seen;unsigned examined=0;
    for(const auto& entry:std::filesystem::directory_iterator(prepared)) {
        if(stop.stop_requested())throw Error("Track catalogue operation cancelled.");
        if(++examined>(characters?MaxInstalledCharacterVariants:1024U))throw Error("Prepared directory exceeds its entry limit.");
        const auto hash=path_text(entry.path().filename());if(!digest(hash))continue;
        for(auto& item:group(entry.path(),hash,characters)) {
            const auto key=std::make_pair(item.id,item.revision);
            if(auto previous=seen.find(key);previous!=seen.end()) {
                if(previous->second!=item.bank)throw Error("Conflicting prepared banks claim the same course identity.");
                continue;
            }
            seen.emplace(key,item.bank);item.hidden=hidden_id(info,item.id);item.enabled=enabled.contains(item.id) && !item.hidden;
            if(info.at("items").contains(item.id))item.imported_at=info.at("items").at(item.id).at("imported_at").get<std::uint64_t>();
            if(presentation)item.managed_bytes=checked_tree(entry.path()/item.storage);
            result.push_back(std::move(item));
            if(result.size()>(characters?MaxInstalledCharacterVariants:1024U))throw Error("Prepared catalogue exceeds its metadata budget.");
        }
    }
    for(const auto& id:enabled)if(std::none_of(result.begin(),result.end(),[&](const auto& item){return item.id==id;}))
        throw Error("An enabled custom course is missing; it was not silently replaced by a stock track.");
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tie(a.name,a.id,a.revision)<std::tie(b.name,b.id,b.revision);});
    if(presentation) {
        std::map<std::string,json> reviews;
        for(auto& item:result) {
            item.source_name="Imported patch "+item.patch.substr(0,8);
            if(item.review.empty())continue;
            // Optional display metadata never weakens artifact validation.
            try {
                if(!reviews.contains(item.review)) {
                    const auto path=root/"reviews"/item.review/"review.json";
                    const auto bytes=read_file(path,8*MiB);
                    if(sha256(bytes)!=item.review)throw Error("Review identity mismatch.");
                    reviews.emplace(item.review,read_json(path,8*MiB));
                }
                for(const auto& package:reviews.at(item.review).at("packages"))
                    if(package.at("patch_sha256")==item.patch && package.contains("display_name"))
                        item.source_name=field(package,"display_name",256);
            } catch(const std::exception&) { /* Safe fallback label for old/missing reviews. */ }
        }
    }
    return result;
}
void cleanup(const std::filesystem::path& root,const std::filesystem::path& job) noexcept {
    try {
        if(job.parent_path()!=root/"prepare-jobs" || !digest(path_text(job.filename())))return;
        ordinary(root);ordinary(root/"prepare-jobs");ordinary(job);
        if(std::filesystem::weakly_canonical(job).parent_path()!=std::filesystem::weakly_canonical(root/"prepare-jobs"))return;
        std::filesystem::remove_all(job);
    } catch(...) {}
}
}
TrackCatalog::~TrackCatalog(){cancel();if(thread_.joinable())thread_.join();}
void TrackCatalog::configure(std::filesystem::path root,std::filesystem::path worker) {
    cancel();if(thread_.joinable())thread_.join();root_=private_storage_path(root);worker_=std::move(worker);
    {std::lock_guard lock(mutex_);view_={};publish();}refresh();
}
void TrackCatalog::publish(){published_=std::make_shared<TrackCatalogView>(view_);}
std::shared_ptr<const TrackCatalogView> TrackCatalog::snapshot() const{std::lock_guard lock(mutex_);return published_;}
void TrackCatalog::cancel(){if(thread_.joinable())thread_.request_stop();}
void TrackCatalog::dismiss(){std::lock_guard lock(mutex_);if(!view_.busy){view_.modal=false;publish();}}
bool TrackCatalog::refresh(){return start(Action::Scan);}
bool TrackCatalog::prepare_review(std::string id,std::vector<std::filesystem::path> roms){return start(Action::Prepare,std::move(id),std::move(roms));}
bool TrackCatalog::set_enabled(std::string id,bool value){return start(value?Action::Enable:Action::Disable,std::move(id));}
bool TrackCatalog::set_hidden(std::string id,bool value){return start(value?Action::Hide:Action::Restore,std::move(id));}
bool TrackCatalog::remove(std::string id){return start(Action::Remove,std::move(id));}
bool TrackCatalog::disable_all(){return start(Action::DisableAll);}
bool TrackCatalog::start(Action action,std::string id,std::vector<std::filesystem::path> roms) {
    {std::lock_guard lock(mutex_);if(view_.busy)return false;}
    if(thread_.joinable())thread_.join();
    {std::lock_guard lock(mutex_);view_.busy=true;view_.modal=action!=Action::Scan;view_.succeeded=false;
        view_.stage="Preparing custom track catalogue";view_.result.clear();view_.completed=0;view_.total=0;publish();}
    try {
        thread_=std::jthread([this,action,id=std::move(id),roms=std::move(roms)](std::stop_token stop) {
            std::filesystem::path job;
            try {
                const bool characters=kind_==Kind::Character;
                const auto prepared=root_/(characters?"prepared-characters":"prepared");
                std::filesystem::create_directories(root_);ordinary(root_);child(prepared);child(root_/"prepare-jobs");
                recover_removal(root_,characters);
                // Recovery must work even if a previously installed artifact
                // or activation file is damaged. Only the enablement list is
                // replaced; content and all save files remain untouched.
                if(action==Action::DisableAll)save_enabled(root_,{},characters);
                auto items=scan(root_,stop,characters,true);std::string result="Custom content catalogue refreshed.";
                if(action==Action::DisableAll)result="All content in this library disabled. Installed files and saves were retained.";
                if(action==Action::Prepare) {
                    if(!digest(id) || roms.empty() || roms.size()>8)throw Error("Select a saved review and its imported Game Pak first.");
                    ordinary(root_/"reviews");
                    if(!worker_.is_absolute() || !std::filesystem::is_regular_file(worker_))throw Error("The packaged ModWorker is missing.");
                    job=root_/"prepare-jobs"/nonce();
                    if(!std::filesystem::create_directory(job))throw Error("Could not reserve a track preparation job.");
                    json request={{"schema",Schema},{"operation",characters?"prepare-characters":"prepare-tracks"},{"source",path_text(root_/"reviews"/id)},{"roms",json::array()}};
                    for(const auto& path:roms)request["roms"].push_back(path_text(std::filesystem::absolute(path)));
                    write_json(job/"request.json",request);
                    auto next_poll=std::chrono::steady_clock::now();
                    const auto outcome=run_worker(worker_,job/"request.json",[&] {
                        if(stop.stop_requested())return false;
                        const auto now=std::chrono::steady_clock::now();if(now<next_poll)return true;next_poll=now+std::chrono::milliseconds(200);
                        const auto path=job/"progress.jsonl";if(!std::filesystem::exists(path))return true;
                        const auto bytes=read_file(path,128*1024);const std::string text(bytes.begin(),bytes.end());
                        const auto end=text.find_last_of('\n');if(end==std::string::npos)return true;
                        const auto before=end?text.find_last_of('\n',end-1):std::string::npos;
                        const auto begin=before==std::string::npos?0:before+1;const auto progress=json::parse(text.substr(begin,end-begin));
                        const auto stage=field(progress,"stage");const unsigned count=progress.at("count"),patch=progress.at("patch");
                        if(count>32 || patch>count)throw Error("Invalid track preparation progress.");
                        std::lock_guard lock(mutex_);view_.stage=stage;view_.completed=patch;view_.total=count;publish();return true;
                    });
                    if(outcome.outcome==WorkerOutcome::Cancelled || stop.stop_requested())throw Error("Preparation cancelled. Enabled tracks and saves were not changed.");
                    if(outcome.outcome!=WorkerOutcome::Completed) {
                        if(std::filesystem::exists(job/"failure.json"))throw Error(field(read_json(job/"failure.json",8192),"error",1024));
                        throw Error("The isolated track preparation stopped safely or exceeded its time limit.");
                    }
                    const auto receipt=read_file(job/"content"/"prepared.json",MiB);const auto hash=sha256(receipt);
                    const auto incoming=group(job/"content",hash,characters);
                    const auto prepared_receipt=read_json(job/"content"/"prepared.json");
                    std::string blocked_reason;
                    const auto& blocked=prepared_receipt.at("blocked");
                    if(!blocked.is_array() || blocked.size()>1024)throw Error("Invalid preparation failure list.");
                    if(!blocked.empty())blocked_reason=field(blocked.front(),"name")+": "+field(blocked.front(),"reason",1024);
                    if(incoming.empty())throw Error(blocked_reason.empty()?"No content of this type has a supported runtime adapter. Existing content remains unchanged.":blocked_reason);
                    if(items.size()+incoming.size()>1024)throw Error("The prepared catalogue is full.");
                    const auto destination=prepared/hash;
                    if(stop.stop_requested())throw Error("Preparation cancelled before publication.");
                    if(std::filesystem::exists(destination)) {
                        ordinary(destination);if(read_file(destination/"prepared.json",MiB)!=receipt)throw Error("Conflicting prepared catalogue identity.");
                    } else std::filesystem::rename(job/"content",destination);
                    auto info=metadata(root_,characters);
                    const auto imported=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    for(const auto& entry:incoming)if(!info["items"].contains(entry.id)) {
                        const bool existed=std::any_of(items.begin(),items.end(),[&](const auto& old){return old.id==entry.id;});
                        info["items"][entry.id]={{"hidden",false},{"imported_at",existed?std::uint64_t(0):imported}};
                    }
                    replace_json(metadata_path(root_,characters),info);
                    items=scan(root_,{},characters,true);result=std::to_string(incoming.size())+(characters?" character variant(s) prepared. Enable them in Custom Characters.":" course variant(s) prepared. Enable them in Custom Tracks.");
                    if(!blocked_reason.empty())result+=" Some content was not prepared: "+blocked_reason;
                } else if(action==Action::Enable || action==Action::Disable) {
                    if(!digest(id) || std::none_of(items.begin(),items.end(),[&](const auto& item){return item.id==id;}))
                        throw Error("Only a prepared custom course can change activation state.");
                    auto enabled=enabled_ids(root_,characters);
                    if(action==Action::Enable && hidden_id(metadata(root_,characters),id))throw Error("Restore this mod to the library before activating it.");
                    if(action==Action::Enable)enabled.insert(id);else enabled.erase(id);
                    if(!characters && enabled.size()>512)throw Error("The enabled course catalogue is full.");
                    if(characters && enabled.size()>MaxActiveStageCharacters)throw Error("The character library exceeds the native presentation identity range. Installed characters remain available.");
                    if(stop.stop_requested())throw Error("Activation change cancelled.");
                    save_enabled(root_,enabled,characters);items=scan(root_,{},characters,true);
                    result=action==Action::Enable?"Custom content enabled for the next game session.":"Custom content disabled. Its files and records were retained.";
                } else if(action==Action::Hide || action==Action::Restore || action==Action::Remove) {
                    if(!digest(id) || std::none_of(items.begin(),items.end(),[&](const auto& item){return item.id==id;}))
                        throw Error("Only installed content can be managed.");
                    if(stop.stop_requested())throw Error("Library change cancelled.");
                    if(action==Action::Remove) {
                        {std::lock_guard lock(mutex_);view_.stage="Removing selected mod; preserving other content and saves";publish();}
                        remove_content(root_,id,characters,stop);
                        result="Selected mod and its prepared revision variants removed. Other mods, original files, retained import material and all saves remain untouched.";
                    } else {
                        auto info=metadata(root_,characters);
                        if(!info["items"].contains(id))info["items"][id]={{"hidden",false},{"imported_at",std::uint64_t(0)}};
                        // Disable first. A failed metadata write can leave it
                        // visible/inactive, never secretly active and hidden.
                        if(action==Action::Hide) {auto enabled=enabled_ids(root_,characters);enabled.erase(id);save_enabled(root_,enabled,characters);}
                        info["items"][id]["hidden"]=action==Action::Hide;replace_json(metadata_path(root_,characters),info);
                        result=action==Action::Hide?"Mod deactivated and hidden. Files and saves retained.":"Mod restored to the library. It remains inactive.";
                    }
                    items=scan(root_,{},characters,true);
                }
                cleanup(root_,job);
                std::lock_guard lock(mutex_);view_.tracks=std::move(items);view_.result=std::move(result);view_.succeeded=true;view_.busy=false;publish();
            } catch(const std::exception& error) {
                cleanup(root_,job);std::lock_guard lock(mutex_);view_.result=std::string(error.what()).substr(0,1024);view_.busy=false;view_.succeeded=false;publish();
            } catch(...) {
                cleanup(root_,job);std::lock_guard lock(mutex_);view_.result="Custom content operation stopped safely after an unexpected error.";view_.busy=false;view_.succeeded=false;publish();
            }
        });
    } catch(const std::exception& error) {
        std::lock_guard lock(mutex_);view_.busy=false;view_.result=error.what();publish();return false;
    }
    return true;
}
bool TrackCatalog::has_enabled(const std::filesystem::path& path) {
    const auto root=private_storage_path(path);
    if(!std::filesystem::exists(root))return false;
    ordinary(root);
    return !enabled_ids(root).empty() || !enabled_ids(root,true).empty();
}
void TrackCatalog::validate_retained_merge(const std::filesystem::path& path,
    const std::filesystem::path& input) {
    const auto root=private_storage_path(path),incoming=private_storage_path(input);
    for(const bool characters:{false,true}) {
        if(std::filesystem::exists(journal_path(root,characters)))
            throw Error("An offline mod removal is pending. Refresh the library before keeping downloaded mods.");
        const auto installed=scan(root,{},characters),added=scan(incoming,{},characters);
        std::set<std::string> groups;
        for(const auto& library:{root,incoming}) {
            const auto folder=library/(characters?"prepared-characters":"prepared");
            if(std::filesystem::exists(folder))for(const auto& entry:std::filesystem::directory_iterator(folder)) {
                const auto hash=path_text(entry.path().filename());if(digest(hash))groups.insert(hash);
            }
        }
        if(groups.size()>(characters?MaxInstalledCharacterVariants:1024U))
            throw Error("Keeping these groups would exceed the offline catalogue's storage limit.");
        std::map<std::pair<std::string,std::string>,TrackCatalogItem> items;
        for(const auto& item:installed)items.emplace(std::make_pair(item.id,item.revision),item);
        for(const auto& item:added) {
            const auto [at,inserted]=items.emplace(std::make_pair(item.id,item.revision),item);
            if(!inserted && (at->second.bank!=item.bank || at->second.artifact!=item.artifact ||
                at->second.patch!=item.patch))
                throw Error("A downloaded mod conflicts with an installed offline identity. Existing files were not replaced.");
        }
        if(items.size()>(characters?MaxInstalledCharacterVariants:1024U))
            throw Error("The offline mod library is full. The online session can still use its verified cache.");
    }
}
std::vector<PreparedTrack> TrackCatalog::load_enabled(const std::filesystem::path& path,std::shared_ptr<const AssetBank> stock) {
    const auto root=private_storage_path(path);
    if(!stock || !stock->digest().empty())throw Error("Enabled custom tracks require a verified original bank.");
    if(!std::filesystem::exists(root) || enabled_ids(root).empty())return {};
    const auto items=scan(root);std::vector<PreparedTrack> result;std::set<std::string> expected,loaded;
    std::size_t total=0;
    for(const auto& item:items)if(item.enabled) {
        expected.insert(item.id);if(item.revision!=stock->revision())continue;
        auto track=read_track_artifact(root/"prepared"/item.group/item.storage,item.artifact,stock);
        if(track.bank->owned_override_bytes()>MaxStaged-total)
            throw Error("Enabled course assets exceed this beta's 256 MiB preparation budget. Disable some tracks before starting.");
        total+=track.bank->owned_override_bytes();
        if(track.root.content_id!=item.id || track.bank->fingerprint()!=item.bank || track.patch_digest!=item.patch ||
           track.root.carrier!=item.carrier || track.root.name!=item.name || track.root.vehicles!=item.vehicles || track.root.race_type!=item.race_type)
            throw Error("Prepared course does not match its catalogue entry.");
        loaded.insert(item.id);result.push_back(std::move(track));
    }
    if(expected!=loaded)throw Error("An enabled course is not prepared for the selected Game Pak revision. Prepare that revision or disable the course before starting.");
    return result;
}
std::vector<TrackCatalogItem> TrackCatalog::selected_items(const std::filesystem::path& path,
    std::string_view revision,Kind kind) {
    if(revision!="us.v77" && revision!="us.v80")throw Error("Unsupported online Game Pak revision.");
    const auto items=scan(private_storage_path(path),{},kind==Kind::Character);
    std::set<std::string> expected,found;std::vector<TrackCatalogItem> result;
    for(const auto& item:items)if(item.enabled) {
        expected.insert(item.id);
        // Character adapter assets are revision-independent; the existing
        // character loader resolves their references against the lobby's stock
        // bank. Course banks, in contrast, require the exact runtime revision.
        if(item.revision==revision || kind==Kind::Character){
            const auto previous=std::find_if(result.begin(),result.end(),[&](const auto& p){return p.id==item.id;});
            if(previous!=result.end()) {
                if(previous->artifact!=item.artifact || previous->patch!=item.patch || previous->bank!=item.bank)
                    throw Error("Conflicting prepared variants claim the same online mod identity.");
            } else result.push_back(item);
            found.insert(item.id);
        }
    }
    if(expected!=found)throw Error("An enabled mod is not prepared for the lobby's Game Pak revision.");
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
std::vector<PreparedCharacter> TrackCatalog::load_enabled_characters(const std::filesystem::path& path,std::shared_ptr<const AssetBank> stock) {
    const auto root=private_storage_path(path);
    if(!stock || !stock->digest().empty() || stock->augmented())throw Error("Characters need the verified original Game Pak.");
    if(!std::filesystem::exists(root) || enabled_ids(root,true).empty())return {};
    const auto items=scan(root,{},true);std::vector<PreparedCharacter> result;
    for(const auto& item:items)if(item.enabled) {
        auto character=read_character_artifact(root/"prepared-characters"/item.group/item.storage,item.artifact,*stock);
        if(character.root.content_id!=item.id || character.root.name!=item.name || character.root.base_character!=item.base_character || item.vehicles!=7)
            throw Error("Character artifact disagrees with its catalogue metadata.");
        // Adapter v2 retained the reviewed source audio but persisted only its
        // three selection cues. Extend it in memory, on the launch worker, so
        // existing installed IDs and their isolated saves remain unchanged.
        if(!item.review.empty()) {
            const auto source=root/"reviews"/item.review;
            ordinary(source);ordinary(source/"blobs");
            if(std::filesystem::is_symlink(source/"review.json") || !std::filesystem::is_regular_file(source/"review.json"))
                throw Error("Character source review is missing or linked.");
            const auto bytes=read_file(source/"review.json",8*MiB);
            if(sha256(bytes)!=item.review)throw Error("Character source review has changed.");
            const auto review=json::parse(bytes.begin(),bytes.end(),[](int depth,json::parse_event_t,json&) {
                if(depth>12)throw Error("Character source review exceeds its nesting limit.");return true;
            }); // Parse the exact bytes whose digest was checked, not a second read.
            if(review.at("schema")!=Schema || !review.at("packages").is_array() || review.at("packages").size()>32)
                throw Error("Invalid retained character source review.");
            const json* package=nullptr;
            for(const auto& p:review.at("packages"))if(p.at("patch_sha256")==item.patch && p.at("source_revision")==item.revision) {
                if(package)throw Error("Ambiguous character source audio.");package=&p;
            }
            if(!package)throw Error("The retained character audio does not match this import.");
            std::array<Bytes,3> audio;
            constexpr unsigned ids[]{2,3,7};
            // US 1.0 and 1.1 share the original control/sample banks. The two
            // table differences are priorities at IDs 80 and 587, outside all
            // character cues. Pin this portability fact rather than assume it.
            constexpr const char* hashes[]{"66704c2e46f4aa3a22f8f27b3ab979de79d95b688028f73ed5ca5787180b5bf7",
                "4ddba4c3092aa4e226b2d045144df0e5876aef65ada01689b5f4d1a16bb563b4"};
            const auto table_hash=stock->revision()=="us.v77"?"3f49a943f552fbbc80c4639c6459f5f76388db7b29332ac1d8aab99769dc2446":
                "346a420ba543af1f562db5dbc1b3bba169f382764ea17f9d9fbcc3e18975fba6";
            for(unsigned i=0;i<3;++i) {
                const auto original=stock->record(39,ids[i]);
                if(sha256(original)!=(i<2?hashes[i]:table_hash))throw Error("Original character audio portability check failed.");
                audio[i]=Bytes(original.begin(),original.end());
            }
            const auto& records=package->at("records");
            if(!records.is_array() || records.size()>65536)throw Error("Character source audio record list is invalid.");
            std::set<unsigned> seen_audio;
            for(const auto& record:records)if(record.at("section")==39)for(unsigned i=0;i<3;++i)if(record.at("record")==ids[i]) {
                const auto hash=field(record,"sha256");
                if(!digest(hash) || !seen_audio.insert(ids[i]).second)throw Error("Ambiguous character audio record.");
                const auto file=source/"blobs"/hash;
                if(std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file))throw Error("Character audio blob is missing or linked.");
                audio[i]=read_file(file,i==1?16*MiB:4*MiB);
                if(audio[i].size()!=record.at("size").get<std::size_t>() || sha256(audio[i])!=hash)
                    throw Error("Character audio blob verification failed.");
            }
            const auto selected=prepare_character_audio(audio[0],audio[1],audio[2],item.base_character);
            if(character_audio_identity(selected)!=character_audio_identity(character.audio))
                throw Error("Retained race audio belongs to a different character import.");
            character.race_audio=prepare_character_race_audio(audio[0],audio[1],audio[2],item.base_character);
        }
        result.push_back(std::move(character));
        if(result.size()>MaxActiveStageCharacters)throw Error("The character library exceeds the native presentation identity range.");
    }
    return result;
}
}
