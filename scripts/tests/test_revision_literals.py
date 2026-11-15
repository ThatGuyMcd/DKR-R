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
DECOMP = ROOT / "extern/dkr-decomp/ver/symbols"

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
    ),
    "water_symbols.hpp": (
        ["enhanced_water_registry.hpp"],
        {
            "gWaveController": (0x80129FC8, 0x8012A588),
            "gWavePlayerCount": (0x8012A078, 0x8012A638),
            "gWaveBatch": (0x8012A07C, 0x8012A63C),
            "gWaveTriangles": (0x800E3080, 0x800E3610),
            "D_800E3090": (0x800E3090, 0x800E3620),
            "gWaveTextureHeader": (0x800E30D0, 0x800E3660),
            "gWaveModel": (0x800E30D8, 0x800E3668),
        },
    ),
    "scenery_symbols.hpp": (["local_scenery_rt64.cpp"], {"dRenderSettingsCommon": (0x800DE8E8, 0x800DEE68)}),
    "title_text_symbols.hpp": (
        ["title_text_rt64.hpp"],
        {
            "dDialogueBoxBegin": (0x800E3690, 0x800E3C20),
            "dDialogueBoxDrawModes": (0x800E36C8, 0x800E3C58),
            "gFonts": (0x8012A7E4, 0x8012ADA4),
            "gNumberOfFonts": (0x8012A7E0, 0x8012ADA0),
            "gCompactKerning": (0x8012A7F0, 0x8012ADB0),
            "gTitleCinematicText": (0x800DF83C, 0x800DFDBC),
            "gTitleCinematicTextColours": (0x800DF9F8, 0x800DFF78),
            "gCreditsMenuElements": (0x800E1B50, 0x800E20D0),
        },
    ),
    "credits_sky_symbols.hpp": (
        ["credits_sky_rt64.hpp"],
        {
            "gVoidData": (0x800DC924, 0x800DCE94),
            "gVoidVerts": (0x8011D480, 0x8011DA00),
            "gVoidCurrVerts": (0x8011D488, 0x8011DA08),
            "gVoidVertexFlip": (0x8011D4B4, 0x8011DA34),
            "gVoidVertCount": (0x8011D4B6, 0x8011DA36),
        },
    ),
    "startup_symbols.hpp": (
        ["startup_flow.cpp"],
        {
            "sMusicDelayTimer": (0x80115D38, 0x801162B8),
            "sMusicDelayLength": (0x80115D3C, 0x801162BC),
            "sMusicFadeVolume": (0x800DC650, 0x800DCBC0),
            "sBootScreenTimer": (0x80126450, 0x801269F0),
        },
    ),
    "detail_symbols.hpp": (
        ["detail_observer.inl"],
        {
            "gObjectCurrDisplayList": (0x8011AE8C, 0x8011B40C),
            "gSceneCurrentPlayerID": (0x8011B0B4, 0x8011B634),
            "gCurrentLightIntensity": (0x8011AD30, 0x8011B2B0),
        },
    ),
    "capture_symbols.hpp": (
        ["replay_probe_capture.cpp", "custom_tracks_hooks.cpp"],
        {
            "gCharacterSelectBytesDefault": (0x800DFDD0, 0x800E0350),
            "gCharacterSelectBytesDrumStick": (0x800DFE40, 0x800E03C0),
            "gCharacterSelectBytesTT": (0x800DFEC0, 0x800E0440),
            "gCharacterSelectBytesComplete": (0x800DFF40, 0x800E04C0),
            "gCurrCharacterSelectData": (0x801263CC, 0x8012696C),
            "gGameNumPlayers": (0x80123500, 0x80123A80),
        },
    ),
    "legacy_world_symbols.hpp": (
        ["runtime_legacy_mods.cpp", "records_guest.inl"],
        {
            "gCharacterIdSlots": (0x801263F0, 0x80126990),
            "gSettingsPtr": (0x80123510, 0x80123A90),
        },
    ),
    "resident_assets_symbols.hpp": (
        ["mods/legacy_resident_assets.cpp"],
        {
            "gTextureAssetTable": (0x80126320, 0x801268C0),
            "gSpriteOffsetTable": (0x80126348, 0x801268E8),
            "gObjectModelTable": (0x8011D620, 0x8011DBA0),
            "gAnimationTable": (0x8011D638, 0x8011DBB8),
            "gObjectAnimationTable": (0x8011D63C, 0x8011DBBC),
            "gSequenceTable": (0x80115CF8, 0x80116278),
            "gSeqLengthTable": (0x80115D0C, 0x8011628C),
            "gNumberOfLoadedTextures": (0x80126330, 0x801268D0),
            "gTextureCache": (0x80126328, 0x801268C8),
            "gSpriteCacheCount": (0x80126358, 0x801268F8),
            "gSpriteCache": (0x8012634C, 0x801268EC),
            "gModelCacheCount": (0x8011D62C, 0x8011DBAC),
            "gModelCache": (0x8011D624, 0x8011DBA4),
        },
    ),
    "display_list_symbols.hpp": (
        ["custom_tracks_hooks.cpp", "runtime_legacy_mods.cpp"],
        {
            "gNumF3dCmdsPerPlayer": (0x800DD3B0, 0x800DD920),
            "gPrevPlayerCount": (0x8012350C, 0x80123A8C),
            "gCurrNumF3dCmdsPerPlayer": (0x80123528, 0x80123AA8),
            "gDisplayLists": (0x801211F0, 0x80121770),
        },
    ),
    "memory_symbols.hpp": (
        ["custom_tracks_hooks.cpp", "mods/legacy_memory_safety.h"],
        {"gMemoryPools": (0x80123580, 0x80123B00)},
    ),
    # The decomp symbol list, so these need extern/dkr-decomp.
    "audio_queue_symbols.hpp": (["runtime_stubs.cpp"], {"__CSPPostNextSeqEvent": (0x800629CC, 0x80062C0C)}),
}

