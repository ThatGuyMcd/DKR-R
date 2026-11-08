"""Compose local ray-tested lava lighting; never edit dependency/generated sources."""

import difflib
from pathlib import Path
import sys

from water_patch_baseline import read_before

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
output = root / "patches/enhanced-water/dkr-lava-lighting.patch"
layers = [output] if "finishLava" in (stage / "src/render/rt64_framebuffer_renderer.cpp").read_text() else []
if "lavaGlowActive" in (stage / "src/hle/rt64_workload.h").read_text():
    layers.insert(0, root / "patches/enhanced-water/dkr-lava-glow.patch")
changes = []


def replace(text, before, after):
    if text.count(before) != 1:
        raise SystemExit(f"Non-unique lighting context: {before[:120]}")
    return text.replace(before, after, 1)


def change(path, fn):
    overlay = None
    if layers and path == "src/render/rt64_framebuffer_renderer.cpp":
        overlay = (stage / path).read_text()
        if "                instanceDrawCall.worldProjection=false;" not in overlay:
            overlay = replace(
                overlay,
                "                instanceDrawCall.waterIndex = UINT32_MAX;",
                "                instanceDrawCall.waterIndex = UINT32_MAX;\n                instanceDrawCall.worldProjection=false;\n                instanceDrawCall.projectionIndex=UINT32_MAX;",
            )
    before = read_before(stage, path, layers, overlay)
    after = fn(before)
    if before == after:
        raise SystemExit(f"No lighting change: {path}")
    changes.append(f"diff --git a/{path} b/{path}\n")
    # Narrow context preserves Android's adjacent draw-metrics instrumentation.
    # Every edited semantic anchor above is still required to be unique.
    changes.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=1
        )
    )


change(
    "src/render/rt64_framebuffer_renderer_call.h",
    lambda s: replace(
        s,
        "        uint32_t waterIndex = UINT32_MAX;",
        "        uint32_t waterIndex = UINT32_MAX;\n        bool worldProjection = false;\n        uint32_t projectionIndex = UINT32_MAX;",
    ),
)


def header(s):
    s = replace(
        s,
        "struct WaterDescriptorSet : RenderDescriptorSetBase {",
        """struct LavaLightingSet : RenderDescriptorSetBase {
    uint32_t output,input;
    explicit LavaLightingSet(RenderDevice *device=nullptr) {
        builder.begin();output=builder.addReadWriteTexture(1);input=builder.addTexture(2);builder.end();
        if(device)create(device);
    }
};
struct WaterDescriptorSet : RenderDescriptorSetBase {""",
    )
    s = replace(
        s,
        "        std::unique_ptr<RenderTexture> originalColor;",
        """        std::unique_ptr<RenderTexture> originalColor, lavaLight;
        std::unique_ptr<RenderTextureView> lavaLightView;
        std::unique_ptr<LavaLightingSet> lavaLightSet;
        uint32_t lavaWidth=0,lavaHeight=0;
        bool lavaPrepared=false;""",
    )
    return replace(
        s,
        "    WaterRayScene rays;",
        """    WaterRayScene rays;
    std::unique_ptr<RenderShader> lavaLightingVS,lavaLightingPS,lavaLightingCS;
    std::unique_ptr<RenderPipelineLayout> lavaLightingLayout;
    std::unique_ptr<RenderPipeline> lavaLightingCompute;
    std::map<uint32_t,std::unique_ptr<RenderPipeline>> lavaLightingPipelines;
    bool lavaLightingUnavailable=false;
    RenderPipeline *lavaLightingPipeline(RenderDevice *device,RenderShaderFormat format,const RenderMultisampling& samples);
    bool prepareLavaLighting(RenderWorker *worker,uint32_t sceneIndex,const interop::WaterDrawParams& draw);
    bool drawLavaLighting(RenderWorker *worker,uint32_t drawIndex,bool depthReadOnly,const RenderViewport& viewport);
""",
    )


change("src/render/rt64_water_renderer.h", header)

