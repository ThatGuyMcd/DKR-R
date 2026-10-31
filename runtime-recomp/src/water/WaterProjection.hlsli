#ifndef DKR_WATER_PROJECTION_INCLUDED
#define DKR_WATER_PROJECTION_INCLUDED
#include "shared/rt64_water_params.h"

float3 waterDecodeWorld(float4 position, WaterDrawParams p) {
    if (p.material.cameraPosition.w == 1.0f) {
        position=mul(p.inverseWorldViewProjection,position);
        position.xyz/=abs(position.w)>0.000001f ? position.w : 1.0f;
    }
    return position.xyz;
}

// Exact inverses of the raster viewport + per-draw screen transform. The
// camera includes RT64's final aspect adjustment, not just the retail MVP.
float3 waterWorldFromPixel(float2 pixel, float depth, WaterDrawParams p) {
    float2 ndc=pixel/float2(p.viewportScale.w,p.viewportTranslate.w)*float2(2,-2)+float2(-1,1);
    ndc=(ndc-p.screenTransform.zw)/p.screenTransform.xy;
    float2 screen=(ndc*float2(0.5,-0.5)+0.5)*p.screen.xy;
    float3 projected=float3((screen-p.viewportTranslate.xy)/p.viewportScale.xy*float2(1,-1),
        (depth-p.viewportTranslate.z)/p.viewportScale.z);
    float4 world=mul(p.inverseViewProjection,float4(projected,1));
    return world.xyz/world.w;
}

float4 waterPixelFromClip(float4 clip, WaterDrawParams p) {
    float3 ndc=clip.xyz/clip.w;
    float3 screen=ndc*float3(1,-1,1)*p.viewportScale.xyz+p.viewportTranslate.xyz;
    float2 xy=(screen.xy/p.screen.xy*float2(2,-2)+float2(-1,1))*p.screenTransform.xy+p.screenTransform.zw;
    return float4((xy*float2(0.5,-0.5)+0.5)*float2(p.viewportScale.w,p.viewportTranslate.w),screen.z,clip.w);
}
float4 waterPixelFromWorld(float3 world, WaterDrawParams p) {
    return waterPixelFromClip(mul(p.viewProjection,float4(world,1)),p);
}
#endif
