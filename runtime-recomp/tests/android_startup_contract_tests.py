"""Patch Pipeline contracts for the 1050022 and 1050023 startup regressions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidStartupContractTests(unittest.TestCase):
    def test_session_gate_reset_precedes_workers(self):
        relative = Path("librecomp/src/recomp.cpp")
        with tempfile.TemporaryDirectory(prefix="dkr-session-bootstrap-") as directory:
            stage = Path(directory)
            (stage / relative).parent.mkdir(parents=True)
            shutil.copyfile(ROOT / "extern/n64-modern-runtime" / relative, stage / relative)
            subprocess.run(["git", "init", "--quiet", str(stage)], check=True)
            patch = str(ROOT / "patches/performance/runtime-session-bootstrap.patch")
            subprocess.run(["git", "-C", str(stage), "apply", "--recount",
                            "--whitespace=error", patch], check=True)
            body = (stage / relative).read_text().split("void recomp::start(", 1)[1]
            self.assertLess(body.index("game_status.store(GameStatus::None)"),
                            body.index("exited.store(false)"))
            self.assertLess(body.index("current_game.reset()"), body.index("std::thread game_thread"))
            cmake = (ROOT / "runtime-recomp/cmake/PerformancePatches.cmake").read_text()
            self.assertIn('dkr_stage_patch(librecomp "${DKR_MODERN_RUNTIME_SOURCE}" session-bootstrap', cmake)
            self.assertIn('dkr_stage_patch(ultramodern "${CMAKE_BINARY_DIR}/generated/host-task-lifetime" vi-bootstrap', cmake)

    def test_no_colour_readback_probe_in_startup(self):
        helper = (ROOT / "runtime-recomp/src/game/android_transfer_qualification.hpp").read_text()
        patch = (ROOT / "patches/android/renderer-transfer-qualification.patch").read_text()
        self.assertTrue("qualify_color_target" not in helper + patch, "Unsafe colour probe must not ship")
        self.assertNotRegex(helper, r"copyTextureRegion\s*\(")
        self.assertIn("qualify_transfer(*textureCopyWorker)", patch)
        self.assertLess(helper.index("worker.execute(); worker.wait();"),
                        helper.index("readback->map"))

    def test_guard_patch_precedes_destination_texture_access(self):
        # Apply the real checked patch to a private copy, never to the submodule.
        relative = Path("src/contrib/plume/plume_vulkan.cpp")
        with tempfile.TemporaryDirectory(prefix="dkr-copy-contract-") as directory:
            stage = Path(directory)
            (stage / relative).parent.mkdir(parents=True)
            shutil.copyfile(ROOT / "extern/rt64" / relative, stage / relative)
            subprocess.run(["git", "init", "--quiet", str(stage)], check=True)
            patch = str(ROOT / "patches/android/vulkan-copy-contract.patch")
            for extra in (["--check"], []):
                subprocess.run(["git", "-C", str(stage), "apply", "--recount",
                                "--whitespace=error", *extra, patch], check=True)
            source = (stage / relative).read_text()
            body = source.split("void VulkanCommandList::copyTextureRegion(", 1)[1]
            body = body.split("void VulkanCommandList::copyBuffer(", 1)[0]
            self.assertLess(body.index("supported_texture_copy"), body.index("dstTexture->desc"))
            self.assertIn("quarantine_resource", body)
            self.assertIn("Stage::TextureCopy", body)

    def test_android_pipeline_selects_guarded_source(self):
        cmake = (ROOT / "runtime-recomp/CMakeLists.txt").read_text()
        self.assertTrue('dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-surface-recovery" android-copy-contract' in cmake,
                        "Android must compile the guard through the checked patch pipeline")
        self.assertIn('"${DKRPORT_ROOT}/patches/android/vulkan-copy-contract.patch"', cmake)


if __name__ == "__main__":
    unittest.main()
