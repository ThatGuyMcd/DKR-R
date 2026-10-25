#include "../shared_saves.hpp"
#include "legacy_mod_launch.hpp"
#include "vehicle_binding.hpp"
#include "legacy_track_catalog.hpp"
#include "../dkr_save_codec.hpp"
#include "../custom_tracks.hpp"
#include "../rom_revision.hpp"
#include "magic_code_game_world.hpp"
#include "online_game_pak.hpp"
#include <algorithm>

namespace dkr::mods {
namespace {
void ordinary(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_directory(path))
        throw Error("The modded-save path must contain ordinary directories, not links.");
}
void child(const std::filesystem::path& path) {std::filesystem::create_directory(path);ordinary(path);}
void regular(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw Error("The Adventure save must be a regular file, not a link.");
}
}
AssetBank::Overrides publish_dkrmap_artwork(std::shared_ptr<const AssetBank> boot) {
    if(!boot)throw Error("dkrmap artwork requires a verified boot bank.");
    const auto directory=AssetDirectory::build(boot);
    namespace authored=dkr::runtime::custom_tracks;
    AssetBank::Overrides artwork;
    for(const auto section:{authored::Section::Textures3D,authored::Section::Textures2D,authored::Section::Sprites}) {
        const unsigned data=section==authored::Section::Textures3D?2:section==authored::Section::Textures2D?4:12;
        const auto count=boot->record_count(data);
        const auto table_bytes=directory->read(data+1,0,directory->section_size(data+1));
        std::vector<std::int32_t> table;
        for(std::size_t at=0;at+4<=table_bytes.size();at+=4) {
            table.push_back(static_cast<std::int32_t>(be32(table_bytes,at)));
            if(table.back()==-1)break;
        }
        if(table.size()<2 || table.back()!=-1 || table.size()-2!=count)
            throw Error("Boot artwork table does not describe its records.");
        // Exact lengths also cover large character/portrait namespaces; never
        // truncate them at the old 4096-word retail scan limit.
        const auto extended=authored::build_extended_table(section,table.data(),table.size());
        for(std::size_t id=count;id+2<extended.size();++id) {
            const auto size=extended[id+1]-extended[id];
            const auto bytes=authored::payload_for(section,extended[id],size);
            if(!bytes || size<=0)throw Error("Shared track artwork lost its published payload.");
            artwork.emplace(AssetKey{data,static_cast<unsigned>(id)},Bytes(bytes,bytes+size));
        }
    }
    return artwork;
}
std::shared_ptr<const PreparedModLaunch> prepare_mod_launch(
    const std::filesystem::path& config,const std::filesystem::path& rom,bool online,
    const LaunchProgress& progress,std::stop_token stop) {
    auto out=std::make_shared<PreparedModLaunch>();
    // The offline catalog is never consulted for online launches. The lobby's
    // separately verified, frozen RuntimeResources supply online assets.
    if(online)return out;
    std::string personal_error;
    if(!dkr::runtime::saves::shared::prepare(config,personal_error))throw Error(personal_error);
    auto report=[&](const char* message){if(stop.stop_requested())throw Error("Game launch cancelled.");if(progress)progress(message);};
    const auto code_root=config/"mods"/"custom-magic-codes";
    if(std::filesystem::exists(code_root/"library.json")) {
        report("Preparing Custom Magic Codes");
        const auto revision=dkr::runtime::rom::inspect(rom).revision;
        code::Library library(code_root,DKR_RELEASE_VERSION);
        auto frozen=std::make_shared<code::FrozenSet>(library.freeze(online::asset_revision(revision),false,
            code::GameWorld::policy().modules,stop));
        if(!frozen->ordered.empty())out->code=std::move(frozen);
    }
    auto finish=[&]() -> std::shared_ptr<const PreparedModLaunch> {
        if(out->code) {
            if(std::any_of(out->code->ordered.begin(),out->code->ordered.end(),[](const auto& p){return (p->api==2&&p->modules.contains("catalogues2"))||p->modules.contains("trophies")||!p->tracks.empty();})) {
                std::vector<std::filesystem::path> labs;
                for(const auto& track:dkr::runtime::custom_tracks::tracks())if(track.enabled)labs.push_back(track.source);
                // Header IDs are not published until the retail boot later.
                // Derive them now in the SAME enabled-track scan order that
                // offline table assembly uses, not online's canonical order
                // and not a stale/empty previous-session global ID map.
                code::Json sdk2_inventory;
                auto courses=code::trophy::installed_catalogue(rom,config/"mods"/"legacy",labs,stop,false,&sdk2_inventory);
                auto rebound=std::make_shared<code::FrozenSet>(*out->code);
                code::trophy::rebind(*rebound,courses,online::asset_revision(dkr::runtime::rom::inspect(rom).revision),sdk2_inventory);out->code=std::move(rebound);
            }
            const auto identity="dkr-offline-code-session-1\n"+out->fingerprint+"\n"+out->code->identity;
            out->fingerprint=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
        }
        out->save_subfolder.clear();
        out->save_path=config/"saves"/"dkr.us.v77.bin";
        out->pak_directory=config;
        std::string save_error;
        if(!dkr::runtime::saves::shared::prepare(config,save_error))throw Error(save_error);
        report("Custom mods ready");return out;
    };
    const auto root=config/"mods"/"legacy";
    report("Preparing custom cars");
    auto cars=vehicles::Library(config/"mods"/"vehicles").freeze(stop);
    if(!TrackCatalog::has_enabled(root)&&cars.packages.empty())return finish();
    report("Validating the original Game Pak");
    auto bytes=read_file(rom,MaxImage);canonicalize_rom(bytes);
    const auto stock=AssetBank::stock(std::move(bytes));
    report("Checking enabled character assets");
    auto characters=TrackCatalog::load_enabled_characters(root,stock);
    if(characters.size()>MaxActiveStageCharacters)throw Error("The character library exceeds the native presentation identity range.");
    std::shared_ptr<const CharacterNamespace> names;
    if(!characters.empty())names=std::make_shared<const CharacterNamespace>(allocate_characters(stock,std::move(characters)));
    report("Checking enabled custom courses");
    auto tracks=TrackCatalog::load_enabled(root,stock);
    report("Preparing the isolated game asset session");
    // Character IDs are allocated first. Publish dkrmap IDs against that
    // namespace, then retain the same immutable artwork in every scene bank.
    auto artwork=std::make_shared<const AssetBank::Overrides>(publish_dkrmap_artwork(names?names->apply(stock):stock));
    auto vehicle_resources=vehicles::prepare_resources(stock,
        AssetBank::append_artwork(names?names->apply(stock):stock,*artwork),names,std::move(cars));
    auto session=std::make_shared<RuntimeSession>(stock,names,std::vector<Bytes>{},
        [artwork,vehicle_resources](std::shared_ptr<const AssetBank> bank){
            return vehicle_resources->apply(AssetBank::append_artwork(std::move(bank),*artwork));
        });
    vehicles::bind_resources(session,vehicle_resources);
    std::vector<std::string> identities;
    if(!vehicle_resources->library.packages.empty())identities.push_back("vehicles:"+vehicle_resources->identity);
    for(auto& track:tracks) {
        report("Preparing custom Track Select previews");
        identities.push_back("track:"+track.root.content_id+":"+track.bank->fingerprint());
        session->admit(std::move(track));
    }
    if(names)for(const auto& character:names->characters)identities.push_back("character:"+character.id);
    // dkrmap artwork stays out of this identity: it names the modded save
    // folder, and a track re-export must not strand the player's progress.
    // The scene banks already fingerprint the artwork for cache ownership.
    if(identities.empty())throw Error("Enabled mod content did not produce a playable library.");
    std::sort(identities.begin(),identities.end());
    std::string identity="dkr-offline-mod-session-1\n"+stock->fingerprint()+"\n";
    for(const auto& part:identities)identity+=part+"\n";
    out->fingerprint=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    out->session=std::move(session);return finish();
}
ModLaunch::~ModLaunch(){cancel();if(thread_.joinable())thread_.join();}
ModLaunchView ModLaunch::snapshot()const{std::lock_guard lock(mutex_);return state_;}
void ModLaunch::cancel(){
    if(thread_.joinable())thread_.request_stop();
    // Cancellation must win even if preparation completed just before the
    // button was pressed. A hidden/abandoned result can never auto-launch.
    std::lock_guard lock(mutex_);state_.modal=false;state_.prepared.reset();
}
void ModLaunch::abandon(){cancel();}
void ModLaunch::dismiss(){std::lock_guard lock(mutex_);if(!state_.busy){state_.modal=false;state_.prepared.reset();}}
void ModLaunch::report_error(std::string error){std::lock_guard lock(mutex_);if(!state_.busy){state_={};state_.modal=true;state_.error=std::move(error);}}
bool ModLaunch::start(std::filesystem::path config,std::filesystem::path rom,bool online) {
    {std::lock_guard lock(mutex_);if(state_.busy)return false;}
    if(thread_.joinable())thread_.join();
    {std::lock_guard lock(mutex_);state_={};state_.busy=true;state_.modal=true;state_.stage="Checking custom mods";}
    try {
        thread_=std::jthread([this,config=std::move(config),rom=std::move(rom),online](std::stop_token stop) {
            try {
                auto prepared=prepare_mod_launch(config,rom,online,[&](const char* stage){std::lock_guard lock(mutex_);state_.stage=stage;},stop);
                std::lock_guard lock(mutex_);
                if(state_.modal && !stop.stop_requested())state_.prepared=std::move(prepared);
                state_.busy=false;
            }catch(const std::exception& error){std::lock_guard lock(mutex_);state_.busy=false;state_.error=error.what();}
             catch(...){std::lock_guard lock(mutex_);state_.busy=false;state_.error="Custom mod preparation stopped safely after an unexpected error.";}
        });
    }catch(const std::exception& error){std::lock_guard lock(mutex_);state_.busy=false;state_.error=error.what();return false;}
    return true;
}
}
