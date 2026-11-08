"""Author the low-pass spray layer: aircraft/hovercraft skimming over liquid.

Read the composed stage; emit one pipeline patch. Re-runs reverse this layer
in memory first, so the patch is always authored against the lava-glow
baseline (which also carries the caustic texture).
"""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root=Path(__file__).resolve().parents[1]
stage=Path(sys.argv[1]).resolve()
output=root/'patches/enhanced-water/dkr-lowpass.patch'
layers=[]
if 'WATER_CRAFT_AIR' in (stage/'src/shared/rt64_water_params.h').read_text():layers.append(output)
changes=[]

def replace(t,a,b):
    if t.count(a)!=1:raise SystemExit(f'Non-unique low-pass context: {a[:120]}')
    return t.replace(a,b,1)

def change(path,fn):
    before=read_before(stage,path,layers);after=fn(before)
    if before==after:raise SystemExit(f'No low-pass change: {path}')
    changes.append(f'diff --git a/{path} b/{path}\n')
    changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
        fromfile=f'a/{path}',tofile=f'b/{path}',n=3))

def params(t):
    t=replace(t,'#define WATER_SHORE_SEGMENTS_PER_TILE 8\n','''#define WATER_SHORE_SEGMENTS_PER_TILE 8
// Low passes: clearance above the liquid at which a skimming craft still
// disturbs it, full-strength clearance, and the speed for full spray.
#define WATER_LOWPASS_PLANE_CEILING 300.0f
#define WATER_LOWPASS_PLANE_FULL 30.0f
#define WATER_LOWPASS_HOVER_CEILING 120.0f
#define WATER_LOWPASS_HOVER_FULL 20.0f
#define WATER_LOWPASS_HOVER_SCALE 0.55f
#define WATER_LOWPASS_FULL_SPEED 900.0f
''')
    t=replace(t,'''    float4 shoreSegments[WATER_CRAFT_COUNT * WATER_SHORE_SEGMENTS_PER_TILE]; // immutable mean-water intersections, XZ endpoints
''','''    // Water draws carry low-pass clearance in the last two shoreline vectors (WATER_CRAFT_AIR).
    float4 shoreSegments[WATER_CRAFT_COUNT * WATER_SHORE_SEGMENTS_PER_TILE]; // immutable mean-water intersections, XZ endpoints
''')
    t=replace(t,'''struct WaterDrawParams {
''','''// Low-pass clearance above the liquid, four crafts per vector: positive
// values are aircraft, negative values hovercraft, zero means no low pass.
// WaterDrawParams is exactly at the 2048-byte structured-buffer stride, so
// the clearance shares the last two shoreline vectors instead of growing it.
// Water draws publish no shoreline segments (every shoreCounts entry stays
// zero, so the field shaders never read them) and lava draws reuse only
// shoreSegments[0..15] for emitters and write no clearance. Low passes are
// gated on active crafts (W>=0), which lava and vertical water never have.
#define WATER_CRAFT_AIR_SEGMENT (WATER_CRAFT_COUNT * WATER_SHORE_SEGMENTS_PER_TILE - WATER_CRAFT_COUNT / 4)
#define WATER_CRAFT_AIR(m, i) ((m).shoreSegments[WATER_CRAFT_AIR_SEGMENT + (i) / 4][(i) % 4])

struct WaterDrawParams {
''')
    t=replace(t,'''static_assert(sizeof(WaterDrawParams) <= 2048, "D3D structured-buffer stride limit");
''','''static_assert(sizeof(WaterDrawParams) <= 2048, "D3D structured-buffer stride limit");
static_assert(WATER_CRAFT_AIR_SEGMENT >= 16, "Low-pass clearance must not overlap the lava emitters");
''')
    return t+'''
#ifndef HLSL_CPU
// Matches lowPassStrength in rt64_water_renderer.cpp.
float waterLowPassStrength(float air) {
    if (air>0) return 1-smoothstep(WATER_LOWPASS_PLANE_FULL,WATER_LOWPASS_PLANE_CEILING,air);
    if (air<0) return WATER_LOWPASS_HOVER_SCALE*(1-smoothstep(WATER_LOWPASS_HOVER_FULL,WATER_LOWPASS_HOVER_CEILING,-air));
    return 0;
}
#endif
'''
change('src/shared/rt64_water_params.h',params)

def header(t):
    return replace(t,'    std::array<interop::float4,WATER_CRAFT_COUNT> sprayHeading{};\n',
        '    std::array<interop::float4,WATER_CRAFT_COUNT> sprayHeading{};\n'
        '    std::array<float,WATER_CRAFT_COUNT> lowPassDistance{};\n')
