"""Exercise shell command failures with fake compilers and no private inputs."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "posix", "POSIX entrypoints")
class DeveloperEntrypointTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.bin = self.root / "tools"
        self.bin.mkdir()
        self.trace = self.root / "trace"
        self.env = {
            **os.environ,
            "PATH": f"{self.bin}{os.pathsep}{os.environ['PATH']}",
            "DKR_TEST_TRACE": str(self.trace),
        }

    def executable(self, path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)

    def invoke(self, script, *args):
        return subprocess.run([str(self.root / script), *args], env=self.env, capture_output=True, text=True)

    def linux_fixture(self):
        shutil.copy2(ROOT / "Build-Linux.sh", self.root / "Build-Linux.sh")
        (self.root / "VERSION").write_text("1.0.5\n")
        for name in ("v77", "v80", "extern/rt64", "extern/n64-modern-runtime"):
            (self.root / name).mkdir(parents=True)
        self.env.update(
            DKR_LINUX_GENERATED_SOURCE=str(self.root / "v77"),
            DKR_LINUX_V80_GENERATED_SOURCE=str(self.root / "v80"),
            DKR_LINUX_BUILD_DIR=str(self.root / "build"),
        )
        self.executable(self.bin / "cmake", 'echo cmake >> "$DKR_TEST_TRACE"')
        self.executable(self.bin / "ctest", 'echo ctest >> "$DKR_TEST_TRACE"; exit "${DKR_TEST_EXIT:-0}"')
        self.executable(self.root / "build/bin/Release/DKR-R", 'echo runtime >> "$DKR_TEST_TRACE"')
        self.executable(self.root / "scripts/Package-Linux-AppImage.sh", 'echo package >> "$DKR_TEST_TRACE"')

    def test_linux_build_only_packages_when_requested(self):
        self.linux_fixture()
        result = self.invoke("Build-Linux.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("package", self.trace.read_text())
        result = self.invoke("Build-Linux.sh", "--package")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("package", self.trace.read_text())

    def test_linux_failed_tests_do_not_run_or_package(self):
        self.linux_fixture()
        self.env["DKR_TEST_EXIT"] = "7"
        result = self.invoke("Build-Linux.sh", "--package")
        self.assertEqual(result.returncode, 7)
        self.assertNotIn("runtime", self.trace.read_text())
        self.assertNotIn("package", self.trace.read_text())

    def test_linux_help_and_invalid_arguments_do_not_build(self):
        self.linux_fixture()
        self.assertEqual(self.invoke("Build-Linux.sh", "--help").returncode, 0)
        self.assertNotEqual(self.invoke("Build-Linux.sh", "--unknown").returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_missing_linux_libraries_print_install_command(self):
        shutil.copy2(ROOT / "Setup-Linux.sh", self.root / "Setup-Linux.sh")
        for tool in ("cmake", "ninja", "c++", "git", "python3"):
            self.executable(self.bin / tool, "exit 0")
        self.executable(self.bin / "pkg-config", "exit 1")
        result = self.invoke("Setup-Linux.sh")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libsdl2-dev", result.stderr)
        self.assertIn("sudo apt install", result.stderr)

    def test_failed_build_kit_does_not_poison_retry(self):
        (self.root / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/Package-macOS-Build-Kit.sh", self.root / "scripts/Package-macOS-Build-Kit.sh")
        for name in (
            "VERSION",
            "dependencies.lock.json",
            "docs/BUILDING.md",
            "LICENSE.md",
            "THIRD_PARTY.md",
            "packaging/MACOS-BUILD-README.md",
            "Build-macOS.sh",
            "Setup-macOS.sh",
            "Run-macOS.sh",
            ".claude/settings.json",
            ".codex/hooks.json",
        ):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("1.0.5\n")
        for name in ("assets", "patches", "runtime-recomp", "tests/fixtures/pattern-guard"):
            (self.root / name).mkdir(parents=True)
        self.executable(self.bin / "rsync", "exit 0")
        self.executable(self.bin / "tar", 'echo partial > "$4"; exit "${DKR_TEST_EXIT:-9}"')
        self.env.update(DKR_RELEASE_VERSION="1.0.5", DKR_MAC_KIT_STAGE_ROOT=str(self.root / "stage"))
        output = self.root / "dist/DKR-R-1.0.5-macOS-Build-Kit.tar.gz"
        result = self.invoke("scripts/Package-macOS-Build-Kit.sh")
        self.assertEqual(result.returncode, 9, result.stderr)
        self.assertFalse(output.exists())
        self.assertEqual(list((self.root / "stage").iterdir()), [])
        self.assertEqual(list((self.root / "dist").iterdir()), [])
        self.env["DKR_TEST_EXIT"] = "0"
        result = self.invoke("scripts/Package-macOS-Build-Kit.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(output.is_file())
        original = output.read_bytes()
        self.assertNotEqual(self.invoke("scripts/Package-macOS-Build-Kit.sh").returncode, 0)
        self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
