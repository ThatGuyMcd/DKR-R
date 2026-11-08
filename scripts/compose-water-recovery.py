"""Author bounded water scratch pooling/recovery; dependencies stay read-only."""

import difflib
from pathlib import Path
import sys

from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
output = root / "patches/enhanced-water/dkr-recovery.patch"
layers = [output] if "waterRecovery" in (stage / "src/render/rt64_framebuffer_renderer.h").read_text() else []
changes = []


def replace(t, a, b):
    if t.count(a) != 1:
        raise SystemExit(f"Non-unique recovery context: {a[:120]}")
    return t.replace(a, b, 1)


def change(path, fn):
    before = read_before(stage, path, layers)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No recovery change: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    changes.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
        )
    )


def header(t):
    t = replace(t, '#include "WaterRayScene.h"', '#include "WaterRayScene.h"\n#include "WaterResourcePolicy.h"')
    t = replace(
        t,
        "    std::vector<uint32_t> drawScenes;",
        "    std::set<std::tuple<uint64_t,bool,bool>> rejectedScenes;\n"
        "    uint32_t budgetLogCooldown=0;\n"
        "    std::vector<uint32_t> drawScenes;",
    )
    return t


def resources(t):
    t = replace(
        t,
        "    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear();",
        """    // The preceding workload fence has completed before this reset.
    // Keep only scratch targets actually used, not historical menu targets.
    scenes.resize(water_policy::retainedScenes(uint32_t(scenes.size()),uint32_t(sceneKeys.size())));
    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear(); rejectedScenes.clear();
    if(budgetLogCooldown) --budgetLogCooldown;""",
    )
    a = t.index(
        "    const uint64_t pixels=uint64_t(color->width)*color->height;",
        t.index("uint32_t WaterRenderer::prepareScene"),
    )
    b = t.index("    auto &scene = scenes[index];", a)
    t = (
        t[:a]
        + """    if(rejectedScenes.count(sceneKey))return water_policy::invalidScene;
    const uint32_t index=uint32_t(sceneKeys.size());
    const auto bytes=[](const SceneInputs &s) {
        return s.color ? water_policy::sceneBytes(s.width,s.height,s.surfaceSamples,
            bool(s.originalColor),bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples) : uint64_t(0);
    };
    uint64_t active=0;
    for(uint32_t i=0;i<index;i++)active+=bytes(scenes[i]);
    const uint64_t requested=water_policy::sceneBytes(color->width,color->height,color->multisampling.sampleCount,
        classic,selfDepth,depth->width,depth->height,depth->multisampling.sampleCount);
    if(!water_policy::fits(active,requested,index)) {
        rejectedScenes.insert(sceneKey);
        if(!budgetLogCooldown) {
            std::fprintf(stderr,"[water] target fallback (recoverable): %ux%u samples=%u active=%llu requested=%llu classic=%u isolated=%u\\n",
                color->width,color->height,color->multisampling.sampleCount,
                static_cast<unsigned long long>(active),static_cast<unsigned long long>(requested),classic,selfDepth);
            budgetLogCooldown=240;
        }
        return water_policy::invalidScene;
    }
    // Cached targets not yet assigned in this workload are disposable. Avoid
    // stale resources rejecting a perfectly valid current target.
    uint64_t pooled=active+requested;
    for(uint32_t i=index+1;i<scenes.size();i++)pooled+=bytes(scenes[i]);
    if(pooled>water_policy::sceneBudget)scenes.resize(std::min(size_t(index+1),scenes.size()));
    if(index==scenes.size())scenes.emplace_back();
    auto &cached=scenes[index];
    if(cached.color && (cached.width!=color->width || cached.height!=color->height ||
        cached.surfaceSamples!=color->multisampling.sampleCount || bool(cached.originalColor)!=classic ||
        cached.selfDepth!=selfDepth || (selfDepth && (cached.waterDepthWidth!=depth->width ||
        cached.waterDepthHeight!=depth->height || cached.waterDepthSamples!=depth->multisampling.sampleCount))))
    {
        // Move out first: normal reverse-order destruction releases descriptor
        // sets/views/framebuffers before their textures (assignment does not).
        auto retired=std::move(cached);cached=SceneInputs{};
    }
    // Publish the key only after every texture, framebuffer and binding exists.
    // A failed candidate must never be looked up as a prepared scene.
    try {
"""
        + t[b:]
    )
    t = replace(
        t,
        "    return index;\n}\n\nbool WaterRenderer::captureScene",
        """    sceneKeys.emplace(sceneKey,index);
    return index;
    } catch(...) {
        {auto retired=std::move(scenes[index]);scenes[index]=SceneInputs{};}
        rejectedScenes.insert(sceneKey);
        throw;
    }
}

bool WaterRenderer::captureScene""",
    )
    t = replace(
        t,
        "    scene.surfaceFramebuffer=checkedWaterResource(worker->device->createFramebuffer(RenderFramebufferDesc(&surfaceAttachment,1,",
        """    const bool attachmentsChanged=scene.sourceColor!=color || scene.sourceDepth!=depth ||
        scene.sourceColorTexture!=color->texture.get() || scene.sourceDepthTexture!=depth->texture.get() ||
        scene.sourceColorRevision!=color->textureRevision || scene.sourceDepthRevision!=depth->textureRevision;
    if(!scene.surfaceFramebuffer || attachmentsChanged)
    scene.surfaceFramebuffer=checkedWaterResource(worker->device->createFramebuffer(RenderFramebufferDesc(&surfaceAttachment,1,""",
    )
    t = replace(
        t,
        "    if (scene.selfDepth) {\n        const RenderTexture *colorAttachment=color->texture.get();",
        "    if (scene.selfDepth && (!scene.waterColorFramebuffer || attachmentsChanged)) {\n        const RenderTexture *colorAttachment=color->texture.get();",
    )
    t = replace(
        t,
        "    else scene.waterColorFramebuffer.reset();",
        "    else if(!scene.selfDepth) scene.waterColorFramebuffer.reset();",
    )
    t = replace(
        t,
        "    scene.sourceColor = color; scene.sourceDepth = depth;",
        "    scene.sourceColor = color; scene.sourceDepth = depth;\n"
        "    scene.sourceColorTexture=color->texture.get();scene.sourceDepthTexture=depth->texture.get();\n"
        "    scene.sourceColorRevision=color->textureRevision;scene.sourceDepthRevision=depth->textureRevision;",
    )
    return t