implementation = r"""
RenderPipeline *WaterRenderer::lavaLightingPipeline(RenderDevice *device,RenderShaderFormat format,const RenderMultisampling& samples) {
    if(lavaLightingUnavailable || !device->getCapabilities().rayQuery || !rayLayout)return nullptr;
    const uint32_t key=uint32_t(samples.sampleCount);
    if(auto it=lavaLightingPipelines.find(key);it!=lavaLightingPipelines.end())return it->second.get();
    try {
        if(!lavaLightingVS || !lavaLightingPS) {
            lavaLightingVS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(LavaLightingVSBlobDXIL,LavaLightingVSBlobSPIRV,LavaLightingVSBlobMSL,"VSMain",format)));
            lavaLightingPS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(LavaLightingPSBlobDXIL,LavaLightingPSBlobSPIRV,LavaLightingPSBlobMSL,"PSMain",format)));
            lavaLightingCS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(LavaLightingCSBlobDXIL,LavaLightingCSBlobSPIRV,LavaLightingCSBlobMSL,"CSMain",format)));
            LavaLightingSet lightSet;WaterRaySet raySet;
            RenderPipelineLayoutBuilder builder;builder.begin(false,true);
            builder.addPushConstant(0,0,sizeof(uint32_t)*4,RenderShaderStageFlag::COMPUTE|RenderShaderStageFlag::VERTEX|RenderShaderStageFlag::PIXEL);
            builder.addDescriptorSet(*descriptorSet);builder.addDescriptorSet(raySet);builder.addDescriptorSet(lightSet);builder.end();
            lavaLightingLayout=checkedWaterResource(builder.create(device));
            lavaLightingCompute=checkedWaterResource(device->createComputePipeline(RenderComputePipelineDesc(lavaLightingLayout.get(),lavaLightingCS.get(),8,8,1)));
        }
        RenderGraphicsPipelineDesc desc;
        desc.pipelineLayout=lavaLightingLayout.get();desc.vertexShader=lavaLightingVS.get();desc.pixelShader=lavaLightingPS.get();
        desc.renderTargetCount=1;desc.renderTargetFormat[0]=RenderTarget::colorBufferFormat(usesHDR);
        desc.renderTargetBlend[0]=RenderBlendDesc::Copy();desc.multisampling=samples;
        desc.depthEnabled=false;desc.depthWriteEnabled=false;desc.depthTargetFormat=RenderFormat::D32_FLOAT;
        desc.depthClipEnabled=false;desc.cullMode=RenderCullMode::NONE;desc.primitiveTopology=RenderPrimitiveTopology::TRIANGLE_LIST;
        auto result=device->createGraphicsPipeline(desc);
        if(!waterGraphicsPipelineValid(result.get()))throw std::runtime_error("lava lighting pipeline rejected");
        auto* pointer=result.get();lavaLightingPipelines.emplace(key,std::move(result));return pointer;
    } catch(const std::exception& error) {
        lavaLightingUnavailable=true;
        std::fprintf(stderr,"[water][lava-lighting] retaining emissive lava without scene lighting: %s\n",error.what());
        return nullptr;
    }
}
bool WaterRenderer::prepareLavaLighting(RenderWorker *worker,uint32_t index,const interop::WaterDrawParams& draw) {
    if(index>=scenes.size() || !lavaLightingCompute)return false;
    auto& scene=scenes[index];
    const float q=draw.material.shoreCounts[0].x;
    const uint32_t divisor=q>=12 ? 16 : q>=6 ? 8 : 4;
    const uint32_t width=(scene.width+divisor-1)/divisor,height=(scene.height+divisor-1)/divisor;
    if(scene.lavaLight && scene.lavaWidth==width && scene.lavaHeight==height) {scene.lavaPrepared=true;return true;}
    if(scene.lavaPrepared)return false; // Earlier batches already own this descriptor.
    // A shared scratch image per scene, never per batch/light or MSAA sample.
    // Account this optional buffer inside the existing aggregate safety budget.
    uint64_t used=0;
    for(const auto& entry:sceneKeys) {
        const auto& s=scenes[entry.second];
        used+=water_policy::scratchBytes(s.width,s.height,s.surfaceSamples,s.classic,s.captureOwner!=entry.second,
            bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight);
        if(entry.second!=index)used+=uint64_t(s.lavaWidth)*s.lavaHeight*16;
    }
    if(used>water_policy::sceneBudget || uint64_t(width)*height*16>water_policy::sceneBudget-used)return false;
    // Unused pooled scenes are from the already completed preceding workload.
    // Do not let adding lighting make their retained scratch exceed the cap.
    uint64_t retained=used+uint64_t(width)*height*16;
    for(uint32_t i=uint32_t(sceneKeys.size());i<scenes.size();++i) {
        const auto& s=scenes[i];
        retained+=water_policy::scratchBytes(s.width,s.height,s.surfaceSamples,s.classic,s.captureOwner!=i,
            bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight);
        retained+=uint64_t(s.lavaWidth)*s.lavaHeight*16;
    }
    if(retained>water_policy::sceneBudget)scenes.resize(sceneKeys.size());
    // This image is not yet referenced by a prepared batch. Retire it before
    // allocating its replacement; never temporarily double the lighting budget.
    scene.lavaLightSet.reset();scene.lavaLightView.reset();scene.lavaLight.reset();
    scene.lavaWidth=scene.lavaHeight=0;
    try {
    // Alpha holds perspective depth, not colour: half precision can round
    // valid distant receivers to sky or reconstruct a different world point.
    auto texture=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::Texture2D(width,height,1,RenderFormat::R32G32B32A32_FLOAT,RenderTextureFlag::STORAGE|RenderTextureFlag::UNORDERED_ACCESS)));
    auto view=checkedWaterResource(texture->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R32G32B32A32_FLOAT)));
    auto set=std::make_unique<LavaLightingSet>(worker->device);
    set->setTexture(set->output,texture.get(),RenderTextureLayout::GENERAL,view.get());
    set->setTexture(set->input,texture.get(),RenderTextureLayout::SHADER_READ,view.get());
    scene.lavaLightSet=std::move(set);scene.lavaLightView=std::move(view);scene.lavaLight=std::move(texture);
    scene.lavaWidth=width;scene.lavaHeight=height;scene.lavaPrepared=true;return true;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"[water][lava-lighting] optional buffer unavailable; retaining enhanced surface: %s\n",error.what());
        return false;
    }
}
bool WaterRenderer::drawLavaLighting(RenderWorker *worker,uint32_t drawIndex,bool depthReadOnly,const RenderViewport& viewport) {
    if(drawIndex>=draws.size() || drawIndex>=drawScenes.size() || !rays.set || lavaLightingUnavailable)return false;
    auto& scene=scenes[drawScenes[drawIndex]];
    const auto& d=draws[drawIndex];
    if(d.material.effects.z!=5 || d.material.effects.w<0.5f || d.material.sunDirection.w<=0 || !scene.sourceColor)return false;
    // Pipeline/resource creation belongs to workload preparation, never record.
    auto it=lavaLightingPipelines.find(uint32_t(scene.sourceColor->multisampling.sampleCount));
    if(it==lavaLightingPipelines.end() || !it->second || !scene.lavaLightSet)return false;
    const auto bounds=lavaLightingBounds(d);
    if(bounds[2]<=bounds[0] || bounds[3]<=bounds[1])return false;
    // Fresh capture after world geometry, before orthographic HUD. It shares
    // existing viewport scratch resources; no new full-resolution attachment.
    scene.captured=false;scenes[scene.captureOwner].opticalCaptured=false;
    captureScene(worker,drawIndex,depthReadOnly);
    worker->commandList->setFramebuffer(nullptr);
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(scene.lavaLight.get(),RenderTextureLayout::GENERAL));
    worker->commandList->setComputePipelineLayout(lavaLightingLayout.get());
    worker->commandList->setComputeDescriptorSet(materialSet(drawIndex),0);
    worker->commandList->setComputeDescriptorSet(rays.set->get(),1);
    worker->commandList->setComputeDescriptorSet(scene.lavaLightSet->get(),2);
    worker->commandList->setPipeline(lavaLightingCompute.get());
    const float quality=d.material.shoreCounts[0].x;
    const uint32_t divisor=quality>=12 ? 16 : quality>=6 ? 8 : 4;
    const uint32_t x=uint32_t(std::max(0.0f,std::max(float(d.viewBounds.x/divisor),std::floor(bounds[0]/divisor)-1)));
    const uint32_t y=uint32_t(std::max(0.0f,std::max(float(d.viewBounds.y/divisor),std::floor(bounds[1]/divisor)-1)));
    const uint32_t right=std::min(scene.lavaWidth,uint32_t(std::min(float(std::ceil(d.viewBounds.z/divisor)),std::ceil(bounds[2]/divisor)+1)));
    const uint32_t bottom=std::min(scene.lavaHeight,uint32_t(std::min(float(std::ceil(d.viewBounds.w/divisor)),std::ceil(bounds[3]/divisor)+1)));
    if(right<=x || bottom<=y)return false;
    const std::array<uint32_t,4> pc{drawIndex,x,y,0};worker->commandList->setComputePushConstants(0,pc.data());
    worker->commandList->dispatch((right-x+7)/8,(bottom-y+7)/8,1);
    worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(scene.lavaLight.get(),RenderTextureLayout::SHADER_READ));
    worker->commandList->setViewports(viewport);
    worker->commandList->setScissors(RenderRect(int32_t(bounds[0]),int32_t(bounds[1]),int32_t(bounds[2]),int32_t(bounds[3])));
    worker->commandList->setGraphicsPipelineLayout(lavaLightingLayout.get());
    worker->commandList->setGraphicsDescriptorSet(materialSet(drawIndex),0);
    worker->commandList->setGraphicsDescriptorSet(rays.set->get(),1);
    worker->commandList->setGraphicsDescriptorSet(scene.lavaLightSet->get(),2);
    worker->commandList->setPipeline(it->second.get());
    worker->commandList->setGraphicsPushConstants(0,pc.data());
    return true;
}
"""


