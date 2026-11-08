"""Authored v1.0.9 documentation replacements; consumed only by pipeline.py."""
from pathlib import Path

AUTH = Path(__file__).resolve().parent


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Documentation input anchor changed: ' + old[:120])
    return text.replace(old, new, 1)


def section(text, start, end, replacement):
    a, b = text.index(start), text.index(end)
    return text[:a] + replacement + text[b:]


def build(root, baseline=None):
    result = {}

    def read(name):
        return baseline.read(name).decode('utf-8').replace('\r\n', '\n') if baseline is not None else (root / name).read_text(encoding='utf-8')

    for name in ('README.md', 'ONLINE_MULTIPLAYER.md', 'PUBLIC_LOBBIES.md', 'SPECTATORS.md',
                 'VEHICLES.md', 'ACHIEVEMENTS.md', 'MOD_LIBRARY_ADMIN.md', 'RELEASE-VALIDATION.md'):
        target = name if name in ('README.md', 'RELEASE-VALIDATION.md') else 'docs/' + name
        result[target] = (AUTH / 'pages' / name).read_text(encoding='utf-8')

    # Preserve earlier validation verbatim: changing an old result to the current
    # version would erase evidence. Its original relative paths remain historical.
    result['docs/history/RELEASE-VALIDATION-1.0.8.md'] = read('RELEASE-VALIDATION.md')

    notes = '''DKR-R v1.0.9 Release Build — 24 October 2026

PUBLIC LOBBIES
Sign in with Discord from DKR-R Online, or reuse the Mod Store sign-in.
Browse Public/Friends, see the connected-account total and choose Private or
Public when hosting. Private is the default and stays off the public list.
Host approval, capacity, compatible content and code consent still apply.
The owner-deployed website API handles discovery/presence/admission; existing
peer transport and Rollback run the match. FTP mod uploads remain unchanged.

ONLINE AND SPECTATORS
The public host mode is Rollback; the old backend selector is removed.
Spectator-capable rooms have up to four racers and eight spectators, cameras
and host-approved player swaps. Adventure still has two racing places.
Running-spectator discovery is separately gated by the API service setting.
The shared Rollback connection Scoreboard estimates timing and reliability
without automatically changing settings. Not rated does not mean zero.
Retains party start, checkpoint-clock, departure, return and lobby-layout fixes.
Approved long mod transfers survive benign roster/account refreshes.

CARS, ACHIEVEMENTS AND TEXTURES
Import .dkrkart cars directly, from ZIPs or the Mod Store Vehicles category.
Choose enabled cars in Track Select; Vanilla stays available. These change
visuals, not physics/handling. Adventure, custom hovercrafts and planes are
outside custom-car support. Vehicles retains backend category ID legacy.
94 local achievements need no account; watching/development do not award
gameplay goals. Recognition of provable existing progress is explicit.
Texture Dumping exports installed content, captures encountered textures,
browses workspaces and creates replacement starter packs. Keep original-art
dumps private and distribute only artwork you have permission to share.

RETAINED FEATURES AND FIXES
SDK 2.0 remains additive to API 1. Mod Store installation/update/repair,
Custom Trophy Races, optional Camera++, personal/online/development save
isolation, presentation fixes and the 1.0.8 Windows startup memory fix remain.
Upstream track-ID/path safety, failure-safe track import, saved graphics preset
display and reviewed TLS support are retained in the merged source.
No save reset, forced API conversion or mod reinstall is required.

PACKAGES AND UPDATES
Windows x64 ZIP; Linux x86_64 and ARM64 AppImages; Steam Deck (the same x86_64
payload); Android ARM64 APK, versionCode 1090001 and the existing signer/app ID.
Linux requires GLIBC 2.38, GLIBCXX_3.4.32 and a compatible Vulkan driver.
Published 1.0.8 macOS/iOS assets exist; no new Apple package is in this build set.
Use matching complete 1.0.9 packages on all peers. Keep assets/libexec together.
Automatic installation requires a fresh DKR-R-update.json and DKR-R-update.sig
signed with the existing trusted release key. An old signature cannot be reused.

Compilation/linking and static source/binary/package checks are recorded in
RELEASE-VALIDATION.md. Owner acceptance of the preceding Windows lobby pilot
is not blanket hardware/network qualification. No ROM, saves, credentials,
installed mod library or private diagnostics are supplied with the game.
'''
    result['docs/PUBLIC-1.0.9-NOTES.txt'] = notes

    changelog = '''## 1.0.9 — 24 October 2026

- Add worldwide Public/Friends lobby browsing, distinct signed-in account counts,
  shared Discord sign-in and Private/Public hosting to the existing launcher.
- Keep private rooms out of public lists; preserve host approval, friend/code
  admission, role/capacity checks and verified mod/save preparation.
- Add the private website/API directory for discovery and admission. Existing
  peer transport/Rollback runs gameplay; FTP mod uploading is unchanged.
- Retain up to four racers/eight spectators, spectator cameras and confirmed
  host-approved swaps. Running-spectator directory entry is separately gated.
- Offer Rollback as the public host mode, removing the old backend selector.
- Add the shared connection Scoreboard with separate racer/spectator grades.
- Retain countdown/start, checkpoint-clock, party departure and lobby-return
  repairs; preserve approved preparation across benign account/roster refreshes.
- Add custom .dkrkart cars, native Track Select picking, CPU choices, reviewed
  seating/wheel/LOD handling and Store Vehicles installation/update/repair.
  Hover/plane exports remain reserved; custom cars do not change handling.
- Add 94 local achievements, explicit existing-progress recognition, pinned
  goals and safe progress import/export, without account synchronisation.
- Add installed-content texture export, live capture, dump browsing and starter
  replacement packs, preserving edited Working files on re-export.
- Retain SDK 2/API 1 compatibility, shared personal saves, isolated online/dev
  progress, Camera++, previous presentation fixes and the Windows memory fix.
- Retain merged upstream track-ID safety/failure-safe imports, saved graphics
  preset display and reviewed TLS integration.
- Preserve website batch entry, image clear/reset, five automatic upload retries,
  FTP file usage filters and the Vehicles label with stored category ID `legacy`.
- Rebuild Windows x64, both Linux AppImages, Steam Deck and Android ARM64. APK
  versionCode is 1090001 with the original signing identity. Linux ABI requirements
  are GLIBC 2.38 and GLIBCXX_3.4.32. New Apple packages were not built in this pass.
- Refresh current guides and release/signing instructions; preserve dated records
  and separate static build verification from physical-device acceptance.

'''
    result['CHANGELOG.md'] = once(read('CHANGELOG.md'), '## 1.0.8 — 20 October 2026', changelog + '## 1.0.8 — 20 October 2026')

    text = read('docs/README.md')
    text = once(text, 'current v1.0.7 instructions', 'current v1.0.9 instructions')
    text = once(text, '| [DKR-R Online](ONLINE_MULTIPLAYER.md) | Quick Join/friends, backends, host mods, saves and connection limits. |', '''| [DKR-R Online](ONLINE_MULTIPLAYER.md) | Public/private hosting, Rollback, Scoreboard, session mods, saves and connection limits. |
| [Public lobbies](PUBLIC_LOBBIES.md) | Shared Discord sign-in, Public/Friends, account count and directory privacy. |
| [Spectators](SPECTATORS.md) | Watching, cameras, host-approved swaps and late-join gates. |
| [Vehicles](VEHICLES.md) | Custom .dkrkart cars, native selection and managed Store installations. |
| [Achievements](ACHIEVEMENTS.md) | 94 local goals, recognition, notifications and progress protection. |
| [Library administration](MOD_LIBRARY_ADMIN.md) | FTP uploads, batch entries, image controls, usage filters and deployment. |''')
    text = once(text, 'Imports, priorities, CRT filters and track-owned HD packs.', 'Imports, priorities, CRT filters, track-owned HD packs and texture export/capture.')
    text = once(text, 'Desktop delivery versus Android/ARM previews and macOS source support.', 'v1.0.9 packages, Linux ABI, Android identity and separate Apple release coverage.')
    text = once(text, '- [Changelog](../CHANGELOG.md), [beta 5 notes]', '- [v1.0.9 release notes](PUBLIC-1.0.9-NOTES.txt), [v1.0.8 notes](PUBLIC-1.0.8-NOTES.txt) and\n  [documentation audit](DOCUMENTATION-1.0.9.md).\n- [Changelog](../CHANGELOG.md), [beta 5 notes]')
    text = section(text, 'The 1.0.7 release targets', '## Current code-mod API', '''The v1.0.9 build set contains Windows x64, Linux x86_64/ARM64 AppImages,
Steam Deck and Android ARM64. Published v1.0.8 macOS/iOS packages exist; new
Apple packages were not built in this pass. See the platform guide and validation
record for exact coverage. Hardware ray tracing and unqualified online graphics
experiments must not be inferred from a successful desktop compile.

''')
    result['docs/README.md'] = text

    text = read('docs/PLATFORMS.md')
    text = section(text, "We're keeping Windows", 'Both clean US', '''The v1.0.9 Release Build set was prepared on 24 October 2026. Windows and Linux
ship together; both Linux architectures have AppImages. Keep the complete
package, including its private helpers and required notices.

| Platform | v1.0.9 delivery | Requirements and coverage |
|---|---|---|
| Windows x64 | `DKR-R-1.0.9-Windows-x64.zip` | Windows 10+, compatible Vulkan driver; extract the entire ZIP. |
| Linux x86_64 | `DKR-R-1.0.9-Linux-x86_64.AppImage` | GLIBC 2.38, GLIBCXX_3.4.32 and a compatible Vulkan driver. |
| Linux ARM64 | `DKR-R-1.0.9-Linux-aarch64.AppImage` | 64-bit Linux/Vulkan, the same ABI requirements; physical GPU acceptance is separate. |
| Steam Deck | `DKR-R-1.0.9-SteamDeck-x86_64.AppImage` | Exact Linux x86_64 payload; SteamOS/controller/gyro acceptance needs the device. |
| Android ARM64 | `DKR-R-1.0.9-Android-arm64.apk` | Android 9+ (API 28), ARM64 OS and compatible Vulkan; versionCode 1090001, non-debuggable Release. |
| macOS | No new package in this build set | Published v1.0.8 includes a notarised ARM64 ZIP. A v1.0.9 package needs an Apple host and current feature qualification. |
| iOS | No new package in this build set | Published v1.0.8 includes an ARM64 IPA. Sideload source targets iOS 18+; v1.0.9 device packaging/qualification remains separate. |

The previous Apple assets are not v1.0.9 binaries and must not be renamed or
included in its update manifest. Source compile-check workflows do not produce
qualified release packages. Current public Rollback integration remains gated
out on Apple; a source port alone does not establish parity with this release.

Android retains application ID `io.github.thatguymcd.dkrr.preview` and the
distributed signing certificate. The historical `.preview` ID is retained for
upgrade continuity; do not change it just to make the package name look stable.
Static checks verify the original signer, APK layout and at least 16 KiB native
load alignment. They do not establish device performance or system-installer
acceptance. See [release validation](../RELEASE-VALIDATION.md).

''')
    text = once(text, "Android's integrated rollback port", "Android's integrated Rollback port")
    text = once(text, 'an active experimental match', 'an active Rollback match')
    result['docs/PLATFORMS.md'] = text

    text = read('docs/LAUNCHER.md')
    text = once(text, '# Launcher and overlay', '# Launcher and overlay — v1.0.9')
    text = section(text, '**DKR-R Online** contains', '**Graphics**', '''**DKR-R Online** keeps Host / Quick Join, friends/profile and host settings.
The existing Open lobbies panel now offers **Public / Friends**, the header
shows the distinct connected-account total, and the profile reuses **Sign in
with Discord**. Hosting adds **Private / Public**, with Private the default.
The lobby retains approval, Ready, start, spectator/role and leave controls.
**Check Rollback connection** opens the shared Scoreboard. The host mode is
Rollback; the old backend selector is removed. Hide the lobby code when streaming.
See [Online](ONLINE_MULTIPLAYER.md), [Public lobbies](PUBLIC_LOBBIES.md) and
[Spectators](SPECTATORS.md).

**Mods / Hacks** has tracks, characters, Vehicles, Custom Magic Codes, Mod Store,
Track Lab and import/help tools. Cars can be imported as `.dkrkart` or ZIPs and
chosen in the native Track Select picker. Cards stay compact; Details contains
longer help and settings. See [Mods](LEGACY-MODS-BETA.md), [Vehicles](VEHICLES.md)
and [Magic Codes](MAGIC_CODES.md).

''')
    text = once(text, '**Textures** imports/enables packs and sets priorities. **About DKR-R** contains', '''**Textures** imports/enables packs and sets priorities. Its collapsible
**Texture Dumping** section has Export, Capture and My Dumps tabs; switching
tabs does not cancel an active job. See [Textures](TEXTURE_PACKS.md).

**Achievements**, below Textures, shows 94 local goals, filters, pinned goals,
notification settings and progress import/export. It needs no account.
See [Achievements](ACHIEVEMENTS.md).

**About DKR-R** contains''')
    result['docs/LAUNCHER.md'] = text

    text = read('docs/UPDATES.md')
    text = section(text, '## v1.0.7 release contract', '## Launcher options', '''## v1.0.9 release contract

The updater expects the exact filenames **DKR-R-update.json** and
**DKR-R-update.sig** beside the platform downloads. The manifest must name
the new package bytes and a committed source tree; the signature must be
created again with the existing trusted Ed25519 private key. A v1.0.8 signature
cannot authenticate an edited manifest, and its public key cannot sign one.

v1.0.9 is stable. The established release tag convention is `Version1.0.9`;
the tag used for publication must exactly match the signed manifest. The
signing operation does not create that tag or publish a release.

The launcher checks GitHub after appearing and can offer a newer compatible
release. Choose Download update, then Install update and restart after playing.
Nothing is downloaded or installed without your choice. v1.0.6 needs one manual
upgrade to an updater-enabled build. The old `1.0.7+beta` comparison issue only
affects upgrading those builds to equal-version 1.0.7; it is not a reason to
disable signatures or require every newer release to install manually.

''')
    old = 'python scripts/update_release.py sign --version <version> --tag <tag> --commit <40-character-commit> --channel stable --private-key <protected-key.pem> --output <fresh-output-folder> --artifact windows-x64=<package.zip> --artifact linux-x86_64=<package.AppImage>'
    text = once(text, old, 'python patches/release-prep/v1.0.9/update_assets.py sign --packages <release-folder> --commit <40-character-release-source-commit> --private-key <protected-key.pem> --output <fresh-signed-output-folder>')
    text = once(text, 'Add `linux-aarch64`,', '''The v1.0.9 wrapper calls the existing `scripts/update_release.py` signer,
checks all five finished package hashes, and signs four entries: Windows x64,
Linux x86_64, Linux ARM64 and Android ARM64. Steam Deck uses the Linux x86_64
entry. The detached `.sig` contains 128 hexadecimal characters representing
the 64-byte Ed25519 signature. Its exact manifest bytes, including the final
newline, must remain unchanged after signing. The wrapper verifies the result
against the pinned public key without launching the updater or game.

`prepare` writes `DKR-R-update.UNSIGNED.json` for review; it is deliberately
not named as an installable update and has no fabricated `.sig`. Supply the
original private key to `sign` to produce the real pair. `verify` checks an
existing pair and every package again. See the
[release signing handoff](../patches/release-prep/v1.0.9/UPDATER.txt).

Add `linux-aarch64`,''')
    text = once(text, 'only when their packages are qualified.', 'only when their packages are qualified. Do not reuse v1.0.8 Apple packages in the v1.0.9 manifest.')
    text = once(text, 'For Android, retain the actual', 'For v1.0.9 Android, versionCode is `1090001`. Retain the actual')
    result['docs/UPDATES.md'] = text

    text = read('docs/BUILDING.md')
    text = section(text, '# Building DKR-R v1.0.7', 'For contributor checks', '''# Building DKR-R v1.0.9

Use the [v1.0.9 release Patch Pipeline](../patches/release-prep/v1.0.9/BUILDING.txt)
and its `ReleaseOverlay.cmake`. It composes the accepted Online directory,
Scoreboard, party repairs, spectators, vehicles, achievements and texture tools,
plus the previous SDK/Store/save/presentation layers. Building the base target
alone omits those additions. `VERSION` and the compiled release value are `1.0.9`.

The recipe requires the verified frozen 1.0.7 source capsule and existing normal,
owned, camera, shadow, visual-model and RSP inputs. These are read-only build
inputs, not files to edit or rebaseline to pass a check. A fresh contributor
checkout needs legitimate prepared ROM inputs before it can reproduce them.
Windows/Linux/ARM64/Android must share the accepted source ledger. Never relabel
an earlier binary as a new release.

The v1.0.9 build set and exact static checks are in
[release validation](../RELEASE-VALIDATION.md). Source build commands for Apple
remain below; no new Apple package was built in this pass.

''')
    text = once(text, 'The [platform guide](PLATFORMS.md) separates public packages from preview ports.', 'The [platform guide](PLATFORMS.md) lists package versions, ABI requirements and actual qualification.')
    text = once(text, '## Configure a complete desktop build', '## Prepared visual inputs and release configuration')
    text = once(text, "Beta 5's local Online maximum-detail renderer", 'The retained visual-model/vehicle renderer')
    text = section(text, 'Example Windows configuration,', 'The Linux release toolchain', '''For an already prepared platform cache, configure through the current overlay:

```text
python -B patches/release-prep/v1.0.9/release.py configure --build <platform-build> --capsule <verified-capsule> --log <fresh-evidence>/configure.log
cmake --build <platform-build> --config RelWithDebInfo --target DKRPortGame --parallel
```

The displayed configuration reuses the cache's reviewed compiler/toolchain,
normal/owned/RSP inputs and platform dependencies. It is not a universal
fresh-machine bootstrap command. Select the correct Windows/Linux/ARM64/Android
cache, never reuse compiled objects across architectures, and retain both visual
model inputs. Older ARM/Android caches can supply them with
`--define DKR_VISUAL_MODEL_GENERATED_V77:PATH=<model-v77>` and its V80 equivalent.
Use the actual platform's build configuration (Ninja uses its configured build
type); build the required desktop helpers before packaging.

Use `patches/release-prep/v1.0.9/audit.py` and `package.py` for the release source
graph and allowlisted package composition. Their `--help` describes explicit
paths. They retain earlier contract audits and read-only frozen inputs.
Keep evidence and new outputs outside the repository. Linux packaging must
produce both AppImages; Steam Deck copies the exact x86_64 payload.

Release runtime/gameplay acceptance is owner-led. Do not launch a game, mod,
test executable or AppImage while preparing these packages. The native build
and static audits are distinct from acceptance on physical hardware.
The general Linux packager's no-execution settings are both
`DKR_SKIP_RUNTIME_TESTS=1` and `DKR_DEFER_RUNTIME_TESTS_TO_USER=1`; Windows uses
`-SkipRuntimeTests -DeferRuntimeTestsToUser`. They defer execution without
disabling package ownership, content or architecture checks.

''')
    text = once(text, 'The integrated experimental backend is gated out on Apple; use a supported\nlegacy backend rather than expecting Auto to provide an unavailable engine.', 'The current integrated Rollback backend remains gated out on Apple. The public\nRollback-only v1.0.9 layer does not promise a fallback to a removed backend.\nExisting v1.0.8 Apple releases have their own feature/build scope; validate\ncurrent Apple integration before producing a v1.0.9 package.')
    text = once(text, 'For 1.0.7, compose', 'For 1.0.9, compose')
    text = section(text, 'Package Android with', 'For ARM64 AppImages,', '''For Android, use the current release `android-recipe` action and the external
adapter stage from the overlay before packaging. It supplies the exact frozen
account include, forwards the directory instance header only to the trusted
API, adapts atomic shared-pointer snapshots for Android libc++ and retains the
Android texture timestamp formatter. Do not edit the checked-out sources.

The generated lean recipe wraps `scripts/Package-Android-Preview.ps1
-ReleaseBuild` with fresh Gradle/output paths, versionName `1.0.9`, versionCode
`1090001` and the original distributed signing identity. The output is
`DKR-R-1.0.9-Android-arm64.apk`, non-debuggable. The retained application ID
contains `.preview` for upgrade compatibility. `release.py inspect-apk` checks
signer continuity, archive metadata and all native PT_LOAD alignments (16 KiB
minimum). An APK compile is not a device/system-installer acceptance test.

''')
    text = once(text, 'AppImage, not ARM64. Beta 3 also has an extracted portable tarball with the\nsame x86_64 application/libraries and `START-DKR-R.sh`; it is not another engine.', 'AppImage, not ARM64. Earlier extracted portable tarballs are historical and\nare not part of this v1.0.9 release set.')
    text = once(text, '## v1.0.8 startup hotfix', '## Retained v1.0.8 startup hotfix')
    text = once(text, 'Use [the startup hotfix pipeline]', 'The current release includes [the startup hotfix pipeline]')
    text = once(text, 'It composes the same frozen 1.0.7 inputs, adds only the isolated runtime-memory\npatch and compiles the actual 1.0.8 version. Android version code is 1080001.', 'That earlier layer used the frozen 1.0.7 inputs and an isolated runtime-memory\npatch for 1.0.8 (Android code 1080001). The current outer release layer supplies\n1.0.9 and code 1090001 while retaining the startup correction.')
    result['docs/BUILDING.md'] = text

    text = read('docs/ARCHITECTURE.md')
    text = once(text, '## v1.0.7 release contract', '## v1.0.9 release contract')
    text = once(text, 'The release composes Camera++, SDK 2, Store, shared saves and presentation fixes through the Patch Pipeline.', 'The release composes the Online directory, Scoreboard, party/spectator repairs, vehicles, achievements and texture tools over Camera++, SDK 2, Store, shared saves and presentation fixes through the Patch Pipeline.')
    text = once(text, 'but online and legacy mod-set save namespaces remain separate from offline\nprogress.', 'but online and development save owners remain separate from personal\nprogress. Old mod-set saves are recovery inputs, not current per-mod-list ownership.')
    text = section(text, '## Three online backends', 'The owned revision registry', '''## Public Rollback and party ownership

v1.0.9 public hosting admits the owned Rollback engine (the existing internal
`ExperimentalRollback` value). The old Auto/Host Prediction/Strict Input Sync
selector is removed. Historical implementations/wire values are not renamed
or offered as an automatic fallback.

Isolated full-scene owned CPU closures for both ROM revisions capture guest
state plus audited native sidecars, predict bounded remote input and restore/
replay on correction. Scene construction, start/return, lifetime cuts and
finish transitions retain explicit ownership/confirmation gates. The directory
never advances gameplay or replaces checkpoint/save admission.

Racer and spectator identities are distinct from four controller ports.
Spectators consume confirmed viewing checkpoints; approved racer swaps use an
agreed handover boundary. Required racer departure safely returns the party;
spectator departure does not remove a racing input owner.

''')
    text = once(text, 'PeerJS introduces Quick Join peers;', '''The authenticated website API supplies a private worldwide SQLite Durable
Object directory. It leases/deduplicates account presence, publishes public
room summaries and serializes bounded admissions/reservations. Account/role/
epoch/policy/handshake-key binding prevents public browsing from granting
native membership. Reconnection can expire unused capabilities while preserving
already approved content preparation. Existing native peers survive directory
outages independently. See [public lobbies](PUBLIC_LOBBIES.md).

PeerJS introduces Quick Join peers;''')
    result['docs/ARCHITECTURE.md'] = text

    text = read('runtime-recomp/README.md')
    text = section(text, '## v1.0.7 release contract', 'Both clean US revisions', '''## v1.0.9 release contract

This is the project-owned native integration layer. The complete release uses
the [v1.0.9 overlay](../patches/release-prep/v1.0.9/BUILDING.txt), not this base
target alone: directory, Scoreboard, party/spectator repairs, vehicles,
achievements, texture tools and the preceding SDK/Store/save/presentation layers.
SDK 2 is additive to API 1. Do not edit dependencies or generated guest functions.

It combines generated DKR CPU/Rare RSP output, N64ModernRuntime, the F3DDKR/RT64
bridge and the shared launcher/overlay.

''')
    text = section(text, 'Host Prediction and Strict Input Sync', 'Normal generated output alone', '''The public online mode is Rollback, using the existing internally named
Experimental Rollback owner and isolated full-scene CPU closures for v77/v80.
It restores/replays checkpoints while rendering, confirmed audio and persistence
remain external consumers. Old host-mode enum values are retained privately;
the removed selector does not provide a supported fallback.

''')
    text = once(text, 'Protocol 50 carries', 'The native compatibility protocol carries')
    text = once(text, 'Online and legacy mod sets hold\nseparate save owners.', 'Online and development sessions hold separate save owners; ordinary mods\nshare personal Adventure and older mod-set saves remain recovery copies.')
    result['runtime-recomp/README.md'] = text

    for name in ('docs/DEVELOPMENT.md', 'docs/F3DDKR.md', 'docs/HUD-WORKSHOP.md',
                 'docs/RENDER_SNAPSHOT_ARCHITECTURE.md', 'docs/CUSTOM_TRACKS.md',
                 'docs/TEXTURE_PACKS.md', 'docs/TROUBLESHOOTING.md', 'scripts/README.md'):
        text = once(read(name), '## v1.0.7 release contract', '## v1.0.9 release contract')
        if name == 'docs/DEVELOPMENT.md':
            text = once(text, 'patches/release-prep/v1.0.7/BUILDING.txt', 'patches/release-prep/v1.0.9/BUILDING.txt')
            text = once(text, 'Keep the changes focused and reproducible.', 'All edits, including documentation, go through authored Patch Pipeline layers.\nThe v1.0.9 documentation overlay records a recoverable baseline and allowed\ndocument changes; it does not alter native compile inputs.\n\nKeep the changes focused and reproducible.')
        elif name == 'docs/CUSTOM_TRACKS.md':
            text = once(text, 'Custom tracks are playable in DKR-R 1.0.5.', 'Custom tracks are playable in DKR-R 1.0.9; the existing `.dkrmap` format is retained.')
        elif name == 'scripts/README.md':
            text = once(text, 'patches/release-prep/v1.0.7/release.py and BUILDING.txt', 'patches/release-prep/v1.0.9/release.py and BUILDING.txt')
            text = once(text, 'For 1.0.7, rebuild', 'For 1.0.9, rebuild')
            text = once(text, '| Validate release metadata |', '| Prepare/verify/sign v1.0.9 updates | `python patches/release-prep/v1.0.9/update_assets.py --help` | Finished packages, committed source, original Ed25519 private key for signing only |\n| Apply/audit release docs | `python patches/release-prep/v1.0.9/documentation/pipeline.py --help` | Recoverable external baseline; current guide overlay |\n| Validate release metadata |')
        elif name == 'docs/TEXTURE_PACKS.md':
            # The current guide is a concise user workflow, not a renamed
            # historical playtest checklist or a promise of gameplay acceptance.
            text += '''
## Texture Dumping — v1.0.9

Expand **Texture Dumping** beneath pack controls. Its **Export**, **Capture**
and **My Dumps** tabs share active jobs; switching tabs/closing the section
does not cancel an export or capture.

Choose your original ROM first. **Export textures** can include original stored
textures, installed/disabled tracks, characters and custom cars. Exporting does
not enable a mod or run code. **Include source artwork** retains exact available
embedded/companion PNGs; missing high-resolution art is not reconstructed.
Skipped/unsupported items appear in `dump.json` rather than being marked complete.

**Capture next game** records eligible original decoded textures encountered
while you play, even with an HD pack enabled. It does not capture screenshots,
CRT output, depth buffers or texture variants never encountered. Export filters
do not filter live capture. Stop capture in the overlay and let queued writes
finish; resume/revisit missed textures if the writer paused.

The writable `texture-dumps` workspace has **Originals**, editable **Working**,
optional **Source_Artwork** and ownership/identity reports in `dump.json`.
Re-export preserves Working edits. My Dumps can reopen an existing workspace,
search/page previews and refresh changed files. Browsing does not modify PNGs.

Edit Working, then choose **Create starter pack** and import `Texture-Starter.zip`
through the normal texture-pack importer. Rebuild/re-import after further edits.
Only supported known/observed identities are included. Shared hashes replace
all matching uses; a character folder does not make a shared hash exclusive.
Water replacements still need the existing complete-family rules.

Keep original-art dumps private. The starter omits ROMs and source reports,
but unedited Working images can still be original game art. Distribute only
replacement artwork you have permission to share. The detailed original
[texture-tool implementation/checklist](../patches/texture-dumping/TEXTURE-DUMPING.txt)
is retained as engineering history, not v1.0.9 hardware qualification.
'''
        elif name == 'docs/TROUBLESHOOTING.md':
            text = once(text, 'Auto selects Experimental Rollback; it is not fallback to Host Prediction.', 'v1.0.9 uses Rollback; the old host-backend selector is removed.')
            text = once(text, 'In 1.0.7, a compatibility failure', 'A compatibility failure')
            text = once(text, '## Graphics or uneven presentation', '''## Public list, sign-in or online count

Public discovery needs a Discord account and Online connection. Reuse the
Mod Store sign-in or finish browser pairing from Online. Confirm the Public
filter, host visibility and service status. A private/locked/full or starting
room does not grant a new racing place. An old private lobby made while the
service was unavailable needs recreation before public publication.

The total counts accounts, not instances/controllers. A sleeping/crashed client
can linger up to 60 seconds. Appear offline is a friend-presence control;
Disconnect Online removes this instance from aggregate presence. Linux without
Secret Service can use session-only sign-in. Do not disable certificate checks
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

## Graphics or uneven presentation''')
        result[name] = text

    result['docs/MAGIC_CODES.md'] = once(read('docs/MAGIC_CODES.md'), 'require Auto / Experimental Rollback', 'require the v1.0.9 Rollback host mode')
    result['docs/SOUND.md'] = once(read('docs/SOUND.md'), 'Master volume remains local and live in Experimental Rollback. Individual\ncategory mixing still uses the authored mix there; your saved category\npreferences are retained for offline and legacy modes.', 'Master volume remains local and live in Rollback. Individual category mixing\nstill uses the authored mix there; saved category preferences remain available\noffline. The old online backend selector is no longer offered in v1.0.9.')
    result['docs/SAVES.md'] = read('docs/SAVES.md') + '''
## Spectators and achievements — v1.0.9

Watching uses session-owned state, not your personal Adventure or Paks. The
host owns communal online progress; confirmed handovers do not transplant a
save into another participant's personal profile. Development remains isolated.

Achievements have separate `achievements/progress-v1.json` storage. Max Out
does not itself award gameplay goals. Recognising provable existing milestones
is an explicit achievement setting. Spectating and development do not award
gameplay progress. See [Achievements](ACHIEVEMENTS.md) and [Spectators](SPECTATORS.md).
'''
    result['docs/GRAPHICS.md'] = read('docs/GRAPHICS.md') + '''
## v1.0.9 spectator and custom-car presentation

Spectators choose All splits, Follow player or the T.T.-style director without
changing racing input. Custom GPU-rendered cars use safe racer cameras in the
director; original vehicles can use its cinematic view. Custom cars replace
visuals, not physics, and their online choices remain admitted session state.
The saved graphics preset display and previous water/camera fixes are retained.
See [Spectators](SPECTATORS.md) and [Vehicles](VEHICLES.md).
'''

    text = read('docs/LEGACY-MODS-BETA.md')
    text = once(text, '# Custom tracks, characters and the Mod Store', '# Custom tracks, characters, vehicles and the Mod Store — v1.0.9')
    text = once(text, 'manage tracks, characters and Custom Magic Codes.', 'manage tracks, characters, Vehicles and Custom Magic Codes.')
    text = once(text, '## Track and character imports', '''## Vehicles

Import supported `.dkrkart` cars directly or from ZIPs, or install from the
Store's Vehicles category. Managed cars use the same receipts/Update/Repair
workflow. Enable them before Track Select; Vanilla remains available. Custom
cars change visuals, not handling, and Adventure does not use them. Hover/plane
formats are reserved. The stored category ID remains `legacy` for compatibility.
See [Vehicles](VEHICLES.md).

## Track and character imports''')
    result['docs/LEGACY-MODS-BETA.md'] = text

    result['packaging/MACOS-BUILD-README.md'] = '''# macOS builds and release coverage

Published v1.0.8 includes a notarised macOS ARM64 ZIP and an iOS ARM64 IPA.
No new Apple package is part of the Windows/Linux/Android v1.0.9 build set.
Do not rename an earlier Apple asset to v1.0.9 or include it in that manifest.

Use [Building DKR-R](../docs/BUILDING.md#macos) for Xcode/Homebrew preparation,
source builds, ad-hoc packaging and Developer ID notarisation. The source kit
contains the dependency lock; first preparation fetches the pinned checkouts.
Preparation checks staged Python/native tools and repairs incomplete stages
without deleting protected source.

The current integrated Rollback backend remains gated out on Apple. The
v1.0.9 public Rollback-only layer does not promise a legacy-mode fallback.
Current feature integration, bundle/updater ownership, signatures/notarisation
and physical-device acceptance need qualification on an Apple host before a
new Apple release. Source compile checks are not release packages.
See [platform support](../docs/PLATFORMS.md) and [release validation](../RELEASE-VALIDATION.md).
'''
    text = read('packaging/RELEASE-README.md')
    text = once(text, 'Read RELEASE-NOTES.txt for the changes.', '''DKR-R Online adds Public/Friends browsing, shared Discord sign-in, connected
account counts and Private/Public hosting. Public discovery uses the deployed
website API; the existing peer/Rollback path runs the game. Spectators,
connection Scoreboard, custom cars, achievements and texture tools are included.
Running-spectator entry has a separate service gate. No new Apple package is
part of this build set; earlier Apple releases retain their own versions.

Read RELEASE-NOTES.txt for the changes.''')
    text = once(text, 'Use a manual upgrade from the old 1.0.7+beta builds.', 'Automatic updates need newly signed DKR-R-update.json/DKR-R-update.sig assets.\nDo not reuse an old signature or alter package bytes after signing.')
    result['packaging/RELEASE-README.md'] = text

    text = read('tools/blender/README.md')
    text = once(text, 'DKR-R 1.0.7\nincludes', 'DKR-R 1.0.9\nincludes')
    text = once(text, 'format conversion for 1.0.7.', 'format conversion for 1.0.9. Public lobby discovery does not change the track format.')
    result['tools/blender/README.md'] = text

    text = read('runtime-recomp/tests/legacy_mods/README.md')
    text = once(text, 'For v1.0.7 use the [release pipeline](../../../patches/release-prep/v1.0.7/BUILDING.txt).', 'For v1.0.9 use the [release pipeline](../../../patches/release-prep/v1.0.9/BUILDING.txt).')
    result['runtime-recomp/tests/legacy_mods/README.md'] = text

    for name in ('modding/USER-GUIDE.txt', 'modding/SDK.txt', 'modding/IMPLEMENTATION-STATUS.txt', 'modding/MODDERS-GUIDE.txt'):
        text = once(read(name), 'v1.0.7 release', 'v1.0.9 release — SDK 2.0 retained')
        if name.endswith('MODDERS-GUIDE.txt'):
            text = once(text, 'This book is for v1.0.7 with SDK 2.0.', 'This book is for v1.0.9 with the retained SDK 2.0/API 1 compatibility.')
            text = once(text, 'Use Auto/Experimental Rollback for code mods. Other backends do not acquire', 'Use the v1.0.9 Rollback host mode for code mods. Unsupported engines do not acquire')
        else:
            text = once(text, 'Use it with v1.0.7.', 'Use it with the current compatible v1.0.9 app.')
        text += '''
V1.0.9 ONLINE AND PROGRESS
The public host mode is Rollback; the old backend selector is removed.
Public discovery reuses Discord sign-in. Native content/code consent, host
authority, lifecycle/confirmed execution and development isolation still apply.
Spectators do not gain a racing port or authority to issue gameplay commands.
Watching/development do not award gameplay achievements. API 1/2 headers,
capability versions and package formats are unchanged by lobby discovery.
See docs/ONLINE_MULTIPLAYER.md, docs/SPECTATORS.md and docs/ACHIEVEMENTS.md.
'''
        result[name] = text

    result['docs/DOCUMENTATION-1.0.9.md'] = '''# v1.0.9 documentation audit

The release documentation update inventories first-party Markdown, TXT and
reStructuredText files in tracked and non-ignored source. It excludes dependency
checkouts, generated recompilation trees, build output and local Worker caches.
Frozen source-capsule TXT blobs are classified as source inputs, not guides.
Licences, notices and contributor attribution are preserved.

Current guides are checked for version/default/menu changes, removed backend
choices, public discovery, spectators, Scoreboard, cars, achievements, texture
tools, website FTP/batch administration, platform coverage and update signing.
Version-independent format/CI/input references remain current without an
artificial version bump. The Blender addon's own changelog/version and SDK API
numbers are independent of the application release.

Dated beta/playtest plans, old public release notes, investigations, archived
validation and patch-layer handoffs retain their original version, commands,
limits and test results. They are engineering history, not installation advice
for v1.0.9. The documentation index separates them from current guides.

All changes come from the
[documentation Patch Pipeline](../patches/release-prep/v1.0.9/documentation/pipeline.py).
It creates a recoverable pre-edit source ZIP/hash ledger, preflights every
target, records before/after hashes and checks that unrelated first-party files
and submodule revisions remain unchanged. Its external
`DOCUMENTATION-INVENTORY.json` records each document's disposition and version
mentions; `VERIFICATION.json` records counts and local-link checks. Current
release metadata/dependency patch checks are run separately. No native package
is rebuilt or altered for documentation-only updates.

The [v1.0.9 validation record](../RELEASE-VALIDATION.md) remains precise about
static checks and outstanding hardware acceptance. The v1.0.8 record is archived
verbatim; its signing status describes that earlier build pass. Signing the
current manifest has its own receipt and requires the original private key.
'''
    return result
