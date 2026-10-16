#include "shared/rt64_water_params.h"
[[vk::push_constant]] ConstantBuffer<WaterFieldPushConstants> params : register(b0);
StructuredBuffer<WaterDrawParams> waterDraws : register(t1);
Texture2D<float4> previousField : register(t2);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> nextField : register(u3);

float4 source(int2 pixel,uint tile) {
    if (params.reset || any(pixel<0) || any(pixel>=int(params.resolution))) return 0;
    return previousField.Load(int3(pixel+int2(tile*params.resolution,0),0));
}
float4 filteredSource(float2 pixel,uint tile) {
    int2 p=int2(floor(pixel)); float2 f=frac(pixel);
    return lerp(lerp(source(p,tile),source(p+int2(1,0),tile),f.x),
        lerp(source(p+int2(0,1),tile),source(p+1,tile),f.x),f.y);
}
float segmentDistance(float2 p,float2 a,float2 b) {
    float2 d=b-a;
    return length(p-(a+d*saturate(dot(p-a,d)/max(dot(d,d),0.001))));
}
[numthreads(8,8,1)]
void CSMain(uint2 pixel : SV_DispatchThreadID) {
    if (pixel.x>=params.resolution*WATER_CRAFT_COUNT || pixel.y>=params.resolution) return;
    uint tile=pixel.x/params.resolution;
    uint2 local=uint2(pixel.x%params.resolution,pixel.y);
    WaterDrawParams draw=waterDraws[params.drawIndex];
    WaterMaterial m=draw.material;
    float cell=params.simulation.w/params.resolution;
    float dt=params.simulation.x;
    float2 world=draw.fieldOrigins[tile].xy+(float2(local)+0.5)*cell;
    float2 previousPixel=(world-draw.fieldOrigins[tile].zw)/cell-0.5;
    float4 center=filteredSource(previousPixel,tile);
    float4 neighbors=filteredSource(previousPixel+float2(1,0),tile)+filteredSource(previousPixel-float2(1,0),tile)
        +filteredSource(previousPixel+float2(0,1),tile)+filteredSource(previousPixel-float2(0,1),tile);
    // A damped wave equation (CFL <= 0.25 at the supported resolutions).
    float velocity=center.y+dt*(pow(120/cell,2)*(neighbors.x-4*center.x)-2.5*center.y);
    float height=center.x+dt*velocity;
    float2 wind=m.surface.yz*8;
    float foam=filteredSource(previousPixel-wind*dt/cell,tile).z;
    foam=lerp(foam,neighbors.z*0.25,min(dt*0.55,1))*exp(-dt*0.32);
    float interval=max(m.animation.y-m.animation.x,0.00001);
    float a=saturate((params.simulation.y-m.animation.x)/interval);
    float b=saturate((params.simulation.z-m.animation.x)/interval);
    // A separate persistent shore channel is anchored to original collision
    // geometry. Do not advect this mask with the free-moving wake foam.
    float shore=center.w*exp(-dt*0.75);
    float2 craftPosition=lerp(m.craftPrevious[tile].xz,m.craftCurrent[tile].xz,b);
    float influence=1-smoothstep(1200,1500,length(world-craftPosition));
    if (influence>0 && m.craftCurrent[tile].w>=0) {
        float shoreDistance=10000;
        [loop] for (uint s=0;s<min(uint(m.shoreCounts[tile/4][tile%4]),uint(WATER_SHORE_SEGMENTS_PER_TILE));s++) {
            float4 segment=m.shoreSegments[tile*WATER_SHORE_SEGMENTS_PER_TILE+s];
            if (any(world<min(segment.xy,segment.zw)-64) || any(world>max(segment.xy,segment.zw)+64)) continue;
            shoreDistance=min(shoreDistance,segmentDistance(world,segment.xy,segment.zw));
        }
        float width=max(22,cell*1.5);
        float mask=1-smoothstep(width*0.25,width,shoreDistance);
        float wash=0.35+0.65*saturate(sin(params.simulation.z*1.7+world.x*0.003+world.y*0.002));
        shore += mask*influence*wash*(1-shore)*dt*0.9;
    }
    [unroll] for (uint i=0;i<WATER_CRAFT_COUNT;i++) {
        float4 old=m.craftPrevious[i], current=m.craftCurrent[i];
        if (current.w<0) continue;
        float2 from=lerp(old.xz,current.xz,a), to=lerp(old.xz,current.xz,b);
        float traveled=length(to-from);
        float2 delta=current.xz-old.xz;
        float speed=length(delta)/interval;
        float2 heading=delta/max(length(delta),0.001);
        float contact=saturate(current.w*3);
        float density=min(traveled/cell,2)*contact;
        float d=segmentDistance(world,from,to);
        float centerTrail=exp(-pow(d/18,2));
        float2 relative=world-to;
        float behind=-dot(relative,heading), side=abs(dot(relative,float2(-heading.y,heading.x)));
        float wing=exp(-pow((side-behind*0.42)/10,2))*smoothstep(0,24,behind)*(1-smoothstep(65,100,behind));
        foam += (centerTrail*0.30+wing*0.11)*density*saturate(speed/180);
        velocity += wing*density*0.13;
        // A landing is consumed only by the fixed step crossing this update.
        bool landed=old.w==0 && current.w>0 && params.simulation.y<m.animation.y && params.simulation.z>=m.animation.y;
        if (landed && !params.reset) {
            float impact=saturate((old.y-current.y)/interval/180);
            float radius=length(world-current.xz);
            float ring=exp(-pow((radius-32)/20,2))*impact;
            foam += ring*0.65;
            velocity += ring*0.7;
        }
    }
    if (m.craftCurrent[tile].w<0) { height=0;velocity=0;foam=0;shore=0; }
    nextField[pixel]=float4(clamp(height,-1,1),clamp(velocity,-4,4),saturate(foam),saturate(shore));
}
