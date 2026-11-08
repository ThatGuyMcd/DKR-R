#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dkr::runtime::water::streaming {
// Selection metadata only. No guest writes, GPU pointers, extra texture
// ownership, fences, permanent preloads or renderer ABI changes.
struct Member {
    std::uint64_t hash=0;
    std::uint32_t layer=0;
    std::string path;
};
struct Family {
    std::vector<Member> members;
    bool complete=true,ready=false,reported=false;
    std::uint64_t checked=0;
};
struct Cache {
    std::vector<Family> families;
    std::uint64_t epoch=1;
    std::size_t members=0;
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<const void*,Cache> caches;
};
inline Registry& registry() {
    // Process-lifetime lock avoids static destruction ordering against renderer
    // owners. Entries are erased on cache clear/destruction, not retained here.
    static auto* value=new Registry;
    return *value;
}
inline void forget(const void* map) {
    auto& r=registry();std::scoped_lock lock(r.mutex);r.caches.erase(map);
}
inline void changed(const void* map) {
    auto& r=registry();std::scoped_lock lock(r.mutex);
    if(auto it=r.caches.find(map);it!=r.caches.end())++it->second.epoch;
}
inline bool register_family(const void* map,std::vector<Member> members,bool complete) {
    if(members.size()<2 || members.size()>128)return false;
    std::sort(members.begin(),members.end(),[](const Member& a,const Member& b){return a.hash<b.hash;});
    members.erase(std::unique(members.begin(),members.end(),[](const Member& a,const Member& b){return a.hash==b.hash;}),members.end());
    if(members.size()<2)return false;
    auto& r=registry();std::scoped_lock lock(r.mutex);
    auto& cache=r.caches[map];
    for(const auto& family:cache.families) {
        if(family.members.size()!=members.size())continue;
        bool same=true;
        for(std::size_t i=0;same && i<members.size();++i)
            same=family.members[i].hash==members[i].hash && family.members[i].layer==members[i].layer && family.members[i].path==members[i].path;
        if(same)return false;
    }
    if(cache.families.size()>=128 || cache.members+members.size()>4096)return false;
    cache.members+=members.size();cache.families.push_back({std::move(members),complete});++cache.epoch;
    return true;
}

struct Frame {
    std::unordered_set<std::uint64_t> blocked;
    const void* map=nullptr;
    std::uint64_t epoch=~0ULL;
    std::uint32_t source_version=0,descriptor_version=0;
};
inline auto& frames() {
    static thread_local std::unordered_map<const void*,Frame> value;
    return value;
}
// updateTextureCache and createGPUTiles run on the same renderer worker.
// Freeze ONE decision for its entire workload: descriptors and tile dimensions
// must agree, even if streaming finishes while that workload is being built.
template<class Map,class Replacements,class Versions>
void capture(const void* renderer,Map& map,Replacements& replacements,Versions& versions,std::uint32_t& global) {
    auto& snapshot=frames()[renderer];snapshot.blocked.clear();
    auto& r=registry();std::scoped_lock lock(r.mutex);
    const auto found=r.caches.find(&map.replacementMap);
    const auto epoch=found==r.caches.end() ? 0 : found->second.epoch;
    if(snapshot.map!=&map || snapshot.epoch!=epoch || snapshot.source_version!=global) {
        snapshot.map=&map;snapshot.epoch=epoch;snapshot.source_version=global;
        ++snapshot.descriptor_version;
    }
    // A renderer-local monotonically changing token avoids aliasing an old
    // descriptor snapshot when a cache clears and its family epoch restarts.
    global=snapshot.descriptor_version;
    if(found==r.caches.end())return;
    auto& cache=found->second;
    for(auto& family:cache.families) {
        if(family.checked!=cache.epoch) {
            family.ready=family.complete;
            for(const auto& member:family.members)
                if(member.path.empty() || !map.replacementMap.getFromRelativePath(member.layer,member.path))family.ready=false;
            family.checked=cache.epoch;
            if(family.ready && !family.reported) {
                std::fprintf(stderr,"[water][streaming] HD family ready: %zu identities; workload-atomic promotion\n",family.members.size());
                family.reported=true;
            }
        }
        if(!family.ready)for(const auto& member:family.members)snapshot.blocked.insert(member.hash);
    }
    // Gate changes participate in descriptor invalidation. XOR only the copied
    // version, never a cache/native texture version or any guest state.
    for(const auto hash:snapshot.blocked) {
        const auto at=map.hashMap.find(hash);
        if(at==map.hashMap.end() || at->second>=replacements.size())continue;
        replacements[at->second]=nullptr;versions[at->second]^=0x80000000U;
    }
}
inline bool blocked(const void* renderer,std::uint64_t hash) {
    const auto at=frames().find(renderer);
    return at!=frames().end() && at->second.blocked.find(hash)!=at->second.blocked.end();
}
inline void release_frame(const void* renderer){frames().erase(renderer);}
}
