"""Reject host/simulator output masquerading as device compilation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('verify_ios_compile', Path(__file__).parents[1] / 'verify_ios_compile.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class DeviceObjectTests(unittest.TestCase):
    def test_device_object_requires_architecture_platform_and_deployment_target(self):
        for architecture, platform, minimum, accepted in (
                ('arm64', 'IOS', '18.0', True), ('x86_64', 'IOS', '18.0', False),
                ('arm64', 'IOSSIMULATOR', '18.0', False), ('arm64', 'MACOS', '18.0', False),
                ('arm64', 'IOS', '17.0', False)):
            with self.subTest(architecture=architecture, platform=platform, minimum=minimum):
                output = [architecture, f' platform {platform}\n minos {minimum}\n sdk 26.0\n']
                with mock.patch.object(verify.subprocess, 'check_output', side_effect=output):
                    if accepted:
                        self.assertEqual(verify.inspect_object(Path('device.o'))['platform'], 'IOS')
                    else:
                        with self.assertRaises(ValueError):
                            verify.inspect_object(Path('device.o'))

    def test_missing_platform_objects_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'expected exactly one compiled platform object'):
                verify.verify(Path(directory))


if __name__ == '__main__':
    unittest.main()
