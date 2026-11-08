"""Generate authored water-ray patches; dependency/staged inputs stay read-only."""

import difflib
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1]).resolve()
plume = root / "extern/rt64/src/contrib/plume"


def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"Non-unique ray context: {old[:100]}")
    return text.replace(old, new, 1)


def patch(name, changes):
    out = []
    for path, before, after in changes:
        if before == after:
            raise SystemExit(f"No changes in {path}")
        out.append(f"diff --git a/{path} b/{path}\n")
        out.extend(
            difflib.unified_diff(
                before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
            )
        )
    (root / "patches/enhanced-water" / name).write_text("".join(out), newline="\n")


changes = []


def baseline(path):
    before = (stage / path).read_text()
    if "waterRayGeometry" not in (stage / "src/hle/rt64_state.h").read_text():
        return before
    # The stage can predate the last generator invocation. Normalize the one
    # reviewed transactional-layout correction before removing that layer.
    if path == "src/render/rt64_water_renderer.cpp":
        before = before.replace(
            "        if(!rayShader) {",
            "        if(!rayShader || !rayLayout) {\n            // Do not keep a shader if its companion layout creation failed.\n            rayShader.reset();rayLayout.reset();",
        )
    patch_path = root / "patches/enhanced-water/dkr-rays.patch"
    hunks = []
    active = False
    old = []
    new = []

    def finish():
        if active and (old or new):
            hunks.append(("".join(new), "".join(old)))

    for line in patch_path.read_text().splitlines(True):
        if line.startswith("diff --git "):
            finish()
            old = []
            new = []
            active = line.split()[2][2:] == path
        elif line.startswith("@@ "):
            finish()
            old = []
            new = []
        elif line.startswith(("--- ", "+++ ")):
            continue
        elif line.startswith(" "):
            old.append(line[1:])
            new.append(line[1:])
        elif line.startswith("-"):
            old.append(line[1:])
        elif line.startswith("+"):
            new.append(line[1:])
    finish()
    for new, old in reversed(hunks):
        if before.count(new) != 1:
            raise SystemExit(f"Cannot reverse previous ray patch in {path}")
        before = before.replace(new, old, 1)
    return before


def native(path, fn):
    before = baseline(path)
    changes.append((path, before, fn(before)))


native(
    "src/hle/rt64_state.h",
    lambda t: replace(
        replace(t, "#pragma once", '#pragma once\n#include "enhanced_water_ray_geometry.hpp"'),
        "        interop::WaterMaterial waterMaterial{};",
        "        interop::WaterMaterial waterMaterial{};\n        std::shared_ptr<const dkr::runtime::water::enhanced::RayGeometry> waterRayGeometry;",
    ),
)
native(
    "src/hle/rt64_workload.h",
    lambda t: replace(
        replace(t, "#pragma once", '#pragma once\n#include "enhanced_water_ray_geometry.hpp"'),
        "        std::vector<interop::WaterMaterial> waterMaterials;",
        "        std::vector<interop::WaterMaterial> waterMaterials;\n        std::shared_ptr<const dkr::runtime::water::enhanced::RayGeometry> waterRayGeometry;",
    ),
)
native(
    "src/hle/rt64_workload.cpp",
    lambda t: replace(
        t, "        waterMaterials.clear();", "        waterMaterials.clear();\n        waterRayGeometry.reset();"
    ),
)
native(
    "src/hle/rt64_state.cpp",
    lambda t: replace(
        t,
        "            drawCall.waterMaterialIndex = uint32_t(workload.waterMaterials.size() - 1);",
        "            drawCall.waterMaterialIndex = uint32_t(workload.waterMaterials.size() - 1);\n            if(waterMaterial.effects.w>0.5f)workload.waterRayGeometry=waterRayGeometry;",
    ),
)


def water_header(t):
    t = replace(t, "#pragma once", '#pragma once\n#include "WaterRayScene.h"')
    t = replace(
        t,
        "    std::unique_ptr<RenderShader> pixelShader, classicShader, classicMSShader;",
        "    std::unique_ptr<RenderShader> pixelShader, classicShader, classicMSShader;\n    std::unique_ptr<RenderShader> rayShader;\n    std::unique_ptr<RenderPipelineLayout> rayLayout;\n    WaterRayScene rays;\n    bool rayUnavailable=false;\n    bool prepareRays(RenderWorker *worker,RenderShaderFormat format,std::shared_ptr<const dkr::runtime::water::enhanced::RayGeometry> geometry);",
    )
    return replace(t, "bool selfDepth=false);", "bool selfDepth=false, bool hardware=false);")


