#include "online_mod_bundle.hpp"
#include "legacy_character_artifact.hpp"
#include "legacy_track_artifact.hpp"
#include <json/json.hpp>
#include <chrono>
#include <iostream>

using namespace dkr::mods;
using namespace dkr::mods::online;
namespace {
unsigned checks=0;
void check(bool value) {if(!value)throw Error("Online manifest assertion "+std::to_string(checks));++checks;}
template<class F>void rejects(F function) {bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected);}
Bytes bytes(std::string_view text) {return {text.begin(),text.end()};}
void file(const std::filesystem::path& path,std::string_view value) {write_new_file(path,bytes(value));}
}
int main() {
    const auto root=std::filesystem::current_path()/("online-mod-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Manifest value;value.revision="us.v77";
        check(decode(encode(value))==value);check(transfer_size(value)==0);
        const auto patch=sha256(bytes("delta"));const auto artifact=sha256(bytes("artifact"));const auto bank=sha256(bytes("bank"));
        value.payloads.push_back({PayloadKind::Xdelta,patch,"us.v80",5});
        value.content.push_back({Kind::Character,sha256(bytes("a")),"A",patch,artifact,bank,CharacterAdapterVersion});
        value.content.push_back({Kind::Track,sha256(bytes("b")),"B",patch,artifact,bank,TrackAdapterVersion});
        check(decode(encode(value))==canonical(value));check(transfer_size(value)==5);
        const auto digest=identity(value);std::reverse(value.content.begin(),value.content.end());check(identity(value)==digest);
        auto bad=value;bad.payloads.push_back(bad.payloads.front());rejects([&]{encode(bad);});
        bad=value;bad.content.push_back(bad.content.front());rejects([&]{encode(bad);});
        bad=value;bad.payloads.front().size=MaxPatch+1;rejects([&]{encode(bad);});
        bad=value;bad.payloads.front().source_revision="pal";rejects([&]{encode(bad);});
        bad=value;bad.content.front().adapter=999;rejects([&]{encode(bad);});
        bad=value;bad.content.front().name="bad\nlabel";rejects([&]{encode(bad);});
        bad=value;bad.payloads.front().digest[0]='A';rejects([&]{encode(bad);});
        bad=value;bad.content.front().payload=sha256(bytes("missing"));rejects([&]{encode(bad);});
        bad=value;bad.content.clear();rejects([&]{encode(bad);});
        auto json=nlohmann::json::parse(encode(value));json["native_code"]="no";rejects([&]{decode(bytes(json.dump()));});
        rejects([&]{decode(bytes("{\"schema\":1,\"schema\":1}"));});
        rejects([&]{decode(Bytes(MaxManifestBytes+1,' '));});
        bad=value;bad.custom_ai=false;check(identity(bad)!=digest);
        bad=value;bad.content.front().bank=sha256(bytes("changed"));check(identity(bad)!=digest);
        Manifest large;large.revision="us.v77";
        for(unsigned i=0;i<MaxCharacters;++i) {
            const auto id=sha256(bytes("character-"+std::to_string(i)));
            large.payloads.push_back({PayloadKind::Xdelta,id,"us.v77",5});
            large.content.push_back({Kind::Character,id,"Racer "+std::to_string(i),id,artifact,bank,CharacterAdapterVersion});
        }
        check(decode(encode(large))==canonical(large));
        std::reverse(large.content.begin(),large.content.end());check(identity(large)==identity(canonical(large)));
        large.payloads.push_back({PayloadKind::Xdelta,patch,"us.v77",5});
        large.content.push_back({Kind::Character,sha256(bytes("over-limit")),"Over",patch,artifact,bank,CharacterAdapterVersion});
        rejects([&]{encode(large);});
        std::filesystem::create_directory(root);std::filesystem::create_directory(root/"track");
        file(root/"track"/"manifest.json",R"({"schemaVersion":1,"id":"example","name":"Example","adds":[{"section":"LEVEL_HEADERS","file":"header.bin"}]})");
        file(root/"track"/"header.bin","payload");file(root/"track"/"private.txt","must not be shared");
        const auto packed=pack_track_lab(root/"track");check(packed==pack_track_lab(root/"track"));
        unpack_track_lab(packed,root/"received");check(read_file(root/"received"/"header.bin",100)==bytes("payload"));
        check(!std::filesystem::exists(root/"received"/"private.txt"));rejects([&]{unpack_track_lab(packed,root/"received");});
        auto broken=packed;broken.push_back(0);rejects([&]{unpack_track_lab(broken,root/"broken");});
        check(!std::filesystem::exists(root/"broken"));
        const auto nested=root/utf8_path("nested-\xE2\x98\x83");
        std::filesystem::create_directories(nested/"textures");
        file(nested/"manifest.json",R"({"schemaVersion":1,"id":"nested","name":"Nested","adds":[{"section":"TEXTURES_3D","file":"textures/0.bin"}]})");
        file(nested/"textures"/"0.bin","nested payload");
        const auto nested_package=pack_track_lab(private_storage_path(nested));
        check(nested_package==pack_track_lab(nested));
        const auto nested_received=private_storage_path(root/utf8_path("received-\xE2\x98\x83"));
        unpack_track_lab(nested_package,nested_received);
        check(read_file(nested_received/"textures"/"0.bin",100)==bytes("nested payload"));
        check(pack_track_lab(nested_received)==nested_package);
        std::filesystem::create_directory(root/"unsafe");
        file(root/"unsafe"/"manifest.json",R"({"schemaVersion":1,"adds":[{"file":"../private.txt"}]})");
        rejects([&]{pack_track_lab(root/"unsafe");});
        std::stop_source stop;stop.request_stop();rejects([&]{pack_track_lab(root/"track",stop.get_token());});
        check(export_legacy(root/"missing","us.v77").manifest.content.empty());
        // No offline activation file was read or written by packet parsing.
        check(!std::filesystem::exists(root/"track-catalog.json"));
        std::cout<<checks<<" online manifest/package checks passed. No game window opened.\n";
        std::filesystem::remove_all(root);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
