"""Verify Android platform, NDK, shared runtime, and page-alignment gates."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('verify_android_compile', Path(__file__).parents[1] / 'verify_android_compile.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def description():
    note = (28).to_bytes(4, 'little') + b'r29'.ljust(64, b'\0') + b'14206865'.ljust(64, b'\0')
    return ('Class: ELF64\nMachine: AArch64\nType: DYN (Shared object)\n'
            ' LOAD 0x0000 0x0000 0x0000 0x20 0x20 R E 0x4000\n'
            '(NEEDED) Shared library: [libc++_shared.so]\n'
            '(NEEDED) Shared library: [libSDL2.so]\n'
            'Android 0x84 NT_ANDROID_TYPE_IDENT\n description data: ' + note.hex(' ') + '\n')


class AndroidObjectTests(unittest.TestCase):
    def test_valid_library_records_device_contract(self):
        result = verify.library_contract(description(), True)
        self.assertEqual(result['api'], 28)
        self.assertEqual(result['ndk_build'], '14206865')
        self.assertEqual(result['load_alignments'], [16384])

    def test_host_architecture_old_api_missing_note_alignment_and_static_cpp_rejected(self):
        good = description()
        for bad in (good.replace('AArch64', 'X86-64'), good.replace('ELF64', 'ELF32'),
                    good.replace('DYN', 'REL'), good.replace('0x4000', '0x1000'),
                    good.replace(' LOAD ', ' NOTE '), good.replace('libc++_shared.so', 'libc++.a'),
                    good.replace('1c 00 00 00', '1b 00 00 00'),
                    good.replace('NT_ANDROID_TYPE_IDENT', 'NO_ANDROID_NOTE'),
                    good.replace('72 32 39', '72 32 38')):
            with self.subTest(description=bad), self.assertRaises(ValueError):
                verify.library_contract(bad, True)

    def test_missing_native_objects_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'compiled platform object'):
                verify.verify(Path(directory), Path('unused-readelf'))


if __name__ == '__main__':
    unittest.main()
