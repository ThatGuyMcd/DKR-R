"""Author the final water stability/tuning layer; never edit staged/dependency sources."""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root=Path(__file__).resolve().parents[1]
stage=Path(sys.argv[1]).resolve()
output=root/'patches/enhanced-water/dkr-stability.patch'
layers=[]
if 'lavaSurface' in (stage/'src/shaders/WaterPS.hlsl').read_text():
    layers.append(root/'patches/enhanced-water/dkr-lava.patch')
if 'waterfallPipeline' in (stage/'src/render/rt64_water_renderer.h').read_text():
    layers.append(root/'patches/enhanced-water/dkr-waterfall.patch')
if 'captureOwner' in (stage/'src/render/rt64_water_renderer.h').read_text():
    layers.append(output)
changes=[]
def replace(t,a,b):
    if t.count(a)!=1:raise SystemExit(f'Non-unique stability context: {a[:150]}')
    return t.replace(a,b,1)
def change(path,fn):
    before=read_before(stage,path,layers);after=fn(before)
    if before==after:raise SystemExit(f'No stability change: {path}')
    changes.append(f'diff --git a/{path} b/{path}\n')
    changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=f'a/{path}',tofile=f'b/{path}',n=3))

def params(t):
    t=replace(t,'    float4 effects;', '    float4 performance;    // SSR steps, wake resolution, hardware distance, spray tier\n    float4 appearance;     // reflection, refraction, wake and waterfall strengths\n    float4 effects;')
    t=replace(t,'    float4 viewBounds;', '    float4 originalRegion; // compact original capture origin XY, atlas cell size ZW\n    float4 viewBounds;')
    t=t.replace('sizeof(WaterMaterial) == 1552','sizeof(WaterMaterial) == 1584').replace('sizeof(WaterDrawParams) == 2000','sizeof(WaterDrawParams) == 2048')
    return t
change('src/shared/rt64_water_params.h',params)

def header(t):
    t=replace(t,'        std::unique_ptr<RenderTexture> color, depth, originalColor;',
        '        std::shared_ptr<RenderTexture> color,depth;\n        std::unique_ptr<RenderTexture> originalColor;\n        uint32_t originalWidth=0,originalHeight=0,captureOwner=UINT32_MAX;\n        bool classic=false,opticalCaptured=false;\n        uint32_t opticalCamera=UINT32_MAX;\n        interop::float4 opticalView{};')
    t=replace(t,'        std::unique_ptr<RenderTexture> surface;', '        std::shared_ptr<RenderTexture> surface;')
    t=replace(t,'bool classic, bool selfDepth);','bool classic, bool selfDepth,water_work::Region originalRegion={});')
    t=replace(t,'    bool rayUnavailable=false;','    bool rayUnavailable=false;\n    uint32_t rayRetry=0;')
    return t
change('src/render/rt64_water_renderer.h',header)

