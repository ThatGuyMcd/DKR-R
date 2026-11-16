"""Cross-check the per-revision guest address tables without a ROM.

The tables (AddressTable, kUsV77, kUsV80) are generated from
runtime-recomp/symbols/revision_addresses.toml into revision_symbols.hpp, which
revision_addresses.hpp includes. RECORDED holds the values the hand-written
tables had; the generated tables must equal them (with extern/dkr-decomp, since
one entry comes from the decomp list), and every recorded entry is checked
against sources that do not come from the toml:

* the SDK symbol tables (modding/reference-us-v77|v80/data.toml, in the
  repository): a named symbol must sit at the table's address in both revisions;
* the decomp symbol lists (extern/dkr-decomp, skipped without extern/): the same
  names, and the entries the SDK kit only knows by address;
* structure: field and value counts agree, addresses are inside RAM, the
  intentional aliases (two names, one address) are the only duplicates, and v80 minus v77 is the same as the
  SDK symbols around it.

Entries are verified at the strongest level available; test_coverage prints
how many reached which level.
"""

import bisect
import importlib.util
from pathlib import Path
import re
import sys
import unittest

import tomllib

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "runtime-recomp/symbols/revision_addresses.toml"
SDK = {rev: ROOT / f"modding/reference-us-v{rev}/data.toml" for rev in ("77", "80")}
DECOMP = {rev: ROOT / f"extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{rev}.txt" for rev in ("77", "80")}
RAM_START, RAM_END = 0x80000400, 0x80800000

