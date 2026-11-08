"""Author Playtest 16 renderer changes; never mutate protected dependencies."""

import difflib
from pathlib import Path
import sys

from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
output = root / "patches/enhanced-water/dkr-performance.patch"
applied = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else output
layers = [applied] if "WaterWorkPolicy.h" in (stage / "src/render/rt64_water_renderer.h").read_text() else []
changes = []


def replace(t, a, b):
    if t.count(a) != 1:
        raise SystemExit(f"Non-unique performance context: {a[:140]}")
    return t.replace(a, b, 1)


def change(path, fn):
    before = read_before(stage, path, layers)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No performance change: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    changes.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
        )
    )


def header(t):
    t = replace(t, '#include "WaterResourcePolicy.h"', '#include "WaterResourcePolicy.h"\n#include "WaterWorkPolicy.h"')
    t = replace(
        t,
        "    float fieldTime = 0, fieldDesired = 0;",
        "    bool fieldWasActive=false;\n    float fieldTime = 0, fieldDesired = 0;",
    )
    t = replace(t, "        std::set<uint32_t> vertices;", "        std::vector<uint32_t> vertices;")
    return replace(
        t,
        "    std::vector<uint32_t> profileKinds;",
        "    std::vector<uint32_t> profileKinds;\n    uint32_t profileFrames=0;\n    std::array<double,5> profileTotals{};",
    )


change("src/render/rt64_water_renderer.h", header)


def renderer(t):
    t = replace(t, '#include "rt64_water_renderer.h"', '#include "rt64_water_renderer.h"\n#include <cstring>')
    # Capture/reduction push constants include the dispatch region (32 bytes).
    t = t.replace(
        "builder.addPushConstant(0,0,16,RenderShaderStageFlag::COMPUTE);",
        "builder.addPushConstant(0,0,32,RenderShaderStageFlag::COMPUTE);",
    )
    t = t.replace(
        "builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::COMPUTE);",
        "builder.addPushConstant(0, 0, 32, RenderShaderStageFlag::COMPUTE);",
    )
    t = replace(
        t,
        "    const uint32_t params[] = {scene.width,scene.height,samples,0};",
        """    auto region=water_work::captureRegion(scene.width,scene.height,view.x,view.y,view.z,view.w);
    const uint32_t params[] = {scene.width,scene.height,samples,0,region.x,region.y,region.width,region.height};""",
    )
    t = replace(
        t,
        "    worker->commandList->dispatch((scene.width+7)/8,(scene.height+7)/8,1);\n    if (draws[drawIndex].material.identity.x >= 2)",
        "    worker->commandList->dispatch((region.width+7)/8,(region.height+7)/8,1);\n    if (draws[drawIndex].material.identity.x >= 2)",
    )
    t = replace(
        t,
        "            const uint32_t reduceParams[] = {w,h,sourceWidth,sourceHeight};",
        """            region=water_work::reduceRegion(region,w,h);
            const uint32_t reduceParams[] = {w,h,sourceWidth,sourceHeight,region.x,region.y,region.width,region.height};""",
    )
    t = replace(
        t,
        "            worker->commandList->dispatch((w+7)/8,(h+7)/8,1);",
        "            worker->commandList->dispatch((region.width+7)/8,(region.height+7)/8,1);",
    )
    t = replace(
        t,
        "    const uint32_t params[]={scene.width,scene.height,samples,0};",
        """    const auto& view=draws[drawIndex].viewBounds;
    const auto region=water_work::captureRegion(scene.width,scene.height,view.x,view.y,view.z,view.w);
    const uint32_t params[]={scene.width,scene.height,samples,0,region.x,region.y,region.width,region.height};""",
    )
    t = replace(
        t,
        "    worker->commandList->dispatch((scene.width+7)/8,(scene.height+7)/8,1);",
        "    worker->commandList->dispatch((region.width+7)/8,(region.height+7)/8,1);",
    )
    t = replace(
        t,
        "    const uint32_t resolution=active ? (m.identity.x>=3 ? 512 : 256) : 1;",
        "    const uint32_t resolution=water_work::fieldResolution(active,bool(fieldTextures[0]),fieldResolution,uint32_t(m.identity.x));",
    )
    t = replace(
        t,
        "        for (uint32_t i=0;i<2;i++) {\n            fieldTextures[i]=",
        """        // Descriptors/views must retire before replacing their textures.
        for(uint32_t i=0;i<2;i++) {fieldSets[i].reset();fieldViews[i].reset();}
        for (uint32_t i=0;i<2;i++) {
            fieldTextures[i]=""",
    )
    t = replace(
        t,
        "    const bool reset=fieldGeneration!=uint32_t(m.identity.w) || desired<fieldDesired-0.00001f || desired>fieldDesired+0.25f;",
        """    const bool reset=fieldGeneration!=uint32_t(m.identity.w) || active!=fieldWasActive ||
        desired<fieldDesired-0.00001f || desired>fieldDesired+0.25f;
    fieldWasActive=active;""",
    )
    # Normal grouping decodes raw (not interpolated) source positions. Keep
    # that exact camera inverse, caching it across same-camera water batches.
    t = replace(
        t,
        "    const auto cameraInverse=hlslpp::inverse(hlslpp::float4x4(draws.back().material.cameraViewProjection));",
        """    const auto camera=hlslpp::float4x4(draws.back().material.cameraViewProjection);
    struct CameraCache {hlslpp::float4x4 matrix,inverse;bool valid=false;};
    thread_local CameraCache cache;
    if(!cache.valid || std::memcmp(&cache.matrix,&camera,sizeof(camera))) {
        cache.matrix=camera;cache.inverse=hlslpp::inverse(camera);cache.valid=true;
    }
    const auto cameraInverse=cache.inverse;""",
    )
    t = replace(
        t,
        "            g.vertices.insert(v);",
        "            if(std::find(g.vertices.begin(),g.vertices.end(),v)==g.vertices.end())g.vertices.push_back(v);",
    )
    t = replace(
        t,
        "    for (const auto &[key,g] : normalGroups) {",
        """    for (auto &[key,g] : normalGroups) {
        // Preserve the original set's ascending vertex order without a heap
        // allocation/tree node for every vertex in every frame.
        std::sort(g.vertices.begin(),g.vertices.end());""",
    )
    t = (
        replace(
            t,
            "    if (!profilePool || profileKinds.empty() || draws.empty()) return;",
            "    if (!profilePool || profileKinds.empty() || draws.empty()) return;",
        )
        if False
        else t
    )
    t = replace(
        t,
        "    static FILE *csv = []() {",
        """    for(unsigned i=0;i<4;i++)profileTotals[i]+=milliseconds[i];
    profileTotals[4]+=frameMilliseconds;
    if(++profileFrames==120) {
        std::fprintf(stderr,"[water][timing] frames=%u normal-ms=%.3f capture-ms=%.3f surface-ms=%.3f field-ms=%.3f frame-gpu-ms=%.3f bytes=%llu\\n",
            profileFrames,profileTotals[0]/profileFrames,profileTotals[1]/profileFrames,profileTotals[2]/profileFrames,
            profileTotals[3]/profileFrames,profileTotals[4]/profileFrames,static_cast<unsigned long long>(allocationBytes()));
        profileFrames=0;profileTotals.fill(0);
    }
    static FILE *csv = []() {""",
    )
    return t