prepare=r'''uint32_t WaterRenderer::prepareScene(RenderWorker *worker, RenderTarget *color, RenderTarget *depth, uint64_t key, bool classic, bool selfDepth,water_work::Region originalRegion) {
    const auto sceneKey=std::make_tuple(key,classic,selfDepth);
    if(rejectedScenes.count(sceneKey))return water_policy::invalidScene;
    const auto found=sceneKeys.find(sceneKey);
    const bool existing=found!=sceneKeys.end();
    const uint32_t index=existing ? found->second : uint32_t(sceneKeys.size());
    uint32_t owner=index;
    if(!classic) for(const auto& entry:sceneKeys) {
        const auto& candidate=scenes[entry.second];
        if(!candidate.classic && std::get<0>(entry.first)==key) {owner=candidate.captureOwner;break;}
    }
    const auto bytes=[&](uint32_t i) {
        const auto& s=scenes[i];
        return s.color ? water_policy::scratchBytes(s.width,s.height,s.surfaceSamples,s.classic,s.captureOwner!=i,
            bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight) : uint64_t(0);
    };
    uint64_t active=0;
    for(const auto& entry:sceneKeys)if(entry.second!=index)active+=bytes(entry.second);
    uint32_t ow=classic ? water_policy::bucketExtent(originalRegion.width) : 0;
    uint32_t oh=classic ? water_policy::bucketExtent(originalRegion.height) : 0;
    if(existing && classic) {ow=std::max(ow,scenes[index].originalWidth);oh=std::max(oh,scenes[index].originalHeight);}
    const auto requested=water_policy::scratchBytes(color->width,color->height,color->multisampling.sampleCount,
        classic,owner!=index,selfDepth,depth->width,depth->height,depth->multisampling.sampleCount,ow,oh);
    if(!water_policy::fits(active,requested,uint32_t(sceneKeys.size())-(existing ? 1 : 0))) {
        // Never remove an earlier prepared draw if a later atlas growth fails.
        if(!existing)rejectedScenes.insert(sceneKey);
        if(!budgetLogCooldown) {
            std::fprintf(stderr,"[water] scratch fallback: %ux%u samples=%u active=%llu requested=%llu vertical=%u shared=%u region=%ux%u\n",
                color->width,color->height,color->multisampling.sampleCount,(unsigned long long)active,
                (unsigned long long)requested,classic,owner!=index,ow,oh);budgetLogCooldown=240;
        }
        ++dkr::runtime::water::enhanced::scratch_fallbacks;
        return water_policy::invalidScene;
    }
    // Discard unused pooled modes only at this completed-workload boundary.
    if(!existing && index<scenes.size()) {
        uint64_t retained=active+requested;
        for(uint32_t i=index+1;i<scenes.size();i++)retained+=bytes(i);
        if(retained>water_policy::sceneBudget)scenes.resize(index+1);
    }
    if(index==scenes.size())scenes.emplace_back();
    auto& cached=scenes[index];
    const uint32_t width=classic ? 1 : color->width,height=classic ? 1 : color->height;
    const bool incompatible=cached.color && (cached.width!=width || cached.height!=height || cached.classic!=classic ||
        cached.surfaceSamples!=color->multisampling.sampleCount || cached.selfDepth!=selfDepth ||
        (owner!=index && cached.color!=scenes[owner].color));
    if(incompatible) {
        if(existing)return water_policy::invalidScene; // Earlier draws still own this descriptor.
        // Unused pooled aliases may retain the old root's full optical/mask
        // allocation. Retire them before allocating an incompatible root.
        if(owner==index)scenes.resize(index+1);
        auto retired=std::move(cached);cached=SceneInputs{};
    }
    try {
    auto& scene=scenes[index];scene.classic=classic;scene.selfDepth=selfDepth;scene.captureOwner=owner;
    scene.surfaceSamples=color->multisampling.sampleCount;
    if(!scene.color) {
        scene.width=width;scene.height=height;
        if(owner!=index) {
            scene.color=scenes[owner].color;scene.depth=scenes[owner].depth;scene.surface=scenes[owner].surface;
            scene.mipCount=scenes[owner].mipCount;
        } else {
            scene.color=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::Texture2D(width,height,1,RenderFormat::R16G16B16A16_FLOAT,RenderTextureFlag::UNORDERED_ACCESS)));
            scene.mipCount=classic ? 1 : std::min(6u,1u+uint32_t(std::floor(std::log2(float(std::min(width,height))))));
            scene.depth=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::Texture2D(width,height,scene.mipCount,RenderFormat::R32_FLOAT,RenderTextureFlag::UNORDERED_ACCESS)));
            if(!classic)scene.surface=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::ColorTarget(width,height,RenderFormat::R32_FLOAT,color->multisampling)));
        }
        scene.colorView=checkedWaterResource(scene.color->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R16G16B16A16_FLOAT)));
        scene.depthView=checkedWaterResource(scene.depth->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R32_FLOAT)));
        for(uint32_t mip=0;mip<scene.mipCount;mip++) {
            auto view=RenderTextureViewDesc::Texture2D(RenderFormat::R32_FLOAT);view.mipSlice=mip;view.mipLevels=1;
            scene.depthMipViews.push_back(checkedWaterResource(scene.depth->createTextureView(view)));
            if(mip>0) {
                auto set=std::make_unique<WaterDepthReduceSet>(worker->device);
                set->setTexture(set->input,scene.depth.get(),RenderTextureLayout::GENERAL,scene.depthMipViews[mip-1].get());
                set->setTexture(set->output,scene.depth.get(),RenderTextureLayout::GENERAL,scene.depthMipViews[mip].get());
                scene.reduceSets.push_back(std::move(set));
            }
        }
        scene.captureSet=std::make_unique<WaterCaptureSet>(worker->device);
        scene.materialSet=std::make_unique<WaterDescriptorSet>(worker->device);
        if(!classic) {
            scene.surfaceView=checkedWaterResource(scene.surface->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R32_FLOAT)));
            scene.surfaceWidth=width;scene.surfaceHeight=height;scene.spraySet=std::make_unique<WaterSpraySet>(worker->device);
        }
    }
    if(classic && (!scene.originalColor || scene.originalWidth<ow || scene.originalHeight<oh || scene.originalSamples!=color->multisampling.sampleCount)) {
        // Allocate replacement transactionally before retiring the old descriptor/view.
        const auto samples=color->multisampling.sampleCount;
        auto texture=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::Texture2D(ow*(samples>1 ? 2 : 1),oh*((samples+1)/2),1,
            RenderFormat::R16G16B16A16_FLOAT,RenderTextureFlag::UNORDERED_ACCESS)));
        auto view=checkedWaterResource(texture->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R16G16B16A16_FLOAT)));
        auto set=std::make_unique<WaterOriginalSet>(worker->device);
        scene.originalSet.reset();scene.originalColorView.reset();scene.originalColor.reset();
        scene.originalColor=std::move(texture);scene.originalColorView=std::move(view);scene.originalSet=std::move(set);
        scene.originalSamples=samples;scene.originalWidth=ow;scene.originalHeight=oh;
    }
    if(selfDepth && (!scene.waterDepth || scene.waterDepthWidth!=depth->width || scene.waterDepthHeight!=depth->height || scene.waterDepthSamples!=depth->multisampling.sampleCount)) {
        scene.surfaceFramebuffer.reset();scene.waterColorFramebuffer.reset();
        scene.waterDepth=checkedWaterResource(worker->device->createTexture(RenderTextureDesc::DepthTarget(depth->width,depth->height,RenderFormat::D32_FLOAT,depth->multisampling)));
        scene.waterDepthWidth=depth->width;scene.waterDepthHeight=depth->height;scene.waterDepthSamples=depth->multisampling.sampleCount;
    }
    const bool changed=scene.sourceColorTexture!=color->texture.get() || scene.sourceDepthTexture!=depth->texture.get() ||
        scene.sourceColorRevision!=color->textureRevision || scene.sourceDepthRevision!=depth->textureRevision;
    if(!classic) {
        const RenderTexture* attachment=scene.surface.get();
        if(!scene.surfaceFramebuffer || changed)scene.surfaceFramebuffer=checkedWaterResource(worker->device->createFramebuffer(RenderFramebufferDesc(&attachment,1,
            selfDepth ? scene.waterDepth.get() : depth->texture.get(),!selfDepth)));
        if(selfDepth && (!scene.waterColorFramebuffer || changed)) {
            const RenderTexture* output=color->texture.get();
            scene.waterColorFramebuffer=checkedWaterResource(worker->device->createFramebuffer(RenderFramebufferDesc(&output,1,scene.waterDepth.get(),true)));
        }
        scene.spraySet->setTexture(scene.spraySet->sceneDepth,scene.depth.get(),RenderTextureLayout::SHADER_READ,scene.depthMipViews[0].get());
        scene.spraySet->setTexture(scene.spraySet->waterDepth,scene.surface.get(),RenderTextureLayout::SHADER_READ,scene.surfaceView.get());
    }
    scene.sourceColor=color;scene.sourceDepth=depth;scene.sourceColorTexture=color->texture.get();scene.sourceDepthTexture=depth->texture.get();
    scene.sourceColorRevision=color->textureRevision;scene.sourceDepthRevision=depth->textureRevision;
    auto& cs=scene.captureSet;
    cs->setTexture(cs->color,color->texture.get(),RenderTextureLayout::SHADER_READ,color->textureView.get());
    cs->setTexture(cs->depth,depth->texture.get(),RenderTextureLayout::SHADER_READ,depth->textureView.get());
    cs->setTexture(cs->outputColor,scene.color.get(),RenderTextureLayout::GENERAL,scene.colorView.get());
    cs->setTexture(cs->outputDepth,scene.depth.get(),RenderTextureLayout::GENERAL,scene.depthMipViews[0].get());
    if(classic) {
        scene.originalSet->setTexture(scene.originalSet->color,color->texture.get(),RenderTextureLayout::SHADER_READ,color->textureView.get());
        scene.originalSet->setTexture(scene.originalSet->output,scene.originalColor.get(),RenderTextureLayout::GENERAL,scene.originalColorView.get());
    }
    auto& ms=scene.materialSet;
    ms->setTexture(ms->originalColor,classic ? scene.originalColor.get() : scene.color.get(),RenderTextureLayout::SHADER_READ,
        classic ? scene.originalColorView.get() : scene.colorView.get());
    ms->setTexture(ms->sceneColor,scene.color.get(),RenderTextureLayout::SHADER_READ,scene.colorView.get());
    ms->setTexture(ms->sceneDepth,scene.depth.get(),RenderTextureLayout::SHADER_READ,scene.depthView.get());
    if(!existing)sceneKeys.emplace(sceneKey,index);
    return index;
    } catch(...) {
        // Earlier prepared draws keep their valid scene if atlas growth failed.
        if(!existing) {auto retired=std::move(scenes[index]);scenes[index]=SceneInputs{};rejectedScenes.insert(sceneKey);}
        throw;
    }
}
'''

