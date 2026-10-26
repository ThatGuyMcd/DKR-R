"""Contract IDs ignore text newline conventions, not semantic or binary changes."""

import pathlib
import shutil
import subprocess
import tempfile
import unittest

HELPER = pathlib.Path(__file__).resolve().parents[1] / "cmake/OwnedContentIdentity.cmake"


@unittest.skipUnless(shutil.which("cmake"), "needs CMake (python3 scripts/check.py --suite host-tools)")
class OwnedContentIdentityTests(unittest.TestCase):
    def identity(self, root, files, *, outside=False):
        root = pathlib.Path(root)
        authored, generated = root / "authored", root / "generated"
        authored.mkdir(exist_ok=True)
        generated.mkdir(exist_ok=True)
        inputs = []
        for name, content in files:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            inputs.append(f'"{path.as_posix()}"')
        script = root / "check.cmake"
        output = root / "identity.txt"
        script.write_text(
            f'include("{HELPER.as_posix()}")\n'
            f'dkr_owned_content_identity(value "{authored.as_posix()}" '
            f'"{generated.as_posix()}" {" ".join(inputs)})\n'
            f'file(WRITE "{output.as_posix()}" "${{value}}")\n',
            encoding="utf-8",
        )
        result = subprocess.run([shutil.which("cmake"), "-P", str(script)], capture_output=True, text=True)
        if outside:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("outside its checked roots", result.stderr)
            return
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return output.read_text(encoding="utf-8")

    def test_text_newlines_and_relocated_roots_are_portable(self):
        with tempfile.TemporaryDirectory() as temp:
            roots = [pathlib.Path(temp) / name for name in ("windows layout", "linux-layout")]
            for root in roots:
                root.mkdir()
            files = [
                (f"generated/sample{ext}", b"int x = 1;\n// checked input\n") for ext in (".c", ".cpp", ".h", ".hpp")
            ]
            files += [
                ("generated/manifest.json", b'{"revision":77,\n"full_scenes":true}\n'),
                ("authored/src/native.cpp", b"int native_value = 2;\n"),
            ]
            other = [(name, data.replace(b"\n", b"\r\n")) for name, data in reversed(files)]
            self.assertEqual(self.identity(roots[0], files), self.identity(roots[1], other))

    def test_semantic_change_changes_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            before = self.identity(temp, [("generated/guest.c", b"int x = 1;\n")])
            after = self.identity(temp, [("generated/guest.c", b"int x = 2;\n")])
            self.assertNotEqual(before, after)

    def test_manifest_change_changes_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            before = self.identity(temp, [("generated/manifest.json", b'{"revision":77}\n')])
            after = self.identity(temp, [("generated/manifest.json", b'{"revision":80}\n')])
            self.assertNotEqual(before, after)

    def test_binary_inputs_remain_byte_exact(self):
        with tempfile.TemporaryDirectory() as temp:
            before = self.identity(temp, [("generated/state.bin", b"\x00\r\n\xff")])
            after = self.identity(temp, [("generated/state.bin", b"\x00\n\xff")])
            self.assertNotEqual(before, after)

    def test_outside_root_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            self.identity(temp, [("unchecked.c", b"int x;\n")], outside=True)


if __name__ == "__main__":
    unittest.main()
