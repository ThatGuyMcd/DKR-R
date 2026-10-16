# DKR-R updates

Version 1.0.7 checks GitHub after the launcher has appeared. You'll get
a notice when a newer release is available. Choose Download update, then Install
update and restart when you're finished playing. Nothing is downloaded or
installed without you choosing it.

Published v1.0.6 does not contain the updater. Install an updater-enabled build
manually once; future compatible releases can then use it. The release must
include signed update metadata before an automatic installation can be offered.

## Launcher options

Play has a **Check for updates** button. About DKR-R has an Updates section
where you can check now, disable startup checks,
include beta/playtest releases, skip a version, or pause a download. Stable
releases are the default. A failed check doesn't stop the launcher or offline play.

Finish imports and leave your online session before updating. Download work is
cancelled when you launch the game; a partial download can be resumed when the
server supports it. Already verified packages are reused.

## Keeping your data

Updates replace application files, not your saves, ROMs, mods, texture packs,
controller settings or logs. Portable mode keeps `portable.txt` and
`dkr-runtime-data` where they are. Restart to apply a pending portable-mode change
before installing an update.

On Windows, start `DKR-R.exe`. It is the only executable at the top of the
release folder. The actual launcher/game, `DKR-R-Runtime.exe`, lives inside
`libexec/dkr-r` with its DLLs and helpers. Leave that folder intact.
`DKR-R.exe` checks for interrupted updates before loading the runtime.
`portable.txt`, portable data and assets still belong in the main release folder,
not inside `libexec/dkr-r`. Extract manual test upgrades into a fresh folder so
old test-build executables are not left beside the new launcher. If you use
portable mode, copy both `portable.txt` and your existing `dkr-runtime-data`
folder into the new main folder before launching. Don't copy just one EXE out
of a release ZIP.

Linux and Steam Deck replace the complete AppImage at its existing path. The
shortcut still points to the same file. Package-manager/developer installations
use manual updates instead. A read-only location can keep playing but cannot
accept an in-place update.

Desktop installers keep application backups. Windows transaction backups are
under `.dkr-update-backups`; AppImage/bundle backups sit beside the original
application. Download and handoff records are in your profile's `updates` folder.
If a new launcher cannot start, the installer can restore the old application.
Never delete the only backup while investigating a failed update. An interrupted
Windows transaction is checked on the next launch through `DKR-R.exe`.
The Updates section reports a failed/recovered previous installation on return.

For an interrupted AppImage/bundle swap, the copied helper in your profile's
`updates/handoff-…` folder can recover its exact recorded target with
`DKR-R-Updater --recover <that-folder/request.json>`. Close all copies first.
Recovery checks the signed manifest and backup identity; it does not search
other folders or delete user data.

Android opens its system installer. Allow DKR-R to install updates when Android
asks, then confirm the APK installation. The application ID, signing identity
and version code must be compatible. A signing-key mismatch is not fixed by
uninstalling: that would delete private app data. Cancel and contact the release
maintainer instead. Android controls installation/recovery; desktop-style
automatic downgrades are not offered.

iOS uses your existing distribution/sideloading route. macOS needs a complete,
qualified application bundle with the private updater helper. Builds without a
supported installer show the release page rather than attempting an unsafe swap.

## Publishing trusted updates

Every installable release needs `DKR-R-update.json` and `DKR-R-update.sig` beside
its qualified platform packages. The manifest names the exact packages, their
sizes/checksums, version, channel, source commit and OS requirements. Windows ZIPs
also carry a complete file table and `assets/updates/install.json` ownership receipt.

The app verifies the manifest's Ed25519 signature using the public key in
`packaging/update-public-key.json`, then verifies the downloaded package. GitHub
checksums alone aren't a substitute for the publisher signature. Old releases
without this metadata remain manual downloads.

Use `scripts/update_release.py` with OpenSSL to prepare/sign release assets:

```text
python scripts/update_release.py public-key --private-key <protected-key.pem>
python scripts/update_release.py sign --version <version> --tag <tag> --commit <40-character-commit> --channel stable --private-key <protected-key.pem> --output <fresh-output-folder> --artifact windows-x64=<package.zip> --artifact linux-x86_64=<package.AppImage>
```

Add `linux-aarch64`, `android-arm64`, `macos-arm64`, `macos-x86_64` or `ios-arm64`
only when their packages are qualified. Steam Deck uses the Linux x86_64 entry.
macOS declares its minimum OS in Info.plist, or with `--minimum-os`. Linux
packages currently require glibc 2.38; update the manifest requirements when the
toolchain changes.

Keep the private signing key outside the repository and package folders. Back it
up securely. Never send it in chat, commit it, or distribute it with a build.
The signing tool checks that it matches the pinned public key. A deliberate key
rotation needs an old-key-signed bridge release; `--verification-key` can select
that previous public key for the bridge. Don't simply replace the public key
and publish a release old clients cannot verify.

For Android, retain the actual distributed APK signing key or an authorised
Android signing lineage, and increment `versionCode` for every published APK.
A fresh CI runner's newly generated debug key is not upgrade-compatible just
because the application ID matches.

Prepare the GitHub release as a draft. Upload all qualified packages and both
signed metadata files, verify names/checksums/signature, then publish. Don't
replace published assets under the same version. Use a new version for fixes.

## Qualification

Run `DKRUpdateCore` and `DKRUpdateService` on Windows/Linux, and
`python -m unittest discover -s scripts/tests -p test_update_release.py`.
`scripts/test_update_package.py` exercises a complete signed Windows package in
an isolated temporary install with the real runtime's ROM-free save tests.
Pass `--extractor <built-DKRUpdateCoreTests.exe>` to exercise the production
signed ZIP extractor as well, including Unicode Windows staging paths.

Before publishing, check the launcher notice and install/restart on actual
hardware. Include interrupted transfers, a corrupt signature/package, locked
files, read-only folders, disk-space failures, multiple instances, portable
data, Unicode paths and interrupted-install recovery. Compile success does
not qualify the Android system installer or an Apple package.