def renderer(t):
    t=replace(t,'#include "rt64_water_renderer.h"','#include "rt64_water_renderer.h"\n#include "enhanced_water_settings.hpp"')
    t=replace(t,'    if (std::getenv("WR64_TEST_WATER_SHADER_FAILURE"))',
        '    dkr::runtime::water::enhanced::active_reflection=-1;\n    dkr::runtime::water::enhanced::scratch_fallbacks=0;\n    std::fprintf(stderr,"[water] shared-optics/compact-vertical scratch policy active; budget=256 MiB; quality defaults preserved\\n");\n    if (std::getenv("WR64_TEST_WATER_SHADER_FAILURE"))')
    t=replace(t,'if (std::getenv("DKRR_WATER_PROFILE") && device->getCapabilities().queryPools)',
        'if ((std::getenv("DKRR_WATER_PROFILE") || dkr::runtime::water::enhanced::timing_enabled.load()) && device->getCapabilities().queryPools)')
    start=t.index('uint32_t WaterRenderer::prepareScene(');end=t.index('\nbool WaterRenderer::captureScene(',start)
    t=t[:start]+prepare+t[end:]
    t=replace(t,'        spray->setBuffer(spray->draws,drawBuffer.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));',
        '        if(spray)spray->setBuffer(spray->draws,drawBuffer.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));')
    t=replace(t,'        if (particleBuffer.get()) spray->setBuffer(', '        if (spray && particleBuffer.get()) spray->setBuffer(')
    t=replace(t,'    rays.resetFrame();','    rays.resetFrame();\n    if(rayRetry && !--rayRetry)rayUnavailable=false;')
    t=replace(t,'scene.captured=false; scene.surfaceCleared=false;', 'scene.captured=false; scene.opticalCaptured=false; scene.surfaceCleared=false;')
    t=replace(t,'        rayUnavailable=true;', '        rayUnavailable=true;rayRetry=120;')
    t=replace(t,'    pipelines.emplace(key, std::move(result));',
        '    if(!result && hardware)return nullptr; // Transient failure must not poison the cache forever.\n    pipelines.emplace(key, std::move(result));')
    t=replace(t,'    if(scene.captured && scene.capturedCamera==camera &&',
        '    if(scene.classic) {\n        if(scene.captured)return false;scene.captured=true;\n        worker->commandList->setFramebuffer(nullptr);\n        worker->commandList->barriers(RenderBarrierStage::GRAPHICS,{\n            RenderTextureBarrier(scene.color.get(),RenderTextureLayout::SHADER_READ),\n            RenderTextureBarrier(scene.depth.get(),RenderTextureLayout::SHADER_READ)});\n        return true;\n    }\n    auto& optical=scenes[scene.captureOwner];\n    const bool same=optical.opticalCaptured && optical.opticalCamera==camera &&\n        optical.opticalView.x==view.x && optical.opticalView.y==view.y &&\n        optical.opticalView.z==view.z && optical.opticalView.w==view.w;\n    if(same && scene.captured && scene.capturedCamera==camera &&')
    # A second legacy depth mode shares the immutable pre-water optical image,
    # but still initializes its own isolated depth attachment when needed.
    t=replace(t,'    worker->commandList->barriers(RenderBarrierStage::COMPUTE, {\n        RenderTextureBarrier(scene.sourceColor->texture.get()',
        '    if(!same) {\n    optical.opticalCaptured=true;optical.opticalCamera=camera;optical.opticalView=view;\n    worker->commandList->barriers(RenderBarrierStage::COMPUTE, {\n        RenderTextureBarrier(scene.sourceColor->texture.get()')
    t=replace(t,'    endProfile(worker,profileToken);\n    return true;',
        '''    } else if(scene.selfDepth) {
        // A reused optical image can still require this mode's private depth
        // copy. Restore the source attachment even without another capture.
        worker->commandList->barriers(RenderBarrierStage::GRAPHICS,
            RenderTextureBarrier(scene.sourceDepth->texture.get(),depthReadOnly ? RenderTextureLayout::DEPTH_READ : RenderTextureLayout::DEPTH_WRITE));
    }
    endProfile(worker,profileToken);
    return true;''')
    t=replace(t,'    const auto& view=draws[drawIndex].viewBounds;\n    const auto region=water_work::captureRegion(scene.width,scene.height,view.x,view.y,view.z,view.w);\n    const uint32_t params[]={scene.width,scene.height,samples,0,region.x,region.y,region.width,region.height};',
        '''    const auto& region=draws[drawIndex].originalRegion;
    const uint32_t x=uint32_t(region.x),y=uint32_t(region.y);
    const uint32_t width=std::min(scene.originalWidth,scene.sourceColor->width-x);
    const uint32_t height=std::min(scene.originalHeight,scene.sourceColor->height-y);
    const uint32_t params[]={scene.originalWidth,scene.originalHeight,samples,0,x,y,width,height};''')
    t=replace(t,'    worker->commandList->dispatch((region.width+7)/8,(region.height+7)/8,1);\n    worker->commandList->barriers(RenderBarrierStage::GRAPHICS,{',
        '    worker->commandList->dispatch((width+7)/8,(height+7)/8,1);\n    worker->commandList->barriers(RenderBarrierStage::GRAPHICS,{')
    t=replace(t,'    if (!scene.surfaceCleared) {\n        worker->commandList->clearColor(0,RenderColor(0,0,0,0));\n        scene.surfaceCleared=true;',
        '    auto& maskOwner=scenes[scene.captureOwner];\n    if (!maskOwner.surfaceCleared) {\n        worker->commandList->clearColor(0,RenderColor(0,0,0,0));\n        maskOwner.surfaceCleared=true;')
    t=replace(t,'    const uint32_t budget=m.identity.x>=3 ? 4096 : 2048;',
        '    const uint32_t budget=m.performance.w>=2 ? 4096 : m.performance.w>=1 ? 2048 : 512;')
    t=replace(t,'distance*(b-a)*std::clamp(speed/250.0f', 'distance*(b-a)*(m.performance.w<1 ? 0.5f : 1.0f)*std::clamp(speed/250.0f')
    t=replace(t,'    const uint32_t resolution=water_work::fieldResolution(active,bool(fieldTextures[0]),fieldResolution,uint32_t(m.identity.x));',
        '    const uint32_t resolution=active && m.performance.y>=128 ? uint32_t(m.performance.y) : water_work::fieldResolution(active,bool(fieldTextures[0]),fieldResolution,uint32_t(m.identity.x));')
    t=replace(t,'    wireStarts.push_back(uint32_t(wireIndices.size()));',
        '    wireStarts.push_back(uint32_t(wireIndices.size()));\n    if(draws.back().material.effects.z>=3)return;')
    t=replace(t,'    normalVertices.clear(); normalFaces.clear();',
        '''    for(uint32_t i=0;i<drawScenes.size();i++) {
        auto& d=draws[i];const auto& s=scenes[drawScenes[i]];
        if(s.classic) {d.originalRegion.z=float(s.originalWidth);d.originalRegion.w=float(s.originalHeight);}
    }
    normalVertices.clear(); normalFaces.clear();''')
    a=t.index('    for (const auto &scene:scenes) {',t.index('uint64_t WaterRenderer::allocationBytes()'))
    b=t.index('    return bytes;',a)
    t=t[:a]+'''    for(uint32_t i=0;i<scenes.size();i++) {
        const auto& s=scenes[i];if(!s.color)continue;
        bytes+=water_policy::scratchBytes(s.width,s.height,s.surfaceSamples,s.classic,s.captureOwner!=i,
            bool(s.waterDepth),s.waterDepthWidth,s.waterDepthHeight,s.waterDepthSamples,s.originalWidth,s.originalHeight);
    }
'''+t[b:]
    # Plume separates Vulkan STORAGE usage from D3D12 UNORDERED_ACCESS.
    # Water's compute-written textures need both, not just the D3D12 bit.
    t=t.replace('RenderTextureFlag::UNORDERED_ACCESS',
        'RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS')
    return t
