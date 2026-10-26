#!/usr/bin/env python3
"""scripts/gen_guest_symbols.py: the committed requests reproduce the shipped
v1.0.11 headers (golden hashes, updated deliberately when a symbol table pin
moves), and missing or ambiguous symbols fail."""

import hashlib
import importlib.util
from pathlib import Path
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("gen_guest_symbols", ROOT / "scripts/gen_guest_symbols.py")
gen_guest_symbols = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen_guest_symbols)

GOLDEN = {
    "achievement_symbols.hpp": "36b9daa3",
    "polish_symbols.hpp": "4eac568d",
    "render_heap_addresses.hpp": "0cbe828b",
    "vehicle_symbols.hpp": "a3c8f2a1",
}
DECOMP = ROOT / "extern/dkr-decomp/ver/symbols"


class RepositoryRequestTests(unittest.TestCase):
    def test_requests_reproduce_the_shipped_headers(self):
        for spec in sorted(gen_guest_symbols.SPECS.glob("*.toml")):
            request = gen_guest_symbols.load(spec)
            with self.subTest(header=request.header):
                if request.table == "decomp" and not DECOMP.is_dir():
                    self.skipTest("needs extern/dkr-decomp")
                text = gen_guest_symbols.render(request, ROOT)
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest()[:8], GOLDEN[request.header])

    @unittest.skipUnless(DECOMP.is_dir(), "needs extern/dkr-decomp")
    def test_inline_title_and_credits_tables_match_the_decomp(self):
        # title_text_rt64.hpp and credits_sky_rt64.hpp carry their tables
        # inline, among hand-written code; they must still be what the pinned
        # symbol table says.
        def table(*names):
            rows = []
            for revision in gen_guest_symbols.REVISIONS:
                found = gen_guest_symbols.table("decomp", revision, ROOT)
                rows.append(",".join(f"0x{found[name][0]:08X}U" for name in names))
            return rows

        game = ROOT / "runtime-recomp/src/game"
        title = (game / "title_text_rt64.hpp").read_text(encoding="utf-8")
        rows = table(
            "dDialogueBoxBegin",
            "dDialogueBoxDrawModes",
            "gFonts",
            "gNumberOfFonts",
            "gCompactKerning",
            "gTitleCinematicText",
            "gTitleCinematicTextColours",
        )
        self.assertIn("    {" + rows[0] + "},\n    {" + rows[1] + "}\n", title)
        self.assertIn("credit_rows_{{" + ",".join(table("gCreditsMenuElements")) + "}}", title)
        credits = (game / "credits_sky_rt64.hpp").read_text(encoding="utf-8")
        rows = table("gVoidData", "gVoidVerts", "gVoidCurrVerts", "gVoidVertexFlip", "gVoidVertCount")
        self.assertIn("    {" + rows[0] + "},\n    {" + rows[1] + "}\n", credits)

    def test_every_golden_header_has_a_request(self):
        headers = {gen_guest_symbols.load(spec).header for spec in gen_guest_symbols.SPECS.glob("*.toml")}
        self.assertEqual(headers, set(GOLDEN))


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        for revision, base in (("77", 0x80100000), ("80", 0x80200000)):
            kit = self.root / f"modding/reference-us-v{revision}"
            kit.mkdir(parents=True)
            (kit / "data.toml").write_text(
                textwrap.dedent(f"""\
                    [[section]]
                    symbols = [
                      {{ name = "gOne", vram = 0x{base + 0xAB:08X} }},
                      {{ name = "gTwice", vram = 0x{base:08X} }},
                      {{ name = "gTwice", vram = 0x{base + 4:08X} }},
                    ]
                    """),
                encoding="utf-8",
            )
            decomp = self.root / "extern/dkr-decomp/ver/symbols"
            decomp.mkdir(parents=True, exist_ok=True)
            (decomp / f"symbol_addrs.us.v{revision}.txt").write_text(
                f"gOne = 0x{base + 0xAB:08X}; // type:u32\n", encoding="utf-8"
            )

    def tearDown(self):
        self.scratch.cleanup()

    def request(self, symbols, template, table="sdk", hexcase="upper"):
        path = self.root / "request.toml"
        listed = ", ".join(f'"{name}"' for name in symbols)
        path.write_text(
            f'schema = 1\nheader = "x.hpp"\ntable = "{table}"\nhex = "{hexcase}"\nsymbols = [{listed}]\n'
            f"template = '''{template}'''\n",
            encoding="utf-8",
        )
        return gen_guest_symbols.load(path)

    def test_placeholders_and_formats(self):
        request = self.request(["gOne"], "{@ALL:77@} @gOne:80@\n", hexcase="lower")
        self.assertEqual(gen_guest_symbols.render(request, self.root), "{0x801000abU} 0x802000abU\n")
        request = self.request(["gOne"], "@gOne:77@\n", table="decomp")
        self.assertEqual(gen_guest_symbols.render(request, self.root), "0x801000ABU\n")

    def test_missing_ambiguous_and_unlisted_symbols_fail(self):
        for symbols, template in ((["gAbsent"], "@ALL:77@"), (["gTwice"], "@ALL:77@"), (["gOne"], "@gTwice:77@")):
            with self.subTest(symbols=symbols, template=template), self.assertRaises(gen_guest_symbols.SymbolError):
                gen_guest_symbols.render(self.request(symbols, template), self.root)

    def test_check_mode_reports_a_stale_header(self):
        out = self.root / "out"
        spec = gen_guest_symbols.SPECS / "render_heap_addresses.toml"
        self.assertEqual(gen_guest_symbols.main(["--out", str(out), str(spec)]), 0)
        self.assertEqual(gen_guest_symbols.main(["--out", str(out), "--check", str(spec)]), 0)
        (out / "render_heap_addresses.hpp").write_text("stale\n", encoding="utf-8")
        self.assertEqual(gen_guest_symbols.main(["--out", str(out), "--check", str(spec)]), 1)

    def test_headers_without_a_request_are_removed(self):
        out = self.root / "out"
        out.mkdir()
        (out / "retired_symbols.hpp").write_text("#pragma once\n", encoding="utf-8")
        spec = gen_guest_symbols.SPECS / "render_heap_addresses.toml"
        self.assertEqual(gen_guest_symbols.main(["--out", str(out), "--check", str(spec)]), 1)
        self.assertEqual(gen_guest_symbols.main(["--out", str(out), str(spec)]), 0)
        self.assertEqual(sorted(path.name for path in out.iterdir()), ["render_heap_addresses.hpp"])


if __name__ == "__main__":
    unittest.main()
