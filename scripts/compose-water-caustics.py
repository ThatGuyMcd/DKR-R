"""Author the caustic texture layer without editing dependencies.

Read the fully composed stage; emit one final pipeline patch. Re-runs reverse
only this layer in memory. The caustic PNG is an authored native input that
EnhancedWater.cmake embeds with the dependency's own file_to_c tool.
"""
from pathlib import Path
import difflib
import sys
from water_patch_baseline import read_before

root=Path(__file__).resolve().parents[1]
stage=Path(sys.argv[1]).resolve()
output=root/'patches/enhanced-water/dkr-caustics.patch'
layers=[output] if 'causticTexture' in (stage/'src/shaders/WaterPS.hlsl').read_text() else []
changes=[]

def replace(text,old,new):
    if text.count(old)!=1:
        raise SystemExit(f'Non-unique caustics context: {old[:120]}')
    return text.replace(old,new,1)

def change(path,fn):
    before=read_before(stage,path,layers)
    after=fn(before)
    if before==after:raise SystemExit(f'No caustics changes: {path}')
    changes.append(f'diff --git a/{path} b/{path}\n')
    changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
        fromfile=f'a/{path}',tofile=f'b/{path}',n=3))

def header(t):
    t=replace(t,'''    uint32_t draws, sceneColor, sceneDepth, currentField, previousField, originalColor;
    explicit WaterDescriptorSet(RenderDevice *device = nullptr) {''',
        '''    uint32_t draws, sceneColor, sceneDepth, currentField, previousField, originalColor, caustics;
    explicit WaterDescriptorSet(RenderDevice *device = nullptr, const RenderSampler *causticSampler = nullptr) {''')
    t=replace(t,'''        originalColor = builder.addTexture(6);
        builder.end();''','''        originalColor = builder.addTexture(6);
        caustics = builder.addTexture(7);
        // Immutable samplers go last: D3D12 drops them from the root table but
        // still counts them in the set, so any later range would read empty.
        builder.addImmutableSampler(8, causticSampler);
        builder.end();''')
    t=replace(t,'''    BufferPair drawBuffer;
    std::unique_ptr<WaterDescriptorSet> descriptorSet;''','''    BufferPair drawBuffer;
    // Declared before every descriptor set: the immutable sampler and the
    // texture must outlive the sets that sample them.
    std::unique_ptr<RenderSampler> causticSampler;
    std::unique_ptr<RenderTexture> causticTexture;
    std::unique_ptr<RenderTextureView> causticView;
    std::unique_ptr<RenderBuffer> causticUpload;
    std::vector<uint32_t> causticOffsets;
    uint32_t causticSize=0;
    bool causticPending=false;
    std::unique_ptr<WaterDescriptorSet> descriptorSet;''')
    return replace(t,'    void recordField(RenderWorker *worker);\n',
        '    void recordField(RenderWorker *worker);\n    void recordCaustics(RenderWorker *worker);\n')
change('src/render/rt64_water_renderer.h',header)

