#!/usr/bin/env python3
"""scripts/gen_guest_symbols.py: the committed requests reproduce the shipped
v1.0.11 headers (golden hashes, updated deliberately when a symbol table pin
moves), and missing or ambiguous symbols fail."""

import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("gen_guest_symbols", ROOT / "scripts/gen_guest_symbols.py")
gen_guest_symbols = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gen_guest_symbols
SPEC.loader.exec_module(gen_guest_symbols)

GOLDEN = {
    "behaviour_site_symbols.hpp": "0e201b44",
    "revision_symbols.hpp": "e93fe4af",
    "enhancements_symbols.hpp": "99c824ec",
    "hud_layout_code_symbols.hpp": "9edfa0b2",
    "hud_layout_symbols.hpp": "fa371568",
    "audio_queue_symbols.hpp": "069b742c",
    "display_list_symbols.hpp": "833f48e9",
    "memory_symbols.hpp": "e4995134",
    "camera_symbols.hpp": "1e0b6071",
    "capture_symbols.hpp": "b465bddf",
    "credits_sky_symbols.hpp": "bddb1255",
    "detail_symbols.hpp": "3bf05f6b",
    "scenery_symbols.hpp": "152c08fe",
    "startup_symbols.hpp": "2c4c29a5",
    "title_text_symbols.hpp": "33ca82db",
    "water_symbols.hpp": "f8992cae",
    "achievement_symbols.hpp": "36b9daa3",
    "autopilot_symbols.hpp": "632fb177",
    "polish_symbols.hpp": "4eac568d",
    "render_heap_addresses.hpp": "0cbe828b",
    # v1.0.15 added the light intensity, inverse view and viewport layout symbols.
    "vehicle_symbols.hpp": "fd41fa2d",
    "legacy_world_symbols.hpp": "c72fb949",
    "resident_assets_symbols.hpp": "bdd2b7fa",
}
DECOMP = ROOT / "extern/dkr-decomp/ver/symbols"


