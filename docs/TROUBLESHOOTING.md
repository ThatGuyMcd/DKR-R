# Troubleshooting DKR-R

For slow mod joins, use **Copy Join Diagnostics** and include whether the wait
is Downloading, Preparing or waiting for the host. Send the joining client log
as well as the host log. Restarting through the updater now hands logging to
the new process correctly.

On Steam Deck without a keyring, enable Keep me signed in and sign out/back in
once to save the device credential. See [sign-in storage](PUBLIC_LOBBIES.md#sign-in-once).
For missing Discord pictures, check that the profile API migrations and matching
Workers are deployed, then sign in again. Existing online play can still work
when picture import is unavailable.

## v1.0.12 release contract

Normal mods share one personal Adventure. Check portable/profile location and Backups & Recovery before restoring. Keep installation receipts/recovery folders. Export logs/support details; report the exact release, ROM revision, course, packs and settings. The accepted HD-water startup fix is included; a hardware-specific problem still needs its own report.

Start with the symptom and keep the original data. Please don't delete your
saves or whole profile to troubleshoot a graphics, mod or connection failure.

## Modded joins, race starts and spectator black screens

Check that everyone has the complete v1.0.12 package first. This release
includes the accepted playtest.4 fixes. Keep the downloaded offline-mod
option enabled if you want those copies; matching installed content no
longer causes the old identity-conflict warning. Genuine conflicts and
damaged files are still reported without replacing your offline mods.

If joining fails, use **Retry Join** and **Copy Join Diagnostics** in
Session mods. Send the full host and affected client/spectator logs from
the same attempt. Include the track, enabled mods, role and whether the
problem happened on first join, race start or a later track change.
Please don't delete your whole mod library or turn off safeguards.

A spectator error after five failed recoveries includes the reason. The
log also records `[spectator][music]`, `[spectator][admission]` and
`[spectator][playback]`. A race-start stall needs both racing logs too.

## Missing HUD elements, memory reports or rejected graphics

Use the full v1.0.12 package first. Keep `runtime.log` and
`runtime-previous.log`, with the track, ROM revision, mods, texture packs
and action immediately before the problem. For Online, include the host
and affected client or spectator logs from the same attempt.

The normal game heap now uses the admitted expansion RAM headroom.
`[memory][primary-pool]` and `[hud][heap]` help identify the actual heap
state. More headroom does not make arbitrarily large mods safe.

Malformed graphics are still rejected. The scheduler handoff repair
closes an earlier memory ownership gap, but the exact previously logged
malformed task was not reproduced with game assets during preparation.
Keep any new command/provenance reports; do not disable those checks.

Water staying vanilla can mean an incomplete HD animation. All frames
need matching replacements before that family can use HD consistently.
For `unsupported car texture profile`, use the original updated export
with v1.0.12. Unknown format revisions or enabled custom boost anchors
still need exporter/runtime support. See [Vehicles](VEHICLES.md).

## ROM rejected

Only the clean US v1.0/v77 and Rev A/v1.1/v80 engines are supported.
[ROM setup](ROM_SETUP.md) lists the exact hashes and supported byte orders.
Import the original ROM; import a legacy patch through Mods / Hacks instead of
selecting its already-patched ROM as the game.

A damaged private `rom-cache` can be recreated from the selected original.
Close DKR-R first and remove only that cache, not saves/settings.
The old Windows extracted Rev A engine cache is not used by current linked
payload selection; deleting it is not a current revision-repair instruction.

## Saves or mods look missing

Check normal versus portable mode and any config override first. A profile
switch does not migrate data. Check Save Manager's Offline, Online, Modded and
Recovery scopes, then use Refresh Stored Saves. Restoration is explicit.
See [Saves](SAVES.md); never overwrite the only surviving copy.

After source re-preparation, an existing asset-only edition can remain selected.
Choose the new edition deliberately. Hidden content is inactive. Importing
does not automatically turn legacy mods on.

## Mod preparation or gameplay fails

Read Details for asset-only/native-adapted/partial support.
An xdelta needs its exact original source revision; all clients must own that
source if reconstruction requires it. A filename or successful emulator run is
not proof of native compatibility.

Use **Help & imports → Prepare all source imports again** after supplying missing
sources/revisions. Cancel safely; do not modify receipts/reviews/journals.
Memory budgets remain real even with the larger character roster.
Reduce enabled content for a budget failure without deleting the installed set.

For an asset crash, send the log/dump, mod name/edition, original ROM revision,
scene and whether the same track fails without mods. Do not send ROMs or
unlicensed extracted assets. See [legacy mods](LEGACY-MODS-BETA.md).

## Online stalls or failure

Use compatible complete packages and the same original ROM revision.
The host creates the lobby after choosing mods/rules; changing them requires a
new lobby. Clients must complete consent, preparation and verification.

v1.0.12 uses Rollback; the old host-backend selector is removed.
Try automatic input delay and a suitable prediction window for your route.
A bounded input wait, renderer slowdown and scene-loading barrier are different
problems: record the network overlay state and exact scene.

Send host and client logs from the same attempt. Include player count, backend,
delay/window, enabled mods, scene, and whether the overlay responds.
Don't publish private codes or profile keys.
See [Online](ONLINE_MULTIPLAYER.md) for direct-routing limits.

A compatibility failure names its component. A replay-engine mismatch
usually means different builds, even if filenames look similar. Prepared-mod
mismatches need the host's exact verified resources, not just matching names.
Checkpoint size/schema, launch, save and epoch failures need paired logs for
diagnosis; don't delete offline saves or bypass the check. Use the complete
same release on Windows and Linux/SteamOS before retrying.

## Public list, sign-in or online count

Public discovery needs a Discord account and Online connection. Reuse the
Mod Store sign-in or finish browser pairing from Online. Confirm the Public
filter, host visibility and service status. A private/locked/full or starting
room does not grant a new racing place. An old private lobby made while the
service was unavailable needs recreation before public publication.

The total counts accounts, not instances/controllers. A sleeping/crashed client
can linger up to 60 seconds. Appear offline is a friend-presence control;
Disconnect Online removes this instance from aggregate presence. Without a Linux
keyring, Keep me signed in uses the owner-only device store; session-only sign-in
is still available with remembering off. Do not disable certificate checks
to work around TLS errors. See [Public lobbies](PUBLIC_LOBBIES.md).

If mod preparation is already approved, account/roster refresh should not
restart it. Report long-transfer resets with paired logs and privacy/lock/phase
changes. A running spectator request also needs the service's separate flag;
do not treat that rejection as a free racing place or remove a checkpoint guard.

## Vehicles, achievements and texture tools

Custom cars need Car/Track Select, an enabled `.dkrkart` and a compatible driver
model. Adventure/hover/plane support is not supplied by renaming an export.
See [Vehicles](VEHICLES.md). For achievements, check the active profile and
goal requirements; Max Out Save and watching do not award gameplay goals.
Use [existing-progress recognition](ACHIEVEMENTS.md) only for provable milestones.

Texture Dumping reports skipped sources and unknown identities. Capture the
missing runtime variant, preserve Working edits and refresh the browser after
external edits. Keep original art private. See [Textures](TEXTURE_PACKS.md).

## Graphics or uneven presentation

First compare the same scene offline at 60 FPS. Temporarily disable enhanced
water/hardware tracing, packs and CRT filters to isolate additional work.
Do not confuse presentation FPS with game speed.

On Linux/SteamOS, verify the native Vulkan driver; software/remote-display
drivers are not a useful performance baseline. A launcher can open before the
game's Vulkan device is created. If an API fails, choose Automatic on the next
launch rather than deleting a ROM/save.

Some graphics/scenery controls have narrower scope in Experimental Rollback.
See [Graphics](GRAPHICS.md) for the actual restrictions.

## Controller or gyro trouble

Use [Controls](CONTROLLERS.md) to choose SDL2/SDL3 and run the N64 mapping wizard.
Auto keeps one selected backend; it should not repeatedly rediscover every
device during a race. Report periodic spikes with controller model, connection
type, OS and backend; compare explicit SDL2/SDL3.

Steam Deck native gyro must not also be mapped to mouse/stick by Steam Input.
Calibrate while still, then recenter at the desired neutral angle.
An absent physical sensor needs device/permission diagnosis, not guessed gyro
values. The private helper's `--self-test` can report its supported interfaces.

## Audio stops

Note whether only music stopped or effects/voices stopped too, and whether
changing tracks or returning to the title restores it. Include custom music/
character details and the log from that session.
See [Sound](SOUND.md). Do not claim a missing cue was supplied by a mod until
its source actually contains it.

## Android startup and performance

Send device/Android version, build code, graphics settings and scene.
Use **About DKR-R → Support summary → Export Android Startup Logs** after
reopening the app. If performance recording or export itself crashes, stop
retrying and report that separately. `adb logcat -d` can capture earlier
Java/native loader failures when a normal runtime log never starts.

A black screen with continuing audio differs from a silent process crash.
Say which happened. Recording/screenshots can add load or expose driver/surface
faults; include whether capture was active. [Platform support](PLATFORMS.md)
does not promise every handheld is already qualified.

## Logs and support reports

Open **About DKR-R → Support summary → EXPORT SUPPORT SUMMARY**.
The summary omits private paths, save contents, account/device identifiers and
lobby codes. Raw logs/dumps are different: review them before sharing.

Use **OPEN LOGS** rather than assuming a downloaded package's folder is active.
Normal Windows uses `%APPDATA%\DKRPort\logs`; Linux uses the active
`dkr-port/logs` config root. Portable/config overrides select their own root.
Android uses the app export controls.

`runtime.log` is the current attempt and `runtime-previous.log` the preceding
attempt. Older rotated logs are under `logs/history`, bounded to 20 files and
128 MiB. Diagnostic logging and crash dumps have separate switches.
A crash dump and its matching runtime log are more useful than a later clean log.

## A patch has no effect

A compile-only rebuild does not regenerate game hooks. Recompose/regenerate
both revision payloads, and owned payloads when relevant.
Follow [Building](BUILDING.md); never edit generated C to bypass the pipeline.

## Source dependency preparation fails

Keep the checkout and read the dependency name/path in the error. Inspect
`git -C <dependency-path> status` and `git -C <dependency-path> diff` before
retrying. Commit or stash developer changes you need to retain; do not delete
an entire `extern/` tree as the first recovery step.

`python3 scripts/apply_dependency_patches.py --check` validates complete
ordered patch stacks without changing checkout files or the real index.
It accepts pristine or exact intermediate states that normal application can
finish. Add `--require-all` to require every manifest checkout. Checksum or
pinned-commit errors require correcting the source inputs, not bypassing checks.

Normal application refuses unrecognised managed edits. `--repair` explicitly
discards working-tree edits on managed patch paths, while preserving unrelated
tracked edits and the real index. Use it only after preserving work you need.
Bootstrap's separate `--force` can discard a dependency's local work, but
refuses unreviewed dirty submodules; inspect and resolve each child first.

If CMake reports an old `DKRPORT_ROOT` override, migrate to
`DKR_DEPENDENCIES_DIR` following the [worktree guide](DEVELOPMENT.md#worktrees-and-incremental-builds).
A configure failure lists missing dependencies together. The platform setup
scripts check compiler/SDK prerequisites; the ROM-free checks do not require
full runtime SDKs.