# The symbol each entry is supposed to be, chosen from what the field is for.
# The SDK table must put that symbol at the header's address in both revisions.
EXPECTED_SYMBOL = {
    "EntrypointStackTop": "gCameras",
    "MusicPlayer": "gMusicPlayer",
    "MusicBaseVolume": "gMusicBaseVolume",
    "BlockMusicChange": "gBlockMusicChange",
    "MusicNextSequence": "gMusicNextSeqID",
    "ScreenViewports": "gScreenViewports",
    "OrthoMatrix": "gOrthoMatrixF",
    "IsInRace": "gIsInRace",
    "LevelLoadTimer": "gLevelLoadTimer",
    "CurrentRngSeed": "gCurrentRNGSeed",
    "PreviousRngSeed": "gPrevRNGSeed",
    "LogicUpdateRate": "sLogicUpdateRate",
    "TracksMode": "gIsInTracksMode",
    "NumberOfReadyPlayers": "gNumberOfReadyPlayers",
    "NumberOfActivePlayers": "gNumberOfActivePlayers",
    "CurrentMenuId": "gCurrentMenuId",
    "ActiveMagicCodes": "gActiveMagicCodes",
    "UnlockedMagicCodes": "gUnlockedMagicCodes",
    "TrophyRaceWorldId": "gTrophyRaceWorldId",
    "WaveModel": "gWaveModel",
    "WaveHeightIndices": "gWaveHeightIndices",
    "WaveGenCount": "gWaveGenCount",
    "WaveGenList": "gWaveGenList",
    "WaveGenObjects": "gWaveGenObjs",
    "MusicTempo": "sMusicTempo",
    "DynamicMusicChannelMask": "gDynamicMusicChannelMask",
    "ObjectCount": "gObjectCount",
    "ObjectList": "gObjPtrList",
    "ObjectListStart": "gObjectListStart",
    "ObjectMapSpawnList": "gObjectMapSpawnList",
    "ObjectMapSize": "gObjectMapSize",
    "RaceEndTimer": "gRaceEndTimer",
    "RaceEndStage": "gRaceEndStage",
    "PathUpdateOff": "gPathUpdateOff",
    "RaceFinishTriggered": "gRaceFinishTriggered",
    "NumberOfFinishedRacers": "gNumFinishedRacers",
    "ObjectCurrentMatrix": "gObjectCurrMatrix",
    "Racers": "gRacers",
    "RacersByPort": "gRacersByPort",
    "NumberOfRacers": "gNumRacers",
    "TrackDisplayList": "gTrackDL",
    "SceneActiveCamera": "gSceneActiveCamera",
    "ShadowHeapFlip": "gShadowHeapFlip",
    "ShadowHeapTriangles": "gShadowHeapTris",
    "ShadowHeapVertices": "gShadowHeapVerts",
    "ShadowHeapData": "gShadowHeapData",
    "RaceStartTimer": "gRaceStartTimer",
    "Cameras": "gCameras",
    "PlayerIdMap": "sPlayerID",
    "ViewportLayout": "gViewportLayout",
    "ActiveCameraId": "gActiveCameraID",
    "CurrentCameraFov": "gCurCamFOV",
    "CutsceneCameraActive": "gCutsceneCameraActive",
    "ViewProjectionMatrix": "gViewProjMatrixF",
    "CurrentMapId": "gMapId",
    "CurrentLevelHeader": "gCurrentLevelHeader",
    "SpTaskNumber": "gSPTaskNum",
    "IsPaused": "gIsPaused",
    "PostRaceViewport": "gPostRaceViewPort",
    "TextureCache": "gTextureCache",
    "NumberOfLoadedTextures": "gNumberOfLoadedTextures",
    "MenuSelectedCharacter": "gMenuSelectedCharacter",
    "MenuCurrentCharacter": "gMenuCurrentCharacter",
    "ActivePlayersArray": "gActivePlayersArray",
    "CharacterSelectStatus": "gCharselectStatus",
    "PlayersCharacterArray": "gPlayersCharacterArray",
    "CharacterIdSlots": "gCharacterIdSlots",
    "PlayerSelectVehicle": "gPlayerSelectVehicle",
    "MenuButtons": "gMenuButtons",
    "NumberOfGameplayPlayers": "gNumActivePlayers",
    "CurrentHud": "gCurrentHud",
    "PlayerHud": "gPlayerHud",
    "HudNumPlayers": "gHUDNumPlayers",
    "HudDisplayList": "gHudDL",
    "HudColour": "gHudColour",
    "RumblePresent": "gRumblePresent",
    "TitleDemoIndex": "gTitleDemoIndex",
    "TitleRevealTimer": "gTitleRevealTimer",
    "WaveController": "gWaveController",
    "NumberOfLevelSegments": "gNumberOfLevelSegments",
    "WavePowerBase": "gWavePowerBase",
    "WaveMagnitude": "gWaveMagnitude",
    "WavePowerDivisor": "gWavePowerDivisor",
    "IsInTracksMenu": "gIsInTracksMenu",
    "GameMode": "gGameMode",
    "MenuStage": "gMenuStage",
    "MenuDelay": "gMenuDelay",
    "PostraceFinishState": "gPostraceFinishState",
    "WaveTexUVMaskX": "gWaveTexUVMaskX",
    "WaveTexUVMaskY": "gWaveTexUVMaskY",
    "CurrentSequence": "gCurrentSequenceID",
    "SequenceTable": "gSequenceTable",
    "SequenceLengths": "gSeqLengthTable",
    "SequenceSoundTable": "gSeqSoundTable",
}
# The SDK kit names these only by address (D_<address>), so all it can say is
# that a declared data symbol starts there in both revisions.
ADDRESS_ONLY = {
    "FrustumReference",
    "WaveSelectionMap",
    "VoidLateralX",
    "VoidLateralZ",
    "VoidCentreX",
    "VoidCentreZ",
    "RaceStartProgress",
}
# Entries with no SDK symbol; the decomp list names them (extern only).
DECOMP_ONLY = {"AspMainTextStart": "aspMainTextStart"}
# Fields that deliberately share an address. A new duplicate is an error.
INTENTIONAL_ALIASES = {frozenset({"EntrypointStackTop", "Cameras"})}

