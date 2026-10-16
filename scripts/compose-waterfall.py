"""Compose the authored waterfall layer in memory. Never edit RT64 or its mirror."""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root=Path(__file__).resolve().parents[1]
stage=Path(sys.argv[1]).resolve()
output=root/'patches/enhanced-water/dkr-waterfall.patch'
layers=[]
if 'lavaSurface' in (stage/'src/shaders/WaterPS.hlsl').read_text():
    layers.append(root/'patches/enhanced-water/dkr-lava.patch')
if 'waterfallPipeline' in (stage/'src/render/rt64_water_renderer.h').read_text():
    layers.append(output)
changes=[]
def replace(t,a,b):
    if t.count(a)!=1: raise SystemExit(f'Non-unique waterfall context: {a[:120]}')
    return t.replace(a,b,1)
def change(path,fn,original=False):
    before=(root/'extern/rt64'/path).read_text() if original else read_before(stage,path,layers)
    after=fn(before)
    if before==after: raise SystemExit(f'No waterfall change: {path}')
    changes.append(f'diff --git a/{path} b/{path}\n')
    changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=f'a/{path}',tofile=f'b/{path}',n=3))

def raster(t):
    t=replace(t,'    bool isFrontFace, out float4 resultColor, out float4 resultAlpha)',
        '''    bool isFrontFace, out float4 resultColor, out float4 resultAlpha
#ifdef DKR_WATERFALL
    , float4 waterfallFlow
#endif
    )''')
    t=replace(t,'    colorCombiner.run(ccInputs, combinerColor, alphaCompareValue);', '''    colorCombiner.run(ccInputs, combinerColor, alphaCompareValue);
#ifdef DKR_WATERFALL
    WaterDrawParams waterfall=waterfallDraws[gConstants.padding];
    const bool anchored=waterfallFlow.w>0.5;
    float2 flow=anchored ? waterfallFlow.xy : vertexUV/max(waterfall.material.optics.yz,1);
    float2 flowDx=ddx(flow),flowDy=ddy(flow);
    float footprint=max(length(flowDx),length(flowDy));
    float mappingArea=abs(flowDx.x*flowDy.y-flowDx.y*flowDy.x);
    float direction=anchored ? waterfallFlow.z : mappingArea>0.00000001 ? 1 : 0;
    // Replace RGB, not the authored coverage/cutout/alpha-comparison path.
    // Apply before the retail blender so fog and destination opacity remain correct.
    combinerColor.rgb=waterfallComposite(combinerColor.rgb,
        flow,anchored ? waterfall.camera.w : 0,direction,vertexColor.rgb,footprint,
        waterfall.material.appearance.w);
#endif''')
    t=replace(t,'    , in float2 vertexUV : TEXCOORD','''    , in float2 vertexUV : TEXCOORD
#ifdef DKR_WATERFALL
    , in float4 waterfallFlow : TEXCOORD1
#endif''')
    t=replace(t,'isFrontFace, resultColor, resultAlpha)) {','''isFrontFace, resultColor, resultAlpha
#ifdef DKR_WATERFALL
        , waterfallFlow
#endif
        )) {''')
    return t.replace('    ) \n','    )\n')
change('src/shaders/RasterPS.hlsl',raster,True)

def header(t):
    t=replace(t,'    std::unique_ptr<RenderPipelineLayout> layout;', '''    std::unique_ptr<RenderPipelineLayout> layout;
    struct WaterfallSet : RenderDescriptorSetBase {
        uint32_t draws;
        explicit WaterfallSet(RenderDevice *device=nullptr) {
            builder.begin();draws=builder.addStructuredBuffer(1);builder.end();
            if(device)create(device);
        }
    };
    std::unique_ptr<WaterfallSet> waterfallSet;
    std::unique_ptr<RenderPipelineLayout> waterfallLayout;
    std::unique_ptr<RenderShader> waterfallVS,waterfallPS,waterfallMSPS;
    std::map<uint32_t,std::unique_ptr<RenderPipeline>> waterfallPipelines;
    std::vector<interop::float4> waterfallFlow;
    BufferPair waterfallFlowBuffer;
    RenderVertexBufferView waterfallFlowView;
    void collectWaterfall(const DrawData& data,uint32_t start,uint32_t count);
    RenderPipeline *waterfallPipeline(RenderDevice *device,const RenderMultisampling &samples,bool zCompare,bool zWrite,bool coverageAdd);''')
    return replace(t,'WaterRenderer(RenderDevice *device, RenderShaderFormat format, bool usesHDR);',
        'WaterRenderer(RenderDevice *device, RenderShaderFormat format, bool usesHDR,const ShaderLibrary *library);')
