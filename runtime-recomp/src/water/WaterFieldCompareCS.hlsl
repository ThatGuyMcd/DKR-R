// Headless-only reference comparison; not submitted by the game.
struct ProbeParams { uint width; uint height; uint phase; uint unused; };
[[vk::push_constant]] ConstantBuffer<ProbeParams> params : register(b0);
Texture2D<float4> referenceField : register(t1);
Texture2D<float4> optimizedField : register(t2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> initialField : register(u3);
RWStructuredBuffer<float4> errors : register(u4);
groupshared float4 groupErrors[64];
[numthreads(8,8,1)]
void CSMain(uint2 pixel : SV_DispatchThreadID, uint lane : SV_GroupIndex, uint2 group : SV_GroupID) {
    bool valid=all(pixel<uint2(params.width,params.height));
    if(params.phase==0) {
        if(valid) initialField[pixel]=float4(float((pixel.x+pixel.y)%7)*0.002,
            float(pixel.x%5)*0.01,float(pixel.y%11)*0.012,float((pixel.x+pixel.y)%13)*0.02);
        return;
    }
    float4 error=0;
    if(valid) {
        float4 a=referenceField.Load(int3(pixel,0)),b=optimizedField.Load(int3(pixel,0));
        error=abs(a-b);
        if(any(isnan(a)) || any(isnan(b)) || any(isinf(a)) || any(isinf(b))) error=10000;
    }
    groupErrors[lane]=error;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for(uint offset=32;offset>0;offset>>=1) {
        if(lane<offset) groupErrors[lane]=max(groupErrors[lane],groupErrors[lane+offset]);
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane==0) errors[group.y*((params.width+7)/8)+group.x]=groupErrors[0];
}
