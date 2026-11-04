#pragma once
#include <atomic>
#include <ostream>
#include <string_view>
namespace dkr::chat {
inline std::atomic<bool> host_enabled{true}, show_feed{true};
inline std::atomic<int> shortcut_key{23}; // SDL_SCANCODE_T, overridden by the chat key picker.
inline std::atomic<int> feed_corner{0};
inline bool valid_shortcut(int key) {return key>3&&key<512&&key!=41&&key!=58&&key!=68;}
inline void save_settings(std::ostream& out) {
    out << "online_chat_host=" << int(host_enabled.load()) << '\n'
        << "online_chat_feed=" << int(show_feed.load()) << '\n'
        << "online_chat_key=" << shortcut_key.load() << '\n'
        << "online_chat_corner=" << feed_corner.load() << '\n';
}
inline bool load_setting(std::string_view key,int value) {
    if(key=="online_chat_host")host_enabled=value!=0;
    else if(key=="online_chat_feed")show_feed=value!=0;
    else if(key=="online_chat_key"){if(valid_shortcut(value))shortcut_key=value;}
    else if(key=="online_chat_corner"){if(value>=0&&value<4)feed_corner=value;}
    else return false;
    return true;
}
} // namespace dkr::chat