change('src/render/rt64_water_renderer.h',header)

pipeline=r'''
RenderPipeline *WaterRenderer::waterfallPipeline(RenderDevice *device,const RenderMultisampling &samples,bool zCompare,bool zWrite,bool coverageAdd) {
    const uint32_t key=samples.sampleCount|(zCompare ? 16 : 0)|(zWrite ? 32 : 0)|(coverageAdd ? 64 : 0);
    if(auto found=waterfallPipelines.find(key);found!=waterfallPipelines.end())return found->second.get();
    const RenderInputSlot slots[]={{0,16},{1,8},{2,16},{3,16}};
    const RenderInputElement elements[]={
        {"POSITION",0,0,RenderFormat::R32G32B32A32_FLOAT,0,0},
        {"TEXCOORD",0,1,RenderFormat::R32G32_FLOAT,1,0},
        {"COLOR",0,2,RenderFormat::R32G32B32A32_FLOAT,2,0},
        {"POSITION",1,3,RenderFormat::R32G32B32A32_FLOAT,3,0}};
    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout=waterfallLayout.get();desc.vertexShader=waterfallVS.get();
    desc.pixelShader=samples.sampleCount>1 ? waterfallMSPS.get() : waterfallPS.get();
    desc.inputSlots=slots;desc.inputSlotsCount=4;desc.inputElements=elements;desc.inputElementsCount=4;
    desc.renderTargetCount=1;desc.renderTargetFormat[0]=RenderTarget::colorBufferFormat(usesHDR);
    // Same dual-source blend/coverage contract as the retail dynamic pipeline.
    desc.renderTargetBlend[0]=RenderBlendDesc::Copy();
    auto& blend=desc.renderTargetBlend[0];blend.blendEnabled=true;
    blend.srcBlend=RenderBlend::SRC1_ALPHA;blend.dstBlend=RenderBlend::INV_SRC1_ALPHA;
    blend.blendOp=RenderBlendOperation::ADD;
    if(coverageAdd)blend.dstBlendAlpha=RenderBlend::ONE;
    desc.multisampling=samples;desc.depthEnabled=zCompare || zWrite;
    desc.depthFunction=zCompare ? RenderComparisonFunction::LESS : RenderComparisonFunction::ALWAYS;
    desc.depthWriteEnabled=zWrite;desc.depthTargetFormat=RenderFormat::D32_FLOAT;
    desc.depthClipEnabled=false;desc.cullMode=RenderCullMode::NONE;
    desc.primitiveTopology=RenderPrimitiveTopology::TRIANGLE_LIST;
    auto result=device->createGraphicsPipeline(desc);
    if(!waterGraphicsPipelineValid(result.get())) {
        std::fprintf(stderr,"[water][waterfall] native pipeline rejected; retaining original draw (samples=%u compare=%u write=%u coverage=%u)\n",
            unsigned(samples.sampleCount),unsigned(zCompare),unsigned(zWrite),unsigned(coverageAdd));
        result.reset();
    }
    // Cache a failed variant, not an invalid wrapper. The original waterfall
    // remains visible and unrelated horizontal-water effects stay enabled.
    auto *pointer=result.get();waterfallPipelines.emplace(key,std::move(result));return pointer;
}
'''
def renderer(t):
    for suffix in ('spirv','dxil'):
        t=replace(t,f'#include "shaders/WaterVS.hlsl.{suffix}.h"',
            '\n'.join(f'#include "shaders/{name}.hlsl.{suffix}.h"' for name in ('WaterVS','WaterfallVS','WaterfallPS','WaterfallMSPS')))
    t=replace(t,'WaterRenderer::WaterRenderer(RenderDevice *device, RenderShaderFormat format, bool hdr) : usesHDR(hdr)',
        'WaterRenderer::WaterRenderer(RenderDevice *device, RenderShaderFormat format, bool hdr,const ShaderLibrary *library) : usesHDR(hdr)')
    t=replace(t,'shared-optics/compact-vertical scratch policy active','shared-optics/direct-waterfall scratch policy active')
    t=replace(t,'    descriptorSet = std::make_unique<WaterDescriptorSet>(device);', '''    waterfallSet=std::make_unique<WaterfallSet>(device);
    FramebufferRendererDescriptorCommonSet common(library->samplerLibrary,device->getCapabilities().raytracing);
    FramebufferRendererDescriptorTextureSet textures;
    FramebufferRendererDescriptorFramebufferSet framebuffer;
    RenderPipelineLayoutBuilder waterfallBuilder;
    waterfallBuilder.begin(false,true);
    waterfallBuilder.addPushConstant(0,0,sizeof(interop::RasterParams),RenderShaderStageFlag::VERTEX|RenderShaderStageFlag::PIXEL);
    waterfallBuilder.addDescriptorSet(common);waterfallBuilder.addDescriptorSet(textures);
    waterfallBuilder.addDescriptorSet(textures);waterfallBuilder.addDescriptorSet(framebuffer);
    waterfallBuilder.addDescriptorSet(*waterfallSet);waterfallBuilder.end();
    waterfallLayout=checkedWaterResource(waterfallBuilder.create(device));
    waterfallVS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(WaterfallVSBlobDXIL,WaterfallVSBlobSPIRV,WaterfallVSBlobMSL,"VSMain",format)));
    waterfallPS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(WaterfallPSBlobDXIL,WaterfallPSBlobSPIRV,WaterfallPSBlobMSL,"PSMain",format)));
    waterfallMSPS=checkedWaterResource(device->createShader(CREATE_SHADER_INPUTS(WaterfallMSPSBlobDXIL,WaterfallMSPSBlobSPIRV,WaterfallMSPSBlobMSL,"PSMain",format)));
    descriptorSet = std::make_unique<WaterDescriptorSet>(device);''')
    t=replace(t,'#include "rt64_render_target.h"','#include "rt64_render_target.h"\n#include "rt64_descriptor_sets.h"\n#include "shared/rt64_raster_params.h"\n#include "WaterPipelineSafety.h"')
    t=replace(t,'    auto result = device->createGraphicsPipeline(desc);', '''    auto result = device->createGraphicsPipeline(desc);
    if(!waterGraphicsPipelineValid(result.get()))result.reset();''')
    t=replace(t,'void WaterRenderer::bindBuffer() {','''void WaterRenderer::bindBuffer() {
    waterfallSet->setBuffer(waterfallSet->draws,drawBuffer.get(),RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    waterfallFlowView=RenderVertexBufferView(waterfallFlowBuffer.get()->at(0),waterfallFlow.size()*sizeof(interop::float4));''')
    t=replace(t,'    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear(); rejectedScenes.clear();',
        '    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear(); rejectedScenes.clear();\n    waterfallFlow.clear();')
    t=replace(t,'drawBuffer.allocatedSize+normalVertexBuffer.allocatedSize+normalFaceBuffer.allocatedSize+wireBuffer.allocatedSize+particleBuffer.allocatedSize',
        'drawBuffer.allocatedSize+normalVertexBuffer.allocatedSize+normalFaceBuffer.allocatedSize+wireBuffer.allocatedSize+particleBuffer.allocatedSize+waterfallFlowBuffer.allocatedSize')
    t=replace(t,'void WaterRenderer::prepareNormals(RenderWorker *worker, const OutputBuffers &outputs, std::vector<BufferUploader::Upload> &uploads) {',
        '''void WaterRenderer::collectWaterfall(const DrawData& data,uint32_t start,uint32_t count) {
    // Same indexed vertex IDs as the raster streams. Missing/object metadata
    // uses the retail UV stream, never a guessed world-axis projection.
    waterfallFlow.resize(data.vertexCount(),interop::float4(0,0,0,0));
    const auto end=std::min<uint64_t>(uint64_t(start)+count,data.faceIndices.size());
    for(uint64_t i=start;i<end;++i) {
        const auto index=data.faceIndices[i];
        if(index>=waterfallFlow.size())continue;
        if(auto found=data.waterfallFlow.find(index);found!=data.waterfallFlow.end())waterfallFlow[index]=found->second;
    }
}

void WaterRenderer::prepareNormals(RenderWorker *worker, const OutputBuffers &outputs, std::vector<BufferUploader::Upload> &uploads) {
    if(waterfallFlow.empty())waterfallFlow.emplace_back(0,0,0,0);
    uploads.push_back({waterfallFlow.data(),{0,waterfallFlow.size()},sizeof(interop::float4),RenderBufferFlag::VERTEX,{},&waterfallFlowBuffer});''')
    a=t.index('    uint32_t ow=classic ? water_policy::bucketExtent')
    b=t.index('    const auto requested=',a)
    t=t[:a]+'    const uint32_t ow=0,oh=0; // Direct waterfall shader: no framebuffer atlas.\n'+t[b:]
    a=t.index('    if(classic && (!scene.originalColor')
    b=t.index('    if(selfDepth &&',a)
    t=t[:a]+t[b:]
    a=t.index('    if(classic) {\n        scene.originalSet->setTexture')
    b=t.index('    ms->setTexture(ms->sceneColor',a)
    t=t[:a]+'''    auto& ms=scene.materialSet;
    ms->setTexture(ms->originalColor,scene.color.get(),RenderTextureLayout::SHADER_READ,scene.colorView.get());
'''+t[b:]
    closing=t.rfind('\n}')
    return t[:closing]+pipeline+t[closing:]
