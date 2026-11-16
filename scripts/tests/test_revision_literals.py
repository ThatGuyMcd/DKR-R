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
    "enhancements_symbols.hpp": (
        ["runtime_enhancements.cpp"],
        {
            "gSceneCurrentPlayerID": (0x8011B0B4, 0x8011B634),
            "gScenePlayerViewports": (0x8011D37C, 0x8011D8FC),
        },
    ),
    "hud_layout_symbols.hpp": (
        ["runtime_hud_layout.cpp"],
        {
            "gViewportLayout": (0x80120CE0, 0x80121260),
            "gPrevPlayerCount": (0x8012350C, 0x80123A8C),
            "gCurrDisplayList": (0x801211F8, 0x80121778),
            "gGameCurrTriList": (0x80121228, 0x801217A8),
            "gHudOffsetX": (0x80126D24, 0x801272E4),
            "gDialogueBoxBackground": (0x8012A7E8, 0x8012ADA8),
            "gTimeTrialEnabled": (0x8011AEF4, 0x8011B474),
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
# Call sites the HUD layout recognises by return address: decomp function symbol
# -> {revision: [(offset spelled in the source, old absolute address)]}.
RECORDED_CALL_SITES = {
    "hud_main_time_trial": {
        "77": [
            (0xDD4, 0x800A3550),
            (0xE10, 0x800A358C),
            (0xE4C, 0x800A35C8),
            (0xEA4, 0x800A3620),
            (0xEDC, 0x800A3658),
            (0xF14, 0x800A3690),
        ],
        "80": [
            (0xDDC, 0x800A3A98),
            (0xE18, 0x800A3AD4),
            (0xE54, 0x800A3B10),
            (0xEAC, 0x800A3B68),
            (0xEE4, 0x800A3BA0),
            (0xF1C, 0x800A3BD8),
        ],
    },
    "hud_finish_position": {
        "77": [(0x174, 0x800A608C), (0x250, 0x800A6168), (0x32C, 0x800A6244)],
        "80": [(0x174, 0x800A65D4), (0x250, 0x800A66B0), (0x32C, 0x800A678C)],
    },
}


# Patch sites of mods/legacy_behaviour.cpp: (site name, function, offset in it, old v77 address, old v80
# address or None for a site only v77 has). The function tables must resolve each to exactly these.
RECORDED_SITES = [
    ("track_spawn_objects_30", "track_spawn_objects", 0x30, 0x8000C928, 0x8000C928),
    ("track_spawn_objects_34", "track_spawn_objects", 0x34, 0x8000C92C, 0x8000C92C),
    ("track_spawn_objects_7c", "track_spawn_objects", 0x7C, 0x8000C974, 0x8000C974),
    ("trackmenu_set_records_d0", "trackmenu_set_records", 0xD0, 0x80082988, 0x80082DD8),
    ("trackmenu_set_records_e8", "trackmenu_set_records", 0xE8, 0x800829A0, 0x80082DF0),
    ("menu_track_select_init_5f8", "menu_track_select_init", 0x5F8, 0x8008ED98, 0x8008F250),
    ("menu_track_select_init_650", "menu_track_select_init", 0x650, 0x8008EDF0, 0x8008F2A8),
    ("menu_track_select_init_680", "menu_track_select_init", 0x680, 0x8008EE20, 0x8008F2D8),
    ("menu_track_select_init_6b4", "menu_track_select_init", 0x6B4, 0x8008EE54, 0x8008F30C),
    ("trackmenu_render_names_1dc", "trackmenu_render_names", 0x1DC, 0x800900F8, 0x800905B0),
    ("trackmenu_render_names_210", "trackmenu_render_names", 0x210, 0x8009012C, 0x800905E4),
    ("trackmenu_setup_render_a38", "trackmenu_setup_render", 0xA38, 0x80091968, None),
    ("trackmenu_setup_render_1040", "trackmenu_setup_render", 0x1040, 0x80091F70, None),
    ("trackmenu_setup_render_1098", "trackmenu_setup_render", 0x1098, 0x80091FC8, None),
    ("func_80092188_4b0", "func_80092188", 0x4B0, 0x80092638, None),
    ("is_tt_unlocked_4", "is_tt_unlocked", 0x4, 0x8009ECBC, 0x8009F200),
    ("is_drumstick_unlocked_4", "is_drumstick_unlocked", 0x4, 0x8009ECD4, 0x8009F218),
]


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

    def test_behaviour_patch_sites_are_function_plus_offset_and_resolve_to_the_old_addresses(self):
        request = requests()["behaviour_site_symbols.hpp"]
        self.assertEqual(request.table, "functions")
        self.assertEqual(
            [(site.name, site.function, site.offset, site.revisions) for site in request.sites],
            [
                (name, function, offset, ("77", "80") if old80 is not None else ("77",))
                for name, function, offset, _old77, old80 in RECORDED_SITES
            ],
        )
        functions = {revision: gen.function_table(revision, ROOT) for revision in gen.REVISIONS}
        for name, function, offset, old77, old80 in RECORDED_SITES:
            for revision, old in (("77", old77), ("80", old80)):
                if old is None:
                    continue
                found = functions[revision][function]
                self.assertEqual(len(found), 1, f"{function} v{revision}")
                self.assertEqual(found[0][0] + offset, old, f"{name} v{revision}")
        source = (ROOT / GAME / "mods/legacy_behaviour.cpp").read_text(encoding="utf-8").lower()
        for name, _function, _offset, old77, old80 in RECORDED_SITES:
            for old in (old77, old80):
                if old is not None:
                    self.assertNotIn(f"0x{old:08x}", source, name)

    def test_hud_call_sites_are_function_plus_offset_and_resolve_to_the_old_addresses(self):
        request = requests()["hud_layout_code_symbols.hpp"]
        self.assertEqual(request.table, "functions")
        by_name = {site.name: site for site in request.sites}
        functions = {revision: gen.function_table(revision, ROOT) for revision in gen.REVISIONS}
        source = (ROOT / GAME / "runtime_hud_layout.cpp").read_text(encoding="utf-8")
        checked = 0
        for symbol, revisions in RECORDED_CALL_SITES.items():
            for revision, sites in revisions.items():
                base = functions[revision][symbol]
                self.assertEqual(len(base), 1, symbol)
                prefix = "stopwatch" if symbol == "hud_main_time_trial" else "finish"
                for index, (offset, absolute) in enumerate(sites):
                    self.assertEqual(base[0][0] + offset, absolute, f"{symbol}+{offset:#x} v{revision}")
                    # Per-revision offsets get a per-revision site; shared ones are one site for both.
                    site = by_name.get(f"{prefix}{index}_{revision}") or by_name[f"{prefix}{index}"]
                    self.assertEqual((site.function, site.offset), (symbol, offset))
                    self.assertIn(revision, site.revisions)
                    self.assertNotIn(f"0x{absolute:08X}", source.upper().replace("0X", "0x"), f"{absolute:#x}")
                    checked += 1
        self.assertEqual(checked, 18)
        self.assertEqual(len(request.sites), 6 + 6 + 3)
        # The offsets live only in the request now, not in the source.
        for offset in (0xDD4, 0xDDC, 0x174, 0x32C):
            self.assertNotIn(f"0x{offset:X}", source)


if __name__ == "__main__":
    unittest.main()
