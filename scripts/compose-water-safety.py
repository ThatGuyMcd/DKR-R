"""Create DKR's checked, renderer-only safety patch from the preceding stage.

Reads generated input; writes only the authored patch, never RT64 or a stage.
Run after renderer + adapter + budget patches have been applied.
"""

import difflib
from pathlib import Path
import re
import sys

stage = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
output = []

# Rebuilding the authored safety patch from an already-composed build stage
# must first remove the PREVIOUS patch in memory. No generated file is edited.
previous_patch = root / "patches/enhanced-water/dkr-safety.patch"
reverse_hunks = {}
if previous_patch.exists() and "fieldDesired" in (stage / "src/render/rt64_water_renderer.h").read_text():
    path = None
    old = []
    new = []

    def finish_hunk():
        if path and (old or new):
            reverse_hunks.setdefault(path, []).append(("".join(new), "".join(old)))

    for line in previous_patch.read_text().splitlines(True):
        if line.startswith("diff --git "):
            finish_hunk()
            old = []
            new = []
            path = line.split()[2][2:]
        elif line.startswith("@@ "):
            finish_hunk()
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
    finish_hunk()


def change(path, transform):
    before = (stage / path).read_text()
    for new, old in reversed(reverse_hunks.get(path, [])):
        if before.count(new) != 1:
            raise SystemExit(f"Cannot reverse the previous authored safety hunk in {path}")
        before = before.replace(new, old, 1)
    after = transform(before)
    if before == after:
        raise SystemExit(f"No reviewed changes in {path}")
    output.append(f"diff --git a/{path} b/{path}\n")
    output.extend(
        difflib.unified_diff(
            before.splitlines(True), after.splitlines(True), fromfile=f"a/{path}", tofile=f"b/{path}", n=3
        )
    )


def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"Non-unique reviewed water context: {old[:100]}")
    return text.replace(old, new, 1)


def resources(text):
    # Balanced calls can span lines; requireWater checks before dereferencing
    # every optional texture/view/buffer/framebuffer allocation.
    pattern = r"(?:worker->device->create(?:Texture|Framebuffer|Buffer)|(?:scene\.\w+|fieldTextures\[i\])->createTextureView)\("
    matches = list(re.finditer(pattern, text))
    if len(matches) < 12:
        raise SystemExit("Allocation coverage changed")
    for m in reversed(matches):
        end = m.end()
        depth = 1
        while depth and end < len(text):
            if text[end] == "(":
                depth += 1
            elif text[end] == ")":
                depth -= 1
            end += 1
        if depth:
            raise SystemExit("Unbalanced allocation")
        text = text[: m.start()] + "checkedWaterResource(" + text[m.start() : end] + ")" + text[end:]
    text = replace(
        text, '"water shader or pipeline creation failed"', '"water resource creation failed; retaining Original"'
    )
    text = replace(
        text,
        "const uint32_t resolution=m.identity.x>=2 ? 512 : 256;",
        "const uint32_t resolution=m.identity.x>=3 ? 512 : 256;",
    )
    text = replace(text, "if (reset || m.identity.x<2) {", "if (reset || m.identity.x<2 || m.effects.x<0.5f) {")
    text = replace(
        text,
        "    const auto sceneKey=std::make_pair(key,selfDepth);",
        """    const auto sceneKey=std::make_pair(key,selfDepth);
    auto found = sceneKeys.find(sceneKey);
    if (found != sceneKeys.end()) return found->second;
    const uint64_t pixels=uint64_t(color->width)*color->height;
    const uint32_t candidate=uint32_t(sceneKeys.size());
    const uint32_t retainedOriginal=candidate<scenes.size() ? scenes[candidate].originalSamples : 0;
    const uint64_t perPixel=16+4*color->multisampling.sampleCount+
        (selfDepth ? 4*depth->multisampling.sampleCount : 0)+
        8*std::max(retainedOriginal,classic ? color->multisampling.sampleCount : 0u);
    uint64_t resident=pixels*perPixel;
    for (uint32_t i=0;i<scenes.size();i++) if (i!=candidate) {
        const auto &s=scenes[i];
        resident+=uint64_t(s.width)*s.height*(16+4*s.surfaceSamples+8*s.originalSamples);
        resident+=uint64_t(s.waterDepthWidth)*s.waterDepthHeight*4*s.waterDepthSamples;
    }
    // Bound intermediate memory without reducing game-image resolution.
    // Unsupported/oversized targets retain authored water instead of OOM.
    if (!color->width || !color->height || color->width>8192 || color->height>8192 ||
        resident>256ULL*1024*1024 || sceneKeys.size()>=16)
        throw std::runtime_error("water intermediate target exceeds its bounded budget");
""",
    )
    text = replace(
        text,
        """    auto found = sceneKeys.find(sceneKey);
    if (found != sceneKeys.end()) return found->second;
    const uint32_t index = uint32_t(sceneKeys.size());""",
        "    const uint32_t index = uint32_t(sceneKeys.size());",
    )
    text = replace(
        text,
        "            auto key = std::make_tuple(group,int(data.posFloats[v*3]),int(data.posFloats[v*3+1]),int(data.posFloats[v*3+2]));",
        """            const auto &material=draws.back().material;
            hlslpp::float4 position(data.posFloats[v*3],data.posFloats[v*3+1],data.posFloats[v*3+2],1);
            if (material.cameraPosition.w==1.0f) {
                if (v>=data.worldIndices.size() || data.worldIndices[v]>=data.worldTransforms.size()) continue;
                position=hlslpp::mul(position,hlslpp::float4x4(data.worldTransforms[data.worldIndices[v]]));
                position=hlslpp::mul(position,cameraInverse);
                if (!std::isfinite(float(position.w)) || std::abs(float(position.w))<0.000001f) continue;
                position=position/float(position.w);
            }
            if (!std::isfinite(float(position.x)) || !std::isfinite(float(position.y)) || !std::isfinite(float(position.z)) ||
                std::abs(float(position.x))>10000000 || std::abs(float(position.y))>10000000 || std::abs(float(position.z))>10000000) continue;
            auto key = std::make_tuple(group,int(std::round(float(position.x)*4)),int(std::round(float(position.y)*4)),int(std::round(float(position.z)*4)));""",
    )
    text = replace(
        text,
        "    wireStarts.push_back(uint32_t(wireIndices.size()));",
        """    wireStarts.push_back(uint32_t(wireIndices.size()));
    const auto cameraInverse=hlslpp::inverse(hlslpp::float4x4(draws.back().material.cameraViewProjection));""",
    )
    text = replace(
        text,
        "const bool reset=fieldGeneration!=uint32_t(m.identity.w) || desired<fieldTime-0.1f || desired>fieldTime+0.5f;",
        "const bool reset=fieldGeneration!=uint32_t(m.identity.w) || desired<fieldDesired-0.00001f || desired>fieldDesired+0.25f;\n    fieldDesired=desired;",
    )
    text = text.replace("WR64_WATER_", "DKRR_WATER_")
    return text


