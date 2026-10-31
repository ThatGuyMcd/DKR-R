#!/usr/bin/env python3
"""The fold checkpoint accepts layout changes but detects source changes."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/fold"))
import current_main  # noqa: E402


class CheckpointTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.name = "runtime-recomp/src/game/example.cpp"
        self.path = self.root / self.name
        self.path.parent.mkdir(parents=True)
        source = b'#include "owner.hpp"\nint value = 3;\n'
        self.path.write_bytes(source)
        self.files = {self.name: current_main.digest(source)}

    def test_layout_is_allowed(self):
        self.path.write_bytes(b'#include "owner.hpp"\nint value=3; // layout only\n')
        self.assertEqual(current_main.compare(self.root, self.files), [])

    def test_include_changes_are_detected(self):
        self.path.write_bytes(b'#include "another_owner.hpp"\nint value = 3;\n')
        self.assertEqual(len(current_main.compare(self.root, self.files)), 1)

    def test_missing_source_is_detected(self):
        self.path.unlink()
        self.assertEqual(current_main.compare(self.root, self.files), [f"{self.name}: missing"])

    def test_an_empty_checkpoint_is_rejected(self):
        with self.assertRaises(ValueError):
            current_main.compare(self.root, {})

    def test_a_source_path_cannot_escape_the_checkpoint(self):
        with self.assertRaises(ValueError):
            current_main.compare(self.root, {"runtime-recomp/../outside.cpp": "unused"})


if __name__ == "__main__":
    unittest.main()
