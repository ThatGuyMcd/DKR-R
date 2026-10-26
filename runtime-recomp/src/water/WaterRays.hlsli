#pragma once
#include "WaterfallFlow.hlsli"
struct WaterRayVertex {float4 position;float4 colour;float4 liquid;};
struct WaterOpacityMask {float4 params;uint alpha[256];};
RaytracingAccelerationStructure waterWorld : register(t1,space1);
StructuredBuffer<WaterRayVertex> waterRayVertices : register(t2,space1);
StructuredBuffer<WaterOpacityMask> waterOpacityMasks : register(t3,space1);

uint waterRayFirst(uint geometry,uint primitive) {
    uint opaque=uint(waterOpacityMasks[0].params.w);
    return (primitive+(geometry>0 ? opaque : 0))*3;
}
float waterfallAlpha(uint mask,uint x,uint y) {
    uint cell=(y%32)*32+x%32;
    return ((waterOpacityMasks[mask].alpha[cell/4]>>((cell%4)*8))&255)/255.0;
}
float waterfallRayOpacity(uint first,float3 weights) {
    uint mask=uint(waterRayVertices[first].liquid.z);
    if(!mask)return 1;
    float2 at=waterRayVertices[first].liquid.xy*weights.x+waterRayVertices[first+1].liquid.xy*weights.y+
        waterRayVertices[first+2].liquid.xy*weights.z;
    float2 pixel=frac(at/waterOpacityMasks[mask].params.xy)*32;
    uint2 i=uint2(pixel)%32;float2 f=frac(pixel);
    float a=lerp(lerp(waterfallAlpha(mask,i.x,i.y),waterfallAlpha(mask,i.x+1,i.y),f.x),
        lerp(waterfallAlpha(mask,i.x,i.y+1),waterfallAlpha(mask,i.x+1,i.y+1),f.x),f.y);
    return a*dot(float3(waterRayVertices[first].colour.a,waterRayVertices[first+1].colour.a,waterRayVertices[first+2].colour.a),weights);
}

#ifndef WATER_RAY_OPACITY_ONLY
float4 hardwareWaterReflection(float3 world,float3 normal,float3 view,WaterDrawParams p,out bool waterfallHit,out bool capturedHit) {
    waterfallHit=false;capturedHit=false;
    if(p.material.effects.w<0.5)return 0;
    RayDesc ray;ray.Origin=world+normal*3;ray.Direction=reflect(-view,normal);ray.TMin=1;
    ray.TMax=p.material.performance.z>0 ? clamp(p.material.performance.z,1500,12000) : 6000;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(waterWorld,RAY_FLAG_NONE,0xFF,ray);
    while(query.Proceed()) {
        if(query.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE) {
            uint first=waterRayFirst(query.CandidateGeometryIndex(),query.CandidatePrimitiveIndex());
            float2 uv=query.CandidateTriangleBarycentrics();
            if(p.material.appearance.w>0 && waterfallRayOpacity(first,float3(1-uv.x-uv.y,uv.x,uv.y))>0.03)
                query.CommitNonOpaqueTriangleHit();
        }
    }
    if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return 0;
    uint first=waterRayFirst(query.CommittedGeometryIndex(),query.CommittedPrimitiveIndex());
    float2 uv=query.CommittedTriangleBarycentrics();float3 weights=float3(1-uv.x-uv.y,uv.x,uv.y);
    float3 hit=ray.Origin+ray.Direction*query.CommittedRayT();
    if(waterRayVertices[first].liquid.z>0) {
        waterfallHit=true;
        float3 shade=waterRayVertices[first].colour.rgb*weights.x+waterRayVertices[first+1].colour.rgb*weights.y+
            waterRayVertices[first+2].colour.rgb*weights.z;
        const uint mask=uint(waterRayVertices[first].liquid.z);
        const float2 authored=waterRayVertices[first].liquid.xy*weights.x+
            waterRayVertices[first+1].liquid.xy*weights.y+waterRayVertices[first+2].liquid.xy*weights.z;
        const float2 flow=authored/waterOpacityMasks[mask].params.xy;
        const float direction=waterRayVertices[first].liquid.w;
        // Identical normalized, immutable UV frame/time on surface and ray.
        // World axes must not turn a sloping river into a sideways waterfall.
        return float4(pow(waterfallComposite(shade,flow,p.camera.w,direction,shade,0,p.material.appearance.w),2.2),
            saturate(1-query.CommittedRayT()/(ray.TMax*(4.0/3.0)))*waterfallRayOpacity(first,weights));
    }
    // Prefer the exact original combiner/replacement texture/fog result when
    // this hit is visible. Off-screen static geometry uses its authored vertex
    // colours; no invented texture or foreground pixel is pulled into it.
    float4 projected=waterPixelFromWorld(hit,p);
    if(projected.w>0 && all(projected.xy>=p.viewBounds.xy+2) && all(projected.xy<p.viewBounds.zw-2)) {
        float depth=sceneDepth.Load(int3(int2(projected.xy),0));
        if(depth<0.9999 && length(waterWorldFromPixel(projected.xy,depth,p)-hit)<45) {
            capturedHit=true;
            return float4(backgroundColor(projected.xy,p),1);
        }
    }
    float3 authored=waterRayVertices[first].colour.rgb*weights.x+
        waterRayVertices[first+1].colour.rgb*weights.y+waterRayVertices[first+2].colour.rgb*weights.z;
    return float4(pow(saturate(authored),2.2)*0.45,saturate(1-query.CommittedRayT()/(ray.TMax*(4.0/3.0))));
}
#endif
