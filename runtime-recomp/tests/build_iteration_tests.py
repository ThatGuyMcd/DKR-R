"""Exercise CMake reconfiguration using isolated synthetic sources and patches."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "runtime-recomp/cmake"


@unittest.skipUnless(shutil.which("ninja"), "Incremental build fixture requires Ninja")
class BuildIterationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dkrr-build-iteration-")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "source"
        self.build = Path(self.temp.name) / "build"
        shutil.copytree(ROOT / "runtime-recomp/tests/cmake/build_iteration", self.source)
        (self.source / "fixture.c").write_text("int answer(void) { return 1; }\n")
        for name, old, new in (("first", 1, 2), ("second", 2, 3)):
            (self.source / f"{name}.patch").write_text(
                "diff --git a/fixture.c b/fixture.c\n--- a/fixture.c\n+++ b/fixture.c\n"
                "@@ -1 +1 @@\n"
                f"-int answer(void) {{ return {old}; }}\n+int answer(void) {{ return {new}; }}\n")
        (self.source / "payload").mkdir()
        (self.source / "payload/functions.c").write_text("void recomp_entrypoint(void) {}\n")
        self.configure = ["cmake", "-S", str(self.source), "-B", str(self.build),
                          "-G", "Ninja", f"-DDKR_CMAKE_DIR={CMAKE}",
                          f"-DDKR_DEPENDENCIES_DIR={self.source / 'shared-extern'}"]

    def run_ok(self, *args):
        result = subprocess.run(args, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_reconfigure_preserves_stages_and_tracks_map_inputs(self):
        self.run_ok(*self.configure)
        self.run_ok("cmake", "--build", str(self.build))
        staged = self.build / "generated/second/fixture.c"
        symbol_map = self.build / "map.hpp"
        self.assertIn("return 3", staged.read_text())
        self.assertIn("return 1", (self.source / "fixture.c").read_text())
        # Backdate outputs so timestamp equality cannot pass due to clock granularity.
        for path in (staged, symbol_map):
            os.utime(path, (1000000000, 1000000000))
        times = [p.stat().st_mtime_ns for p in (staged, symbol_map)]
        self.run_ok(*self.configure)
        self.assertEqual(times, [p.stat().st_mtime_ns for p in (staged, symbol_map)])
        self.assertIn("no work to do", self.run_ok("cmake", "--build", str(self.build)))
        # A new TU is discovered by CONFIGURE_DEPENDS; edits use explicit dependencies.
        extra = self.source / "payload/extra.cpp"
        extra.write_text("void extra_one(void) {}\n")
        self.run_ok("cmake", "--build", str(self.build))
        self.assertIn("#define extra_one alternate_extra_one", symbol_map.read_text())
        extra.write_text("void extra_two(void) {}\n")
        self.run_ok("cmake", "--build", str(self.build))
        self.assertIn("#define extra_two alternate_extra_two", symbol_map.read_text())
        self.assertNotIn("extra_one", symbol_map.read_text())
        extra.unlink()
        self.run_ok("cmake", "--build", str(self.build))
        self.assertNotIn("extra_two", symbol_map.read_text())

    def test_changed_patch_rebuilds_and_invalid_patch_preserves_published_source(self):
        self.run_ok(*self.configure)
        self.run_ok("cmake", "--build", str(self.build))
        patch = self.source / "second.patch"
        patch.write_text(patch.read_text().replace("return 3", "return 4"))
        self.run_ok("cmake", "--build", str(self.build))
        staged = self.build / "generated/second/fixture.c"
        self.assertIn("return 4", staged.read_text())
        previous = staged.stat().st_mtime_ns
        patch.write_text(patch.read_text().replace("return 2", "return 99"))
        result = subprocess.run(self.configure, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("second check failed", result.stderr)
        self.assertIn("return 4", staged.read_text())
        self.assertEqual(previous, staged.stat().st_mtime_ns)

    def test_preflight_checks_only_consumed_headers_and_requires_test_python(self):
        dependencies = self.source / "minimal-extern"
        for relative in ("n64-modern-runtime/CMakeLists.txt",
                         "n64-modern-runtime/N64Recomp/include/recomp.h",
                         "n64-modern-runtime/N64Recomp/lib/fmt/CMakeLists.txt",
                         "gekkonet/GekkoLib/CMakeLists.txt", "monocypher/src/monocypher.c"):
            path = dependencies / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        command = ["cmake", f"-DDKR_DEPENDENCIES_DIR={dependencies}",
                   "-DDKR_NETPLAY_WEBRTC=OFF", "-DDKR_RUNTIME_BUILD_RT64=OFF"]
        script = ["-P", str(CMAKE / "DependencyPreflight.cmake")]
        self.run_ok(*command, "-DBUILD_TESTING=OFF", *script)
        host = subprocess.run([*command, "-DBUILD_TESTING=ON", *script],
                              capture_output=True, text=True, timeout=60)
        self.assertNotEqual(host.returncode, 0)
        self.assertIn("hlsl++.h", host.stderr)
        android = subprocess.run([*command, "-DBUILD_TESTING=ON", "-DANDROID=ON",
                                  "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE", *script],
                                 capture_output=True, text=True, timeout=60)
        self.assertNotEqual(android.returncode, 0)
        self.assertIn("Python3", android.stderr)
        self.assertIn("REQUIRED", android.stderr)
        self.assertNotIn("hlsl++.h", android.stderr)

    def test_source_root_isolation_and_legacy_override_diagnostic(self):
        self.run_ok(*self.configure)
        source_root, dependencies = (self.build / "roots.txt").read_text().splitlines()
        # CMake canonicalizes Windows short-name aliases and macOS symlinks.
        self.assertEqual(Path(source_root).resolve(), ROOT.resolve())
        self.assertEqual(Path(dependencies).resolve(), (self.source / "shared-extern").resolve())
        result = subprocess.run([*self.configure, f"-DDKRPORT_ROOT={self.source}"],
                                capture_output=True, text=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("DKRPORT_ROOT can no longer redirect", result.stderr)
        self.assertIn("DKR_DEPENDENCIES_DIR", result.stderr)


if __name__ == "__main__":
    unittest.main()
