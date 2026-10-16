#pragma once
#include "WaterWorkPolicy.h"
#include "hle/rt64_workload.h"
#include "shared/rt64_water_params.h"
#include <limits>

namespace RT64 {
// Mirror the RSP position path, including velocity interpolation and final
// widescreen raster mapping. Anything uncertain uses the whole scissor.
inline water_work::Region waterOriginalRegion(const DrawData& data,uint32_t start,uint32_t count,
    const interop::WaterDrawParams& p,float weight) {
    const auto full=water_work::captureRegion(uint32_t(p.screen.z),uint32_t(p.screen.w),
        p.viewBounds.x,p.viewBounds.y,p.viewBounds.z,p.viewBounds.w);
    float left=std::numeric_limits<float>::infinity(),top=left,right=-left,bottom=-left;
    if(!count || uint64_t(start)+count>data.faceIndices.size())return full;
    const auto& world=data.lerpWorldTransforms.empty() ? data.worldTransforms : data.lerpWorldTransforms;
    for(uint32_t i=0;i<count;i++) {
        const auto v=data.faceIndices[start+i];
        if(uint64_t(v)*3+2>=data.posFloats.size() || v>=data.worldIndices.size() || v>=data.viewProjIndices.size())return full;
        const auto wi=data.worldIndices[v],vi=data.viewProjIndices[v];
        if(wi>=world.size() || vi>=data.modViewProjTransforms.size() || vi>=data.rspViewports.size())return full;
        hlslpp::float4 pos(data.posFloats[v*3],data.posFloats[v*3+1],data.posFloats[v*3+2],1);
        if(uint64_t(v)*3+2<data.velFloats.size()) {
            pos.x-=data.velFloats[v*3]*(1-weight);pos.y-=data.velFloats[v*3+1]*(1-weight);pos.z-=data.velFloats[v*3+2]*(1-weight);
        }
        const auto clip=hlslpp::mul(hlslpp::mul(pos,hlslpp::float4x4(world[wi])),
            hlslpp::float4x4(data.modViewProjTransforms[vi]));
        if(!std::isfinite(float(clip.w)) || float(clip.w)<=0.00001F)return full;
        const auto& vp=data.rspViewports[vi];
        const float x=float(clip.x/clip.w)*vp.scale.x+vp.translate.x;
        const float y=-float(clip.y/clip.w)*vp.scale.y+vp.translate.y;
        const float px=(((x/p.screen.x*2-1)*p.screenTransform.x+p.screenTransform.z)*0.5F+0.5F)*p.viewportScale.w;
        const float py=(((y/p.screen.y*-2+1)*p.screenTransform.y+p.screenTransform.w)*-0.5F+0.5F)*p.viewportTranslate.w;
        if(!std::isfinite(px)||!std::isfinite(py))return full;
        left=std::min(left,px);top=std::min(top,py);right=std::max(right,px);bottom=std::max(bottom,py);
    }
    // Guard pixel/sample coverage and small CPU/GPU floating-point differences.
    const auto result=water_work::captureRegion(uint32_t(p.screen.z),uint32_t(p.screen.w),
        std::max(left-8,p.viewBounds.x),std::max(top-8,p.viewBounds.y),
        std::min(right+8,p.viewBounds.z),std::min(bottom+8,p.viewBounds.w));
    return result.width && result.height ? result : full;
}
}
