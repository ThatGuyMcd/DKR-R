# Beta 3: analogue driving, CRT controls and look behind

Implement the approved analogue driving plan, then package one matching Windows
and Linux AppImage test pair as 1.0.14-beta.3. Keep all current online fixes,
the brighter DKR water and the launcher design.

The user authorised extending the existing checked Patch Pipeline policies for
these test builds while guest-hooks/hooks.toml is absent. Both-owner review
remains required before merging a compatibility-breaking change.

Work order:

1. Add independent, calibrated per-profile accelerator/brake actions and preview
   bars. Resolve the LT item binding and duplicate RT digital acceleration in an
   explicit preset. Preserve old bindings until the user applies it.
2. Record compact accelerator/brake values and their mode with the normal input
   frame. Carry them through every wire format, prediction, rollback and spectator
   path; version incompatible packets and the existing compatibility ledger.
3. Expose an additive SDK capability without changing the old DkrInput ABI.
   Supply an optional gameplay .nrm and source using that capability.
4. Apply normalized requests after native throttle/brake updates and before their
   physics consumers, preserving native buildup, release, boosts and restrictions.
   Cover car, hovercraft and plane in both supported ROM revisions. CPU racers
   and disabled/digital profiles retain their original path.
5. Add the full existing CRT control section to Graphics > HUD & effects, sharing
   its settings and renderer with Textures.
6. Restore a standalone Look behind checkbox and binding. Keep it local to the
   presentation camera, work without ModernCamera++, and keep original views
   available when that mod is unavailable or quarantined.
7. Run policy, SDK, protocol, replay, UI, guard/tooling and host suites; then run
   actual game checks and package matching Windows and Linux AppImage builds,
   the .nrm, sources and setup instructions. Record test limits honestly.

Implementation and checks:

- The optional SDK 2 mod uses the new `analogue-driving` v1 capability. It
  requests the recorded pedal values through two services; the old 20-byte
  `DkrInput` stays unchanged. The build kit reproduced the supplied `.nrm`
  byte for byte.
- Both revision policies derive their seven hook sites from the selected ELF
  and check native opcodes. The hooks cap native throttle/brake buildup before
  the physics consumer. They do not assign speed. Full input and release leave
  the original native fields alone.
- The online sample carries separate calibrated bytes and a mode flag. Owner
  packets, confirmed feeds, live delay, rollback and recordings retain them.
  The packet protocol and replay-file version distinguish this test build.
- Look behind reads a retained image and redraws the local player's camera.
  It does not write shared guest RAM or require a camera mod. The compiled
  session test covers native slots 0 and 2 using physical profile 0, both ROM
  revisions, hold/release, blocked input and the setting being disabled.
- Windows passed all 151 host tests. Linux passed 151 of 152; its regression
  tooling harness hit its existing 20-second child timeout under concurrent
  work, then passed unchanged in an isolated retry.
- The actual production Windows and Linux input, packet/replay and camera
  checks passed. They cover partial travel, combined pedals, digital fallbacks,
  disconnects, analogue-only rollback corrections, versioned recording and
  unchanged camera/simulation RAM. Both platforms have matching online IDs.

These checks do not replace a physical-controller playtest of car, hovercraft
and plane handling, boosts, reverse or mixed controllers. Package and rendered
runtime results are recorded with the delivered test files. No main merge or
cross-play approval is implied by the test-build authorisation.

The rendered host/client check caught an additional integration fault before
delivery: the party envelope still declared the old 25-byte controller sample.
The extended sample is 28 bytes. All six send paths and the receive path now
use one checked codec constant. The strict party wire version was advanced,
and an actual PartyRuntime regression verifies partial, combined and repair
delivery plus rejection of the legacy prefix. Both platforms pass the complete
party suite, including the existing two-to-four-player loss/jitter simulations.
