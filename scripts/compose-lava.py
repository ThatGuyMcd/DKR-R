"""Compose the molten-lava patch; dependency and generated sources stay read-only."""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root=Path(__file__).resolve().parents[1]
stage=Path(sys.argv[1]).resolve()
output=root/'patches/enhanced-water/dkr-lava.patch'
layers=[output] if 'lavaSurface' in (stage/'src/shaders/WaterPS.hlsl').read_text() else []
changes=[]

def replace(text,before,after):
    if text.count(before)!=1:
        raise SystemExit(f'Non-unique lava context: {before[:120]}')
    return text.replace(before,after,1)

def change(path,fn):
    before=read_before(stage,path,layers)
    after=fn(before)
    if before==after:raise SystemExit(f'No lava change: {path}')
    changes.append(f'diff --git a/{path} b/{path}\n')
    changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
        fromfile=f'a/{path}',tofile=f'b/{path}',n=3))

lava=r'''    if (m.effects.z==5) {
        // Keep the authored static/tessellated lava mesh and depth contract.
        // Molten material is opaque: no water transmission, caustics or wakes.
        float3 geometric=surfaceNormal;
        geometric=dot(geometric,geometric)>0.000001 ? normalize(geometric) : float3(0,1,0);
        if(geometric.y<0)geometric=-geometric;
        float2 slope=rippleGradient(worldPosition.xz,p.camera.w,m.animation.z,m.surface.yz);
        float3 N=normalize(geometric+float3(-slope.x,0,-slope.y));
        float3 V=normalize(p.camera.xyz-worldPosition);
        float footprint=max(length(ddx(worldPosition.xz)),length(ddy(worldPosition.xz)))*LAVA_WORLD_SCALE;
        float4 molten=lavaSurface(worldPosition.xz,p.camera.w,footprint);
        float roughness=clamp(m.shallowColor.a+molten.a*0.18,0.065,0.65);
        float4 reflection=0;
#ifdef HARDWARE_WATER
        bool fallingHit=false,capturedHit=false;
        float4 traced=hardwareWaterReflection(worldPosition,N,V,p,fallingHit,capturedHit);
        if(fallingHit || capturedHit)reflection=traced;
        else {
            reflection=screenReflection(worldPosition,N,V,roughness,p);
            if(traced.a>reflection.a)reflection=traced;
        }
#else
        reflection=screenReflection(worldPosition,N,V,roughness,p);
#endif
        float fresnel=0.04+0.96*pow(1-saturate(dot(N,V)),5);
        float3 reflected=lerp(sky(reflect(-V,N),m),reflection.rgb,reflection.a);
        float reflectionWeight=saturate(m.appearance.x)*fresnel*(0.18+molten.a*0.37);
        // Emission is surface shading, not a new scene-lighting simulation.
        float3 color=molten.rgb*(m.shallowColor.rgb/float3(1,0.18,0.015))+reflected*reflectionWeight;
        return float4(lavaDisplayColor(color,p.fogColor.rgb,p.fogColor.a>0.5 ? sourceColor.a : 0),1);
    }
'''

def shader(text):
    text=replace(text,'#include "WaterProjection.hlsli"',
        '#include "WaterProjection.hlsli"\n#include "LavaFlow.hlsli"')
    return replace(text,'    if (m.effects.z>=3) {',lava+'    if (m.effects.z>=3 && m.effects.z<=4) {')
change('src/shaders/WaterPS.hlsl',shader)
change('src/render/rt64_water_renderer.cpp',lambda text:replace(text,
    '    if(draws.back().material.effects.z>=3)return;',
    '    if(draws.back().material.effects.z>=3 && draws.back().material.effects.z<=4)return;'))

output.write_text(''.join(changes),newline='\n')
print(f'Composed {output.name}: {len(changes)} diff records')