# The hand-written tables revision_addresses.hpp held before they were generated,
# in field order. The generated tables must equal them entry for entry.
RECORDED = {
    "77": [
        0x80120AC0,
        0x800D7600,
        0x800DC630,
        0x800DC638,
        0x800DC648,
        0x800DC65C,
        0x800DC8AC,
        0x800DD064,
        0x800DD2B8,
        0x800DD31C,
        0x800DD394,
        0x800DD434,
        0x800DD438,
        0x800DD404,
        0x800DF4B8,
        0x800DF480,
        0x800DF4BC,
        0x800DF470,
        0x800DFD98,
        0x800DFD9C,
        0x800E0FE8,
        0x800E30D4,
        0x800E30D8,
        0x800E3044,
        0x800E3188,
        0x800E3190,
        0x800E3194,
        0x80115D30,
        0x80115F7C,
        0x8011AE5C,
        0x8011AE58,
        0x8011AE60,
        0x8011AE98,
        0x8011AEA0,
        0x8011AD4E,
        0x8011AD50,
        0x8011ADAC,
        0x8011ADB4,
        0x8011ADC0,
        0x8011AE90,
        0x8011AEE4,
        0x8011AEEC,
        0x8011AEF0,
        0x8011B0A0,
        0x8011B0B0,
        0x8011B0C8,
        0x8011D320,
        0x8011D338,
        0x8011D350,
        0x8011D4A0,
        0x8011D4A4,
        0x8011D4AC,
        0x8011D4B0,
        0x8011D540,
        0x8011D544,
        0x80120AC0,
        0x80121150,
        0x80120CE0,
        0x80120CE4,
        0x80120D10,
        0x80120D14,
        0x80120F20,
        0x80121164,
        0x80121168,
        0x801234E8,
        0x80123515,
        0x80123516,
        0x80126328,
        0x80126330,
        0x801263B4,
        0x801263C0,
        0x801263D4,
        0x801263DC,
        0x801263E8,
        0x801263F0,
        0x801269C0,
        0x801267D8,
        0x80126D37,
        0x80126CDC,
        0x80126CE0,
        0x80126D0C,
        0x80126CFC,
        0x800E2834,
        0x801241E5,
        0x80126864,
        0x8012686C,
        0x80129FC8,
        0x8012A0E0,
        0x8012A720,
        0x8012A724,
        0x8012A728,
        0x800E097C,
        0x801234EC,
        0x801263E0,
        0x800DF47C,
        0x80126C28,
        0x8012A094,
        0x8012A098,
        0x80115D04,
        0x80115CF8,
        0x80115D0C,
        0x80115D1C,
    ],
    "80": [
        0x80121040,
        0x800D7B60,
        0x800DCBA0,
        0x800DCBA8,
        0x800DCBB8,
        0x800DCBCC,
        0x800DCE1C,
        0x800DD5D4,
        0x800DD828,
        0x800DD88C,
        0x800DD904,
        0x800DD9A4,
        0x800DD9A8,
        0x800DD974,
        0x800DFA38,
        0x800DFA00,
        0x800DFA3C,
        0x800DF9F0,
        0x800E0318,
        0x800E031C,
        0x800E1568,
        0x800E3664,
        0x800E3668,
        0x800E35D4,
        0x800E3718,
        0x800E3720,
        0x800E3724,
        0x801162B0,
        0x801164FC,
        0x8011B3DC,
        0x8011B3D8,
        0x8011B3E0,
        0x8011B418,
        0x8011B420,
        0x8011B2CE,
        0x8011B2D0,
        0x8011B32C,
        0x8011B334,
        0x8011B340,
        0x8011B410,
        0x8011B464,
        0x8011B46C,
        0x8011B470,
        0x8011B620,
        0x8011B630,
        0x8011B648,
        0x8011D8A0,
        0x8011D8B8,
        0x8011D8D0,
        0x8011DA20,
        0x8011DA24,
        0x8011DA2C,
        0x8011DA30,
        0x8011DAC0,
        0x8011DAC4,
        0x80121040,
        0x801216D0,
        0x80121260,
        0x80121264,
        0x80121290,
        0x80121294,
        0x801214A0,
        0x801216E4,
        0x801216E8,
        0x80123A68,
        0x80123A95,
        0x80123A96,
        0x801268C8,
        0x801268D0,
        0x80126954,
        0x80126960,
        0x80126974,
        0x8012697C,
        0x80126988,
        0x80126990,
        0x80126F80,
        0x80126D78,
        0x801272F7,
        0x8012729C,
        0x801272A0,
        0x801272CC,
        0x801272BC,
        0x800E2DB4,
        0x80124765,
        0x80126E04,
        0x80126E0C,
        0x8012A588,
        0x8012A6A0,
        0x8012ACE0,
        0x8012ACE4,
        0x8012ACE8,
        0x800E0EFC,
        0x80123A6C,
        0x80126980,
        0x800DF9FC,
        0x801271E8,
        0x8012A654,
        0x8012A658,
        0x80116284,
        0x80116278,
        0x8011628C,
        0x8011629C,
    ],
}


def parse_header():
    spec = tomllib.loads(SPEC.read_text())
    template = spec["template"]
    struct = template[template.index("struct AddressTable {") : template.index("inline constexpr AddressTable kUsV77")]
    fields = re.findall(r"^\s+std::uint32_t (\w+);", struct, re.M)
    entries = {
        name: re.findall(
            r"^\s+\.(\w+) = ", template[template.index(f"AddressTable {name}{{") :].split("};", 1)[0], re.M
        )
        for name in ("kUsV77", "kUsV80")
    }
    return fields, entries


