#include "WaterProjection.hlsli"
struct ProjectionCase {
    float4 world;
    float4 expectedPixel;
    float4 rawWorld;
};
StructuredBuffer<ProjectionCase> cases : register(t1);
StructuredBuffer<WaterDrawParams> draws : register(t3);
RWStructuredBuffer<float4> results : register(u2);
[numthreads(1,1,1)]
void CSMain(uint index : SV_DispatchThreadID) {
    uint count,stride; cases.GetDimensions(count,stride);
    if(index>=count)return;
    ProjectionCase c=cases[index];
    WaterDrawParams p=draws[index];
    float4 pixel=waterPixelFromWorld(c.world.xyz,p);
    float3 recovered=waterWorldFromPixel(c.expectedPixel.xy,c.expectedPixel.z,p);
    float3 decoded=waterDecodeWorld(c.rawWorld,p);
    float projectionError=length(pixel.xy-c.expectedPixel.xy);
    // Exercise the affine homogeneous ray projection used by SSR without
    // tying this probe to a scene's visibility, colours or reflection hits.
    float3 origin=c.world.xyz+float3(0,3,0),direction=normalize(float3(0.37,0.2,0.71));
    float4 start=mul(p.viewProjection,float4(origin,1)),ray=mul(p.viewProjection,float4(direction,0));
    const float distances[6]={5,16,100,250,1500,3000};
    [unroll] for(uint step=0;step<6;step++) {
        float4 direct=waterPixelFromWorld(origin+direction*distances[step],p);
        float4 affine=waterPixelFromClip(start+ray*distances[step],p);
        // SSR stops when a ray leaves the viewport. Near-singular, entirely
        // off-screen projections are not sampled and need no pixel tolerance.
        bool visible=all(direct.xy>=p.viewBounds.xy) && all(direct.xy<p.viewBounds.zw);
        if(direct.w>1 && affine.w>1 && visible)projectionError=max(projectionError,length(direct.xy-affine.xy));
    }
    results[index]=float4(projectionError,length(recovered-c.world.xyz),
        length(decoded-c.world.xyz),any(pixel.xy<p.viewBounds.xy)||any(pixel.xy>=p.viewBounds.zw) ? 1 : 0);
}