def renderer(t):
    t=replace(t,'#include <stdexcept>\n','''#include <stdexcept>
#include "stb/stb_image.h"
#include "textures/WaterCaustics.png.h"
''')
    t=replace(t,'''    descriptorSet = std::make_unique<WaterDescriptorSet>(device);
    RenderPipelineLayoutBuilder builder;''','''    // The caustic pattern is an authored grayscale PNG embedded at build time.
    // Box-filtered mips are staged here and uploaded once by recordCaustics.
    RenderSamplerDesc causticDesc;
    causticDesc.maxAnisotropy=getDefaultSamplerAnisotropy();
    causticDesc.anisotropyEnabled=causticDesc.maxAnisotropy>1;
    causticSampler=checkedWaterResource(device->createSampler(causticDesc));
    int causticWidth=0,causticHeight=0,causticChannels=0;
    stbi_uc *causticPixels=stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(WaterCausticsPNG),int(sizeof(WaterCausticsPNG)),
        &causticWidth,&causticHeight,&causticChannels,1);
    if (!causticPixels || causticWidth<1 || causticWidth!=causticHeight || (causticWidth&(causticWidth-1))) {
        stbi_image_free(causticPixels);
        throw std::runtime_error("water caustic texture is invalid; retaining Original");
    }
    causticSize=uint32_t(causticWidth);
    std::vector<std::vector<uint8_t>> causticMips{std::vector<uint8_t>(causticPixels,causticPixels+size_t(causticSize)*causticSize)};
    stbi_image_free(causticPixels);
    for (uint32_t size=causticSize;size>1;size/=2) {
        const auto &source=causticMips.back();
        const uint32_t half=size/2;
        std::vector<uint8_t> next(size_t(half)*half);
        for (uint32_t y=0;y<half;y++) for (uint32_t x=0;x<half;x++) {
            const size_t at=size_t(2*y)*size+2*x;
            next[size_t(y)*half+x]=uint8_t((source[at]+source[at+1]+source[at+size]+source[at+size+1]+2)/4);
        }
        causticMips.push_back(std::move(next));
    }
    // D3D12 placed footprints: 256-byte row pitch, 512-byte mip placement.
    uint32_t causticBytes=0;
    for (uint32_t level=0;level<causticMips.size();level++) {
        causticBytes=(causticBytes+511)&~511u;
        causticOffsets.push_back(causticBytes);
        const uint32_t size=std::max(causticSize>>level,1u);
        causticBytes+=((size+255)&~255u)*size;
    }
    causticUpload=checkedWaterResource(device->createBuffer(RenderBufferDesc::UploadBuffer(causticBytes)));
    auto *causticStaging=static_cast<uint8_t *>(causticUpload->map());
    std::memset(causticStaging,0,causticBytes);
    for (uint32_t level=0;level<causticMips.size();level++) {
        const uint32_t size=std::max(causticSize>>level,1u), pitch=(size+255)&~255u;
        for (uint32_t y=0;y<size;y++) std::memcpy(causticStaging+causticOffsets[level]+size_t(y)*pitch,causticMips[level].data()+size_t(y)*size,size);
    }
    causticUpload->unmap();
    causticTexture=checkedWaterResource(device->createTexture(RenderTextureDesc::Texture2D(causticSize,causticSize,uint32_t(causticMips.size()),RenderFormat::R8_UNORM)));
    causticView=checkedWaterResource(causticTexture->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R8_UNORM)));
    causticTexture->setName("Water authored caustic pattern");
    causticPending=true;
    descriptorSet = std::make_unique<WaterDescriptorSet>(device,causticSampler.get());
    RenderPipelineLayoutBuilder builder;''')
    t=replace(t,'''    for (auto &scene : scenes) { scene.captured=false; scene.surfaceCleared=false; }
}''','''    for (auto &scene : scenes) { scene.captured=false; scene.surfaceCleared=false; }
    // The workload that recorded the caustic upload has completed.
    if (!causticPending) causticUpload.reset();
}''')
    t=replace(t,'        scene.materialSet = std::make_unique<WaterDescriptorSet>(worker->device);\n',
        '        scene.materialSet = std::make_unique<WaterDescriptorSet>(worker->device,causticSampler.get());\n')
    t=replace(t,'''    ms->setTexture(ms->sceneDepth,scene.depth.get(),RenderTextureLayout::SHADER_READ,scene.depthView.get());
''','''    ms->setTexture(ms->sceneDepth,scene.depth.get(),RenderTextureLayout::SHADER_READ,scene.depthView.get());
    ms->setTexture(ms->caustics,causticTexture.get(),RenderTextureLayout::SHADER_READ,causticView.get());
''')
    return replace(t,'''uint32_t WaterRenderer::beginProfile(RenderWorker *worker, uint32_t kind) {''','''void WaterRenderer::recordCaustics(RenderWorker *worker) {
    if (!causticPending) return;
    worker->commandList->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(causticTexture.get(),RenderTextureLayout::COPY_DEST));
    for (uint32_t level=0;level<causticOffsets.size();level++) {
        const uint32_t size=std::max(causticSize>>level,1u);
        worker->commandList->copyTextureRegion(RenderTextureCopyLocation::Subresource(causticTexture.get(),level),
            RenderTextureCopyLocation::PlacedFootprint(causticUpload.get(),RenderFormat::R8_UNORM,size,size,1,(size+255)&~255u,causticOffsets[level]));
    }
    worker->commandList->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(causticTexture.get(),RenderTextureLayout::SHADER_READ));
    causticPending=false;
}

uint32_t WaterRenderer::beginProfile(RenderWorker *worker, uint32_t kind) {''')
change('src/render/rt64_water_renderer.cpp',renderer)