change('src/render/rt64_water_renderer.h',header)

def renderer(t):
    t=replace(t,'void WaterRenderer::stepSpray(const interop::WaterMaterial &m,float fromTime,float toTime,bool reset) {\n',
        '''// Matches waterLowPassStrength in rt64_water_params.h.
static float lowPassStrength(float air) {
    const auto fade=[](float full,float ceiling,float clearance) {
        const float t=std::clamp((clearance-full)/(ceiling-full),0.0f,1.0f);
        return 1-t*t*(3-2*t);
    };
    if (air>0) return fade(WATER_LOWPASS_PLANE_FULL,WATER_LOWPASS_PLANE_CEILING,air);
    if (air<0) return WATER_LOWPASS_HOVER_SCALE*fade(WATER_LOWPASS_HOVER_FULL,WATER_LOWPASS_HOVER_CEILING,-air);
    return 0;
}

void WaterRenderer::stepSpray(const interop::WaterMaterial &m,float fromTime,float toTime,bool reset) {
''')
    t=replace(t,'        particles.clear(); sprayDistance.fill(0); sprayHeading.fill({0,0,0,0});\n',
        '        particles.clear(); sprayDistance.fill(0); sprayHeading.fill({0,0,0,0}); lowPassDistance.fill(0);\n')
    a=t.index('void WaterRenderer::drawSpray')
    end=t.rindex('    }\n}\n',0,a)
    t=t[:end]+'''    }
    // Low passes lift spray from the liquid beneath the craft, not from its
    // hull: outward curtains (kind 2) and slowly settling mist (kind 1).
    for (uint32_t i=0;i<WATER_CRAFT_COUNT;i++) {
        const auto &old=m.craftPrevious[i], &current=m.craftCurrent[i];
        const float air=WATER_CRAFT_AIR(m,i);
        const float dx=current.x-old.x,dz=current.z-old.z;
        const float distance=std::sqrt(dx*dx+dz*dz), speed=distance/interval;
        const float wash=current.w<0 ? 0.0f : lowPassStrength(air)*std::clamp(speed/WATER_LOWPASS_FULL_SPEED,0.0f,1.0f);
        if (wash<=0.02f) { lowPassDistance[i]=0; continue; }
        const float hx=dx/distance, hz=dz/distance;
        const float clearance=std::abs(air), radius=20+clearance*0.3f, back=10+clearance*0.15f;
        const float surface=current.y-clearance;
        lowPassDistance[i]+=distance*(b-a)*wash;
        uint32_t count=std::min(48u,uint32_t(lowPassDistance[i]/2));
        lowPassDistance[i]-=float(count)*2;
        for (uint32_t j=0;j<count && particles.size()<budget;j++) {
            const float f=a+(b-a)*(float(j)+0.5f)/float(count);
            const float x=old.x+dx*f-hx*back, z=old.z+dz*f-hz*back;
            const float birth=fromTime+(toTime-fromTime)*(float(j)+0.5f)/float(count);
            interop::WaterSprayParticle p{};
            if (random()<0.06f) {
                // A faint haze, visible mostly against the sun.
                const float spread=(random()*2-1)*radius*0.6f;
                p.origin={x+hz*spread,surface+3,z-hx*spread,birth};
                p.velocity={hx*speed*0.10f+hz*spread*0.4f,12+random()*28*wash,hz*speed*0.10f-hx*spread*0.4f,1.1f+random()*1.2f};
                p.appearance={18+random()*18+radius*0.1f,0.02f+random()*0.03f*wash,random(),1};
            } else {
                // Fine droplets thrown outward and up, drawn as motion streaks.
                const float side=random()<0.5f ? -1.0f : 1.0f;
                const float offset=side*radius*(0.25f+random()*0.35f);
                const float lateral=side*(60+random()*120)*(0.5f+0.5f*wash);
                const float drift=speed*(0.06f+random()*0.12f);
                p.origin={x+hz*offset,surface+1.5f,z-hx*offset,birth};
                p.velocity={hx*drift+hz*lateral,(70+random()*160)*(0.45f+0.55f*wash),hz*drift-hx*lateral,0.3f+random()*0.45f};
                p.appearance={0.6f+random()*0.8f,(0.55f+random()*0.35f)*(0.6f+0.4f*wash),random(),2};
            }
            particles.push_back(p);
        }
    }
}
'''+t[end+len('    }\n}\n'):]
    t=replace(t,'''    const bool active=horizontal!=draws.end() && std::any_of(std::begin(m.craftCurrent),std::end(m.craftCurrent),[](const auto& c){return c.w>0;});''',
        '''    // A low pass over the liquid keeps the field alive without hull contact.
    bool lowPass=false;
    for (uint32_t i=0;i<WATER_CRAFT_COUNT;i++) lowPass|=m.craftCurrent[i].w>=0 && WATER_CRAFT_AIR(m,i)!=0;
    const bool active=horizontal!=draws.end() && (lowPass ||
        std::any_of(std::begin(m.craftCurrent),std::end(m.craftCurrent),[](const auto& c){return c.w>0;}));''')
    return t
