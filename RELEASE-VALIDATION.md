# v1.0.15 release preparation — 8 November 2026

This records local preparation since the public `Version1.0.14` release at
`fbae505f9fe2dae63d3a351aa3a82da2b4e7dae1`. No push, tag, upload, draft or
publication is part of this handoff. The external handoff records the exact
local source commit, final package hashes, signatures and detailed evidence.

## Included changes

Online input recovery, reusable rollback checkpoints, buffered confirmed
spectators and live host settings are joined by storage cleanup, the pinned
Play bar and controller fixes, native selected-character labels, the widescreen
initials keyboard, isolated file dialogs and improved crash context. See the
[release notes](docs/PUBLIC-1.0.15-NOTES.txt), [changelog](CHANGELOG.md) and
[documentation review](docs/DOCUMENTATION-1.0.15.md).

Current first-party sources and existing baselined compatibility layers carry
these changes. No protected dependency, RecompiledFuncs, RecompiledPatches or
frozen capsule bytes are edited or repinned. Both ROM revisions and their owned
payloads remain in the checked integration. New owned identities require
matching complete packages. Local preparation is not approval of a cross-play
compatibility-breaking merge; the repository's both-owner rule still applies.

## Current build and package coverage

The release handoff supplies Windows x64, Linux x86_64, Linux ARM64 and the
exact Linux x86_64 AppImage copied under its Steam Deck name. Its receipts bind
the current source audit and complete allowlisted files, including the new
private file-picker helper. ZIP/AppImage contents are read back against those
receipts. Linux ELF inspection and packaged helper probes are separate checks.
ARM64 checks use QEMU with a target sysroot; they do not qualify a physical GPU,
desktop portal or controller. Platform exclusions remain in [Platforms](docs/PLATFORMS.md).

Fresh Release host runs passed all 172 tests on Windows and all 172 on Linux.
Guard/tooling, metadata, source-asset checks, installed hooks and
document links must all pass before the source is frozen. Final outcomes,
including any failed attempt or rerun, are retained outside the repository.

The reviewed packages passed eight Windows and four AppImage startup cases:
both ROMs, manual and automatic startup, D3D12/Vulkan on Windows and software
Vulkan under WSL. The initial six-second Linux run missed first presentation
in one v1.1 case while the twelve-process lobby ran; it exited cleanly. The
complete rerun allowed fifteen seconds, kept the same presentation checks and
passed all four cases. Both attempts are retained. WSL/software graphics are
not Steam Deck frame-rate qualification.

## Character labels, initials and crash repairs

The selected-character native fixture passed 39,778 checks. A real long-name
Wario package reproduced the missing upper label before the repair and showed
it afterwards. Three fresh WSL software-Vulkan launches exercised eight real
characters, both rows/all four physical stage slots, shoulder browsing and
4:3, 16:9, 16:10 and wide resizing, each stopping cleanly after 90 seconds.
L/R label controls were detected in 48 fixed-size selected-state captures;
this checks the draw path, not OCR of every name. Names and resize layouts
were inspected separately. Rayman displayed a label, but the exact reported
session-dependent Rayman omission was not reproduced. Physical four-controller
visual acceptance remains separate. See the [visibility record](docs/notes/2026-11-08-character-label-visibility.md).

The initials fixture executes freshly generated draw code for both revisions,
all selections, fractional scrolling and three menu heights. It checks glyphs,
SP/DEL/OK, state restoration and unchanged initials/input state. The normal 4:3
clip remains at that aspect. See the [menu record](docs/notes/2026-11-08-menu-ui-repairs.md).

Native crash/picker tests cover fault context and binary identities, redaction,
child crashes, malformed replies, timeout/cancellation, parent lifetime and
Unicode paths. Earlier same-module qualification included real GTK dialogs,
sanitisers and 250 rapid Windows consent toggles. Those earlier checks retain
their original package scope. Private matching symbols are retained separately.

The supplied three archives contained logs/manifests, not actual dumps. Dialog
isolation contains the shell/dialog failure route; the exact original trigger
is unproved. The Windows graphics-startup access violation and Linux gameplay
SIGSEGV remain unresolved. Better fault context is not a claim to fix either.
See [crash reporting](docs/CRASH_REPORTING.md).

## Storage and UI coverage