change("src/render/rt64_water_renderer.cpp", resources)


def fallback(text):
    text = replace(
        text,
        """                        wp.viewProjection = wp.material.cameraViewProjection;
                        wp.camera.x = wp.material.cameraPosition.x;
                        wp.camera.y = wp.material.cameraPosition.y;
                        wp.camera.z = wp.material.cameraPosition.z;""",
        """                        wp.viewProjection = wp.material.cameraViewProjection;
                        // Match the matrix used by interpolated water vertices,
                        // not the current simulation camera (DKR stores MVPs).
                        const auto address=uint32_t(wp.material.effects.y);
                        const auto &addresses=drawData.worldTransformPhysicalAddresses;
                        for (uint32_t i=0;i<addresses.size();i++) if (addresses[i]==address) {
                            if (i<drawData.lerpWorldTransforms.size()) wp.viewProjection=drawData.lerpWorldTransforms[i];
                            break;
                        }
                        const auto eye=hlslpp::mul(hlslpp::float4(0,0,1,0),hlslpp::inverse(wp.viewProjection));
                        wp.camera.x=float(eye.x/eye.w);
                        wp.camera.y=float(eye.y/eye.w);
                        wp.camera.z=float(eye.z/eye.w);""",
    )
    text = replace(
        text,
        "                if (wantsWater && waterRenderer && waterSamplesSupported) {",
        "                if (wantsWater && waterRenderer && !waterUnavailable && waterSamplesSupported) {\n                    try {",
    )
    text = replace(
        text,
        "                        instanceDrawCall.waterIndex = uint32_t(waterRenderer->draws.size());",
        """                        const auto waterScene=waterRenderer->prepareScene(p.worker,p.fbStorage->colorTarget,p.fbStorage->depthTarget,
                            (uint64_t(framebufferCount) << 32) | proj.transformsIndex,classic,selfDepth);
                        instanceDrawCall.originalWaterPipeline=triangles.pipeline;
                        instanceDrawCall.waterIndex = uint32_t(waterRenderer->draws.size());""",
    )
    text = replace(
        text,
        "                        if (wp.material.optics.w>0.5f) instanceDrawCall.originalWaterPipeline=triangles.pipeline;",
        "",
    )
    text = replace(
        text,
        "                    if (drawCall.originalWaterPipeline) {",
        "                    if (waterRenderer->draws[drawCall.waterIndex].material.optics.w>0.5f) {",
    )
    text = replace(
        text,
        "                    const bool classic=drawCall.originalWaterPipeline!=nullptr;",
        "                    const bool classic=waterRenderer->draws[drawCall.waterIndex].material.optics.w>0.5f;",
    )
    text = replace(
        text,
        """                        waterRenderer->drawScenes.push_back(waterRenderer->prepareScene(p.worker,p.fbStorage->colorTarget,p.fbStorage->depthTarget,
                            (uint64_t(framebufferCount) << 32) | proj.transformsIndex,classic,selfDepth));""",
        "                        waterRenderer->drawScenes.push_back(waterScene);",
    )
    text = replace(
        text,
        """                    }
                }

                // Determine to use the draw call either in the RT scene or the raster scene.""",
        """                    }
                    } catch (const std::exception &error) {
                        waterUnavailable=true;
                        std::fprintf(stderr,"[water] resource fallback to Original: %s\\n",error.what());
                    }
                }

                // Determine to use the draw call either in the RT scene or the raster scene.""",
    )
    text = replace(
        text,
        """        if (waterRenderer && !waterRenderer->draws.empty()) {
            waterRenderer->prepareField(worker);
            waterRenderer->prepareNormals(worker, *outputBuffers, shaderUploads);
            shaderUploads.push_back({waterRenderer->draws.data(), {0,waterRenderer->draws.size()}, sizeof(interop::WaterDrawParams), RenderBufferFlag::STORAGE, {}, &waterRenderer->drawBuffer});
        }""",
        """        if (waterRenderer && !waterRenderer->draws.empty()) {
            const auto uploadStart=shaderUploads.size();
            try {
                if (waterUnavailable) throw std::runtime_error("water preparation was incomplete");
                waterRenderer->prepareField(worker);
                waterRenderer->prepareNormals(worker, *outputBuffers, shaderUploads);
                shaderUploads.push_back({waterRenderer->draws.data(), {0,waterRenderer->draws.size()}, sizeof(interop::WaterDrawParams), RenderBufferFlag::STORAGE, {}, &waterRenderer->drawBuffer});
            } catch (const std::exception &error) {
                shaderUploads.resize(uploadStart);
                for (auto &draw : instanceDrawCallVector) if (draw.waterIndex!=UINT32_MAX) {
                    draw.triangles.pipeline=draw.originalWaterPipeline;
                    draw.originalWaterPipeline=nullptr;
                    draw.waterIndex=UINT32_MAX;
                }
                waterRenderer->reset();
                waterUnavailable=true;
                std::fprintf(stderr,"[water] frame fallback to Original: %s\\n",error.what());
            }
        }""",
    )
    return text


