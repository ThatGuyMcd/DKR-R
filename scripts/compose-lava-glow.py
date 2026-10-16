"""Compose cached local glow into the existing RSP colour pass, in authored patches only."""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
output = root / "patches/enhanced-water/dkr-lava-glow.patch"
layers = [output] if "lavaGlowActive" in (stage / "src/hle/rt64_workload.h").read_text() else []
changes = []


def replace(text, before, after):
    if text.count(before) != 1:
        raise SystemExit(f"Non-unique glow context: {before[:120]}")
    return text.replace(before, after, 1)


def change(path, fn):
    before = read_before(stage, path, layers)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No glow change: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    changes.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile=f"a/{path}", tofile=f"b/{path}", n=2))


def workload_header(s):
    s = replace(s, "        std::vector<float> posFloats;", """        // Immutable local irradiance; the existing RSP pass interpolates its pulse.
        std::vector<float> lavaGlow;
        bool lavaGlowActive=false;
        float lavaGlowTime=0,lavaGlowStep=0,lavaGlowStrength=0,lavaGlowSpeed=1,lavaGlowIntensity=0;
        std::vector<float> posFloats;""")
    return replace(s, "        BufferPair normalColorBuffer;", "        BufferPair normalColorBuffer;\n        BufferPair lavaGlowBuffer;")


change("src/hle/rt64_workload.h", workload_header)


def workload(s):
    s = replace(s, "        drawData.posFloats.clear();", """        drawData.lavaGlow.clear();drawData.lavaGlowActive=false;
        drawData.lavaGlowTime=drawData.lavaGlowStep=drawData.lavaGlowStrength=drawData.lavaGlowIntensity=0;
        drawData.lavaGlowSpeed=1;
        drawData.posFloats.clear();""")
    s = replace(s, "    void Workload::uploadDrawData(RenderWorker *worker, BufferUploader *bufferUploader) {",
        """    void Workload::uploadDrawData(RenderWorker *worker, BufferUploader *bufferUploader) {
        // Only lava scenes upload a per-vertex stream; otherwise bind a 4-byte dummy.
        drawData.lavaGlow.resize(drawData.lavaGlowActive ? drawData.vertexCount() : 1,0);""")
    return replace(s, "        bufferUploader->submit(worker, {", """        bufferUploader->submit(worker, {
            {drawData.lavaGlow.data(),{0,drawData.lavaGlow.size()},sizeof(float),RenderBufferFlag::FORMATTED,
             {RenderFormat::R32_FLOAT},&drawBuffers.lavaGlowBuffer},""")


change("src/hle/rt64_workload.cpp", workload)
change("src/hle/rt64_rsp.cpp", lambda s: replace(s,
    "            const uint32_t newIndex = workload.drawData.vertexCount();", """            const uint32_t newIndex = workload.drawData.vertexCount();
            if(workload.drawData.lavaGlowActive) {
                auto& glow=workload.drawData.lavaGlow;
                const float value=globalIndex<glow.size() ? glow[globalIndex] : 0;
                glow.resize(newIndex+1,0);glow[newIndex]=value;
            }"""))


def descriptors(s):
    s = replace(s, "        uint32_t dstWorld;", "        uint32_t dstWorld;\n        uint32_t srcLavaGlow;")
    return replace(s, "            dstWorld = builder.addReadWriteStructuredBuffer(22);",
        "            dstWorld = builder.addReadWriteStructuredBuffer(22);\n            srcLavaGlow = builder.addFormattedBuffer(23);")


change("src/render/rt64_descriptor_sets.h", descriptors)
def process_header(s):
    s = replace(s, "            uint32_t waterWorldActive;", "            uint32_t waterWorldActive;\n            float lavaGlowScale,lavaGlowCooling;\n            uint32_t padding{};")
    return replace(s, "            uint32_t padding{};\n        };",
        '            uint32_t padding{};\n        };\n        static_assert(sizeof(ProcessCB)==32,"RSP HLSL constant rows");')


change("src/render/rt64_rsp_processor.h", process_header)
change("src/render/rt64_shader_library.cpp", lambda s: replace(s,
    """        // RSP Process.
        {
            RSPProcessDescriptorSet descriptorSet;
            layoutBuilder.begin();
            layoutBuilder.addPushConstant(0, 0, sizeof(uint32_t) * 4, RenderShaderStageFlag::COMPUTE);""",
    """        // RSP Process.
        {
            RSPProcessDescriptorSet descriptorSet;
            layoutBuilder.begin();
            // Match both 16-byte HLSL constant rows, including padding.
            layoutBuilder.addPushConstant(0, 0, sizeof(uint32_t) * 8, RenderShaderStageFlag::COMPUTE);"""))