change("src/render/rt64_water_renderer.cpp", renderer)


def capture(t):
    t = replace(
        t,
        "struct CaptureParams { uint width; uint height; uint samples; uint unused; };",
        "struct CaptureParams { uint width; uint height; uint samples; uint unused; uint2 origin; uint2 extent; };",
    )
    return replace(
        t,
        "    if (pixel.x >= params.width || pixel.y >= params.height) return;",
        "    if (any(pixel>=params.extent)) return;\n    pixel+=params.origin;\n    if (pixel.x >= params.width || pixel.y >= params.height) return;",
    )


change("src/shaders/WaterCaptureCS.hlsl", capture)
change("src/shaders/WaterOriginalCS.hlsl", capture)


def reduce(t):
    t = replace(
        t,
        "struct ReduceParams { uint width; uint height; uint sourceWidth; uint sourceHeight; };",
        "struct ReduceParams { uint width; uint height; uint sourceWidth; uint sourceHeight; uint2 origin; uint2 extent; };",
    )
    return replace(
        t,
        "    if (pixel.x >= params.width || pixel.y >= params.height) return;",
        "    if (any(pixel>=params.extent)) return;\n    pixel+=params.origin;\n    if (pixel.x >= params.width || pixel.y >= params.height) return;",
    )


change("src/shaders/WaterDepthReduceCS.hlsl", reduce)


