#include "online_mod_profile.hpp"
#include "online_mod_prepare.hpp"
#include "online_mod_runtime.hpp"
#include "online_game_pak.hpp"
#include "legacy_track_catalog.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>
using namespace dkr::mods;
using namespace dkr::mods::online;
namespace {
unsigned checks=0;
void check(bool value){if(!value)throw Error("Profile check "+std::to_string(checks));++checks;}
void write(const std::filesystem::path& path,const nlohmann::json& doc){const auto text=doc.dump();write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected);}
}
int main(int argc,char** argv) {
    std::filesystem::path root;
    try {
        if(argc==6 && std::string_view(argv[1])=="--v80-from-prepared") {
            // Reuse only the private source library's retained reviews. Still
            // reconstruct every selected course/character in a fresh profile.
            root=std::filesystem::absolute(std::filesystem::current_path()/("online-profile-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
            check(std::filesystem::create_directory(root));
            std::filesystem::create_directory(root/"payloads");
            std::filesystem::create_directory(root/"online");
            const auto bundle=export_host(utf8_path(argv[2]),{},"us.v80",true);
            check(!bundle.manifest.content.empty());
            for(const auto& [id,data]:bundle.payloads)write_new_file(root/"payloads"/id,*data);
            const std::vector<std::filesystem::path> roms{utf8_path(argv[3]),utf8_path(argv[4])};
            const auto profile=prepare_profile(bundle.manifest,root/"payloads",roms,root/"online",utf8_path(argv[5]),
                {},[](auto stage){std::cout<<stage<<'\n';});
            validate_profile(*profile);++checks;
            const auto ready=with_matching_game_pak(roms,profile->manifest.revision,{},
                [](const auto& path){return dkr::runtime::rom::inspect(path);},
                [&](const auto& path){return RuntimeResources::prepare(profile,path);});
            auto a=ready->new_session(),b=ready->new_session();
            check(a!=b && a->checkpoint()==b->checkpoint());
            check(a->characters()->characters.size()==std::count_if(bundle.manifest.content.begin(),bundle.manifest.content.end(),
                [](const auto& item){return item.kind==Kind::Character;}));
            check(a->tracks().size()==std::count_if(bundle.manifest.content.begin(),bundle.manifest.content.end(),
                [](const auto& item){return item.kind==Kind::Track;}));
            std::filesystem::remove_all(private_storage_path(root));
            std::cout<<"us.v80: "<<bundle.manifest.content.size()<<" complete selected roots reconstructed and runtime-prepared.\n";return 0;
        }
        if((argc==5 || argc==6) && (std::string_view(argv[1])=="--cached-runtime" || std::string_view(argv[1])=="--retain-cached" || std::string_view(argv[1])=="--validate-kept")) {
            auto profile=std::make_shared<Profile>();const auto rom=utf8_path(argv[3]);
            const auto pak=dkr::runtime::rom::inspect(rom);
            profile->manifest=export_host(utf8_path(argv[4]),{},std::string(asset_revision(pak.revision)),true).manifest;
            profile->digest=identity(profile->manifest);profile->directory=utf8_path(argv[2]);
            validate_profile(*profile);std::cerr<<"Cached profile verified; preparing actual runtime resources.\n";
            if(std::string_view(argv[1])!="--cached-runtime") {
                const bool validate_only=std::string_view(argv[1])=="--validate-kept";
                if(validate_only && argc!=6)throw Error("Supply the private kept fixture to validate.");
                root=validate_only?utf8_path(argv[5]):std::filesystem::absolute(std::filesystem::current_path()/("offline-keep-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
                if(!validate_only){std::filesystem::create_directory(root);retain_profile(*profile,root/"legacy",root/"tracks");}
                check(TrackCatalog::selected_items(root/"legacy",profile->manifest.revision,TrackCatalog::Kind::Track).empty());
                check(TrackCatalog::selected_items(root/"legacy",profile->manifest.revision,TrackCatalog::Kind::Character).empty());
                const auto reviews=std::distance(std::filesystem::directory_iterator(root/"legacy"/"reviews"),std::filesystem::directory_iterator{});
                check(reviews==profile->manifest.payloads.size());
                if(!validate_only)retain_profile(*profile,root/"legacy",root/"tracks");
                check(reviews==std::distance(std::filesystem::directory_iterator(root/"legacy"/"reviews"),std::filesystem::directory_iterator{}));
                TrackCatalog characters(TrackCatalog::Kind::Character),tracks;characters.configure(root/"legacy",{});tracks.configure(root/"legacy",{});
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
                while((characters.snapshot()->busy || tracks.snapshot()->busy) && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                check(!characters.snapshot()->busy && !tracks.snapshot()->busy);
                check(characters.snapshot()->succeeded && tracks.snapshot()->succeeded);
                // A reviewed xdelta can contain additional, host-disabled
                // roots and both revision variants. The whole reviewed patch
                // is retained, but every new root must stay inactive.
                for(const auto* catalog:{characters.snapshot().get(),tracks.snapshot().get()})
                    for(const auto& item:catalog->tracks)check(!item.enabled && !item.hidden);
                for(const auto& content:profile->manifest.content) {
                    const auto catalog=content.kind==Kind::Character?characters.snapshot():tracks.snapshot();
                    check(std::any_of(catalog->tracks.begin(),catalog->tracks.end(),[&](const auto& item){
                        return item.id==content.id && item.bank==content.bank && item.artifact==content.artifact;}));
                }
                std::cout<<profile->manifest.content.size()<<" required production roots visible and inactive; "<<reviews<<" reviewed patches retained and deduplicated.\n";
                if(!validate_only)std::filesystem::remove_all(private_storage_path(root));return 0;
            }
            const auto resources=RuntimeResources::prepare(profile,rom);
            std::cerr<<"Resources ready; creating independent worlds.\n";
            auto a=resources->new_session(),b=resources->new_session();check(a!=b && a->checkpoint()==b->checkpoint());
            std::cout<<"Cached production mod runtime verified.\n";return 0;
        }
        // Match the launcher: it passes a normal config path, not \\?\.
        // Production boundaries must handle their own deep staging paths.
        root=std::filesystem::absolute(std::filesystem::current_path()/("online-profile-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
        check(std::filesystem::create_directory(root));std::filesystem::create_directory(root/"payloads");
        const auto lab=root/utf8_path("course-\xE2\x98\x83.dkrmap");std::filesystem::create_directory(lab);
        std::filesystem::create_directory(lab/"headers");
        write(lab/"manifest.json",{{"schemaVersion",1},{"id","online-test"},{"name","Online Test"},
            {"adds",nlohmann::json::array({{{"section","LEVEL_HEADERS"},{"file","headers/header.bin"}}})}});
        Bytes header(200,0);header[0]=6;header[0x4E]=1;header[0x37]=73;header[0xBB]=5;write_new_file(lab/"headers"/"header.bin",header);
        const auto lab_before=sha256(pack_track_lab(lab));
        const auto fixture_revision=argc>=3
            ?std::string(asset_revision(dkr::runtime::rom::inspect(utf8_path(argv[1])).revision))
            :std::string("us.v77");
        auto bundle=export_host(root/"missing-legacy",{lab},fixture_revision,true);
        check(bundle.manifest.content.size()==1 && bundle.manifest.payloads.size()==1);
        for(const auto& [id,data]:bundle.payloads)write_new_file(root/"payloads"/id,*data);
        check(lab_before==sha256(pack_track_lab(lab)));
        if(argc>=3) {
            const auto rom=utf8_path(argv[1]),worker=utf8_path(argv[2]);
            std::vector<std::filesystem::path> roms{rom};if(argc>=4)roms.push_back(utf8_path(argv[3]));
            const auto online=root/"online";std::filesystem::create_directory(online);
            auto profile=prepare_profile(bundle.manifest,root/"payloads",roms,online,worker);
            check(profile->digest==identity(bundle.manifest));validate_profile(*profile);++checks;
#ifdef _WIN32
            check(!root.native().starts_with(L"\\\\?\\"));
            check(profile->directory.native().starts_with(L"\\\\?\\"));
#endif
            auto again=prepare_profile(bundle.manifest,root/"payloads",roms,online,worker);check(again->directory==profile->directory);
            const auto resources=with_matching_game_pak(roms,profile->manifest.revision,{},
                [](const auto& path){return dkr::runtime::rom::inspect(path);},
                [&](const auto& path){return RuntimeResources::prepare(profile,path);});
            auto first=resources->new_session(),second=resources->new_session();
            check(first!=second && first->checkpoint()==second->checkpoint());
            check(resources->authored()->courses.size()==1 && resources->authored()->courses[0].level_id<128);
            const auto level=static_cast<unsigned>(resources->authored()->courses[0].level_id);
            check(first->boot_bank()->record(23,level)[0]==1);
            check(first->boot_bank()->appended_courses() && first->boot_bank()->record_count(23)==level+1);
            check(dkr::runtime::custom_tracks::tracks().empty()); // pure preparation never scans/publishes the offline list
            const auto offline=root/"offline",offline_labs=root/"offline-labs";
            std::filesystem::create_directory(offline);
            write(offline/"track-catalog.json",{{"schema",1},{"enabled",nlohmann::json::array()}});
            write(offline/"character-catalog.json",{{"schema",1},{"enabled",nlohmann::json::array()}});
            write_new_file(offline/"offline-save-sentinel",Bytes{1,2,3,4});
            const auto state_before=read_file(offline/"track-catalog.json",MiB);
            retain_profile(*profile,offline,offline_labs);
            check(read_file(offline/"track-catalog.json",MiB)==state_before);
            check(read_file(offline/"offline-save-sentinel",16)==Bytes({1,2,3,4}));
            dkr::runtime::custom_tracks::scan(offline_labs);
            auto kept=dkr::runtime::custom_tracks::tracks();
            check(kept.size()==1 && kept[0].kept_online && !kept[0].enabled);
            check(lab_before==sha256(pack_track_lab(kept[0].source)));
            std::string activation_error;
            check(dkr::runtime::custom_tracks::set_kept_enabled(kept[0].id,true,activation_error));
            dkr::runtime::custom_tracks::scan(offline_labs);
            check(dkr::runtime::custom_tracks::tracks()[0].enabled);
            retain_profile(*profile,offline,offline_labs); // Reuse without resetting an existing user's activation.
            dkr::runtime::custom_tracks::scan(offline_labs);
            check(dkr::runtime::custom_tracks::tracks().size()==1 && dkr::runtime::custom_tracks::tracks()[0].enabled);
            check(dkr::runtime::custom_tracks::set_kept_enabled(kept[0].id,false,activation_error));
            dkr::runtime::custom_tracks::scan(offline_labs);
            check(!dkr::runtime::custom_tracks::tracks()[0].enabled);
            auto unverified=*profile;unverified.digest=std::string(64,'a');
            rejects([&]{retain_profile(unverified,root/"must-not-publish",root/"must-not-publish-tracks");});
            check(!std::filesystem::exists(root/"must-not-publish"));
            std::stop_source cancelled;cancelled.request_stop();
            rejects([&]{retain_profile(*profile,root/"cancelled",root/"cancelled-tracks",cancelled.get_token());});
            check(!std::filesystem::exists(root/"cancelled"));
            dkr::runtime::custom_tracks::scan(root/"empty-library");
            check(!std::filesystem::exists(root/"missing-legacy") && lab_before==sha256(pack_track_lab(lab)));
            auto altered=*profile;altered.manifest.custom_ai=false;rejects([&]{validate_profile(altered);});
            if(argc>=5) {
                const auto library=utf8_path(argv[4]);
                const auto track_state=sha256(read_file(library/"track-catalog.json",MiB));
                const auto char_state=sha256(read_file(library/"character-catalog.json",MiB));
                std::filesystem::path character_host;
                for(const auto* revision:{"us.v77","us.v80"}) {
                    // The existing offline library need not contain prepared
                    // v80 course variants. Never silently use v77 banks there.
                    // Qualify the revision-independent characters separately.
                    auto host=export_host(character_host.empty()?library:character_host,{},revision,true);
                    check(!host.manifest.content.empty());std::uint64_t bytes=0;
                    for(const auto& [id,data]:host.payloads) {
                        bytes+=data->size();check(sha256(*data)==id);
                        if(!std::filesystem::exists(root/"payloads"/id))write_new_file(root/"payloads"/id,*data);
                    }
                    check(bytes==transfer_size(host.manifest));
                    auto prepared=prepare_profile(host.manifest,root/"payloads",roms,online,worker,
                        {},[](auto stage){std::cout<<stage<<'\n';});
                    validate_profile(*prepared);++checks;
                    const auto kept_root=root/(std::string("kept-")+revision);
                    retain_profile(*prepared,kept_root,root/(std::string("kept-labs-")+revision));
                    check(TrackCatalog::selected_items(kept_root,revision,TrackCatalog::Kind::Track).empty());
                    check(TrackCatalog::selected_items(kept_root,revision,TrackCatalog::Kind::Character).empty());
                    check(std::filesystem::directory_iterator(kept_root/"reviews")!=std::filesystem::directory_iterator{});
                    retain_profile(*prepared,kept_root,root/(std::string("kept-labs-")+revision));++checks;
                    const auto match_rom=std::find_if(roms.begin(),roms.end(),[&](const auto& r){
                        auto bytes=read_file(r,MaxImage);canonicalize_rom(bytes);return verified_revision(bytes)==revision;});
                    check(match_rom!=roms.end());
                    const auto ready=with_matching_game_pak(roms,prepared->manifest.revision,{},
                        [](const auto& path){return dkr::runtime::rom::inspect(path);},
                        [&](const auto& path){return RuntimeResources::prepare(prepared,path);});
                    auto host_world=ready->new_session(),client_world=ready->new_session();
                    check(host_world!=client_world && host_world->checkpoint()==client_world->checkpoint());
                    check(host_world->characters() && host_world->characters()->characters.size()==
                        std::count_if(host.manifest.content.begin(),host.manifest.content.end(),[](const auto& item){return item.kind==Kind::Character;}));
                    check(host_world->tracks().size()==std::count_if(host.manifest.content.begin(),host.manifest.content.end(),[](const auto& c){return c.kind==Kind::Track;}));
                    std::cout<<revision<<": "<<host.manifest.content.size()<<" selected roots reconstructed/verified, "<<bytes<<" patch bytes.\n";
                    if(character_host.empty()) {
                        // This copy is test-fixture setup, not a production
                        // entry point. Keep its own deep files long-path-safe;
                        // public prepare_profile inputs above remain ordinary.
                        character_host=private_storage_path(root/"character-host");std::filesystem::create_directory(character_host);
                        for(const auto* folder:{"reviews","prepared-characters"})
                            std::filesystem::copy(prepared->directory/"legacy"/folder,character_host/folder,std::filesystem::copy_options::recursive);
                        std::filesystem::copy_file(prepared->directory/"legacy"/"character-catalog.json",character_host/"character-catalog.json");
                    }
                }
                check(track_state==sha256(read_file(library/"track-catalog.json",MiB)));
                check(char_state==sha256(read_file(library/"character-catalog.json",MiB)));
            }
        }
        std::filesystem::remove_all(private_storage_path(root));std::cout<<checks<<" isolated profile checks passed. No game window opened.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';if(!root.empty())std::cerr<<"Private fixture retained for diagnostics.\n";return 1;}
}
