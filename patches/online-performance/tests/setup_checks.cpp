#include "setup_policy.hpp"
#include "runtime_log.hpp"
#include "online_mod_cache.hpp"
#include "../updates/update_process.hpp"
#include <iostream>
#include <thread>
#include <limits>
namespace p=dkr::online_performance;
namespace mods=dkr::mods::online;
namespace updates=dkr::updates;
enum class Traffic {Control,Authoritative,Realtime,Replica,Checkpoint};
unsigned checks=0;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
int main(int argc,char** argv)try {
#ifdef _WIN32
    if(argc==3 && std::string_view(argv[1])=="--child"){
        DWORD flags=0;const auto h=reinterpret_cast<HANDLE>(std::stoull(argv[2]));
        return GetHandleInformation(h,&flags)?1:0;
    }
#endif
    check(!p::setup_bulk,"default race queue");
    {p::SetupBulk scope(true);check(p::setup_bulk,"setup permission");
     std::thread peer([]{check(!p::setup_bulk,"permission cannot leak to other threads");});peer.join();
     {p::SetupBulk nested(false);check(!p::setup_bulk,"nested restriction");}check(p::setup_bulk,"restore outer permission");}
    check(!p::setup_bulk,"permission retired at call exit");
    for(std::size_t size:{1U,8193U,11300U}){
        std::size_t used=0;unsigned packets=0;
        while(p::additional_capacity(Traffic::Checkpoint,true,used,used,size)){used+=size;++packets;}
        check(used<=p::SetupBytes&&packets>16,"bounded useful setup burst");
        check(!p::additional_capacity(Traffic::Checkpoint,false,0,0,size),"race bulk has no extra permission");
        check(p::additional_capacity(Traffic::Control,false,0,used,1000),"heartbeat headroom under bulk pressure");
        check(!p::additional_capacity(Traffic::Replica,true,0,0,size),"live spectator queue unchanged");
    }
    check(!p::additional_capacity(Traffic::Checkpoint,true,0,0,std::numeric_limits<std::size_t>::max()),"overflow frame rejected");
    check(!p::additional_capacity(Traffic::Control,true,p::ControlReserve,0,1),"control lane remains bounded");
    constexpr std::uint64_t MiB=1024*1024;
    check(p::resource_budget(16*1024*MiB,8*1024*MiB,false)==768*MiB,"large collection retained on capable desktop");
    check(p::resource_budget(16*1024*MiB,1024*MiB,false)==256*MiB,"memory pressure reduces retention");
    check(p::resource_budget(4*1024*MiB,4*1024*MiB,false)==256*MiB,"small hardware cap retained");
    check(p::resource_budget(16*1024*MiB,8*1024*MiB,true)==256*MiB,"mobile cap unchanged");
    check(p::resource_budget(0,0,false)==0,"unknown memory disables strong retention");
    const auto root=std::filesystem::temp_directory_path()/("dkrr-online-setup-"+updates::unique_id());
    std::filesystem::create_directory(root);
    struct Cleanup{std::filesystem::path root;~Cleanup(){std::error_code e;std::filesystem::remove_all(root,e);}} cleanup{root};
    dkr::mods::Bytes content(2*MiB+73);for(std::size_t i=0;i<content.size();++i)content[i]=std::uint8_t(i*13);
    mods::Payload expected{};expected.digest=dkr::mods::sha256(content);expected.size=content.size();
    {
        mods::PayloadFile file(root/"partial",expected);
        for(std::size_t i=0;i<content.size();i+=8192)file.append(dkr::mods::View(content).subspan(i,std::min<std::size_t>(8192,content.size()-i)));
        check(file.size()==content.size(),"all bytes persisted");file.publish(root/expected.digest);
    }
    check(mods::cached_payload(root,expected),"durable full hash verified before publish");
    {
        mods::PayloadFile file(root/"resume",expected);file.append(dkr::mods::View(content).first(8192));
    }
    {
        mods::PayloadFile file(root/"resume",expected);check(file.size()==8192,"cancellation retains resumable prefix");
        bool failed=false;try{file.publish(root/"incomplete");}catch(...){failed=true;}
        check(failed&&!std::filesystem::exists(root/"incomplete"),"incomplete payload cannot publish");
    }
    const auto log=root/"test.log";
#ifdef _WIN32
    FILE* stream=nullptr;_wfopen_s(&stream,log.c_str(),L"w");
#else
    FILE* stream=std::fopen(log.c_str(),"w");
#endif
    check(stream!=nullptr&&p::protect_log(stream),"persistent log made non-inheritable");
#ifdef _WIN32
    const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stream)));
    DWORD flags=HANDLE_FLAG_INHERIT;
    check(GetHandleInformation(handle,&flags)&&!(flags&HANDLE_FLAG_INHERIT),"actual CRT handle flag cleared");
    // Force the old hazardous CRT flag back on: the production child launcher
    // must still exclude it. Only this synthetic executable is started.
    check(SetHandleInformation(handle,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)!=0,"fixture inheritable handle");
    updates::Child child(updates::application_executable(),{"--child",std::to_string(reinterpret_cast<std::uintptr_t>(handle))},root);
    check(child.wait(10000)==0,"production Child does not inherit CRT file handles");
#else
    check((fcntl(fileno(stream),F_GETFD)&FD_CLOEXEC)!=0,"actual POSIX log descriptor closes on exec");
#endif
    std::fclose(stream);
    const auto one=p::fallback_log(root),two=p::fallback_log(root);
    check(one!=two&&one.parent_path()==root/"history","separate logs have unique bounded-history names");
    std::cout<<checks<<" online setup/cache/logging checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
