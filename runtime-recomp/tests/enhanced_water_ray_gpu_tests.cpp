#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "render/WaterRayScene.h"
#include "render/WaterProjection.h"
#include "render/rt64_water_renderer.h"
#include "render/rt64_render_target.h"
#include "shaders/WaterRayProbeCS.hlsl.spirv.h"
#include "shaders/WaterFieldCS.hlsl.spirv.h"
#include "shaders/WaterFieldReferenceCS.hlsl.spirv.h"
#include "shaders/WaterFieldCompareCS.hlsl.spirv.h"
#include "shaders/WaterProjectionProbeCS.hlsl.spirv.h"
#ifdef _WIN32
#include "shaders/WaterRayProbeCS.hlsl.dxil.h"
#include "shaders/WaterFieldCS.hlsl.dxil.h"
#include "shaders/WaterFieldReferenceCS.hlsl.dxil.h"
#include "shaders/WaterFieldCompareCS.hlsl.dxil.h"
#include "shaders/WaterProjectionProbeCS.hlsl.dxil.h"
#endif
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <stdexcept>

namespace plume {
#ifdef _WIN32
std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#if PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow window);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
}
namespace {
template<class T> std::unique_ptr<T> required(std::unique_ptr<T> value) {
    if(!value)throw std::runtime_error("Headless water-ray resource creation failed");return value;
}
struct OutputSet : plume::RenderDescriptorSetBase {
    uint32_t output;
    explicit OutputSet(plume::RenderDevice* device) {
        builder.begin();output=builder.addReadWriteStructuredBuffer(1);builder.end();create(device);
        if(!get())throw std::runtime_error("Probe descriptor allocation failed");
    }
};
struct WakeSet : plume::RenderDescriptorSetBase {
    uint32_t draws,previous,next;
    WakeSet() {
        builder.begin();
        draws=builder.addStructuredBuffer(1);
        previous=builder.addTexture(2);
        next=builder.addReadWriteTexture(3);
        builder.end();
    }
};
struct FieldCompareSet : plume::RenderDescriptorSetBase {
    uint32_t reference,optimized,initial,errors;
    explicit FieldCompareSet(plume::RenderDevice* device) {
        builder.begin();reference=builder.addTexture(1);optimized=builder.addTexture(2);
        initial=builder.addReadWriteTexture(3);errors=builder.addReadWriteStructuredBuffer(4);
        builder.end();create(device);
        if(!get())throw std::runtime_error("No field comparison descriptors");
    }
};
void fieldProbe(RT64::RenderWorker& worker,plume::RenderShaderFormat format) {
    using namespace RT64;
    WakeSet baseline,optimized;baseline.create(worker.device);optimized.create(worker.device);
    RenderPipelineLayoutBuilder builder;builder.begin();
    builder.addPushConstant(0,0,32,RenderShaderStageFlag::COMPUTE);builder.addDescriptorSet(baseline);builder.end();
    auto layout=required(builder.create(worker.device));
    FieldCompareSet compare(worker.device);
    builder.begin();builder.addPushConstant(0,0,16,RenderShaderStageFlag::COMPUTE);builder.addDescriptorSet(compare);builder.end();
    auto compareLayout=required(builder.create(worker.device));
    std::unique_ptr<RenderShader> referenceShader,optimizedShader,compareShader;
#ifdef _WIN32
    if(format==RenderShaderFormat::DXIL) {
        referenceShader=required(worker.device->createShader(WaterFieldReferenceCSBlobDXIL,sizeof(WaterFieldReferenceCSBlobDXIL),"CSMain",format));
        optimizedShader=required(worker.device->createShader(WaterFieldCSBlobDXIL,sizeof(WaterFieldCSBlobDXIL),"CSMain",format));
        compareShader=required(worker.device->createShader(WaterFieldCompareCSBlobDXIL,sizeof(WaterFieldCompareCSBlobDXIL),"CSMain",format));
    } else
#endif
    {
        referenceShader=required(worker.device->createShader(WaterFieldReferenceCSBlobSPIRV,sizeof(WaterFieldReferenceCSBlobSPIRV),"CSMain",format));
        optimizedShader=required(worker.device->createShader(WaterFieldCSBlobSPIRV,sizeof(WaterFieldCSBlobSPIRV),"CSMain",format));
        compareShader=required(worker.device->createShader(WaterFieldCompareCSBlobSPIRV,sizeof(WaterFieldCompareCSBlobSPIRV),"CSMain",format));
    }
    auto referencePipeline=required(worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(),referenceShader.get(),8,8,1)));
    auto optimizedPipeline=required(worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(),optimizedShader.get(),8,8,1)));
    auto comparePipeline=required(worker.device->createComputePipeline(RenderComputePipelineDesc(compareLayout.get(),compareShader.get(),8,8,1)));
    auto draws=required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(sizeof(interop::WaterDrawParams),RenderBufferFlag::STORAGE)));
    auto upload=required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(sizeof(interop::WaterDrawParams))));
    baseline.setBuffer(baseline.draws,draws.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    optimized.setBuffer(optimized.draws,draws.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    unsigned cases=0;float maximumError=0;
    auto timings=worker.device->getCapabilities().queryPools ? worker.device->createQueryPool(4) : nullptr;
    std::array<double,2> referenceMs{},optimizedMs{};std::array<unsigned,2> timedCases{};
    for(unsigned resolution:{1u,256u,512u}) {
        const unsigned width=resolution*8,height=resolution,gx=(width+7)/8,gy=(height+7)/8;
        std::array<std::unique_ptr<RenderTexture>,3> textures;
        for(auto& texture:textures)texture=required(worker.device->createTexture(RenderTextureDesc::Texture2D(width,height,1,RenderFormat::R16G16B16A16_FLOAT,RenderTextureFlag::UNORDERED_ACCESS)));
        baseline.setTexture(baseline.previous,textures[0].get(),RenderTextureLayout::SHADER_READ);
        optimized.setTexture(optimized.previous,textures[0].get(),RenderTextureLayout::SHADER_READ);
        baseline.setTexture(baseline.next,textures[1].get(),RenderTextureLayout::GENERAL);
        optimized.setTexture(optimized.next,textures[2].get(),RenderTextureLayout::GENERAL);
        compare.setTexture(compare.reference,textures[1].get(),RenderTextureLayout::SHADER_READ);
        compare.setTexture(compare.optimized,textures[2].get(),RenderTextureLayout::SHADER_READ);
        compare.setTexture(compare.initial,textures[0].get(),RenderTextureLayout::GENERAL);
        const uint64_t errorBytes=uint64_t(gx)*gy*16;
        auto errors=required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(errorBytes,RenderBufferFlag::STORAGE|RenderBufferFlag::UNORDERED_ACCESS)));
        auto readback=required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(errorBytes)));
        compare.setBuffer(compare.errors,errors.get(),RenderBufferStructuredView(16));
        for(unsigned scenario=0;scenario<8;scenario++) {
            interop::WaterDrawParams draw{};
            draw.material.animation={2,2.1f,1,1};draw.material.surface={0,0.7f,-0.2f,0};
            for(unsigned i=0;i<8;i++) {
                draw.material.craftPrevious[i]={float(i*50+4),12,float(i*15+9),scenario==3 || scenario==7 ? 0.0f : 1.0f};
                draw.material.craftCurrent[i]={float(i*50+24),0,float(i*15+12),scenario==4 && i>1 ? -1.0f : 1.0f};
                const float shift=scenario==2 ? 3.25f : 3.0f,cell=4096.0f/resolution;
                draw.fieldOrigins[i]={-1500,-1500,-1500-shift*cell,-1500+cell};
                if(scenario==5) {
                    draw.material.shoreCounts[i/4][i%4]=1;
                    draw.material.shoreSegments[i*8]={-1500,100,1500,100};
                }
            }
            const RenderRange none(0,0),written(0,sizeof(draw));auto* mapped=upload->map(0,&none);
            if(!mapped)throw std::runtime_error("No field upload");std::memcpy(mapped,&draw,sizeof(draw));upload->unmap(0,&written);
            interop::WaterFieldPushConstants constants{0,resolution,scenario==0 ? 1u : 0u,0,{1.0f/120,2.095f,2.1033333f,4096}};
            if(scenario==6) {constants.reset=1;constants.simulation={0,2,2,4096};}
            if(scenario==7)constants.reset=1;
            const uint32_t initialize[]={width,height,0,0},comparison[]={width,height,1,0};
            worker.commandList->begin();
            worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(draws.get(),RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(draws->at(0),upload->at(0),sizeof(draw));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(draws.get(),RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[0].get(),RenderTextureLayout::GENERAL));
            for(unsigned i=1;i<3;i++)worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[i].get(),RenderTextureLayout::SHADER_READ));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(errors.get(),RenderBufferAccess::WRITE));
            worker.commandList->setComputePipelineLayout(compareLayout.get());worker.commandList->setPipeline(comparePipeline.get());
            worker.commandList->setComputeDescriptorSet(compare.get(),0);worker.commandList->setComputePushConstants(0,initialize);worker.commandList->dispatch(gx,gy,1);
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[0].get(),RenderTextureLayout::SHADER_READ));
            for(unsigned i=1;i<3;i++)worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[i].get(),RenderTextureLayout::GENERAL));
            worker.commandList->setComputePipelineLayout(layout.get());worker.commandList->setComputePushConstants(0,&constants);
            if(timings) {worker.commandList->resetQueryPool(timings.get(),0,4);worker.commandList->writeTimestamp(timings.get(),0);}
            worker.commandList->setPipeline(referencePipeline.get());worker.commandList->setComputeDescriptorSet(baseline.get(),0);worker.commandList->dispatch(gx,gy,1);
            if(timings) {worker.commandList->writeTimestamp(timings.get(),1);worker.commandList->writeTimestamp(timings.get(),2);}
            worker.commandList->setPipeline(optimizedPipeline.get());worker.commandList->setComputeDescriptorSet(optimized.get(),0);worker.commandList->dispatch(gx,gy,1);
            if(timings)worker.commandList->writeTimestamp(timings.get(),3);
            for(unsigned i=1;i<3;i++)worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[i].get(),RenderTextureLayout::SHADER_READ));
            // The initial UAV remains bound by the comparison layout, though
            // phase 1 never accesses it. Restore its declared layout for Vulkan.
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderTextureBarrier(textures[0].get(),RenderTextureLayout::GENERAL));
            worker.commandList->setComputePipelineLayout(compareLayout.get());worker.commandList->setPipeline(comparePipeline.get());
            worker.commandList->setComputeDescriptorSet(compare.get(),0);worker.commandList->setComputePushConstants(0,comparison);worker.commandList->dispatch(gx,gy,1);
            worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(errors.get(),RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(readback.get(),RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(readback->at(0),errors->at(0),errorBytes);
            worker.commandList->end();worker.execute();worker.wait();
            if(timings && resolution>=256) {
                timings->queryResults();const auto* stamps=timings->getResults();const auto index=resolution==512 ? 1 : 0;
                referenceMs[index]+=double(stamps[1]-stamps[0])/1000000;optimizedMs[index]+=double(stamps[3]-stamps[2])/1000000;timedCases[index]++;
            }
            const RenderRange all(0,errorBytes);mapped=readback->map(0,&all);if(!mapped)throw std::runtime_error("No wake readback");
            const auto* values=static_cast<const float*>(mapped);float error=0;
            for(size_t i=0;i<errorBytes/sizeof(float);i++)error=std::max(error,values[i]);
            readback->unmap(0,&none);maximumError=std::max(error,maximumError);cases++;
            if(!std::isfinite(error) || error>0.002f) {
                std::fprintf(stderr,"Wake resolution=%u scenario=%u max-error=%g\n",resolution,scenario,error);
                throw std::runtime_error("Reference wake shader mismatch");
            }
        }
    }
    std::printf("PASS: %u real GPU reference wake comparisons (1/256/512, reset, shifted, fractional, landing, inactive, shore); max-error=%g.\n",cases,maximumError);
    for(unsigned i=0;i<2;i++)if(timedCases[i])std::printf("Synthetic wake-only mean: resolution=%u baseline=%.3fms optimized=%.3fms (not gameplay FPS).\n",i ? 512 : 256,referenceMs[i]/timedCases[i],optimizedMs[i]/timedCases[i]);
}
struct ProjectionSet : plume::RenderDescriptorSetBase {
    uint32_t draws,output,input;
    explicit ProjectionSet(plume::RenderDevice* device) {
        builder.begin();draws=builder.addStructuredBuffer(1);output=builder.addReadWriteStructuredBuffer(2);
        input=builder.addStructuredBuffer(3);builder.end();create(device);
        if(!get())throw std::runtime_error("Projection descriptor allocation failed");
    }
};
struct ProjectionCase {
    interop::WaterDrawParams draw;
    interop::float4 world,expectedPixel,rawWorld;
};
// Draw parameters alone nearly fill D3D's 2048-byte structured stride, so the
// probe inputs are uploaded to a parallel buffer with the same index.
struct ProjectionInput {
    interop::float4 world,expectedPixel,rawWorld;
};
static_assert(sizeof(interop::WaterDrawParams)<=2048 && sizeof(ProjectionInput)==48,"GPU projection probe ABI and D3D stride");
void projectionProbe(RT64::RenderWorker& worker,plume::RenderShaderFormat format) {
    using namespace RT64;
    std::vector<ProjectionCase> cases;
    // Original, widescreen, ultra-wide, portrait and split-view dimensions.
    for(const auto size:std::array<std::array<unsigned,2>,6>{{{640,480},{2560,1440},{3440,1440},{5120,1440},{1080,2400},{1920,1080}}})
    for(unsigned views:{1u,2u,4u})for(unsigned view=0;view<views;view++)for(float fov:{45.0f,90.0f,120.0f})for(float yaw:{-1.1f,0.0f,1.1f})
    for(float nx:{-0.94f,0.0f,0.94f})for(float ny:{-0.8f,0.8f}) {
        ProjectionCase c{};auto& p=c.draw;
        const float sx=views==4 ? 80.0f : 160.0f,sy=views>1 ? 60.0f : 120.0f;
        const float tx=views==4 ? 80.0f+160.0f*(view%2) : 160.0f;
        const float ty=views>1 ? 60.0f+120.0f*(views==4 ? view/2 : view) : 120.0f;
        p.screen={320,240,float(size[0]),float(size[1])};
        p.viewportScale={sx,sy,0.5f,float(size[0])};
        p.viewportTranslate={tx,ty,0.5f,float(size[1])};
        p.screenTransform={0.997f,0.995f,0.0003f,-0.0004f}; // actual offset/non-unit mapping
        const float actualAspect=(float(size[0])*(views==4 ? 0.5f : 1.0f))/(float(size[1])*(views>1 ? 0.5f : 1.0f));
        const float sourceAspect=sx/sy;
        const float f=1.0f/std::tan(fov*3.14159265359f/360.0f),cameraNear=4,cameraFar=12000;
        const auto projection=hlslpp::float4x4(f/sourceAspect,0,0,0,0,f,0,0,0,0,cameraFar/(cameraFar-cameraNear),1,0,0,-cameraFar*cameraNear/(cameraFar-cameraNear),0);
        // Non-origin/interpolated camera ensures the order is not commutative.
        const float cosine=std::cos(yaw),sine=std::sin(yaw);
        const auto viewMatrix=hlslpp::float4x4(cosine,0,-sine,0,0,1,0,0,sine,0,cosine,0,-13,-7,41,1);
        const auto camera=hlslpp::mul(viewMatrix,projection);
        auto raster=hlslpp::float4x4::identity();raster[0][0]=sourceAspect/actualAspect;
        p.material.cameraPosition.w=1;
        setWaterCameraProjection(p,camera,raster);
        // Generate edge samples independently, then apply the same two stages
        // as RSPProcessCS rather than the helper's composed projection.
        auto world=hlslpp::mul(hlslpp::float4(nx,ny,0.95f,1),hlslpp::inverse(hlslpp::mul(camera,raster)));
        world=world/hlslpp::float4(float(world.w));c.world={float(world.x),float(world.y),float(world.z),1};
        const auto raw=hlslpp::mul(world,camera),clip=hlslpp::mul(raw,raster);
        c.rawWorld=raw;
        const float screenX=float(clip.x/clip.w)*sx+tx,screenY=-float(clip.y/clip.w)*sy+ty;
        const float mappedX=(screenX/320*2-1)*p.screenTransform.x+p.screenTransform.z;
        const float mappedY=(1-screenY/240*2)*p.screenTransform.y+p.screenTransform.w;
        c.expectedPixel={(mappedX*0.5f+0.5f)*size[0],(0.5f-mappedY*0.5f)*size[1],float(clip.z/clip.w)*0.5f+0.5f,1};
        p.viewBounds={(tx-sx)/320*size[0],(ty-sy)/240*size[1],(tx+sx)/320*size[0],(ty+sy)/240*size[1]};
        cases.push_back(c);
    }
    ProjectionSet set(worker.device);
    RenderPipelineLayoutBuilder builder;builder.begin();builder.addDescriptorSet(set);builder.end();
    auto layout=required(builder.create(worker.device));std::unique_ptr<RenderShader> shader;
#ifdef _WIN32
    if(format==RenderShaderFormat::DXIL)shader=required(worker.device->createShader(WaterProjectionProbeCSBlobDXIL,sizeof(WaterProjectionProbeCSBlobDXIL),"CSMain",format));
    else
#endif
    shader=required(worker.device->createShader(WaterProjectionProbeCSBlobSPIRV,sizeof(WaterProjectionProbeCSBlobSPIRV),"CSMain",format));
    auto pipeline=required(worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(),shader.get(),1,1,1)));
    std::vector<interop::WaterDrawParams> draws;std::vector<ProjectionInput> inputs;
    for(const auto& c:cases) {draws.push_back(c.draw);inputs.push_back({c.world,c.expectedPixel,c.rawWorld});}
    const uint64_t drawBytes=draws.size()*sizeof(interop::WaterDrawParams),inputBytes=inputs.size()*sizeof(ProjectionInput);
    const uint64_t outputBytes=cases.size()*16;
    auto drawBuffer=required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(drawBytes,RenderBufferFlag::STORAGE)));
    auto input=required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(inputBytes,RenderBufferFlag::STORAGE)));
    auto upload=required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(drawBytes+inputBytes)));
    auto output=required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(outputBytes,RenderBufferFlag::STORAGE|RenderBufferFlag::UNORDERED_ACCESS)));
    auto readback=required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(outputBytes)));
    const RenderRange none(0,0),written(0,drawBytes+inputBytes);
    auto mapped=upload->map(0,&none);if(!mapped)throw std::runtime_error("No projection upload");
    std::memcpy(mapped,draws.data(),drawBytes);
    std::memcpy(static_cast<std::uint8_t*>(mapped)+drawBytes,inputs.data(),inputBytes);upload->unmap(0,&written);
    set.setBuffer(set.draws,drawBuffer.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    set.setBuffer(set.input,input.get(),RenderBufferStructuredView(sizeof(ProjectionInput)));
    set.setBuffer(set.output,output.get(),RenderBufferStructuredView(16));
    worker.commandList->begin();
    worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(drawBuffer.get(),RenderBufferAccess::WRITE));
    worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(input.get(),RenderBufferAccess::WRITE));
    worker.commandList->copyBufferRegion(drawBuffer->at(0),upload->at(0),drawBytes);
    worker.commandList->copyBufferRegion(input->at(0),upload->at(drawBytes),inputBytes);
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(drawBuffer.get(),RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(input.get(),RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(output.get(),RenderBufferAccess::WRITE));
    worker.commandList->setComputePipelineLayout(layout.get());worker.commandList->setPipeline(pipeline.get());
    worker.commandList->setComputeDescriptorSet(set.get(),0);worker.commandList->dispatch(uint32_t(cases.size()),1,1);
    worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(output.get(),RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(readback.get(),RenderBufferAccess::WRITE));
    worker.commandList->copyBufferRegion(readback->at(0),output->at(0),outputBytes);
    worker.commandList->end();worker.execute();worker.wait();
    const RenderRange all(0,outputBytes);
    mapped=readback->map(0,&all);if(!mapped)throw std::runtime_error("No projection readback");
    std::vector<std::array<float,4>> errors(cases.size());std::memcpy(errors.data(),mapped,outputBytes);readback->unmap(0,&none);
    for(unsigned i=0;i<errors.size();i++) {
        const auto& e=errors[i];
        if(!std::isfinite(e[0]) || !std::isfinite(e[1]) || !std::isfinite(e[2]) || e[0]>0.05f || e[1]>0.2f || e[2]>0.2f || e[3]!=0) {
            std::fprintf(stderr,"Projection case %u: pixel=%g world=%g geometry=%g outside=%g\n",i,e[0],e[1],e[2],e[3]);
            throw std::runtime_error("GPU water projection mismatch");
        }
    }
    std::printf("PASS: %zu real GPU projection cases, wide edges, split views, offsets, FOV and undistorted geometry.\n",cases.size());
}
void resourceProbe(RT64::RenderWorker& worker,plume::RenderShaderFormat format) {
    using namespace RT64;
    RenderTarget color(0,Framebuffer::Type::Color,RenderMultisampling{},false);
    RenderTarget depth(0,Framebuffer::Type::Depth,RenderMultisampling{},false);
    color.resize(&worker,2560,1440);depth.resize(&worker,2560,1440);
    WaterRenderer water(worker.device,format,false);
    for(unsigned pass=0;pass<12;pass++) {
        water.reset();
        if(pass%4==0) {color.setupColor(&worker,2560,1440);depth.setupDepth(&worker,2560,1440);}
        const auto modern=water.prepareScene(&worker,&color,&depth,1,false,true);
        if(modern==water_policy::invalidScene)throw std::runtime_error("1440p modern water rejected");
        auto* framebuffer=water.scenes[modern].surfaceFramebuffer.get();
        if(water.prepareScene(&worker,&color,&depth,1,false,true)!=modern || water.scenes[modern].surfaceFramebuffer.get()!=framebuffer)
            throw std::runtime_error("Scratch key/framebuffer reuse failed");
        if(water.scenes[modern].sourceColorRevision!=color.textureRevision || water.scenes[modern].sourceDepthRevision!=depth.textureRevision)
            throw std::runtime_error("Recreated source attachment revision was not adopted");
        if(pass%3!=2) {
            const auto classic=water.prepareScene(&worker,&color,&depth,1,true,false);
            if(classic==water_policy::invalidScene)throw std::runtime_error("1440p original-colour overlay rejected");
            if(water.prepareScene(&worker,&color,&depth,2,false,true)==water_policy::invalidScene)
                throw std::runtime_error("Third 1440p scratch rejected within payload budget");
            // Deliberately exceed the budget without allocating a fourth
            // scratch. Existing accepted scenes must remain available.
            if(water.prepareScene(&worker,&color,&depth,3,false,true)!=water_policy::invalidScene ||
                water.prepareScene(&worker,&color,&depth,3,false,true)!=water_policy::invalidScene || water.sceneKeys.size()!=3)
                throw std::runtime_error("Bounded target-local fallback failed");
        }
        if(water.allocationBytes()>water_policy::sceneBudget)throw std::runtime_error("Scratch pool exceeded bounded payload");
    }
    water.reset();water.reset();
    if(!water.scenes.empty())throw std::runtime_error("Unused scene pool retained");
    if(water.prepareScene(&worker,&color,&depth,1,false,true)==water_policy::invalidScene)
        throw std::runtime_error("Water failed to recover after budget fallback");
    std::puts("PASS: real 1440p water allocations, mixed modes, target-local budget rejection, pool reuse/reclaim and recovery.");
    interop::WaterDrawParams fieldDraw{};fieldDraw.material.identity={3,0,0,1};
    fieldDraw.material.animation={0,0.0333333f,1,1};fieldDraw.camera.w=0.0166667f;
    fieldDraw.material.craftCurrent[0].w=1;
    water.draws.push_back(fieldDraw);water.prepareField(&worker);
    const auto* retained=water.fieldTextures[0].get();
    if(water.fieldResolution!=512 || !retained)throw std::runtime_error("High wake atlas missing");
    for(unsigned transition=0;transition<8;transition++) {
        water.reset();fieldDraw.material.craftCurrent[0].w=transition%2 ? 1.0f : 0.0f;
        fieldDraw.camera.w+=1.0f/60;water.draws.push_back(fieldDraw);water.prepareField(&worker);
        if(water.fieldTextures[0].get()!=retained || water.fieldResolution!=512)
            throw std::runtime_error("Wet/dry transition reallocated wake textures");
        if(water.fieldSteps.empty() || !water.fieldSteps.front().first.reset)
            throw std::runtime_error("Wet/dry transition failed to reset history");
    }
    std::puts("PASS: real high-quality wake resources retained across eight wet/dry transitions with history resets.");
}
}
int main(int argc,char** argv) {
    using namespace RT64;
    try {
        std::unique_ptr<RenderInterface> api;
        RenderShaderFormat format=RenderShaderFormat::SPIRV;
#ifdef _WIN32
        if(argc<2 || std::strcmp(argv[1],"vulkan")!=0) {
            api=plume::CreateD3D12Interface();format=RenderShaderFormat::DXIL;
        }
#endif
        if(!api) {
#if PLUME_SDL_VULKAN_ENABLED
            api=plume::CreateVulkanInterface(nullptr);
#else
            api=plume::CreateVulkanInterface();
#endif
        }
        if(!api)throw std::runtime_error("No headless graphics interface");
        auto device=required(api->createDevice());
        RenderWorker worker(device.get(),"Headless water probes",RenderCommandListType::DIRECT);
        if(!worker.commandQueue || !worker.commandList || !worker.commandFence)throw std::runtime_error("No headless command resources");
        projectionProbe(worker,format);
        resourceProbe(worker,format);
        fieldProbe(worker,format);
        // Validate the actual wake shader/pipeline with the portable constant
        // range even when this device cannot execute hardware ray queries.
        // Production's shared CPU/shader header separately asserts this ABI.
        // Do not import its SIMD/GUI dependencies into the headless probe.
        constexpr uint32_t fieldConstantBytes=32;
        WakeSet fieldSet;
        RenderPipelineLayoutBuilder fieldBuilder;fieldBuilder.begin();
        fieldBuilder.addPushConstant(0,0,fieldConstantBytes,RenderShaderStageFlag::COMPUTE);
        fieldBuilder.addDescriptorSet(fieldSet);fieldBuilder.end();
        auto fieldLayout=required(fieldBuilder.create(device.get()));
        std::unique_ptr<RenderShader> fieldShader;
#ifdef _WIN32
        if(format==RenderShaderFormat::DXIL)fieldShader=required(device->createShader(WaterFieldCSBlobDXIL,sizeof(WaterFieldCSBlobDXIL),"CSMain",format));
        else
#endif
        fieldShader=required(device->createShader(WaterFieldCSBlobSPIRV,sizeof(WaterFieldCSBlobSPIRV),"CSMain",format));
        auto fieldPipeline=required(device->createComputePipeline(RenderComputePipelineDesc(fieldLayout.get(),fieldShader.get(),8,8,1)));
        std::puts("PASS: eight-racer wake shader/pipeline accepts 32-byte constants.");
        std::printf("Water ray-query capability: %s\n",device->getCapabilities().rayQuery ? "enabled" : "unavailable");
        if(!device->getCapabilities().rayQuery)return 77;
        WaterRayScene rays;
        OutputSet output(device.get());WaterRaySet rayLayout;
        RenderPipelineLayoutBuilder builder;builder.begin();builder.addDescriptorSet(output);builder.addDescriptorSet(rayLayout);builder.end();
        auto layout=required(builder.create(device.get()));
        std::unique_ptr<RenderShader> shader;
#ifdef _WIN32
        if(format==RenderShaderFormat::DXIL)shader=required(device->createShader(WaterRayProbeCSBlobDXIL,sizeof(WaterRayProbeCSBlobDXIL),"CSMain",format));
        else
#endif
        shader=required(device->createShader(WaterRayProbeCSBlobSPIRV,sizeof(WaterRayProbeCSBlobSPIRV),"CSMain",format));
        auto pipeline=required(device->createComputePipeline(RenderComputePipelineDesc(layout.get(),shader.get(),3,1,1)));
        constexpr uint32_t bytes=3*4*sizeof(uint32_t);
        auto gpu=required(device->createBuffer(RenderBufferDesc::DefaultBuffer(bytes,RenderBufferFlag::STORAGE|RenderBufferFlag::UNORDERED_ACCESS)));
        auto cpu=required(device->createBuffer(RenderBufferDesc::ReadbackBuffer(bytes)));
        output.setBuffer(output.output,gpu.get(),RenderBufferStructuredView(16));
        std::shared_ptr<dkr::runtime::water::enhanced::RayGeometry> geometry;
        for(unsigned pass=0;pass<4;pass++) {
            // Repeated immutable reuse, followed by a complete scene change.
            const float z=pass<2 ? 10.0f : 12.0f;
            if(pass%2==0) {
                geometry=std::make_shared<dkr::runtime::water::enhanced::RayGeometry>();
                geometry->vertices={{{-1,-1,z,1},{1,0,0,1}},{{1,-1,z,1},{0,1,0,1}},{{0,1,z,1},{0,0,1,1}}};
            }
            rays.resetFrame();if(!rays.prepare(&worker,geometry))throw std::runtime_error("Synthetic water AS preparation failed");
            worker.commandList->begin();rays.record(&worker);
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,RenderBufferBarrier(gpu.get(),RenderBufferAccess::WRITE));
            worker.commandList->setComputePipelineLayout(layout.get());worker.commandList->setPipeline(pipeline.get());
            worker.commandList->setComputeDescriptorSet(output.get(),0);worker.commandList->setComputeDescriptorSet(rays.set->get(),1);
            worker.commandList->dispatch(1,1,1);
            worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(gpu.get(),RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(cpu.get(),RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(cpu->at(0),gpu->at(0),bytes);
            worker.commandList->end();worker.execute();worker.wait();
            std::array<uint32_t,12> values{};const RenderRange all(0,bytes),none(0,0);
            auto* mapped=cpu->map(0,&all);if(!mapped)throw std::runtime_error("No probe readback");
            std::memcpy(values.data(),mapped,bytes);cpu->unmap(0,&none);
            if(values[0]!=1 || values[1]!=uint32_t(z*1000) || values[2]!=0 || values[4]!=0 || values[8]!=0)
                throw std::runtime_error("Hardware ray intersection/readback mismatch");
        }
        std::puts("PASS: real GPU ray hit/miss, static reuse and scene replacement; no gameplay or FPS claim.");return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
