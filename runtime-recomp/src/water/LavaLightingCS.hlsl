#include "WaterProjection.hlsli"
struct LavaPushConstants {uint drawIndex;uint offsetX;uint offsetY;uint reserved;};
[[vk::push_constant]] ConstantBuffer<LavaPushConstants> gConstants : register(b0);
StructuredBuffer<WaterDrawParams> waterDraws : register(t1);
Texture2D<float> sceneDepth : register(t3);
RWTexture2D<float4> lightingOutput : register(u1,space2);
#include "LavaLighting.hlsli"

uint lavaSampleDivisor(WaterMaterial m) {
    return m.shoreCounts[0].x>=12 ? 16 : m.shoreCounts[0].x>=6 ? 8 : 4;
}
[numthreads(8,8,1)]
void CSMain(uint3 thread : SV_DispatchThreadID) {
    WaterDrawParams p=waterDraws[gConstants.drawIndex];
    uint divisor=lavaSampleDivisor(p.material);
    uint2 at=uint2(gConstants.offsetX,gConstants.offsetY)+thread.xy;
    uint w,h;lightingOutput.GetDimensions(w,h);
    if(any(at>=uint2(w,h)) || any(at>=uint2(ceil(p.viewBounds.zw/divisor))))return;
    float2 pixel=clamp((float2(at)+0.5)*divisor,p.viewBounds.xy+1.5,p.viewBounds.zw-1.5);
    int2 texel=int2(pixel);
    float depth=sceneDepth.Load(int3(texel,0));
    float4 result=float4(0,0,0,1);
    if(depth<0.9999) {
        float3 world=waterWorldFromPixel(pixel,depth,p);
        float3 dx=waterWorldFromPixel(pixel+float2(1,0),sceneDepth.Load(int3(texel+int2(1,0),0)),p)-world;
        float3 dy=waterWorldFromPixel(pixel+float2(0,1),sceneDepth.Load(int3(texel+int2(0,1),0)),p)-world;
        float3 n=cross(dx,dy);float n2=dot(n,n);
        if(all(isfinite(world)) && length(dx)<=150 && length(dy)<=150 && n2>=0.000001) {
            n*=rsqrt(n2);if(dot(n,p.camera.xyz-world)<0)n=-n;
            result=float4(lavaIrradiance(world,n,p.material,p.camera.w),depth);
        }
    }
    // Every dispatched texel is written, including sky and invalid receivers.
    // No history feedback or lighting survives a camera/scene change.
    lightingOutput[at]=result;
}