def field(t):
    # Immutable pre-optimisation shader for the headless equivalence probe.
    (root / "patches/enhanced-water/native/WaterFieldReferenceCS.hlsl").write_text(t, newline="\n")
    t = replace(
        t,
        "[numthreads(8,8,1)]\nvoid CSMain(uint2 pixel : SV_DispatchThreadID)",
        """// Racer motion is uniform for all 64 cells in this group. Computing it
// once per racer avoids repeating square roots and segment setup per cell.
groupshared float4 craftPath[WATER_CRAFT_COUNT];
groupshared float4 craftMotion[WATER_CRAFT_COUNT];
groupshared float4 craftResponse[WATER_CRAFT_COUNT];
[numthreads(8,8,1)]
void CSMain(uint2 pixel : SV_DispatchThreadID, uint lane : SV_GroupIndex)""",
    )
    t = replace(
        t,
        "    float cell=params.simulation.w/params.resolution;",
        """    // Reset still injects this step's wake/shore response, like the reference.
    float cell=params.simulation.w/params.resolution;
    float interval=max(m.animation.y-m.animation.x,0.00001);
    float a=saturate((params.simulation.y-m.animation.x)/interval);
    float b=saturate((params.simulation.z-m.animation.x)/interval);
    if(lane<WATER_CRAFT_COUNT) {
        float4 old=m.craftPrevious[lane],current=m.craftCurrent[lane];
        float2 from=lerp(old.xz,current.xz,a),to=lerp(old.xz,current.xz,b);
        float2 delta=current.xz-old.xz;
        float distance=length(delta);
        float2 heading=delta/max(distance,0.001);
        craftPath[lane]=float4(from,to);
        craftMotion[lane]=float4(heading,distance/interval,min(length(to-from)/cell,2)*saturate(current.w*3));
        bool landed=!params.reset && old.w==0 && current.w>0 && params.simulation.y<m.animation.y && params.simulation.z>=m.animation.y;
        craftResponse[lane]=float4(saturate((old.y-current.y)/interval/180),landed ? 1 : 0,current.w,0);
    }
    GroupMemoryBarrierWithGroupSync();
    if (pixel.x>=params.resolution*WATER_CRAFT_COUNT || pixel.y>=params.resolution) return;
    uint tile=pixel.x/params.resolution;
    uint2 local=uint2(pixel.x%params.resolution,pixel.y);
    if(m.craftCurrent[tile].w<0) {nextField[pixel]=0;return;}""",
    )
    t = replace(
        t,
        """    if (pixel.x>=params.resolution*WATER_CRAFT_COUNT || pixel.y>=params.resolution) return;
    uint tile=pixel.x/params.resolution;
    uint2 local=uint2(pixel.x%params.resolution,pixel.y);
    WaterDrawParams draw=waterDraws[params.drawIndex];""",
        """    // Zero-duration reset has no history, swept motion, or shoreline growth.
    // This dispatch-uniform fast path preserves the exact reference result.
    if(params.reset && params.simulation.x==0 && params.simulation.y==params.simulation.z) {
        if(pixel.x<params.resolution*WATER_CRAFT_COUNT && pixel.y<params.resolution)nextField[pixel]=0;
        return;
    }
    // Every other lane reaches the barrier, including a synthetic 1x1 atlas.
    WaterDrawParams draw=waterDraws[params.drawIndex];""",
    )
    t = replace(
        t,
        """    float4 center=filteredSource(previousPixel,tile);
    float4 neighbors=filteredSource(previousPixel+float2(1,0),tile)+filteredSource(previousPixel-float2(1,0),tile)
        +filteredSource(previousPixel+float2(0,1),tile)+filteredSource(previousPixel-float2(0,1),tile);""",
        """    // Origins are snapped to whole cells. Keep the old interpolation
    // fallback for non-aligned input, but use five exact loads on the grid.
    float2 shifted=(draw.fieldOrigins[tile].xy-draw.fieldOrigins[tile].zw)/cell;
    bool aligned=all(shifted==round(shifted));
    int2 at=int2(local)+int2(round(shifted));
    float4 center=aligned ? source(at,tile) : filteredSource(previousPixel,tile);
    float4 neighbors=aligned ? source(at+int2(1,0),tile)+source(at-int2(1,0),tile)+source(at+int2(0,1),tile)+source(at-int2(0,1),tile) :
        filteredSource(previousPixel+float2(1,0),tile)+filteredSource(previousPixel-float2(1,0),tile)
        +filteredSource(previousPixel+float2(0,1),tile)+filteredSource(previousPixel-float2(0,1),tile);""",
    )
    t = replace(
        t,
        """    float interval=max(m.animation.y-m.animation.x,0.00001);
    float a=saturate((params.simulation.y-m.animation.x)/interval);
    float b=saturate((params.simulation.z-m.animation.x)/interval);
    // A separate persistent shore channel""",
        "    // A separate persistent shore channel",
    )
    t = replace(
        t,
        """        float4 old=m.craftPrevious[i], current=m.craftCurrent[i];
        if (current.w<0) continue;
        float2 from=lerp(old.xz,current.xz,a), to=lerp(old.xz,current.xz,b);
        float traveled=length(to-from);
        float2 delta=current.xz-old.xz;
        float speed=length(delta)/interval;
        float2 heading=delta/max(length(delta),0.001);
        float contact=saturate(current.w*3);
        float density=min(traveled/cell,2)*contact;""",
        """        if (craftResponse[i].z<0) continue;
        float2 from=craftPath[i].xy,to=craftPath[i].zw;
        float2 heading=craftMotion[i].xy;
        float speed=craftMotion[i].z,density=craftMotion[i].w;""",
    )
    t = replace(
        t,
        """        bool landed=old.w==0 && current.w>0 && params.simulation.y<m.animation.y && params.simulation.z>=m.animation.y;
        if (landed && !params.reset) {
            float impact=saturate((old.y-current.y)/interval/180);
            float radius=length(world-current.xz);""",
        """        if (craftResponse[i].y>0) {
            float impact=craftResponse[i].x;
            float radius=length(world-m.craftCurrent[i].xz);""",
    )
    return t


