#pragma once
#include "region.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace dkr::launcher_themes {
struct CountryDetection { std::string country,source; };
using RegionalValues=std::map<std::string,std::string,std::less<>>;
inline std::string trim_region(std::string value) {
    const auto begin=value.find_first_not_of(" \t\r\n");if(begin==value.npos)return {};
    value=value.substr(begin,value.find_last_not_of(" \t\r\n")-begin+1);
    if(value.size()>=2&&((value.front()=='"'&&value.back()=='"')||(value.front()=='\''&&value.back()=='\'')))value=value.substr(1,value.size()-2);
    return value;
}
inline RegionalValues parse_regional(std::string_view text,bool plasma) {
    RegionalValues values;if(text.size()>65536)return values;
    bool formats=!plasma;
    for(std::size_t at=0;at<text.size();) {
        const auto end=text.find('\n',at);const auto line=trim_region(std::string(text.substr(at,end==text.npos?text.size()-at:end-at)));
        at=end==text.npos?text.size():end+1;
        if(line.empty()||line.size()>1024||line[0]=='#'||line[0]==';')continue;
        if(line[0]=='['){formats=line=="[Formats]";continue;}
        const auto split=line.find('=');if(!formats||split==line.npos)continue;
        const auto key=trim_region(line.substr(0,split));
        if(key=="LC_ALL"||key=="LC_ADDRESS"||key=="LC_TIME"||key=="LANG")values[key]=trim_region(line.substr(split+1));
    }
    return values;
}
inline RegionalValues read_regional(const std::filesystem::path& path,bool plasma) {
    std::error_code error;
    if(!std::filesystem::is_regular_file(path,error)||std::filesystem::file_size(path,error)>65536||error)return {};
    std::ifstream in(path,std::ios::binary);std::string text(65537,'\0');
    in.read(text.data(),std::streamsize(text.size()));text.resize(std::size_t(in.gcount()));
    if(in.bad()||text.size()>65536)return {};
    return parse_regional(text,plasma);
}
inline std::string regional_country(const RegionalValues& values) {
    for(const auto key:{"LC_ALL","LC_ADDRESS","LC_TIME","LANG"})
        if(const auto found=values.find(key);found!=values.end())
            if(auto country=country_from_locale(found->second);!country.empty())return country;
    return {};
}
inline CountryDetection resolve_country(const RegionalValues& user,const RegionalValues& environment,
                                        const RegionalValues& system) {
    if(auto country=regional_country(user);!country.empty())return {country,"user regional formats"};
    auto regional=environment;regional.erase("LANG");
    if(auto country=regional_country(regional);!country.empty())return {country,"regional environment"};
    if(auto country=regional_country(system);!country.empty())return {country,"system locale configuration"};
    if(auto country=regional_country(environment);!country.empty())return {country,"language territory fallback"};
    return {{},"country unavailable; international calendar"};
}
}