def processor(s):
    s = replace(s, '#include "rt64_buffer_uploader.h"', '#include "rt64_buffer_uploader.h"\n#include "lava_lighting_policy.hpp"')
    s = replace(s, "        processCB.waterWorldActive = p.outputBuffers->waterWorldActive ? 1U : 0U;", """        processCB.waterWorldActive = p.outputBuffers->waterWorldActive ? 1U : 0U;
        const float glowTime=drawData.lavaGlowTime-drawData.lavaGlowStep*(1-std::clamp(p.curFrameWeight,0.0f,1.0f));
        processCB.lavaGlowCooling=dkr::runtime::water::enhanced::lava_cooling(glowTime,drawData.lavaGlowStrength,drawData.lavaGlowSpeed);
        processCB.lavaGlowScale=drawData.lavaGlowActive ? std::clamp(drawData.lavaGlowIntensity,0.0f,3.0f) *
            dkr::runtime::water::enhanced::lava_pulse(glowTime,drawData.lavaGlowStrength,drawData.lavaGlowSpeed) : 0;""")
    return replace(s, "        const uint32_t modifyCount = drawData.modifyCount();", """        processSet->setBuffer(processSet->srcLavaGlow,p.drawBuffers->lavaGlowBuffer.get(),p.drawBuffers->lavaGlowBuffer.getView(0));
        const uint32_t modifyCount = drawData.modifyCount();""")


change("src/render/rt64_rsp_processor.cpp", processor)


def shader(s):
    s = replace(s, "    uint waterWorldActive;", "    uint waterWorldActive;\n    float lavaGlowScale;\n    float lavaGlowCooling;\n    uint reserved;")
    s = replace(s, "RWStructuredBuffer<float4> dstWorld : register(u22);",
        "RWStructuredBuffer<float4> dstWorld : register(u22);\nBuffer<float> srcLavaGlow : register(t23);")
    return replace(s, "    dstCol[vertexOffsetIndex] = vertexColor;", """    if(gConstants.lavaGlowScale>0) {
        const float energy=min(0.18,min(0.14,max(0,srcLavaGlow[vertexOffsetIndex]))*gConstants.lavaGlowScale);
        const float3 warmth=lerp(float3(1,0.19,0.012),float3(1,0.07,0.006),saturate(gConstants.lavaGlowCooling));
        vertexColor.rgb=min(vertexColor.rgb+max(vertexColor.rgb,0.12)*warmth*energy,1);
    }
    dstCol[vertexOffsetIndex] = vertexColor;""")


change("src/shaders/RSPProcessCS.hlsl", shader)


def surface(s):
    s = replace(s, "        float4 molten=lavaSurface(worldPosition.xz,p.camera.w,footprint);",
        """        float4 molten=lavaSurface(worldPosition.xz,p.camera.w,footprint);
        // Surface heat and nearby glow share the interpolated clock. Pulse
        // between hot and cooling palettes, never between lit and black.
        const float cooling=saturate(m.sunColor.w)*0.5*(1-sin(p.camera.w*m.optics.y*1.7));
        molten.rgb=lavaHeatPalette(molten.rgb,cooling)*m.shoreCounts[0].z;""")
    return replace(s, "        float4 traced=hardwareWaterReflection(worldPosition,N,V,p,fallingHit,capturedHit);",
        """        float4 traced=m.shoreCounts[0].y>0.5 ? hardwareWaterReflection(worldPosition,N,V,p,fallingHit,capturedHit) : float4(0,0,0,0);""")


change("src/shaders/WaterPS.hlsl", surface)


change("src/render/rt64_water_renderer.h", lambda s: replace(s,
    "std::array<double,5> profileTotals{};", "std::array<double,6> profileTotals{};"))


def profiler(s):
    s = replace(s, "double milliseconds[4] = {};", "double milliseconds[5] = {};")
    s = replace(s, "for(unsigned i=0;i<4;i++)profileTotals[i]+=milliseconds[i];", "for(unsigned i=0;i<5;i++)profileTotals[i]+=milliseconds[i];")
    s = replace(s, "profileTotals[4]+=frameMilliseconds;", "profileTotals[5]+=frameMilliseconds;")
    s = replace(s, "field-ms=%.3f frame-gpu-ms=%.3f", "field-ms=%.3f lava-ms=%.3f frame-gpu-ms=%.3f")
    s = replace(s, "profileTotals[3]/profileFrames,profileTotals[4]/profileFrames,static_cast<unsigned long long>(allocationBytes())",
        "profileTotals[3]/profileFrames,profileTotals[4]/profileFrames,profileTotals[5]/profileFrames,static_cast<unsigned long long>(allocationBytes())")
    s = replace(s, "field_ms,frame_gpu_ms", "field_ms,lava_ms,frame_gpu_ms")
    s = replace(s, "%.5f,%.5f,%.5f,%.5f,%.5f\\n", "%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\\n")
    return replace(s, "milliseconds[0],milliseconds[1],milliseconds[2],milliseconds[3],frameMilliseconds",
        "milliseconds[0],milliseconds[1],milliseconds[2],milliseconds[3],milliseconds[4],frameMilliseconds")


change("src/render/rt64_water_renderer.cpp", profiler)


def framebuffer(s):
    s = replace(s, "            if(waterRenderer->drawLavaLighting(worker,pendingLava,depthState,framebuffer.viewport)) {",
        """            const auto lavaProfileToken=waterRenderer->beginProfile(worker,4);
            if(waterRenderer->drawLavaLighting(worker,pendingLava,depthState,framebuffer.viewport)) {""")
    return replace(s, "            pendingLava=UINT32_MAX;pendingLavaProjection=UINT32_MAX;",
        "            waterRenderer->endProfile(worker,lavaProfileToken);\n            pendingLava=UINT32_MAX;pendingLavaProjection=UINT32_MAX;")


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)
output.write_text("".join(changes), newline="\n")
print(f"Composed {output.name}: {len(changes)} diff records")
