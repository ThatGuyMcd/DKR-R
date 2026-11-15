"""Cross-check runtime-recomp/src/game/revision_addresses.hpp without a ROM.

The header holds two positional address tables, one per ROM revision. Nothing
in the build proves an entry is right: an insertion or a transposition still
compiles and reads the wrong guest memory. This test parses the header and
checks every entry against sources that do not come from it:

* the SDK symbol tables (modding/reference-us-v77|v80/data.toml, in the
  repository): a named symbol must sit at the table's address in both revisions;
* the decomp symbol lists (extern/dkr-decomp, skipped without extern/): the same
  names, and the entries the SDK kit only knows by address;
* structure: field, value, alias and select() counts agree, addresses are inside
  RAM, aliases are the only duplicates, and v80 minus v77 is the same as the
  SDK symbols around it.

Entries are verified at the strongest level available; test_coverage prints
how many reached which level.
"""

import bisect
from pathlib import Path
import re
import sys
import unittest

import tomllib

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "runtime-recomp/src/game/revision_addresses.hpp"
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


def parse_header():
    text = HEADER.read_text()
    struct = text[text.index("struct AddressTable {") : text.index("inline constexpr AddressTable kUsV77")]
    fields = re.findall(r"^\s+std::uint32_t (\w+);", struct, re.M)

    def table(name):
        start = text.index(f"inline constexpr AddressTable {name}{{")
        body = re.sub(r"//[^\n]*", "", text[start : text.index("};", start)])
        return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{8})U", body)]

    aliases = re.findall(r"^inline std::uint32_t (\w+) = kUsV77\.(\w+);", text, re.M)
    selected = re.findall(r"^    (\w+) = table\.(\w+);", text, re.M)
    return fields, {"77": table("kUsV77"), "80": table("kUsV80")}, aliases, selected


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


FIELDS, TABLES, ALIASES, SELECTED = parse_header()


class StructureTests(unittest.TestCase):
    def test_every_list_has_the_same_fields_in_the_same_order(self):
        self.assertEqual(len(FIELDS), len(set(FIELDS)), "duplicate field name")
        for rev, values in TABLES.items():
            self.assertEqual(len(values), len(FIELDS), f"v{rev}: {len(values)} values for {len(FIELDS)} fields")
        # Declaration order of the aliases and of select()'s assignments does not
        # matter (WaveTexUVMask* come first there); each field must appear once.
        self.assertEqual(sorted(name for name, _ in ALIASES), sorted(FIELDS), "mutable aliases differ from the struct")
        self.assertTrue(all(name == source for name, source in ALIASES))
        self.assertEqual(sorted(name for name, _ in SELECTED), sorted(FIELDS), "select() misses or repeats a field")
        self.assertTrue(all(name == source for name, source in SELECTED))

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