change('src/render/rt64_water_renderer.cpp',renderer)

def field(t):
    t=replace(t,'groupshared float4 craftResponse[WATER_CRAFT_COUNT];\n',
        'groupshared float4 craftResponse[WATER_CRAFT_COUNT];\n'
        'groupshared float4 craftWash[WATER_CRAFT_COUNT];\n')
    t=replace(t,'''        craftResponse[lane]=float4(saturate((old.y-current.y)/interval/180),landed ? 1 : 0,current.w,0);
''','''        craftResponse[lane]=float4(saturate((old.y-current.y)/interval/180),landed ? 1 : 0,current.w,0);
        // Low pass: a footprint trailing beneath the craft, wider when higher.
        float air=WATER_CRAFT_AIR(m,lane);
        float clearance=abs(air);
        float wash=current.w>=0 ? waterLowPassStrength(air)*saturate(distance/interval/WATER_LOWPASS_FULL_SPEED) : 0;
        craftWash[lane]=float4(to-heading*(10+clearance*0.15),20+clearance*0.3,wash*min(length(to-from)/cell,2));
''')
    t=replace(t,'''            foam += ring*0.65;
            velocity += ring*0.7;
        }
''','''            foam += ring*0.65;
            velocity += ring*0.7;
        }
        float4 wash=craftWash[i];
        if (wash.w>0) {
            float2 offset=world-wash.xy;
            float r=wash.z, behind=max(-dot(offset,heading),0);
            float lateral=abs(dot(offset,float2(-heading.y,heading.x)));
            // The moving pressure dent radiates its own waves; curtains spread
            // wider than the hull wake; a churned trail follows the path.
            float dent=exp(-dot(offset,offset)/(r*r));
            float curtain=exp(-pow((lateral-behind*0.55-r*0.35)/(6+r*0.25),2))*smoothstep(0,r*0.5,behind)*(1-smoothstep(r*2.5,r*4.5,behind));
            float churn=exp(-pow(d/(r*0.5),2));
            foam += (churn*0.035+curtain*0.03)*wash.w;
            velocity += (curtain*0.05-dent*0.12)*wash.w;
        }
''')
    return t
change('src/shaders/WaterFieldCS.hlsl',field)