def framebuffer(t):
    return replace(t,'''        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->recordField(worker);
''','''        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->recordField(worker);
        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->recordCaustics(worker);
''')
change('src/render/rt64_framebuffer_renderer.cpp',framebuffer)

def pixels(t):
    t=replace(t,'Texture2D<float4> originalColor : register(t6);\n','''Texture2D<float4> originalColor : register(t6);
Texture2D<float> causticTexture : register(t7);
SamplerState causticSampler : register(s8);
''')
    t=replace(t,'''    // Derivatives must be evaluated for the whole pixel quad, before the
    // depth-validity branch. A branch crossing a shoreline otherwise leaves
    // caustic receiver normals/filter widths undefined on some backends.
    float bottomDepth=sceneDepth.Load(int3(int2(boundPixel(refractedPixel,p)),0));
    float3 bottom=waterWorldFromPixel(refractedPixel,bottomDepth,p);
    float3 bottomNormal=cross(ddx(bottom),ddy(bottom));
    bottomNormal*=rsqrt(max(dot(bottomNormal,bottomNormal),0.000001));
    if (bottomNormal.y<0) bottomNormal=-bottomNormal;
    float2 causticUV=bottom.xz*0.035;
    float causticFilter=1-smoothstep(0.25,1.5,length(fwidth(causticUV)));
''','''    // Caustics follow the reference water workflow: one authored pattern read
    // twice on the water plane, panned in opposite directions, the second
    // layer rescaled so the two never align. Mipmaps keep distant cells from
    // breaking into stair-stepped lines. Sample before the depth-validity
    // branch: a branch crossing a shoreline leaves derivatives undefined.
    float2 causticUV=worldPosition.xz/130+ripples*0.6;
    float caustic=causticTexture.Sample(causticSampler,causticUV+float2(-0.05,0.05)*p.camera.w)
        +causticTexture.Sample(causticSampler,(causticUV+float2(0.05,-0.05)*p.camera.w)*0.66);
    // Vertical water column under this pixel. Unlike the view-ray thickness it
    // does not grow at grazing angles. Negative without a submerged surface.
    float waterDepth=thickness>=0 ? max(worldPosition.y-waterWorldFromPixel(position.xy,
        sceneDepth.Load(int3(int2(position.xy),0)),p).y,0) : -1;
''')
    return replace(t,'''        // Stable world-space light focusing, confined to an actual shallow
        // submerged receiver. Dark source lighting and steep walls reject it.
        float phaseA=causticUV.x+sin(causticUV.y*1.3+p.camera.w*0.65);
        float phaseB=causticUV.y-sin(causticUV.x*1.1-p.camera.w*0.5);
        float focusing=pow(saturate(1-abs(sin(phaseA)+sin(phaseB)*0.55)),12);
        float receiver=smoothstep(0.2,0.65,bottomNormal.y)*saturate(dot(bottomNormal,L));
        receiver*=smoothstep(0.015,0.08,dot(bottomColor,float3(0.2126,0.7152,0.0722)));
        receiver*=bottom.y<worldPosition.y-2 && refractedThickness<visibilityRange ? 1 : 0;
        bottomColor+=m.sunColor.rgb*focusing*causticFilter*receiver*m.optics.z;
''','''        // Focused light only in shallow water and near the camera, and never
        // on receivers whose source lighting is dark.
        float receiver=smoothstep(0,2,waterDepth)*(1-smoothstep(0,60,waterDepth));
        receiver*=1-smoothstep(900,2200,length(p.camera.xyz-worldPosition));
        receiver*=smoothstep(0.015,0.08,dot(bottomColor,float3(0.2126,0.7152,0.0722)));
        bottomColor+=m.sunColor.rgb*caustic*receiver*m.optics.z*4;
''')
change('src/shaders/WaterPS.hlsl',pixels)

output.write_text(''.join(changes),newline='\n')
print('Generated the caustic texture patch.')
