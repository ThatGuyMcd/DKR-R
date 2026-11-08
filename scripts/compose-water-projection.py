"""Author the widescreen projection correction without editing dependencies.

Read the fully composed stage; emit one final pipeline patch. Re-runs reverse
only this layer in memory. Shared native shader functions are authored inputs.
"""

import difflib
from pathlib import Path
import sys

from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
output = root / "patches/enhanced-water/dkr-projection.patch"
layers = [output] if "inverseWorldViewProjection" in (stage / "src/shared/rt64_water_params.h").read_text() else []
changes = []


def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"Non-unique projection context: {old[:120]}")
    return text.replace(old, new, 1)


def change(path, fn):
    before = read_before(stage, path, layers)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No projection changes: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    changes.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
        )
    )


def params(t):
    t = replace(
        t,
        "    float4x4 inverseViewProjection;",
        "    float4x4 inverseViewProjection; // inverse of final raster camera\n"
        "    float4x4 inverseWorldViewProjection; // decode DKR model MVP output only",
    )
    return replace(t, "sizeof(WaterDrawParams) == 1936", "sizeof(WaterDrawParams) == 2000")


change("src/shared/rt64_water_params.h", params)


def framebuffer(t):
    t = replace(
        t,
        '#include "rt64_framebuffer_renderer.h"',
        '#include "rt64_framebuffer_renderer.h"\n#include "WaterProjection.h"',
    )
    t = replace(
        t,
        "                        wp.viewProjection = wp.material.cameraViewProjection;",
        "                        auto cameraViewProjection = hlslpp::float4x4(wp.material.cameraViewProjection);",
    )
    t = replace(
        t,
        "if (i<drawData.lerpWorldTransforms.size()) wp.viewProjection=drawData.lerpWorldTransforms[i];",
        "if (i<drawData.lerpWorldTransforms.size()) cameraViewProjection=drawData.lerpWorldTransforms[i];",
    )
    t = replace(
        t,
        """                        const auto eye=hlslpp::mul(hlslpp::float4(0,0,1,0),hlslpp::inverse(wp.viewProjection));
                        wp.camera.x=float(eye.x/eye.w);
                        wp.camera.y=float(eye.y/eye.w);
                        wp.camera.z=float(eye.z/eye.w);""",
        """                        setWaterCameraProjection(wp,cameraViewProjection,
                            drawData.modViewProjTransforms[proj.transformsIndex]);""",
    )
    t = replace(
        t,
        "                    wp.inverseViewProjection = hlslpp::inverse(wp.viewProjection);",
        """                    wp.inverseViewProjection = hlslpp::inverse(wp.viewProjection);
                    if(wp.material.cameraPosition.w!=1.0f)
                        wp.inverseWorldViewProjection=hlslpp::float4x4::identity();""",
    )
    return replace(
        t,
        "for (uint32_t row=0;row<4;row++) for (uint32_t col=0;col<4;col++) waterMatricesValid &= std::isfinite(wp.inverseViewProjection[row][col]);",
        """for (uint32_t row=0;row<4;row++) for (uint32_t col=0;col<4;col++)
                        waterMatricesValid &= std::isfinite(wp.inverseViewProjection[row][col]) &&
                            std::isfinite(wp.inverseWorldViewProjection[row][col]);""",
    )


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)


def pixels(t):
    t = replace(t, '#include "shared/rt64_water_params.h"', '#include "WaterProjection.hlsli"')
    a = t.index("float3 worldFromPixel(")
    b = t.index("\nfloat opticalDepth(", a)
    t = t[:a] + t[b + 1 :]
    a = t.index("float4 pixelFromWorld(")
    b = t.index("\nfloat4 screenReflection(", a)
    t = t[:a] + t[b + 1 :]
    return t.replace("worldFromPixel(", "waterWorldFromPixel(").replace("pixelFromWorld(", "waterPixelFromWorld(")


change("src/shaders/WaterPS.hlsl", pixels)


def vertices(t):
    t = replace(t, '#include "shared/rt64_water_params.h"', '#include "WaterProjection.hlsli"')
    return replace(
        t,
        """    if (p.material.cameraPosition.w == 1.0f) {
        world = mul(p.inverseViewProjection, world);
        world.xyz /= abs(world.w)>0.000001f ? world.w : 1.0f;
    }
    worldPosition = world.xyz;""",
        "    worldPosition = waterDecodeWorld(world,p);",
    )


change("src/shaders/WaterVS.hlsl", vertices)


def normals(t):
    t = replace(t, '#include "shared/rt64_water_params.h"', '#include "WaterProjection.hlsli"')
    return replace(
        t,
        """    float4 v = positions[index];
    if (p.material.cameraPosition.w == 1.0f) {
        v = mul(p.inverseViewProjection, v);
        v.xyz /= abs(v.w)>0.000001f ? v.w : 1.0f;
    }
    return v.xyz;""",
        "    return waterDecodeWorld(positions[index],p);",
    )


change("src/shaders/WaterNormalsCS.hlsl", normals)


def spray(t):
    t = replace(t, '#include "shared/rt64_water_params.h"', '#include "WaterProjection.hlsli"')
    return replace(
        t,
        """        float2 ndc=position.xy/float2(p.viewportScale.w,p.viewportTranslate.w)*float2(2,-2)+float2(-1,1);
        ndc=(ndc-p.screenTransform.zw)/p.screenTransform.xy;
        float2 screen=(ndc*float2(0.5,-0.5)+0.5)*p.screen.xy;
        float3 projected=float3((screen-p.viewportTranslate.xy)/p.viewportScale.xy*float2(1,-1),(depth-p.viewportTranslate.z)/p.viewportScale.z);
        float4 background=mul(p.inverseViewProjection,float4(projected,1));
        alpha*=saturate((length(background.xyz/background.w-p.camera.xyz)-fade.y)/12);""",
        """        float3 background=waterWorldFromPixel(position.xy,depth,p);
        alpha*=saturate((length(background-p.camera.xyz)-fade.y)/12);""",
    )


change("src/shaders/WaterSprayPS.hlsl", spray)

output.write_text("".join(changes), newline="\n")
print("Generated the final camera/viewport projection correction patch.")
