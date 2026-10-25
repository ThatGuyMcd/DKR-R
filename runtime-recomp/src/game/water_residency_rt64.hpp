#pragma once
#include "enhanced_water_rt64.hpp"
#include "polish_symbols.hpp"
#include "rice_texture_pack_policy.hpp"
#include "liquid_replacement_gate.hpp"
#include "performance_observation.hpp"
#include "render/rt64_texture_cache.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

namespace dkr::runtime::water {
template<class Clock=std::chrono::steady_clock>
class LiquidResidencyT {
    RT64::TextureCache* cache_=nullptr;
    std::uint64_t generation_=0,evictions_=0;
    std::uint32_t scene_=~0U,loaded_=~0U,model_=0;
    streaming::SelectionPtr selection_;
    typename Clock::time_point retry_after_{};
    unsigned retries_=0;
    bool pending_=false;
    static std::uint32_t crc(const std::uint8_t* source,unsigned row,unsigned height) {
        std::uint32_t result=0;
        for(int y=int(height)-1;y>=0;--y) {
            std::uint32_t value=0;
            for(int x=int(row)-4;x>=0;x-=4) {
                std::memcpy(&value,source+x,4);value^=std::uint32_t(x);
                result=(result<<4)+((result>>28)&15);result+=value;
            }
            value^=std::uint32_t(y);result+=value;source+=row;
        }
        return result;
    }
    static void enqueue(RT64::TextureCache& cache,const std::vector<RT64::ReplacementResolvedPath>& batch) {
        if(batch.empty())return;
        // The map lock has been released. Preserve the upload worker lock order.
        {std::scoped_lock lock(cache.uploadQueueMutex);
         cache.resolvedPathQueue.insert(cache.resolvedPathQueue.end(),batch.begin(),batch.end());}
        cache.uploadQueueChanged.notify_all();
    }
    std::vector<RT64::ReplacementResolvedPath> missing(RT64::ReplacementMap& map) {
        std::vector<RT64::ReplacementResolvedPath> batch;
        std::set<std::pair<std::uint32_t,std::string>> requested;pending_=false;
        if(!selection_)return batch;
        for(const auto& group:selection_->groups)if(group.complete)for(const auto& member:group.members) {
            if(map.getFromRelativePath(member.layer,member.path))continue;
            pending_=true;
            if(member.layer>=map.fileSystemResolvedPaths.size())continue;
            const auto at=map.fileSystemResolvedPaths[member.layer].find(member.hash);
            if(at==map.fileSystemResolvedPaths[member.layer].end())continue;
            const auto& path=at->second;
            if(path.resolvedOperation==RT64::ReplacementOperation::Stream && batch.size()<128 &&
               requested.emplace(path.fileSystemIndex,path.relativePath).second)batch.push_back(path);
        }
        return batch;
    }
public:
    void publish(const void* workload,std::uint64_t frame)const {streaming::publish(workload,frame,selection_);}
    void prepare(RT64::State& state,const enhanced::Registry& materials,std::uint32_t scene,bool original) {
        auto* cache=state.ext.textureCache;
        if(!cache || !original) {selection_.reset();loaded_=~0U;model_=0;return;}
        const auto generation=cache->getReplacementGeneration();
        enhanced::Memory m(std::span<const std::uint8_t>(state.RDRAM,0x800000));
        const auto loaded=m.u32(revision_addresses::NumberOfLoadedTextures),model=m.u32(enhanced::level_model_symbol());
        const bool reset=cache!=cache_ || generation!=generation_ || scene!=scene_;
        if(reset) {cache_=cache;generation_=generation;scene_=scene;loaded_=~0U;model_=0;selection_.reset();retries_=0;}
        std::unique_lock map_lock(cache->textureMapMutex);
        auto& map=cache->textureMap.replacementMap;
        if(map.fileSystems.empty()) {selection_.reset();return;}
        const auto evictions=streaming::eviction_epoch(&map);
        const auto now=Clock::now();
        if(loaded==loaded_ && model==model_ && selection_) {
            if(evictions!=evictions_) {evictions_=evictions;pending_=true;retries_=0;}
            if(!pending_ || retries_>=5 || now<retry_after_)return;
            auto batch=missing(map);++retries_;retry_after_=now+std::chrono::seconds(1);
            map_lock.unlock();enqueue(*cache,batch);
            if(!batch.empty())std::fprintf(stderr,"[water][reload] scene=%u queued=%zu attempt=%u/5; asynchronous residency recovery\n",scene,batch.size(),retries_);
            return;
        }
        surgical::Scope timing(surgical::Region::LiquidWarmup);
        loaded_=loaded;model_=model;evictions_=evictions;retries_=0;
        struct Header {std::uint32_t address;bool square;};
        std::vector<Header> headers;
        for(const auto address:materials.liquid_headers())headers.push_back({address,false});
        const auto& symbols=presentation::polish_symbols[revision_addresses::CurrentMenuId==revision_addresses::kUsV80.CurrentMenuId?1:0];
        if(const auto waves=m.u32(symbols[7]);waves && m.valid(waves,32))headers.push_back({waves,true});
        std::vector<streaming::Family> families;
        unsigned inspected=0,incomplete=0,unsupported=0;
        for(const auto source:headers) {
            const auto first=source.address;
            if(!first || (first&3U) || !m.valid(first,32))continue;
            const unsigned count=std::max(1U,unsigned(m.u16(first+0x12)>>8));
            const unsigned stride=m.u16(first+0x16);
            if(count<2)continue;
            if(count>64 || stride<32 || !m.valid(first,std::uint64_t(count)*stride) || inspected+count>512) {++unsupported;continue;}
            streaming::Family family;
            bool valid=true;unsigned matched=0;char first_missing[40]{};
            for(unsigned frame=0;frame<count;++frame) {
                const auto header=first+frame*stride;
                const unsigned width=m.u8(header),height=source.square?width:m.u8(header+1),format=m.u8(header+2)&15U;
                constexpr std::array<unsigned,7> formats{0,0,4,4,3,3,3},sizes{3,2,1,0,2,1,0};
                if(format>=formats.size() || !width || !height || (source.square && width!=16 && width!=32)) {valid=false;break;}
                const unsigned size=sizes[format],row=width<<size>>1;
                const auto bytes=std::uint64_t(row)*height;
                if(row<4 || (row&3U) || !bytes || bytes>stride-32 || bytes>65536 || !m.valid(header+32,bytes)) {valid=false;break;}
                ++inspected;
                char identity[40];std::snprintf(identity,sizeof(identity),"%08x#%u#%u",crc(state.RDRAM+((header+32)&0x7FFFFFU),row,height),formats[format],size);
                const auto alias=rice_texture::native_alias(identity);
                streaming::Member member{alias};bool found=false;
                for(auto layer=map.fileSystemResolvedPaths.rbegin();layer!=map.fileSystemResolvedPaths.rend();++layer) {
                    const auto at=layer->find(alias);if(at==layer->end())continue;
                    member.layer=at->second.fileSystemIndex;member.path=at->second.relativePath;found=true;++matched;break;
                }
                if(!found && !first_missing[0])std::snprintf(first_missing,sizeof(first_missing),"%s",identity);
                family.complete &= found;family.members.push_back(std::move(member));
            }
            if(!valid) {++unsupported;continue;}
            if(!matched)continue;
            if(!family.complete) {
                ++incomplete;static unsigned reports=0;
                if(reports++<64)std::fprintf(stderr,"[water][coverage] scene=%u header=%08X frames=%u matched=%u missing=%u first-missing=%s; whole animation native because the pack is incomplete\n",scene,first,count,matched,count-matched,first_missing);
            }
            families.push_back(std::move(family));
        }
        const auto family_count=families.size();
        selection_=streaming::select_families(&map,scene,std::move(families));
        auto batch=missing(map);retry_after_=now+std::chrono::seconds(1);
        map_lock.unlock();enqueue(*cache,batch);
        std::fprintf(stderr,"[water][residency] scene=%u headers=%zu identities=%u families=%zu queued=%zu incomplete=%u unsupported=%u; immutable workload families; asynchronous complete-family selection\n",scene,headers.size(),inspected,family_count,batch.size(),incomplete,unsupported);
    }
};
using LiquidResidency=LiquidResidencyT<>;
inline LiquidResidency& residency(){static thread_local LiquidResidency value;return value;}
}