def spray_vs(t):
    t=replace(t,'    out float3 world : TEXCOORD1, out float2 fade : TEXCOORD2) {',
        '    out float3 world : TEXCOORD1, out float4 fade : TEXCOORD2) {')
    t=replace(t,'''    float drag=1.5, response=(1-exp(-drag*age))/drag;
    world=particle.origin.xyz+particle.velocity.xyz*response;
    world.y-=140*(age-response)/drag;
    float4 clip=mul(p.viewProjection,float4(world,1));
    float4 top=mul(p.viewProjection,float4(world+float3(0,particle.appearance.x,0),1));''','''    // Kind 0 is hull spray, 1 low-pass mist, 2 low-pass curtain droplets.
    float kind=particle.appearance.w;
    bool mist=kind>0.5 && kind<1.5, streak=kind>1.5;
    float life=saturate(age/particle.velocity.w);
    float drag=mist ? 2.6 : 1.5, gravity=mist ? 12 : 140, response=(1-exp(-drag*age))/drag;
    world=particle.origin.xyz+particle.velocity.xyz*response;
    world.y-=gravity*(age-response)/drag;
    float size=particle.appearance.x*(mist ? 1+life*1.2 : 1);
    float4 clip=mul(p.viewProjection,float4(world,1));
    float4 top=mul(p.viewProjection,float4(world+float3(0,size,0),1));''')
    t=replace(t,'''    position=float4(xy*clip.w,screen.z*clip.w,clip.w);
    float radius=clamp(projectedRadius,0.0005,0.035);
    position.xy+=uv*radius*float2(p.screen.y/p.screen.x,1)*clip.w;
    float life=saturate(age/particle.velocity.w);
    float distance=length(world-p.camera.xyz);
    float nearFade=smoothstep(particle.appearance.x*4,particle.appearance.x*12,distance);
    float sizeFade=1-smoothstep(0.012,0.035,projectedRadius);
    fade=float2(particle.appearance.y*smoothstep(0,0.07,age)*(1-smoothstep(0.55,1,life))*nearFade*sizeFade,distance);''','''    position=float4(xy*clip.w,screen.z*clip.w,clip.w);
    // Streaks keep a minimum width so fine droplets stay visible.
    float radius=clamp(projectedRadius,streak ? 0.0022 : 0.0005,mist ? 0.24 : streak ? 0.12 : 0.035);
    float2 aspect=float2(p.screen.y/p.screen.x,1);
    float2 offset=uv*radius*aspect;
    float energy=1;
    if (streak) {
        // Stretch curtain droplets along their recent screen motion.
        float decay=exp(-drag*age);
        float3 velocity=particle.velocity.xyz*decay-float3(0,gravity*(1-decay)/drag,0);
        float4 tail=mul(p.viewProjection,float4(world-velocity*0.09,1));
        if (all(isfinite(tail)) && tail.w>0.01) {
            float3 tailScreen=tail.xyz/tail.w*float3(1,-1,1)*p.viewportScale.xyz+p.viewportTranslate.xyz;
            float2 tailXY=(tailScreen.xy/p.screen.xy*float2(2,-2)+float2(-1,1))*p.screenTransform.xy+p.screenTransform.zw;
            float2 trail=(xy-tailXY)/aspect;
            float span=length(trail);
            if (all(isfinite(trail)) && span>radius*0.5) {
                float2 along=trail/span, across=float2(-along.y,along.x);
                span=min(span,0.2);
                offset=(across*uv.x*radius+along*(uv.y*(span*0.5+radius)-span*0.5))*aspect;
                energy=pow(saturate(2*radius/(span+2*radius)),0.3);
            }
        }
    }
    position.xy+=offset*clip.w;
    float distance=length(world-p.camera.xyz);
    float nearFade=mist ? smoothstep(size*1.5,size*4,distance) : smoothstep(particle.appearance.x*4,particle.appearance.x*12,distance);
    float sizeFade=mist ? 1-smoothstep(0.14,0.24,projectedRadius) : streak ? 1-smoothstep(0.07,0.12,projectedRadius) : 1-smoothstep(0.012,0.035,projectedRadius);
    fade=float4(particle.appearance.y*smoothstep(0,0.07,age)*(1-smoothstep(0.55,1,life))*nearFade*sizeFade*energy,distance,kind,particle.appearance.z);''')
    return t
change('src/shaders/WaterSprayVS.hlsl',spray_vs)

def spray_ps(t):
    t=replace(t,'    float3 world : TEXCOORD1, float2 fade : TEXCOORD2) : SV_TARGET0 {',
        '    float3 world : TEXCOORD1, float4 fade : TEXCOORD2) : SV_TARGET0 {')
    t=replace(t,'''    float alpha=(1-smoothstep(0.10,1,radius))*fade.x;''','''    bool mist=fade.z>0.5 && fade.z<1.5;
    // Streaks are brightest at the head (uv.y=+1) and fade toward the tail.
    float streak=(1-smoothstep(0.15,1,abs(uv.x)))*smoothstep(-1,0.3,uv.y)*(1-smoothstep(0.75,1,uv.y));
    float alpha=(mist ? exp(-radius*2.2)*(1-smoothstep(0.6,1,radius)) : fade.z>1.5 ? streak : 1-smoothstep(0.10,1,radius))*fade.x;
    float soft=mist ? 40 : 12;''')
    t=replace(t,'    if (water>0) alpha*=saturate((water-fade.y)/12);',
        '    if (water>0) alpha*=saturate((water-fade.y)/soft);')
    t=replace(t,'        alpha*=saturate((length(background-p.camera.xyz)-fade.y)/12);',
        '        alpha*=saturate((length(background-p.camera.xyz)-fade.y)/soft);')
    t=replace(t,'''    float3 light=(p.material.skyColor.rgb*0.3+p.material.sunColor.rgb*0.7)*p.material.sunColor.a;
''','''    float3 light=(p.material.skyColor.rgb*0.3+p.material.sunColor.rgb*0.7)*p.material.sunColor.a;
    if (fade.z>0.5) {
        // Low-pass spray is lit white water, glowing faintly against the sun.
        light=max(light,float3(0.62,0.66,0.68)*p.material.sunColor.a*2);
        float scatter=pow(saturate(dot(normalize(world-p.camera.xyz),normalize(p.material.sunDirection.xyz))),6);
        light+=p.material.sunColor.rgb*scatter*(mist ? 0.55 : 0.35);
        // A few droplets catch the sun as glints.
        if (fade.z>1.5 && fade.w>0.85) light+=p.material.sunColor.rgb*(0.25+scatter);
    }
''')
    return t
