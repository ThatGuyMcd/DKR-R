"""The SDK book covers additive imports and keeps its plain-text chapters."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("sdk_guide", ROOT / "scripts/render_magic_code_guide.py")
GUIDE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUIDE)


class GuideTests(unittest.TestCase):
    def fixture(self, root, documented=True):
        headers = root / "include/dkr"
        headers.mkdir(parents=True)
        (headers / "magic_codes.h").write_text("DKR_IMPORT int dkr_old(void);\n")
        (headers / "game.h").write_text("static inline int dkr2_scene(void);\n")
        (headers / "driving.h").write_text("DKR_IMPORT int dkr2_driving_input(void);\n")
        source = "Book\n====\nFIRST\n-----\ndkr_old dkr2_scene\n\n## Driving\n"
        if documented:
            source += "dkr2_driving_input\n"
        (root / "MODDERS-GUIDE.txt").write_text(source)

    def test_plain_text_chapters_and_both_api_generations(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            report = GUIDE.render(root, root / "guide.html")
            page = (root / "guide.html").read_text(encoding="utf-8")
            self.assertEqual(report["chapters"], 2)
            self.assertEqual(report["public_functions"], 3)
            self.assertIn("SDK 2.0 / API 1 and 2", page)
            self.assertIn("driving.h", page)
            self.assertNotIn("v1.0.7 beta", page)
            self.assertFalse(report["executed_mod_code"])

    def test_undocumented_additive_import_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root, documented=False)
            with self.assertRaisesRegex(ValueError, "dkr2_driving_input"):
                GUIDE.render(root, root / "guide.html")
            self.assertFalse((root / "guide.html").exists())


if __name__ == "__main__":
    unittest.main()
