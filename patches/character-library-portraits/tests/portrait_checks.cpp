#include "character_portraits.hpp"
#include "legacy_mod_format.hpp"
#include "legacy_character_artifact.hpp"
#include <json/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace dkr;
unsigned checks=0;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char* why){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,why);}
int main(int argc,char** argv)try {
    namespace fs=std::filesystem;using nlohmann::json;using namespace character_portraits;
    if(argc==8&&std::string_view(argv[1])=="--portrait") {
        const auto image=load({mods::utf8_path(argv[2]),argv[3],argv[4],argv[5],argv[6],argv[7],{}});
        std::cout<<image.width<<' '<<image.height<<' '<<mods::sha256(image.rgba)<<'\n';return 0;
    }
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root=mods::private_storage_path(fs::temp_directory_path()/("dkrr-portraits-"+std::to_string(stamp)));fs::create_directory(root);
    struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};
    Request request{root,std::string(64,'a'),{},std::string(64,'c'),std::string(64,'d'),"us.v77",{}};
    const auto folder=root/"prepared-characters"/request.group/request.storage;fs::create_directories(folder/"blobs");
    auto fixture=[&](unsigned format,mods::Bytes pixels,unsigned width=2,unsigned height=1){
        mods::Bytes bytes(32);bytes[0]=std::uint8_t(width);bytes[1]=std::uint8_t(height);bytes[2]=std::uint8_t(format);bytes[0x12]=1;
        bytes.insert(bytes.end(),pixels.begin(),pixels.end());const auto hash=mods::sha256(bytes);
        {std::ofstream out(folder/"blobs"/hash,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        json document={{"schema",1},{"adapter",mods::CharacterAdapterVersion},{"id",request.id},{"revision",request.revision},{"portrait",122},
            {"records",json::array({{{"section",4},{"record",122},{"hash",hash},{"size",bytes.size()}}})}};
        const auto text=document.dump();request.artifact=mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
        std::ofstream(folder/"character.json",std::ios::binary|std::ios::trunc)<<text;
        return hash;
    };
    const auto blob=fixture(1,{0xF8,0x01,0x07,0xC1});
    const auto image=load(request);
    check(image.width==2&&image.height==1&&image.rgba.size()==8,"native portrait dimensions");
    check(image.rgba==mods::Bytes({255,0,0,255,0,255,0,255}),"RGBA16 colour/alpha expansion and orientation");
    {Service service;std::shared_ptr<const Image> result;
     const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
     while(!result&&std::chrono::steady_clock::now()<until){result=service.request(request);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
     check(result&&result->rgba==image.rgba,"background decode publishes immutable image");
     check(service.request(request)==result,"same identity reuses immutable result");}
    auto invalid=request;invalid.storage="../escape";rejects([&]{load(invalid);},"path traversal rejected");
    invalid=request;invalid.artifact=std::string(64,'e');rejects([&]{load(invalid);},"changed manifest rejected");
    {std::ofstream(folder/"blobs"/blob,std::ios::binary|std::ios::trunc)<<"bad";}
    rejects([&]{load(request);},"changed/truncated blob rejected");
    fixture(0,{1,2,3,4,5,6,7,8});check(load(request).rgba==mods::Bytes({1,2,3,4,5,6,7,8}),"RGBA32 native channels");
    fixture(4,{9,44,177,255});check(load(request).rgba==mods::Bytes({9,9,9,44,177,177,177,255}),"IA16 portrait alpha");
    fixture(5,{0xF1,0x24});check(load(request).rgba==mods::Bytes({255,255,255,17,34,34,34,68}),"IA8 portrait alpha");
    fixture(1,{0xFF});rejects([&]{load(request);},"truncated texels rejected");
    fixture(1,{},0);rejects([&]{load(request);},"zero dimensions rejected");
    fixture(15,{0,0,0,0});rejects([&]{load(request);},"unknown format rejected");
    fixture(7,{0});rejects([&]{load(request);},"palette format requires matching imported ROM");
    std::cout<<checks<<" character portrait checks passed; no game or ROM executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
