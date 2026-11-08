"""Author shared split-screen capture and bounded eight-racer wake support.

No dependency or generated stage file is changed. Output is a pipeline patch.
"""

import difflib
from pathlib import Path
import sys

from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
directory = root / "patches/enhanced-water"
layers = []
if "waterRayGeometry" in (stage / "src/hle/rt64_state.h").read_text():
    layers.append(directory / "dkr-rays.patch")
if "WATER_CRAFT_COUNT" in (stage / "src/shared/rt64_water_params.h").read_text():
    layers.append(directory / "dkr-views.patch")
changes = []


def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"Non-unique view context: {old[:100]}")
    return text.replace(old, new, 1)


def change(path, fn):
    before = read_before(stage, path, layers)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No view changes: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    changes.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
        )
    )


def params(t):
    t = replace(
        t,
        "#define WATER_SHORE_SEGMENTS_PER_TILE 20",
        "#define WATER_CRAFT_COUNT 8\n#define WATER_SHORE_SEGMENTS_PER_TILE 8",
    )
    t = t.replace("craftPrevious[4]", "craftPrevious[WATER_CRAFT_COUNT]").replace(
        "craftCurrent[4]", "craftCurrent[WATER_CRAFT_COUNT]"
    )
    t = t.replace("float4 shoreCounts;", "float4 shoreCounts[2];").replace(
        "[4 * WATER_SHORE_SEGMENTS_PER_TILE]", "[WATER_CRAFT_COUNT * WATER_SHORE_SEGMENTS_PER_TILE]"
    )
    t = t.replace("fieldOrigins[4]", "fieldOrigins[WATER_CRAFT_COUNT]").replace(
        "origins[4]", "origins[WATER_CRAFT_COUNT]"
    )
    t = t.replace("== 1664", "== 1552").replace("== 1984", "== 1936").replace("== 96", "== 160")
    return t


change("src/shared/rt64_water_params.h", params)


def header(t):
    t = (
        t.replace("std::array<float,4> sprayDistance", "std::array<float,WATER_CRAFT_COUNT> sprayDistance")
        .replace(
            "std::array<interop::float4,4> sprayHeading", "std::array<interop::float4,WATER_CRAFT_COUNT> sprayHeading"
        )
        .replace(
            "std::array<interop::float4,4> fieldOrigins", "std::array<interop::float4,WATER_CRAFT_COUNT> fieldOrigins"
        )
    )
    t = replace(
        t,
        "        bool captured = false;",
        "        bool captured = false;\n        interop::float4 capturedView{};\n        uint32_t capturedCamera=UINT32_MAX;",
    )
    return replace(
        t,
        "std::map<std::pair<uint64_t,bool>,uint32_t> sceneKeys;",
        "std::map<std::tuple<uint64_t,bool,bool>,uint32_t> sceneKeys;",
    )


change("src/render/rt64_water_renderer.h", header)


def renderer(t):
    t = replace(
        t,
        "    const auto sceneKey=std::make_pair(key,selfDepth);",
        "    const auto sceneKey=std::make_tuple(key,classic,selfDepth);",
    )
    t = replace(
        t,
        "    if (scene.captured) return false;",
        """    const auto &draw=draws[drawIndex];
    const uint32_t camera=uint32_t(draw.material.effects.y);
    const auto &view=draw.viewBounds;
    if(scene.captured && scene.capturedCamera==camera &&
        scene.capturedView.x==view.x && scene.capturedView.y==view.y &&
        scene.capturedView.z==view.z && scene.capturedView.w==view.w)return false;
    // Each viewport re-captures this scratch target after its opaque pass.
    // Earlier view commands have already consumed it; all sampling stays
    // inside the corresponding view bounds. Four views do not need four
    // full-window colour/depth copies resident simultaneously.
    scene.capturedCamera=camera;scene.capturedView=view;""",
    )
    t = replace(
        t,
        "    const auto sourceDraw=draws.front();",
        """    const auto horizontal=std::find_if(draws.begin(),draws.end(),[](const auto& d){return d.material.effects.z<3;});
    const auto sourceDraw=horizontal==draws.end() ? draws.front() : *horizontal;""",
    )
    t = replace(
        t,
        "    const uint32_t resolution=m.identity.x>=3 ? 512 : 256;",
        """    const bool active=horizontal!=draws.end() && std::any_of(std::begin(m.craftCurrent),std::end(m.craftCurrent),[](const auto& c){return c.w>0;});
    const uint32_t resolution=active ? (m.identity.x>=3 ? 512 : 256) : 1;""",
    )
    t = replace(t, "    const float desired=draws.front().camera.w;", "    const float desired=sourceDraw.camera.w;")
    t = replace(
        t,
        "    if (reset) fieldInputs.clear();",
        "    if (reset || !active) fieldInputs.clear();\n    if(!active)stepSpray(m,fieldTime,desired,true);",
    )
    t = replace(t, "    while (fieldTime+0.00001f<desired) {", "    while (active && fieldTime+0.00001f<desired) {")
    t = (
        t.replace("i<4;i++", "i<WATER_CRAFT_COUNT;i++")
        .replace("resolution*4,resolution", "resolution*WATER_CRAFT_COUNT,resolution")
        .replace("fieldResolution*4+7", "fieldResolution*WATER_CRAFT_COUNT+7")
        .replace("fieldResolution*4*8*2", "fieldResolution*WATER_CRAFT_COUNT*8*2")
    )
    return t


change("src/render/rt64_water_renderer.cpp", renderer)


def framebuffer(t):
    t = replace(
        t,
        "(uint64_t(framebufferCount) << 32) | proj.transformsIndex,classic,selfDepth);",
        "uint64_t(framebufferCount),classic,selfDepth);",
    )
    return replace(
        t,
        "            if (activeWaterDraw!=UINT32_MAX && waterScene!=waterRenderer->drawScenes[activeWaterDraw]) finishWater();",
        """            if(activeWaterDraw!=UINT32_MAX) {
                bool changed=waterScene!=waterRenderer->drawScenes[activeWaterDraw];
                if(!changed && drawCall.waterIndex!=UINT32_MAX) {
                    const auto &a=waterRenderer->draws[activeWaterDraw],&b=waterRenderer->draws[drawCall.waterIndex];
                    changed=a.material.effects.y!=b.material.effects.y || a.viewBounds.x!=b.viewBounds.x ||
                        a.viewBounds.y!=b.viewBounds.y || a.viewBounds.z!=b.viewBounds.z || a.viewBounds.w!=b.viewBounds.w;
                }
                if(changed)finishWater();
            }""",
    )


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)


def field(t):
    t = t.replace("resolution*4", "resolution*WATER_CRAFT_COUNT").replace("tile>=4", "tile>=WATER_CRAFT_COUNT")
    t = t.replace("i<4;i++", "i<WATER_CRAFT_COUNT;i++").replace("m.shoreCounts[tile]", "m.shoreCounts[tile/4][tile%4]")
    return t


change("src/shaders/WaterFieldCS.hlsl", field)


def pixels(t):
    t = (
        t.replace("uint tile=4;", "uint tile=WATER_CRAFT_COUNT;")
        .replace("tile==4", "tile==WATER_CRAFT_COUNT")
        .replace("i<4;i++", "i<WATER_CRAFT_COUNT;i++")
    )
    return t


change("src/shaders/WaterPS.hlsl", pixels)
(directory / "dkr-views.patch").write_text("".join(changes), newline="\n")
print("Generated shared-view and bounded eight-racer visual patch.")