change('src/render/rt64_water_renderer.cpp',renderer)

def framebuffer(t):
    t=replace(t,'#include "WaterProjection.h"','#include "WaterProjection.h"\n#include "WaterBounds.h"\n#include "enhanced_water_settings.hpp"')
    t=replace(t,'                    bool hardware=!classic && wp.material.effects.w>0.5f &&',
        '                    bool hardware=!classic && wp.material.appearance.x>0 && wp.material.effects.w>0.5f &&')
    t=t.replace('waterRenderer->rayUnavailable=true;','waterRenderer->rayUnavailable=true;waterRenderer->rayRetry=120;')
    t=replace(t,'            worker->commandList->setFramebuffer(nullptr);\n            worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(scene.surface.get()',
        '            if(scene.classic) {activeWaterDraw=UINT32_MAX;switchToGraphicsPipeline();return;}\n            worker->commandList->setFramebuffer(nullptr);\n            worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(scene.surface.get()')
    t=replace(t,'                    const bool needsSurface=selfDepth || (!waterRenderer->particles.empty() &&\n                        waterRenderer->draws[drawCall.waterIndex].material.effects.x>0.5f);',
        '                    const bool needsSurface=!classic && (selfDepth || (!waterRenderer->particles.empty() &&\n                        waterRenderer->draws[drawCall.waterIndex].material.effects.x>0.5f));')
    t=replace(t,'                        const auto waterScene=waterRenderer->prepareScene(p.worker,p.fbStorage->colorTarget,p.fbStorage->depthTarget,\n                            uint64_t(framebufferCount),classic,selfDepth);',
        '''                        const auto originalRegion=classic ? waterOriginalRegion(drawData,call.meshDesc.faceIndicesStart,
                            call.callDesc.triangleCount*3,wp,p.interpolationWeight) : water_work::Region{};
                        wp.originalRegion={float(originalRegion.x),float(originalRegion.y),0,0};
                        const auto waterScene=waterRenderer->prepareScene(p.worker,p.fbStorage->colorTarget,p.fbStorage->depthTarget,
                            uint64_t(framebufferCount),classic,selfDepth,originalRegion);''')
    t=replace(t,'                        if(waterScene!=water_policy::invalidScene) {',
        '''                        if(waterScene!=water_policy::invalidScene) {
                        if(!classic)dkr::runtime::water::enhanced::active_reflection.store(hardware ? 2 : wp.material.identity.x>=2 ? 1 : 0);''')
    return t
