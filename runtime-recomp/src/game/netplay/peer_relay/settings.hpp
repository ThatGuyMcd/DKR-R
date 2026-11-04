#pragma once
#include "transport.hpp"
#include <ostream>
#include <string_view>
namespace dkr::relay {
inline void save_settings(std::ostream& out){out<<"online_proxy_host="<<(host_enabled.load()?1:0)<<'\n'<<"online_proxy_volunteer="<<(volunteer_enabled.load()?1:0)<<'\n';}
inline bool load_setting(std::string_view key,int value){if(key=="online_proxy_host"){host_enabled=value!=0;return true;}if(key=="online_proxy_volunteer"){volunteer_enabled=value!=0;return true;}return false;}
}
