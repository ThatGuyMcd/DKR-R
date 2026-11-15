"""The revision literals that moved onto generated symbol tables kept their values.

These guest addresses used to be written into the presentation and tooling
sources next to a revision check. They now come from runtime-recomp/symbols/*.toml
through scripts/gen_guest_symbols.py. RECORDED holds the values the sources had
(v77, v80), taken from the removed lines; the test asserts that every named
symbol still resolves to exactly them in the SDK tables (no ROM, no extern), that
each request lists exactly these symbols, and that no source still spells one of
the old literals.
"""

import importlib.util
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("gen_guest_symbols", ROOT / "scripts/gen_guest_symbols.py")
gen = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gen
SPEC.loader.exec_module(gen)
GAME = "runtime-recomp/src/game/"

# header -> (sources that must not spell the old literals any more, symbol -> (v77, v80))
RECORDED = {
    "camera_symbols.hpp": (
        [
            "camera_obstruction.cpp",
            "camera_rt64.cpp",
            "camera_session.cpp",
            "camera_snapshot.cpp",
            "enhanced_water_rt64.hpp",
        ],
        {
            "gCurrentLevelModel": (0x800DC918, 0x800DCE88),
            "gCameraObject": (0x8011D508, 0x8011DA88),
            "gDialogueCameraAngle": (0x8011D586, 0x8011DB06),
            "gCurrentPlayerIndex": (0x8011D55C, 0x8011DADC),
        },
    )
}


def requests():
    return {gen.load(spec).header: gen.load(spec) for spec in gen.SPECS.glob("*.toml")}


class MovedLiteralTests(unittest.TestCase):
    def test_each_request_lists_exactly_the_recorded_symbols_in_order(self):
        found = requests()
        for header, (_, recorded) in RECORDED.items():
            self.assertIn(header, found)
            self.assertEqual(list(found[header].symbols), list(recorded), header)
            self.assertEqual(found[header].table, "sdk", header)

    def test_every_symbol_resolves_to_its_old_value_in_both_revisions(self):
        tables = {revision: gen.table("sdk", revision, ROOT) for revision in gen.REVISIONS}
        for header, (_, recorded) in RECORDED.items():
            for symbol, (v77, v80) in recorded.items():
                for revision, expected in (("77", v77), ("80", v80)):
                    self.assertEqual(tables[revision].get(symbol), [expected], f"{header} {symbol} v{revision}")

    def test_the_generated_headers_carry_the_recorded_values(self):
        headers = gen.generate([gen.SPECS / f"{Path(h).stem}.toml" for h in RECORDED])
        for header, (_, recorded) in RECORDED.items():
            text = headers[header]
            for symbol, (v77, v80) in recorded.items():
                for value in (v77, v80):
                    self.assertIn(f"0x{value:08X}U", text, f"{header} {symbol}")

    def test_no_source_still_spells_an_old_literal(self):
        for header, (sources, recorded) in RECORDED.items():
            old = {value for pair in recorded.values() for value in pair}
            for source in sources:
                text = (ROOT / GAME / source).read_text(encoding="utf-8")
                spelled = {int(m.group(1), 16) for m in re.finditer(r"0[xX](80[0-9A-Fa-f]{6})U?\b", text)}
                self.assertEqual(spelled & old, set(), f"{source} still carries {header} literals")


if __name__ == "__main__":
    unittest.main()
