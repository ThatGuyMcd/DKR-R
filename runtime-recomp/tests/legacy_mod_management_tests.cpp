#include "legacy_track_catalog.hpp"
#include "legacy_mod_browser.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace dkr::mods;using nlohmann::json;
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw Error("Mod management assertion "+std::to_string(checks));}
void write(const std::filesystem::path& path,const json& value) {
    const auto text=value.dump();write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}
std::string hash(const json& value){const auto text=value.dump();return sha256(View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}
void wait(TrackCatalog& catalog,bool expected=true) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(catalog.snapshot()->busy&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(!catalog.snapshot()->busy);
    if(catalog.snapshot()->succeeded!=expected)throw Error(catalog.snapshot()->result);
    check(catalog.snapshot()->succeeded==expected);
}
json entry(char id,const char* name,bool character=false,bool second=false) {
    const std::string identity(64,id),storage(64,second?'f':id);
    json value={{"id",identity},{"storage",storage},{"name",name},{"revision",second?"us.v80":"us.v77"},
        {"artifact",hash(json::object())},{"patch",std::string(64,'9')},{"vehicles",7}};
    if(character)value["behaviour"]=0;else {value["bank"]=identity;value["carrier"]=4;value["race_type"]=0;}
    return value;
}
json receipt(const json& entries,bool characters=false) {
    return {{"schema",1},{"adapter",characters?CharacterAdapterVersion:TrackAdapterVersion},{"enabled",false},
        {characters?"characters":"tracks",entries},{"blocked",json::array()}};
}
// Synthetic catalogue/management fixtures only. These are deliberately not
// playable artifacts and are never passed to the runtime asset loader.
std::filesystem::path install(const std::filesystem::path& prepared,const json& data,bool chars=false) {
    const auto folder=prepared/hash(data);std::filesystem::create_directories(folder);write(folder/"prepared.json",data);
    for(const auto& item:data.at(chars?"characters":"tracks")) {
        const auto path=folder/item.at("storage").get<std::string>();std::filesystem::create_directory(path);
        write(path/(chars?"character.json":"track.json"),json::object());write(path/"payload.json",{{"untouched",true}});
    }
    return folder;
}
}
int main() {
    const auto root=private_storage_path(std::filesystem::current_path()/("mod-management-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try {
        std::filesystem::create_directory(root);write(root/"save-sentinel.json",{{"save",true}});
        const std::string a(64,'a'),b(64,'b'),c(64,'c');
        const auto original=receipt(json::array({entry('a',"Long Custom Track Name"),entry('a',"Long Custom Track Name",false,true),entry('b',"Surviving Track")}));
        const auto initial=install(root/"prepared",original);
        install(root/"prepared-characters",receipt(json::array({entry('a',"Mario",true),entry('b',"Dixie",true),entry('c',"Link",true)}),true),true);
        {
            TrackCatalog tracks,characters(TrackCatalog::Kind::Character);tracks.configure(root,{});characters.configure(root,{});wait(tracks);wait(characters);
            check(tracks.snapshot()->tracks.size()==3);
            const auto cards=browser::cards(*tracks.snapshot(),false);check(cards.size()==2);check(cards[0].revisions==3);check(cards[0].item.managed_bytes>0);
            browser::Filters filter;filter.revision=2;filter.query="LONG";check(browser::select(cards,filter).size()==1);
            filter.query="";filter.compatibility=2;check(browser::select(cards,filter).size()==1);
            for(const auto& id:{a,b}){check(characters.set_enabled(id,true));wait(characters);}
            // The current same-stage roster admits more than the original
            // two mods. Keep this fixture aligned with the existing feature.
            check(characters.set_enabled(c,true));wait(characters);
            check(browser::active_count(browser::cards(*characters.snapshot(),true))==3);
            check(characters.set_hidden(a,true));wait(characters);
            check(browser::active_count(browser::cards(*characters.snapshot(),true))==2);
            check(characters.set_enabled(a,true));wait(characters,false);
            check(characters.set_hidden(a,false));wait(characters);check(!characters.snapshot()->tracks[1].hidden); // names are sorted, not activation order.
            check(characters.set_enabled(c,true));wait(characters);
            check(tracks.set_enabled(a,true));wait(tracks);check(tracks.set_enabled(b,true));wait(tracks);
            check(tracks.set_hidden(a,true));wait(tracks);
            for(const auto& item:tracks.snapshot()->tracks)if(item.id==a)check(item.hidden&&!item.enabled);
            check(tracks.set_hidden(a,false));wait(tracks);
            check(tracks.remove("../bad"));wait(tracks,false);check(std::filesystem::exists(initial));
            check(tracks.remove(a));wait(tracks);check(tracks.snapshot()->tracks.size()==1);
            const auto survivor=tracks.snapshot()->tracks.front();check(survivor.id==b&&survivor.enabled);check(!std::filesystem::exists(initial));
            check(read_file(root/"prepared"/survivor.group/survivor.storage/"payload.json",1024)==read_file(root/"prepared-characters"/characters.snapshot()->tracks.front().group/characters.snapshot()->tracks.front().storage/"payload.json",1024));
            check(std::filesystem::exists(root/"save-sentinel.json"));check(characters.snapshot()->tracks.size()==3);
            check(tracks.refresh());wait(tracks);check(tracks.snapshot()->tracks.size()==1);check(std::filesystem::is_empty(root/"prepare-jobs"));
            check(tracks.remove(b));wait(tracks);check(tracks.snapshot()->tracks.empty());
            check(characters.remove(c));wait(characters);check(characters.snapshot()->tracks.size()==2);
        }
        // Recovery at each publication boundary: journal, retirement, publish.
        const std::string job_id(64,'8');
        for(int stage=0;stage<3;++stage) {
            const auto recovery=root/("recovery"+std::to_string(stage));const auto old=install(recovery/"prepared",original);
            const auto job=recovery/"prepare-jobs"/job_id;std::filesystem::create_directories(job/"retired");
            const auto survivors=receipt(json::array({entry('b',"Surviving Track")}));const auto next=install(job/"incoming",survivors);
            write(recovery/"track-removal.json",{{"schema",1},{"id",a},{"job",job_id},{"old",json::array({hash(original)})},{"new",json::array({hash(survivors)})}});
            if(stage>=1)std::filesystem::rename(old,job/"retired"/hash(original));
            if(stage>=2)std::filesystem::rename(next,recovery/"prepared"/hash(survivors));
            TrackCatalog catalog;catalog.configure(recovery,{});wait(catalog);check(catalog.snapshot()->tracks.size()==1);
            check(catalog.snapshot()->tracks.front().id==b);check(!std::filesystem::exists(old));check(!std::filesystem::exists(job));
        }
        // A corrupted recovery journal must not discard surviving entries.
        const auto broken=root/"broken";const auto preserved=install(broken/"prepared",original);
        const auto badjob=broken/"prepare-jobs"/job_id;std::filesystem::create_directories(badjob/"retired");std::filesystem::create_directory(badjob/"incoming");
        write(broken/"track-removal.json",{{"schema",1},{"id",a},{"job",job_id},{"old",json::array({hash(original)})},{"new",json::array()}});
        {TrackCatalog catalog;catalog.configure(broken,{});wait(catalog,false);check(std::filesystem::exists(preserved));}
        // Failure to read/write presentation metadata never deletes content.
        const auto unwritable=root/"unwritable";const auto kept=install(unwritable/"prepared",original);
        std::filesystem::create_directory(unwritable/"track-library.json");
        {TrackCatalog catalog;catalog.configure(unwritable,{});wait(catalog,false);check(catalog.remove(a));wait(catalog,false);check(std::filesystem::exists(kept));}
        // Links inside managed content may not be followed by copy or removal.
        const auto linked=root/"linked";const auto linked_group=install(linked/"prepared",original);
        const auto outside=root/"outside";std::filesystem::create_directory(outside);write(outside/"sentinel.json",{{"safe",true}});
        std::error_code link_error;std::filesystem::create_directory_symlink(outside,linked_group/a/"redirect",link_error);
        if(!link_error) {
            TrackCatalog catalog;catalog.configure(linked,{});wait(catalog,false);
            check(catalog.remove(a));wait(catalog,false);check(std::filesystem::exists(outside/"sentinel.json"));check(std::filesystem::exists(linked_group));
        }
        // Stable filtering, hidden-state counts and dates with unknowns last.
        TrackCatalogView view;TrackCatalogItem item;item.id=a;item.name="Zebra";item.enabled=true;item.revision="us.v77";view.tracks.push_back(item);
        item.id=b;item.name="Alpha";item.hidden=true;item.enabled=false;item.imported_at=123;view.tracks.push_back(item);
        const auto cards=browser::cards(view,true);browser::Filters filter;filter.visibility=1;filter.sort=5;
        check(browser::select(cards,filter).front().item.id==b);check(browser::active_count(cards)==1);
        filter.visibility=2;check(browser::select(cards,filter).size()==1);
        check(!browser::can_activate(cards[0],true,MaxActiveStageCharacters,1,false));check(!browser::can_activate(cards[0],true,0,1,true));
        std::filesystem::remove_all(root);std::cout<<checks<<" management, recovery and browser checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"; fixture retained at "<<root<<'\n';return 1;}
}
