# Magic Codes: v1.0.4 repair and validation

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


## Scope

This is an authored-runtime/Patch Pipeline repair. No RT64, N64Recomp,
N64ModernRuntime, generated source, shadow, scenery, physics, EEPROM format,
network protocol, or rollback algorithm is hand-edited.

The previous Windows ZIP, Linux AppImage and source ZIP are retained locally in
`build/rollback-pre-magic-codes-20260906`. The replacement packages are built
separately under `dist/magic-codes-v1.0.4-20260906`; they do not overwrite the rollback.

## Findings and changes

1. The former process-wide `g_applied` was reset by UI configuration, not by each
   game session. Returning to the launcher and starting another game could leave
   the new RDRAM without selected codes. `game_main.cpp` now begins a fresh Magic
   Code session before starting the runtime thread. Injection remains at the
   existing first `main_game_loop` hook, after native initialization. It ORs the
   selected bits into the active/unlocked words and preserves progression bits.
   Later native toggles remain authoritative: injection never runs every frame.
2. One-shot selections previously disappeared from the launch queue on injection,
   before their native action happened. They now remain queued until an explicit
   success branch executes and the containing retail mode returns. A missing
   active bit alone is not evidence of completion: native clear-all/toggle can
   also remove it. Cancelled native actions remain queued for a later launch;
   untick them in the launcher/overlay to cancel the queued request itself.
3. Credits follow the native **Options > Magic Codes > Back** transition. There
   is no synthetic title-screen/menu jump or new credits renderer. Credits are
   deferred during online launches and remain queued for offline use. The UI
   describes this route explicitly.
4. EOLAOBFENRLONE increments the chosen Adventure save through the original
   `menu_file_select_loop` code. No custom increment, file selection, save
   replacement or EEPROM write is added. Completion means the native in-memory
   award ran, not that EEPROM has reached durable storage. Allow normal saving
   before quitting. Crash-atomic coordination of EEPROM and launcher settings
   would require a broader save-system change and is not claimed by this repair.
5. A frozen per-game selection is distinct from next-launch preferences. Online
   startup takes the accepted lobby manifest; JOINTVENTURE topology checks use
   that frozen selection. Editing codes in the UI is disabled while in a lobby.
   The one-shot queue can change without changing a running simulation's codes.
   The launcher's cached offline compatibility manifest is invalidated after a
   code change, not just after choosing a different ROM. This closes the former
   stale-manifest path; active lobby manifests are never replaced.
6. Queue mutation and disk replacement share a single mutex. Per-action request
   generations keep completion of an old request from deleting an action that
   was cancelled and requeued in the overlay. Failed writes preserve the queue,
   expose an error, and retry acknowledgement at most once per five seconds.
   No filesystem access or mutex acquisition is added to ordinary game ticks
   once initial injection completes and no action acknowledgement is pending.
7. Retail mode filters are deliberately unchanged. The UI now describes Tracks,
   Adventure, Time Trial, challenge, menu-only and diagnostic restrictions. For
   example, FREEFRUIT and forced balloon colours are not universal Adventure
   modifiers. EPC is a fault diagnostic, not a visual gameplay effect.

## Exact native observation points

| Purpose | US v1.0 / v77 | US v1.1 / v80 |
|---|---|---|
| Existing first main-loop injection | `0x8006C60C` | `0x8006C84C` |
| Credits branch after `menu_init(25)` returns | `0x8008A484` | `0x8008A934` |
| Free-balloon branch before final `sh t4, 0(v0)` | `0x8008E2F4` | `0x8008E7AC` |
| Existing common post-mode commit | `0x8006C90C` | `0x8006CB4C` |
| Active Magic Codes word | `0x800DFD98` | `0x800E0318` |
| Unlocked Magic Codes word | `0x800DFD9C` | `0x800E031C` |

These are separate revision-specific instruction observations, not a guessed
global offset. The balloon observation precedes its final store because the next
instruction is also a no-award branch target. It records only a pending event;
queue acknowledgement waits until the common post-mode hook. Neither success
hook changes guest registers or RDRAM. Native menus run on the authored frontend
timeline; additionally, speculative/replay execution cannot record or acknowledge
these external queue events. Native state changes themselves remain replayable.

The existing netplay commit stays first and unchanged. Its hook is extended,
not duplicated. Policy hashes change, so all peers need the same rebuilt release.

## Automated verification

- `DKRMagicCodePolicy`: bit exclusions, progression preservation, one-shot
  completion, duplicate completion, replay suppression and offline-only credits.
- `DKRMagicCodeRuntime`: compiles the production runtime implementation against
  test RDRAM and a substituted replay gate. Exercises all 24 selectable bits on
  both revision tables, repeated sessions, native cancellation, independently
  completed actions, unchanged guest registers, frozen online selection,
  cancel/requeue races, concurrent queue edits, persistence and failed-IO retry.
- `DKRMagicCodePipeline`: verifies unique authored hook anchors and actual
  generated placement for both revisions, including the balloon store, while
  rejecting patches to `get_filtered_cheats`.

These tests do not substitute for two-machine online gameplay, physical Steam
Deck/controller testing, or playing every race modifier in every retail mode.
Release validation results and remaining manual checks are recorded separately
with the packaged build.

## Validation completed on 2026-09-06

- Windows Release: **57/57 CTest cases passed**, including all three Magic Code
  suites and the existing direct-session, rollback, save, input and presentation
  regression suites.
- Linux Release: **57/57 CTest cases passed** from the same authored sources.
- Both sets of generated hooks were checked against their native instruction
  anchors. Both retail `get_filtered_cheats` implementations remain unchanged.
- Windows visual smoke checks: v1.0 and v1.1 rendered the intro/title content;
  closing the test instances returned exit code zero. The v1.1 timed test
  completed 1,325 graphics tasks in 45 seconds and stopped cleanly. Unused credits
  and balloon selections remained queued afterwards.
- Launcher visual check: the expanded Magic Code grid, pending selections and
  wrapped availability/instruction text displayed correctly in the shared UI.
- Repository asset scan and v1.0.4 release metadata validation passed.

### Manual acceptance still required

The automated input tool did not reliably drive native game-menu keystrokes, so
the credits transition and balloon award were verified by production-callback
tests plus generated-instruction audits, **not** claimed as visually exercised
end-to-end. Before publishing, test those actions through the native menus on
both revisions, then verify the following:

1. Quit to the launcher and relaunch without restarting the application; selected
   persistent codes still apply.
2. Use credits via Options > Magic Codes > Back. The credits queue clears only
   when credits start. Exiting an unused session keeps it queued.
3. Select an Adventure file with EOLAOBFENRLONE queued, receive one balloon and
   allow normal saving. Re-entering the file does not award another balloon
   unless the code is explicitly queued again.
4. Exercise representative allowed and excluded contexts: FREEFRUIT in Tracks
   versus Adventure/Time Trial; forced balloon colours in an ordinary race versus
   a challenge; DOUBLEVISION at character select; JUKEBOX in Audio Options.
5. On two machines with this same build, change codes before hosting, synchronize
   the client, and verify regular races, 2P Adventure hubs and boss transitions.
   Launch codes must stay agreed even after one-shot actions are consumed.
6. Check the Magic Code grid and controls on a physical Steam Deck. Linux AppImage
   self-tests are not a substitute for Steam Deck gameplay acceptance.