change('src/render/rt64_water_renderer.cpp',renderer)

def framebuffer(t):
    t=replace(t,'p.rasterShaderCache->shaderFormat, shaderLibrary->usesHDR);',
        'p.rasterShaderCache->shaderFormat, shaderLibrary->usesHDR,shaderLibrary);')
    t=replace(t,'const bool wantsWater = waterMaterial && waterMaterial->identity.x > 0.0f &&',
        'const bool wantsWater = waterMaterial && waterMaterial->identity.x > 0.0f &&\n                    (waterMaterial->optics.w<0.5f || waterMaterial->appearance.w>0.0f) &&')
    a=t.index('                        // Use the original texture combiner')
    b=t.index('                    const bool hardware=',a)
    t=t[:a]+'''                        // Single retail-compatible draw. No composite capture, full-screen
                        // allocation or second shading pass for vertical water.
                        worker->commandList->setGraphicsPipelineLayout(waterRenderer->waterfallLayout.get());
                        worker->commandList->setGraphicsDescriptorSet(descCommonSet->get(),0);
                        worker->commandList->setGraphicsDescriptorSet(descTextureSet->get(),1);
                        worker->commandList->setGraphicsDescriptorSet(descTextureSet->get(),2);
                        worker->commandList->setGraphicsDescriptorSet(depthState ? descRealFbSet : descDummyFbSet,3);
                        worker->commandList->setGraphicsDescriptorSet(waterRenderer->waterfallSet->get(),4);
                        const RenderVertexBufferView views[]={indexedVertexViews[0],indexedVertexViews[1],indexedVertexViews[2],waterRenderer->waterfallFlowView};
                        const RenderInputSlot slots[]={{0,16},{1,8},{2,16},{3,16}};
                        worker->commandList->setVertexBuffers(0,views,4,slots);
                        rasterParams.renderIndex=i;rasterParams.padding=drawCall.waterIndex;
                        rasterParams.generatedMipmaps=sampleGeneratedMips && triangles.generatedMipmaps;
                        rasterParams.screenScale=triangles.screenScale;rasterParams.screenOffset=triangles.screenOffset;
                        worker->commandList->setGraphicsPushConstants(0,&rasterParams);
                        worker->commandList->setPipeline(triangles.pipeline);
                        drawCallTriangles(drawCall);
                        rasterParams.padding=0;activeWaterDraw=drawCall.waterIndex;
                        switchToGraphicsPipeline();break;
                    }
'''+t[b:]
    t=replace(t,'                        waterPipeline = waterRenderer->pipeline(p.worker->device,p.fbStorage->colorTarget->multisampling,',
        '''                        waterPipeline = classic ? waterRenderer->waterfallPipeline(p.worker->device,p.fbStorage->colorTarget->multisampling,
                            call.callDesc.otherMode.zCmp(),call.callDesc.otherMode.zUpd(),call.callDesc.otherMode.cvgDst()==CVG_DST_WRAP) :
                            waterRenderer->pipeline(p.worker->device,p.fbStorage->colorTarget->multisampling,''')
    t=replace(t,'waterMatricesValid = waterPipeline && waterRenderer->pipeline(',
        'waterMatricesValid = waterPipeline && (classic || waterRenderer->pipeline(')
    t=replace(t,'selfDepth,false,true,classic,selfDepth);','selfDepth,false,true,classic,selfDepth));')
    a=t.index('                        const auto originalRegion=classic ? waterOriginalRegion(')
    b=t.index('                        const auto waterScene=',a)
    t=t[:a]+'                        const auto originalRegion=water_work::Region{};\n'+t[b:]
    t=replace(t,'                        waterRenderer->collectNormals(drawData,','''                        if(classic)waterRenderer->collectWaterfall(drawData,call.meshDesc.faceIndicesStart,call.callDesc.triangleCount*3);
                        else waterRenderer->collectNormals(drawData,''')
    return t
