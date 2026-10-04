#pragma once

#include "local_scenery_policy.hpp"
#include "local_scenery_arena.hpp"
#include "netplay/local_scenery.hpp"
#include <array>
#include <bit>
#include <cstring>

namespace dkr::runtime::local_scenery {
inline constexpr std::size_t kMaxViewPlans=8; // Four viewports, two world boundaries.
// A workload-local selection, not a scene cache or simulation visibility list.
// The same selection controls resource copies and both typed native draw passes.
// Fixed storage prevents a new sort/allocation for every split-screen viewport.
struct Candidate {
    std::uint16_t index=0,frame=0;
    unsigned alpha=0;
    float squared_distance=0;
};
struct ViewPlan {
    std::array<Candidate,netplay::experimental::LocalSceneryCapture::kMaxSprites> candidates{};
    std::size_t count=0;
};

inline ViewPlan plan_view(const netplay::experimental::LocalSceneryScene& scene,
        const dkr_owned_draw_event& event,const Settings& settings) noexcept {
    ViewPlan plan;
    if(event.kind!=DKR_OWNED_LOCAL_SCENERY || !dkr_owned_draw_event_valid(&event) ||
       event.parameters[0]!=scene.scene || !settings.enabled(int(event.parameters[1])) ||
       scene.sprites.size()>plan.candidates.size())return plan;
    const auto aspect=std::bit_cast<float>(event.parameters[7]);
    std::array<float,3> camera;
    for(unsigned i=0;i<camera.size();++i) {
        camera[i]=std::bit_cast<float>(event.parameters[2+i]);
        if(!std::isfinite(camera[i]))return plan;
    }
    if(!std::isfinite(aspect)||aspect<0.1F||aspect>8)return plan;
    for(std::size_t index=0;index<scene.sprites.size();++index) {
        const auto& sprite=scene.sprites[index];
        if(sprite.transform_flags&(0x5000U|0x80U|(0x200U<<(event.token&1U))) ||
           !region_visible(sprite.segment,event,settings.retention))continue;
        float length2=0;
        bool position_valid=true;
        for(unsigned axis=0;axis<camera.size();++axis) {
            const auto position=sprite.position[axis];
            position_valid&=std::isfinite(position)&&position>=-32768&&position<=32767;
            const auto difference=position-camera[axis];length2+=difference*difference;
        }
        if(!position_valid)continue;
        const auto alpha=fade_alpha(settings.distance(sprite.draw_distance),length2);
        if(!alpha || unsigned(alpha<255||((sprite.flags|sprite.transform_flags)&4U)!=0)!=event.parameters[15])continue;
        const std::size_t frame=event.parameters[6]?sprite.animation:
            (sprite.animation&255U)*sprite.frames.size()/256;
        if(frame>=sprite.frames.size() || frame>UINT16_MAX)continue;
        plan.candidates[plan.count++]={std::uint16_t(index),std::uint16_t(frame),alpha,length2};
    }
    std::sort(plan.candidates.begin(),plan.candidates.begin()+plan.count,[](const auto& left,const auto& right) {
        return left.squared_distance!=right.squared_distance?
            left.squared_distance>right.squared_distance:left.index<right.index;
    });
    return plan;
}

struct TextureBinding {
    const netplay::experimental::LocalSceneryTexture* texture=nullptr;
    std::uint32_t address=0;
};
// This workspace is private decoder RAM. Only its native texture arena is
// written; the first 8 MiB (canonical guest commands/data) stays untouched.
// Bindings borrow the already retained immutable scene, not guest allocations.
// A false return disables the OPTIONAL pass; partially written native scratch
// is never submitted. The caller catches allocation failures as well.
inline bool copy_view_textures(const netplay::experimental::LocalSceneryScene& scene,
        std::span<const ViewPlan> plans,std::span<std::uint8_t> decoder,
        std::vector<TextureBinding>& bindings,std::uint64_t& copied) {
    bindings.clear();copied=0;
    if(decoder.size()<kTextureEnd || plans.size()>kMaxViewPlans ||
       scene.sprites.size()>netplay::experimental::LocalSceneryCapture::kMaxSprites)return false;
    auto cursor=kTextureBegin;
    for(const auto& plan:plans) {
        if(plan.count>plan.candidates.size())return false;
        for(std::size_t item=0;item<plan.count;++item) {
            const auto& candidate=plan.candidates[item];
            if(candidate.index>=scene.sprites.size())return false;
            const auto& sprite=scene.sprites[candidate.index];
            if(candidate.frame>=sprite.frames.size())return false;
            for(const auto& tile:sprite.frames[candidate.frame].tiles) {
                if(tile.texture>=sprite.textures.size())return false;
                const auto& texture=sprite.textures[tile.texture];
                if(!texture || texture->bytes.empty() || texture->bytes.size()>kTextureEnd-kTextureBegin)return false;
                const auto prior=std::find_if(bindings.begin(),bindings.end(),[&](const auto& binding){return binding.texture==texture.get();});
                if(prior!=bindings.end())continue;
                const auto aligned=(texture->bytes.size()+7U)&~std::size_t(7U);
                if(aligned>kTextureEnd-cursor)return false;
                bindings.push_back({texture.get(),cursor});
                std::memcpy(decoder.data()+cursor,texture->bytes.data(),texture->bytes.size());
                copied+=texture->bytes.size();cursor+=std::uint32_t(aligned);
            }
        }
    }
    return true;
}
}
