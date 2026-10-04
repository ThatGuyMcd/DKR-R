# Experimental rollback performance pass — 2 October 2026

Candidate: `1.0.5-beta.14-experimental.perf.1`. Original Beta 14 packages and
the full pre-edit dirty source snapshot are preserved outside the checkout.
This changes the existing experimental backend, not the stable backends or
the launcher flow. It does not qualify every game scene or WAN connection.

## Confirmed causes and surgical changes

1. **Interpolation approval arrived too late.** At a requested 60 FPS, the
   experimental race presented about 30.2 FPS with zero interpolated frames,
   despite later framebuffer diagnostics reporting interpolation enabled.
   Console presentation lacked the native early-present path's exact workload
   approval. The Patch Pipeline now attaches the completed owned frame's
   framebuffer identity to that workload. Explicit retail VI presentation,
   gamma-off and the one-scanline origin correction remain unchanged.
2. **A full GPU drain serialized every authored frame.** The renderer already
   copies guest RAM, but retained CPU upload sources needed an ownership audit.
   RT64's first-target workload/present notifications are not whole-operation
   completion. Pipeline callbacks now signal after each entire operation.
   A bounded two-submission deque retains immutable generation leases to those
   callbacks. CPU upload waits, RT64 resource/WSI synchronization, shutdown and
   exception drains remain. No unbounded queue or additional input delay.
3. **Repeated allocations and memory initialization.** Immutable publication
   and mutable decode now reuse a lazy pool, capped at four immutable images
   and one decoder (80 MiB including cached payload capacity). No const-cast,
   borrowed live RAM, shortened snapshot or disabled state hash is used.
4. **Checkpoint copying overhead.** Experimental callers explicitly opt into
   contiguous full checkpoints and matching-kind spare reuse. Full bases load
   through one checked copy; delta reconstruction still validates its chain.
   Cached spare capacity is trimmed to the existing budget. Stable callers
   retain the original policy; no dirty-write tracking or skipped state domain.
5. **Corrections waited on an independent old image.** Gameplay-only restore
   now retires the obsolete generation and rewinds the owned CPU world without
   draining already-copied GPU work. Menu-resource ticks, pending scene changes,
   epoch replacement and shutdown retain their conservative gates. No blending
   across corrected generations or scene epochs is permitted.

## Ownership evidence

Read-only audit of the protected dependency sources established:

- State/RSP/RDP-TMEM guest RAM reads occur during synchronous decoding.
- Workload uploads use workload-owned DrawData; transforms/tiles upload from
  owned vectors. Their CPU completion is still awaited before decoder reuse.
- Framebuffer storage copies RAM bytes; texture uploads copy TMEM into an
  upload-owned vector. Render/present workers do not retain guest RAM pointers.
- Original GPU fences, framebuffer operations, queue barriers and swapchain
  ownership were not removed. A GPU fence is not assumed to prove WSI completion.
- Quiescence includes retained image consumers, even after CPU decoding finishes.
  Scene replacement cannot bypass those leases.

Changes to RT64 are checked Patch Pipeline inputs only:
`experimental-present-state.patch`, `experimental-workload-history.patch`,
and `experimental-present-queue.patch`. No submodule or protected generated
source was edited. Owned runtime, storage and test sources carry the remaining
implementation. Existing Android, water, HUD, custom-mod and Track Lab changes
were preserved. The public stable protocol VERSION file is unchanged.

## Measurements and qualification

Test machine: Ryzen 9 3900X, RTX 2080 SUPER, Windows, two simultaneous normal
DKR-R instances with isolated profiles and real Quick Join admission. Modern
4× (1280×960) targets, retail gamma; audio is dummy only in automated tests.
Cold menus/shader startup must not be averaged into warm race frame pacing.

| Stage | Requested FPS | Observed race FPS | Actual interpolation FPS |
|---|---:|---:|---:|
| Original presentation, profiled baseline | 60 | ~30.2 | 0 |
| Exact workload approval/history guard | 120 | ~103 | ~77 |
| Plus bounded image/checkpoint reuse | 120 | ~117 | ~87.5 |
| Plus bounded asynchronous submissions | 120 | ~119.5 | ~89.5 |

