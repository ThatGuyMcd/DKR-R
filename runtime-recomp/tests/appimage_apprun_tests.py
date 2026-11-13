"""Check when packaging/linux/AppRun exposes the bundled libwayland-client fallback."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APPRUN = ROOT / "packaging/linux/AppRun"
LIBRARY = "libwayland-client.so.0"


def elf_header(elf_class, machine):
    """Enough of an ELF header for AppRun's magic, EI_CLASS and e_machine reads."""
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[4] = elf_class
    header[5] = 1  # little-endian
    header[18:20] = machine.to_bytes(2, "little")
    return bytes(header)


X86_64 = elf_header(2, 0x3E)
I386 = elf_header(1, 0x03)
AARCH64 = elf_header(2, 0xB7)


@unittest.skipUnless(os.name == "posix", "POSIX AppImage entry point")
class AppRunWaylandFallbackTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        # AppRun resolves its own path, so compare against the resolved root.
        self.root = Path(temporary.name).resolve()
        self.appdir = self.root / "AppDir"
        self.fallback = self.appdir / "usr/lib/wayland-fallback"
        self.fallback.mkdir(parents=True)
        (self.fallback / LIBRARY).write_bytes(X86_64)
        (self.appdir / "AppRun").write_bytes(APPRUN.read_bytes())
        (self.appdir / "AppRun").chmod(0o755)
        game = self.appdir / "usr/bin/DKR-R"
        game.parent.mkdir(parents=True)
        game.write_text('#!/bin/sh\nprintf "LD=%s args=%s\\n" "${LD_LIBRARY_PATH:-}" "$*"\n')
        game.chmod(0o755)
        self.host32 = self.root / "host/usr/lib"
        self.host64 = self.root / "host/usr/lib64"
        self.host32.mkdir(parents=True)
        self.host64.mkdir(parents=True)
        self.env = {k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"}
        self.env["DKR_APPRUN_LIBRARY_DIRS"] = f"{self.host64} {self.host32}"

    def run_apprun(self):
        result = subprocess.run(
            [str(self.appdir / "AppRun"), "--self-test-pak", "x"], env=self.env, capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("args=--self-test-pak x", result.stdout)
        return result.stdout

    def assert_exposed(self, exposed):
        output = self.run_apprun()
        if exposed:
            self.assertIn(f"LD={self.fallback}", output)
        else:
            self.assertIn("LD= ", output)

    def test_no_host_library_exposes_fallback(self):
        self.assert_exposed(True)

    def test_matching_host_library_keeps_fallback_off(self):
        (self.host64 / LIBRARY).write_bytes(X86_64)
        self.assert_exposed(False)

    def test_matching_host_library_through_symlink(self):
        (self.host64 / "libwayland-client.so.0.22.0").write_bytes(X86_64)
        (self.host64 / LIBRARY).symlink_to("libwayland-client.so.0.22.0")
        self.assert_exposed(False)

    def test_only_32_bit_host_library_exposes_fallback(self):
        (self.host32 / LIBRARY).write_bytes(I386)
        self.assert_exposed(True)

    def test_wrong_machine_host_library_exposes_fallback(self):
        (self.host64 / LIBRARY).write_bytes(AARCH64)
        self.assert_exposed(True)

    def test_empty_host_file_exposes_fallback(self):
        (self.host32 / LIBRARY).touch()
        self.assert_exposed(True)

    def test_fallback_is_appended_to_existing_path(self):
        self.env["LD_LIBRARY_PATH"] = "/opt/user"
        self.assertIn(f"LD=/opt/user:{self.fallback} ", self.run_apprun())

    def test_without_fallback_directory_nothing_changes(self):
        (self.fallback / LIBRARY).unlink()
        self.fallback.rmdir()
        self.assert_exposed(False)


if __name__ == "__main__":
    unittest.main()