# drawLavaLighting prepares bindings; caller binds the target and issues the draw.
def renderer(s):
    s = replace(
        s, '#include "rt64_water_renderer.h"', '#include "rt64_water_renderer.h"\n#include "LavaLightingBounds.h"'
    )
    s = replace(
        s,
        "scene.captured=false; scene.opticalCaptured=false; scene.surfaceCleared=false;",
        "scene.captured=false; scene.opticalCaptured=false; scene.surfaceCleared=false; scene.lavaPrepared=false;",
    )
    for fmt in ("spirv", "dxil"):
        s = replace(
            s,
            f'#include "shaders/WaterHardwarePS.hlsl.{fmt}.h"',
            f'#include "shaders/WaterHardwarePS.hlsl.{fmt}.h"\n#include "shaders/LavaLightingVS.hlsl.{fmt}.h"\n#include "shaders/LavaLightingPS.hlsl.{fmt}.h"\n#include "shaders/LavaLightingCS.hlsl.{fmt}.h"',
        )
    s = replace(
        s,
        "bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight) : uint64_t(0);",
        "bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight)+uint64_t(s.lavaWidth)*s.lavaHeight*16 : uint64_t(0);",
    )
    s = replace(
        s,
        "classic,owner!=index,selfDepth,depth->width,depth->height,depth->multisampling.sampleCount,ow,oh);",
        "classic,owner!=index,selfDepth,depth->width,depth->height,depth->multisampling.sampleCount,ow,oh)+(existing ? uint64_t(scenes[index].lavaWidth)*scenes[index].lavaHeight*16 : 0);",
    )
    return replace(s, "bool WaterRenderer::prepareRays(", implementation + "\nbool WaterRenderer::prepareRays(")