def sdk_symbols(rev):
    sections = tomllib.loads(SDK[rev].read_text())["section"]
    by_address, by_name = {}, {}
    for section in sections:
        for symbol in section["symbols"]:
            by_address.setdefault(symbol["vram"], []).append(symbol["name"])
            by_name.setdefault(symbol["name"], []).append(symbol["vram"])
    return by_address, by_name


def decomp_symbols(rev):
    symbols = {}
    for match in re.finditer(r"^(\w+) = (0x[0-9A-Fa-f]+);(.*)$", DECOMP[rev].read_text(), re.M):
        size = re.search(r"size:(0x[0-9A-Fa-f]+)", match.group(3))
        symbols[match.group(1)] = (int(match.group(2), 16), int(size.group(1), 16) if size else None)
    return symbols


FIELDS, ENTRIES = parse_header()
TABLES = RECORDED


class StructureTests(unittest.TestCase):
    def test_every_list_has_the_same_fields_in_the_same_order(self):
        self.assertEqual(len(FIELDS), len(set(FIELDS)), "duplicate field name")
        for rev, values in TABLES.items():
            self.assertEqual(len(values), len(FIELDS), f"v{rev}: {len(values)} values for {len(FIELDS)} fields")

    def test_the_template_initialises_every_field_in_declaration_order(self):
        for name, entries in ENTRIES.items():
            self.assertEqual(entries, FIELDS, name)

    def test_addresses_are_inside_ram(self):
        for rev, values in TABLES.items():
            for name, address in zip(FIELDS, values):
                self.assertTrue(RAM_START <= address < RAM_END, f"v{rev} {name}={address:#x} is outside RAM")

    def test_only_intentional_aliases_share_an_address(self):
        for rev, values in TABLES.items():
            owners = {}
            for name, address in zip(FIELDS, values):
                owners.setdefault(address, set()).add(name)
            shared = {frozenset(names) for names in owners.values() if len(names) > 1}
            self.assertEqual(shared, INTENTIONAL_ALIASES, f"v{rev}: unexpected shared addresses")

    def test_both_revisions_share_the_same_pairs(self):
        a, b = TABLES["77"], TABLES["80"]
        for i, name in enumerate(FIELDS):
            for j in range(i + 1, len(FIELDS)):
                self.assertEqual(a[i] == a[j], b[i] == b[j], f"{name} / {FIELDS[j]} alias in one revision only")

    def test_deltas_are_forward_and_four_byte_steps(self):
        for name, a, b in zip(FIELDS, TABLES["77"], TABLES["80"]):
            self.assertGreater(b, a, f"{name}: v80 is not above v77")
            self.assertEqual((b - a) % 4, 0, f"{name}: v80 - v77 = {b - a:#x} is not word aligned")

    def test_delta_matches_the_symbols_around_each_entry(self):
        self.assertEqual(delta_mismatches(TABLES["77"], TABLES["80"]), [])

    def test_the_checks_catch_an_insertion_and_a_swap(self):
        # A value inserted early shifts every later entry by one position; a
        # swap exchanges two. Both must be reported, or the checks prove nothing.
        a, b = list(TABLES["77"]), list(TABLES["80"])
        shifted = b[:10] + [b[9]] + b[10:-1]
        self.assertTrue(delta_mismatches(a, shifted))
        self.assertTrue(named_mismatches("80", shifted))
        swapped = list(b)
        swapped[20], swapped[21] = swapped[21], swapped[20]
        self.assertTrue(named_mismatches("80", swapped))
        self.assertTrue(delta_mismatches(a, swapped))
        bumped = list(b)
        bumped[30] += 4
        self.assertTrue(named_mismatches("80", bumped))


def delta_mismatches(table77, table80):
    """Entries whose v80 - v77 differs from the SDK symbols on both sides.

    Between two SDK symbols that exist in both revisions and moved by the same
    amount, nothing else can have moved differently.
    """
    by77, _ = sdk_symbols("77")
    by80, _ = sdk_symbols("80")
    names77 = {name: address for address, names in by77.items() for name in names}
    names80 = {name: address for address, names in by80.items() for name in names}
    pairs = sorted((names77[n], names80[n] - names77[n]) for n in names77.keys() & names80.keys())
    starts = [address for address, _ in pairs]
    problems = []
    for name, a, b in zip(FIELDS, table77, table80):
        at = bisect.bisect_right(starts, a)
        if at == 0 or at == len(pairs):
            continue
        before, after = pairs[at - 1][1], pairs[at][1]
        if before == after and b - a != before:
            problems.append(f"{name}: moved {b - a:#x}, its neighbours moved {before:#x}")
    return problems