change('src/shaders/WaterSprayPS.hlsl',spray_ps)

def surface(t):
    t=replace(t,'''    ripples += clamp(wakeGradient*8,-0.10,0.10);
''','''    ripples += clamp(wakeGradient*8,-0.10,0.10);
    // Low pass: downwash and hovercraft skirts roughen and whiten a teardrop
    // of liquid beneath and behind each craft. Analytic, so it never lags.
    float churn=0,churnFoam=0;
    float2 churnHeading=0;
    {
        float interval=max(m.animation.y-m.animation.x,0.00001);
        float f=saturate((p.camera.w-m.animation.x)/interval);
        [loop] for (uint i=0;i<WATER_CRAFT_COUNT;i++) {
            float air=WATER_CRAFT_AIR(m,i);
            if (air==0 || m.craftCurrent[i].w<0) continue;
            float2 delta=m.craftCurrent[i].xz-m.craftPrevious[i].xz;
            float travel=length(delta);
            float wash=waterLowPassStrength(air)*saturate(travel/interval/WATER_LOWPASS_FULL_SPEED);
            if (wash<=0.001) continue;
            float2 heading=delta/travel;
            float clearance=abs(air),radius=20+clearance*0.3;
            float2 offset=worldPosition.xz-lerp(m.craftPrevious[i].xz,m.craftCurrent[i].xz,f)+heading*(10+clearance*0.15);
            float ahead=dot(offset,heading),lateral=dot(offset,float2(-heading.y,heading.x));
            float extent=ahead>0 ? radius*0.7 : radius*3.5;
            float footprint=exp(-pow(lateral/radius,2)-pow(ahead/extent,2))*wash;
            if (footprint>churn) { churn=footprint; churnHeading=heading; }
        }
    }
    if (churn>0.001) {
        // World-anchored streaks along the heading: the craft sweeps over them.
        float2 across=float2(-churnHeading.y,churnHeading.x);
        float2 streakAt=float2(dot(worldPosition.xz,across)*0.16,dot(worldPosition.xz,churnHeading)*0.012);
        float s0=foamNoise(streakAt);
        float2 slope=across*(foamNoise(streakAt+float2(0.35,0))-s0)*0.5+churnHeading*(foamNoise(streakAt+float2(0,0.03))-s0)*0.2;
        ripples += clamp(slope*churn*0.6,-0.12,0.12);
        churnFoam=churn*(0.25+0.75*smoothstep(0.35,0.8,s0))*0.35;
    }
''')
    t=replace(t,'''    float roughness = clamp(m.shallowColor.a + length(float2(ddx(N.x), ddy(N.z))) * 0.2, 0.065, 0.65);''',
        '''    float roughness = clamp(m.shallowColor.a + length(float2(ddx(N.x), ddy(N.z))) * 0.2 + churn * 0.25, 0.065, 0.65);''')
    t=replace(t,'''    float foam=saturate((wake.y*1.8+shore*1.3+crest+contact)*m.animation.w*breakup);
    color=lerp(color,float3(0.73,0.79,0.80)*(0.65+0.35*nl)*m.sunColor.a,foam);
''','''    float foam=saturate((wake.y*1.8+shore*1.3+crest+contact)*m.animation.w*breakup);
    foam=saturate(foam+churnFoam);
    color=lerp(color,float3(0.73,0.79,0.80)*(0.65+0.35*nl)*m.sunColor.a,foam);
    // Sun glitter on churned liquid; sub-pixel cells are filtered out.
    float2 glitterAt=worldPosition.xz*0.45;
    float glitterFilter=1-smoothstep(0.35,1.0,length(fwidth(glitterAt)));
    if (churn>0.001) {
        float spark=smoothstep(0.93,1.0,foamHash(floor(glitterAt)+floor(p.camera.w*12)*float2(7.13,3.71)));
        color+=m.sunColor.rgb*m.sunDirection.a*spark*pow(saturate(dot(reflect(-V,N),L)),4)*churn*glitterFilter*1.2;
    }
''')
    return t
change('src/shaders/WaterPS.hlsl',surface)

output.write_text(''.join(changes),newline='\n')
print('Generated low-pass spray patch.')
