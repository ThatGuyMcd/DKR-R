"""The first-party sources the prepare generators pin still match their pins.

scripts/generate_replay_probe.py and scripts/character_portrait_pipeline.py
refuse to run when a pinned runtime source has changed. They only run with the
ROMs, so without this an edit to a pinned file passes CI and breaks the next
prepare. Dependency sources (extern/) are pinned through patches/manifest.json
and are not read here.
"""

import hashlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import character_portrait_pipeline as portrait  # noqa: E402
import generate_replay_probe as probe  # noqa: E402

# Second reviewed content for a path, not a table of its own.
ALTERNATES = "REVIEWED_MAIN_MERGE_HASH"


def pin_tables():
    return {
        name: table
        for name, table in vars(probe).items()
        if isinstance(table, dict) and table and all(isinstance(key, str) and "/" in key for key in table)
    }


def matches(data, pin):
    # Pins are of LF or of CRLF bytes; a checkout may hold either.
    lf = data.replace(b"\r\n", b"\n")
    return pin in {hashlib.sha256(candidate).hexdigest() for candidate in (data, lf, lf.replace(b"\n", b"\r\n"))}


class GeneratorPins(unittest.TestCase):
    def assert_pinned(self, path, *pins):
        data = (ROOT / path).read_bytes()
        self.assertTrue(
            any(matches(data, pin) for pin in pins if pin),
            f"{path} no longer matches its pin. Review the change against what the generator does with the file, "
            "then update the pin and prepare from the ROMs to confirm the generator accepts it.",
        )

    def test_owned_engine_generator_pins(self):
        tables = pin_tables()
        alternates = tables.pop(ALTERNATES)
        checked = 0
        for table in tables.values():
            for path, pin in table.items():
                if path.startswith("extern/"):
                    continue
                with self.subTest(path=path):
                    self.assert_pinned(path, pin, alternates.get(path))
                checked += 1
        self.assertGreater(checked, 30)

    def test_portrait_pipeline_pins(self):
        for name, pin in portrait.NATIVE_FILES.items():
            with self.subTest(path=name):
                self.assert_pinned(portrait.MODS.relative_to(portrait.ROOT) / name, pin)


if __name__ == "__main__":
    unittest.main()