change('src/render/rt64_framebuffer_renderer.cpp',framebuffer)

def workload_header(t):
    t=replace(t,'#include "rt64_transform_group.h"','#include "rt64_transform_group.h"\n#include <map>')
    return replace(t,'        std::vector<float> tcVelFloats;', '''        std::vector<float> tcVelFloats;
        // Renderer-only metadata, immutable for this submitted task. Does not
        // change generic group matching or guest/netplay state.
        std::map<uint32_t,hlslpp::float2> waterfallPeriods;
        std::map<uint32_t,hlslpp::float4> waterfallFlow;''')
change('src/hle/rt64_workload.h',workload_header)
change('src/hle/rt64_workload.cpp',lambda t:replace(t,'        drawData.tcVelFloats.clear();',
    '        drawData.tcVelFloats.clear();\n        drawData.waterfallPeriods.clear();\n        drawData.waterfallFlow.clear();'))

def rsp(t):
    return replace(t,'        // Indicates the vertex has been used in a tri.', '''        if(state->waterMaterial.identity.x>0 && state->waterMaterial.optics.w>0.5f) {
            const auto& optics=state->waterMaterial.optics;
            if(std::isfinite(optics.y) && std::isfinite(optics.z) && optics.y>=1 && optics.z>=1 && optics.y<=2048 && optics.z<=2048)
                for(unsigned i=0;i<3;i++)workload.drawData.waterfallPeriods[globalIndices[i]]={optics.y,optics.z};
        }

        // Indicates the vertex has been used in a tri.''')
