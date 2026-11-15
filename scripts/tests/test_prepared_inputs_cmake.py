"""PreparedInputs.cmake: cached defaults follow the payload they came with."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).parents[2] / "runtime-recomp/cmake/PreparedInputs.cmake"
OWNED = "DKR_EXPERIMENTAL_OWNED_GENERATED"
CAMERA = "DKR_CAMERA_KERNEL_V77"
PAYLOAD = "DKR_GENERATED_SOURCE_V77"


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class PreparedInputs(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.source, self.build = self.root / "source", self.root / "build-tree"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(prepared_inputs NONE)\n"
            f'set(DKRPORT_ROOT "{self.root.as_posix()}")\nset(DKR_RUNTIME_BUILD_GENERATED ON)\n'
            f'include("{MODULE.as_posix()}")\n'
        )
        self.prepared = self.bundle("build/prepared")

    def bundle(self, name):
        bundle = self.root / name
        (bundle / "generated-v80").mkdir(parents=True)
        (bundle / "prepared.json").write_text("{}\n")
        return bundle

    def configure(self, *definitions):
        done = subprocess.run(
            ["cmake", "-S", str(self.source), "-B", str(self.build), *[f"-D{d}" for d in definitions]],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(done.returncode, 0, done.stderr)
        cache = {}
        for line in (self.build / "CMakeCache.txt").read_text().splitlines():
            name, separator, value = line.partition("=")
            if separator and not line.startswith(("#", "//")):
                cache[name.split(":")[0]] = value
        return cache, done.stderr

    def test_defaults_come_from_the_prepared_directory(self):
        cache, _ = self.configure()
        self.assertEqual(cache[PAYLOAD], f"{self.prepared.as_posix()}/generated-v77")
        self.assertEqual(cache[OWNED], f"{self.prepared.as_posix()}/owned-v77")

    def test_naming_a_payload_later_drops_the_prepared_engines(self):
        self.configure()
        cache, warning = self.configure(f"{PAYLOAD}={self.root.as_posix()}/elsewhere")
        self.assertIn("none of the prepared", warning)
        self.assertEqual(cache[OWNED], "")
        self.assertEqual(cache[CAMERA], "")
        self.assertEqual(cache["DKR_EXPERIMENTAL_OWNED_GENERATED_V80"], "")

    def test_clearing_the_payload_brings_the_bundle_back(self):
        self.configure()
        self.configure(f"{PAYLOAD}={self.root.as_posix()}/elsewhere")
        cache, _ = self.configure(f"{PAYLOAD}=")
        self.assertEqual(cache[OWNED], f"{self.prepared.as_posix()}/owned-v77")

    def test_a_callers_own_engine_survives_a_foreign_payload(self):
        self.configure()
        mine = f"{self.root.as_posix()}/my-owned"
        cache, _ = self.configure(f"{PAYLOAD}={self.root.as_posix()}/elsewhere", f"{OWNED}={mine}")
        self.assertEqual(cache[OWNED], mine)
        self.assertEqual(cache[CAMERA], "")

    def test_another_prepared_directory_replaces_every_default(self):
        self.configure()
        other = self.bundle("build/prepared-other")
        cache, warning = self.configure(f"DKR_PREPARED_DIR={other.as_posix()}")
        self.assertNotIn("none of the prepared", warning)
        self.assertEqual(cache[PAYLOAD], f"{other.as_posix()}/generated-v77")
        self.assertEqual(cache[OWNED], f"{other.as_posix()}/owned-v77")


if __name__ == "__main__":
    unittest.main()
