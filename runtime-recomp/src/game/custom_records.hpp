#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::legacy {class ModWorld;}

namespace dkr::runtime::saves::records {
using Bytes=std::vector<std::uint8_t>;
struct Record {
    std::string key;unsigned vehicle=0;
    std::uint16_t lap=0,lap_name=0x7fff,course=0,course_name=0x7fff;
};
using Map=std::map<std::pair<std::string,unsigned>,Record>;
inline std::uint64_t checksum(std::span<const std::uint8_t> bytes) {
    std::uint64_t h=14695981039346656037ULL;for(auto b:bytes){h^=b;h*=1099511628211ULL;}return h;
}
inline void append_integer(Bytes& out,std::uint64_t value,unsigned n) {while(n--){out.push_back(std::uint8_t(value));value>>=8;}}
inline std::uint64_t integer(std::span<const std::uint8_t> bytes,unsigned at,unsigned n) {
    std::uint64_t value=0;for(unsigned i=0;i<n;++i)value|=std::uint64_t(bytes[at+i])<<(8*i);return value;
}
inline Bytes encode(const Map& map) {
    Bytes payload;
    for(const auto& [id,r]:map) {
        payload.insert(payload.end(),r.key.begin(),r.key.end());append_integer(payload,r.vehicle,4);
        for(auto value:{r.lap,r.lap_name,r.course,r.course_name})append_integer(payload,value,2);
    }
    Bytes out={'D','K','R','R','E','C','1',0};append_integer(out,map.size(),4);append_integer(out,checksum(payload),8);
    out.insert(out.end(),payload.begin(),payload.end());return out;
}
inline bool decode(std::span<const std::uint8_t> bytes,Map& map) {
    if(bytes.size()<20||!std::equal(bytes.begin(),bytes.begin()+8,"DKRREC1\0"))return false;
    const auto count=integer(bytes,8,4);
    if(count>8192||bytes.size()!=20+count*76||checksum(bytes.subspan(20))!=integer(bytes,12,8))return false;
    Map next;
    for(unsigned at=20;at<bytes.size();at+=76) {
        Record r;r.key.assign(bytes.begin()+at,bytes.begin()+at+64);r.vehicle=unsigned(integer(bytes,at+64,4));
        if(r.vehicle>2||!std::all_of(r.key.begin(),r.key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))return false;
        r.lap=std::uint16_t(integer(bytes,at+68,2));r.lap_name=std::uint16_t(integer(bytes,at+70,2));
        r.course=std::uint16_t(integer(bytes,at+72,2));r.course_name=std::uint16_t(integer(bytes,at+74,2));
        if(!next.emplace(std::pair{r.key,r.vehicle},r).second)return false;
    }
    map=std::move(next);return true;
}
inline bool valid(std::span<const std::uint8_t> bytes) {Map map;return decode(bytes,map);}
// Normal guest hooks enqueue copies only. No disk access during replay.
bool begin(const std::filesystem::path& path,bool restore,std::string& error);
Bytes seed();
void submit(Bytes bytes);
bool finish(std::string& error);
bool persist(const std::filesystem::path& path,std::span<const std::uint8_t> bytes,std::string& error);
Bytes snapshot(legacy::ModWorld& world,std::uint8_t* ram);
}