native("src/render/rt64_water_renderer.h", water_header)


def water_cpp(t):
    t = replace(
        t,
        '#include "shaders/WaterPS.hlsl.spirv.h"',
        '#include "shaders/WaterPS.hlsl.spirv.h"\n#include "shaders/WaterHardwarePS.hlsl.spirv.h"',
    )
    t = replace(
        t,
        '#include "shaders/WaterPS.hlsl.dxil.h"',
        '#include "shaders/WaterPS.hlsl.dxil.h"\n#include "shaders/WaterHardwarePS.hlsl.dxil.h"',
    )
    t = replace(
        t,
        "    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear();",
        "    draws.clear(); normalGroups.clear(); sceneKeys.clear(); drawScenes.clear();\n    rays.resetFrame();",
    )
    t = replace(
        t,
        "bool wireframe, bool surface, bool classic, bool selfDepth) {",
        "bool wireframe, bool surface, bool classic, bool selfDepth, bool hardware) {",
    )
    t = replace(t, "(uint32_t(selfDepth) << 21);", "(uint32_t(selfDepth) << 21) | (uint32_t(hardware) << 22);")
    t = replace(
        t,
        "    desc.pipelineLayout = layout.get();",
        "    desc.pipelineLayout = hardware ? rayLayout.get() : layout.get();",
    )
    t = replace(
        t,
        "(samples.sampleCount>1 ? classicMSShader.get() : classicShader.get()) : pixelShader.get();",
        "(samples.sampleCount>1 ? classicMSShader.get() : classicShader.get()) : hardware ? rayShader.get() : pixelShader.get();",
    )
    point = "void WaterRenderer::collectNormals("
    at = t.index(point)
    t = (
        t[:at]
        + """bool WaterRenderer::prepareRays(RenderWorker *worker,RenderShaderFormat format,std::shared_ptr<const dkr::runtime::water::enhanced::RayGeometry> geometry) {
    if(rayUnavailable || !worker->device->getCapabilities().rayQuery)return false;
    try {
        if(!rayShader || !rayLayout) {
            // Do not keep a shader if its companion layout creation failed.
            rayShader.reset();rayLayout.reset();
            rayShader=checkedWaterResource(worker->device->createShader(CREATE_SHADER_INPUTS(WaterHardwarePSBlobDXIL,WaterHardwarePSBlobSPIRV,WaterHardwarePSBlobMSL,"PSMain",format)));
            WaterRaySet raySet;
            RenderPipelineLayoutBuilder builder;builder.begin(false,true);
            builder.addPushConstant(0,0,sizeof(interop::WaterPushConstants),RenderShaderStageFlag::VERTEX|RenderShaderStageFlag::PIXEL);
            builder.addDescriptorSet(*descriptorSet);builder.addDescriptorSet(raySet);builder.end();
            rayLayout=checkedWaterResource(builder.create(worker->device));
        }
        return rays.prepare(worker,std::move(geometry));
    } catch(const std::exception &error) {
        rayUnavailable=true;
        std::fprintf(stderr,"[water][rays] retaining screen-space reflections: %s\\n",error.what());
        return false;
    }
}

"""
        + t[at:]
    )
    return t


native("src/render/rt64_water_renderer.cpp", water_cpp)


