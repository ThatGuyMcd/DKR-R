"""Check the Linux AppDir closure check against fake ELF files and a stub readelf."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/Check-AppDir-Closure.sh"

# Prints a readelf -d style NEEDED line for every "NEEDED <soname>" line in the
# fake ELF, so the check runs anywhere without real ELF files or binutils.
STUB_READELF = """#!/bin/sh
sed -n 's/^NEEDED \\(.*\\)$/ 0x0000000000000001 (NEEDED)             Shared library: [\\1]/p' "$2"
"""


@unittest.skipUnless(os.name == "posix", "POSIX packaging script")
class AppDirClosureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.appdir = self.root / "DKR-R.AppDir"
        readelf = self.root / "readelf"
        readelf.write_text(STUB_READELF)
        readelf.chmod(0o755)
        self.env = {**os.environ, "READELF": str(readelf)}
        self.env.pop("DKR_APPDIR_EXTRA_HOST_LIBS", None)

    def elf(self, relative, *needed):
        path = self.appdir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\x7fELF\n" + "".join(f"NEEDED {name}\n" for name in needed).encode())

    def check(self, *args):
        return subprocess.run(
            ["bash", str(SCRIPT), str(self.appdir), *args], env=self.env, capture_output=True, text=True
        )

    def issue_45_appdir(self):
        self.elf("usr/bin/DKR-R", "libSDL2-2.0.so.0", "libwayland-client.so.0", "libc.so.6")
        self.elf("usr/lib/libSDL2-2.0.so.0", "libwayland-client.so.0", "libwayland-egl.so.1", "libX11.so.6")
        self.elf("usr/lib/libwayland-egl.so.1", "libwayland-client.so.0")
        self.elf("usr/lib/libffi.so.8", "libc.so.6")
        (self.appdir / "usr/share/doc").mkdir(parents=True)
        (self.appdir / "usr/bin/assets").mkdir(parents=True)
        (self.appdir / "usr/bin/assets/logo.png").write_bytes(b"\x89PNG")

    def test_half_bundled_wayland_fails(self):
        self.issue_45_appdir()
        result = self.check()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("usr/bin/DKR-R needs libwayland-client.so.0", result.stderr)
        self.assertIn("usr/lib/libSDL2-2.0.so.0 needs libwayland-client.so.0", result.stderr)
        self.assertIn("3 unbundled dependencies across 4 ELF files", result.stderr)

    def test_fallback_directory_completes_the_closure(self):
        self.issue_45_appdir()
        self.elf("usr/lib/wayland-fallback/libwayland-client.so.0", "libffi.so.8", "libc.so.6")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("5 ELF files inspected", result.stdout)

    def test_unbundled_dependency_of_fallback_fails(self):
        self.issue_45_appdir()
        self.elf("usr/lib/wayland-fallback/libwayland-client.so.0", "libffi.so.7")
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("needs libffi.so.7", result.stderr)

    def test_host_resolved_executable_and_extra_allowlist(self):
        self.issue_45_appdir()
        self.elf("usr/lib/wayland-fallback/libwayland-client.so.0", "libffi.so.8")
        self.elf("usr/libexec/dkr-r/DKR-R-FilePicker", "libgtk-3.so.0")
        self.elf("usr/libexec/dkr-r/DKR-R-Updater", "libcurl.so.4")
        self.assertEqual(self.check().returncode, 1)
        self.env["DKR_APPDIR_EXTRA_HOST_LIBS"] = "libcurl.so.4"
        result = self.check("--host-resolved", "usr/libexec/dkr-r/DKR-R-FilePicker")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_symlinked_soname_counts_as_bundled(self):
        self.elf("usr/bin/DKR-R", "libfoo.so.1")
        self.elf("usr/lib/libfoo.so.1.2.3")
        (self.appdir / "usr/lib/libfoo.so.1").symlink_to("libfoo.so.1.2.3")
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_empty_appdir_is_rejected(self):
        (self.appdir / "usr/bin").mkdir(parents=True)
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("found no ELF files", result.stderr)


if __name__ == "__main__":
    unittest.main()
