#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
namespace dkr::runtime::netplay::experimental {
// Windows narrow CRT argv uses the active ANSI code page, not UTF-8. Parse
// application-supplied paths from the Unicode command line without a shell.
struct WindowsUtf8Arguments {
    std::vector<std::string> storage;
    std::vector<char*> values;
    bool valid=false;
    WindowsUtf8Arguments() {
        int count=0;auto** wide=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!wide||count<=0)return;
        struct Release {LPWSTR* value;~Release(){LocalFree(value);}} release{wide};
        storage.reserve(count);values.reserve(count);
        for(int i=0;i<count;++i) {
            const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
            if(bytes<=0)return;
            std::string text(std::size_t(bytes),'\0');
            if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,text.data(),bytes,nullptr,nullptr)!=bytes)return;
            text.pop_back();storage.push_back(std::move(text));
        }
        for(auto& text:storage)values.push_back(text.data());
        valid=true;
    }
};
}
#endif