# header -> (symbol, offset, old v77, old v80): addresses the sources spelled that no
# symbol names. The header expresses each as a named symbol plus a byte offset.
OFFSETS = {
    # The 3D texture table, word 1 of gTextureAssetTable.
    "resident_assets_symbols.hpp": [("gTextureAssetTable", 4, 0x80126324, 0x801268C4)],
}


def requests():
    return {gen.load(spec).header: gen.load(spec) for spec in gen.SPECS.glob("*.toml")}


class MovedLiteralTests(unittest.TestCase):
    def test_each_request_lists_exactly_the_recorded_symbols_in_order(self):
        found = requests()
        for header, (_, recorded) in RECORDED.items():
            self.assertIn(header, found)
            self.assertEqual(list(found[header].symbols), list(recorded), header)
            self.assertIn(found[header].table, ("sdk", "decomp"), header)

    def test_every_symbol_resolves_to_its_old_value_in_both_revisions(self):
        found = requests()
        for header, (_, recorded) in RECORDED.items():
            kind = found[header].table
            if kind == "decomp" and not DECOMP.is_dir():
                continue  # reported by test_decomp_requests_need_extern
            tables = {revision: gen.table(kind, revision, ROOT) for revision in gen.REVISIONS}
            for symbol, (v77, v80) in recorded.items():
                for revision, expected in (("77", v77), ("80", v80)):
                    self.assertEqual(tables[revision].get(symbol), [expected], f"{header} {symbol} v{revision}")

    @unittest.skipUnless(DECOMP.is_dir(), "extern/dkr-decomp is not checked out; decomp requests skipped")
    def test_decomp_requests_need_extern(self):
        self.assertTrue(any(found.table == "decomp" for found in requests().values() if found.header in RECORDED))

    def test_the_generated_headers_carry_the_recorded_values(self):
        wanted = [h for h in RECORDED if DECOMP.is_dir() or requests()[h].table == "sdk"]
        headers = gen.generate([gen.SPECS / f"{Path(h).stem}.toml" for h in wanted])
        for header, (_, recorded) in RECORDED.items():
            if header not in headers:
                continue
            text = headers[header]
            for symbol, (v77, v80) in recorded.items():
                for value in (v77, v80):
                    self.assertIn(f"0x{value:08X}U", text, f"{header} {symbol}")

    def test_unnamed_addresses_are_a_symbol_plus_the_old_offset(self):
        tables = {revision: gen.table("sdk", revision, ROOT) for revision in gen.REVISIONS}
        headers = gen.generate([gen.SPECS / f"{Path(h).stem}.toml" for h in OFFSETS])
        for header, entries in OFFSETS.items():
            for symbol, offset, v77, v80 in entries:
                for revision, expected in (("77", v77), ("80", v80)):
                    base = tables[revision][symbol][0]
                    self.assertEqual(base + offset, expected, f"{header} {symbol} v{revision}")
                    self.assertIn(f"0x{base:08X}U + {offset}U", headers[header], f"{header} {symbol} v{revision}")

    def test_no_source_still_spells_an_old_literal(self):
        for header, (sources, recorded) in RECORDED.items():
            old = {value for pair in recorded.values() for value in pair}
            old |= {value for _, _, v77, v80 in OFFSETS.get(header, []) for value in (v77, v80)}
            for source in sources:
                text = (ROOT / GAME / source).read_text(encoding="utf-8")
                spelled = {int(m.group(1), 16) for m in re.finditer(r"0[xX](80[0-9A-Fa-f]{6})U?\b", text)}
                self.assertEqual(spelled & old, set(), f"{source} still carries {header} literals")


if __name__ == "__main__":
    unittest.main()
