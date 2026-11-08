"""Guard the live LOD wiring; pixel correctness is tested by live_lod_probe.hlsl."""

import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


class LiveLodPipelineTests(unittest.TestCase):
    def test_ui_publishes_before_hidden_overlay_early_return(self):
        draw = read("runtime-recomp/src/game/runtime_ui.cpp").split(
            "void dkr::runtime::ui::draw(RT64::Application& application) {", 1
        )[1]
        before_return = draw.split("return;", 1)[0]
        self.assertIn("RT64::setDefaultSamplerMipLODBias(", before_return)
        self.assertIn("dkr::runtime::enhancements::effective_texture_lod_bias()", before_return)

    def test_raster_pass_uses_live_value_not_constructor_snapshot(self):
        renderer = read("extern/rt64/src/render/rt64_framebuffer_renderer.cpp")
        self.assertIn("rasterParams.mipLODBias = getDefaultSamplerMipLODBias();", renderer)
        library = read("extern/rt64/src/render/rt64_shader_library.cpp")
        self.assertNotIn("this->samplerMipLODBias", library)
        self.assertIn("samplerDesc.mipLODBias = 0.0f;", library)
        self.assertNotIn("samplerDesc.mipLODBias = getDefault", library)

    def test_both_texture_stages_and_both_sampling_paths(self):
        raster = read("extern/rt64/src/shaders/RasterPS.hlsl")
        self.assertEqual(raster.count("gConstants.mipLODBias"), 2)
        sampler = read("extern/rt64/src/shaders/TextureSampler.hlsli")
        self.assertIn("log2(ddMax) + mipLODBias", sampler)
        self.assertIn("exp2(mipLODBias)", sampler)
        self.assertIn("(ddxUV / originalSize) * lodGradientScale", sampler)
        self.assertIn("(ddyUV / originalSize) * lodGradientScale", sampler)
        self.assertIn("if (flagHasMipmaps)", sampler)

    def test_existing_patch_is_the_single_bias_owner(self):
        path = "patches/rt64/0015-configurable-default-mip-lod-bias.patch"
        digest = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
        self.assertIn(digest.lower(), read("patches/manifest.json").lower())
        owners = [
            p.name
            for p in (ROOT / "patches/rt64").glob("*.patch")
            if "gDefaultSamplerMipLODBias" in p.read_text(encoding="utf-8")
        ]
        self.assertEqual(owners, [Path(path).name])


class GeneratedMipmapPipelineTests(unittest.TestCase):
    def test_manifest_and_separate_patch_ownership(self):
        path = ROOT / "patches/rt64/0016-optional-generated-texture-mips.patch"
        self.assertIn(hashlib.sha256(path.read_bytes()).hexdigest(), read("patches/manifest.json"))
        self.assertNotIn("gDefaultSamplerMipLODBias", path.read_text(encoding="utf-8"))

    def test_generation_only_at_session_boundary(self):
        renderer = read("runtime-recomp/src/game/rt64_renderer.cpp")
        self.assertLess(
            renderer.index("RT64::beginGeneratedMipSession"), renderer.index("std::make_unique<RT64::Application>")
        )
        ui = read("runtime-recomp/src/game/runtime_ui.cpp")
        self.assertNotIn("beginGeneratedMipSession", ui)
        draw = ui.split("void dkr::runtime::ui::draw(RT64::Application& application) {", 1)[1].split("return;", 1)[0]
        self.assertIn("RT64::setGeneratedMipSampling", draw)
        self.assertEqual(ui.count('"modern_generate_texture_mipmaps'), 2)
        self.assertIn("g_generated_mipmaps_requested{false}", read("runtime-recomp/src/game/runtime_enhancements.cpp"))

    def test_upload_order_lifetime_and_dds_preservation(self):
        cache = read("extern/rt64/src/render/rt64_texture_cache.cpp")
        dds = cache.split("bool TextureCache::setDDS(", 1)[1].split("bool TextureCache::setLowMipCache(", 1)[0]
        self.assertNotIn("GeneratedMips::", dds)
        section = cache.split("GeneratedMips::Scratch mipScratch", 1)[1]
        self.assertLess(section.index("GeneratedMips::record("), section.index("directWorker->wait();"))
        self.assertLess(section.index("directWorker->wait();"), section.index("textureMap.add("))
        self.assertIn("textureCache->generateMipmaps", cache)
        generator = read("extern/rt64/src/render/rt64_generated_mips.h")
        self.assertNotIn("waitForCommandFence", generator)
        self.assertIn("retained.textures.emplace_back(std::move(dst.texture))", generator)
        self.assertIn("MaxScratchBytes", generator)

    def test_draw_scope_and_sampling_exclusions(self):
        sampler = read("extern/rt64/src/shaders/TextureSampler.hlsli")
        for guard in (
            "!renderFlagRect",
            "!otherMode.forceBlend",
            "!otherMode.cvgXAlpha",
            "otherMode.alphaCompare() == G_AC_NONE",
            "!gpuTileFlagRawTMEM",
            "!gpuTileFlagFromCopy",
            "wholeS && wholeT",
        ):
            self.assertIn(guard, sampler)
        renderer = read("extern/rt64/src/render/rt64_framebuffer_renderer.cpp")
        self.assertIn("generatedMipPerspectiveScope(proj.type == Projection::Type::Perspective", renderer)
        self.assertIn("drawData.worldTransforms, call.callDesc.minWorldMatrix, call.callDesc.maxWorldMatrix", renderer)
        self.assertIn("(!generated || eligible)", sampler)

    def test_loading_feedback_releases_upload_lock_and_does_not_replay_game(self):
        cache = read("extern/rt64/src/render/rt64_texture_cache.cpp")
        wait = cache.split("void TextureCache::waitForGPUUploads()", 1)[1].split(
            "void TextureCache::addResolvedPaths", 1
        )[0]
        self.assertIn("!generateMipmaps || !MipConfiguration::waitFeedback", wait)
        self.assertLess(wait.index("queueLock.unlock()"), wait.index("MipConfiguration::waitFeedback();"))
        self.assertLess(wait.index("MipConfiguration::waitFeedback();"), wait.index("queueLock.lock()"))
        presenter = read("runtime-recomp/src/game/runtime_mipmap_loading.hpp")
        for forbidden in ("repeatLastPresent", "updateScreen(", "advanceToNext", "notifyPresentId", "fetch_add"):
            self.assertNotIn(forbidden, presenter)
        self.assertIn("std::try_to_lock", presenter)
        self.assertIn("totalPresentations.load", presenter)
        self.assertIn("swapChainFramebuffers[index]", presenter)


if __name__ == "__main__":
    unittest.main()