change("src/render/rt64_water_renderer.cpp", renderer)

finish = r"""        uint32_t pendingLava=UINT32_MAX,pendingLavaProjection=UINT32_MAX;
        auto finishLava = [&]() {
            if(pendingLava==UINT32_MAX)return;
            finishWater();
            if(waterRenderer->drawLavaLighting(worker,pendingLava,depthState,framebuffer.viewport)) {
                worker->commandList->setFramebuffer(depthState ? fbStorage->colorWriteDepthRead.get() : fbStorage->colorDepthWrite.get());
                worker->commandList->drawInstanced(3,1,0,0);
            }
            pendingLava=UINT32_MAX;pendingLavaProjection=UINT32_MAX;
            switchToGraphicsPipeline();
        };

"""


def framebuffer(s):
    s = replace(
        s,
        "                instanceDrawCall.waterIndex = UINT32_MAX;",
        """                instanceDrawCall.waterIndex = UINT32_MAX;
                instanceDrawCall.worldProjection=false;
                instanceDrawCall.projectionIndex=UINT32_MAX;""",
    )
    s = replace(
        s,
        "        for (uint32_t i : rasterScene.instanceIndices) {",
        finish + "        for (uint32_t i : rasterScene.instanceIndices) {",
    )
    s = replace(
        s,
        "            const InstanceDrawCall &drawCall = instanceDrawCallVector[i];",
        """            const InstanceDrawCall &drawCall = instanceDrawCallVector[i];
            if(pendingLava!=UINT32_MAX && (!drawCall.worldProjection || drawCall.projectionIndex!=pendingLavaProjection))finishLava();
            if(pendingLava==UINT32_MAX && drawCall.waterIndex!=UINT32_MAX) {
                const auto& m=waterRenderer->draws[drawCall.waterIndex].material;
                if(m.effects.z==5 && m.effects.w>0.5f && m.sunDirection.w>0) {
                    pendingLava=drawCall.waterIndex;pendingLavaProjection=drawCall.projectionIndex;
                }
            }""",
    )
    s = replace(
        s,
        "        // Mark targets for resolve.\n        finishWater();",
        "        // Mark targets for resolve.\n        finishLava();\n        finishWater();",
    )
    s = replace(
        s,
        "                        switch (proj.type) {",
        """                        instanceDrawCall.worldProjection=proj.type==Projection::Type::Perspective;
                        instanceDrawCall.projectionIndex=proj.transformsIndex;
                        switch (proj.type) {""",
    )
    s = replace(
        s,
        "bool hardware=!classic && wp.material.appearance.x>0 && wp.material.effects.w>0.5f &&",
        "bool hardware=!classic && (wp.material.appearance.x>0 || (wp.material.effects.z==5 && wp.material.sunDirection.w>0)) && wp.material.effects.w>0.5f &&",
    )
    s = replace(
        s,
        "                    wp.material.effects.w=hardware ? 1.0f : 0.0f;",
        """                    wp.material.effects.w=hardware ? 1.0f : 0.0f;
                    if(hardware && wp.material.effects.z==5 && wp.material.sunDirection.w>0 &&
                       !waterRenderer->lavaLightingPipeline(p.worker->device,p.rasterShaderCache->shaderFormat,p.fbStorage->colorTarget->multisampling))
                        wp.material.sunDirection.w=0;""",
    )
    s = replace(
        s,
        "                        if(waterScene!=water_policy::invalidScene) {",
        """                        if(waterScene!=water_policy::invalidScene) {
                        if(hardware && wp.material.effects.z==5 && wp.material.sunDirection.w>0 &&
                           !waterRenderer->prepareLavaLighting(p.worker,waterScene,wp))wp.material.sunDirection.w=0;""",
    )
    return s


change("src/render/rt64_framebuffer_renderer.cpp", framebuffer)


def lava_shader(s):
    s = replace(
        s,
        "        float4 reflection=0;\n#ifdef HARDWARE_WATER",
        "        float4 reflection=0;\n        if(m.appearance.x>0) {\n#ifdef HARDWARE_WATER",
    )
    return replace(s, "#endif\n        float fresnel=0.04", "#endif\n        }\n        float fresnel=0.04")


change("src/shaders/WaterPS.hlsl", lava_shader)
output.write_text("".join(changes), newline="\n")
print(f"Composed {output.name}: {len(changes)} diff records")
