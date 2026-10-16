// Headless qualification only. No scene, window, ROM or gameplay input.
#define WATER_RAY_OPACITY_ONLY
#include "WaterRays.hlsli"
#include "LavaFlow.hlsli"
#include "shared/rt64_water_params.h"
#include "LavaLighting.hlsli"
RWStructuredBuffer<uint4> results : register(u1,space0);
[numthreads(4,1,1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if(id.x>=4) {
        const uint sample=id.x-4;
        if(sample>=128*128)return;
        float2 world=(float2(sample%128,sample/128)+0.5)*4-256;
        // Read back the actual runtime material and display transform, not
        // a CPU approximation. Covers whole pools instead of a lucky point.
        float4 now=lavaSurface(world,4,4*LAVA_WORLD_SCALE);
        float4 next=lavaSurface(world,4+1.0/120,4*LAVA_WORLD_SCALE);
        float4 evolved=lavaSurface(world,6,4*LAVA_WORLD_SCALE);
        results[19+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(now.rgb,0),0,0),now.a));
        results[20+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(next.rgb,0),0,0),next.a));
        results[21+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(evolved.rgb,0),0,0),evolved.a));
        results[22+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(now.rgb,0.35),0,0),now.a));
        results[23+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(now.rgb,1),0,0),now.a));
        results[24+sample*6]=asuint(float4(lavaDisplayColor(lavaHeatPalette(now.rgb,1)*0.25,0,0),now.a));
        return;
    }
    RayDesc ray;
    ray.Origin=float3(id.x==1 ? 20 : id.x==3 ? -0.5 : 0,id.x==3 ? -0.5 : 0,0);
    ray.Direction=float3(0,0,id.x==2 ? -1 : 1);
    ray.TMin=0.01;ray.TMax=100;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(waterWorld,RAY_FLAG_NONE,0xFF,ray);
    while(query.Proceed()) {
        if(query.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE) {
            float2 uv=query.CandidateTriangleBarycentrics();
            if(waterfallRayOpacity(waterRayFirst(query.CandidateGeometryIndex(),query.CandidatePrimitiveIndex()),
                float3(1-uv.x-uv.y,uv.x,uv.y))>0.03)query.CommitNonOpaqueTriangleHit();
        }
    }
    bool hit=query.CommittedStatus()==COMMITTED_TRIANGLE_HIT;
    results[id.x]=hit ? uint4(1,uint(round(query.CommittedRayT()*1000)),query.CommittedPrimitiveIndex(),query.CommittedGeometryIndex()) : 0;
    if(id.x==0) {
        results[4]=asuint(float4(waterfallFilm(float2(0.19,0.43),4.0,1,float3(1,1,1),0),1));
        results[5]=asuint(float4(waterfallFilm(float2(0.19,0.43),4.0+1.0/120,1,float3(1,1,1),0),1));
        results[6]=asuint(float4(waterfallFilm(float2(2.19,-2.57),4.0,1,float3(1,1,1),0),1));
        const float delta=(3.1/6.28318530718)/120;
        results[7]=asuint(float4(waterfallFilm(float2(0.19,0.43+delta),4.0+1.0/120,1,float3(1,1,1),0),1));
        results[8]=asuint(float4(waterfallFilm(float2(0.19,0.43),4.0,-1,float3(1,1,1),0),1));
        results[9]=asuint(float4(waterfallFilm(float2(0.19,0.43-delta),4.0+1.0/120,-1,float3(1,1,1),0),1));
        results[10]=asuint(float4(waterfallComposite(float3(0.2,0.4,0.6),float2(0,0),4,0,float3(1,1,1),0,1),1));
        results[11]=asuint(lavaSurface(float2(37,91),4,0));
        results[12]=asuint(lavaSurface(float2(37,91),4+1.0/120,0));
        results[13]=asuint(lavaSurface(float2(37,91)+LAVA_FLOW/120,4+1.0/120,0));
        results[14]=asuint(lavaSurface(float2(37,91),4,10));
        WaterMaterial light=(WaterMaterial)0;
        light.sunDirection.w=1;light.skyColor.w=1;light.optics.y=1;
        light.shoreSegments[0]=float4(0,0,20,100);
        results[15]=asuint(float4(lavaIrradiance(0,float3(0,0,1),light,0),1));
        light.shoreSegments[0].x=20;
        results[16]=asuint(float4(lavaIrradiance(float3(20,0,0),float3(0,0,1),light,0),1));
        light.sunColor.w=0.6;
        results[17]=asuint(float4(lavaIrradiance(float3(20,0,0),float3(0,0,1),light,6.28318530718/1.7),1));
        light.sunDirection.w=0;
        results[18]=asuint(float4(lavaIrradiance(float3(20,0,0),float3(0,0,1),light,0),1));
    }
}