class RepositoryRequestTests(unittest.TestCase):
    def test_requests_reproduce_the_shipped_headers(self):
        for spec in sorted(gen_guest_symbols.SPECS.glob("*.toml")):
            request = gen_guest_symbols.load(spec)
            with self.subTest(header=request.header):
                if (request.table == "decomp" or request.decomp) and not DECOMP.is_dir():
                    self.skipTest("needs extern/dkr-decomp")
                text = gen_guest_symbols.render(request, ROOT)
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest()[:8], GOLDEN[request.header])

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
                      {{ name = "D_{base + 0x10:08X}", vram = 0x{base + 0x10:08X} }},
                    ]
                    """),
                encoding="utf-8",
            )
            functions = [
                f'{{ name = "fOne", vram = 0x{base + 0x100:08X}, size = 0x40 }},',
                f'{{ name = "fTwice", vram = 0x{base + 0x200:08X}, size = 0x10 }},',
                f'{{ name = "fTwice", vram = 0x{base + 0x300:08X}, size = 0x10 }},',
            ]
            if revision == "77":
                functions.append(f'{{ name = "fOnly77", vram = 0x{base + 0x400:08X}, size = 0x20 }},')
            (kit / "functions.toml").write_text(
                "[[section]]\nfunctions = [\n" + "\n".join(functions) + "\n]\n", encoding="utf-8"
            )
            decomp = self.root / "extern/dkr-decomp/ver/symbols"
            decomp.mkdir(parents=True, exist_ok=True)
            (decomp / f"symbol_addrs.us.v{revision}.txt").write_text(
                f"gOne = 0x{base + 0xAB:08X}; // type:u32\ngDecompOnly = 0x{base + 0x20:08X}; // type:u32\n",
                encoding="utf-8",
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

    def test_per_revision_names_and_decomp_symbols(self):
        path = self.root / "request.toml"
        path.write_text(
            'schema = 1\nheader = "x.hpp"\ntable = "sdk"\nhex = "upper"\n'
            'symbols = ["Unnamed", "gDecompOnly", "gOne"]\ndecomp = ["gDecompOnly"]\n'
            "template = '''@Unnamed:77@ @Unnamed:80@ @gDecompOnly:80@ @ALL:77@'''\n"
            '[names.Unnamed]\n"77" = "D_80100010"\n"80" = "D_80200010"\n',
            encoding="utf-8",
        )
        request = gen_guest_symbols.load(path)
        self.assertEqual(
            gen_guest_symbols.render(request, self.root),
            "0x80100010U 0x80200010U 0x80200020U 0x80100010U,0x80100020U,0x801000ABU",
        )

    def sites(self, body, template="@ALL:77@|@ALL:80@"):
        path = self.root / "request.toml"
        path.write_text(
            'schema = 1\nheader = "x.hpp"\ntable = "functions"\nhex = "upper"\n'
            f"template = '''{template}'''\n" + body,
            encoding="utf-8",
        )
        return gen_guest_symbols.load(path)

    def test_function_sites_resolve_per_revision(self):
        request = self.sites(
            '[[site]]\nname = "a"\nfunction = "fOne"\noffset = 0x10\n'
            '[[site]]\nname = "b"\nfunction = "fOnly77"\noffset = 0x1C\nrevisions = ["77"]\n',
            "@a:77@ @a:80@ @b:77@ [@ALL:77@] [@ALL:80@]",
        )
        self.assertEqual(
            gen_guest_symbols.render(request, self.root),
            "0x80100110U 0x80200110U 0x8010041CU [0x80100110U,0x8010041CU] [0x80200110U]",
        )

    def test_function_sites_reject_bad_requests(self):
        site = '[[site]]\nname = "a"\nfunction = "{}"\noffset = {}\n{}'
        for function, offset, extra, why in (
            ("fMissing", "0", "", "unknown function"),
            ("fOne", "0x40", "", "offset at the end of the function"),
            ("fOne", "0x1000", "", "offset beyond the function"),
            ("fOnly77", "0", "", "declared for a revision without the function"),
            ("fTwice", "0", "", "ambiguous function"),
        ):
            with self.subTest(why), self.assertRaises(gen_guest_symbols.SymbolError):
                gen_guest_symbols.render(self.sites(site.format(function, offset, extra)), self.root)
        with self.assertRaises(gen_guest_symbols.SymbolError):  # a placeholder for a revision the site lacks
            gen_guest_symbols.render(
                self.sites(site.format("fOnly77", "0", 'revisions = ["77"]\n'), "@a:80@"), self.root
            )
        for body in (
            site.format("fOne", "2", ""),  # not word aligned
            site.format("fOne", "0", 'revisions = ["81"]\n'),
            site.format("fOne", "0", "") * 2,  # duplicate name
            "",  # nothing
        ):
            with self.subTest(body), self.assertRaises(gen_guest_symbols.SymbolError):
                self.sites(body)

    def test_names_and_decomp_must_be_listed_symbols(self):
        path = self.root / "request.toml"
        for extra in ('decomp = ["gTwo"]\n', '[names.gTwo]\n"77" = "x"\n'):
            path.write_text(
                'schema = 1\nheader = "x.hpp"\ntable = "sdk"\nhex = "upper"\nsymbols = ["gOne"]\n'
                "template = '''@gOne:77@'''\n" + extra,
                encoding="utf-8",
            )
            with self.assertRaises(gen_guest_symbols.SymbolError):
                gen_guest_symbols.load(path)

    def test_missing_ambiguous_and_unlisted_symbols_fail(self):
        for symbols, template in ((["gAbsent"], "@ALL:77@"), (["gTwice"], "@ALL:77@"), (["gOne"], "@gTwice:77@")):
            with self.subTest(symbols=symbols, template=template), self.assertRaises(gen_guest_symbols.SymbolError):
                gen_guest_symbols.render(self.request(symbols, template), self.root)

    def test_shared_dependencies_keep_the_sdk_in_the_configuring_worktree(self):
        dependencies = self.root / "shared-dependencies"
        (self.root / "extern").rename(dependencies)
        decomp = self.request(["gOne"], "@gOne:77@ @gOne:80@", table="decomp")
        self.assertEqual(gen_guest_symbols.render(decomp, self.root, dependencies), "0x801000ABU 0x802000ABU")
        with self.assertRaises(FileNotFoundError):
            gen_guest_symbols.render(decomp, self.root)
        sdk = self.request(["gOne"], "@gOne:77@ @gOne:80@")
        self.assertEqual(gen_guest_symbols.render(sdk, self.root, dependencies), "0x801000ABU 0x802000ABU")

    def test_cli_uses_the_explicit_dependency_directory(self):
        dependencies = self.root / "shared-dependencies"
        (self.root / "extern").rename(dependencies)
        self.request(["gOne"], "@gOne:77@ @gOne:80@", table="decomp")
        out = self.root / "out"
        self.assertEqual(
            gen_guest_symbols.main(
                ["--dependencies", str(dependencies), "--out", str(out), str(self.root / "request.toml")]
            ),
            0,
        )
        self.assertEqual((out / "x.hpp").read_text(), "0x801000ABU 0x802000ABU")

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