change('src/render/rt64_framebuffer_renderer.cpp',framebuffer)

def original(t):
    t=replace(t,'RWTexture2D<float4> originalColor',
        '[[vk::image_format("rgba16f")]] RWTexture2D<float4> originalColor')
    t=replace(t,'    pixel+=params.origin;\n    if (pixel.x >= params.width || pixel.y >= params.height) return;',
        '    const uint2 sourcePixel=pixel+params.origin;')
    return t.replace('sourceColor.Load(pixel,i)','sourceColor.Load(sourcePixel,i)').replace('sourceColor.Load(uint3(pixel,0))','sourceColor.Load(uint3(sourcePixel,0))')
change('src/shaders/WaterOriginalCS.hlsl',original)

def capture_format(t):
    return replace(t,'RWTexture2D<float4> capturedColor',
        '[[vk::image_format("rgba16f")]] RWTexture2D<float4> capturedColor')
change('src/shaders/WaterCaptureCS.hlsl',capture_format)
def field_format(t):
    return replace(t,'RWTexture2D<float4> nextField',
        '[[vk::image_format("rgba16f")]] RWTexture2D<float4> nextField')
change('src/shaders/WaterFieldCS.hlsl',field_format)

def pixel(t):
    t=replace(t,'    uint depthWidth,depthHeight,depthLevels; sceneDepth.GetDimensions(0,depthWidth,depthHeight,depthLevels);',
        '    if(p.material.appearance.x<=0)return 0;\n    uint depthWidth,depthHeight,depthLevels; sceneDepth.GetDimensions(0,depthWidth,depthHeight,depthLevels);')
    t=replace(t,'step<(p.material.identity.x>=3 ? 36u : 24u)', 'step<uint(p.material.performance.x>0 ? p.material.performance.x : (p.material.identity.x>=3 ? 36 : 24))')
    t=replace(t,'        uint2 at=uint2(position.xy);','        uint2 at=uint2(position.xy-p.originalRegion.xy);')
    t=replace(t,'        uint w,h; sceneColor.GetDimensions(w,h);\n        at+=uint2((sampleIndex&1)*w,(sampleIndex>>1)*h);',
        '        at+=uint2((sampleIndex&1)*uint(p.originalRegion.z),(sampleIndex>>1)*uint(p.originalRegion.w));')
    t=replace(t,'float streak=smoothstep(0.67,0.91,foamNoise(flow))*filter*m.animation.w;',
        'float streak=m.appearance.w<=0 ? 0 : smoothstep(0.67,0.91,foamNoise(flow))*filter*m.animation.w*m.appearance.w;')
    t=replace(t,'    float3 wake=interaction(worldPosition.xz,p);','    float3 wake=interaction(worldPosition.xz,p)*m.appearance.z;')
    t=replace(t,'position.xy + ripples * 13 * saturate(thickness/80)', 'position.xy + ripples * 13 * m.appearance.y * saturate(thickness/80)')
    t=replace(t,'    color += reflected*fresnel;', '    color += reflected*fresnel*m.appearance.x;')
    t=replace(t,'    if(reflection.a<0.75) {','    if(reflection.a<0.75 && m.appearance.x>0) {')
    t=replace(t,'        uint2 originalPixel=uint2(position.xy);','        uint2 originalPixel=uint2(position.xy-p.originalRegion.xy);')
    t=replace(t,'        uint originalWidth,originalHeight;\n        sceneColor.GetDimensions(originalWidth,originalHeight);\n        originalPixel+=uint2((sampleIndex&1)*originalWidth,(sampleIndex>>1)*originalHeight);',
        '        originalPixel+=uint2((sampleIndex&1)*uint(p.originalRegion.z),(sampleIndex>>1)*uint(p.originalRegion.w));')
    return t
change('src/shaders/WaterPS.hlsl',pixel)
output.write_text(''.join(changes),newline='\n')
print(f'Authored {output}; no dependency/stage files changed.')
