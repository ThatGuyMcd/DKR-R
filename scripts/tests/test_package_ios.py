"""Packaging regressions runnable without Xcode or a compiled game."""
import json
import plistlib
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_ios as package


class PackageIOSTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name)
        self.app = self.build / "DKR-R.app"
        self.app.mkdir()
        self.binary = self.app / "DKR-R"
        self.binary.write_bytes(b"test binary")
        self.binary.chmod(0o755)
        (self.app / "Info.plist").write_bytes(plistlib.dumps({}))

    def test_release_and_rc_versions(self):
        for version, expected in (("1.0.5", "1.0.5"), ("1.0.5-rc2", "1.0.5"), ("9999.99.99", "9999.99.99")):
            with self.subTest(version=version):
                self.assertEqual(package.release_number(version), expected)
                self.assertEqual(package.bundle_version(version), expected)

    def test_invalid_versions_are_rejected(self):
        for version in ("1.0", "1.0.5.1", "01.0.5", "0.1.0", "10000.0.0", "1.100.0", "1.0.100", "1.0.5-rc0", "1.0.5/other", "1.0.5junk"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                package.release_number(version)

    def test_device_platform_and_minimum_are_exact(self):
        for platform, minimum, accepted in (("IOS", "18.0", True), ("2", "18.0.0", True), ("IOSSIMULATOR", "18.0", False), ("20", "18.0", False), ("IOS", "18.01", False), ("MACOS", "18.0", False)):
            output = f"    platform {platform}\n       minos {minimum}\n"
            with self.subTest(platform=platform, minimum=minimum), patch.object(package, "run", side_effect=["Mach-O 64-bit executable arm64", output]):
                if accepted:
                    package.check_binary(self.app)
                else:
                    with self.assertRaises(RuntimeError):
                        package.check_binary(self.app)

    def test_mixed_device_and_simulator_is_rejected(self):
        with patch.object(package, "run", side_effect=["Mach-O arm64", "platform IOS\nminos 18.0\nplatform IOSSIMULATOR\nminos 18.0\n"]):
            with self.assertRaises(RuntimeError):
                package.check_binary(self.app)

    def test_rom_in_repository_excluded_directory_is_rejected(self):
        for directory in ("runtime", "build", "extern", ".git"):
            with self.subTest(directory=directory):
                target = self.app / directory / "game.z64"
                target.parent.mkdir()
                target.write_bytes(b"\x80\x37\x12\x40rom")
                with self.assertRaisesRegex(RuntimeError, "game-data"):
                    package.scan_app(self.app)
                target.unlink()

    def test_zip_rom_is_rejected(self):
        with zipfile.ZipFile(self.app / "game.zip", "w") as archive:
            archive.writestr("game.z64", b"\x80\x37\x12\x40rom")
        with self.assertRaisesRegex(RuntimeError, "archive"):
            package.scan_app(self.app)

    def test_renamed_rom_is_rejected(self):
        (self.app / "innocent.dat").write_bytes(b"\x80\x37\x12\x40rom")
        with self.assertRaisesRegex(RuntimeError, "ROM header"):
            package.scan_app(self.app)

    def test_unknown_large_data_is_rejected_but_executable_is_allowed(self):
        with self.binary.open("wb") as stream:
            stream.truncate(package.asset_scan.MAX_SOURCE_SIZE + 1)
        package.scan_app(self.app)
        with (self.app / "unknown.dat").open("wb") as stream:
            stream.truncate(package.asset_scan.MAX_SOURCE_SIZE + 1)
        with self.assertRaisesRegex(RuntimeError, "Unexplained"):
            package.scan_app(self.app)

    def test_symlink_files_and_directories_are_rejected(self):
        for target in (self.binary, self.build):
            link = self.app / "link"
            link.symlink_to(target)
            with self.subTest(target=target):
                with self.assertRaisesRegex(RuntimeError, "symbolic"):
                    package.scan_app(self.app)
                with self.assertRaisesRegex(RuntimeError, "symbolic"):
                    package.write_ipa(self.app, self.build / "test.ipa")
            link.unlink()

    def test_configured_and_staged_versions_must_agree(self):
        metadata = self.build / "dkr-release.json"
        metadata.write_text(json.dumps({"version": "1.0.5-rc2"}))
        package.require_metadata(self.app, self.build, "1.0.5-rc2")
        with self.assertRaisesRegex(RuntimeError, "differs"):
            package.require_metadata(self.app, self.build, "1.0.5")
        (self.app / "dkr-release.json").write_text(json.dumps({"version": "1.0.4"}))
        with self.assertRaisesRegex(RuntimeError, "differs"):
            package.require_metadata(self.app, self.build, "1.0.5-rc2")

    def test_missing_and_malformed_metadata_fail(self):
        with self.assertRaises(RuntimeError):
            package.require_metadata(self.app, self.build, "1.0.5")
        for content in ("not json", "[]"):
            (self.build / "dkr-release.json").write_text(content)
            with self.assertRaises(RuntimeError):
                package.require_metadata(self.app, self.build, "1.0.5")

    def test_entitlement_inspection_fails_closed(self):
        for result in (RuntimeError("inspection failed"), "garbage", plistlib.dumps({"get-task-allow": False}).decode(), plistlib.dumps({"unexpected": True}).decode()):
            with self.subTest(result=result), patch.object(package, "run", side_effect=["", "", result]):
                with self.assertRaises(RuntimeError):
                    package.sign_app(self.app)
        with patch.object(package, "run", side_effect=["", "", plistlib.dumps({}).decode()]):
            package.sign_app(self.app)

    def test_archive_layout_and_executable_mode(self):
        destination = self.build / "test.ipa"
        package.scan_app(self.app)
        package.write_ipa(self.app, destination)
        with zipfile.ZipFile(destination) as archive:
            self.assertEqual(set(archive.namelist()), {"Payload/DKR-R.app/DKR-R", "Payload/DKR-R.app/Info.plist"})
            self.assertEqual(archive.getinfo("Payload/DKR-R.app/DKR-R").external_attr >> 16 & 0o777, 0o755)
        self.assertFalse(destination.with_suffix(".ipa.partial").exists())


if __name__ == "__main__":
    unittest.main()
