# Legacy mod compatibility — v1.0.5 Beta 7

## What changed

The importer previously coupled admission to a few exact reconstructed-ROM
hashes. This update admits bounded, structurally validated native asset graphs
without that package whitelist. Exact hashes are still used as optional intent
metadata for known packs (names, supported vehicles and collateral deletions),
not as the general admission condition. Unknown executable code is never run.

- Asset directories can be relocated: Mario and Dixie use a different ROM
  offset. Discovery requires a unique validated native 50-section directory in
  a bounded scan; malformed and ambiguous directories fail closed.
- Racer detection compares primary model geometry and decoded texture pixels,
  not exporter padding, recompression or unrelated vertex colour changes.
- GoldenEye BSP validation follows `traverse_segments_bsp_tree`: left leaves
  emit the low segment and right leaves emit the high segment; unused leaf
  split bytes are not range constraints. Recursive ranges, coverage, duplicate
  leaves, cycles and allocation bounds remain checked.
- Repacked music banks are compared after resolving instruments, envelopes,
  key maps, samples, loops and predictor books. All 129 entries in each supplied
  OOT/GoldenEye/Rainbow music bank are equivalent to stock. Only the selected
  course's sequence is imported; live instrument/sample banks remain stock.
- Complete character graphs allocate append-only resource IDs and validate
  native object headers against the selected US revision. Stock racers remain
  unchanged. Track preparation verifies source/target bank identities and
  translates the ten-entry v 1.1 2D-texture insertion, dependent sprites and
  unchanged native level-header initialization fields.
- Isolated preparation produces separate track variants under one public
  course identity. The launcher presents one entry, not duplicate rows.
  Failed preparation reports a specific reason; one rejected character does
  not discard other independently valid characters in the same import.

No submodule, RecompiledFuncs or RecompiledPatches source was hand-edited.
The existing checked native hooks, renderer, HUD, shadow, online, launcher
window ownership and save-isolation mechanisms are unchanged.

## Supplied corpus and authored limits

| Patch | Intended extracted content | Important limit |
| --- | --- | --- |
| Rexy | One T.T.-derived racer | Roaming-dinosaur/global edits are not a new racer dependency. |
| Bond | One Banjo-derived racer | Early car-only experiment; donor stage/other-vehicle assets remain. |
| OOT 1.1 | Link; Dodongo's Cavern, Lon Lon Ranch, Temple of Time, Graveyard | Author does not support plane AI; plane option excluded. |
| GoldenEye | Basement Archives, Temple, Runway, Surface | Collateral edits/deletions to other courses excluded. |
| Rainbow Road 1.1 | Rainbow Road | Car/hovercraft; collateral Greenwood Village edits excluded. |
| Mario | One Diddy-derived racer | Relocated native asset directory supported. |
| Dixie Kong | One Diddy-derived racer | Relocated native asset directory supported. |

Bond's scope is explicitly documented by its author on the
[N64 Vault character page](https://n64vault.com/wiki%3Adiddy-kong-racing-models).
Other pack intent is backed by the supplied archive readmes and asset diffs.
The importer cannot manufacture missing models, animations, voices or AI paths.
Track-only conversion retains native non-track sound effects; separate character
selection cues use the existing isolated sound-bank adapter.

## Validation and boundaries

`runtime-recomp/tests/legacy_compatibility_tests.cpp` takes locally owned ROMs
and supplied patches as external inputs. No ROM or extracted fixture is stored
in the public source or release package. It checks exact expected content roots,
both US revisions, persistence and manifest hashes, additive stock preservation,
live-bank admission, selected-song isolation, malformed BSP/audio/provenance,
complete isolated worker imports, catalogue activation, combined sessions and
reimport deduplication. The existing 20 legacy suites and previously working
Big Boo/Haunter/Yooka workflow are also regression checks.

Automated validation is not a claim that every course/vehicle has been visually
played through. The user performs visual acceptance. In particular, check each
character at selection and in supported vehicles, track preview/racing/exit,
repeat visits and returning to stock content on both original US revisions.

Offline-only and two-extra-active-character limits remain. Arbitrary executable
ROM hacks, palettes without a native adapter, missing xdelta source ROMs and
unsupported game-engine revisions can still be rejected safely. Universal
compatibility with every possible ROM hack is not claimed.