def framebuffer(t):
    t = replace(
        t,
        "                    const bool classic=wp.material.optics.w>0.5f;",
        """                    const bool classic=wp.material.optics.w>0.5f;
                    bool hardware=!classic && wp.material.effects.w>0.5f &&
                        waterRenderer->prepareRays(p.worker,p.rasterShaderCache->shaderFormat,p.curWorkload->waterRayGeometry);
                    wp.material.effects.w=hardware ? 1.0f : 0.0f;""",
    )
    t = replace(
        t,
        "wp.material.identity.y==12,false,classic,selfDepth);",
        "wp.material.identity.y==12,false,classic,selfDepth,hardware);",
    )
    t = replace(
        t,
        "                        waterMatricesValid = waterPipeline && waterRenderer->pipeline(",
        """                        if(hardware && !waterPipeline) {
                            // Failure of the optional hardware pipeline must
                            // retain the software water, not remove the effect.
                            hardware=false;wp.material.effects.w=0;waterRenderer->rayUnavailable=true;
                            waterPipeline=waterRenderer->pipeline(p.worker->device,p.fbStorage->colorTarget->multisampling,
                                call.callDesc.otherMode.zCmp() || selfDepth,call.callDesc.otherMode.zUpd() && !selfDepth,wp.material.identity.y==12,false,classic,selfDepth,false);
                        }
                        waterMatricesValid = waterPipeline && waterRenderer->pipeline(""",
    )
    t = replace(
        t,
        "                    worker->commandList->setGraphicsPipelineLayout(waterRenderer->layout.get());",
        """                    const bool hardware=waterRenderer->draws[drawCall.waterIndex].material.effects.w>0.5f;
                    worker->commandList->setGraphicsPipelineLayout(hardware ? waterRenderer->rayLayout.get() : waterRenderer->layout.get());
                    if(hardware)worker->commandList->setGraphicsDescriptorSet(waterRenderer->rays.set->get(),1);""",
    )
    # The surface mask uses the same vertex inputs, but its layout is always
    # non-ray. Rebind the colour layout after that auxiliary pass.
    t = replace(
        t,
        "                    worker->commandList->setPipeline(triangles.pipeline);\n                    activeWaterDraw=drawCall.waterIndex;",
        """                    worker->commandList->setGraphicsPipelineLayout(hardware ? waterRenderer->rayLayout.get() : waterRenderer->layout.get());
                    worker->commandList->setGraphicsDescriptorSet(waterRenderer->materialSet(drawCall.waterIndex),0);
                    if(hardware)worker->commandList->setGraphicsDescriptorSet(waterRenderer->rays.set->get(),1);
                    worker->commandList->setGraphicsPushConstants(0,&pc);
                    worker->commandList->setPipeline(triangles.pipeline);
                    activeWaterDraw=drawCall.waterIndex;""",
    )
    t = replace(
        t,
        "                    waterRenderer->beginSurface(worker,drawCall.waterIndex);",
        """                    worker->commandList->setGraphicsPipelineLayout(waterRenderer->layout.get());
                    worker->commandList->setGraphicsDescriptorSet(waterRenderer->materialSet(drawCall.waterIndex),0);
                    worker->commandList->setGraphicsPushConstants(0,&pc);
                    waterRenderer->beginSurface(worker,drawCall.waterIndex);""",
    )
    return replace(
        t,
        "        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->recordNormals(worker, *outputBuffers);",
        "        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->rays.record(worker);\n        if (waterRenderer && !waterRenderer->draws.empty()) waterRenderer->recordNormals(worker, *outputBuffers);",
    )


native("src/render/rt64_framebuffer_renderer.cpp", framebuffer)


def shader(t):
    t = replace(
        t,
        "float2 rippleCornerGradient(",
        '#ifdef HARDWARE_WATER\n#include "WaterRays.hlsli"\n#endif\n\nfloat2 rippleCornerGradient(',
    )
    return replace(
        t,
        "    float4 reflection=screenReflection(worldPosition,N,V,roughness,p);",
        """    float4 reflection=screenReflection(worldPosition,N,V,roughness,p);
#ifdef HARDWARE_WATER
    // SSR covers moving actors using their exact rendered colours; hardware
    // rays add static off-screen world intersections, not a second game pass.
    if(reflection.a<0.75) {
        float4 hardwareReflection=hardwareWaterReflection(worldPosition,N,V,p);
        if(hardwareReflection.a>reflection.a)reflection=hardwareReflection;
    }
#endif""",
    )


native("src/shaders/WaterPS.hlsl", shader)
patch("dkr-rays.patch", changes)

changes = []


def backend(name, fn):
    before = (plume / name).read_text()
    changes.append(("src/contrib/plume/" + name, before, fn(before)))


backend(
    "plume_render_interface_types.h",
    lambda t: replace(
        t,
        "        bool raytracingStateUpdate = false;",
        "        bool raytracingStateUpdate = false;\n        bool rayQuery = false; // Feature actually enabled, not inferred from GPU model.",
    ),
)
backend(
    "plume_vulkan.h",
    lambda t: replace(
        t,
        "        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};",
        "        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};\n        VkPhysicalDeviceAccelerationStructurePropertiesKHR waterASProperties = {};",
    ),
)


