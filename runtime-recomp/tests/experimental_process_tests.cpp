#include "netplay/experimental_process.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
using namespace dkr::runtime::netplay::experimental;
namespace {
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
const std::vector<std::string> arguments{"with spaces", "quotes\"in value", "trailing\\", "", "caf\xC3\xA9"};
}
int run_tests(int argc,char** argv) try {
    if(argc>1&&std::string_view(argv[1])=="--argv") {
        if(argc!=int(arguments.size()+2))return 2;
        for(std::size_t i=0;i<arguments.size();++i)if(arguments[i]!=argv[i+2])return 3;
        return 0;
    }
    if(argc>1&&std::string_view(argv[1])=="--fail")return 7;
    if(argc>1&&std::string_view(argv[1])=="--sleep") {std::this_thread::sleep_for(std::chrono::seconds(10));return 0;}
    if(argc>1&&std::string_view(argv[1])=="--short-sleep") {std::this_thread::sleep_for(std::chrono::milliseconds(200));return 0;}
    const auto exe=std::filesystem::absolute(argv[0]);std::string error;
    std::vector<std::string> args{"--argv"};args.insert(args.end(),arguments.begin(),arguments.end());
    check(run_child(exe,args,{},std::chrono::seconds(3),error),"Exact argv/Unicode child failed");
    check(!run_child(exe,std::vector<std::string>{"--fail"},{},std::chrono::seconds(3),error),"Nonzero child was accepted");
    check(!run_child(exe,std::vector<std::string>{std::string("embedded\0nul",12)},{},std::chrono::seconds(3),error),"NUL argument was accepted");
    auto begin=std::chrono::steady_clock::now();
    check(!run_child(exe,std::vector<std::string>{"--sleep"},[]{return false;},std::chrono::seconds(3),error),"Cancellation ignored");
    check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(3),"Cancellation did not reap promptly");
    begin=std::chrono::steady_clock::now();
    check(!run_child(exe,std::vector<std::string>{"--sleep"},{},std::chrono::seconds(1),error),"Timeout ignored");
    check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(4),"Timeout did not reap promptly");
    check(launch_child(exe,std::vector<std::string>{"--short-sleep"},error),"Detached launch failed");
    check(!launch_child(exe,std::vector<std::string>{"--short-sleep"},error),"Duplicate application launch was permitted");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check(launch_child(exe,std::vector<std::string>{"--argv","with spaces","quotes\"in value","trailing\\","","caf\xC3\xA9"},error),"Relaunch after child exit failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout<<"Experimental child lifecycle: exact argv, Unicode, failure, invalid input, cancel, timeout, duplicate refusal and relaunch passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#if defined(_WIN32)
int wmain(int argc,wchar_t** wide) {
    std::vector<std::string> storage;std::vector<char*> argv;
    for(int i=0;i<argc;++i) {
        const auto count=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(count<=0)return 4;
        std::string text(std::size_t(count),'\0');
        WideCharToMultiByte(CP_UTF8,0,wide[i],-1,text.data(),count,nullptr,nullptr);
        text.pop_back();storage.push_back(std::move(text));
    }
    for(auto& text:storage)argv.push_back(text.data());
    return run_tests(argc,argv.data());
}
#else
int main(int argc,char** argv){return run_tests(argc,argv);}
#endif