change('src/hle/rt64_rsp.cpp',rsp)

def frame(t):
    t='#include "waterfall_scroll_policy.hpp"\n'+t
    t=replace(t,'        if ((curGroup.texcoordInterpolation != G_EX_COMPONENT_SKIP) && (curVertexCount == prevVertexCount)) {', '''        const auto& currentPeriods=curWorkload.drawData.waterfallPeriods;
        const auto& previousPeriods=prevWorkload.drawData.waterfallPeriods;
        const auto firstWaterfall=currentPeriods.lower_bound(curVertexIndex);
        const bool verticalUV=firstWaterfall!=currentPeriods.end() && firstWaterfall->first<curVertexIndex+curVertexCount;
        if ((verticalUV || curGroup.texcoordInterpolation != G_EX_COMPONENT_SKIP) && (curVertexCount == prevVertexCount)) {''')
    t=replace(t,'            const hlslpp::float2 WrappingModuloHalf = WrappingModulo / 2.0f;', '')
    t=replace(t,'            if (((curGroup.texcoordInterpolation != G_EX_COMPONENT_AUTO) || (curVertexHash == prevVertexHash)) && (curTcHash != prevTcHash)) {',
        '            if ((verticalUV || (curGroup.texcoordInterpolation != G_EX_COMPONENT_AUTO) || (curVertexHash == prevVertexHash)) && (curTcHash != prevTcHash)) {')
    t=replace(t,'                    curVelFloatsRef[i * 2 + 0] = (curTcFloatsRef', '''                    auto period=WrappingModulo;
                    const auto current=verticalUV ? currentPeriods.find(curVertexIndex+i) : currentPeriods.end();
                    const auto previous=verticalUV ? previousPeriods.find(prevVertexIndex+i) : previousPeriods.end();
                    const bool waterfall=current!=currentPeriods.end() && previous!=previousPeriods.end() &&
                        hlslpp::all(current->second==previous->second);
                    if(waterfall)period=current->second;
                    else if(curGroup.texcoordInterpolation==G_EX_COMPONENT_SKIP ||
                        (curGroup.texcoordInterpolation==G_EX_COMPONENT_AUTO && curVertexHash!=prevVertexHash))continue;
                    const auto WrappingModuloHalf=period/2.0f;
                    curVelFloatsRef[i * 2 + 0] = (curTcFloatsRef''')
    # Existing normal/wave wrapping is unchanged; vertical UVs use their own period.
    t=t.replace('/ WrappingModulo[0]) * WrappingModulo[0]', '/ period[0]) * period[0]')
    t=t.replace('/ WrappingModulo[1]) * WrappingModulo[1]', '/ period[1]) * period[1]')
    t=replace(t,'                    if (fabsf(curVelFloatsRef[i * 2]) > WrappingModuloHalf[0]) {', '''                    if(waterfall) {
                        curVelFloatsRef[i*2]=dkr::runtime::water::waterfall_uv_velocity(prevTcFloatsRef[i*2],curTcFloatsRef[i*2],period[0]);
                        curVelFloatsRef[i*2+1]=dkr::runtime::water::waterfall_uv_velocity(prevTcFloatsRef[i*2+1],curTcFloatsRef[i*2+1],period[1]);
                    }
                    if (fabsf(curVelFloatsRef[i * 2]) > WrappingModuloHalf[0]) {''')
    return t
change('src/hle/rt64_game_frame.cpp',frame)

def pixel(t):
    a=t.index('    float4 reflection=screenReflection(')
    b=t.index('    float3 reflected=',a)
    return t[:a]+'''    float4 reflection=0;
#ifdef HARDWARE_WATER
    bool reflectedWaterfall=false,capturedHit=false;
    float4 hardwareReflection=hardwareWaterReflection(worldPosition,N,V,p,reflectedWaterfall,capturedHit);
    if(reflectedWaterfall || capturedHit)reflection=hardwareReflection;
    else {
        // Moving racers still use SSR; static liquid film must not switch
        // appearance at the screen/capture edge or disappear with draw order.
        reflection=screenReflection(worldPosition,N,V,roughness,p);
        if(hardwareReflection.a>reflection.a)reflection=hardwareReflection;
    }
#else
    reflection=screenReflection(worldPosition,N,V,roughness,p);
#endif
'''+t[b:]
change('src/shaders/WaterPS.hlsl',pixel)

output.write_text(''.join(changes),newline='\n')
print(f'Authored {output} ({len(changes)} diff records)')