Startup cleanup runs once per application launch and can continue at background
priority during play. Successful Store installs clean their own verified
outputs without a global rescan. Live online resources retain leases; saves,
ROMs, installed content and current/previous recovery remain protected.
Recognised original dumps older than 30 days require a separate manual choice.
See [Storage](docs/STORAGE.md).

Earlier storage qualification covered retained editions, interrupted retirement,
linked files, Windows paths, leases and actual idle/gameplay checks. Its native
four-racer/eight-viewer baseline exited cleanly with zero racer input waits;
viewers replayed 1,396–3,289 race frames. This was a same-PC pre-menu/crash
build with no added network latency, not the final package bytes.

Play now has initial launch focus without a focus trap, conditional edge-aligned
scrolling, a transparent child and a full-width footer with a narrower centred
button. The left stick excludes the sidebar navigation switch. Session mods
has no tooltip. Save Manager's pending-change bar also reaches the panel edges.
The launcher and overlay share these changes; active-game editing restrictions
are unchanged.

## Earlier online timing qualification

The 7 November mixed-platform run followed the spectator-budget correction.
Resetting its timestamp on every pump had discarded fractional milliseconds;
retaining that time fixes starvation without changing bandwidth/burst limits.
The regression exercises high-frequency pumping, bounded bursts and clock reversal.

A Linux AppImage host, three Windows racers and eight late Windows spectators
completed a same-PC controlled wire test: measured 150/450/750 ms RTT, ±25 ms
jitter per direction, 1% packet loss and 200 ms spikes. One viewer also received
a temporary four-second RTT burst. All 12 exited cleanly; viewers replayed
835–3,041 race frames. Cumulative racing input waits were 0 / 0 / 16.677 /
10.114 ms. Racers had zero spectator-checkpoint waits. Auto delay was ON and
the window was 20, while fresh profiles still default to 14. Independent clock
checks before, twice during and after stayed within 5 ppm; temporary calibration
and scoped shaping were restored afterwards.

These measurements predate the storage/UI/crash follow-ups. They remain useful
coverage of the unchanged online work, not exact-byte qualification of the new
packages, separate-machine WAN testing or controller-to-screen latency.
The external handoff preserves the original results and failed earlier attempts.
A fresh native Windows full-lobby smoke test on the current runtime completed
with four racers and eight late viewers. All 12 exited cleanly, all racers
logged zero racing-input and spectator-checkpoint waits, and viewers replayed
1,627–2,531 race frames. Its package/runtime hashes are retained in the handoff.
This is a same-PC baseline without added latency; it does not replace the
measured mixed-platform wire test. Later package documentation refreshes must
retain the tested executable bytes or repeat runtime qualification.

Live-edit qualification also completed five prediction changes
(14 → 20 → 2 → 20 → 6 → 20), Auto/manual switching, client rejection and queued
cancellation with four racers. Deliberately small windows under high latency
caused waits. This establishes safe live edits, not zero-wait performance.
Earlier [playability](docs/notes/2026-11-04-online-playability.md),
[full-lobby](docs/notes/2026-11-06-full-lobby-playability.md) and
[live-settings](docs/notes/2026-11-07-live-host-settings.md) notes retain their
original private-build scope.

## Remaining acceptance

Physical Steam Deck LCD Wi-Fi in Gaming/Desktop Mode, separate machines/WAN,
physical ARM graphics, four-controller feel, audio and suspend/resume require
device checks. Native tick rate, rendered FPS and input-to-screen latency are
separate measurements. Neither locked 60 FPS nor pause-free play at arbitrary
latency is guaranteed. Confirmed spectators do not supply racing inputs but
still consume bounded host CPU and bandwidth.

The vanilla online car textures/remote wheels remain unresolved. Draft
translations, experimental graphics/haptics and custom battle-map requirements
remain. The read-only GitHub issue snapshot is in the handoff; preparation does
not close reports or claim unrelated fixes.

## Review and update signing

The proposed tag is `Version1.0.15`. Commit the complete reviewed source locally
before signing the finished packages with the existing trusted key. The signed
manifest binds that exact commit and package bytes. Verify the signature after
copying and qualify upgrades from the exact public v1.0.14 package: normal,
corrupt-stage rejection, interrupted replacement and legacy-layout migration.
Synthetic profiles are used; real player data is not an update fixture.

Read the handoff's document review, GitHub release text, final validation and
upload list before approving publication. Evidence, profiles, private symbols
and signing material are not public release assets. Previous
[v1.0.14 validation](docs/history/RELEASE-VALIDATION-1.0.14.md) remains available.
