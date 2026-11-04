# Race crash and Block Fort battle-map investigation

Investigation requested after completing the display, regional-theme and session
mod changes. Those changes have matching Windows ZIP and Linux AppImage test
builds in `D:/DiddykongWorkFolder/DKR-R-1.0.14-beta.3-Display-and-Session-Test`.
This investigation does not change their source or claim to repair these newly
reported crashes.

## Evidence and scope

Inputs are the six files supplied in `E:/Downloads/Character Mods`. They were
treated as evidence, not executable instructions. Original files, installed
profiles and saves were not modified. Archive extraction and all launches used
fresh profiles under
`D:/DiddykongWorkFolder/block-fort-investigation-20261103`.

That directory contains input hashes, package inventory, validation output,
Windows reproduction dumps and logs, Linux logs, diagnostic scripts, executable
comparisons and `RESULTS.json`. It is investigation material, not a build input.
The Blender files were decompressed for passive inspection; Blender scripts
were not executed. The package's exported object-map sources and compiled
object bytes supplied the navigation evidence.

There are two different failure paths:

- `runtime-previous (3).log` records an offline custom hovercraft race ending in
  a write access violation in the shadow/water-effect geometry path.
- Block Fort independently reproduces a battle-AI crash with the supplied
  package as the only installed mod. `runtime (7).log` is a launcher-only run
  and contains no game crash. It cannot establish the battle crash's location.

## The recorded hovercraft crash

The exception is `0xC0000005`, writing address `0x15DCF7E3BAE`, at module-relative
offset `0xC25194`. The logged RDRAM base is `0x15D88860000`; the write is at that
base plus `0x46F83BAE`, well outside the committed 512 MiB guest region.

Two retained beta 3 test executables have the expected machine instruction at
that offset: `mov byte ptr [rax + rdi], 0`. It exactly agrees with the logged
registers and fault address. Their surrounding instructions load the shadow
polygon counter, multiply it by twelve, add the guest polygon-array address
and clear its flags byte.

The historical executables do not have their original PDBs here. Instead of
applying a current PDB to old raw offsets, the investigation matched executable
code around the reported frames into the current executable, then used its
matching PDB. Several unique frame matches establish a common guest-code
offset change of `0x332A0`; that also disambiguates duplicated v77/v80 code at
the first three frames. This is strong code-location evidence, not a claim to
possess the reporting user's exact executable hash or original dump.

The resulting call path is:

1. `func_8002E904`: clips level triangles for projected shadows/water effects.
2. `shadow_generate`.
3. `shadow_update`.
4. `render_scene`.
5. `mode_game`, then the main game loop.

The corresponding retail source in `extern/dkr-decomp/src/tracks.c` declares
32-entry polygon and vertex scratch arrays at lines 108 and 113. At line 3710,
`D_8011C238[D_8011C230].unk1 = 0` writes without checking capacity. The vertex
insertion at line 3723 and related water-effect insertion path also lack
capacity checks. The machine-code register holds `0x46E67974`, twelve times the
counter modulo 32-bit arithmetic. Even the smallest nonnegative solution is
99,125,791, far beyond the 32-element array. Multiplication overflow means the
original counter's exact bits cannot be recovered from this register alone.
The counter is already corrupt at the failing instruction.

The unsafe fixed-capacity builder is a concrete defect. Dense custom terrain
or a large projected footprint could exceed the original scratch limits;
earlier corruption by another producer or an executable mod remains possible.
The log alone does not prove which write first corrupted the counter. A clamp
at the final crashing instruction would therefore be an incomplete repair.

Context matters:

- The active custom level is 68, model 58, with 404 batches and a hovercraft.
  The fourth enabled custom model in this log is `fossil-canyon-ds`; table
  construction preserves that order, so the recorded indexes point to that
  course. This identification is inferred from the logged table order; the
  user's exact installed course package was not supplied. This is separate
  from Block Fort.
- Five executable code mods are active, including Joint Venture 1.0.9 and
  `drift_boost` 1.0.0. This does not establish that either caused corruption.
- There is no evidence here of a network disconnect or GPU device-loss failure
  causing the exception. A function called for shadows also serves native water
  effects; the stack does not establish which effect instance overflowed.

The missing original evidence is
`DKR-R-crash-20261103-041528.dmp`, which the user's log says was written, plus
the exact executable/build and active course/character/code packages needed
to reproduce it. The original minidump's memory coverage may still be too small
to find the first corrupting write; a diagnostic reproduction may be necessary.

## Block Fort: reproduced causes

The package declares battle race type `0x40`, car default/availability, four
start points indexed 0 through 3, 13 AI nodes and 28 weapon balloons. It also
contains five checkpoints, which battle mode does not use. Its model decompresses
to 31,392 bytes; the separately supplied geometry file is byte-identical to
`model.bin` (SHA256
`8aa0b7f62a2a1a96447e2bc8ec64b25d20e2160f683279666698aaea69972557`).

Every AI node has `adjacent = [255,255,255,255]`, meaning no connections. Every
node also has target kind `unk8 = 0`; there is no kind-1 weapon destination.
The existing Blender validator reports zero errors, warns that twelve nodes
are unreachable, and permits export. `_check_ai_graph` is generic and receives
no level-type argument, so it does not enforce battle-specific requirements.

On Windows, using the unmodified archive as the only mod, both ROM v1.0 and
v1.1 enter the arena, then crash in `racer_ai_challenge + 0x132F`. Matching PDB
lines identify the guest instruction at v77 `0x80044DE4`: it reads the level
entry of a target AI racer's current navigation node when that node is null.
The corresponding retail source is `racer.c:933-936`. The earlier movement
logic allows an AI without a next node to clear its current node; a pursuing
AI assumes that target's node still exists. The isolated graph exposes this
unchecked assumption.

