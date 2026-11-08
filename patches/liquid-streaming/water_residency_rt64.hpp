#pragma once
#include "enhanced_water_rt64.hpp"
#include "polish_symbols.hpp"
#include "rice_texture_pack_policy.hpp"
#include "liquid_replacement_gate.hpp"
#include "performance_observation.hpp"
#include "render/rt64_texture_cache.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

namespace dkr::runtime::water {
class LiquidResidency {
    RT64::TextureCache* cache_=nullptr;
    std::uint64_t generation_=0;
    std::uint32_t scene_=~0U,loaded_=~0U,model_=0;
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
public:
    void prepare(RT64::State& state,const enhanced::Registry& materials,std::uint32_t scene,bool original) {
        auto* cache=state.ext.textureCache;if(!cache)return;
        const auto generation=cache->getReplacementGeneration();
        enhanced::Memory m(std::span<const std::uint8_t>(state.RDRAM,0x800000));
        const auto loaded=m.u32(revision_addresses::NumberOfLoadedTextures),model=m.u32(enhanced::level_model_symbol());
        if(cache!=cache_ || generation!=generation_ || scene!=scene_) {
            cache_=cache;generation_=generation;scene_=scene;loaded_=~0U;model_=0;
        }
        if(!original || (loaded==loaded_ && model==model_))return;
        surgical::Scope timing(surgical::Region::LiquidWarmup);
        // Do not consume the scan before the replacement set is installed.
        std::unique_lock map_lock(cache->textureMapMutex);
        auto& map=cache->textureMap.replacementMap;
        if(map.fileSystems.empty())return;
        loaded_=loaded;model_=model;
        struct Header {std::uint32_t address;bool square;};
        std::vector<Header> headers;
        for(const auto address:materials.liquid_headers())headers.push_back({address,false});
        const auto& symbols=presentation::polish_symbols[revision_addresses::CurrentMenuId==revision_addresses::kUsV80.CurrentMenuId?1:0];
        if(const auto waves=m.u32(symbols[7]);waves && m.valid(waves,32))headers.push_back({waves,true});
        std::vector<RT64::ReplacementResolvedPath> batch;
        std::set<std::pair<std::uint32_t,std::string>> requested;
        unsigned inspected=0,registered=0,incomplete=0,unsupported=0;
        for(const auto source:headers) {
            const auto first=source.address;
            if(!first || (first&3U) || !m.valid(first,32))continue;
            const unsigned count=std::max(1U,unsigned(m.u16(first+0x12)>>8));
            const unsigned stride=m.u16(first+0x16);
            if(count<2)continue;
            if(count>64 || stride<32 || !m.valid(first,std::uint64_t(count)*stride) || inspected+count>512) {++unsupported;continue;}
            std::vector<streaming::Member> members;
            std::vector<RT64::ReplacementResolvedPath> paths;
            bool valid=true,complete=true;unsigned matched=0;
            for(unsigned frame=0;frame<count;++frame) {
                const auto header=first+frame*stride;
                const unsigned width=m.u8(header),height=source.square ? width : m.u8(header+1),format=m.u8(header+2)&15U;
                constexpr std::array<unsigned,7> formats{0,0,4,4,3,3,3},sizes{3,2,1,0,2,1,0};
                // Runtime bit 0x4 means SEMI_TRANSPARENT, not interlaced.
                // Rice hashes raw word-swapped texels, including LINE_SWAP.
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
                    const auto& path=at->second;
                    // Respect the existing pack order/operation. This queues
                    // ordinary streaming work, never a new synchronous load.
                    member.layer=path.fileSystemIndex;member.path=path.relativePath;
                    if(path.resolvedOperation==RT64::ReplacementOperation::Stream &&
                       !map.getFromRelativePath(path.fileSystemIndex,path.relativePath))paths.push_back(path);
                    found=true;++matched;break;
                }
                complete &= found;members.push_back(std::move(member));
            }
            if(!valid) {++unsupported;continue;}
            if(!matched)continue; // No HD water in these packs: normal native path.
            if(!complete)++incomplete;
            if(streaming::register_family(&map,std::move(members),complete))++registered;
            for(const auto& path:paths)if(batch.size()<128 && requested.emplace(path.fileSystemIndex,path.relativePath).second)batch.push_back(path);
        }
        // Capture sees only map metadata protected by textureMapMutex. Release
        // it before taking uploadQueueMutex: never reverse the worker lock order.
        map_lock.unlock();
        if(!batch.empty()) {
            std::scoped_lock queue_lock(cache->uploadQueueMutex);
            cache->resolvedPathQueue.insert(cache->resolvedPathQueue.end(),batch.begin(),batch.end());
        }
        if(!batch.empty())cache->uploadQueueChanged.notify_all();
        std::fprintf(stderr,"[water][residency] scene=%u headers=%zu identities=%u families=%u queued=%zu incomplete=%u unsupported=%u; asynchronous family batch; original optics off; complete-family selection\n",
            scene,headers.size(),inspected,registered,batch.size(),incomplete,unsupported);
    }
};
inline LiquidResidency& residency(){static thread_local LiquidResidency value;return value;}
}
