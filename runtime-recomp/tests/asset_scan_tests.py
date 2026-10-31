"""The fast source scan must prune build outputs, while still checking sources."""

import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import scan_for_game_assets as scan


class AssetScanTests(unittest.TestCase):
    def test_excluded_trees_are_never_visited(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "source.py").write_text("print('source')\n")
            for name in ("build", "extern", "dist", "__pycache__", "services/test-tools/node_modules"):
                (root / name / "deep").mkdir(parents=True)
                (root / name / "deep/game.z64").write_bytes(b"\x80\x37\x12\x40")
            scandir = os.scandir

            def checked_scandir(path):
                self.assertFalse(set(Path(path).resolve().relative_to(root.resolve()).parts) & scan.SKIP_PARTS)
                return scandir(path)

            with patch.object(os, "scandir", side_effect=checked_scandir), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(scan.main(root), 0)

    def test_sources_with_rom_headers_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "innocent.dat").write_bytes(b"\x80\x37\x12\x40payload")
            (root / "services/online-directory").mkdir(parents=True)
            (root / "services/online-directory/source.dat").write_bytes(b"\x80\x37\x12\x40payload")
            with contextlib.redirect_stderr(io.StringIO()) as error:
                self.assertEqual(scan.main(root), 1)
            self.assertIn("Nintendo 64 ROM header", error.getvalue())
            self.assertIn("services/online-directory/source.dat", error.getvalue())

    def test_empty_or_missing_roots_do_not_pass(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stderr(io.StringIO()):
            root = Path(temporary)
            self.assertEqual(scan.main(root), 1)
            self.assertEqual(scan.main(root / "missing"), 2)


if __name__ == "__main__":
    unittest.main()