def named_mismatches(rev, table):
    """Named entries whose address is not where the SDK table puts the symbol."""
    _, names = sdk_symbols(rev)
    values = dict(zip(FIELDS, table))
    return [
        f"v{rev} {field}: SDK has {symbol} at {names.get(symbol)}, header has {values[field]:#x}"
        for field, symbol in EXPECTED_SYMBOL.items()
        if names.get(symbol) != [values[field]]
    ]


@unittest.skipUnless(
    all(path.is_file() for path in DECOMP.values()), "extern/dkr-decomp is not checked out; generated tables skipped"
)
class GeneratedTests(unittest.TestCase):
    def test_generated_tables_equal_the_recorded_hand_written_ones(self):
        spec = importlib.util.spec_from_file_location("gen_guest_symbols", ROOT / "scripts/gen_guest_symbols.py")
        gen = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = gen
        spec.loader.exec_module(gen)
        text = gen.generate([SPEC])["revision_symbols.hpp"]
        for rev, name in (("77", "kUsV77"), ("80", "kUsV80")):
            body = text.split(f"AddressTable {name}{{", 1)[1].split("};", 1)[0]
            generated = {
                field: sum(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)U", expression))
                for field, expression in re.findall(r"\.(\w+) = ([^,]+),", body)
            }
            self.assertEqual(list(generated), FIELDS)
            self.assertEqual([generated[f] for f in FIELDS], RECORDED[rev], f"v{rev}")


class SdkTests(unittest.TestCase):
    def test_named_entries_sit_at_their_symbol_in_both_revisions(self):
        for field in EXPECTED_SYMBOL:
            self.assertIn(field, FIELDS, f"{field} is not in the header any more")
        for rev in SDK:
            self.assertEqual(named_mismatches(rev, TABLES[rev]), [])

    def test_address_only_entries_are_declared_data_symbols_in_both_revisions(self):
        for rev in SDK:
            by_address, _ = sdk_symbols(rev)
            for field, address in zip(FIELDS, TABLES[rev]):
                if field in ADDRESS_ONLY:
                    self.assertIn(address, by_address, f"v{rev} {field}={address:#x} is not a declared symbol")

    def test_entry_stack_top_is_the_end_of_the_entry_thread_stack(self):
        # gCameras starts where the 0x1000 byte entrypointThreadStack ends
        # (src/hasm/entrypoint.s in the decomp), so the initial sp equals it.
        for rev in SDK:
            _, names = sdk_symbols(rev)
            top = dict(zip(FIELDS, TABLES[rev]))["EntrypointStackTop"]
            self.assertEqual(top, names["entrypointThreadStack"][0] + 0x1000, f"v{rev}")

    def test_coverage(self):
        covered = set(EXPECTED_SYMBOL) | set(ADDRESS_ONLY)
        uncovered = [field for field in FIELDS if field not in covered]
        self.assertEqual(sorted(uncovered), sorted(DECOMP_ONLY), "an entry has no independent source")
        print(
            f"\nrevision_addresses: {len(FIELDS)} entries x 2 revisions; "
            f"{len(EXPECTED_SYMBOL)} named in the SDK tables, {len(ADDRESS_ONLY)} by address only, "
            f"{len(DECOMP_ONLY)} decomp only",
            file=sys.stderr,
        )


@unittest.skipUnless(
    all(path.is_file() for path in DECOMP.values()), "extern/dkr-decomp is not checked out; decomp checks skipped"
)
class DecompTests(unittest.TestCase):
    def test_named_entries_agree_with_the_decomp_symbol_lists(self):
        for rev in DECOMP:
            symbols = decomp_symbols(rev)
            values = dict(zip(FIELDS, TABLES[rev]))
            for field, symbol in {**EXPECTED_SYMBOL, **DECOMP_ONLY}.items():
                self.assertIn(symbol, symbols, f"v{rev}: decomp has no {symbol}")
                self.assertEqual(symbols[symbol][0], values[field], f"v{rev} {field}: decomp {symbol} differs")

    def test_address_only_entries_are_symbols_in_the_decomp_lists(self):
        for rev in DECOMP:
            symbols = decomp_symbols(rev)
            starts = {address for address, _ in symbols.values()}
            for field, address in zip(FIELDS, TABLES[rev]):
                if field in ADDRESS_ONLY:
                    self.assertIn(address, starts, f"v{rev} {field}={address:#x} starts no decomp symbol")


if __name__ == "__main__":
    unittest.main()
