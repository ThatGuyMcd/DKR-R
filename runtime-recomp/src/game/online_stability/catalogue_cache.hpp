#pragma once
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace dkr::online_stability {
// A transaction-local metadata snapshot. It neither persists an asset proof
// nor replaces deep runtime validation; every subsequent job starts empty.
template<class Catalogue,class Item> class CatalogueScope final {
    using Key=std::tuple<std::filesystem::path,std::string,typename Catalogue::Kind>;
    inline static thread_local CatalogueScope* current_=nullptr;
    CatalogueScope* previous_=current_;
    std::map<Key,std::vector<Item>> entries_;
    unsigned scans_=0,hits_=0;
public:
    CatalogueScope(){current_=this;}
    ~CatalogueScope(){current_=previous_;std::fprintf(stderr,"[online-mods][catalogues] scans=%u reused=%u\n",scans_,hits_);}
    CatalogueScope(const CatalogueScope&)=delete;
    CatalogueScope& operator=(const CatalogueScope&)=delete;
    static std::vector<Item> installed(const std::filesystem::path& library,std::string_view revision,typename Catalogue::Kind kind) {
        if(!current_)return Catalogue::installed_items(library,revision,kind);
        const Key key{library,std::string(revision),kind};
        if(const auto at=current_->entries_.find(key);at!=current_->entries_.end()){++current_->hits_;return at->second;}
        ++current_->scans_;auto items=Catalogue::installed_items(library,revision,kind);
        if(current_->entries_.size()<128)current_->entries_.emplace(key,items);
        return items;
    }
};
}
