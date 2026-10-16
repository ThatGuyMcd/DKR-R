import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
import json
import stat

spec = importlib.util.spec_from_file_location("update_release", Path(__file__).resolve().parents[1] / "update_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class UpdateReleaseTests(unittest.TestCase):
    def test_paths(self):
        for value in ("../save.eep", "/file", "C:/file", "a\\b", "NUL.dll", "a//b", "a. "):
            self.assertFalse(release.safe_path(value), value)
        for value in ("portable.txt", "mods/pack", "dkr-runtime-data/save.eep", "assets/rom.z64"):
            self.assertTrue(release.protected(value), value)

    def test_receipt_and_personal_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "DKR-R.exe").write_bytes(b"program")
            release.prepare_tree(root, "1.0.7", "windows-x64")
            receipt = json.loads((root / "assets/updates/install.json").read_text())
            self.assertIn("DKR-R.exe", receipt["files"])
            self.assertIn("assets/updates/install.json", receipt["files"])
            (root / "portable.txt").write_text("portable")
            with self.assertRaises(ValueError): release.prepare_tree(root, "1.0.7", "windows-x64")
            self.assertEqual((root / "portable.txt").read_text(), "portable")

    def test_zip_traversal_and_links(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "package.zip"
            for name in ("root/../save", "root/portable.txt"):
                with zipfile.ZipFile(path, "w") as z: z.writestr(name, b"bad")
                with self.assertRaises(ValueError): release.zip_table(path, "macos-arm64")
            with zipfile.ZipFile(path, "w") as z:
                link = zipfile.ZipInfo("root/Contents/link")
                link.create_system = 3
                link.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(link, b"../../../outside")
            with self.assertRaises(ValueError): release.zip_table(path, "macos-arm64")

    def test_case_collisions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "package.zip"
            with zipfile.ZipFile(path, "w") as z:
                z.writestr("root/A.txt", b"a")
                z.writestr("root/a.txt", b"b")
            with self.assertRaises(ValueError): release.zip_table(path, "macos-arm64")

    def test_windows_runtime_layouts(self):
        common = ("DKR-R.exe", "assets/updates/install.json",
                  "libexec/dkr-r/DKR-R-Updater.exe", "libexec/dkr-r/DKR-R-ModWorker.exe",
                  "libexec/dkr-r/DKR-R-InputHost.exe", "libexec/dkr-r/SDL3.dll")
        components = ("DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "package.zip"
            for prefix in ("", "libexec/dkr-r/"):
                with zipfile.ZipFile(path, "w") as z:
                    for name in (*common, *(prefix + name for name in components)):
                        z.writestr("root/" + name, b"program")
                _, table = release.zip_table(path, "windows-x64")
                self.assertIn(prefix + "DKR-R-Runtime.exe", {f["path"] for f in table})
            with zipfile.ZipFile(path, "w") as z:
                for name in (*common, "libexec/dkr-r/DKR-R-Runtime.exe", "SDL2.dll",
                             "libexec/dkr-r/dxcompiler.dll", "libexec/dkr-r/dxil.dll"):
                    z.writestr("root/" + name, b"program")
            with self.assertRaises(ValueError): release.zip_table(path, "windows-x64")

if __name__ == "__main__": unittest.main()