The asynchronous 90-second run's warm race intervals were approximately
8.3 ms median, 9.0–9.3 ms p95. Initial menus and shaders had substantially
higher outliers and are reported separately in the raw logs. These are local
measurements, not promises for all hardware, distant peers or every track.

Actual CPU qualification compares complete state and confirmed effects after
late-input replay while an admitted immutable image and a modified mutable
decoder remain held. Both render images remain unchanged and the corrected
CPU/effect bytes equal the no-delay reference for 2/3/4 input-owner cases.
The original conservative eight-pulse parked-restore proof remains in place.

The real private-loopback Quick Join impairment test uses 70–130 ms one-way
delay, one-in-seven packet loss and reordering. It completed 60 authored frames,
31 corrections and 77 dropped packets with identical canonical state and
confirmed PCM. This is CPU/network evidence, not rendered WAN qualification.

`Test-IntegratedRollback.ps1 -Profile -AssertPerformance` checks warm race
averages, genuine interpolation and worst-window p95, not merely an enabled
flag. The 60 FPS gate is >=58 FPS and <=18 ms p95; higher refresh gates require
>=90% of target and p95 <=1.5 display intervals. Hardware Windows peers only.

The WSL Linux renderer is llvmpipe software Vulkan. Cross-platform launch/state
checks can run there, but its FPS must not be presented as hardware Linux or
Steam Deck performance. Native Linux and distant-peer testing remain required.

### Final candidate checks

| API / requested FPS | Peer 0 measured FPS | Peer 1 measured FPS | Worst warm-window p95 |
|---|---:|---:|---:|
| Vulkan / 60 | 59.96 | 59.96 | 17.510 ms |
| DX12 / 120 | 119.37 | 119.30 | 9.311 ms |
| DX12 / 180 | 178.85 | 179.15 | 6.910 ms |

Each was a 90-second run of two normal executable instances. Warm intervals
began after at least 300 confirmed race frames. Interpolated-frame averages
were ~29.96, ~89.4 and ~149 FPS respectively, so these are not repeated-frame
FPS claims. The local integration runs did not require late-input corrections;
the separate impaired Quick Join and real CPU proofs cover correction behavior.

All 105 owned regression tests passed on Windows and Linux. Targeted ownership,
snapshot and storage tests also passed after the final rebuild. Actual Vulkan
readback passed at both Accurate 2x/30 and Modern 4x/120. A distinct active-water
scene produced hash `38b537d8860d18e3`, whereas the reference and all corrected
outputs produced `8113345c7ea2bed1`. The actual present queue accepted four
submissions, rejected one retired submission, and completed all five exact
workload/present callbacks without retaining a RAM lease.

The active-water CPU replay proof also passed with full state/effect equality
and retained decoder/image isolation. GPU test fixtures remain private and
are excluded from packages; no private launcher/probe is shipped.

## Handoff conditions

The owned regression suite, actual GPU retirement/readback, 60/120/180 pacing
and packaged Windows/Linux Quick Join launch checks passed. The cross-platform
check executed the actual AppImage with extract-and-run, not just a build-tree
binary, and both peers reached confirmed racing through the normal countdown
and same-window ownership handoff. Packaging's content, controller-Pak and
live SDL input-switch checks also passed without skipping runtime tests.

Use the same candidate on every peer. Original Beta 14 archives remain unchanged;
their SHA-256 values were rechecked against the pre-edit values. Experimental
admission restrictions and full-game/WAN qualification limits remain explicit
in the package's testing guide. No Git commit, push or public release was made.

Packages are in the private `beta14-performance-packages-20261002` directory:

- Windows ZIP SHA-256: `28c551907eed3d9a87d303fcb7ab01ea6f51f293b817029f08f9b5546a8a8b8b`
- Linux x64 AppImage SHA-256: `593cc0851ac6985e525a82ceb7154a077074eed4dcdd0373ddd3a01469793e67`
