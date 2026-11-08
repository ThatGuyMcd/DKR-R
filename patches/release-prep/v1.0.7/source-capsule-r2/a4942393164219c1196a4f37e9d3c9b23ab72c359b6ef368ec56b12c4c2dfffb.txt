#pragma once
#include "enhanced_water_rt64.hpp"
#include "polish_symbols.hpp"
#include "rice_texture_pack_policy.hpp"
#include "render/rt64_texture_cache.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace dkr::runtime::water {
// Animated liquids revisit different Rice identities every few frames. Loading
// only the currently visible frame makes the first complete cycle alternate
// between HD frames and original artwork. Warm the validated liquid family in
// one background request batch, not one request per visible frame.
// Never synchronously wait, upload dummy images, force optical effects, alter
// pack files/priority or retain textures beyond the normal replacement budget.
class LiquidResidency {
    RT64::TextureCache* cache_=nullptr;
    std::uint64_t generation_=0;
    std::uint32_t scene_=~0U,loaded_=0,model_=0;
    unsigned queued_=0;
    std::unordered_set<std::uint64_t> warmed_;
    static std::uint32_t crc(const std::uint8_t* source,unsigned width,unsigned height,unsigned size) {
        const unsigned stride=width<<size>>1;
        std::uint32_t result=0;
        for(int y=int(height)-1;y>=0;--y) {
            std::uint32_t value=0;
            for(int x=int(stride)-4;x>=0;x-=4) {
                std::memcpy(&value,source+x,4);value^=std::uint32_t(x);
                result=(result<<4)+((result>>28)&15);result+=value;
            }
            value^=std::uint32_t(y);result+=value;source+=stride;
        }
        return result;
    }
public:
    void prepare(RT64::State& state,const enhanced::Registry& registry,std::uint32_t scene,bool original) {
        auto* cache=state.ext.textureCache;
        if(!cache)return;
        const auto generation=cache->getReplacementGeneration();
        enhanced::Memory m(std::span<const std::uint8_t>(state.RDRAM,0x800000));
        const auto loaded=m.u32(revision_addresses::NumberOfLoadedTextures),model=m.u32(enhanced::level_model_symbol());
        if(cache!=cache_ || generation!=generation_ || scene!=scene_) {
            cache_=cache;generation_=generation;scene_=scene;loaded_=~0U;model_=0;queued_=0;warmed_.clear();
        }
        if(!original || (loaded==loaded_ && model==model_))return;
        loaded_=loaded;model_=model;
        {
            std::scoped_lock lock(cache->textureMapMutex);
            if(cache->textureMap.replacementMap.fileSystems.empty())return;
        }
        std::vector<std::uint32_t> headers=registry.liquid_headers();
        const auto& symbols=presentation::polish_symbols[revision_addresses::CurrentMenuId==revision_addresses::kUsV80.CurrentMenuId?1:0];
        const auto waves=m.u32(symbols[7]);
        if(waves && m.valid(waves,32))headers.push_back(waves);
        std::vector<RT64::ReplacementResolvedPath> batch;
        unsigned hashed=0;
        for(auto first:headers) {
            if(!first || !m.valid(first,32))continue;
            const unsigned frames=std::max(1U,unsigned(m.u16(first+0x12)>>8));
            const unsigned stride=m.u16(first+0x16);
            // Static surfaces do not alternate frame identities. Leave them
            // on the normal streaming path, along with unsupported formats.
            if(frames<2 || frames>64 || stride<32 || !m.valid(first,std::uint64_t(frames)*stride))continue;
            for(unsigned frame=0;frame<frames && queued_+batch.size()<128 && hashed<512;++frame) {
                const auto header=first+frame*stride;
                const unsigned width=m.u8(header),height=m.u8(header+1),format=m.u8(header+2)&15U;
                constexpr std::array<unsigned,7> formats{0,0,4,4,3,3,3},sizes{3,2,1,0,2,1,0};
                if(format>=formats.size() || !width || !height || (m.u16(header+6)&4))continue;
                const unsigned size=sizes[format],row=width<<size>>1;
                const auto bytes=std::uint64_t(row)*height;
                if(row<4 || (row&3) || !bytes || bytes>stride-32 || bytes>65536 || !m.valid(header+32,bytes))continue;
                ++hashed;
                const auto crc_value=crc(state.RDRAM+((header+32)&0x7FFFFF),width,height,size);
                char identity[40];std::snprintf(identity,sizeof(identity),"%08x#%u#%u",crc_value,formats[format],size);
                const auto alias=rice_texture::native_alias(identity);
                if(!warmed_.insert(alias).second)continue;
                std::scoped_lock lock(cache->textureMapMutex);
                auto& map=cache->textureMap.replacementMap;
                for(auto layer=map.fileSystemResolvedPaths.rbegin();layer!=map.fileSystemResolvedPaths.rend();++layer) {
                    const auto it=layer->find(alias);
                    if(it==layer->end())continue;
                    const auto& path=it->second;
                    // Reuse normal stream-set deduplication on the upload
                    // worker. Never mix a direct load with an already queued
                    // stream of the same path (the pool forbids duplicates).
                    // Author-selected Stall/Preload policies stay untouched.
                    if(path.resolvedOperation==RT64::ReplacementOperation::Stream &&
                       !map.getFromRelativePath(path.fileSystemIndex,path.relativePath))
                        batch.push_back(path);
                    break;
                }
            }
        }
        if(!batch.empty()) {
            queued_+=unsigned(batch.size());
            {
                std::scoped_lock lock(cache->uploadQueueMutex);
                cache->resolvedPathQueue.insert(cache->resolvedPathQueue.end(),batch.begin(),batch.end());
            }
            cache->uploadQueueChanged.notify_all();
            std::fprintf(stderr,"[water][residency] scene=%u warmed-liquid-frames=%zu; asynchronous family batch; original optics off\n",scene,batch.size());
        }
    }
};
inline LiquidResidency& residency(){static thread_local LiquidResidency value;return value;}
}