change("src/render/rt64_framebuffer_renderer.cpp", fallback)
change(
    "src/render/rt64_water_renderer.h",
    lambda text: replace(text, "    float fieldTime = 0;", "    float fieldTime = 0, fieldDesired = 0;"),
)


def quality(text):
    text = replace(
        text,
        "[loop] for (uint step=0;step<36;step++) {",
        "[loop] for (uint step=0;step<(p.material.identity.x>=3 ? 36u : 24u);step++) {",
    )
    return replace(
        text,
        "    if (position.z < 0 || position.z > 1022.0/1024.0) discard;",
        """    if (position.z < 0 || position.z > 1022.0/1024.0) discard;
    if (m.effects.z>=3) {
        // Preserve the original waterfall/fountain's transparency, palette
        // and course fog. This is NOT a horizontal ocean plane. Continuous
        // world-space streaks add detail without wrapping the authored UVs.
        uint2 at=uint2(position.xy);
#ifdef CLASSIC_MSAA
        uint w,h; sceneColor.GetDimensions(w,h);
        at+=uint2((sampleIndex&1)*w,(sampleIndex>>1)*h);
#endif
        float3 base=originalColor.Load(int3(at,0)).rgb;
        float2 flow=float2(worldPosition.x+worldPosition.z,worldPosition.y+p.camera.w*90)*float2(0.09,0.035);
        float filter=1-smoothstep(0.3,1.3,max(length(ddx(flow)),length(ddy(flow))));
        float streak=smoothstep(0.67,0.91,foamNoise(flow))*filter*m.animation.w;
        float visibility=p.fogColor.a>0.5 ? 1-saturate(sourceColor.a) : 1;
        float3 result=lerp(base,base+float3(0.025,0.033,0.035),saturate(streak*visibility));
        return float4(pow(saturate(result),1.0/2.2),1);
    }""",
    )


change("src/shaders/WaterPS.hlsl", quality)
(root / "patches/enhanced-water/dkr-safety.patch").write_text("".join(output), newline="\n")
print("Generated reviewed allocation, normal-space, quality and fallback patch.")
