#include "WaterProjection.hlsli"
// Draw parameters alone nearly fill D3D's 2048-byte structured stride, so the
// probe inputs live in a parallel buffer with the same index.
struct ProjectionInput {
    float4 world;
    float4 expectedPixel;
    float4 rawWorld;
};
struct ProjectionCase {
    WaterDrawParams draw;
    float4 world;
    float4 expectedPixel;
    float4 rawWorld;
};
StructuredBuffer<WaterDrawParams> draws : register(t1);
RWStructuredBuffer<float4> results : register(u2);
StructuredBuffer<ProjectionInput> inputs : register(t3);
[numthreads(1,1,1)]
void CSMain(uint index : SV_DispatchThreadID) {
    uint count,stride; inputs.GetDimensions(count,stride);
    if(index>=count)return;
    ProjectionInput input=inputs[index];
    ProjectionCase c;
    c.draw=draws[index];c.world=input.world;c.expectedPixel=input.expectedPixel;c.rawWorld=input.rawWorld;
    float4 pixel=waterPixelFromWorld(c.world.xyz,c.draw);
    float3 recovered=waterWorldFromPixel(c.expectedPixel.xy,c.expectedPixel.z,c.draw);
    float3 decoded=waterDecodeWorld(c.rawWorld,c.draw);
    float projectionError=length(pixel.xy-c.expectedPixel.xy);
    // Exercise the affine homogeneous ray projection used by SSR without
    // tying this probe to a scene's visibility, colours or reflection hits.
    float3 origin=c.world.xyz+float3(0,3,0),direction=normalize(float3(0.37,0.2,0.71));
    float4 start=mul(c.draw.viewProjection,float4(origin,1)),ray=mul(c.draw.viewProjection,float4(direction,0));
    const float distances[6]={5,16,100,250,1500,3000};
    [unroll] for(uint step=0;step<6;step++) {
        float4 direct=waterPixelFromWorld(origin+direction*distances[step],c.draw);
        float4 affine=waterPixelFromClip(start+ray*distances[step],c.draw);
        // SSR stops when a ray leaves the viewport. Near-singular, entirely
        // off-screen projections are not sampled and need no pixel tolerance.
        bool visible=all(direct.xy>=c.draw.viewBounds.xy) && all(direct.xy<c.draw.viewBounds.zw);
        if(direct.w>1 && affine.w>1 && visible)projectionError=max(projectionError,length(direct.xy-affine.xy));
    }
    results[index]=float4(projectionError,length(recovered-c.world.xyz),
        length(decoded-c.world.xyz),any(pixel.xy<c.draw.viewBounds.xy)||any(pixel.xy>=c.draw.viewBounds.zw) ? 1 : 0);
}