def vulkan(t):
    t = replace(
        t,
        "        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,",
        "        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,\n        VK_KHR_RAY_QUERY_EXTENSION_NAME,",
    )
    t = replace(
        t,
        "        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {};",
        """        const bool rayQueryFound=bufferDeviceAddressFound &&
            supportedOptionalExtensions.count(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            supportedOptionalExtensions.count(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
            supportedOptionalExtensions.count(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {};""",
    )
    t = replace(
        t,
        "        VkPhysicalDeviceFeatures2 deviceFeatures = {};",
        """        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
        // Query-only GPUs still need acceleration structures, not a ray
        // tracing pipeline. Do not query unsupported pipeline features.
        if(rayQueryFound && !rayTracingFound) {
            accelerationStructureFeatures.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelerationStructureFeatures.pNext=featuresChain;featuresChain=&accelerationStructureFeatures;
        }
        if(rayQueryFound) {
            rayQueryFeatures.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQueryFeatures.pNext=featuresChain;featuresChain=&rayQueryFeatures;
        }
        VkPhysicalDeviceFeatures2 deviceFeatures = {};""",
    )
    point = "        const bool rayTracingSupported = rayTracingPipelineFeatures.rayTracingPipeline && accelerationStructureFeatures.accelerationStructure;"
    t = replace(
        t,
        point,
        point
        + """
        const bool rayQuerySupported=rayQueryFound && accelerationStructureFeatures.accelerationStructure &&
            bufferDeviceAddressFeatures.bufferDeviceAddress && rayQueryFeatures.rayQuery;
        if(rayQuerySupported) {
            rayQueryFeatures.pNext=createDeviceChain;createDeviceChain=&rayQueryFeatures;
            if(!rayTracingSupported) {
                accelerationStructureFeatures.pNext=createDeviceChain;createDeviceChain=&accelerationStructureFeatures;
            }
        }""",
    )
    t = replace(
        t,
        "        capabilities.raytracing = rayTracingSupported;",
        "        capabilities.raytracing = rayTracingSupported;\n        capabilities.rayQuery = rayQuerySupported;",
    )
    t = replace(
        t,
        "static VkPipelineStageFlags toStageFlags(RenderBarrierStages stages, bool geometrySupported, bool rtSupported)",
        "static VkPipelineStageFlags toStageFlags(RenderBarrierStages stages, bool geometrySupported, bool rtSupported, bool querySupported=false)",
    )
    t = replace(
        t,
        """            if (rtSupported) {
                flags |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            }""",
        """            if (rtSupported || querySupported) flags |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            if (rtSupported) flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;""",
    )
    t = replace(
        t,
        "        const bool rtEnabled = queue->device->capabilities.raytracing;",
        "        const bool rtEnabled = queue->device->capabilities.raytracing;\n        const bool queryEnabled = queue->device->capabilities.rayQuery;",
    )
    for stages in ["stages", "interfaceBuffer->barrierStages", "interfaceTexture->barrierStages"]:
        t = replace(
            t,
            f"toStageFlags({stages}, geometryEnabled, rtEnabled)",
            f"toStageFlags({stages}, geometryEnabled, rtEnabled, queryEnabled)",
        )
    t = replace(
        t,
        "        // Check for properties.",
        """        if(rayQueryFound || rayTracingFound) {
            waterASProperties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 properties={};properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext=&waterASProperties;vkGetPhysicalDeviceProperties2(physicalDevice,&properties);
        }
        // Check for properties.""",
    )
    t = replace(
        t,
        "        VkResult res;\n        if (minAlignment > 0)",
        """        if(desc.flags & RenderBufferFlag::ACCELERATION_STRUCTURE_SCRATCH)
            minAlignment=std::max<uint64_t>(minAlignment,device->waterASProperties.minAccelerationStructureScratchOffsetAlignment);
        VkResult res;
        if (minAlignment > 0)""",
    )
    return t


backend("plume_vulkan.cpp", vulkan)


def d3d(t):
    t = replace(
        t,
        "        // Use copy-optimized states.",
        """        if(stages==RenderBarrierStage::COMPUTE && accessBits==RenderBufferAccess::READ &&
            (bufferFlags & RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT))return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        // Use copy-optimized states.""",
    )
    t = replace(
        t,
        "            bool triangleFanSupportOption = false;",
        """            D3D12_FEATURE_DATA_SHADER_MODEL rayShaderModel = {D3D_SHADER_MODEL_6_5};
            const bool rayQueryOption=rtStateUpdateSupportOption &&
                SUCCEEDED(deviceOption->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&rayShaderModel,sizeof(rayShaderModel))) &&
                rayShaderModel.HighestShaderModel>=D3D_SHADER_MODEL_6_5;
            bool triangleFanSupportOption = false;""",
    )
    return replace(
        t,
        "                capabilities.raytracingStateUpdate = rtStateUpdateSupportOption;",
        "                capabilities.raytracingStateUpdate = rtStateUpdateSupportOption;\n                capabilities.rayQuery = rayQueryOption;",
    )


backend("plume_d3d12.cpp", d3d)
patch("dkr-ray-capabilities.patch", changes)
print("Generated renderer and capability patches without changing dependency inputs.")