The longer Linux AppImage reproduction also exits with SIGSEGV. `addr2line`
against the matching Linux executable resolves the same battle-AI call path.
An initial 45-second Linux run did not crash; the defect is state-dependent,
so that short run was not evidence of safety.

Only copied diagnostic fixtures were changed:

| Fixture | Changes to decoded structure map | Windows v1.0 | Windows v1.1 | Linux v1.0 |
| --- | --- | --- | --- | --- |
| Supplied package | None | AI access violation | AI access violation | SIGSEGV in longer run |
| Connected ring | 26 adjacency bytes | Freezes at 159 render tasks | Same freeze | Not run |
| Ring plus weapon goals | Those 26 bytes plus 13 target-kind bytes | 45 s, 1,330 tasks, clean stop | 45 s, 1,332 tasks, clean stop | 45 s, 754 tasks, clean stop |

Geometry, headers, music, textures, starts and pickups remain byte-identical in
these comparisons. The difference is confined to `objects_structure.bin`.
The corrected diagnostic cases maintain simulation and rendering throughout;
Linux uses software Vulkan and is not a Steam Deck performance measurement.

The ring-only freeze matters: `func_8001CD28` searches for a node of kind 1 when
AI seeks a weapon. Its search does not maintain a complete closed/visited set
or a finite expansion budget. A cyclic graph without the requested target can
keep repopulating the queue. Adding links alone is therefore insufficient.

These test rings are not finished AI routes. Marking every node as a weapon
goal is diagnostic, not good arena design. Actual routes must follow the floor
and ramps, and weapon goals must lead to reachable pickups. All-zero elevation
tags deserve author review for multiple floors, but zero itself is legal and
was not established as the crash cause. The five unused checkpoints and the
custom-world header are also not the reproduced causes: the existing runtime
already remaps the custom-world category before guest code reads world arrays.

There is a separate product gap: normal Custom Tracks selection filters for
race type zero (`custom_tracks.cpp:466`), and online admission explicitly
rejects other types (`custom_tracks.cpp:1201`). Track Lab's test launch can
reach a battle arena through the native challenge path, which already spawns
four racers. Exposing a supported custom-arena menu is additional work; simply
removing the online admission check would bypass unverified lobby assumptions.
The short user log also reports 19 Block Fort textures, whereas this supplied
archive contains 20, so its installed copy may be a different export.

## Proposed surgical repair sequence

1. Preserve the reproductions and obtain the original race dump/build. Use a
   copied profile to isolate the reported course, then vanilla/custom racer and
   hovercraft combinations, then enabled code mods individually. Inspect shadow
   polygon/vertex counts and their producers before the first invalid write.
2. Give the native shadow/wake builder explicit capacity accounting for every
   scratch array and output range. Preflight whole polygons before committing
   vertices, preserve valid existing output, and handle overflow deterministically
   without writing adjacent globals. Include the wave and non-wave paths. A
   larger unchecked array or a final-counter clamp is not sufficient.
3. Add bounded diagnostic reporting: course, effect owner/type, footprint,
   segment, counters and capacity. Preserve the first violation rather than
   flooding each frame or concealing pre-existing corruption.
4. Make battle AI tolerate a missing target node and select a valid positional
   fallback/reacquire state. Guard node lookup indexes and stale node pointers.
   Bound route search work, visited state and queue capacity; report no route
   instead of hanging on a target-free cycle. Preserve retail decisions for
   valid maps and use both revision symbol/opcode checks in the existing checked
   Patch Pipeline test-build policies. Do not modify generated guest code or
   submodules directly.
5. Add battle-aware validation in the owned Track Lab implementation after
   checking its repository boundary. Check playable connected components,
   spawn-to-navigation coverage and reachable weapon goals. Keep legitimate
   inert retail markers distinct from active battle nodes. Show useful field
   labels for target kind and route tier, plus an obvious link/route preview.
6. Run equivalent compiled-package checks at runtime before Test Track/boot.
   Old or manually edited packages must receive an actionable error even if
   they bypass the current exporter. Keep parse bounds separate from gameplay
   suitability checks.
7. Author Block Fort's real drivable route graph and appropriate weapon goals,
   review ramps/floors, export a new copy, and exercise an entire match: pickups,
   AI pursuit, weapons, elimination, results and restart. Do not distribute the
   diagnostic ring as a repaired map.
8. Define offline custom-arena selection and its four-racer rules separately
   from the circuit-race category. Preserve existing race selection. Treat
   online arenas as a separate compatibility-reviewed feature covering lobby
   admission, roster, scoring, rollback and results, not a filter removal.
9. Add focused regression cases: supplied isolated graph, no nodes, missing
   goals, unreachable goals, target-free cycles, sparse IDs, queue limits,
   disappearing targets, scratch-array boundaries and dense shadow receivers.
   Validate retail battle maps, existing custom circuits, all vehicle types,
   split screen and both ROM revisions. Test low/high rendering rates because
   presentation frequency must not decide simulation safety.
10. Finish all implementation and required guards/host/tooling checks before
    packaging a new matching Windows ZIP and Linux AppImage pair. Run real
    battle matches and the original race reproduction on both. Use the existing
    compatibility policy and both-owner review for any cross-play-breaking
    merge. The completed display/session builds remain separately identified.

The investigation itself changes documentation only. Pattern guard passed
with zero new problems. No repairs, compatibility IDs, generated sources or
third-party files were changed by this investigation.