# Track the attachment, not just the wrapper which may be reused after resize.
def attachment(t):
    return replace(
        t,
        "        RenderTarget *sourceColor = nullptr, *sourceDepth = nullptr;",
        "        RenderTarget *sourceColor = nullptr, *sourceDepth = nullptr;\n"
        "        RenderTexture *sourceColorTexture=nullptr,*sourceDepthTexture=nullptr;\n"
        "        uint64_t sourceColorRevision=0,sourceDepthRevision=0;",
    )


change("src/render/rt64_water_renderer.h", lambda t: attachment(header(t)))
change("src/render/rt64_water_renderer.cpp", resources)


def framebuffer_header(t):
    t = replace(t, "#pragma once", '#pragma once\n#include "WaterResourcePolicy.h"')
    return replace(
        t,
        "        bool waterUnavailable = false;",
        "        bool waterUnavailable = false; // shader/device support only\n        water_policy::Recovery waterRecovery;",
    )


change("src/render/rt64_framebuffer_renderer.h", framebuffer_header)


def framebuffer(t):
    t = replace(
        t,
        "        if (waterRenderer) waterRenderer->reset();",
        "        waterRecovery.beginFrame();\n        if (waterRenderer) waterRenderer->reset();",
    )
    t = replace(
        t,
        "if (wantsWater && waterRenderer && !waterUnavailable && waterSamplesSupported)",
        "if (wantsWater && waterRenderer && !waterUnavailable && waterRecovery.ready() && waterSamplesSupported)",
    )
    t = replace(
        t,
        "                        instanceDrawCall.originalWaterPipeline=triangles.pipeline;",
        "                        if(waterScene!=water_policy::invalidScene) {\n                        instanceDrawCall.originalWaterPipeline=triangles.pipeline;",
    )
    t = replace(
        t,
        "                        waterRenderer->worldView = RenderVertexBufferView(RenderBufferReference(outputBuffers.rasterWorldPosBuffer.buffer.get()), PosStride * vertexCount);",
        "                        waterRenderer->worldView = RenderVertexBufferView(RenderBufferReference(outputBuffers.rasterWorldPosBuffer.buffer.get()), PosStride * vertexCount);\n                        }",
    )
    t = replace(
        t,
        '                        waterUnavailable=true;\n                        std::fprintf(stderr,"[water] resource fallback',
        '                        waterRecovery.fail();\n                        std::fprintf(stderr,"[water] resource fallback',
    )
    t = replace(
        t,
        '                if (waterUnavailable) throw std::runtime_error("water preparation was incomplete");',
        '                if (waterRecovery.failed) throw std::runtime_error("water preparation was incomplete; bounded retry scheduled");',
    )
    return replace(
        t,
        '                waterUnavailable=true;\n                std::fprintf(stderr,"[water] frame fallback',
        '                waterRecovery.fail();\n                std::fprintf(stderr,"[water] frame fallback',
    )


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)
output.write_text("".join(changes))
print(f"Authored {output}; no dependency/stage files changed.")
