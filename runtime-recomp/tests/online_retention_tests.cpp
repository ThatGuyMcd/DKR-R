#include "mods/online_mod_profile.hpp"
#include "mods/online_mod_prepare.hpp"
#include "custom_tracks.hpp"
#include "online_stability/preparation_policy.hpp"
#include <json/json.hpp>
#include <cassert>
#include <chrono>
#include <iostream>

using namespace dkr::mods;
using namespace dkr::mods::online;
namespace {
void json_file(const std::filesystem::path& path,const nlohmann::json& value){
    const auto data=value.dump();write_new_file(path,View(reinterpret_cast<const std::uint8_t*>(data.data()),data.size()));
}
}
int main()try{
    const auto root=std::filesystem::temp_directory_path()/("dkrr-course-retention-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root));
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code e;std::filesystem::remove_all(root,e);}} cleanup{root};
    for(const auto* dir:{"payloads","online","source.dkrmap","source.dkrmap/headers"})std::filesystem::create_directory(root/dir);
    json_file(root/"source.dkrmap/manifest.json",{{"schemaVersion",1},{"id","retention-test"},{"name","Host course"},
        {"adds",nlohmann::json::array({{{"section","LEVEL_HEADERS"},{"file","headers/header.bin"}}})}});
    Bytes header(200,0);header[0]=6;header[0x4E]=1;header[0x37]=73;header[0xBB]=5;
    write_new_file(root/"source.dkrmap/headers/header.bin",header);
    const auto bundle=export_host(root/"no-legacy",{root/"source.dkrmap"},"us.v77",true);
    for(const auto& [digest,data]:bundle.payloads)write_new_file(root/"payloads"/digest,*data);
    const auto profile=prepare_profile(bundle.manifest,root/"payloads",{},root/"online",{});
    retain_profile(*profile,root/"legacy",root/"tracks");
    dkr::runtime::custom_tracks::scan(root/"tracks");
    auto installed=dkr::runtime::custom_tracks::tracks();assert(installed.size()==1);
    const auto course=installed[0].source,manifest=course/"manifest.json";
    const auto data=read_file(manifest,MiB);auto doc=nlohmann::json::parse(data.begin(),data.end());doc["name"]="My offline edit";
    assert(std::filesystem::remove(manifest));json_file(manifest,doc);
    const auto preserved=pack_track_lab(course);
    retain_profile(*profile,root/"legacy",root/"tracks");
    assert(pack_track_lab(course)==preserved);
    const auto incoming=pack_track_lab(profile_sources(*profile).courses.at(0));
    const auto archive=dkr::online_stability::archived_course(root/"tracks",sha256(incoming));
    assert(pack_track_lab(archive)==incoming);
    const auto count=std::distance(std::filesystem::recursive_directory_iterator(root/"tracks"),std::filesystem::recursive_directory_iterator{});
    retain_profile(*profile,root/"legacy",root/"tracks");
    assert(count==std::distance(std::filesystem::recursive_directory_iterator(root/"tracks"),std::filesystem::recursive_directory_iterator{}));
    dkr::runtime::custom_tracks::scan(root/"tracks");
    installed=dkr::runtime::custom_tracks::tracks();assert(installed.size()==1&&installed[0].name=="My offline edit"&&!installed[0].enabled);
    assert(std::filesystem::remove(archive/"manifest.json"));json_file(archive/"manifest.json",doc);
    bool rejected=false;try{retain_profile(*profile,root/"legacy",root/"tracks");}catch(const std::exception&){rejected=true;}
    assert(rejected&&pack_track_lab(course)==preserved);
    std::cout<<"Actual profile retention: identical reuse, same-id variant archive, repeated keep, inactive archive and corrupt archive rejection passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
