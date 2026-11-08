#include "preparation_policy.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fstream>
#endif

namespace dkr::online_stability {
std::uint64_t available_memory(){
#ifdef _WIN32
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    return GlobalMemoryStatusEx(&memory)?memory.ullAvailPhys:0;
#elif defined(__linux__)
    std::ifstream input("/proc/meminfo");std::string key,line;std::uint64_t kib=0;
    while(input>>key){
        if(key=="MemAvailable:")return input>>kib?kib*1024:0;
        std::getline(input,line);
    }
    return 0;
#else
    return 0; // Unknown platforms retain the existing two-item worker limit.
#endif
}
}
