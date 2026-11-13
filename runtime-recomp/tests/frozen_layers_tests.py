#!/usr/bin/env python3
"""scripts/check_frozen_layers.py: first-party files under patches/ stay as
recorded, registered third-party patches stay editable."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_frozen_layers", ROOT / "scripts/check_frozen_layers.py")
assert spec and spec.loader
check_frozen_layers = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = check_frozen_layers
spec.loader.exec_module(check_frozen_layers)


def run(root: Path, *args: str) -> tuple[int, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = check_frozen_layers.main(list(args), root)
    return status, out.getvalue() + err.getvalue()


class FrozenLayerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write(
            "patches/manifest.json", json.dumps({"dependencies": [{"patches": [{"path": "patches/dep/0001.patch"}]}]})
        )
        self.write("patches/dep/0001.patch", "third-party\n")
        self.write("patches/feature/pipeline.py", "print('layer')\n")
        self.write("patches/feature/native/ui.inl", "int layer;\n")
        self.git("init", "--quiet")
        self.git("add", "patches")
        (self.root / "architecture").mkdir()
        self.assertEqual(run(self.root, "--write")[0], 0)

    def write(self, name: str, text: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def git(self, *args: str) -> None:
        subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def test_record_lists_only_first_party_files(self):
        record = (self.root / check_frozen_layers.RECORD).read_text(encoding="utf-8")
        self.assertIn(" patches/feature/pipeline.py\n", record)
        self.assertIn(" patches/feature/native/ui.inl\n", record)
        self.assertNotIn("patches/dep/0001.patch", record)
        self.assertNotIn("patches/manifest.json", record)

    def test_untouched_layers_pass(self):
        status, output = run(self.root)
        self.assertEqual(status, 0, output)
        self.assertIn("2 files unchanged", output)

    def test_an_edited_layer_file_fails(self):
        self.write("patches/feature/pipeline.py", "print('edited')\n")
        status, output = run(self.root)
        self.assertEqual(status, 1)
        self.assertIn("patches/feature/pipeline.py: changed", output)
        self.assertIn("AGENTS.md", output)

    def test_a_new_layer_file_fails(self):
        self.write("patches/feature/native/extra.inl", "int more;\n")
        self.git("add", "patches")
        status, output = run(self.root)
        self.assertEqual(status, 1)
        self.assertIn("patches/feature/native/extra.inl: added", output)

    def test_a_deleted_layer_file_fails(self):
        (self.root / "patches/feature/native/ui.inl").unlink()
        status, output = run(self.root)
        self.assertEqual(status, 1)
        self.assertIn("patches/feature/native/ui.inl: deleted", output)

    def test_registered_third_party_patches_stay_editable(self):
        self.write("patches/dep/0001.patch", "third-party, updated\n")
        self.write("patches/dep/0002.patch", "another\n")
        manifest = {
            "dependencies": [{"patches": [{"path": "patches/dep/0001.patch"}, {"path": "patches/dep/0002.patch"}]}]
        }
        self.write("patches/manifest.json", json.dumps(manifest))
        self.git("add", "patches")
        self.assertEqual(run(self.root)[0], 0)

    def test_an_unregistered_patch_is_a_layer_file(self):
        self.write("patches/dep/0003.patch", "not in the manifest\n")
        self.git("add", "patches")
        status, output = run(self.root)
        self.assertEqual(status, 1)
        self.assertIn("patches/dep/0003.patch: added", output)

    def test_repository_record_is_current(self):
        self.assertEqual(check_frozen_layers.problems(ROOT), [])


if __name__ == "__main__":
    unittest.main()
