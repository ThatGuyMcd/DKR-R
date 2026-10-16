#include "WaterProjection.hlsli"
[[vk::push_constant]] ConstantBuffer<WaterPushConstants> gConstants : register(b0);
StructuredBuffer<WaterDrawParams> waterDraws : register(t1);
Texture2D<float4> sceneColor : register(t2);
Texture2D<float> sceneDepth : register(t3);
Texture2D<float4> lightingInput : register(t2,space2);

float4 PSMain(float4 position : SV_Position) : SV_Target {
    WaterDrawParams p=waterDraws[gConstants.drawIndex];
    int2 pixel=int2(position.xy);
    if(any(position.xy<p.viewBounds.xy+1) || any(position.xy>=p.viewBounds.zw-1))discard;
    float depth=sceneDepth.Load(int3(pixel,0));
    if(depth>=0.9999)discard;
    float3 world=waterWorldFromPixel(position.xy,depth,p);
    // Reconstruct only coherent receivers; never smear light across sky or
    // silhouettes. The final raster/aspect-adjusted inverse is shared with water.
    float3 dx=waterWorldFromPixel(position.xy+float2(1,0),sceneDepth.Load(int3(pixel+int2(1,0),0)),p)-world;
    float3 dy=waterWorldFromPixel(position.xy+float2(0,1),sceneDepth.Load(int3(pixel+int2(0,1),0)),p)-world;
    if(any(!isfinite(world)) || length(dx)>150 || length(dy)>150)discard;
    uint divisor=p.material.shoreCounts[0].x>=12 ? 16 : p.material.shoreCounts[0].x>=6 ? 8 : 4;
    float2 at=position.xy/divisor-0.5,f=frac(at);
    int2 baseAt=int2(floor(at));
    int2 lo=int2(floor(p.viewBounds.xy/divisor)),hi=int2(ceil(p.viewBounds.zw/divisor))-1;
    float3 irradiance=0;float total=0;
    // Reject samples across silhouettes or foreground/background boundaries.
    // Four taps, no new rays in the full-resolution composite.
    [unroll] for(uint i=0;i<4;++i) {
        int2 offset=int2(i&1,i>>1),sampleAt=clamp(baseAt+offset,lo,hi);
        float4 light=lightingInput.Load(int3(sampleAt,0));
        float2 samplePixel=clamp((float2(sampleAt)+0.5)*divisor,p.viewBounds.xy+1.5,p.viewBounds.zw-1.5);
        float3 sampleWorld=waterWorldFromPixel(samplePixel,light.a,p);
        float weight=(offset.x?f.x:1-f.x)*(offset.y?f.y:1-f.y);
        if(light.a<0.9999 && all(isfinite(sampleWorld)) &&
           distance(world,sampleWorld)<=max(8,(length(dx)+length(dy))*divisor*2)) {
            irradiance+=light.rgb*weight;total+=weight;
        }
    }
    if(total<=0.00001)discard;
    irradiance/=total;
    if(max(irradiance.r,max(irradiance.g,irradiance.b))<0.001)discard;
    float3 base=sceneColor.Load(int3(pixel,0)).rgb;
    // Scene capture is already linear. Apply a bounded one-bounce diffuse
    // response then encode once. No postprocess feedback, depth write or HUD tint.
    float3 lit=base+max(base,0.08)*irradiance;
    return float4(pow(max(lit,0),1.0/2.2),1);
}
