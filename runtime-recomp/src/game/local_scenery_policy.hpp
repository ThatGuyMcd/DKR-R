#pragma once

#include "modern_camera_policy.hpp"
#include "netplay/experimental_draw_events.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dkr::runtime::local_scenery {
// One immutable preference snapshot per decoded workload, not per draw or
// sprite. A live slider change must not split the opaque/faded passes across
// different ranges and draw a billboard twice (or omit it from both).
struct Settings {
    enhancements::PresentationProfile profile=enhancements::PresentationProfile::Accurate;
    enhancements::SceneryRetentionMode retention=enhancements::SceneryRetentionMode::Authored;
    int view_distance=1;
    bool keep_hub=false,keep_track=false,keep_minigame=false;
    constexpr bool enabled(int race_type) const {
        return enhancements::scenery_retention_enabled_for_race_type(
            profile,race_type,keep_hub,keep_track,keep_minigame);
    }
    constexpr int distance(int authored) const {
        return enhancements::effective_view_distance(profile,authored,view_distance);
    }
};
// Same regional gate as the existing enhancement hooks, applied only to
// copied native visuals. Authored/Current region retain actual BSP visibility;
// Visible + adjacent/Full forward relax that gate, not depth or forward culling.
constexpr bool region_visible(int segment,const dkr_owned_draw_event& view,
        enhancements::SceneryRetentionMode mode) {
    if(segment==-1)return true; // Retail objectsVisible[0].
    if(segment<0||unsigned(segment)>=view.parameters[8])return false;
    if(mode==enhancements::SceneryRetentionMode::VisibleAndAdjacent||
       mode==enhancements::SceneryRetentionMode::FullForwardView)return true;
    return (view.parameters[9+unsigned(segment)/32]&(1U<<(unsigned(segment)&31U)))!=0;
}
inline unsigned fade_alpha(int distance,float squared_distance) {
    if(!std::isfinite(squared_distance)||squared_distance<0)return 0;
    if(distance<=0)return 255;
    if(squared_distance>float(distance)*distance)return 0;
    // Most admitted sprites are fully opaque. No square root is needed until
    // the last 20% of the range; the boundary result remains the retail fade.
    const float opaque_distance=float(distance)*0.8F;
    if(squared_distance<=opaque_distance*opaque_distance)return 255;
    // Retail fades in its final 20%, retaining alpha=1 at the admitted edge.
    return unsigned(std::clamp((float(distance)-std::sqrt(squared_distance))/
        (float(distance)*0.2F)*255.0F,1.0F,255.0F));
}

inline constexpr std::uint32_t kNativeIdentityBit=0x80000000U;
// The exact placement/camera bits cannot collide within one scene. Scene and
// camera continuity affect only the prefix. Canonical owned groups use the
// lower namespace at their final selector, not in checkpointed metadata.
constexpr std::uint32_t native_identity(std::uint32_t prefix,unsigned placement,unsigned camera) {
    return kNativeIdentityBit|((prefix&0x001FFFFFU)<<10)|((camera&3U)<<8)|(placement+1U);
}
constexpr std::uint32_t canonical_identity(std::uint32_t id) {
    if(id==0||id==UINT32_MAX)return id;
    const auto result=id&~kNativeIdentityBit;
    return result?result:0x7FFFFFFFU;
}
static_assert((native_identity(0,0,0)&kNativeIdentityBit)!=0);
static_assert(native_identity(0,127,3)!=UINT32_MAX);
static_assert((canonical_identity(0x80001234U)&kNativeIdentityBit)==0);
static_assert(canonical_identity(0)==0&&canonical_identity(UINT32_MAX)==UINT32_MAX);
}