change("src/shaders/WaterFieldCS.hlsl", field)


def pixels(t):
    # Exact dimensions are already known; avoid a texture dimensions query
    # inside every SSR search iteration. Reflection sample counts are unchanged.
    return replace(
        t,
        "        uint mipWidth,mipHeight,levels; sceneDepth.GetDimensions(mip,mipWidth,mipHeight,levels);",
        "        uint mipWidth=max(depthWidth>>mip,1u),mipHeight=max(depthHeight>>mip,1u);",
    )


change("src/shaders/WaterPS.hlsl", pixels)


def framebuffer(t):
    t = replace(
        t,
        "                    wp.inverseViewProjection = hlslpp::inverse(wp.viewProjection);",
        "                    if(wp.material.cameraPosition.w!=1.0f)\n                        wp.inverseViewProjection = hlslpp::inverse(wp.viewProjection);",
    )
    start = t.index(
        "                    worker->commandList->setGraphicsPipelineLayout(waterRenderer->layout.get());",
        t.index("if (drawCall.waterIndex != UINT32_MAX)"),
    )
    end = t.index("                    if (selfDepth) waterRenderer->beginColor", start)
    old = t[start:end]
    # The depth prepass remains mandatory for translucent modern water. The
    # mask-only pass is unused when there are no particles to draw.
    oldprefix = """                    worker->commandList->setGraphicsPipelineLayout(waterRenderer->layout.get());"""
    oldend = """                    drawCallTriangles(drawCall);
"""
    body = old[old.index(oldprefix) :]
    body = body.replace(
        "                    const bool classic=waterRenderer->draws[drawCall.waterIndex].material.optics.w>0.5f;\n", ""
    )
    body = body.replace(
        "                    const bool selfDepth=waterRenderer->scenes[waterRenderer->drawScenes[drawCall.waterIndex]].selfDepth;\n",
        "",
    )
    t = (
        t[:start]
        + """                    const bool classic=waterRenderer->draws[drawCall.waterIndex].material.optics.w>0.5f;
                    const bool selfDepth=waterRenderer->scenes[waterRenderer->drawScenes[drawCall.waterIndex]].selfDepth;
                    const bool needsSurface=selfDepth || (!waterRenderer->particles.empty() &&
                        waterRenderer->draws[drawCall.waterIndex].material.effects.x>0.5f);
                    if(needsSurface) {
"""
        + body
        + """                    }
"""
        + t[end:]
    )
    return t


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)


def workload(t):
    return replace(
        t,
        "            rendererGPUProfiler.log(double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0);",
        """            const auto gpuMilliseconds=double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0;
            rendererGPUProfiler.log(gpuMilliseconds);
            // Read only after the existing completed-workload fence. This
            // adds no wait; opt-in instrumentation is disabled by default.
            if(framebufferRenderer->waterRenderer)
                framebufferRenderer->waterRenderer->finishProfile(gpuMilliseconds);""",
    )


change("src/hle/rt64_workload_queue.cpp", workload)

output.write_text("".join(changes), newline="\n")
print("Generated quality-preserving water workload/performance patch.")
