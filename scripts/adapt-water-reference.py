"""Reproduce the reviewed renderer-only import from the pinned MIT reference.

This is a mechanical patch transformation, not a dependency checkout edit.
Usage: python scripts/adapt-water-reference.py path/to/rt64-water.patch
"""

import hashlib
from pathlib import Path
import re
import sys

SOURCE_SHA256 = "59e12d11e69bc43cc885c1a81b7f3ce0fccf0aede47f58cdf13a22364a3eb578"
source = Path(sys.argv[1]).read_bytes()
if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
    raise SystemExit("Reference does not match reviewed Wave Race commit 9041cc8024f6d2015859f88f5a0307b36cd4e50a")
sections = re.split(r"(?=^diff --git )", source.decode().replace("\r\n", "\n"), flags=re.M)
selected = []
existing = {
    "src/hle/rt64_draw_call.h",
    "src/hle/rt64_state.cpp",
    "src/hle/rt64_state.h",
    "src/hle/rt64_workload.cpp",
    "src/hle/rt64_workload.h",
    "src/render/rt64_descriptor_sets.h",
    "src/render/rt64_framebuffer_renderer.cpp",
    "src/render/rt64_framebuffer_renderer.h",
    "src/render/rt64_framebuffer_renderer_call.h",
    "src/render/rt64_rsp_processor.cpp",
    "src/shaders/RSPProcessCS.hlsl",
}
for section in sections:
    match = re.match(r"diff --git a/(\S+) b/\S+", section)
    if not match:
        continue
    path = match[1]
    if (
        path in existing
        or path.startswith("src/shaders/Water")
        or path
        in {
            "src/shared/rt64_water_params.h",
            "src/render/rt64_water_renderer.cpp",
            "src/render/rt64_water_renderer.h",
        }
    ):
        # The reference's diagnostics are not part of the product renderer.
        if path == "src/render/rt64_framebuffer_renderer.cpp":
            begin = section.index("+                    static const bool waterDiagnostics=")
            end = section.index("+                    bool waterMatricesValid=", begin)
            section = section[:begin] + section[end:]
            # Android's preceding timing patch inserts statements at function
            # entry. Use the unique unchanged state declarations, with context
            # on both sides of the insertion, preserving those statements.
            marker = "+        uint32_t waterProfileToken = UINT32_MAX, waterProfileScene = UINT32_MAX, activeWaterDraw = UINT32_MAX;\n"
            trailing = "         InstanceDrawCall::Type previousCallType = InstanceDrawCall::Type::Unknown;\n         bool previousVertexTestZ = false;\n         const RenderPipeline *previousPipeline = nullptr;\n"
            original = (
                "@@ -466,6 +471,7 @@\n     }\n     \n     void FramebufferRenderer::submitRasterScene(RenderWorker *worker, const Framebuffer &framebuffer, RenderFramebufferStorage *fbStorage, const RasterScene &rasterScene, bool &depthState) {\n"
                + marker
                + trailing
            )
            replacement = (
                "@@ -469,3 +474,4 @@\n" + trailing.splitlines(True)[0] + marker + "".join(trailing.splitlines(True)[1:])
            )
            if section.count(original) != 1:
                raise SystemExit("Reference function-entry context changed")
            section = section.replace(original, replacement, 1)
            # Keep Android's draw metrics at the end of the loop intact.
            original = "@@ -637,6 +729,9 @@\n                 break;\n             }\n         }\n+\n+        finishWater();\n+        if (waterProfileToken != UINT32_MAX) waterRenderer->endProfile(worker,waterProfileToken);\n \n         // Mark targets for resolve.\n         if (fbStorage->colorTarget != nullptr) {\n"
            replacement = "@@ -641,3 +733,5 @@\n         // Mark targets for resolve.\n+        finishWater();\n+        if (waterProfileToken != UINT32_MAX) waterRenderer->endProfile(worker,waterProfileToken);\n         if (fbStorage->colorTarget != nullptr) {\n             fbStorage->colorTarget->markForResolve();\n"
            if section.count(original) != 1:
                raise SystemExit("Reference end-of-loop context changed")
            section = section.replace(original, replacement, 1)
        selected.append(section)
    elif path == "src/hle/rt64_workload_queue.cpp":
        # ONLY the interpolation weight, never its queue teardown changes,
        # private benchmark override or blocking profile readbacks.
        header, *hunks = re.split(r"(?=^@@ )", section, flags=re.M)
        keep = [h for h in hunks if "+                    drawParams.interpolationWeight" in h]
        if len(keep) != 1:
            raise SystemExit("Expected exactly one presentation-weight hunk")
        selected.append(header + keep[0])

out = Path(__file__).resolve().parents[1] / "patches/enhanced-water/renderer.patch"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(selected), encoding="utf-8", newline="\n")
print(f"Imported {len(selected)} reviewed renderer sections into {out}")
