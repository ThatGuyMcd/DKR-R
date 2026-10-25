#pragma once
#include "magic_code_package.hpp"
#include <stop_token>

namespace dkr::mods::code {
struct LibraryItem {
    std::string digest,id,name,version,description,compatibility;
    bool kept_online=false,selected=false,enabled=false,approved=false,deterministic=false,presentation=false;
    Json settings=Json::object();
    Json options=Json::array();
    Json data=Json::object();
};
struct FrozenSet {
    std::vector<std::shared_ptr<const Package>> ordered;
    std::map<std::string,Json> settings;
    std::map<std::string,Json> data;
    std::map<std::string,std::shared_ptr<const Bytes>> payloads;
    std::string identity;
};
// Blocking storage API: frontend/network callers must use their existing
// bounded background jobs. Import/scan never execute code. .dkrcode blobs are
// deliberately outside the stock .nrm loader's activation mechanism.
class Library {
public:
    Library(std::filesystem::path root,std::string host_version);
    std::vector<LibraryItem> scan(std::stop_token stop={})const;
    std::string import(View package,bool kept_online=false);
    void select(std::string_view digest);
    void approve(std::string_view digest,bool approved);
    void enable(std::string_view digest,bool enabled);
    void settings(std::string_view digest,const Json& values);
    void data(std::string_view digest,const Json& documents);
    void remove(std::string_view digest);
    FrozenSet freeze(std::string_view revision,bool online,
                     const std::map<std::string,unsigned>& modules,std::stop_token stop={},bool presentation=false)const;
    std::shared_ptr<const Bytes> payload(std::string_view digest)const;
    static void export_cups(const std::filesystem::path&,View);
private:
    std::filesystem::path root_;
    std::string version_;
};
}
